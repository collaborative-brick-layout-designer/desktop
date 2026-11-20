// Putting a train on the track (trains design 6.4): the Trains panel's Put
// on the track waits for a click on the track; the train's front goes
// where the click is and the rest of it the way more track lies (it tries
// both ways and takes one where it fits). One undo step. The web's
// onPlaceTrainClick (editor/EditorPage.tsx) does the same.

#include "MapView.h"

#include "../edit/TrainCommands.h"
#include "../track/Routes.h"
#include "../track/Trains.h"
#include "help/HelpButton.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <utility>

namespace bld::ui {

void MapView::startTrainPlacing(const QString& trainId) {
    if (!map_) return;
    const auto& trains = map_->sidecar.trains;
    const auto it = std::find_if(trains.begin(), trains.end(), [&](const core::Train& t) { return t.id == trainId; });
    if (it == trains.end()) return;
    if (runMode_) setRunMode(false);
    if (routeDraft_) cancelRouteDraft();
    if (scene()) scene()->clearSelection();
    placingTrain_ = trainId;
    setInteractive(false);
    if (!trainBar_) {
        auto* bar = new QFrame(viewport());
        bar->setObjectName(QStringLiteral("TrainPlaceBar"));
        bar->setFrameShape(QFrame::StyledPanel);
        bar->setAutoFillBackground(true);
        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(12, 6, 8, 6);
        trainBarText_ = new QLabel(bar);
        trainBarText_->setObjectName(QStringLiteral("TrainPlaceText"));
        row->addWidget(help::withHelp(trainBarText_, QStringLiteral("trains.putOnTrack"), bar));
        auto* cancel = new QPushButton(tr("Cancel"), bar);
        cancel->setObjectName(QStringLiteral("TrainPlaceCancel"));
        row->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, &MapView::cancelTrainPlacing);
        trainBar_ = bar;
    }
    const QString name = it->name.isEmpty() ? tr("the train") : it->name;
    trainBarText_->setText(tr("Click the track where the front of <b>%1</b> goes.").arg(name.toHtmlEscaped()));
    trainBar_->adjustSize();
    trainBar_->move(viewport()->rect().center().x() - trainBar_->width() / 2, 10);
    trainBar_->show();
    trainBar_->raise();
    if (builder_) {
        builder_->setPickedTrain(trainId);
        builder_->refreshTrains(*map_);
    }
    emit trainPlacingChanged(trainId);
}

void MapView::cancelTrainPlacing() {
    if (placingTrain_.isEmpty()) return;
    placingTrain_.clear();
    setInteractive(!runMode_ && !routeDraft_);
    if (trainBar_) trainBar_->hide();
    if (builder_) {
        builder_->setPickedTrain(QString());
        if (map_) builder_->refreshTrains(*map_);
    }
    emit trainPlacingChanged(QString());
}

bool MapView::trainPlaceClick(QPointF studs) {
    if (placingTrain_.isEmpty() || !map_) return false;
    const auto& trains = map_->sidecar.trains;
    const auto it = std::find_if(trains.begin(), trains.end(), [&](const core::Train& t) { return t.id == placingTrain_; });
    if (it == trains.end()) {
        cancelTrainPlacing();
        return false;
    }
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const double reach = std::max(3.0, 24.0 / (transform().m11() * rendering::SceneBuilder::kPixelsPerStud));
    const auto pos = track::nearestPosition(net, studs, reach);
    if (!pos) {
        showStatus(tr("Click on the track."), 3000);
        return false;
    }
    track::StockById stock;
    for (const auto& s : map_->sidecar.rollingStock) stock.insert(s.id, s);
    core::Train placed = *it;
    std::optional<core::Train> fallback;
    bool fits = false;
    for (const bool facingTo : { true, false }) {
        core::TrainPosition p;
        static_cast<core::TrackPosition&>(p) = *pos;
        p.facingTo = facingTo;
        core::Train t = *it;
        t.position = p;
        const auto where = track::placeTrain(net, t, stock, map_->sidecar.switchStates);
        if (where && !where->offTrack) {
            placed = std::move(t);
            fits = true;
            break;
        }
        if (where && !fallback) fallback = t;
        if (!fallback && facingTo) fallback = t;
    }
    if (!fits) placed = fallback.value_or(placed);
    const QString name = placed.name.isEmpty() ? tr("The train") : placed.name;
    const QString id = placed.id;
    cancelTrainPlacing();
    undoStack_->push(new edit::SetTrainCommand(*map_, id, placed, tr("Put %1 on the track").arg(name)));
    showStatus(fits ? tr("%1 is on the track.").arg(name) : tr("%1 runs off the end of the track here.").arg(name), 3000);
    return true;
}

void MapView::setShowSidingLengths(bool on) {
    showSidingLengths_ = on;
    if (!builder_) return;
    builder_->setShowSidingLengths(on);
    if (map_) builder_->refreshFit(*map_);
}

void MapView::showSpot(QPointF studs) {
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    const QRectF area((studs - QPointF(24, 24)) * px, QSizeF(48 * px, 48 * px));
    fitInView(area, Qt::KeepAspectRatio);
    // Not closer than 2:1 (a small spot isn't blown up to fill the screen).
    if (transform().m11() > 2.0) {
        setTransform(QTransform::fromScale(2.0, 2.0));
        centerOn(studs * px);
    }
    if (!builder_ || !map_) return;
    builder_->setSpotlight(studs);
    builder_->refreshFit(*map_);
    QTimer::singleShot(2500, this, [this] {
        if (!builder_) return;
        builder_->setSpotlight(std::nullopt);
        if (map_) builder_->refreshFit(*map_);
    });
}

}  // namespace bld::ui
