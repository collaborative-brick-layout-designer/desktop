#include "Notifications.h"

#include "LibraryApi.h"
#include "ServerApi.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <algorithm>

namespace bld::sync {

namespace {

qint64 msOf(const QJsonValue& v) { return v.isDouble() ? static_cast<qint64>(v.toDouble()) : 0; }

std::optional<NotificationPerson> personOf(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    const QJsonObject o = v.toObject();
    return NotificationPerson{ o.value(QLatin1String("id")).toString(), o.value(QLatin1String("name")).toString(),
                               o.value(QLatin1String("you")).toBool(false) };
}

int minutesOf(const QString& hhmm) {
    const QStringList p = hhmm.split(QLatin1Char(':'));
    if (p.size() != 2) return -1;
    bool a = false, b = false;
    const int h = p[0].toInt(&a), m = p[1].toInt(&b);
    if (!a || !b || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

}  // namespace

bool NotificationItem::isWarning() const {
    return type == QLatin1String("moderation.warning") || type == QLatin1String("moderation.final_warning");
}

QString NotificationQuery::path() const {
    QStringList q;
    const auto add = [&q](const char* key, const QString& value) {
        q << QLatin1String(key) + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(value, ","));
    };
    if (unreadOnly) add("unread", QStringLiteral("1"));
    if (!types.isEmpty()) add("type", types.join(QLatin1Char(',')));
    if (!category.isEmpty()) add("category", category);
    if (const QString words = search.trimmed().left(100); !words.isEmpty()) add("q", words);
    if (!cursor.isEmpty()) add("cursor", cursor);
    add("limit", QString::number(std::clamp(limit, 1, 100)));
    return QStringLiteral("/api/notifications?") + q.join(QLatin1Char('&'));
}

const NotificationTypeSetting* NotificationSettings::type(const QString& t) const {
    for (const auto& s : types)
        if (s.type == t) return &s;
    return nullptr;
}

const NotificationClubSetting* NotificationSettings::club(const QString& id) const {
    for (const auto& c : clubs)
        if (c.id == id) return &c;
    return nullptr;
}

bool NotificationSettings::pushFor(const QString& t, const QString& clubId) const {
    const NotificationTypeSetting* s = type(t);
    if (!s) return false;
    const NotificationClubSetting* c = clubId.isEmpty() ? nullptr : club(clubId);
    const bool muted = c && c->mute && !s->appLocked;
    return !pausePush && !muted && !(c && !c->push) && s->push;
}

bool NotificationSettings::inQuietHours(const QDateTime& utc) const {
    if (!quietOn || quietStart == quietEnd) return false;
    const int s = minutesOf(quietStart), e = minutesOf(quietEnd);
    if (s < 0 || e < 0) return false;
    QTimeZone zone(timeZone.toUtf8());
    if (timeZone.isEmpty() || !zone.isValid()) zone = QTimeZone::UTC;
    const QTime local = utc.toTimeZone(zone).time();
    const int now = local.hour() * 60 + local.minute();
    return s < e ? (now >= s && now < e) : (now >= s || now < e);
}

NotificationApi::NotificationApi(QObject* parent) : QObject(parent) { net_.setTransferTimeout(kServerStallTimeoutMs); }

QUrl NotificationApi::webUrl(const QString& pathAndQuery) const {
    QString rest = pathAndQuery;
    QString fragment;
    if (const qsizetype h = rest.indexOf(QLatin1Char('#')); h >= 0) {
        fragment = rest.mid(h + 1);
        rest = rest.left(h);
    }
    const qsizetype q = rest.indexOf(QLatin1Char('?'));
    const QString path = q < 0 ? rest : rest.left(q);
    // Only a path on this site: never another address an item names.
    if (!path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1String("//"))) return {};
    QUrl u = base_;
    u.setPath(path, QUrl::TolerantMode);
    u.setQuery(q < 0 ? QString() : rest.mid(q + 1));
    u.setFragment(fragment.isEmpty() ? QString() : fragment);
    return u;
}

void NotificationApi::send(const QByteArray& verb, const QString& path, const std::optional<QJsonObject>& body,
                           std::function<void(const QJsonObject&)> ok, Fail failed) {
    QUrl u = base_;
    const qsizetype q = path.indexOf(QLatin1Char('?'));
    u.setPath(q < 0 ? path : path.left(q), QUrl::TolerantMode);
    if (q >= 0) u.setQuery(path.mid(q + 1), QUrl::StrictMode);
    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    QByteArray bytes;
    if (body) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        bytes = QJsonDocument(*body).toJson(QJsonDocument::Compact);
    }
    QNetworkReply* r = verb == "GET" ? net_.get(req) : net_.sendCustomRequest(req, verb, bytes);
    ++pending_;
    connect(r, &QNetworkReply::finished, this, [this, r, ok = std::move(ok), failed = std::move(failed)] {
        r->deleteLater();
        --pending_;
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = r->readAll();
        if (status >= 200 && status < 300) {
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            // A web page where the JSON should be: not this app answering.
            if (doc.isNull() && !raw.trimmed().isEmpty()) {
                if (failed) failed(readRefusal(status, raw));
                return;
            }
            if (ok) ok(doc.object());
            return;
        }
        if (failed) failed(readRefusal(status, raw, networkFailureText(r)));
    });
}

NotificationItem NotificationApi::itemFromJson(const QJsonObject& o) {
    NotificationItem n;
    n.id = o.value(QLatin1String("id")).toString();
    n.type = o.value(QLatin1String("type")).toString();
    n.category = o.value(QLatin1String("category")).toString();
    n.title = o.value(QLatin1String("title")).toString();
    n.body = o.value(QLatin1String("body")).toString();
    n.link = o.value(QLatin1String("link")).toString();
    n.createdAt = msOf(o.value(QLatin1String("createdAt")));
    n.readAt = msOf(o.value(QLatin1String("readAt")));
    n.doneAt = msOf(o.value(QLatin1String("doneAt")));
    n.outcome = o.value(QLatin1String("outcome")).toString();
    n.outcomeBy = personOf(o.value(QLatin1String("outcomeBy")));
    n.actor = personOf(o.value(QLatin1String("actor")));
    if (const QJsonValue c = o.value(QLatin1String("club")); c.isObject()) {
        const QJsonObject co = c.toObject();
        n.club = NotificationClub{ co.value(QLatin1String("id")).toString(), co.value(QLatin1String("name")).toString(),
                                   co.value(QLatin1String("slug")).toString() };
    }
    const QJsonObject subject = o.value(QLatin1String("subject")).toObject();
    n.subjectKind = subject.value(QLatin1String("kind")).toString();
    n.subjectId = subject.value(QLatin1String("id")).toString();
    n.groupKey = o.value(QLatin1String("groupKey")).toString();
    for (const auto& a : o.value(QLatin1String("actions")).toArray()) n.actions << a.toString();
    return n;
}

NotificationCounts NotificationApi::countsFromJson(const QJsonObject& o) {
    return { static_cast<int>(msOf(o.value(QLatin1String("unread")))),
             static_cast<int>(msOf(o.value(QLatin1String("waiting")))),
             static_cast<int>(msOf(o.value(QLatin1String("warnings")))) };
}

NotificationSettings NotificationApi::settingsFromJson(const QJsonObject& o) {
    NotificationSettings s;
    for (const auto& c : o.value(QLatin1String("categories")).toArray()) s.categories << c.toString();
    for (const auto& v : o.value(QLatin1String("types")).toArray()) {
        const QJsonObject t = v.toObject();
        NotificationTypeSetting ts;
        ts.type = t.value(QLatin1String("type")).toString();
        ts.category = t.value(QLatin1String("category")).toString();
        ts.label = t.value(QLatin1String("label")).toString();
        ts.waits = t.value(QLatin1String("waits")).toBool(false);
        ts.appLocked = t.value(QLatin1String("appLocked")).toBool(false);
        ts.emailApart = t.value(QLatin1String("emailApart")).toString();
        ts.app = t.value(QLatin1String("app")).toBool(true);
        ts.email = t.value(QLatin1String("email")).toString(QStringLiteral("off"));
        ts.push = t.value(QLatin1String("push")).toBool(false);
        s.types << ts;
    }
    const QJsonObject quiet = o.value(QLatin1String("quiet")).toObject();
    s.quietOn = quiet.value(QLatin1String("on")).toBool(false);
    s.quietStart = quiet.value(QLatin1String("start")).toString(QStringLiteral("22:00"));
    s.quietEnd = quiet.value(QLatin1String("end")).toString(QStringLiteral("07:00"));
    s.timeZone = o.value(QLatin1String("timeZone")).toString();
    s.dailyAt = o.value(QLatin1String("dailyAt")).toString(QStringLiteral("08:00"));
    s.weeklyDay = o.value(QLatin1String("weeklyDay")).toInt(1);
    s.pauseEmail = o.value(QLatin1String("pauseEmail")).toBool(false);
    s.pausePush = o.value(QLatin1String("pausePush")).toBool(false);
    for (const auto& v : o.value(QLatin1String("clubs")).toArray()) {
        const QJsonObject c = v.toObject();
        s.clubs << NotificationClubSetting{ c.value(QLatin1String("id")).toString(), c.value(QLatin1String("name")).toString(),
                                            c.value(QLatin1String("slug")).toString(), c.value(QLatin1String("mute")).toBool(false),
                                            c.value(QLatin1String("email")).toBool(true), c.value(QLatin1String("push")).toBool(true) };
    }
    s.emailAvailable = o.value(QLatin1String("emailAvailable")).toBool(true);
    s.pushAvailable = o.value(QLatin1String("pushAvailable")).toBool(false);
    return s;
}

void NotificationApi::list(const NotificationQuery& q, std::function<void(const NotificationPage&)> done, Fail failed) {
    send("GET", q.path(), std::nullopt, [done = std::move(done)](const QJsonObject& o) {
        NotificationPage page;
        for (const auto& v : o.value(QLatin1String("notifications")).toArray()) page.items << itemFromJson(v.toObject());
        page.nextCursor = o.value(QLatin1String("nextCursor")).toString();
        page.counts = countsFromJson(o);
        if (done) done(page);
    }, std::move(failed));
}

void NotificationApi::count(std::function<void(const NotificationCounts&)> done, Fail failed) {
    send("GET", QStringLiteral("/api/notifications/count"), std::nullopt, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(countsFromJson(o));
    }, std::move(failed));
}

void NotificationApi::types(std::function<void(const NotificationTypes&)> done, Fail failed) {
    send("GET", QStringLiteral("/api/notifications/types"), std::nullopt, [done = std::move(done)](const QJsonObject& o) {
        NotificationTypes t;
        for (const auto& c : o.value(QLatin1String("categories")).toArray()) t.categories << c.toString();
        for (const auto& v : o.value(QLatin1String("types")).toArray()) {
            const QJsonObject x = v.toObject();
            t.types << NotificationTypeInfo{ x.value(QLatin1String("type")).toString(), x.value(QLatin1String("category")).toString(),
                                             x.value(QLatin1String("label")).toString(), x.value(QLatin1String("waits")).toBool(false),
                                             x.value(QLatin1String("action")).toString() };
        }
        if (done) done(t);
    }, std::move(failed));
}

void NotificationApi::setRead(const QString& id, bool read, std::function<void()> done, Fail failed) {
    send("POST", QStringLiteral("/api/notifications/%1/%2").arg(idPath(id), read ? QStringLiteral("read") : QStringLiteral("unread")),
         std::nullopt, [done = std::move(done)](const QJsonObject&) {
             if (done) done();
         },
         std::move(failed));
}

void NotificationApi::readAll(const QStringList& ids, std::function<void(int)> done, Fail failed) {
    QJsonObject body;
    if (!ids.isEmpty()) body.insert(QStringLiteral("ids"), QJsonArray::fromStringList(ids.mid(0, 500)));
    send("POST", QStringLiteral("/api/notifications/read-all"), body, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(o.value(QLatin1String("read")).toInt());
    }, std::move(failed));
}

void NotificationApi::acknowledge(const QString& id, std::function<void()> done, Fail failed) {
    send("POST", QStringLiteral("/api/notifications/%1/acknowledge").arg(idPath(id)), std::nullopt,
         [done = std::move(done)](const QJsonObject&) {
             if (done) done();
         },
         std::move(failed));
}

void NotificationApi::act(const QString& id, const QString& action, std::function<void(const NotificationActResult&)> done,
                          Fail failed) {
    send("POST", QStringLiteral("/api/notifications/%1/act").arg(idPath(id)), QJsonObject{ { QStringLiteral("action"), action } },
         [done = std::move(done)](const QJsonObject& o) {
             NotificationActResult r;
             r.outcome = o.value(QLatin1String("outcome")).toString();
             r.already = o.value(QLatin1String("already")).toBool(false);
             r.result = o.value(QLatin1String("result")).toObject();
             if (const QJsonValue n = o.value(QLatin1String("notification")); n.isObject()) r.item = itemFromJson(n.toObject());
             if (done) done(r);
         },
         std::move(failed));
}

void NotificationApi::settings(std::function<void(const NotificationSettings&)> done, Fail failed) {
    send("GET", QStringLiteral("/api/me/notification-settings"), std::nullopt, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(settingsFromJson(o));
    }, std::move(failed));
}

void NotificationApi::saveSettings(const QJsonObject& settings, std::function<void(const NotificationSettings&)> done,
                                   Fail failed) {
    send("PUT", QStringLiteral("/api/me/notification-settings"), QJsonObject{ { QStringLiteral("settings"), settings } },
         [done = std::move(done)](const QJsonObject& o) {
             if (done) done(settingsFromJson(o));
         },
         std::move(failed));
}

bool isNoNotifications(const ServerRefusal& r) { return r.status == 404; }

QList<NotificationGroup> groupNotifications(const QList<NotificationItem>& items) {
    QList<NotificationGroup> out;
    QHash<QString, qsizetype> byKey;
    for (const NotificationItem& n : items) {
        if (!n.groupKey.isEmpty()) {
            if (const auto it = byKey.constFind(n.groupKey); it != byKey.constEnd()) {
                out[*it].rest << n;
                continue;
            }
            byKey.insert(n.groupKey, out.size());
        }
        out << NotificationGroup{ n, {} };
    }
    return out;
}

}  // namespace bld::sync
