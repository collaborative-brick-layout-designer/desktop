#pragma once

// System notifications (the computer's own pop-ups) for new notifications
// while the app runs, on Linux, Windows and macOS (QSystemTrayIcon's
// messages, which use the platform's notification service).
//
// - Only new ones (NotificationCenter::arrived), and only the types whose
//   pop-up ("push") is on in your delivery settings on that server, with its
//   club's mute and pop-up choice and "Pause all pop-ups"; a server that
//   doesn't say pops up what waits for you and warnings.
// - In the server's quiet hours they wait, and come together once the quiet
//   hours end.
// - A burst never pops up one by one: what comes within a moment is one
//   "3 new notifications".
// - Settings › Notifications: "Show system notifications" and "Only when the
//   app is in the background" (this computer's, kept in QSettings).
// - Clicking one brings the app to the front and opens it (several: the
//   Notifications window).
// - A computer without a notification area shows none, quietly, and
//   Settings says so.

#include "../sync/NotificationCenter.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>

class QSystemTrayIcon;
class QWidget;

namespace bld::ui {

class DesktopNotifier : public QObject {
    Q_OBJECT
public:
    static constexpr const char* kShowKey = "notifications/systemShow";
    static constexpr const char* kBackgroundOnlyKey = "notifications/systemBackgroundOnly";
    // On by default; "only in the background" off.
    static bool showSetting();
    static bool backgroundOnlySetting();
    // Whether this computer can show them (a notification area that takes messages).
    static bool available();
    // Tests say whether there is one (nullopt: ask the computer).
    static void setAvailableForTests(std::optional<bool> available);

    DesktopNotifier(sync::NotificationCenter& center, QWidget* window);
    ~DesktopNotifier() override;

    // Settings changed: the tray icon follows.
    void settingsChanged();
    // Shows what waits now (tests; otherwise a moment after the last arrival).
    void flush();
    // Lets out what waited for quiet hours that have ended (each minute; tests call it).
    void checkQuiet();
    // How long a burst is gathered, in ms (tests shorten it).
    void setBurstWindow(int ms) { burst_.setInterval(ms); }

    // Test hooks: what shows a pop-up (default: the tray icon's message), whether
    // the app is in front, and the time now.
    std::function<void(const QString& title, const QString& body)> show;
    std::function<bool()> appInFront;
    std::function<QDateTime()> now;
    // Whether this computer can show them (default: available()).
    std::function<bool()> canShow;
    // The pop-up the last click would open (tests): clicks it.
    void click();

signals:
    void openRequested(const QUrl& server, const bld::sync::NotificationItem& n);
    void showAllRequested();

private:
    struct Waiting {
        QUrl server;
        sync::NotificationItem item;
    };
    void onArrived(const QUrl& server, const QList<sync::NotificationItem>& fresh);
    bool wanted(const QUrl& server, const sync::NotificationItem& n) const;
    bool quietNow(const QUrl& server) const;
    void ensureTray();
    void bringToFront();

    sync::NotificationCenter& center_;
    QWidget* window_;  // our parent
    QSystemTrayIcon* tray_ = nullptr;
    QTimer burst_;
    QTimer quiet_;
    QList<Waiting> pending_;
    QList<Waiting> held_;  // in quiet hours
    QList<Waiting> shown_; // what the last pop-up was about
};

} // namespace bld::ui
