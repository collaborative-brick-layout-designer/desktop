#pragma once

// Settings › Notifications: this computer's system notifications, and how
// notifications reach you from each server you're signed in to, the same
// choices as the web's Settings › Notifications
// (apps/web/src/notifications/NotificationSettings.tsx): for each kind, in
// the app, email (right away, a daily or weekly summary, or off) and
// pop-up; pause all email or pop-ups; quiet hours; when summaries go and in
// which time zone; and per club, mute, email and pop-ups. Every change is
// saved on the server at once (PUT /api/me/notification-settings merges
// it) and shows in the other apps; their changes show here too.

#include "../sync/NotificationCenter.h"

#include <QSet>
#include <QUrl>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QVBoxLayout;

namespace bld::ui {

class NotificationSettingsPanel : public QWidget {
    Q_OBJECT
public:
    // `localChanged`: this computer's system notification switches changed.
    NotificationSettingsPanel(sync::NotificationCenter* center, const std::function<void()>& localChanged,
                              QWidget* parent = nullptr);

    // Which server's settings show (tests).
    void showServer(const QUrl& server);
    QComboBox* serverPicker() const { return servers_; }
    QCheckBox* systemSwitch() const { return system_; }
    QCheckBox* backgroundSwitch() const { return background_; }
    QLabel* systemNote() const { return systemNote_; }
    QLabel* status() const { return status_; }
    // This computer's time zone, as an IANA name (UTC when unknown).
    static QString computerTimeZone();

private:
    void rebuild();
    void save(const QJsonObject& change);
    QUrl current() const;
    QWidget* typeRow(const sync::NotificationTypeSetting& t, QWidget* parent);

    sync::NotificationCenter* center_;
    QCheckBox* system_ = nullptr;
    QCheckBox* background_ = nullptr;
    QLabel* systemNote_ = nullptr;
    QComboBox* servers_ = nullptr;
    QLabel* serverNote_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
    QLabel* status_ = nullptr;
    QSet<QString> open_; // the categories shown open
    bool saving_ = false;
};

} // namespace bld::ui
