#pragma once

// Your notifications on every server you're signed in to, for the bell, its
// panel, the Notifications window, the warning card and the pop-ups. Each
// signed-in server in "Your servers" (ServerList, with a token in the
// TokenStore) is a source of its own, keyed by its address, with its own
// counts, warnings, delivery settings and live hints (GET /api/events: a
// "notification" hint asks again). The bell's number adds up every source.
//
// A server before notifications (404) is marked as such and left alone: the
// app shows its older notices there (LibraryApi::notices), and says
// nothing. A server whose sign-in ended (Servers… signed out) goes.
//
// New ones (unread, not seen before since the app started) come out as
// arrived(), once each, for the pop-ups; the ones there at start are not new.

#include "EventStream.h"
#include "Notifications.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

namespace bld::sync {

class TokenStore;

class NotificationCenter : public QObject {
    Q_OBJECT
public:
    enum class Support { Unknown, Yes, No };

    struct Source {
        QUrl url;
        QString label;  // its name in Your servers, else its host
        Support support = Support::Unknown;
        NotificationCounts counts;
        std::optional<NotificationSettings> settings;
        NotificationTypes types;
        // Warnings waiting for "I understand", newest first.
        QList<NotificationItem> warnings;
    };

    // What was answered here (Accept, Decline, I understand), kept for as long as you're signed
    // in there: the lists replace their rows whenever they ask again, so the row that was
    // clicked may be gone when the server answers, and a list asked for before that may come
    // after it. The server never asks the same one again once it is answered.
    struct Answer {
        bool sending = false;                  // on its way
        QString error;                         // the last try was refused
        QString readError;                     // the last mark as read or unread was refused
        std::optional<NotificationItem> item;  // answered: as it is now
        bool already = false;                  // someone else had answered first
        QString layoutId;                      // the layout an accepted invite opens
    };

    explicit NotificationCenter(QObject* parent = nullptr);
    ~NotificationCenter() override;

    // Where the sign-ins are (then reload()).
    void setTokenStore(std::shared_ptr<TokenStore> tokens);
    // Reads Your servers and their sign-ins again: a source for each server
    // signed in to, none for the others. Asks each one for its counts.
    void reload();
    // A sign-in made (or ended, `token` empty) elsewhere in the app.
    void setSignedIn(const QUrl& server, const QString& token);
    // Asks again: one server, or every one (`server` empty).
    void refresh(const QUrl& server = {});
    // Live hints on or off (tests that only want the routes switch them off).
    void setLive(bool live);
    // The app already holds the event stream of `server` (the Module
    // library's): its hints come through hint() instead of a second stream.
    // An invalid url: none.
    void setOuterStream(const QUrl& server);
    void hint(const QUrl& server, const QJsonObject& hint);
    // The wait before an event stream reconnects (tests shorten it).
    void setReconnectDelay(int firstMs, int maxMs);

    // The signed-in servers, in Your servers' order.
    QList<QUrl> servers() const;
    // Those that have notifications.
    QList<QUrl> supported() const;
    const Source* source(const QUrl& server) const;
    Support support(const QUrl& server) const;
    // The server's api (null when not signed in there).
    NotificationApi* api(const QUrl& server) const;
    // Every server's unread notifications, added up.
    int totalUnread() const;
    int totalWaiting() const;

    // After a change made here (marked read, answered, acknowledged): the
    // counts and lists are asked for again, at once.
    void changedHere(const QUrl& server);
    // Answers one ("accept" or "decline"), says "I understand", or marks it read or unread; then
    // everything is asked for again. answerChanged() says when answer() moved. `refused` takes
    // back what a row showed at once.
    void act(const QUrl& server, const NotificationItem& n, const QString& action);
    void acknowledge(const QUrl& server, const NotificationItem& n);
    void setRead(const QUrl& server, const QString& id, bool read, std::function<void()> refused);
    // Null: not answered here.
    const Answer* answer(const QUrl& server, const QString& id) const;
    // Saves a change to a server's delivery settings (PUT merges it).
    void saveSettings(const QUrl& server, const QJsonObject& change, const std::function<void(const ServerRefusal*)>& done = {});

    // The same key the token and the server's folders use.
    static QString keyOf(const QUrl& server);

signals:
    // A server came or went, or its support became known.
    void sourcesChanged();
    void countsChanged();
    // Something changed on that server: open lists ask again.
    void changed(const QUrl& server);
    void warningsChanged();
    void answerChanged(const QUrl& server, const QString& id);
    void settingsChanged(const QUrl& server);
    // New notifications since the app started, for the pop-ups (once each).
    void arrived(const QUrl& server, const QList<bld::sync::NotificationItem>& fresh);

private:
    struct Live;
    Live* liveOf(const QUrl& server) const;
    void add(const QUrl& url, const QString& label, const QString& token);
    void drop(const QString& key);
    void ask(const QString& key);
    void askSettings(const QString& key);
    // Runs its change on the answer to `id` and says so, unless that sign-in is over.
    void settle(const QUrl& server, const QString& token, const QString& id, const std::function<void(Answer&)>& change);
    void onHint(const QString& key, const QJsonObject& hint);

    std::shared_ptr<TokenStore> tokens_;
    QHash<QString, Live*> live_;
    QStringList order_;
    QString outer_;
    bool streams_ = true;
    int firstDelayMs_ = 5000;
    int maxDelayMs_ = 60000;
    int reloadGeneration_ = 0;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace bld::sync
