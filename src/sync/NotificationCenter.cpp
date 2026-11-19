#include "NotificationCenter.h"

#include "ServerList.h"
#include "TokenStore.h"

#include <utility>

namespace bld::sync {

struct NotificationCenter::Live {
    Source src;
    QString token;
    NotificationApi* api = nullptr;
    EventStream* stream = nullptr;
    QTimer* soon = nullptr;
    QSet<QString> seen;
    bool baselined = false;
    int generation = 0;
    bool settingsAsked = false;
};

NotificationCenter::NotificationCenter(QObject* parent) : QObject(parent) {}

NotificationCenter::~NotificationCenter() {
    for (Live* l : std::as_const(live_)) delete l;
}

QString NotificationCenter::keyOf(const QUrl& server) { return TokenStore::keyFor(server); }

void NotificationCenter::setTokenStore(std::shared_ptr<TokenStore> tokens) { tokens_ = std::move(tokens); }

void NotificationCenter::setLive(bool live) {
    streams_ = live;
    for (auto it = live_.cbegin(); it != live_.cend(); ++it) {
        Live* l = it.value();
        if (!l->stream) continue;
        if (live && it.key() != outer_) l->stream->start(l->src.url, l->token);
        else l->stream->stop();
    }
}

void NotificationCenter::setReconnectDelay(int firstMs, int maxMs) {
    firstDelayMs_ = firstMs;
    maxDelayMs_ = maxMs;
    for (Live* l : std::as_const(live_))
        if (l->stream) l->stream->setReconnectDelay(firstMs, maxMs);
}

void NotificationCenter::reload() {
    if (!tokens_) return;
    const ServerList list = ServerList::load();
    const int gen = ++reloadGeneration_;
    QStringList keys;
    for (const ServerEntry& e : list.servers()) keys << keyOf(e.url);
    order_ = keys;
    // Servers no longer in the list go.
    bool dropped = false;
    for (const QString& k : live_.keys())
        if (!keys.contains(k)) {
            drop(k);
            dropped = true;
        }
    if (dropped) {
        emit sourcesChanged();
        emit countsChanged();
        emit warningsChanged();
    }
    for (const ServerEntry& e : list.servers()) {
        const QUrl url = e.url;
        const QString label = e.label();
        tokens_->load(url, [this, alive = std::weak_ptr<int>(alive_), gen, url, label](const QString& token) {
            if (alive.expired() || gen != reloadGeneration_) return;
            if (token.isEmpty()) {
                if (live_.contains(keyOf(url))) {
                    drop(keyOf(url));
                    emit sourcesChanged();
                    emit countsChanged();
                    emit warningsChanged();
                }
                return;
            }
            add(url, label, token);
        });
    }
}

void NotificationCenter::setSignedIn(const QUrl& server, const QString& token) {
    if (!server.isValid()) return;
    const QString key = keyOf(server);
    if (token.isEmpty()) {
        if (!live_.contains(key)) return;
        drop(key);
        emit sourcesChanged();
        emit countsChanged();
        emit warningsChanged();
        return;
    }
    if (!order_.contains(key)) order_ << key;
    const ServerList list = ServerList::load();
    const ServerEntry* e = list.find(server);
    add(server, e ? e->label() : server.host(), token);
}

void NotificationCenter::add(const QUrl& url, const QString& label, const QString& token) {
    const QString key = keyOf(url);
    Live* l = live_.value(key);
    if (l && l->token == token) {
        l->src.label = label;
        return;
    }
    if (!l) {
        l = new Live;
        l->src.url = url;
        l->api = new NotificationApi(this);
        l->soon = new QTimer(this);
        l->soon->setSingleShot(true);
        l->soon->setInterval(300);
        connect(l->soon, &QTimer::timeout, this, [this, key] { ask(key); });
        l->stream = new EventStream(this);
        l->stream->setReconnectDelay(firstDelayMs_, maxDelayMs_);
        connect(l->stream, &EventStream::hint, this, [this, key](const QJsonObject& h) { onHint(key, h); });
        connect(l->stream, &EventStream::reconnected, this, [this, key] { ask(key); });
        // A refused stream stops trying; the counts are still asked for on
        // focus, and a new sign-in starts it again.
        connect(l->stream, &EventStream::refused, l->stream, &EventStream::stop);
        live_.insert(key, l);
    }
    l->src.label = label;
    l->token = token;
    l->api->setBase(url);
    l->api->setToken(token);
    l->stream->stop();
    if (streams_ && key != outer_) l->stream->start(url, token);
    emit sourcesChanged();
    ask(key);
}

void NotificationCenter::setOuterStream(const QUrl& server) {
    const QString key = server.isValid() ? keyOf(server) : QString();
    if (key == outer_) return;
    const QString was = std::exchange(outer_, key);
    if (Live* l = live_.value(was); l && streams_) l->stream->start(l->src.url, l->token);
    if (Live* l = live_.value(key)) l->stream->stop();
}

void NotificationCenter::hint(const QUrl& server, const QJsonObject& h) {
    if (server.isValid()) onHint(keyOf(server), h);
}

void NotificationCenter::drop(const QString& key) {
    Live* l = live_.take(key);
    if (!l) return;
    l->stream->stop();
    l->stream->deleteLater();
    l->soon->deleteLater();
    l->api->deleteLater();
    delete l;
}

NotificationCenter::Live* NotificationCenter::liveOf(const QUrl& server) const { return live_.value(keyOf(server)); }

void NotificationCenter::refresh(const QUrl& server) {
    if (server.isValid()) {
        if (live_.contains(keyOf(server))) ask(keyOf(server));
        return;
    }
    for (const QString& k : live_.keys()) ask(k);
}

void NotificationCenter::changedHere(const QUrl& server) {
    if (Live* l = liveOf(server)) {
        l->soon->stop();
        ask(keyOf(server));
    }
}

void NotificationCenter::onHint(const QString& key, const QJsonObject& hint) {
    Live* l = live_.value(key);
    if (!l) return;
    const QString kind = hint.value(QLatin1String("kind")).toString();
    if (kind != QLatin1String("notification")) return;
    if (hint.value(QLatin1String("action")).toString() == QLatin1String("update:settings")) askSettings(key);
    // A burst of hints asks once.
    l->soon->start();
}

void NotificationCenter::ask(const QString& key) {
    Live* l = live_.value(key);
    if (!l) return;
    const int gen = ++l->generation;
    const QUrl url = l->src.url;
    // The answers find their source by key; one dropped (or asked again) meanwhile is left alone.
    const auto current = [this, key, gen]() -> Live* {
        Live* now = live_.value(key);
        return now && now->generation == gen ? now : nullptr;
    };
    l->api->count(
        [this, current, key, url](const NotificationCounts& counts) {
            Live* l = current();
            if (!l) return;
            const bool wasKnown = l->src.support == Support::Yes;
            l->src.support = Support::Yes;
            const bool countsMoved = !(l->src.counts == counts);
            l->src.counts = counts;
            if (!wasKnown) {
                emit sourcesChanged();
                askSettings(key);
            }
            if (countsMoved || !wasKnown) emit countsChanged();
            emit changed(url);
            // What is new since last time (the first look only notes what is there).
            if (counts.unread > 0 || !l->baselined) {
                NotificationQuery q;
                q.unreadOnly = true;
                q.limit = 30;
                l->api->list(q, [this, current, url](const NotificationPage& page) {
                    Live* l = current();
                    if (!l) return;
                    QList<NotificationItem> fresh;
                    for (const NotificationItem& n : page.items) {
                        if (l->seen.contains(n.id)) continue;
                        l->seen.insert(n.id);
                        if (l->baselined && !n.read()) fresh << n;
                    }
                    l->baselined = true;
                    if (!fresh.isEmpty()) emit arrived(url, fresh);
                }, {});
            }
            if (counts.warnings > 0) {
                NotificationQuery q;
                q.types = { QStringLiteral("moderation.warning"), QStringLiteral("moderation.final_warning") };
                q.limit = 50;
                l->api->list(q, [this, current](const NotificationPage& page) {
                    Live* l = current();
                    if (!l) return;
                    QList<NotificationItem> open;
                    for (const NotificationItem& n : page.items)
                        if (n.needsAcknowledge()) open << n;
                    if (open.size() == l->src.warnings.size()) {
                        bool same = true;
                        for (qsizetype i = 0; i < open.size(); ++i) same = same && open[i].id == l->src.warnings[i].id;
                        if (same) return;
                    }
                    l->src.warnings = std::move(open);
                    emit warningsChanged();
                }, {});
            } else if (!l->src.warnings.isEmpty()) {
                l->src.warnings.clear();
                emit warningsChanged();
            }
        },
        [this, current](const ServerRefusal& r) {
            Live* l = current();
            if (!l) return;
            // An older server: no notifications there (its notices still show).
            if (isNoNotifications(r) && l->src.support != Support::No) {
                l->src.support = Support::No;
                l->src.counts = {};
                l->src.warnings.clear();
                emit sourcesChanged();
                emit countsChanged();
                emit warningsChanged();
            }
            // Offline or refused: what it last said stays.
        });
}

void NotificationCenter::askSettings(const QString& key) {
    Live* l = live_.value(key);
    if (!l) return;
    const QUrl url = l->src.url;
    l->api->settings(
        [this, key, url](const NotificationSettings& s) {
            Live* l = live_.value(key);
            if (!l) return;
            l->src.settings = s;
            emit settingsChanged(url);
        },
        [](const ServerRefusal&) {
            // A server before delivery settings: pop-ups follow the app's own switch only.
        });
    if (l->src.types.isEmpty())
        l->api->types(
            [this, key, url](const NotificationTypes& t) {
                Live* l = live_.value(key);
                if (!l) return;
                l->src.types = t;
                emit changed(url);
            },
            {});
}

void NotificationCenter::saveSettings(const QUrl& server, const QJsonObject& change, const std::function<void(const ServerRefusal*)>& done) {
    Live* l = liveOf(server);
    if (!l) {
        if (done) {
            ServerRefusal r;
            r.message = tr("You're not signed in to this server.");
            done(&r);
        }
        return;
    }
    const QString key = keyOf(server);
    l->api->saveSettings(
        change,
        [this, key, server, done](const NotificationSettings& s) {
            if (Live* l = live_.value(key)) l->src.settings = s;
            emit settingsChanged(server);
            if (done) done(nullptr);
        },
        [done](const ServerRefusal& r) {
            if (done) done(&r);
        });
}

QList<QUrl> NotificationCenter::servers() const {
    QList<QUrl> out;
    for (const QString& k : order_)
        if (const Live* l = live_.value(k)) out << l->src.url;
    return out;
}

QList<QUrl> NotificationCenter::supported() const {
    QList<QUrl> out;
    for (const QUrl& u : servers())
        if (support(u) == Support::Yes) out << u;
    return out;
}

const NotificationCenter::Source* NotificationCenter::source(const QUrl& server) const {
    const Live* l = liveOf(server);
    return l ? &l->src : nullptr;
}

NotificationCenter::Support NotificationCenter::support(const QUrl& server) const {
    const Live* l = liveOf(server);
    return l ? l->src.support : Support::Unknown;
}

NotificationApi* NotificationCenter::api(const QUrl& server) const {
    const Live* l = liveOf(server);
    return l ? l->api : nullptr;
}

int NotificationCenter::totalUnread() const {
    int n = 0;
    for (const Live* l : live_)
        if (l->src.support == Support::Yes) n += l->src.counts.unread;
    return n;
}

int NotificationCenter::totalWaiting() const {
    int n = 0;
    for (const Live* l : live_)
        if (l->src.support == Support::Yes) n += l->src.counts.waiting;
    return n;
}

}  // namespace bld::sync
