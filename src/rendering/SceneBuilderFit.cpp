// Fit checks on the map (trains design 3.5, 6.2): with View > Siding Lengths,
// each run of track labeled with the length a train can stand in; a ring at
// each spot a check is about (red: a problem, amber: a warning); and the
// spot "Show me" brought into view, ringed. The web's
// apps/web/src/editor/render/FitLayer.tsx draws the same.

#include "SceneBuilder.h"

#include "../core/Map.h"
#include "../edit/venue/VenueUnits.h"
#include "../track/FitChecks.h"

#include <QBrush>
#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPen>

namespace bld::rendering {

namespace {

constexpr double kZ = 0.97e7;  // over the trains, under the switch arrows

}  // namespace

void SceneBuilder::clearFit() {
    for (auto* it : fitItems_) {
        scene_.removeItem(it);
        delete it;
    }
    fitItems_.clear();
    sidingLabels_ = 0;
    checkRings_ = 0;
    spotlightShown_ = false;
}

void SceneBuilder::refreshFit(const core::Map& map) {
    clearFit();
    addFit(map);
}

void SceneBuilder::addFit(const core::Map& map) {
    drawnFitRoutes_ = map.sidecar.routes;
    const double px = kPixelsPerStud;
    const auto add = [this](QGraphicsItem* it, double z) {
        it->setZValue(z);
        it->setAcceptedMouseButtons(Qt::NoButton);
        scene_.addItem(it);
        fitItems_ << it;
    };
    const bool anyTrain = std::any_of(map.sidecar.trains.begin(), map.sidecar.trains.end(), [](const core::Train& t) { return t.position.has_value(); });
    if (spotlight_) {
        const double r = 4.5 * px;
        auto* ring = new QGraphicsEllipseItem(QRectF(*spotlight_ * px - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        ring->setPen(QPen(QColor(0x1D, 0x4E, 0xD8), 0.6 * px));
        add(ring, kZ + 2);
        spotlightShown_ = true;
    }
    if (!anyTrain && map.sidecar.routes.empty() && !showSidingLengths_) return;
    const track::TrackNetwork net = track::trackNetworkOf(map, parts_);
    const auto runs = track::trackRuns(net);
    if (showSidingLengths_) {
        const auto unit = edit::venue::accountLengthUnit();
        QFont f;
        f.setBold(true);
        f.setPixelSize(static_cast<int>(1.6 * px));
        for (const auto& run : runs) {
            auto* text = new QGraphicsSimpleTextItem(edit::venue::formatLength(run.usable, unit));
            text->setFont(f);
            text->setBrush(QColor(0x0F, 0x17, 0x2A));
            const QRectF b = text->boundingRect();
            const QPointF c = run.middle * px;
            auto* tag = new QGraphicsRectItem(QRectF(c.x() - b.width() / 2 - 3, c.y() - b.height() - 6, b.width() + 6, b.height() + 4));
            tag->setBrush(QColor(255, 255, 255, 235));
            tag->setPen(QPen(QColor(0x33, 0x41, 0x55), 1));
            text->setPos(c.x() - b.width() / 2, c.y() - b.height() - 4);
            tag->setData(0, QStringLiteral("siding-length"));
            add(tag, kZ);
            add(text, kZ + 0.5);
            ++sidingLabels_;
        }
    }
    if (!anyTrain && map.sidecar.routes.empty()) return;
    track::FitInput in;
    in.trains = map.sidecar.trains;
    for (const auto& s : map.sidecar.rollingStock) in.stock.insert(s.id, s);
    in.routes = map.sidecar.routes;
    in.states = map.sidecar.switchStates;
    const double r = 2.5 * px;
    for (const auto& c : track::fitChecks(net, in, &runs)) {
        auto* ring = new QGraphicsEllipseItem(QRectF(c.at * px - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        QPen pen(c.error ? QColor(0xDC, 0x26, 0x26) : QColor(0xD9, 0x77, 0x06), 0.4 * px, Qt::DashLine);
        ring->setPen(pen);
        ring->setData(0, c.key);
        add(ring, kZ + 1);
        ++checkRings_;
    }
}

}  // namespace bld::rendering
