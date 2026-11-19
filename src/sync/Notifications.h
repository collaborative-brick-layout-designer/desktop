#pragma once

// A server's notifications (docs/notifications.md in the web repo): what
// waits for your answer (requests, invites), news from your clubs and the
// people you share with, and warnings that need "I understand". The same
// API the web's bell uses, through the desktop's token:
//
//   GET  /api/notifications            a page, newest first, with your counts
//   GET  /api/notifications/count      { unread, waiting, warnings }
//   GET  /api/notifications/types      the types, in plain words
//   POST /api/notifications/:id/read   (and /unread)
//   POST /api/notifications/read-all   { ids? }
//   POST /api/notifications/:id/acknowledge   a warning's "I understand"
//   POST /api/notifications/:id/act    { action: accept | decline }
//   GET / PUT /api/me/notification-settings   how each type reaches you
//
// A server before notifications answers 404 to all of these: the app then
// uses the older notices (LibraryApi::notices) there, and says nothing.
// Titles and bodies are plain text, never HTML. The token rides in the
// Authorization header only.

#include "ServerRefusal.h"

#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkReply;

namespace bld::sync {

struct NotificationPerson {
    QString id;
    QString name;
    bool you = false;  // outcomeBy: the caller answered it
};

struct NotificationClub {
    QString id;
    QString name;
    QString slug;
};

struct NotificationItem {
    QString id;
    QString type;      // club.join_request, moderation.warning, …; unknown types still show
    QString category;  // moderation / clubs / sharing / shared_modules / account; empty: unknown
    QString title;
    QString body;
    QString link;      // a path on the server's website, or empty
    qint64 createdAt = 0;  // Unix ms
    qint64 readAt = 0;     // 0: unread
    qint64 doneAt = 0;     // 0: not answered (or acknowledged) yet
    QString outcome;       // accepted / declined / withdrawn / gone / acknowledged, or empty
    std::optional<NotificationPerson> outcomeBy;
    std::optional<NotificationClub> club;
    std::optional<NotificationPerson> actor;
    QString subjectKind;  // layout / module / org / join_request / …
    QString subjectId;
    QString groupKey;
    QStringList actions;  // accept, decline / acknowledge; empty once done

    bool read() const { return readAt > 0; }
    bool done() const { return doneAt > 0; }
    // A request or invite waiting for Accept or Decline.
    bool waits() const { return actions.contains(QLatin1String("accept")); }
    // A warning waiting for "I understand".
    bool needsAcknowledge() const { return actions.contains(QLatin1String("acknowledge")); }
    bool isWarning() const;
};

struct NotificationCounts {
    int unread = 0;
    int waiting = 0;
    int warnings = 0;
    bool operator==(const NotificationCounts&) const = default;
};

// GET /api/notifications's filters.
struct NotificationQuery {
    bool unreadOnly = false;
    QStringList types;  // one or more types
    QString category;   // one category
    QString search;     // words in the title or body (100 characters at most)
    QString cursor;     // the previous page's nextCursor
    int limit = 30;     // 1..100
    // The path and query it asks for.
    QString path() const;
};

struct NotificationPage {
    QList<NotificationItem> items;
    QString nextCursor;  // empty at the end
    NotificationCounts counts;
};

struct NotificationTypeInfo {
    QString type;
    QString category;
    QString label;
    bool waits = false;
    QString action;  // what `actions` can hold: "accept, decline", "acknowledge", or empty
};

struct NotificationTypes {
    QStringList categories;
    QList<NotificationTypeInfo> types;
    bool isEmpty() const { return types.isEmpty(); }
};

// What answering a request did.
struct NotificationActResult {
    QString outcome;   // accepted / declined (or how it ended, when `already`)
    bool already = false;  // someone else answered first, it was taken back, or it expired
    QJsonObject result;    // the answering route's own reply ({ layoutId } for a layout invite)
    std::optional<NotificationItem> item;  // the item as it is now
};

// How one type reaches you (Settings › Notifications on the web).
struct NotificationTypeSetting {
    QString type;
    QString category;
    QString label;
    bool waits = false;
    bool appLocked = false;  // warnings: always in the app
    QString emailApart;      // "invite" / "account": emailed apart, no email choice; empty: a choice
    bool app = true;
    QString email = QStringLiteral("off");  // now / daily / weekly / off
    bool push = false;
};

struct NotificationClubSetting {
    QString id;
    QString name;
    QString slug;
    bool mute = false;
    bool email = true;
    bool push = true;
};

// GET /api/me/notification-settings.
struct NotificationSettings {
    QStringList categories;
    QList<NotificationTypeSetting> types;
    bool quietOn = false;
    QString quietStart = QStringLiteral("22:00");
    QString quietEnd = QStringLiteral("07:00");
    QString timeZone;  // IANA; empty: not set yet (the server uses UTC)
    QString dailyAt = QStringLiteral("08:00");
    int weeklyDay = 1;  // 0 Sunday … 6 Saturday
    bool pauseEmail = false;
    bool pausePush = false;
    QList<NotificationClubSetting> clubs;
    bool emailAvailable = true;
    bool pushAvailable = false;

    const NotificationTypeSetting* type(const QString& t) const;
    const NotificationClubSetting* club(const QString& id) const;
    // Whether a new one of this type (from this club, or none) should pop up,
    // as the server decides for push (channelsFor): its type's push choice,
    // the club's mute and push, and "pause all pop-ups". An unknown type: no.
    bool pushFor(const QString& type, const QString& clubId) const;
    // Whether `utc` falls in the quiet hours, in the settings' time zone
    // (UTC when not set): start to end, across midnight when end < start.
    bool inQuietHours(const QDateTime& utc) const;
};

class NotificationApi : public QObject {
    Q_OBJECT
public:
    using Fail = std::function<void(const ServerRefusal&)>;

    explicit NotificationApi(QObject* parent = nullptr);

    void setBase(const QUrl& base) { base_ = base; }
    QUrl base() const { return base_; }
    void setToken(const QString& token) { token_ = token; }
    QString token() const { return token_; }
    // A path on the server's website (an item's link) as a full address.
    QUrl webUrl(const QString& pathAndQuery) const;

    void list(const NotificationQuery& q, std::function<void(const NotificationPage&)> done, Fail failed);
    void count(std::function<void(const NotificationCounts&)> done, Fail failed);
    void types(std::function<void(const NotificationTypes&)> done, Fail failed);
    // Mark it read (`read`) or unread again.
    void setRead(const QString& id, bool read, std::function<void()> done, Fail failed);
    // Every one (`ids` empty), or these (at most 500): how many were marked.
    void readAll(const QStringList& ids, std::function<void(int)> done, Fail failed);
    void acknowledge(const QString& id, std::function<void()> done, Fail failed);
    // accept / decline.
    void act(const QString& id, const QString& action, std::function<void(const NotificationActResult&)> done,
             Fail failed);
    void settings(std::function<void(const NotificationSettings&)> done, Fail failed);
    // PUT merges what it's given onto what is stored: send only what changed
    // ({ "types": { "club.invite": { "push": false } } }).
    void saveSettings(const QJsonObject& settings, std::function<void(const NotificationSettings&)> done, Fail failed);

    // How many requests are still out (tests).
    int pending() const { return pending_; }

    static NotificationItem itemFromJson(const QJsonObject& o);
    static NotificationCounts countsFromJson(const QJsonObject& o);
    static NotificationSettings settingsFromJson(const QJsonObject& o);

private:
    void send(const QByteArray& verb, const QString& path, const std::optional<QJsonObject>& body,
              std::function<void(const QJsonObject&)> ok, Fail failed);

    QNetworkAccessManager net_;
    QUrl base_;
    QString token_;
    int pending_ = 0;
};

// The server has no notifications yet (an older one): its 404 for the route.
bool isNoNotifications(const ServerRefusal& r);

// Items with the same group key collapse into one ("3 requests to join
// ArkLUG"): the newest is the head, the rest follow it, newest first.
// The order follows each group's newest.
struct NotificationGroup {
    NotificationItem head;
    QList<NotificationItem> rest;
};
QList<NotificationGroup> groupNotifications(const QList<NotificationItem>& items);

}  // namespace bld::sync
