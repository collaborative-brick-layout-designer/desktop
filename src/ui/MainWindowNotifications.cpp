// The main window's half of the notifications (NotificationViews.h): the
// center that follows every signed-in server, the bell's panel and the
// Notifications window, the must-read warning card ("I understand"), and
// where Open goes in the app: a layout opens live, a module shows in the
// Server window (its Share dialog for one of yours), a club shows its
// things. Anything else opens its page on the website.
//
// A server before notifications keeps its older notices card
// (refreshNotices) and its "Open on the Website" lines.

#include "MainWindow.h"

#include "DesktopNotifier.h"
#include "NoticeArea.h"
#include "NotificationViews.h"
#include "ServerLibrary.h"
#include "ServerWindow.h"

#include "../sync/NotificationCenter.h"
#include "../sync/ServerList.h"
#include "../sync/WhileAlive.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>

namespace bld::ui {

void MainWindow::setTokenStore(std::shared_ptr<sync::TokenStore> store) {
    tokens_ = std::move(store);
    watchTokenSaves();
    if (notifications_) notifications_->setTokenStore(tokens_);
}

void MainWindow::setupNotifications() {
    if (!openWebLink) openWebLink = [](const QUrl& u) { QDesktopServices::openUrl(u); };
    notifications_ = new sync::NotificationCenter(this);
    connect(notifications_, &sync::NotificationCenter::warningsChanged, this, &MainWindow::showNextWarning);
    // Whether the library's server has notifications decides between the
    // warning card and the older notices card, and how a club's requests are answered.
    connect(notifications_, &sync::NotificationCenter::sourcesChanged, this, [this, known = std::make_shared<int>(-1)] {
        // Only when what the library's server has changed (the library asks for its notices itself).
        const int now = static_cast<int>(notifications_->support(serverLibrary_->server()));
        if (now != *known) {
            *known = now;
            if (serverLibrary_->state() == ServerLibrary::State::Ready) refreshNotices();
        }
        showNextWarning();
        if (serverLibrary_ && serverLibrary_->state() == ServerLibrary::State::Ready) emit serverLibrary_->changed();
    });
    serverLibrary_->setNotifications(notifications_, [this](const QUrl& server, const sync::NotificationItem& n,
                                                            const NotificationPlace& place) { openNotification(server, n, place); });
    // New ones pop up on this computer too (as each server's pop-up choices say).
    desktopNotifier_ = new DesktopNotifier(*notifications_, this);
    connect(desktopNotifier_, &DesktopNotifier::showAllRequested, this, &MainWindow::showNotificationsWindow);
    connect(desktopNotifier_, &DesktopNotifier::openRequested, this,
            [this](const QUrl& server, const sync::NotificationItem& n) {
                // Clicking it opens it, and so marks it read.
                if (sync::NotificationApi* api = notifications_->api(server))
                    api->setRead(
                        n.id, true,
                        sync::whileAlive(this, [this, server] { notifications_->changedHere(server); }), {});
                const NotificationPlace place = placeFor(n);
                if (place.kind == NotificationPlace::None) showNotificationsWindow();
                else openNotification(server, n, place);
            });
    // (updateLibraryServer, right after the window is made, asks every server.)
    notifications_->setTokenStore(tokens_);
}

// Found among this window's children each time: a window that closed itself is simply gone.
NotificationPanel* MainWindow::notificationPanel() const { return findChild<NotificationPanel*>(Qt::FindDirectChildrenOnly); }

NotificationsWindow* MainWindow::notificationsWindow() const {
    return findChild<NotificationsWindow*>(Qt::FindDirectChildrenOnly);
}

void MainWindow::toggleNotificationPanel() {
    NotificationPanel* panel = notificationPanel();
    if (panel && panel->isVisible()) {
        panel->close();
        return;
    }
    if (!panel) {
        panel = new NotificationPanel(*notifications_, this);
        connect(panel->list(), &NotificationList::openRequested, this, &MainWindow::openNotification);
        connect(panel, &NotificationPanel::seeAllRequested, this,
                [this] { QTimer::singleShot(0, this, &MainWindow::showNotificationsWindow); });
    }
    notifications_->refresh();
    panel->list()->reload();
    panel->popUp(notificationBell_);
}

void MainWindow::showNotificationsWindow() {
    NotificationsWindow* w = notificationsWindow();
    if (!w) {
        w = new NotificationsWindow(*notifications_, this);
        w->setAttribute(Qt::WA_DeleteOnClose);
        connect(w->list(), &NotificationList::openRequested, this, &MainWindow::openNotification);
    }
    w->list()->reload();
    w->show();
    w->raise();
    w->activateWindow();
}

void MainWindow::showNextWarning() {
    const QString id = QStringLiteral("notification-warning");
    if (!notifications_) return;
    QList<std::pair<QUrl, sync::NotificationItem>> open;
    for (const QUrl& u : notifications_->supported())
        if (const auto* s = notifications_->source(u))
            for (const auto& w : s->warnings) open.append({ u, w });
    if (open.isEmpty()) {
        notices_->hideNotice(id);
        return;
    }
    std::stable_sort(open.begin(), open.end(), [](const auto& a, const auto& b) { return a.second.createdAt > b.second.createdAt; });
    const auto [server, w] = open.first();
    QString title = w.title;
    if (open.size() > 1) title += QLatin1Char(' ') + tr("(1 of %1)").arg(open.size());
    if (notifications_->supported().size() > 1)
        if (const auto* s = notifications_->source(server)) title += QStringLiteral(" · ") + s->label;
    QList<NoticeAction> actions;
    actions << NoticeAction{ tr("I understand"),
                             [this, server, w] {
                                 sync::NotificationApi* api = notifications_->api(server);
                                 if (!api) return;
                                 api->acknowledge(w.id, sync::whileAlive(this, [this, server] { notifications_->changedHere(server); }),
                                                  sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
                                                      statusBar()->showMessage(sync::describe(r), 6000);
                                                      showNextWarning();
                                                  }));
                             },
                             true, true };
    if (const NotificationPlace place = placeFor(w); place.kind != NotificationPlace::None)
        actions << NoticeAction{ place.kind == NotificationPlace::Web ? tr("See what it's about on the website") : tr("See what it's about"),
                                 [this, server, w, place] { openNotification(server, w, place); }, false, false };
    actions << NoticeAction{ tr("All notifications"), [this] { showNotificationsWindow(); }, false, false };
    notices_->showNotice(id, title, w.body, actions, {}, true);
}

void MainWindow::whenLibraryOn(const QUrl& server, std::function<void()> run) {
    const auto here = [this, server] {
        return serverLibrary_->server().isValid() &&
               sync::TokenStore::keyFor(serverLibrary_->server()) == sync::TokenStore::keyFor(server);
    };
    if (here() && serverLibrary_->state() == ServerLibrary::State::Ready) {
        run();
        return;
    }
    if (!here()) {
        browseServer_ = server;
        sync::ServerList list = sync::ServerList::load();
        list.touch(server);
        list.save();
        updateLibraryServer();
    }
    // Once it's ready (and only for this server).
    auto* once = new QObject(this);
    connect(serverLibrary_, &ServerLibrary::stateChanged, once, [this, once, here, run = std::move(run)] {
        const auto state = serverLibrary_->state();
        if (state == ServerLibrary::State::Loading) return;
        once->deleteLater();
        if (state == ServerLibrary::State::Ready && here()) run();
    });
    QTimer::singleShot(30000, once, &QObject::deleteLater);
}

void MainWindow::openLayoutOn(const QUrl& server, const QString& token, const QString& layoutId, const QString& fallbackTitle) {
    // Its title and your role there, as the server lists it.
    auto* api = new sync::LibraryApi(this);
    api->setBase(server);
    api->setToken(token);
    api->layouts(
        [this, api, server, token, layoutId, fallbackTitle](const QList<sync::LayoutEntry>& list) {
            api->deleteLater();
            for (const auto& l : list)
                if (l.id == layoutId) {
                    openServerLayoutOn(server, token, l);
                    return;
                }
            sync::LayoutEntry l;
            l.id = layoutId;
            l.title = fallbackTitle;
            l.role = QStringLiteral("viewer");
            openServerLayoutOn(server, token, l);
        },
        [this, api](const sync::ServerRefusal& r) {
            api->deleteLater();
            QMessageBox::warning(this, tr("Open live"), sync::describe(r));
        });
}

void MainWindow::openNotification(const QUrl& server, const sync::NotificationItem& n, const NotificationPlace& place) {
    sync::NotificationApi* api = notifications_ ? notifications_->api(server) : nullptr;
    if (!api) return;
    switch (place.kind) {
    case NotificationPlace::None:
        return;
    case NotificationPlace::Web:
        if (const QUrl u = api->webUrl(place.link); u.isValid()) openWebLink(u);
        return;
    case NotificationPlace::Layout: {
        const QString token = api->token();
        const QString id = place.id;
        const QString title = n.title;
        // After the click has returned: opening may ask first.
        QTimer::singleShot(0, this, [this, server, token, id, title] { openLayoutOn(server, token, id, title); });
        return;
    }
    case NotificationPlace::Module: {
        const QString id = place.id;
        const bool sharing = n.type.startsWith(QLatin1String("sharing."));
        whenLibraryOn(server, [this, id, sharing] {
            showServerWindow(static_cast<int>(ServerWindow::Tab::Modules));
            // One of yours: its sharing.
            const sync::ServerModule* m = serverLibrary_->module(id);
            if (sharing && m && m->canDelete())
                if (auto* tab = findChild<ServerModulesTab*>()) tab->share(id);
        });
        return;
    }
    case NotificationPlace::Club: {
        const QString slug = place.id;
        whenLibraryOn(server, [this, slug] {
            showServerWindow(static_cast<int>(ServerWindow::Tab::Layouts));
            if (serverWindow_) serverWindow_->showClub(slug);
        });
        return;
    }
    case NotificationPlace::Clubs:
        whenLibraryOn(server, [this] { showServerWindow(static_cast<int>(ServerWindow::Tab::Layouts)); });
        return;
    }
}

}  // namespace bld::ui
