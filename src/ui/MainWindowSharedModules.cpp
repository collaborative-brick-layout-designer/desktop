// Shared (read-only) modules in the window (docs/shared-modules.md; the
// web's SharedModulesHost.tsx, sharedModules.ts and SharedModuleRow.tsx):
//   - what the layout's shared modules are on the server (asked again on a
//     live hint, when the Module library changes, and when the placed
//     copies change), and from it the notices over the map, the badges on
//     it and the Modules panel's lines;
//   - Update, Go back, Put back and Make it mine: the server's steps on a
//     live layout (one writer, so two people pressing Update get the parts
//     once), done here for a layout opened from a file;
//   - on opening: is each placed copy still its owner's version ("Changed
//     outside the app": Put back, or Make it mine)?
//   - placing one: shared when it may follow its owner in this layout,
//     else who to ask.

#include "MainWindow.h"

#include "ConfirmDialog.h"
#include "LiveLayout.h"
#include "MapView.h"
#include "ModulesPanel.h"
#include "NoticeArea.h"
#include "ServerLibrary.h"
#include "SharedModuleWords.h"
#include "SharedModulesBar.h"
#include "SharedUpdatesDialog.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/ModuleEdit.h"
#include "../edit/ModuleCommands.h"
#include "../edit/ModuleLibraryLink.h"
#include "../edit/SharedMarks.h"
#include "../edit/SharedModulePlacement.h"
#include "../parts/BrickPlacement.h"
#include "LayoutMerge.h"
#include "ModuleDoc.h"
#include "WhileAlive.h"

#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

namespace bld::ui {

namespace {

QString hostOf(const QString& url) { return QUrl(url).toString(QUrl::StripTrailingSlash); }

const core::Module* placedModule(const core::Map* map, const QString& id) {
    return map ? core::findModule(map->sidecar.modules, id) : nullptr;
}

QString nameOf(const core::SharedPlaced& p) { return p.name.isEmpty() ? MainWindow::tr("This module") : p.name; }

}  // namespace

void MainWindow::setupSharedModules() {
    sharedApi_ = new sync::LibraryApi(this);
    // Back online after offline edits: a shared module the server updated
    // meanwhile is the server's, with my offline move of it on top
    // (sync/LayoutMerge.h). Its parts move by their real geometry.
    sync::merge::setSharedMover([this](const QJsonObject& brick, const core::SharedPlacement& from, const core::SharedPlacement& to) {
        core::Brick b;
        b.partNumber = brick.value(QLatin1String("partNumber")).toString();
        const QJsonObject a = brick.value(QLatin1String("displayArea")).toObject();
        b.displayArea = QRectF(a.value(QLatin1String("x")).toDouble(), a.value(QLatin1String("y")).toDouble(),
                               a.value(QLatin1String("width")).toDouble(), a.value(QLatin1String("height")).toDouble());
        b.orientation = static_cast<float>(brick.value(QLatin1String("orientation")).toDouble());
        edit::moveBetweenPlacements(b, from, to, parts_);
        QJsonObject out = brick;
        out.insert(QStringLiteral("displayArea"),
                   QJsonObject{ { QStringLiteral("x"), b.displayArea.x() }, { QStringLiteral("y"), b.displayArea.y() },
                                { QStringLiteral("width"), b.displayArea.width() }, { QStringLiteral("height"), b.displayArea.height() } });
        out.insert(QStringLiteral("orientation"), static_cast<double>(b.orientation));
        return out;
    });
    connect(this, &QObject::destroyed, [] { sync::merge::setSharedMover({}); });
    sharedRefresh_ = new QTimer(this);
    sharedRefresh_->setSingleShot(true);
    sharedRefresh_->setInterval(150);
    connect(sharedRefresh_, &QTimer::timeout, this, &MainWindow::fetchSharedModules);

    // A change to a shared module was refused: say why, where it's seen.
    connect(mapView_, &MapView::sharedLockTold, this, [this](const QString& message) {
        notices_->showNotice(QStringLiteral("shared-lock"), tr("This module is shared"), message, { { tr("OK"), {}, true } });
        notices_->addHelp(QStringLiteral("shared-lock"), QStringLiteral("module.shared"));
    });

    SharedModulesBar* bar = mapView_->sharedModulesBar();
    connect(bar, &SharedModulesBar::updateRequested, this, &MainWindow::updateSharedModules);
    connect(bar, &SharedModulesBar::okRequested, this, &MainWindow::okSharedReviews);
    const auto showThem = [this](bool reviews) {
        const core::Map* map = mapView_->currentMap();
        if (!map) return;
        QSet<QString> parts;
        const auto placed = core::sharedModulesIn(map->sidecar.modules);
        if (reviews) {
            for (const auto& p : placed)
                if (p.review) parts.unite(p.members);
        } else {
            for (const auto& u : sharedwords::findUpdates(placed, sharedRows_.modules)) parts.unite(u.placed.members);
        }
        mapView_->showParts(parts);
    };
    connect(bar, &SharedModulesBar::showUpdatesRequested, this, [showThem] { showThem(false); });
    connect(bar, &SharedModulesBar::showReviewRequested, this, [showThem] { showThem(true); });
    connect(bar, &SharedModulesBar::goBackRequested, this, [this] {
        const core::Map* map = mapView_->currentMap();
        if (!map) return;
        for (const auto& p : core::sharedModulesIn(map->sidecar.modules))
            if (p.review && p.review->from > 0) {
                applySharedVersion(p.placedId, p.review->from);
                return;
            }
    });

    // The placed copies changed (one placed, removed, updated): ask again;
    // anything else only redraws what is known.
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, [this](int) { showSharedModules(); });
    connect(mapView_, &MapView::mapLoaded, this, [this] { showSharedModules(); });
    connect(serverLibrary_, &ServerLibrary::changed, this, &MainWindow::refreshSharedModules);
    connect(serverLibrary_, &ServerLibrary::stateChanged, this, &MainWindow::refreshSharedModules);
}

void MainWindow::refreshSharedModules() {
    if (sharedRefresh_) sharedRefresh_->start();
}

bool MainWindow::sharedByServer() const {
    return live_ && live_->active() && !liveLayoutId_.isEmpty() && !live_->signedOut()
        && live_->session().status() == sync::SyncClient::Status::Synced && !live_->session().offlineEdits();
}

sync::LibraryApi& MainWindow::sharedLiveApi() {
    sharedApi_->setBase(liveServer_);
    sharedApi_->setToken(liveToken_);
    return *sharedApi_;
}

sync::LibraryApi* MainWindow::sharedFileApi(const core::SharedPlaced& placed) const {
    if (!serverLibrary_ || serverLibrary_->state() != ServerLibrary::State::Ready) return nullptr;
    if (!placed.server.isEmpty() && hostOf(placed.server) != serverLibrary_->server().toString(QUrl::StripTrailingSlash))
        return nullptr;
    return &serverLibrary_->api();
}

const sync::LayoutSharedModule* MainWindow::sharedRow(const QString& placedId) const {
    return sharedRowsKnown_ ? sharedRows_.row(placedId) : nullptr;
}

void MainWindow::fetchSharedModules() {
    const core::Map* map = mapView_->currentMap();
    const auto placed = map ? core::sharedModulesIn(map->sidecar.modules) : std::vector<core::SharedPlaced>{};
    const int gen = ++sharedGen_;
    if (placed.empty() && sharedUpdatesDialog_ && live_ && live_->active() && !liveLayoutId_.isEmpty()) {
        // Nothing placed yet, but the setting is open: it follows the server too.
        sharedLiveApi().layoutSharedModules(liveLayoutId_, QString(), sync::whileAlive(this, [this](const sync::LayoutSharedModules& rows) {
            if (sharedUpdatesDialog_) sharedUpdatesDialog_->setCurrent(rows.updates);
        }), [](const sync::ServerRefusal&) {});
    }
    if (placed.empty()) {
        sharedRows_ = {};
        sharedRowsKnown_ = false;
        sharedCantCheck_ = false;
        showSharedModules();
        return;
    }
    if (live_ && live_->active() && !liveLayoutId_.isEmpty()) {
        sharedLiveApi().layoutSharedModules(liveLayoutId_, QString(), sync::whileAlive(this, [this, gen](const sync::LayoutSharedModules& rows) {
            if (gen != sharedGen_) return;
            sharedRows_ = rows;
            sharedRowsKnown_ = true;
            sharedCantCheck_ = false;
            if (sharedUpdatesDialog_) sharedUpdatesDialog_->setCurrent(rows.updates);
            showSharedModules();
            checkSharedDrift();
            askSharedWhileAway();
        }), sync::whileAlive(this, [this, gen](const sync::ServerRefusal& r) {
            if (gen != sharedGen_) return;
            // Not reached: what was known stays, and the panel says updates can't be checked.
            sharedCantCheck_ = r.status == 0;
            if (r.status != 0) sharedRowsKnown_ = false;
            showSharedModules();
        }));
        return;
    }
    // A file: what the Module library says about each copy's module.
    sync::LayoutSharedModules rows;
    rows.canUpdate = true;
    rows.canDetach = true;
    bool cant = false;
    for (const auto& p : placed) {
        if (!sharedFileApi(p) || !serverLibrary_->knowsSharedModules()) {
            cant = true;
            continue;
        }
        const sync::ServerModule* sm = serverLibrary_->sharedModule(p.moduleId);
        if (!sm) sm = serverLibrary_->module(p.moduleId);
        sync::LayoutSharedModule row;
        row.placedId = p.placedId;
        row.moduleId = p.moduleId;
        row.version = p.version;
        row.state = sm ? QStringLiteral("shared") : QStringLiteral("unshared");
        if (sm) {
            row.title = sm->title;
            row.ownerName = sm->owner.name;
            row.latest = sm->latestVersion;
        }
        rows.modules << row;
    }
    sharedRows_ = rows;
    sharedRowsKnown_ = !rows.modules.isEmpty();
    sharedCantCheck_ = cant;
    showSharedModules();
    checkSharedDrift();
}

void MainWindow::showSharedModules() {
    SharedModulesBar* bar = mapView_->sharedModulesBar();
    const core::Map* map = mapView_->currentMap();
    const auto placed = map ? core::sharedModulesIn(map->sidecar.modules) : std::vector<core::SharedPlaced>{};

    // Another layout: its checks start over.
    const QString doc = live_ && live_->active() ? QStringLiteral("live:") + liveLayoutId_ : QStringLiteral("file:") + currentFilePath_;
    if (doc != sharedDoc_) {
        sharedDoc_ = doc;
        sharedDriftChecked_.clear();
        for (const QString& id : std::as_const(sharedChangedOutside_)) notices_->hideNotice(QStringLiteral("shared-drift:") + id);
        sharedChangedOutside_.clear();
        sharedRows_ = {};
        sharedRowsKnown_ = false;
        sharedStamp_.clear();
        sharedAway_.clear();
        notices_->hideNotice(QStringLiteral("shared-away"));
    }
    // The placed copies changed: ask the server again.
    QStringList stamp;
    for (const auto& p : placed) stamp << QStringLiteral("%1:%2").arg(p.placedId).arg(p.version);
    if (const QString now = stamp.join(QLatin1Char(',')); now != sharedStamp_) {
        sharedStamp_ = now;
        refreshSharedModules();
    }

    QHash<QString, QString> badges;
    if (placed.empty()) {
        bar->setUpdates({}, {}, 0, {});
        bar->setReview({}, 0);
    } else {
        const bool readOnly = live_ && live_->active() && live_->readOnly();
        bar->setCanUpdate(!readOnly && sharedRowsKnown_ && sharedRows_.canUpdate);
        const QList<sharedwords::Update> updates =
            sharedRowsKnown_ ? sharedwords::findUpdates(placed, sharedRows_.modules) : QList<sharedwords::Update>{};
        bar->setUpdates(sharedwords::updatesText(updates), updates.size() > 1 ? sharedwords::updatesList(updates) : QString(),
                        static_cast<int>(updates.size()), sharedwords::updatesKey(updates));
        std::vector<const core::SharedPlaced*> reviews;
        // A module the "while you were away" notice is about isn't also told as "is now version N".
        const auto away = [this](const QString& id) {
            return std::any_of(sharedAway_.begin(), sharedAway_.end(), [&id](const sync::SharedAwayUpdate& a) { return a.placedId == id; });
        };
        for (const auto& p : placed)
            if (p.review && !away(p.placedId)) reviews.push_back(&p);
        if (reviews.empty()) bar->setReview({}, 0);
        else if (reviews.size() == 1)
            bar->setReview(sharedwords::reviewText(*reviews.front(), edit::sharedMarks(*map, reviews.front()->members, parts_)),
                           reviews.front()->review.value_or(core::SharedReview{}).from);
        else bar->setReview(tr("%n shared module(s) were updated.", nullptr, static_cast<int>(reviews.size())), 0);
        for (const auto& p : placed) badges.insert(p.placedId, sharedwords::badge(p, sharedwords::newerVersion(p, sharedRow(p.placedId))));
    }
    // What the Modules panel says under each one follows what is known ("No longer shared", "v8 is ready").
    QStringList said;
    for (const auto& p : placed) {
        const sync::LayoutSharedModule* row = sharedRow(p.placedId);
        said << p.placedId + QLatin1Char('=') + (row ? row->state + QString::number(row->latest) + row->ownerName : QString())
                    + (sharedCantCheck_ ? QStringLiteral("?") : QString());
    }
    const QString panel = said.join(QLatin1Char(';')) + (sharedRowsKnown_ && sharedRows_.canDetach ? QStringLiteral("d") : QString())
                          + (sharedRowsKnown_ && sharedRows_.canUpdate ? QStringLiteral("u") : QString());
    const bool panelChanged = panel != sharedPanelSaid_;
    sharedPanelSaid_ = panel;
    if (badges != sharedBadges_) {
        sharedBadges_ = badges;
        mapView_->setSharedBadges(badges);
    }
    if (panelChanged && modulesPanel_) modulesPanel_->setMap(map);
}

// ---- On opening: is each copy still its owner's version? -----------------

void MainWindow::checkSharedDrift() {
    core::Map* map = mapView_->currentMap();
    if (!map) return;
    const bool live = live_ && live_->active() && !liveLayoutId_.isEmpty();
    for (const auto& p : core::sharedModulesIn(map->sidecar.modules)) {
        if (p.version <= 0) continue;
        const QString key = QStringLiteral("%1:%2").arg(p.placedId).arg(p.version);
        // Once per copy and version, whatever the answer: a version the
        // server no longer keeps (404) is "no longer available", never asked again.
        if (sharedDriftChecked_.contains(key)) continue;
        const sync::LayoutSharedModule* row = sharedRow(p.placedId);
        if (row && row->state == QLatin1String("gone")) continue;
        sync::LibraryApi* fileApi = live ? nullptr : sharedFileApi(p);
        if (!live && !fileApi) continue;
        sharedDriftChecked_.insert(key);
        const QString placedId = p.placedId;
        const int version = p.version;
        const QString doc = sharedDoc_;
        const auto got = sync::whileAlive(this, [this, placedId, version, doc](const QByteArray& bytes) {
            if (doc != sharedDoc_) return;
            core::Map* map = mapView_->currentMap();
            const core::Module* m = placedModule(map, placedId);
            const auto placed = m ? core::sharedPlaced(*m) : std::nullopt;
            if (!placed || placed->version != version) return;
            std::unique_ptr<core::Map> then = sync::mapFromModuleSnapshot(bytes);
            if (!then) return;
            parts::placement::fixStaleAreas(*then, parts_);
            if (edit::matchesVersion(edit::batchesOfModule(*then), edit::placedParts(*map, m->memberIds), parts_, placed->placement))
                return;
            sharedDriftFound(placedId);
        });
        const sync::LibraryApi::Fail failed = [](const sync::ServerRefusal&) {};
        if (live) sharedLiveApi().sharedModuleVersionSnapshot(liveLayoutId_, placedId, version, got, failed);
        else fileApi->moduleVersionSnapshot(p.moduleId, version, got, failed);
    }
}

void MainWindow::sharedDriftFound(const QString& placedId) {
    const core::Module* m = placedModule(mapView_->currentMap(), placedId);
    const auto p = m ? core::sharedPlaced(*m) : std::nullopt;
    if (!p) return;
    sharedChangedOutside_.insert(placedId);
    const QString id = QStringLiteral("shared-drift:") + placedId;
    const bool canUpdate = sharedRowsKnown_ && sharedRows_.canUpdate && !(live_ && live_->active() && live_->readOnly());
    QList<NoticeAction> actions;
    if (canUpdate)
        actions << NoticeAction{ p->by.isEmpty() ? tr("Put Back the Owner's Version") : tr("Put Back %1's Version").arg(p->by),
                                 [this, placedId] {
                                     // (after the notice has let go of this button)
                                     QTimer::singleShot(0, this, [this, placedId] { sharedModuleAction(placedId, QStringLiteral("shared-putback")); });
                                 },
                                 true };
    if (sharedRowsKnown_ && sharedRows_.canDetach)
        actions << NoticeAction{ tr("Make It Mine…"), [this, placedId] {
                                     QTimer::singleShot(0, this, [this, placedId] { sharedModuleAction(placedId, QStringLiteral("shared-detach")); });
                                 },
                                 false };
    actions << NoticeAction{ tr("Not Now"), {}, false };
    notices_->showNotice(id, tr("“%1” was changed outside the app").arg(nameOf(*p)),
                         p->by.isEmpty()
                             ? tr("Its parts aren't its owner's version %1 anymore: another app changed this layout. "
                                  "Put the owner's version back, or make the module yours.")
                                   .arg(p->version)
                             : tr("Its parts aren't %1's version %2 anymore: another app changed this layout. "
                                  "Put %1's version back, or make the module yours.")
                                   .arg(p->by)
                                   .arg(p->version),
                         actions);
    notices_->addHelp(id, QStringLiteral("module.shared"));
    if (modulesPanel_) modulesPanel_->setMap(mapView_->currentMap());
}

// ---- The steps -----------------------------------------------------------

void MainWindow::sharedStepFailed(const sync::ServerRefusal& r) {
    sharedBusy_ = false;
    mapView_->sharedModulesBar()->setBusy(false);
    notices_->showNotice(QStringLiteral("shared-step"), tr("That didn't work"),
                         sharedwords::stepRefusal(r, ServerLibrary::refusalText(r)), { { tr("OK"), {}, true } });
    refreshSharedModules();
}

void MainWindow::applySharedVersion(const QString& placedId, int version, bool again) {
    if (sharedBusy_) return;
    const core::Module* m = placedModule(mapView_->currentMap(), placedId);
    if (!m || m->libraryModuleId.isEmpty()) return;
    if (!sharedByServer()) {
        applySharedLocally(placedId, version, again);
        return;
    }
    sharedBusy_ = true;
    mapView_->sharedModulesBar()->setBusy(true);
    const QString name = m->name.isEmpty() ? tr("This module") : m->name;
    sharedLiveApi().applySharedModule(liveLayoutId_, placedId, version, again, sync::whileAlive(this, [this, name, placedId, again](const sync::SharedModuleApplied& a) {
        sharedBusy_ = false;
        mapView_->sharedModulesBar()->setBusy(false);
        if (!a.changed) statusBar()->showMessage(tr("“%1” already has version %2.").arg(name).arg(a.version), 6000);
        if (again || sharedChangedOutside_.remove(placedId)) notices_->hideNotice(QStringLiteral("shared-drift:") + placedId);
        refreshSharedModules();
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) { sharedStepFailed(r); }));
}

void MainWindow::applySharedLocally(const QString& placedId, int version, bool again) {
    const core::Module* m = placedModule(mapView_->currentMap(), placedId);
    const auto p = m ? core::sharedPlaced(*m) : std::nullopt;
    if (!p) return;
    sync::LibraryApi* api = sharedFileApi(*p);
    const sync::LayoutSharedModule* row = sharedRow(placedId);
    if (version <= 0 && row) version = row->latest;
    if (!api || version <= 0) {
        notices_->showNotice(QStringLiteral("shared-step"), tr("Can't reach this module's server"),
                             p->server.isEmpty()
                                 ? tr("Sign in to the server “%1” lives on, then try again.").arg(nameOf(*p))
                                 : tr("“%1” lives on %2. Sign in there (File › Servers…), then try again.")
                                       .arg(nameOf(*p), QUrl(p->server).host()),
                             { { tr("OK"), {}, true } });
        return;
    }
    if (p->version == version && !again) {
        statusBar()->showMessage(tr("“%1” already has version %2.").arg(nameOf(*p)).arg(version), 6000);
        return;
    }
    sharedBusy_ = true;
    mapView_->sharedModulesBar()->setBusy(true);
    const QString moduleId = p->moduleId;
    const int from = p->version;
    const sync::LibraryApi::Fail failed = sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
        sync::ServerRefusal said = r;
        if (r.status == 404) said.code = QStringLiteral("version_not_found");
        sharedStepFailed(said);
    });
    // `current`: the version it has now, when the server still keeps it (its parts tell where the module sits).
    const auto put = [this, placedId, moduleId, version, from, again](const std::shared_ptr<core::Map>& next,
                                                                     const std::shared_ptr<core::Map>& current) {
        sharedBusy_ = false;
        mapView_->sharedModulesBar()->setBusy(false);
        core::Map* map = mapView_->currentMap();
        const core::Module* m = placedModule(map, placedId);
        if (!m || !core::isSharedModule(*m)) return;
        parts::placement::fixStaleAreas(*next, parts_);
        std::optional<edit::LayerBatches> basis;
        if (current) {
            parts::placement::fixStaleAreas(*current, parts_);
            basis = edit::batchesOfModule(*current);
        }
        const auto plan = edit::planVersion(*map, *m, edit::batchesOfModule(*next), basis ? &*basis : nullptr, parts_);
        if (!plan) {
            sync::ServerRefusal r;
            r.code = QStringLiteral("no_sheet");
            sharedStepFailed(r);
            return;
        }
        core::Module after = core::withPlacement(*m, plan->placement);
        // Putting the same version back isn't news; another version is, until someone says OK.
        if (from != version) after = core::withReview(after, core::SharedReview{ from, version, QDateTime::currentMSecsSinceEpoch() });
        const QString name = m->name;
        auto* cmd = new edit::ReplaceModulePartsCommand(*map, placedId, plan->batches, moduleId, version);
        cmd->setSharedAfter(*after.shared);
        cmd->setText(again ? tr("Put back the owner's version") : tr("Update shared module"));
        // Only the parts that differ change, as the server does it (doc shape 5): always in a file, and in
        // a live layout made offline where the server does it too (else every part goes in again, as before).
        const bool liveNow = live_ && live_->active() && !liveLayoutId_.isEmpty();
        const bool keep = !liveNow || live_->session().keepIds();
        if (keep) cmd->keepUnchanged(&parts_);
        mapView_->undoStack()->push(cmd);
        if (keep && liveNow) live_->session().noteKeptIds();
        mapView_->refreshScene();
        if (again || sharedChangedOutside_.remove(placedId)) {
            sharedChangedOutside_.remove(placedId);
            notices_->hideNotice(QStringLiteral("shared-drift:") + placedId);
        }
        // (A change of this copy's version: the drift check has nothing to say about the new one.)
        sharedDriftChecked_.insert(QStringLiteral("%1:%2").arg(placedId).arg(version));
        modulesPanel_->setMap(map);
        showSharedModules();
        if (again) statusBar()->showMessage(tr("Put the owner's version of “%1” back. Undo (Ctrl+Z) takes that back.").arg(name), 7000);
    };
    api->moduleVersionSnapshot(moduleId, version, sync::whileAlive(this, [this, api, moduleId, from, version, put, failed](const QByteArray& bytes) {
        QString error;
        std::shared_ptr<core::Map> next(sync::mapFromModuleSnapshot(bytes, &error).release());
        if (!next) {
            sync::ServerRefusal r;
            r.code = QStringLiteral("version_not_found");
            sharedStepFailed(r);
            return;
        }
        if (from <= 0 || from == version) {
            put(next, from == version ? next : nullptr);
            return;
        }
        api->moduleVersionSnapshot(moduleId, from, sync::whileAlive(this, [put, next](const QByteArray& old) {
            put(next, std::shared_ptr<core::Map>(sync::mapFromModuleSnapshot(old).release()));
        }), sync::whileAlive(this, [put, next](const sync::ServerRefusal&) { put(next, nullptr); }));
    }), failed);
}

void MainWindow::updateSharedModules() {
    const core::Map* map = mapView_->currentMap();
    if (!map || sharedBusy_) return;
    const QList<sharedwords::Update> updates = sharedwords::findUpdates(core::sharedModulesIn(map->sidecar.modules), sharedRows_.modules);
    if (updates.isEmpty()) return;
    if (updates.size() == 1 || !sharedByServer()) {
        // (A file's copies go in one after the other: each is its own undo step.)
        applySharedVersion(updates.first().placed.placedId, updates.first().latest);
        return;
    }
    sharedBusy_ = true;
    mapView_->sharedModulesBar()->setBusy(true);
    sharedLiveApi().applyAllSharedModules(liveLayoutId_, sync::whileAlive(this, [this](int updated, int skipped) {
        sharedBusy_ = false;
        mapView_->sharedModulesBar()->setBusy(false);
        if (skipped > 0)
            statusBar()->showMessage(tr("Updated %1. %2 couldn't be updated: they are no longer shared.").arg(updated).arg(skipped), 8000);
        refreshSharedModules();
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) { sharedStepFailed(r); }));
}

// OK: the notice is read. Everyone in the layout stops seeing it.
void MainWindow::okSharedReviews() {
    core::Map* map = mapView_->currentMap();
    if (!map) return;
    std::vector<core::Module> seen;
    for (const auto& m : map->sidecar.modules)
        if (const auto p = core::sharedPlaced(m); p && p->review) seen.push_back(core::withReview(m, std::nullopt));
    if (seen.empty()) return;
    auto* stack = mapView_->undoStack();
    if (seen.size() > 1) stack->beginMacro(tr("Mark shared module updates as seen"));
    for (auto& m : seen) stack->push(new edit::UpdateModuleCommand(*map, std::move(m), tr("Mark shared module update as seen")));
    if (seen.size() > 1) stack->endMacro();
}

void MainWindow::detachSharedModule(const QString& placedId) {
    core::Map* map = mapView_->currentMap();
    const core::Module* m = placedModule(map, placedId);
    const auto p = m ? core::sharedPlaced(*m) : std::nullopt;
    if (!p) return;
    const bool live = live_ && live_->active() && !liveLayoutId_.isEmpty();
    const QString name = nameOf(*p);
    ConfirmOptions o;
    o.title = tr("Make “%1” yours?").arg(name);
    const QString whose = p->by.isEmpty() ? tr("its owner") : p->by;
    // On a live layout the server makes the change for everyone: Undo doesn't take it back.
    o.removes = live ? tr("It stops following %1: no more updates, and this can't be undone.").arg(whose)
                     : tr("It stops following %1: no more updates.").arg(whose);
    o.keeps = tr("Its parts stay where they are, and you can change them like any module of this layout.");
    if (!live) o.undo = tr("You can undo this.");
    o.confirmLabel = tr("Make It Mine");
    o.danger = false;
    if (!confirmModuleLibrary(o)) return;
    const auto done = [this, placedId, name] {
        sharedChangedOutside_.remove(placedId);
        notices_->hideNotice(QStringLiteral("shared-drift:") + placedId);
        statusBar()->showMessage(tr("“%1” is now a module of this layout.").arg(name), 7000);
        refreshSharedModules();
    };
    if (!live) {
        mapView_->undoStack()->push(new edit::DetachSharedModuleCommand(*map, placedId));
        modulesPanel_->setMap(map);
        done();
        return;
    }
    sharedLiveApi().detachSharedModule(liveLayoutId_, placedId, sync::whileAlive(this, [done](bool) { done(); }),
                                       sync::whileAlive(this, [this](const sync::ServerRefusal& r) { sharedStepFailed(r); }));
}

void MainWindow::sharedModuleAction(const QString& placedId, const QString& action) {
    const core::Module* m = placedModule(mapView_->currentMap(), placedId);
    const auto p = m ? core::sharedPlaced(*m) : std::nullopt;
    if (!p) return;
    if (action == QLatin1String("shared-versions")) showSharedVersions(placedId);
    else if (action == QLatin1String("shared-update")) applySharedVersion(placedId, sharedwords::newerVersion(*p, sharedRow(placedId)));
    else if (action == QLatin1String("shared-putback")) applySharedVersion(placedId, p->version, true);
    else if (action == QLatin1String("shared-detach")) detachSharedModule(placedId);
}

// The module's versions seen through the layout, each with its note, and "Update to this" / "Go back to this".
void MainWindow::showSharedVersions(const QString& placedId) {
    const core::Module* m = placedModule(mapView_->currentMap(), placedId);
    const auto p = m ? core::sharedPlaced(*m) : std::nullopt;
    if (!p || !(live_ && live_->active()) || liveLayoutId_.isEmpty()) return;
    if (QDialog* old = sharedVersions_) {
        sharedVersions_ = nullptr;
        old->close();
    }
    // (Heard through a child made before the dialog, so a window being closed isn't told.)
    if (!sharedVersionsGone_) sharedVersionsGone_ = new QObject(this);
    auto* dlg = new QDialog(this);
    connect(dlg, &QObject::destroyed, sharedVersionsGone_, [this, dlg] {
        if (sharedVersions_ == dlg) sharedVersions_ = nullptr;
    });
    sharedVersions_ = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setObjectName(QStringLiteral("sharedVersionsDialog"));
    dlg->setWindowTitle(tr("Versions of “%1”").arg(nameOf(*p)));
    dlg->setMinimumSize(460, 360);
    auto* col = new QVBoxLayout(dlg);
    const QString has = p->version > 0 ? QString::number(p->version) : QStringLiteral("?");
    auto* about = new QLabel(p->by.isEmpty()
                                 ? tr("This layout has version %1. Putting another one in changes it for everyone in the layout.").arg(has)
                                 : tr("%1 saves the versions. This layout has version %2. Putting another one in changes it "
                                      "for everyone in the layout.")
                                       .arg(p->by, has),
                             dlg);
    about->setWordWrap(true);
    col->addWidget(about);
    auto* scroll = new QScrollArea(dlg);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    auto* list = new QVBoxLayout(host);
    list->setContentsMargins(0, 0, 0, 0);
    auto* loading = new QLabel(tr("Loading…"), host);
    loading->setObjectName(QStringLiteral("sharedVersionsStatus"));
    loading->setWordWrap(true);
    list->addWidget(loading);
    list->addStretch(1);
    scroll->setWidget(host);
    col->addWidget(scroll, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    col->addWidget(buttons);
    const int placedVersion = p->version;
    const bool canUpdate = sharedRowsKnown_ && sharedRows_.canUpdate && !live_->readOnly();
    sharedLiveApi().sharedModuleVersions(liveLayoutId_, placedId, sync::whileAlive(dlg, [this, dlg, host, list, loading, placedId, placedVersion, canUpdate](const QList<sync::SharedModuleVersion>& versions) {
        loading->setVisible(versions.isEmpty());
        if (versions.isEmpty()) loading->setText(tr("No versions are kept for this module."));
        int at = 1;
        for (const auto& v : versions) {
            auto* row = new QWidget(host);
            row->setObjectName(QStringLiteral("sharedVersion:%1").arg(v.version));
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 4, 0, 4);
            QString text = v.version == placedVersion ? tr("Version %1 (in this layout)").arg(v.version) : tr("Version %1").arg(v.version);
            QStringList sub;
            if (v.createdAt.isValid()) sub << QLocale().toString(v.createdAt.date(), QLocale::ShortFormat);
            if (!v.author.isEmpty()) sub << v.author;
            if (!sub.isEmpty()) text += QStringLiteral("\n") + sub.join(QStringLiteral(" · "));
            text += QStringLiteral("\n") + (v.note.isEmpty() ? tr("No note") : tr("“%1”").arg(v.note));
            auto* words = new QLabel(text, row);
            words->setTextFormat(Qt::PlainText);
            words->setWordWrap(true);
            h->addWidget(words, 1);
            if (canUpdate && v.version != placedVersion) {
                auto* put = new QPushButton(v.version > placedVersion ? tr("Update to This") : tr("Go Back to This"), row);
                put->setObjectName(QStringLiteral("putSharedVersion"));
                const int version = v.version;
                connect(put, &QPushButton::clicked, dlg, [this, dlg, placedId, version] {
                    dlg->close();
                    applySharedVersion(placedId, version);
                });
                h->addWidget(put, 0, Qt::AlignTop);
            }
            list->insertWidget(at++, row);
        }
    }), sync::whileAlive(dlg, [loading](const sync::ServerRefusal& r) {
        loading->setText(sharedwords::stepRefusal(r, ServerLibrary::refusalText(r)));
    }));
    dlg->open();
}

// ---- Placing one ---------------------------------------------------------

void MainWindow::sharedTipOnce() {
    QSettings s;
    if (s.value(QStringLiteral("sharedModules/tipShown"), false).toBool()) return;
    s.setValue(QStringLiteral("sharedModules/tipShown"), true);
    notices_->showNotice(QStringLiteral("shared-tip"), tr("Shared modules"),
                         tr("Shared modules follow their owner's updates. You can move and turn them; their parts "
                            "stay as the owner made them."),
                         { { tr("OK"), {}, true } });
    notices_->addHelp(QStringLiteral("shared-tip"), QStringLiteral("module.shared"));
}

void MainWindow::sharedLinkFor(const sync::ServerModule& m, const std::function<void(bool, std::optional<QJsonObject>)>& done) {
    // "Can change it" (an editor share) stays a linked copy you can change, as do your own and your clubs'.
    if (!m.sharedWithMe || m.role != QLatin1String("viewer")) {
        done(true, std::nullopt);
        return;
    }
    const QString server = serverLibrary_->server().toString(QUrl::StripTrailingSlash);
    const auto refuse = [this, done](const sync::SharedPlaceCheck& check) {
        const sharedwords::Refusal t = sharedwords::refusalText(check);
        ConfirmOptions o;
        o.title = t.title;
        o.keeps = t.body;
        o.confirmLabel = tr("Copy a Message to Send");
        o.danger = false;
        if (confirmModuleLibrary(o)) {
            QGuiApplication::clipboard()->setText(t.ask);
            statusBar()->showMessage(
                tr("Copied. Send it to %1.").arg(check.moduleOwner.name.isEmpty() ? tr("the module's owner") : check.moduleOwner.name),
                6000);
        }
        done(false, std::nullopt);
    };
    const auto shared = [this, done, server](const QString& by) {
        sharedTipOnce();
        QJsonObject link{ { QStringLiteral("server"), server } };
        if (!by.isEmpty()) link.insert(QStringLiteral("by"), by);
        done(true, link);
    };
    const QString ownerName = m.owner.name;
    const QString moduleId = m.id;
    if (live_ && live_->active() && !liveLayoutId_.isEmpty()) {
        // A live layout elsewhere: its server can't follow this module.
        if (liveServer_.toString(QUrl::StripTrailingSlash) != server) {
            ConfirmOptions o;
            o.title = tr("“%1” can't go in this layout").arg(m.title);
            o.keeps = tr("This layout is on %1, and “%2” is shared with you on %3. A shared module follows its owner "
                         "only in layouts on its own server, or in a file on this computer.")
                          .arg(liveServer_.host(), m.title, serverLibrary_->server().host());
            o.confirmLabel = tr("OK");
            o.danger = false;
            confirmModuleLibrary(o);
            done(false, std::nullopt);
            return;
        }
        sharedLiveApi().layoutSharedModules(liveLayoutId_, moduleId, sync::whileAlive(this, [done, refuse, shared, ownerName](const sync::LayoutSharedModules& rows) {
            if (!rows.check) done(true, std::nullopt);
            else if (rows.check->state != QLatin1String("shared")) refuse(*rows.check);
            else shared(rows.check->moduleOwner.name.isEmpty() ? ownerName : rows.check->moduleOwner.name);
        }), sync::whileAlive(this, [done](const sync::ServerRefusal&) {
            // An older server, or offline: placed as before.
            done(true, std::nullopt);
        }));
        return;
    }
    // A file on this computer is yours: the module follows its owner here
    // when it is shared with you in person (one shared with a club is for
    // the club's layouts).
    if (!m.sharedDirect) {
        sync::SharedPlaceCheck check;
        check.moduleId = m.id;
        check.title = m.title;
        check.state = QStringLiteral("unshared");
        check.moduleOwner = { m.owner.kind, m.owner.name, false };
        check.layoutOwner = { QStringLiteral("user"), QString(), true };
        refuse(check);
        return;
    }
    shared(ownerName);
}

}  // namespace bld::ui

namespace bld::ui {

// ---- Updated while you were away (an Automatically layout) -----------------

void MainWindow::askSharedWhileAway() {
    if (!(live_ && live_->active()) || liveLayoutId_.isEmpty() || sharedAwayAskedFor_ == sharedDoc_) return;
    // Once per layout opened: the server notes the visit when it answers.
    sharedAwayAskedFor_ = sharedDoc_;
    const QString doc = sharedDoc_;
    sharedLiveApi().sharedModulesWhileAway(liveLayoutId_, sync::whileAlive(this, [this, doc](const QList<sync::SharedAwayUpdate>& list) {
        if (doc != sharedDoc_) return;
        sharedAway_ = list;
        showSharedWhileAway();
        showSharedModules();
    }), [](const sync::ServerRefusal&) {});  // An older server, or no answer: nothing to say.
}

void MainWindow::showSharedWhileAway() {
    const core::Map* map = mapView_->currentMap();
    // Only while each copy still has the version the update left it at (Go back, or another update, ends it).
    QList<sync::SharedAwayUpdate> still;
    std::vector<core::SharedPlaced> placed = map ? core::sharedModulesIn(map->sidecar.modules) : std::vector<core::SharedPlaced>{};
    const auto find = [&placed](const QString& id) -> const core::SharedPlaced* {
        for (const auto& p : placed)
            if (p.placedId == id) return &p;
        return nullptr;
    };
    for (const auto& a : std::as_const(sharedAway_))
        if (const core::SharedPlaced* p = find(a.placedId); p && p->version == a.to) still << a;
    sharedAway_ = still;
    const QString id = QStringLiteral("shared-away");
    if (still.isEmpty()) {
        notices_->hideNotice(id);
        return;
    }
    const bool canUpdate = sharedRowsKnown_ && sharedRows_.canUpdate && !(live_ && live_->active() && live_->readOnly());
    QString text;
    QList<NoticeAction> actions;
    QSet<QString> members;
    for (const auto& a : std::as_const(still)) members.unite(find(a.placedId)->members);
    actions << NoticeAction{ tr("Show"), [this, members] { mapView_->showParts(members); }, false, false };
    if (still.size() == 1) {
        const auto& a = still.front();
        const core::SharedPlaced* p = find(a.placedId);
        text = sharedwords::awayText(p->name, p->by, a.from, a.to);
        if (const QString marks = sharedwords::marksText(edit::sharedMarks(*map, p->members, parts_)); !marks.isEmpty())
            text += QLatin1Char(' ') + marks;
        if (canUpdate && a.from > 0 && a.from != a.to) {
            const QString placedId = a.placedId;
            const int from = a.from;
            actions << NoticeAction{ tr("Go Back to Version %1").arg(from), [this, placedId, from] {
                                        sharedAway_.clear();
                                        QTimer::singleShot(0, this, [this, placedId, from] { applySharedVersion(placedId, from); });
                                    },
                                     false };
        }
    } else {
        QStringList lines;
        for (const auto& a : std::as_const(still)) lines << sharedwords::awayLine(find(a.placedId)->name, a.from, a.to);
        text = sharedwords::awayManyText(static_cast<int>(still.size())) + QLatin1Char('\n') + lines.join(QStringLiteral(", "));
    }
    actions << NoticeAction{ tr("OK"), [this, canUpdate] {
                                QList<sync::SharedAwayUpdate> read = sharedAway_;
                                sharedAway_.clear();
                                // Read: its "is now version N" goes for everyone, as OK on that notice does.
                                if (canUpdate) QTimer::singleShot(0, this, [this] { okSharedReviews(); });
                                showSharedModules();
                            },
                             true };
    notices_->showNotice(id, tr("Updated while you were away"), text, actions);
    notices_->addHelp(id, QStringLiteral("module.sharedUpdatesSetting"));
}

// ---- Modules › Updates from Shared Modules… --------------------------------

void MainWindow::showSharedUpdatesSetting() {
    if (!(live_ && live_->active()) || liveLayoutId_.isEmpty()) return;
    if (sharedUpdatesDialog_) {
        sharedUpdatesDialog_->raise();
        sharedUpdatesDialog_->activateWindow();
        return;
    }
    auto* dlg = new SharedUpdatesDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    sharedUpdatesDialog_ = dlg;
    connect(dlg, &QObject::destroyed, this, [this] { sharedUpdatesDialog_ = nullptr; });
    const QString layoutId = liveLayoutId_;
    const auto load = [this, layoutId] {
        sharedLiveApi().layoutSharedModules(layoutId, QString(), sync::whileAlive(this, [this](const sync::LayoutSharedModules& rows) {
            if (!sharedUpdatesDialog_) return;
            sharedUpdatesDialog_->setCurrent(rows.updates);
            sharedUpdatesDialog_->setCanChange(rows.canUpdate && !(live_ && live_->active() && live_->readOnly()));
        }), [](const sync::ServerRefusal&) {});
    };
    connect(dlg, &SharedUpdatesDialog::chosen, this, [this, dlg, layoutId, load](const QString& updates) {
        const QString was = dlg->current();
        dlg->setCurrent(updates);  // shown at once; back if the server says no
        dlg->setBusy(true);
        sharedLiveApi().setSharedModuleUpdates(layoutId, updates, sync::whileAlive(this, [this](const QString& now) {
            if (sharedUpdatesDialog_) {
                sharedUpdatesDialog_->setBusy(false);
                sharedUpdatesDialog_->setCurrent(now);
            }
            statusBar()->showMessage(now == QLatin1String("auto") ? tr("New versions of shared modules now go in automatically.")
                                                                 : tr("Shared modules now ask first before a new version goes in."),
                                     6000);
            refreshSharedModules();
        }), sync::whileAlive(this, [this, was, load](const sync::ServerRefusal& r) {
            if (sharedUpdatesDialog_) {
                sharedUpdatesDialog_->setBusy(false);
                sharedUpdatesDialog_->setCurrent(was);
            }
            notices_->showNotice(QStringLiteral("shared-step"), tr("That didn't work"),
                                 sharedwords::stepRefusal(r, ServerLibrary::refusalText(r)), { { tr("OK"), {}, true } });
            load();
        }));
    });
    load();
    dlg->show();
}

}  // namespace bld::ui

bld::ui::SharedUpdatesDialog* bld::ui::MainWindow::sharedUpdatesDialog() const { return sharedUpdatesDialog_; }
