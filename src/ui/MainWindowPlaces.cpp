// The Places panel in the main window (Live Ops design 6): its changes as
// undo steps, picking track on the map, and a module's Places….

#include "MainWindow.h"

#include "MapView.h"
#include "ModulesPanel.h"
#include "PlacesPanel.h"
#include "PlacesText.h"
#include "LiveLayout.h"

#include "../core/Map.h"
#include "../edit/PlaceCommands.h"

#include <QAction>
#include <QUndoStack>

namespace bld::ui {

void MainWindow::setupPlaces() {
    placesPanel_ = new PlacesPanel(parts_, this);
    addDockWidget(Qt::RightDockWidgetArea, placesPanel_);
    placesPanel_->hide();  // Panels > Places, a module's Places…, or the places tour, shows it
    connect(placesPanel_, &PlacesPanel::placeEdited, this,
            [this](const QString& owner, const QString& id, const std::optional<QJsonObject>& after, const QString& what) {
                if (core::Map* map = mapView_->currentMap()) mapView_->undoStack()->push(new edit::SetPlaceCommand(*map, owner, id, after, what));
            });
    connect(placesPanel_, &PlacesPanel::pickRequested, this, [this](const std::optional<QString>& owner, const QString& place, const QString& spot) {
        mapView_->startPlacePick({ owner, place, spot });
    });
    connect(placesPanel_, &PlacesPanel::showSpotRequested, mapView_, &MapView::showSpot);
    connect(mapView_, &MapView::placePickChanged, placesPanel_, &PlacesPanel::setPicking);
    connect(mapView_, &MapView::placePicked, this, [this](const QString& owner, const QString& id, bool fresh) {
        // What was just put on the map shows there.
        if (auto* act = findChild<QAction*>(QStringLiteral("view.places")); act && !act->isChecked()) act->setChecked(true);
        placesPanel_->openPlace(owner, id, fresh);
        placesPanel_->show();
        placesPanel_->raise();
    });
    connect(mapView_, &MapView::modulePlacesRequested, this, &MainWindow::showModulePlaces);
    connect(modulesPanel_, &ModulesPanel::placesRequested, this, &MainWindow::showModulePlaces);
    connect(mapView_, &MapView::mapLoaded, this, [this] {
        mapView_->cancelPlacePick();
        placesPanel_->setMap(mapView_->currentMap());  // first: the panel's old map is gone
        placesPanel_->showModule(QString());
        refreshPlaces();
    });
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, [this](int) { refreshPlaces(); });
    refreshPlaces();
}

void MainWindow::showModulePlaces(const QString& moduleId) {
    placesPanel_->showModule(moduleId);
    placesPanel_->show();
    placesPanel_->raise();
}

void MainWindow::refreshPlaces() {
    if (!placesPanel_) return;
    placesPanel_->setMap(mapView_->currentMap());
    placesPanel_->setReadOnly(live_ && live_->active() && live_->readOnly());
}

}  // namespace bld::ui
