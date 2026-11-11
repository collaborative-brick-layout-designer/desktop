// Live layouts on a server (sync phase P4): File › Open from Server…'s
// way in, publishing, the live-layout state of the main window and the
// server's parts.

#include "MainWindow.h"

#include "ActiveSheet.h"
#include "LayerPanel.h"
#include "LiveLayout.h"
#include "LoadingCard.h"
#include "NoticeArea.h"
#include "core/Version.h"
#include "MapView.h"
#include "MapViewInternal.h"

#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SidecarIO.h"
#include "ModulesPanel.h"
#include "VenueLibraryPanel.h"

#include "CompareDialog.h"
#include "ConnectDialog.h"
#include "LayoutMerge.h"
#include "PartsUpload.h"
#include "PartsSync.h"
#include "ServerApi.h"
#include "PartsBrowser.h"
#include "ServerList.h"
#include "CatalogLink.h"
#include "ServersDialog.h"
#include "ServerWindow.h"
#include "TokenStore.h"
#include "PrefsSync.h"
#include "theme/AppPrefs.h"
#include "UploadPartsDialog.h"

#include <QAction>
#include <QBuffer>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QMessageBox>
#include <QToolButton>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <utility>

namespace bld::ui {

void MainWindow::setupLiveMenu(QMenu* file) {
    live_ = new LiveLayout(*mapView_, this);
    liveStatus_ = new QLabel(this);
    liveStatus_->setVisible(false);
    statusBar()->addPermanentWidget(liveStatus_);
    // Who else is here (cursors alone miss anyone with the mouse off the map,
    // or on a phone): "People here (2)", listing them.
    peopleHere_ = new QToolButton(this);
    peopleHere_->setObjectName(QStringLiteral("peopleHere"));
    peopleHere_->setAutoRaise(true);
    peopleHere_->setPopupMode(QToolButton::InstantPopup);
    peopleHere_->setToolTip(tr("Everyone else who has this layout open now"));
    peopleHere_->setMenu(new QMenu(peopleHere_));
    peopleHere_->setVisible(false);
    statusBar()->addPermanentWidget(peopleHere_);
    connect(live_, &LiveLayout::peopleChanged, this, &MainWindow::updatePeopleHere);

    file->addSeparator();
    auto* serversAct = file->addAction(tr("S&ervers…"));
    serversAct->setObjectName(QStringLiteral("manageServers"));
    serversAct->setToolTip(tr("Your servers: add one, sign in, or pick your Main one"));
    connect(serversAct, &QAction::triggered, this, &MainWindow::onManageServers);
    // Everything on the server in one window (it replaced Connect to Server…
    // and Download Venues from Server…).
    auto* openFromServer = file->addAction(tr("Open &from Server…"));
    openFromServer->setObjectName(QStringLiteral("file.openFromServer"));
    openFromServer->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    openFromServer->setToolTip(tr("Your layouts, venues, modules and parts on the server, and your clubs'"));
    connect(openFromServer, &QAction::triggered, this, [this] { showServerWindow(); });
    // The other way: the open layout onto a server, yours or a club's (it replaced Publish to Server…).
    auto* saveToServer = file->addAction(tr("Save &to Server…"));
    saveToServer->setObjectName(QStringLiteral("file.saveToServer"));
    saveToServer->setToolTip(tr("Put this layout on a server, yours or a club's, and keep editing it live"));
    connect(saveToServer, &QAction::triggered, this, &MainWindow::onPublishToServer);
    disconnectAct_ = file->addAction(tr("Dis&connect"));
    disconnectAct_->setEnabled(false);
    connect(disconnectAct_, &QAction::triggered, this, &MainWindow::onDisconnect);
    downloadPartsAct_ = file->addAction(tr("Do&wnload Server Parts Again"));
    downloadPartsAct_->setToolTip(tr("Fetch the live layout's server parts that are missing or changed"));
    downloadPartsAct_->setEnabled(false);
    connect(downloadPartsAct_, &QAction::triggered, this, [this] { syncServerParts(liveServer_, liveToken_); });
    // Back in the app during a live session: someone may have uploaded parts meanwhile.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive || !live_->active() || partsSyncRunning_) return;
        if (lastPartsSyncServer_ != liveServer_) return;
        if (!sync::partsRecheckDue(lastPartsSyncMs_, QDateTime::currentMSecsSinceEpoch())) return;
        syncServerParts(liveServer_, liveToken_);
    });
    partsSyncFailed_ = new QToolButton(this);
    partsSyncFailed_->setObjectName(QStringLiteral("partsSyncFailed"));
    partsSyncFailed_->setAutoRaise(true);
    partsSyncFailed_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    partsSyncFailed_->setVisible(false);
    connect(partsSyncFailed_, &QToolButton::clicked, this, [this] {
        if (live_->active()) syncServerParts(liveServer_, liveToken_);
    });
    statusBar()->addPermanentWidget(partsSyncFailed_);
    partsDownloadCard_ = new LoadingCard(mapView_, LoadingCard::Place::Bottom);
    partsDownloadCard_->setObjectName(QStringLiteral("partsDownloadCard"));
    connect(partsDownloadCard_, &QObject::destroyed, this, [this] { partsDownloadCard_ = nullptr; });
    reviewOfflineAct_ = file->addAction(tr("Review Off&line Changes…"));
    reviewOfflineAct_->setToolTip(tr("Compare what you changed offline with the server's layout"));
    reviewOfflineAct_->setEnabled(false);
    connect(reviewOfflineAct_, &QAction::triggered, this, &MainWindow::onReviewOfflineEdits);

    // While live, Undo / Redo revert this desktop's own edits on the server.
    liveUndoAct_ = new QAction(tr("&Undo"), this);
    liveUndoAct_->setShortcut(QKeySequence::Undo);
    liveUndoAct_->setVisible(false);
    connect(liveUndoAct_, &QAction::triggered, this, [this] { live_->undo(); });
    liveRedoAct_ = new QAction(tr("&Redo"), this);
    liveRedoAct_->setShortcut(QKeySequence::Redo);
    liveRedoAct_->setVisible(false);
    connect(liveRedoAct_, &QAction::triggered, this, [this] { live_->redo(); });

    connect(live_, &LiveLayout::mapReloaded, this, &MainWindow::onLiveReloaded);
    connect(live_, &LiveLayout::statusTextChanged, this, [this] { updateLiveUi(); });
    connect(live_, &LiveLayout::undoStateChanged, this, [this] { updateLiveUi(); });
    connect(live_, &LiveLayout::nothingToUndo, this, [this](bool undo) {
        notices_->showNotice(QStringLiteral("live-undo"), undo ? tr("Nothing to undo") : tr("Nothing to redo"),
                             undo ? tr("Someone else changed that after you, so there was nothing to undo.")
                                  : tr("Someone else changed that after you, so there was nothing to redo."),
                             { NoticeAction{ tr("OK"), {} } });
    });
    // Someone else ungrouped or deleted the module being edited here: say
    // why Edit module closed (it used to close without a word).
    connect(live_, &LiveLayout::editedModuleGone, this, [this](const QString& name) {
        notices_->showNotice(
            QStringLiteral("live-module-gone"), tr("Edit module closed"),
            name.isEmpty() ? tr("The module you were editing isn't a module any more: someone ungrouped or deleted "
                                "it. You're back in the whole layout.")
                           : tr("“%1” isn't a module any more: someone ungrouped or deleted it. You're back in the "
                                "whole layout.")
                                 .arg(name),
            { NoticeAction{ tr("OK"), {} } });
    });
    connect(live_, &LiveLayout::offlineEditsReady, this, &MainWindow::onReviewOfflineEdits);
    // Offline edits nothing clashed with went by themselves (area 4, Bugs 5
    // and 6); the clashes, if any, wait for review.
    connect(live_, &LiveLayout::offlineEditsSent, this, [this](int sent, int clashesLeft) {
        QString text = tr("Your %n offline change(s) were sent.", nullptr, sent);
        QList<NoticeAction> actions;
        if (clashesLeft > 0) {
            text += QLatin1Char(' ')
                    + tr("Waiting for review: %n change(s) clashing with someone else's.", nullptr, clashesLeft);
            actions << NoticeAction{ tr("Review now"),
                                     [this] { QTimer::singleShot(0, this, &MainWindow::onReviewOfflineEdits); }, true };
        }
        actions << NoticeAction{ tr("OK"), {} };
        notices_->showNotice(QStringLiteral("live-offline-sent"),
                             clashesLeft > 0 ? tr("Your changes were sent") : tr("You're live again"), text, actions);
        updateLiveUi();
    });
    connect(live_, &LiveLayout::editsNotTaken, this, [this] {
        // Said here instead of "You can only view this layout now".
        notices_->hideNotice(QStringLiteral("live-access"));
        notices_->showNotice(
            QStringLiteral("live-not-taken"), tr("Your latest changes weren't taken"),
            tr("Your access to “%1” changed to view only before your latest changes reached the server, so it "
               "didn't take them. You're still live, viewing it. Nothing is lost: save your changes as a new "
               "layout, or let them go.")
                .arg(live_->title()),
            { NoticeAction{ tr("Save My Changes as a New Layout"),
                            [this] {
                                QTimer::singleShot(0, this, [this] {
                                    if (!live_->active() || !live_->session().notTaken()) return;
                                    const auto kept = *live_->session().notTaken();
                                    saveOfflineAsNew(kept);
                                });
                            },
                            true },
              NoticeAction{ tr("Let Them Go"), [this] { live_->session().forgetNotTaken(); } } },
            {}, true);
    });
    // Queued: after the edit's own signal handling, not inside it.
    connect(live_, &LiveLayout::localEdited, this, &MainWindow::offerPlacedParts, Qt::QueuedConnection);
    liveRoleCheck_ = new QTimer(this);
    liveRoleCheck_->setSingleShot(true);
    liveRoleCheck_->setInterval(300);
    connect(liveRoleCheck_, &QTimer::timeout, this, &MainWindow::checkLiveRole);
    liveRolePoll_ = new QTimer(this);
    liveRolePoll_->setInterval(60000);
    connect(liveRolePoll_, &QTimer::timeout, liveRoleCheck_, qOverload<>(&QTimer::start));
    // Back in step (a reconnect): what we may do may have changed meanwhile.
    // Away from it (a dropped connection): ask too, so a sign-in that ran
    // out (the socket then can't connect at all) is noticed at once.
    connect(&live_->session(), &sync::SyncSession::statusChanged, this, [this](sync::SyncClient::Status s) {
        if ((s == sync::SyncClient::Status::Synced || s == sync::SyncClient::Status::Connecting) && live_->active())
            liveRoleCheck_->start();
        // The server's parts couldn't be had and the connection dropped
        // (often the same Wi-Fi drop): back in step, try again by itself.
        if (s == sync::SyncClient::Status::Connecting && !partsSyncFailed_->isHidden())
            partsRetryOnReconnect_ = true;
        if (s == sync::SyncClient::Status::Synced && std::exchange(partsRetryOnReconnect_, false)
            && live_->active() && !partsSyncFailed_->isHidden() && !partsSyncRunning_)
            syncServerParts(liveServer_, liveToken_);
        updatePeopleHere();
    });
    // The server ended the sign-in while live: the layout stays open, and the
    // notice offers to sign in again; after that it reconnects and sends the
    // edits made meanwhile (they come back for review, as after offline).
    connect(live_, &LiveLayout::signInEnded, this, [this] {
        // Asking the server with the ended sign-in would only be refused again.
        liveRolePoll_->stop();
        liveSignInNeeded();
    });
    connect(live_, &LiveLayout::ended, this, [this](const QString& message, const QString& detail) {
        keepLiveSource();
        if (prefsSync_) prefsSync_->stop();
        liveRolePoll_->stop();
        notices_->hideNotice(QStringLiteral("live-access"));
        notices_->hideNotice(QStringLiteral("live-signin"));
        // The copy left here is the person's work and no longer on the
        // server: unsaved until they save it, so closing or opening another
        // layout asks first.
        mapView_->undoStack()->resetClean();
        updateLiveUi();
        // One plain message per ending (sync::liveCloseText); the server's
        // own code only as details, for an ending this version doesn't know.
        QMessageBox box(QMessageBox::Information, tr("Live layout closed"), message, QMessageBox::Ok, this);
        if (!detail.isEmpty()) box.setDetailedText(detail);
        box.exec();
    });
}

void MainWindow::offerLayoutSource(const import::LayoutSource& source) {
    const QString id = QStringLiteral("layoutSource");
    const QUrl server(source.server);
    // One whole sentence per case: "a layout" on its own can't be translated into a sentence.
    const QString title = source.title;
    const QString keep = tr("Keep working on this copy");
    const sync::ServerList servers = sync::ServerList::load();
    if (const sync::ServerEntry* entry = servers.find(server)) {
        notices_->showNotice(
            id, tr("This layout is on %1").arg(entry->label()),
            title.isEmpty() ? tr("This file is a copy of a layout. Open the live version to work on it with everyone, "
                                 "or keep working on this copy.")
                            : tr("This file is a copy of “%1”. Open the live version to work on it with everyone, or keep "
                                 "working on this copy.")
                                  .arg(title),
            { { tr("Open the live version"),
                // After the click has returned: the connect dialog runs its own loop.
                [this, source] { QTimer::singleShot(0, this, [this, source] { openSourceLive(source); }); }, true },
              { keep, {} } });
        return;
    }
    const QString host = server.port() > 0 ? QStringLiteral("%1:%2").arg(server.host()).arg(server.port()) : server.host();
    notices_->showNotice(
        id, tr("This layout came from %1").arg(host),
        title.isEmpty() ? tr("This file is a copy of a layout, from a server that isn't one of yours. Add the server to "
                             "open the live version, or keep working on this copy.")
                        : tr("This file is a copy of “%1”, from a server that isn't one of yours. Add the server to open the "
                             "live version, or keep working on this copy.")
                              .arg(title),
        { { tr("Add %1 to your servers").arg(host),
            [this, source] {
                sync::ServerList list = sync::ServerList::load();
                list.add(QUrl(source.server));
                list.save();
                // Now it is one of yours: offer the live version (once this card's click returns).
                QTimer::singleShot(0, this, [this, source] { offerLayoutSource(source); });
            },
            true, false },
          { keep, {} } });
}

void MainWindow::openSourceLive(const import::LayoutSource& source) {
    if (!maybeSave()) return;
    sync::ServerApi api;
    sync::ConnectDialog dialog(api, *tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this);
    dialog.openRecent(QUrl(source.server), source.layoutId);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto chosen = dialog.result();
    if (!chosen) return;
    openLive(*chosen);
}

void MainWindow::onManageServers() {
    const QHash<QString, QString> namesBefore = serverPartLabels();
    // Whether the live layout's sign-in is one the keychain keeps (one it
    // couldn't keep is only in memory, and isn't "signed out" for not being there).
    const auto kept = std::make_shared<bool>(false);
    if (live_->active()) tokens_->load(liveServer_, [kept](const QString& token) { *kept = !token.isEmpty(); });
    sync::ServersDialog dialog(*tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this);
    dialog.exec();
    // A renamed live server shows its new name; a new or signed-in one
    // fills the Module library and the status bar.
    updateLiveUi();
    updateLibraryServer();
    // Open Recent, the Server window's list and the parts' tooltips name the servers too.
    rebuildRecentMenu();
    if (serverWindow_) serverWindow_->refreshServers();
    if (partsBrowser_ && serverPartLabels() != namesBefore) partsBrowser_->rebuild();
    afterServersChanged(*kept);
}

void MainWindow::afterServersChanged(bool liveSignInWasKept) {
    if (!live_->active()) return;
    // Another server may be Main now, or Main signed out of: settings follow that.
    startPrefsSync({ liveServer_, liveToken_, liveLayoutId_, live_->title(), live_->readOnly(), liveInfo_ });
    // Signed out of the live layout's server (or it was removed): the layout
    // can't stay live on a sign-in that was given up.
    if (!liveSignInWasKept) return;
    tokens_->load(liveServer_, [this, alive = std::weak_ptr<int>(alive_), server = liveServer_,
                                was = liveToken_](const QString& token) {
        if (alive.expired() || !live_->active() || server != liveServer_ || was != liveToken_) return;
        if (token == liveToken_) return;
        // Signed in again there meanwhile: live goes on with the new sign-in.
        if (!token.isEmpty()) {
            resumeLive(token);
            return;
        }
        const QString name = liveServerName();
        keepLiveSource();
        live_->close();
        liveToken_.clear();
        liveRolePoll_->stop();
        notices_->hideNotice(QStringLiteral("live-access"));
        notices_->hideNotice(QStringLiteral("live-signin"));
        mapView_->hideOpening();
        if (partsDownloadCard_) partsDownloadCard_->finish();
        if (prefsSync_) prefsSync_->stop();
        // The copy is the person's work and no longer on the server: unsaved.
        mapView_->undoStack()->resetClean();
        updateLiveUi();
        updateLibraryServer();
        notices_->showNotice(QStringLiteral("live-signed-out"), tr("This layout is no longer live"),
                             tr("You’re no longer signed in to %1, so your changes don’t reach the server any more. The "
                                "layout stays open here as an unsaved copy. To keep it, save it (File > Save As).")
                                 .arg(name),
                             { NoticeAction{ tr("OK"), {} } });
    });
}

void MainWindow::onPublishToServer() {
    auto* m = mapView_->currentMap();
    if (!m) return;
    if (live_->active()) {
        QMessageBox::information(this, tr("Save to Server"),
                                 tr("This layout is already live on a server."));
        return;
    }
    QBuffer bbm;
    bbm.open(QIODevice::WriteOnly);
    if (!saveload::writeBbm(*m, bbm).ok) {
        QMessageBox::warning(this, tr("Save to Server"), tr("Could not write the layout to send it."));
        return;
    }
    const QByteArray sidecar =
        m->sidecar.isEmpty()
            ? QByteArray()
            : QJsonDocument(saveload::sidecarToJson(m->sidecar)).toJson(QJsonDocument::Compact);
    const QString title = currentFilePath_.isEmpty() ? (m->event.isEmpty() ? tr("Untitled Layout") : m->event)
                                                     : QFileInfo(currentFilePath_).completeBaseName();
    sync::ServerApi api;
    sync::ConnectDialog dialog(
        api, *tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this,
        sync::ConnectDialog::Purpose::Publish);
    dialog.setPublishContent(bbm.data(), sidecar, title);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto published = dialog.result();
    if (!published) return;
    // The published layout is now the live one; the local file stays as it was.
    openLiveFromHere(*published);
    statusBar()->showMessage(tr("Saved “%1” on %2").arg(published->title, liveServerName()),
                             5000);
    // Parts of yours the server lacks would show as missing there: offer them.
    offerPartsUpload(true);
}

QString MainWindow::otherServerRefusal(const QStringList& partKeys, int what) const {
    if (!live_ || !live_->active()) return {};
    const QString here = sync::ServerList::folderName(liveServer_);
    QStringList pending = partKeys;
    QSet<QString> seen;
    while (!pending.isEmpty()) {
        QString key = pending.takeFirst();
        if (seen.contains(key.toUpper())) continue;
        seen.insert(key.toUpper());
        // (The library knows a set by its file name with ".set".)
        auto meta = parts_.metadata(key);
        if (!meta && !key.endsWith(QStringLiteral(".set"), Qt::CaseInsensitive)) {
            key += QStringLiteral(".set");
            meta = parts_.metadata(key);
        }
        if (!meta) continue;
        if (const QString folder = otherServerOf(parts_, key, here); !folder.isEmpty()) {
            const QString server = serverPartLabels().value(folder, folder);
            switch (what) {
            case MapView::PlacingModule:
                return tr("This module has parts that are on another server (%1). It can’t go in this live layout, "
                          "because nobody else here could see them.")
                    .arg(server);
            case MapView::PlacingPaste:
                return tr("Some of the copied parts are on another server (%1). They can’t go in this live layout, "
                          "because nobody else here could see them.")
                    .arg(server);
            default:
                return otherServerReason(server);
            }
        }
        // A set: its own parts too.
        for (const parts::PartSubPart& sub : meta->subparts) pending << sub.subKey;
    }
    return {};
}

void MainWindow::offerPartsUpload(bool quiet) {
    if (!live_->active() || liveToken_.isEmpty()) return;
    // Only your own parts this layout uses, unticked: never every part of a
    // Download Center pack, and nothing goes up unless you tick it.
    const QStringList used = ownPartsInLayout();
    if (used.isEmpty()) return;
    uploadPartsTo(liveServer_, liveToken_, quiet, used, false);
}

void MainWindow::openLive(const sync::ConnectResult& r) {
    liveSheet_ = {};
    if (const core::Map* m = mapView_->currentMap(); m && carrySheetIntoLive_)
        if (const int i = placementLayer(*m); i >= 0) liveSheet_ = { i, m->layers()[i]->name };
    carrySheetIntoLive_ = false;
    connect(layerPanel_, &LayerPanel::activeLayerChanged, this, &MainWindow::rememberLiveSheet, Qt::UniqueConnection);
    // The live layout is now what's open; the file it may have come from isn't.
    forgetFileSource();
    liveServer_ = r.server;
    liveToken_ = r.token;
    liveLayoutId_ = r.layoutId;
    liveInfo_ = r.info;
    sync::ServerApi api;
    if (live_->active()) live_->close();
    api.setBase(r.server);
    currentFilePath_.clear();
    clearActiveView();
    // The layout and any offline edits are kept per server and layout.
    // The folder is named after the id when it is in the server's format (a
    // UUID, as every layout id the server makes is), else after its hash.
    static const QRegularExpression uuidId(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    const QString idFolder = uuidId.match(r.layoutId).hasMatch()
                                 ? r.layoutId
                                 : QString::fromLatin1(QCryptographicHash::hash(r.layoutId.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/live/") + sync::ServerList::folderName(r.server) + QLatin1Char('/')
                             + idFolder;
    // One of your servers, with this layout first among its recent ones.
    {
        sync::ServerList servers = sync::ServerList::load();
        servers.touch(r.server);
        servers.remember(r.server, r.info);
        servers.addRecent(r.server, { r.layoutId, r.title, r.readOnly, {} });
        servers.save();
    }
    mapView_->showOpening(tr("Getting the layout from the server."));
    live_->open(api.layoutSocketUrl(r.layoutId), r.token, r.readOnly, r.title, cacheDir);
    // Name and color our cursor as the web does, once we know who we are.
    auto* who = new sync::ServerApi(this);
    who->setBase(r.server);
    who->setToken(r.token);
    connect(who, &sync::ServerApi::currentUserReady, this,
            [this, who, layout = r.layoutId, server = r.server](const QString& id, const QString& name) {
                // Our name and color on this layout, unless another opened meanwhile.
                if (live_->active() && server == liveServer_ && layout == liveLayoutId_) live_->setUser(id, name, layout);
                sync::ServerList servers = sync::ServerList::load();
                servers.rememberUser(server, name);
                servers.rememberSignIn(server, who->tokenLifetime());
                servers.save();
                who->deleteLater();
            });
    connect(who, &sync::ServerApi::requestFailed, who, &QObject::deleteLater);
    who->fetchCurrentUser();
    // Only what the server has (servers that don't say: everything, as before).
    if (r.info.has(QStringLiteral("partsManifest"))) syncServerParts(r.server, r.token);
    liveAskedParts_.clear();
    liveLayoutSeen_ = false;
    liveLocalParts_.clear();
    // Your own parts: the imported ones and your library folders, not the
    // bundled library or the folders of server parts.
    liveLocalParts_ = scanOwnParts();
    // Sets only to a server that takes them.
    if (!r.info.has(QStringLiteral("uploadSets")))
        liveLocalParts_.removeIf([](const sync::LocalPart& p) { return p.isSet; });
    loadLivePartsCatalog();
    // Settings follow the account on this server while connected.
    if (!prefsSync_) {
        prefsSync_ = new PrefsSync(theme::PrefsStore::instance(), this);
        connect(prefsSync_, &PrefsSync::failed, this, [this](const QString&, bool unauthorized) {
            if (unauthorized)
                statusBar()->showMessage(
                    tr("Your settings didn't sync with the server. Sign in again to sync them."), 6000);
        });
    }
    startPrefsSync(r);
    showServerNotices(r.info);
    notices_->hideNotice(QStringLiteral("live-access"));
    notices_->hideNotice(QStringLiteral("live-signin"));
    notices_->hideNotice(QStringLiteral("live-signed-out"));
    liveRolePoll_->start();
    // The Server window and the Module library show the live layout's server.
    browseServer_ = r.server;
    updateLibraryServer();
    updateLiveUi();
}

void MainWindow::checkLiveRole() {
    if (!live_->active() || liveToken_.isEmpty() || liveLayoutId_.isEmpty()) return;
    auto* api = new sync::ServerApi(this);
    api->setBase(liveServer_);
    api->setToken(liveToken_);
    connect(api, &sync::ServerApi::layoutRoleReady, this,
            [this, api, server = liveServer_](const QString& layoutId, const QString& role) {
                api->deleteLater();
                // Still the same live layout.
                if (live_->active() && server == liveServer_ && layoutId == liveLayoutId_) applyLiveRole(role);
            });
    connect(api, &sync::ServerApi::requestFailed, this,
            [this, api, server = liveServer_, token = liveToken_](const QString&, const QString&, bool unauthorized) {
                api->deleteLater();
                // Refused for a sign-in replaced meanwhile (signed in again): not this one.
                if (unauthorized && live_->active() && server == liveServer_ && token == liveToken_) liveSignInNeeded();
            });
    api->fetchLayoutRole(liveLayoutId_);
}

void MainWindow::liveSignInNeeded() {
    notices_->showNotice(
        QStringLiteral("live-signin"), tr("Sign in again to keep working live"),
        tr("You were signed out of %1 (on the website or another device), or your sign-in there ran out, so your "
           "changes can't reach the server. They're kept here, and sent once you sign in again.")
            .arg(liveServerName()),
        { NoticeAction{ tr("Sign In…"),
                        // Not inside the notice's own click.
                        [this] { QTimer::singleShot(0, this, [this] { signInToLiveServer(); }); }, true } });
}

void MainWindow::signInToLiveServer() {
    if (!live_->active()) return;
    const QUrl url = liveServer_;
    sync::ServerApi api;
    sync::ConnectDialog dialog(api, *tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this,
                               sync::ConnectDialog::Purpose::SignIn);
    dialog.setAddress(url.toString());
    QTimer::singleShot(0, &dialog, &sync::ConnectDialog::connectToServer);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto signedIn = dialog.result();
    if (signedIn && live_->active() && url == liveServer_) resumeLive(signedIn->token);
}

void MainWindow::resumeLive(const QString& token) {
    if (!live_->active() || token.isEmpty()) return;
    openLive({ liveServer_, token, liveLayoutId_, live_->title(), live_->readOnly(), liveInfo_ });
}

void MainWindow::applyLiveRole(const QString& role) {
    // Not yours to open any more: the server closes the session itself.
    if (role.isEmpty()) return;
    const bool readOnly = role == QLatin1String("viewer");
    if (readOnly == live_->readOnly()) return;
    live_->setReadOnly(readOnly);
    {
        sync::ServerList servers = sync::ServerList::load();
        servers.addRecent(liveServer_, { liveLayoutId_, live_->title(), readOnly, {} });
        servers.save();
    }
    if (readOnly)
        notices_->showNotice(
            QStringLiteral("live-access"), tr("You can only view this layout now"),
            tr("Your access to “%1” changed to view only, so changes can't be made here. Ask its owner, or a club "
               "admin, if you need to edit it.")
                .arg(live_->title()),
            { NoticeAction{ tr("OK"), {} } });
    else
        notices_->showNotice(QStringLiteral("live-access"), tr("You can edit this layout now"),
                             tr("Your access to “%1” changed: your changes go to everyone again.").arg(live_->title()),
                             { NoticeAction{ tr("OK"), {} } });
    updateLiveUi();
}

void MainWindow::startPrefsSync(const sync::ConnectResult& r) {
    const sync::ServerList servers = sync::ServerList::load();
    const QUrl account = servers.settingsAccount();
    if (account.isEmpty() || sync::TokenStore::keyFor(account) == sync::TokenStore::keyFor(r.server)) {
        // Live on the Main server (or the only one): its token, as before.
        if (r.info.has(QStringLiteral("preferences"))) prefsSync_->start(r.server, r.token);
        else prefsSync_->stop();
        return;
    }
    // Live elsewhere: settings still follow Main, with Main's own token
    // (never this server's), when signed in there and it keeps settings.
    prefsSync_->stop();
    const sync::ServerEntry* main = servers.find(account);
    if (main && main->features && !main->features->contains(QStringLiteral("preferences"))) return;
    tokens_->load(account, [this, alive = std::weak_ptr<int>(alive_), account](const QString& token) {
        // The window still here, still live, and nothing started meanwhile.
        if (alive.expired() || token.isEmpty() || !live_->active() || prefsSync_->active()) return;
        prefsSync_->start(account, token);
    });
}

void MainWindow::watchTokenSaves() {
    if (!tokens_) return;
    tokens_->onSaveFailed([this, alive = std::weak_ptr<int>(alive_)](const QUrl& server, const QString& reason) {
        if (alive.expired() || !notices_) return;
        const sync::ServerList servers = sync::ServerList::load();
        const sync::ServerEntry* e = servers.find(server);
        const QString name = e ? e->label() : server.host();
        notices_->showNotice(
            QStringLiteral("keychain"), tr("Your sign-in wasn't saved"),
            tr("This computer's keychain couldn't keep your sign-in to %1, so you'll be asked to sign in "
               "again the next time the app starts. On Linux, a keyring such as GNOME Keyring or KWallet "
               "has to be running and unlocked.")
                    .arg(name)
                + QStringLiteral("\n\n") + tr("The keychain said: %1").arg(reason),
            { { tr("OK"), {}, true } });
    });
}

QString MainWindow::liveServerName() const {
    const sync::ServerList servers = sync::ServerList::load();
    const sync::ServerEntry* e = servers.find(liveServer_);
    return e ? e->label() : liveServer_.host();
}

void MainWindow::showServerNotices(const sync::ServerInfo& info) {
    const QString host = liveServerName();
    const QString mine = QCoreApplication::applicationVersion();
    if (info.standing(mine) == sync::Standing::UpdateSuggested) {
        const QUrl url(info.downloadUrl.isEmpty() ? core::desktopDownloadUrl() : info.downloadUrl);
        notices_->showNotice(
            QStringLiteral("server-update"), tr("Please update Collaborative Brick Layout Designer"),
            tr("%1 works best with version %2 or newer, and you have %3. Everything still works for now.")
                .arg(host, info.desktopRecommended, mine),
            { NoticeAction{ tr("Download"), [url] { QDesktopServices::openUrl(url); }, true },
              NoticeAction{ tr("Later"), {} } });
    } else {
        notices_->hideNotice(QStringLiteral("server-update"));
    }
    const QStringList missing = info.missing();
    if (missing.isEmpty()) {
        notices_->hideNotice(QStringLiteral("server-features"));
        return;
    }
    QStringList names;
    for (const QString& f : missing) names << QStringLiteral("• ") + sync::featureLabel(f);
    notices_->showNotice(
        QStringLiteral("server-features"),
        info.features ? tr("This server can't do everything yet") : tr("This server may not do everything yet"),
        (info.features ? tr("%1 is running an older version, so these aren't available there:").arg(host)
                       : tr("%1 hasn't been updated in a while, so these may not work there:").arg(host)) +
            QLatin1Char('\n') + names.join(QLatin1Char('\n')) + QLatin1Char('\n') +
            tr("Everything else works as usual. Whoever runs the server can update it."),
        { NoticeAction{ tr("OK"), {} } });
}

void MainWindow::loadLivePartsCatalog() {
    liveCatalogReady_ = false;
    liveServerParts_.clear();
    if (!live_->active() || liveToken_.isEmpty() || liveLocalParts_.isEmpty()) return;
    if (!liveInfo_.has(QStringLiteral("uploadParts"))) return;  // nothing to offer on such a server
    auto* catalog = new sync::PartsUpload(liveServer_, liveToken_, this);
    const QUrl server = liveServer_;
    connect(catalog, &sync::PartsUpload::catalogReady, this, [this, catalog, server](const QSet<QString>& known) {
        catalog->deleteLater();
        if (server != liveServer_) return; // another layout opened meanwhile
        liveServerParts_ = known;
        liveCatalogReady_ = true;
    });
    // Without the catalog nothing is offered; the Server window's Upload to server… still works.
    connect(catalog, &sync::PartsUpload::failed, catalog, &QObject::deleteLater);
    catalog->fetchCatalog();
}

void MainWindow::offerPlacedParts() {
    if (!live_->active() || live_->readOnly() || !liveCatalogReady_ || liveOfferOpen_) return;
    const core::Map* map = mapView_->currentMap();
    if (!map) return;
    const auto parts =
        sync::partsToOffer(*map, liveServerParts_, liveLocalParts_, liveAskedParts_, defaultVendoredPartsRoot());
    if (parts.isEmpty()) return;
    // Asked once per session, whatever the answer.
    QStringList names;
    for (const auto& p : parts) {
        liveAskedParts_.insert(p.key.toUpper());
        names << p.key;
    }
    auto* box = new QMessageBox(QMessageBox::Question, tr("Upload to Server"),
                                parts.size() == 1
                                    ? tr("%1 isn't on the server yet, so others see a missing part. Upload it?")
                                          .arg(names.first())
                                    : tr("%1 aren't on the server yet, so others see missing parts. Upload them?")
                                          .arg(names.join(QStringLiteral(", "))),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto* uploadBtn = box->addButton(tr("Upload…"), QMessageBox::AcceptRole);
    box->addButton(tr("Not Now"), QMessageBox::RejectRole);
    liveOfferOpen_ = true;
    connect(box, &QMessageBox::finished, this, [this, box, uploadBtn, parts] {
        liveOfferOpen_ = false;
        if (box->clickedButton() != uploadBtn || !live_->active()) return;
        // The box deletes itself on close: the upload dialog's own event loop
        // must not run inside its finished signal, or the box is destroyed
        // while it is still closing. Open it once the signal has returned.
        QTimer::singleShot(0, this, [this, parts] {
            if (!live_->active()) return;
            auto* upload = new sync::PartsUpload(liveServer_, liveToken_, this);
            upload->setServerTakesSets(liveInfo_.has(QStringLiteral("uploadSets")));
            sync::ServerApi api;
            api.setBase(liveServer_);
            api.setToken(liveToken_);
            sync::UploadPartsDialog dialog(api, *upload, parts, this);
            if (dialog.exec() == QDialog::Accepted) {
                statusBar()->showMessage(
                    tr("Uploaded %n part(s) to the server", nullptr, dialog.uploadedCount()), 5000);
                loadLivePartsCatalog();
            }
            upload->deleteLater();
        });
    });
    box->open();
}

void MainWindow::syncServerParts(const QUrl& server, const QString& token, const std::function<void()>& then) {
    if (partsSyncRunning_) {
        if (server == lastPartsSyncServer_) {
            if (then) then();
            return;
        }
        // Another server's parts (a layout opened there meanwhile): once
        // these are done, or they would never come.
        if (const auto replaced = std::exchange(partsSyncNext_, {})) replaced();
        partsSyncNext_ = [this, server, token, then] { syncServerParts(server, token, then); };
        return;
    }
    partsSyncRunning_ = true;
    lastPartsSyncMs_ = QDateTime::currentMSecsSinceEpoch();
    lastPartsSyncServer_ = server;
    partsSyncFailed_->setVisible(false);
    downloadPartsAct_->setEnabled(false);
    // Each server's parts in a folder of its own, never mixed with another's.
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/server-parts/") + sync::ServerList::folderName(server);
    auto* job = new sync::PartsSync(server, token, root, this);
    // The first connect fetches thousands of files: a card with a real
    // progress bar, like the web's, for as long as it takes.
    if (partsDownloadCard_) partsDownloadCard_->showBusy(tr("Checking the server's parts…"));
    const auto done = [this, then] {
        partsSyncRunning_ = false;
        if (partsDownloadCard_) partsDownloadCard_->finish();
        updateLiveUi();
        if (then) QTimer::singleShot(0, this, then);
        if (auto next = std::exchange(partsSyncNext_, {})) QTimer::singleShot(0, this, std::move(next));
    };
    connect(job, &sync::PartsSync::progress, this, [this](int done, int total) {
        if (partsDownloadCard_)
            partsDownloadCard_->showProgress(tr("Downloading server parts…"), done, total,
                                             tr("You can keep working while they download."));
    });
    connect(job, &sync::PartsSync::failed, this, [this, job, done](const QString& message, bool) {
        job->deleteLater();
        done();
        showPartsSyncFailed(tr("Couldn't get the server's parts"), { message });
    });
    connect(job, &sync::PartsSync::finished, this, [this, job, root, done](const sync::PartsSyncResult& r) {
        job->deleteLater();
        done();
        // The server's folder joins the library paths once, then the library reloads when anything came in.
        QStringList paths = loadUserLibraryPaths();
        const bool added = !paths.contains(root);
        if (added) {
            paths << root;
            saveUserLibraryPaths(paths);
        }
        if (added || r.downloaded > 0 || r.removed > 0) onReloadLibrary();
        if (r.failed.isEmpty()) {
            statusBar()->showMessage(
                tr("Server parts up to date (%n file(s) downloaded)", nullptr, r.downloaded), 6000);
        } else {
            showPartsSyncFailed(tr("%n server part file(s) didn't download", nullptr, static_cast<int>(r.failed.size())),
                                r.failed);
        }
    });
    job->start();
}

void MainWindow::showPartsSyncFailed(const QString& summary, const QStringList& details) {
    // Stays until the next try: parts that didn't come draw as outlines.
    partsSyncFailed_->setText(QStringLiteral("⚠ ") + summary + QStringLiteral(" · ") + tr("Try again"));
    QStringList shown = details.mid(0, 8);
    if (details.size() > shown.size()) shown << tr("…and %n more", nullptr, static_cast<int>(details.size() - shown.size()));
    partsSyncFailed_->setToolTip(tr("Parts the server has but this computer doesn't show as outlines.\n\n")
                                 + shown.join(QLatin1Char('\n')));
    partsSyncFailed_->setVisible(live_->active());
}

void MainWindow::keepLiveSource() {
    if (liveLayoutId_.isEmpty() || !liveServer_.isValid()) return;
    fileSource_ = import::LayoutSource{ import::layoutServerBase(liveServer_.toString()), liveLayoutId_, live_->title(),
                                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) };
}

void MainWindow::onDisconnect() {
    // The copy left here came from that layout: a file saved from it says so.
    if (live_->active()) keepLiveSource();
    live_->close();
    liveRolePoll_->stop();
    notices_->hideNotice(QStringLiteral("live-access"));
    notices_->hideNotice(QStringLiteral("live-signin"));
    mapView_->hideOpening();
    if (partsDownloadCard_) partsDownloadCard_->finish();
    if (prefsSync_) prefsSync_->stop();
    updateLiveUi();
    statusBar()->showMessage(tr("Disconnected. The layout stays open here as an unsaved copy."), 5000);
}

void MainWindow::onLiveReloaded() {
    // Parts already in the layout as opened aren't "placed": File > Upload
    // My Parts covers those.
    if (!liveLayoutSeen_ && live_->active() && mapView_->currentMap()) {
        liveAskedParts_ |= sync::partNumbersIn(*mapView_->currentMap());
        liveLayoutSeen_ = true;
        // Sets kept as modules or as loose halves become sets, as when a
        // file opens; the change goes to the server like any edit (after this
        // reload, not inside it). Once they are groups, nothing is found again.
        if (!live_->readOnly())
            QTimer::singleShot(0, this, [this] {
                if (live_->active()) makeSetsOfSetModules();
            });
    }
    const bool moved = keepLiveSheet();
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    // What names the active sheet ("Sheet: …" in the status bar) reads it again.
    if (moved) emit layerPanel_->activeLayerChanged(mapView_->currentMap()->selectedLayerIndex);
    modulesPanel_->setMap(mapView_->currentMap());
    updateTitle();
}

void MainWindow::rememberLiveSheet(int index) {
    const core::Map* m = mapView_->currentMap();
    if (!live_->active() || !m || index < 0 || index >= static_cast<int>(m->layers().size())) return;
    liveSheet_ = { index, m->layers()[index]->name };
}

bool MainWindow::keepLiveSheet() {
    core::Map* m = live_->active() ? mapView_->currentMap() : nullptr;
    if (!m) return false;
    const auto& layers = m->layers();
    const int n = static_cast<int>(layers.size());
    const auto takesParts = [&](int i) {
        return i >= 0 && i < n && layers[i] && layers[i]->kind() == core::LayerKind::Brick;
    };
    // The one chosen (here, or before going live), found again by its place
    // and name, else by name; else the rule a file opens by.
    int want = -1;
    if (!liveSheet_.name.isNull()) {
        if (takesParts(liveSheet_.index) && layers[liveSheet_.index]->name == liveSheet_.name) want = liveSheet_.index;
        for (int i = 0; i < n && want < 0; ++i)
            if (takesParts(i) && layers[i]->name == liveSheet_.name) want = i;
    }
    if (want < 0) want = startingSheet(*m);
    if (want < 0 || want == m->selectedLayerIndex) return false;
    // The shared copy keeps its own choice when this one is written back.
    const int saved = m->selectedLayerIndex;
    const core::Layer* l = saved >= 0 && saved < n ? layers[saved].get() : nullptr;
    m->fileSelection = core::Map::FileSelection{ saved, l, l ? l->guid : QString() };
    m->selectedLayerIndex = want;
    return true;
}

void MainWindow::updatePeopleHere() {
    const auto& people = live_->people();
    const bool on = live_->active() && !people.isEmpty();
    peopleHere_->setVisible(on);
    QMenu* menu = peopleHere_->menu();
    menu->clear();
    if (!on) return;
    peopleHere_->setText(tr("People here (%1)").arg(people.size()));
    for (const auto& person : people) {
        QPixmap dot(12, 12);
        dot.fill(Qt::transparent);
        QPainter p(&dot);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(person.color));
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, 10, 10);
        p.end();
        // (Enabled, so the dot keeps its color; choosing one does nothing.)
        menu->addAction(QIcon(dot), person.name);
    }
}

void MainWindow::updateLiveUi() {
    const bool on = live_->active();
    // Live: the parts list offers this server's parts, not other servers'.
    if (partsBrowser_) partsBrowser_->setLiveServerFolder(on ? sync::ServerList::folderName(liveServer_) : QString());
    liveStatus_->setVisible(on);
    updatePeopleHere();
    liveStatus_->setText(on ? tr("Live on %1: %2").arg(liveServerName(), live_->statusText()) : QString());
    updateServerStatus();
    disconnectAct_->setEnabled(on);
    downloadPartsAct_->setEnabled(on && !partsSyncRunning_);
    if (!on) partsSyncFailed_->setVisible(false);
    undoAct_->setVisible(!on);
    redoAct_->setVisible(!on);
    liveUndoAct_->setVisible(on);
    liveRedoAct_->setVisible(on);
    liveUndoAct_->setEnabled(on && live_->canUndo());
    liveRedoAct_->setEnabled(on && live_->canRedo());
    const bool waiting = on && live_->session().offlineEdits()
                         && live_->session().status() == sync::SyncClient::Status::Synced;
    reviewOfflineAct_->setEnabled(waiting);
    // Clashing edits waiting for review: everything else is live (others'
    // changes show, and edits that don't clash are sent). Said for as long
    // as that lasts (the status bar counts them too).
    const int n = live_->session().unsyncedEdits();
    const auto* comparing = findChild<sync::CompareDialog*>(QString(), Qt::FindDirectChildrenOnly);
    if (waiting && !(comparing && comparing->isVisible())) {
        if (liveReviewNotice_ == n) return updateTitle();
        liveReviewNotice_ = n;
        notices_->showNotice(QStringLiteral("live-review"), tr("Some of your changes wait for review"),
                             tr("Waiting for review: %n change(s) clashing with someone else's. Everything else is "
                                "live: you see what others change, and your other changes are sent.",
                                nullptr, n),
                             { NoticeAction{ tr("Review now"),
                                             [this] { QTimer::singleShot(0, this, &MainWindow::onReviewOfflineEdits); },
                                             true } });
    } else if (liveReviewNotice_ != -1) {
        liveReviewNotice_ = -1;
        notices_->hideNotice(QStringLiteral("live-review"));
    }
    updateTitle();
}

void MainWindow::onReviewOfflineEdits() {
    if (!live_->active()) return;
    // (Shown again afterwards if the edits still wait: Later.)
    notices_->hideNotice(QStringLiteral("live-review"));
    liveReviewNotice_ = -1;
    // One compare window at a time: a reconnect while it is open says
    // "back in step" again.
    if (findChild<sync::CompareDialog*>(QString(), Qt::FindDirectChildrenOnly)) return;
    auto& session = live_->session();
    const auto& off = session.offlineEdits();
    if (!off) return;
    const auto current = session.currentMap();
    if (!current) return;
    const auto server = sync::merge::snapshotOf(*current);
    const auto base = off->base, mine = off->mine;
    const auto changes = sync::merge::compareLayouts(base, mine, server);
    const bool anyMine = std::any_of(changes.cbegin(), changes.cend(), [](const sync::merge::ItemChange& c) {
        return c.mine != sync::merge::Side::Unchanged;
    });
    if (!anyMine) {
        session.resolveOffline(nullptr);
        statusBar()->showMessage(tr("Your offline edits left the layout as it was; nothing to apply."), 5000);
        updateLiveUi();
        return;
    }
    // Nothing clashes with what others changed meanwhile (a Wi-Fi drop of a
    // few seconds, say): sent as they are, as the web does, without asking.
    const bool clash = std::any_of(changes.cbegin(), changes.cend(), [](const sync::merge::ItemChange& c) {
        return c.status == sync::merge::Status::Conflict;
    });
    if (!clash) {
        QString error;
        const auto merged = sync::merge::mergeLayouts(mine, server, changes, {}, &error);
        if (merged) {
            const int n = static_cast<int>(std::count_if(changes.cbegin(), changes.cend(), [](const sync::merge::ItemChange& c) {
                return c.mine != sync::merge::Side::Unchanged;
            }));
            session.resolveOffline(merged.get());
            notices_->showNotice(QStringLiteral("live-offline-sent"), tr("You're live again"),
                                 tr("Your %n offline change(s) were sent.", nullptr, n), { NoticeAction{ tr("OK"), {} } });
            updateLiveUi();
            return;
        }
        // Couldn't be merged by itself: the window lets the person choose.
    }

    sync::CompareDialog dlg(changes, this);
    connect(&dlg, &sync::CompareDialog::highlight, this, [this](const sync::merge::ItemChange& c) {
        // brick:<layer>:<id>
        if (c.kind == QLatin1String("brick")) showBrick(c.key.section(QLatin1Char(':'), 2));
    });
    dlg.exec();
    // The session may have ended or resolved meanwhile.
    if (!live_->active() || !session.offlineEdits()) return;

    QString error;
    switch (dlg.action()) {
    case sync::CompareDialog::Action::Later:
        break;
    case sync::CompareDialog::Action::Apply: {
        // On the server's layout as it is now, so what others changed while
        // the window was open isn't undone (only where I chose mine).
        const auto now = session.currentMap(&error);
        if (!now) {
            QMessageBox::warning(this, tr("Review Offline Changes"),
                                 tr("Could not merge your changes: %1").arg(error));
            break;
        }
        const auto serverNow = sync::merge::snapshotOf(*now);
        const auto merged = sync::merge::mergeLayouts(
            mine, serverNow, sync::merge::compareLayouts(base, mine, serverNow), dlg.choices(), &error);
        if (!merged) {
            QMessageBox::warning(this, tr("Review Offline Changes"),
                                 tr("Could not merge your changes: %1").arg(error));
            break;
        }
        session.resolveOffline(merged.get());
        statusBar()->showMessage(tr("Your offline changes were applied to the live layout."), 5000);
        break;
    }
    case sync::CompareDialog::Action::Discard:
        session.resolveOffline(nullptr);
        statusBar()->showMessage(tr("Your offline changes were discarded."), 5000);
        break;
    case sync::CompareDialog::Action::ReplaceServer: {
        const auto map = sync::merge::mapOf(mine, &error);
        if (!map) {
            QMessageBox::warning(this, tr("Review Offline Changes"),
                                 tr("Could not read your version: %1").arg(error));
            break;
        }
        session.resolveOffline(map.get());
        statusBar()->showMessage(tr("Your version replaced the server's."), 5000);
        break;
    }
    case sync::CompareDialog::Action::SaveAsNew:
        saveOfflineAsNew(mine);
        break;
    }
    updateLiveUi();
}

void MainWindow::saveOfflineAsNew(const sync::merge::Snapshot& mine) {
    QString error;
    const auto map = sync::merge::mapOf(mine, &error);
    QBuffer bbm;
    bbm.open(QIODevice::WriteOnly);
    if (!map || !saveload::writeBbm(*map, bbm).ok) {
        QMessageBox::warning(this, tr("Save as a New Layout"),
                             tr("Could not write your version to publish it. Your offline changes are kept."));
        return;
    }
    const QByteArray sidecar =
        map->sidecar.isEmpty()
            ? QByteArray()
            : QJsonDocument(saveload::sidecarToJson(map->sidecar)).toJson(QJsonDocument::Compact);
    const QString title = tr("%1 (offline copy)").arg(live_->title());
    auto* api = new sync::ServerApi(this);
    api->setBase(liveServer_);
    api->setToken(liveToken_);
    const QUrl server = liveServer_;
    connect(api, &sync::ServerApi::published, this, [this, api, server](const QString&, const QString& t) {
        api->deleteLater();
        if (live_->active() && live_->session().offlineEdits()) live_->session().resolveOffline(nullptr);
        if (live_->active()) live_->session().forgetNotTaken();
        notices_->hideNotice(QStringLiteral("live-not-taken"));
        updateLiveUi();
        QMessageBox::information(this, tr("Save as a New Layout"),
                                 tr("Your version was saved as \"%1\" in your layouts on %2. The live layout "
                                    "stays as the server has it.")
                                     .arg(t, server.host()));
    });
    connect(api, &sync::ServerApi::requestFailed, this, [this, api](const QString&, const QString& message, bool) {
        api->deleteLater();
        QMessageBox::warning(this, tr("Save as a New Layout"),
                             tr("Could not publish your version: %1\nYour offline changes are kept; "
                                "File › Review Offline Changes brings them back.")
                                 .arg(message));
    });
    // Empty organisation: the user's own layouts.
    api->publishLayout(title, bbm.data(), sidecar, QString());
    statusBar()->showMessage(tr("Publishing your version as \"%1\"…").arg(title), 5000);
}

void MainWindow::showBrick(const QString& guid) {
    auto* scene = mapView_->scene();
    QGraphicsItem* found = nullptr;
    for (QGraphicsItem* it : scene->items())
        if (detail::isBrickItem(it) && it->data(detail::kBrickDataGuid).toString() == guid) {
            found = it;
            break;
        }
    if (!found) return;
    scene->clearSelection();
    found->setSelected(true);
    mapView_->centerOn(found);
}

} // namespace bld::ui

void bld::ui::MainWindow::openServerHomeOnWeb() {
    const sync::ServerList servers = sync::ServerList::load();
    const sync::ServerEntry* server = servers.lastUsed();
    if (!server) {
        QTimer::singleShot(0, this, [this] {
            QMessageBox::information(this, tr("Your modules on the web"),
                                     tr("Modules saved on the website are on your club's server. Connect to a "
                                        "server first (File › Servers…), then try again."));
        });
        return;
    }
    QDesktopServices::openUrl(serverHomeUrl(server->url));
}

void bld::ui::MainWindow::openCatalogOnWeb(bool parts) {
    const sync::ServerList servers = sync::ServerList::load();
    const sync::ServerEntry* server = servers.lastUsed();
    if (!server) {
        // Not from inside the click: a message box of its own, queued.
        QTimer::singleShot(0, this, [this] {
            QMessageBox::information(this, tr("Catalog"),
                                     tr("The catalog is on your club's server. Connect to a server first "
                                        "(File › Servers…), then try again."));
        });
        return;
    }
    QDesktopServices::openUrl(catalogWebUrl(server->url, parts));
}
