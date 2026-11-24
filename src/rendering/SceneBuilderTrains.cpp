// Trains on the map (trains design 3.3, 6.4): each car's body lies on the
// chord between its trucks, so on a curve it cuts the inside of the track
// as a real car does. A simple car is a rounded box in its color with a
// tick at each truck; a car shown as a part draws that part's picture. The
// head car's front is pointed, and each car shows its road number. A train
// partly off the end of the track is outlined in red. The web draws the
// same (apps/web/src/editor/render/TrainLayer.tsx).

#include "SceneBuilder.h"

#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "../track/Trains.h"

#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPen>

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace bld::rendering {

namespace {

constexpr double kZ = 0.95e7;  // over the routes, under the switch arrows

QColor darker(const QColor& c) {
    return QColor::fromRgbF(c.redF() * 0.55f, c.greenF() * 0.55f, c.blueF() * 0.55f);
}

}  // namespace

QColor SceneBuilder::trainProblemColor() { return QColor(0xDC, 0x26, 0x26); }

void SceneBuilder::clearTrains() {
    for (auto* it : trainItems_) {
        scene_.removeItem(it);
        delete it;
    }
    trainItems_.clear();
    trainCarsById_.clear();
}

void SceneBuilder::refreshTrains(const core::Map& map) {
    clearTrains();
    addTrains(map);
}

void SceneBuilder::addTrains(const core::Map& map) {
    drawnTrains_ = map.sidecar.trains;
    drawnStock_ = map.sidecar.rollingStock;
    drawnTrainSwitchStates_ = map.sidecar.switchStates;
    if (std::none_of(map.sidecar.trains.begin(), map.sidecar.trains.end(), [](const core::Train& t) { return t.position.has_value(); }))
        return;
    track::StockById stock;
    for (const auto& s : map.sidecar.rollingStock) stock.insert(s.id, s);
    const track::TrackNetwork net = track::trackNetworkOf(map, parts_);
    for (const auto& t : map.sidecar.trains) {
        if (runningTrains_.contains(t.id)) continue;  // drawn by the run (showRunFrame)
        const auto placed = track::placeTrain(net, t, stock, map.sidecar.switchStates);
        if (!placed || placed->units.empty()) continue;
        QList<QGraphicsItem*>& cars = trainCarsById_[t.id];
        for (size_t i = 0; i < placed->units.size(); ++i) {
            const track::PlacedUnit& u = placed->units[i];
            auto* body = makeCar(t, i, u, stock.value(u.stock), placed->offTrack, t.id == pickedTrain_);
            scene_.addItem(body);
            trainItems_ << body;
            cars << body;
        }
    }
}

QGraphicsPathItem* SceneBuilder::makeCar(const core::Train& t, size_t i, const track::PlacedUnit& u, const core::RollingStock& s,
                                         bool offTrack, bool picked) {
    const double px = kPixelsPerStud;
    const bool reversed = i < t.units.size() && t.units[i].reversed;
    const double len = (s.bodyLength > 0 ? s.bodyLength : s.length - 2.0) * px;
    const double wid = s.width * px;
    QColor color(s.color);
    if (!color.isValid()) color = QColor(t.color);
    if (!color.isValid()) color = QColor(0xB4, 0, 0);
    const QColor outline = offTrack ? trainProblemColor() : picked ? QColor(0x1D, 0x4E, 0xD8) : darker(color);
    const double outlineW = offTrack || picked ? 0.5 * px : 0.25 * px;

    // The body: a group of a box (or the part's picture), its trucks, nose and number, turned with the car.
    QPainterPath box;
    const double r = std::min(wid / 3.0, px);
    box.addRoundedRect(QRectF(-len / 2, -wid / 2, len, wid), r, r);
    auto* body = new QGraphicsPathItem(box);
    body->setPen(QPen(outline, outlineW));
    const bool asPart = s.lookMode != QLatin1String("simple") && !s.partKey.isEmpty();
    QPixmap pm;
    if (asPart) pm = parts_.pixmap(s.partKey);
    body->setBrush(pm.isNull() ? QBrush(color) : QBrush(Qt::NoBrush));
    body->setPos(u.centre * px);
    body->setRotation(u.angle);
    body->setZValue(kZ);
    body->setAcceptedMouseButtons(Qt::NoButton);
    body->setData(0, t.id);
    body->setData(1, u.stock);
    if (!pm.isNull()) {
        auto* pic = new QGraphicsPixmapItem(pm, body);
        pic->setTransformationMode(Qt::SmoothTransformation);
        const auto meta = parts_.metadata(s.partKey);
        const double scale = meta && meta->pxPerStud > 0 ? px / meta->pxPerStud : 1.0;
        pic->setScale(scale);
        pic->setOffset(-pm.width() / 2.0, -pm.height() / 2.0);
        pic->setRotation(reversed ? 180 : 0);
        pic->setFlag(QGraphicsItem::ItemStacksBehindParent);
    } else {
        for (const auto& tr : s.trucks) {
            const double at = (reversed ? -tr.at : tr.at) * px;
            auto* tick = new QGraphicsLineItem(at, -wid / 2 - px * 0.4, at, wid / 2 + px * 0.4, body);
            tick->setPen(QPen(QColor(0x1F, 0x29, 0x37), px * 0.6, Qt::SolidLine, Qt::RoundCap));
        }
    }
    if (i == 0) {
        QPainterPath nose;
        nose.moveTo(len / 2 - wid * 0.35, -wid / 2);
        nose.lineTo(len / 2 + wid * 0.25, 0);
        nose.lineTo(len / 2 - wid * 0.35, wid / 2);
        nose.closeSubpath();
        auto* n = new QGraphicsPathItem(nose, body);
        n->setPen(Qt::NoPen);
        n->setBrush(outline);
    }
    if (!s.number.isEmpty()) {
        auto* text = new QGraphicsSimpleTextItem(s.number, body);
        QFont f;
        f.setBold(true);
        f.setPixelSize(std::max(1, static_cast<int>(std::min(wid * 0.6, 4 * px))));
        text->setFont(f);
        text->setBrush(Qt::white);
        text->setPen(QPen(Qt::black, 0.6));
        const QRectF b = text->boundingRect();
        // Upright whichever way the car runs.
        const bool flip = std::cos(qDegreesToRadians(u.angle)) < -0.01;
        text->setTransformOriginPoint(b.center());
        text->setPos(-b.width() / 2, -b.height() / 2);
        if (flip) text->setRotation(180);
    }
    return body;
}

// ---- running trains (trains design 5) --------------------------------------

void SceneBuilder::setRunningTrains(const QSet<QString>& ids, const core::Map& map) {
    if (ids == runningTrains_) return;
    runningTrains_ = ids;
    refreshTrains(map);
    if (ids.isEmpty()) clearRun();
}

void SceneBuilder::clearRun() {
    for (const auto& cars : runCarsById_)
        for (auto* it : cars) {
            scene_.removeItem(it);
            delete it;
        }
    runCarsById_.clear();
    for (auto* it : runMarks_) {
        scene_.removeItem(it);
        delete it;
    }
    runMarks_.clear();
}

void SceneBuilder::showRunFrame(const core::Map& map, const std::vector<std::pair<QString, std::optional<track::PlacedTrain>>>& placed,
                                const std::vector<QPointF>& marks) {
    track::StockById stock;
    for (const auto& s : map.sidecar.rollingStock) stock.insert(s.id, s);
    const double px = kPixelsPerStud;
    QSet<QString> seen;
    for (const auto& [id, p] : placed) {
        seen.insert(id);
        const auto t = std::find_if(map.sidecar.trains.begin(), map.sidecar.trains.end(), [&](const core::Train& x) { return x.id == id; });
        QList<QGraphicsItem*>& cars = runCarsById_[id];
        const size_t n = p ? p->units.size() : 0;
        if (t == map.sidecar.trains.end() || static_cast<size_t>(cars.size()) != n) {
            // New, or its cars changed: drawn again.
            for (auto* it : cars) {
                scene_.removeItem(it);
                delete it;
            }
            cars.clear();
            if (t == map.sidecar.trains.end() || !p) continue;
            for (size_t i = 0; i < n; ++i) {
                auto* body = makeCar(*t, i, p->units[i], stock.value(p->units[i].stock), false, false);
                body->setZValue(kZ + 1);
                scene_.addItem(body);
                cars << body;
            }
            continue;
        }
        // The same cars: moved, and turned (numbers kept upright).
        for (size_t i = 0; i < n; ++i) {
            const track::PlacedUnit& u = p->units[i];
            QGraphicsItem* body = cars[static_cast<qsizetype>(i)];
            body->setPos(u.centre * px);
            body->setRotation(u.angle);
            const bool flip = std::cos(qDegreesToRadians(u.angle)) < -0.01;
            for (QGraphicsItem* child : body->childItems())
                if (child->type() == QGraphicsSimpleTextItem::Type) child->setRotation(flip ? 180 : 0);
        }
    }
    for (auto it = runCarsById_.begin(); it != runCarsById_.end();) {
        if (seen.contains(it.key())) {
            ++it;
            continue;
        }
        for (auto* item : it.value()) {
            scene_.removeItem(item);
            delete item;
        }
        it = runCarsById_.erase(it);
    }
    // A red ring with "!" where two trains would collide.
    if (marks != runMarkSpots_) {
        for (auto* it : runMarks_) {
            scene_.removeItem(it);
            delete it;
        }
        runMarks_.clear();
        runMarkSpots_ = marks;
        for (const QPointF& at : marks) {
            const double r = 5.0 * px;
            auto* ring = new QGraphicsEllipseItem(QRectF(-r, -r, 2 * r, 2 * r));
            ring->setPen(QPen(trainProblemColor(), 0.5 * px));
            QColor fill = trainProblemColor();
            fill.setAlphaF(0.18f);
            ring->setBrush(fill);
            ring->setPos(at * px);
            ring->setZValue(kZ + 2);
            ring->setAcceptedMouseButtons(Qt::NoButton);
            ring->setData(0, QStringLiteral("collision-mark"));
            auto* bang = new QGraphicsSimpleTextItem(QStringLiteral("!"), ring);
            QFont f;
            f.setBold(true);
            f.setPixelSize(static_cast<int>(r * 1.2));
            bang->setFont(f);
            bang->setBrush(trainProblemColor());
            const QRectF b = bang->boundingRect();
            bang->setPos(-b.width() / 2, -b.height() / 2);
            scene_.addItem(ring);
            runMarks_ << ring;
        }
    }
}

}  // namespace bld::rendering
