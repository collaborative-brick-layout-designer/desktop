// The Trains panel's wiring (trains design 6.4): its edits become undo
// steps on the map, Put on the track hands over to the map's click, and the
// panel follows every change (an undo, a live update from someone else).

#include "LiveLayout.h"
#include "MainWindow.h"
#include "MapView.h"
#include "ServerLibrary.h"
#include "TrainsPanel.h"

#include "../core/Map.h"
#include "../edit/TrainCommands.h"

#include <QUndoStack>

namespace bld::ui {

void MainWindow::setupTrains() {
    trainsPanel_ = new TrainsPanel(parts_, this);
    addDockWidget(Qt::RightDockWidgetArea, trainsPanel_);
    trainsPanel_->hide();  // Panels > Trains, or the trains tour, shows it
    connect(trainsPanel_, &TrainsPanel::stockEdited, this,
            [this](const QString& id, const std::optional<core::RollingStock>& after, const QString& what) {
                if (core::Map* map = mapView_->currentMap()) mapView_->undoStack()->push(new edit::SetStockCommand(*map, id, after, what));
            });
    connect(trainsPanel_, &TrainsPanel::stockRemoved, this, [this](const QString& id, const QString& what) {
        if (core::Map* map = mapView_->currentMap()) mapView_->undoStack()->push(edit::makeRemoveStockCommand(*map, id, what));
    });
    connect(trainsPanel_, &TrainsPanel::trainEdited, this,
            [this](const QString& id, const std::optional<core::Train>& after, const QString& what) {
                if (core::Map* map = mapView_->currentMap()) mapView_->undoStack()->push(new edit::SetTrainCommand(*map, id, after, what));
            });
    connect(trainsPanel_, &TrainsPanel::placeRequested, mapView_, &MapView::startTrainPlacing);
    connect(trainsPanel_, &TrainsPanel::placeCancelled, mapView_, &MapView::cancelTrainPlacing);
    connect(mapView_, &MapView::trainPlacingChanged, trainsPanel_, &TrainsPanel::setPlacing);
    connect(mapView_, &MapView::mapLoaded, this, [this] {
        mapView_->cancelTrainPlacing();
        refreshTrains();  // first: the panel's old map is gone
    });
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, [this](int) { refreshTrains(); });
    // Signing in or out, and the clubs you're in, change what the roster buttons offer.
    if (serverLibrary_) {
        // (setServerLibrary skips the same library: the panel reads its sign-in again here)
        connect(serverLibrary_, &ServerLibrary::stateChanged, this, [this] { trainsPanel_->setMap(mapView_->currentMap()); });
        connect(serverLibrary_, &ServerLibrary::changed, this, [this] { trainsPanel_->setMap(mapView_->currentMap()); });
    }
    refreshTrains();
}

void MainWindow::refreshTrains() {
    if (!trainsPanel_) return;
    // The map first: the old one may be gone already.
    trainsPanel_->setMap(mapView_->currentMap());
    trainsPanel_->setServerLibrary(serverLibrary_);
    trainsPanel_->setReadOnly(live_ && live_->active() && live_->readOnly());
}

}  // namespace bld::ui
