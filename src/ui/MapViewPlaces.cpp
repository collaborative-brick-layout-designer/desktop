// Picking track for a place on the map (Live Ops design 6): the Places
// panel's New place, Add a spot and Pick its track wait for a click on the
// track; right-click on track > Make a Place… makes one there. One undo step.
// The web's pickPlaceTrack (editor/EditorPage.tsx) does the same.

#include "MapView.h"

#include "PlacesText.h"

#include "../core/Ids.h"
#include "../core/Map.h"
#include "../core/ModuleEdit.h"
#include "../core/Places.h"
#include "../core/SharedModule.h"
#include "../edit/PlaceCommands.h"
#include "../track/Routes.h"
#include "../track/TrackNetwork.h"
#include "help/HelpButton.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QUndoStack>

#include <algorithm>

namespace bld::ui {

void MapView::setShowPlaces(bool on) {
    showPlaces_ = on;
    if (!builder_) return;
    builder_->setShowPlaces(on);
    if (map_) builder_->refreshPlaces(*map_);
}

void MapView::startPlacePick(const PlacePick& pick) {
    if (!map_ || runReadOnly_) return;
    if (runMode_) setRunMode(false);
    cancelRouteDraft();
    cancelTrainPlacing();
    if (scene()) scene()->clearSelection();
    placePick_ = pick;
    setInteractive(false);
    if (!placeBar_) {
        auto* bar = new QFrame(viewport());
        bar->setObjectName(QStringLiteral("PlacePickBar"));
        bar->setFrameShape(QFrame::StyledPanel);
        bar->setAutoFillBackground(true);
        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(12, 6, 8, 6);
        placeBarText_ = new QLabel(bar);
        placeBarText_->setObjectName(QStringLiteral("PlacePickText"));
        row->addWidget(help::withHelp(placeBarText_, QStringLiteral("ops.spot"), bar));
        auto* cancel = new QPushButton(tr("Cancel"), bar);
        cancel->setObjectName(QStringLiteral("PlacePickCancel"));
        row->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, &MapView::cancelPlacePick);
        placeBar_ = bar;
    }
    const core::Module* mod = pick.owner && !pick.owner->isEmpty() ? core::findModule(map_->sidecar.modules, *pick.owner) : nullptr;
    const QString name = mod ? mod->name.toHtmlEscaped() : QString();
    if (pick.place.isEmpty())
        placeBarText_->setText(name.isEmpty() ? tr("<b>New place:</b> click the track where cars stand.")
                                              : tr("<b>New place:</b> click the track where cars stand on %1.").arg(name));
    else
        placeBarText_->setText(name.isEmpty() ? tr("<b>Spot:</b> click the track where cars stand.")
                                              : tr("<b>Spot:</b> click the track where cars stand on %1.").arg(name));
    placeBar_->adjustSize();
    placeBar_->move(viewport()->rect().center().x() - placeBar_->width() / 2, 10);
    placeBar_->show();
    placeBar_->raise();
    emit placePickChanged(true);
}

void MapView::cancelPlacePick() {
    if (!placePick_) return;
    placePick_.reset();
    setInteractive(!clicksTaken());
    if (placeBar_) placeBar_->hide();
    emit placePickChanged(false);
}

bool MapView::placePickClick(QPointF studs, const PlacePick& pick) {
    if (!map_ || runReadOnly_) return false;
    namespace cp = core::places;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const double reach = std::max(3.0, 24.0 / (transform().m11() * rendering::SceneBuilder::kPixelsPerStud));
    const auto pos = track::nearestPosition(net, studs, reach);
    if (!pos) {
        showStatus(tr("Click on the track."), 3000);
        return false;
    }
    const core::Module* on = nullptr;
    for (const auto& m : map_->sidecar.modules)
        if (m.memberIds.contains(pos->brick)) {
            on = &m;
            break;
        }
    if (on) {
        if (const auto shared = core::sharedPlaced(*on)) {
            showStatus(core::lockMessage(shared->name, shared->by, core::LockedAction::Places), 6000);
            return false;
        }
    }
    if (pick.owner && !pick.owner->isEmpty() && (!on || on->id != *pick.owner)) {
        const core::Module* want = core::findModule(map_->sidecar.modules, *pick.owner);
        showStatus(tr("That track isn’t part of “%1”. Click the module’s own track.").arg(want ? want->name : tr("the module")), 5000);
        return false;
    }
    // A new place goes where its track is: on the module, or the layout's own.
    const QString owner = pick.owner ? *pick.owner : (on ? on->id : QString());
    const QJsonArray list = edit::placesOf(std::as_const(*map_), owner);
    const QJsonObject track{ { QStringLiteral("brick"), pos->brick }, { QStringLiteral("from"), pos->from }, { QStringLiteral("to"), pos->to } };
    const int at = pick.place.isEmpty() ? -1 : cp::indexOf(list, pick.place);
    QJsonObject place;
    if (at >= 0) {
        place = list.at(at).toObject();
        QJsonArray spots = place.value(QLatin1String("spots")).toArray();
        bool found = false;
        QStringList names;
        for (qsizetype i = 0; i < spots.size(); ++i) {
            QJsonObject s = spots.at(i).toObject();
            names << s.value(QLatin1String("name")).toString();
            if (pick.spot.isEmpty() || s.value(QLatin1String("id")).toString() != pick.spot) continue;
            s.insert(QStringLiteral("track"), track);
            spots.replace(i, s);
            found = true;
        }
        if (!found && spots.size() < cp::kMaxSpots) {
            qsizetype n = spots.size() + 1;
            while (names.contains(tr("Spot %1").arg(n))) ++n;
            spots.append(QJsonObject{ { QStringLiteral("id"), core::newBbmId() }, { QStringLiteral("name"), tr("Spot %1").arg(n) }, { QStringLiteral("track"), track } });
        }
        place.insert(QStringLiteral("spots"), spots);
    } else {
        if (list.size() >= cp::kMaxPlaces) {
            showStatus(tr("There can be up to %1 places here.").arg(cp::kMaxPlaces), 4000);
            return false;
        }
        QStringList names;
        for (const QJsonValue& p : std::as_const(map_->sidecar.layoutPlaces)) names << p.toObject().value(QLatin1String("name")).toString();
        for (const auto& m : map_->sidecar.modules)
            for (const QJsonValue& p : m.places) names << p.toObject().value(QLatin1String("name")).toString();
        int n = 1;
        while (names.contains(tr("Place %1").arg(n))) ++n;
        place = QJsonObject{ { QStringLiteral("id"), core::newBbmId() },
                             { QStringLiteral("name"), tr("Place %1").arg(n) },
                             { QStringLiteral("kind"), QStringLiteral("industry") },
                             { QStringLiteral("spots"), QJsonArray{ QJsonObject{ { QStringLiteral("id"), core::newBbmId() },
                                                                                 { QStringLiteral("name"), tr("Spot %1").arg(1) },
                                                                                 { QStringLiteral("track"), track } } } },
                             { QStringLiteral("accepts"), QJsonArray() },
                             { QStringLiteral("ships"), QJsonArray() },
                             { QStringLiteral("tags"), QJsonArray() },
                             { QStringLiteral("notes"), QString() } };
    }
    cancelPlacePick();
    const QString id = place.value(QLatin1String("id")).toString();
    undoStack_->push(new edit::SetPlaceCommand(*map_, owner, id, place, at >= 0 ? tr("Put a spot on the track") : tr("New place")));
    emit placePicked(owner, id, at < 0);
    return true;
}

void MapView::addPlaceMenu(QMenu& menu, QPointF studs) {
    if (!map_ || runReadOnly_) return;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const double reach = std::max(3.0, 20.0 / (transform().m11() * rendering::SceneBuilder::kPixelsPerStud));
    if (!track::nearestPosition(net, studs, reach)) return;
    auto* a = menu.addAction(tr("Make a Place…"));
    a->setObjectName(QStringLiteral("ctxMakePlace"));
    connect(a, &QAction::triggered, this, [this, studs] { placePickClick(studs, PlacePick{}); });
    menu.addSeparator();
}

}  // namespace bld::ui
