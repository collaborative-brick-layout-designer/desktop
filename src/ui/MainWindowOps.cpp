// The Live Ops panel in the main window (Live Ops design 6): its changes as
// undo steps, and the ways there.

#include "MainWindow.h"

#include "LiveLayout.h"
#include "LiveOpsPanel.h"
#include "MapView.h"
#include "TrainsPanel.h"

#include "../core/Ids.h"
#include "../core/Map.h"
#include "../edit/CutCommands.h"
#include "../edit/OpsCommands.h"

#include <QUndoStack>

namespace bld::ui {

void MainWindow::setupOps() {
    opsPanel_ = new LiveOpsPanel(parts_, this);
    addDockWidget(Qt::RightDockWidgetArea, opsPanel_);
    opsPanel_->hide();  // Panels > Live Ops, a car's menu, or the tour, shows it
    connect(opsPanel_, &LiveOpsPanel::changed, this, [this](const edit::OpsChange& change, const QString& what) {
        if (core::Map* map = mapView_->currentMap()) mapView_->undoStack()->push(new edit::OpsCommand(*map, change, what));
    });
    connect(opsPanel_, &LiveOpsPanel::carLeaves, this, [this](const QString& car, const edit::OpsChange& change, const QString& what) {
        core::Map* map = mapView_->currentMap();
        if (!map) return;
        const auto stock = track::stockById(map->sidecar);
        const auto trains = track::settleTrains(map->sidecar.trains, stock);
        mapView_->undoStack()->beginMacro(what);
        if (track::standingIn(car, trains))
            mapView_->undoStack()->push(new edit::CutCommand(*map, track::takeOut(track::trackNetworkOf(*map, parts_), trains, stock, map->sidecar.switchStates, car, core::newBbmId()), what));
        mapView_->undoStack()->push(new edit::OpsCommand(*map, change, what));
        mapView_->undoStack()->endMacro();
    });
    connect(opsPanel_, &LiveOpsPanel::showSpotRequested, mapView_, &MapView::showSpot);
    connect(opsPanel_, &LiveOpsPanel::openPlacesRequested, this, [this] { showModulePlaces(QString()); });
    connect(opsPanel_, &LiveOpsPanel::openTrainsRequested, this, [this] {
        trainsPanel_->show();
        trainsPanel_->raise();
    });
    // A car's menu: its waybill, or where it goes.
    connect(mapView_, &MapView::carOpsRequested, this, [this](const QString& car) {
        const core::Map* map = mapView_->currentMap();
        const QString waybill = map ? edit::pocketOf(*map, car) : QString();
        if (waybill.isEmpty()) opsPanel_->showTab(LiveOpsPanel::Tab::Cars);
        else opsPanel_->openWaybill(waybill);
        opsPanel_->show();
        opsPanel_->raise();
    });
    connect(mapView_, &MapView::mapLoaded, this, [this] { refreshOps(); });
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, [this](int) { refreshOps(); });
    refreshOps();
}

void MainWindow::refreshOps() {
    if (!opsPanel_) return;
    opsPanel_->setMap(mapView_->currentMap());
    opsPanel_->setReadOnly(live_ && live_->active() && live_->readOnly());
}

}  // namespace bld::ui
