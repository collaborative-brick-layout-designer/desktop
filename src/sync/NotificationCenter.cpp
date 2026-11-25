#include "NotificationCenter.h"

#include "ServerList.h"
#include "TokenStore.h"

#include <QDateTime>

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
    // The first look is on its way: answers to later looks that come back first wait in
    // `early` until it does, so only what it didn't have counts as new.
    bool firstAsked = false;
    QList<NotificationItem> early;
    int generation = 0;
    bool settingsAsked = false;
    QHash<QString, Answer> answers;  // by the notification's id
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
    if (l->token != token) l->answers.clear();  // maybe someone else now
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

const NotificationCenter::Answer* NotificationCenter::answer(const QUrl& server, const QString& id) const {
    const Live* l = liveOf(server);
    const auto it = l ? l->answers.constFind(id) : QHash<QString, Answer>::const_iterator();
    return l && it != l->answers.cend() ? &*it : nullptr;
}

void NotificationCenter::settle(const QUrl& server, const QString& token, const QString& id, const std::function<void(Answer&)>& change) {
    Live* l = liveOf(server);
    if (!l || l->token != token) return;
    change(l->answers[id]);
    emit answerChanged(server, id);
}

void NotificationCenter::act(const QUrl& server, const NotificationItem& n, const QString& action) {
    Live* l = liveOf(server);
    if (!l || l->answers[n.id].sending) return;
    const QString token = l->token;
    settle(server, token, n.id, [](Answer& a) {
        a = Answer{};
        a.sending = true;
    });
    l->api->act(
        n.id, action,
        [this, server, token, n](const NotificationActResult& r) {
            settle(server, token, n.id, [&](Answer& a) {
                a = Answer{};
                a.already = r.already;
                a.layoutId = r.result.value(QLatin1String("layoutId")).toString();
                a.item = r.item;
                if (a.item) return;
                a.item = n;
                a.item->outcome = r.outcome;
                a.item->actions.clear();
                a.item->doneAt = QDateTime::currentMSecsSinceEpoch();
            });
            changedHere(server);
        },
        [this, server, token, n](const ServerRefusal& r) {
            settle(server, token, n.id, [&](Answer& a) {
                a = Answer{};
                a.error = r.message.isEmpty() ? describe(r) : r.message;
            });
        });
}

void NotificationCenter::acknowledge(const QUrl& server, const NotificationItem& n) {
    Live* l = liveOf(server);
    if (!l || l->answers[n.id].sending) return;
    const QString token = l->token;
    settle(server, token, n.id, [](Answer& a) {
        a = Answer{};
        a.sending = true;
    });
    l->api->acknowledge(
        n.id,
        [this, server, token, n] {
            settle(server, token, n.id, [&](Answer& a) {
                a = Answer{};
                a.item = n;
                a.item->actions.clear();
                a.item->outcome = QStringLiteral("acknowledged");
                a.item->doneAt = a.item->readAt = QDateTime::currentMSecsSinceEpoch();
            });
            changedHere(server);
        },
        [this, server, token, n](const ServerRefusal& r) {
            settle(server, token, n.id, [&](Answer& a) {
                a = Answer{};
                a.error = describe(r);
            });
        });
}

void NotificationCenter::setRead(const QUrl& server, const QString& id, bool read, std::function<void()> refused) {
    Live* l = liveOf(server);
    if (!l) return;
    const QString token = l->token;
    if (l->answers.contains(id)) settle(server, token, id, [](Answer& a) { a.readError.clear(); });
    l->api->setRead(id, read, [this, server] { changedHere(server); },
                    [this, server, token, id, refused = std::move(refused)](const ServerRefusal& r) {
                        if (refused) refused();
                        settle(server, token, id, [&](Answer& a) { a.readError = describe(r); });
                    });
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
    Live* self = live_.value(key);
    if (!self) return;
    const int gen = ++self->generation;
    const QUrl url = self->src.url;
    // The answers find their source by key; one dropped (or asked again) meanwhile is left alone.
    const auto current = [this, key, gen]() -> Live* {
        Live* now = live_.value(key);
        return now && now->generation == gen ? now : nullptr;
    };
    // The first look and what's new: any answer for this same sign-in counts, even one asked
    // again meanwhile. Dropping it would let the next look become the first, and whatever
    // arrived in between would never pop up.
    const QString token = self->token;
    const auto sameSignIn = [this, key, self, token]() -> Live* {
        Live* now = live_.value(key);
        return now == self && now->token == token ? now : nullptr;
    };
    self->api->count(
        [this, current, sameSignIn, key, url](const NotificationCounts& counts) {
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
                const bool isFirst = !l->baselined && !l->firstAsked;
                if (isFirst) l->firstAsked = true;
                l->api->list(q, [this, sameSignIn, url, isFirst](const NotificationPage& page) {
                    Live* here = sameSignIn();
                    if (!here) return;
                    if (!isFirst && !here->baselined) {
                        // Back before the first look: wait for it.
                        here->early << page.items;
                        return;
                    }
                    QList<NotificationItem> fresh;
                    if (isFirst) {
                        // The first look: what is there now is not new. Answers that came
                        // back before it are new where it didn't have them.
                        for (const NotificationItem& n : page.items) here->seen.insert(n.id);
                        here->baselined = true;
                        for (const NotificationItem& n : std::exchange(here->early, {})) {
                            if (here->seen.contains(n.id)) continue;
                            here->seen.insert(n.id);
                            if (!n.read()) fresh << n;
                        }
                    } else {
                        for (const NotificationItem& n : page.items) {
                            if (here->seen.contains(n.id)) continue;
                            here->seen.insert(n.id);
                            if (!n.read()) fresh << n;
                        }
                    }
                    if (!fresh.isEmpty()) emit arrived(url, fresh);
                }, [sameSignIn, isFirst](const ServerRefusal&) {
                    // The first look failed: the next one is the first, and what came back
                    // meanwhile was there already as far as anyone can tell.
                    Live* here = sameSignIn();
                    if (!here || !isFirst || here->baselined) return;
                    here->firstAsked = false;
                    for (const NotificationItem& n : std::exchange(here->early, {})) here->seen.insert(n.id);
                });
            }
            if (counts.warnings > 0) {
                NotificationQuery q;
                q.types = { QStringLiteral("moderation.warning"), QStringLiteral("moderation.final_warning") };
                q.limit = 50;
                l->api->list(q, [this, current](const NotificationPage& page) {
                    Live* here = current();
                    if (!here) return;
                    QList<NotificationItem> open;
                    for (const NotificationItem& n : page.items)
                        if (n.needsAcknowledge()) open << n;
                    if (open.size() == here->src.warnings.size()) {
                        bool same = true;
                        for (qsizetype i = 0; i < open.size(); ++i) same = same && open[i].id == here->src.warnings[i].id;
                        if (same) return;
                    }
                    here->src.warnings = std::move(open);
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
    Live* self = live_.value(key);
    if (!self) return;
    const QUrl url = self->src.url;
    self->api->settings(
        [this, key, url](const NotificationSettings& s) {
            Live* l = live_.value(key);
            if (!l) return;
            l->src.settings = s;
            emit settingsChanged(url);
        },
        [](const ServerRefusal&) {
            // A server before delivery settings: pop-ups follow the app's own switch only.
        });
    if (self->src.types.isEmpty())
        self->api->types(
            [this, key, url](const NotificationTypes& t) {
                Live* l = live_.value(key);
                if (!l) return;
                l->src.types = t;
                emit changed(url);
            },
            {});
}

void NotificationCenter::saveSettings(const QUrl& server, const QJsonObject& change, const std::function<void(const ServerRefusal*)>& done) {
    Live* self = liveOf(server);
    if (!self) {
        if (done) {
            ServerRefusal r;
            r.message = tr("You're not signed in to this server.");
            done(&r);
        }
        return;
    }
    const QString key = keyOf(server);
    self->api->saveSettings(
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
