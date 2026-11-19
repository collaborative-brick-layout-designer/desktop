#include "DesktopNotifier.h"

#include <QApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWidget>

namespace bld::ui {

bool DesktopNotifier::showSetting() {
    return QSettings().value(QLatin1String(kShowKey), true).toBool();
}

bool DesktopNotifier::backgroundOnlySetting() {
    return QSettings().value(QLatin1String(kBackgroundOnlyKey), false).toBool();
}

namespace {
std::optional<bool>& availableOverride() {
    static std::optional<bool> v;
    return v;
}
} // namespace

void DesktopNotifier::setAvailableForTests(std::optional<bool> available) {
    availableOverride() = available;
}

bool DesktopNotifier::available() {
    if (const std::optional<bool>& forced = availableOverride(); forced.has_value()) return forced.value();
    return QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
}

DesktopNotifier::DesktopNotifier(sync::NotificationCenter& center, QWidget* window)
    : QObject(window), center_(center), window_(window) {
    appInFront = [] { return QGuiApplication::applicationState() == Qt::ApplicationActive; };
    now = [] { return QDateTime::currentDateTimeUtc(); };
    canShow = [] { return available(); };
    burst_.setSingleShot(true);
    burst_.setInterval(1500);
    connect(&burst_, &QTimer::timeout, this, &DesktopNotifier::flush);
    // Quiet hours end on the minute.
    quiet_.setInterval(60000);
    connect(&quiet_, &QTimer::timeout, this, &DesktopNotifier::checkQuiet);
    connect(&center_, &sync::NotificationCenter::arrived, this, &DesktopNotifier::onArrived);
    // (After the window is made: the tray may not be ready before the event loop runs.)
    QTimer::singleShot(0, this, &DesktopNotifier::settingsChanged);
}

DesktopNotifier::~DesktopNotifier() = default;

void DesktopNotifier::settingsChanged() {
    const bool on = showSetting() && canShow && canShow() && !show;
    if (on) ensureTray();
    if (tray_) tray_->setVisible(on);
    if (!showSetting()) {
        pending_.clear();
        held_.clear();
    }
}

void DesktopNotifier::ensureTray() {
    if (tray_) return;
    tray_ = new QSystemTrayIcon(window_ ? window_->windowIcon() : QApplication::windowIcon(), this);
    tray_->setToolTip(QApplication::applicationDisplayName());
    connect(tray_, &QSystemTrayIcon::messageClicked, this, &DesktopNotifier::click);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason why) {
        if (why == QSystemTrayIcon::Trigger || why == QSystemTrayIcon::DoubleClick) bringToFront();
    });
}

bool DesktopNotifier::wanted(const QUrl& server, const sync::NotificationItem& n) const {
    if (n.read()) return false;
    const auto* s = center_.source(server);
    if (s && s->settings) return s->settings->pushFor(n.type, n.club ? n.club->id : QString());
    // A server that doesn't say: what waits for you, and warnings.
    return n.waits() || n.isWarning();
}

bool DesktopNotifier::quietNow(const QUrl& server) const {
    const auto* s = center_.source(server);
    return s && s->settings && s->settings->inQuietHours(now());
}

void DesktopNotifier::onArrived(const QUrl& server, const QList<sync::NotificationItem>& fresh) {
    if (!showSetting()) return;
    for (const auto& n : fresh) {
        if (!wanted(server, n)) continue;
        if (quietNow(server)) {
            held_ << Waiting{ server, n };
            if (!quiet_.isActive()) quiet_.start();
        } else {
            pending_ << Waiting{ server, n };
        }
    }
    if (!pending_.isEmpty()) burst_.start();
}

void DesktopNotifier::checkQuiet() {
    QList<Waiting> still;
    for (const Waiting& w : std::as_const(held_)) {
        if (quietNow(w.server)) still << w;
        else pending_ << w;
    }
    held_ = std::move(still);
    if (held_.isEmpty()) quiet_.stop();
    if (!pending_.isEmpty()) burst_.start();
}

void DesktopNotifier::flush() {
    burst_.stop();
    if (pending_.isEmpty()) return;
    const QList<Waiting> batch = std::exchange(pending_, {});
    // Off since, or in front with "only in the background": none (they're in the bell).
    if (!showSetting()) return;
    if (backgroundOnlySetting() && appInFront && appInFront()) return;
    const bool hooked = static_cast<bool>(show);
    if (!hooked && !(canShow && canShow())) return;
    QString title;
    QString body;
    if (batch.size() == 1) {
        title = batch.first().item.title;
        body = batch.first().item.body;
        if (body.isEmpty()) body = tr("Click to open it.");
    } else {
        title = tr("%n new notification(s)", nullptr, static_cast<int>(batch.size()));
        QStringList titles;
        for (qsizetype i = 0; i < batch.size() && i < 3; ++i) titles << batch[i].item.title;
        body = titles.join(QLatin1Char('\n'));
        if (batch.size() > 3)
            body += QLatin1Char('\n') + tr("and %n more", nullptr, static_cast<int>(batch.size() - 3));
    }
    // A long body is cut, as the web's push is.
    if (body.size() > 140) body = body.left(139).trimmed() + QStringLiteral("…");
    shown_ = batch;
    if (hooked) {
        show(title, body);
        return;
    }
    ensureTray();
    tray_->setVisible(true);
    tray_->showMessage(title, body, window_ ? window_->windowIcon() : QIcon(), 8000);
}

void DesktopNotifier::bringToFront() {
    if (!window_) return;
    if (window_->isMinimized()) window_->showNormal();
    else window_->show();
    window_->raise();
    window_->activateWindow();
}

void DesktopNotifier::click() {
    bringToFront();
    if (shown_.size() == 1) emit openRequested(shown_.first().server, shown_.first().item);
    else if (!shown_.isEmpty()) emit showAllRequested();
}

} // namespace bld::ui
