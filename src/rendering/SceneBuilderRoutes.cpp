// Named routes on the map (trains design 4.2, 4.3, 6.2): each shown route
// as a wide line in its color along the track; a broken one dashed red as
// far as it still goes; a ring on each switch it needs that is set the
// other way. The route being drawn shows dashed, with its ends marked.
// Arrows along each show which way the train runs. The web draws the same
// (apps/web/src/editor/render/RouteLayer.tsx).

#include "SceneBuilder.h"

#include "../core/Map.h"
#include "../track/Routes.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace bld::rendering {

namespace {

constexpr double kZ = 0.9e7;          // over every sheet, under the switch arrows
constexpr double kWidthStuds = 2.4;

void removeRouteItems(QGraphicsScene& scene, const QList<QGraphicsItem*>& items) {
    for (auto* it : items) {
        scene.removeItem(it);
        delete it;
    }
}

QPainterPath pathOf(const std::vector<QPointF>& pts) {
    QPainterPath p;
    const double px = SceneBuilder::kPixelsPerStud;
    p.moveTo(pts.front() * px);
    for (size_t i = 1; i < pts.size(); ++i) p.lineTo(pts[i] * px);
    return p;
}

// How far apart the arrows along a route are (studs).
constexpr double kArrowEvery = 48.0;

struct Arrow {
    QPointF at;   // studs
    QPointF dir;  // unit
};

// Arrows along a line, the way it runs: one every kArrowEvery studs, the
// first half that in (one in the middle of a shorter line).
std::vector<Arrow> arrowsAlong(const std::vector<QPointF>& line) {
    double total = 0;
    for (size_t i = 1; i < line.size(); ++i) total += std::hypot(line[i].x() - line[i - 1].x(), line[i].y() - line[i - 1].y());
    std::vector<Arrow> out;
    if (total <= 1e-9) return out;
    double next = total < kArrowEvery ? total / 2 : kArrowEvery / 2;
    double run = 0;
    for (size_t i = 1; i < line.size(); ++i) {
        const QPointF a = line[i - 1], b = line[i];
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (len <= 1e-9) continue;
        while (next <= run + len) {
            const double t = (next - run) / len;
            out.push_back({ a + (b - a) * t, (b - a) / len });
            next += kArrowEvery;
        }
        run += len;
    }
    return out;
}

// A chevron at an arrow, in scene pixels: wing, tip, wing.
QPainterPath chevron(const Arrow& a, double size) {
    const QPointF c = a.at * SceneBuilder::kPixelsPerStud;
    const QPointF d = a.dir * size;
    const QPointF n(-d.y(), d.x());
    QPainterPath p;
    p.moveTo(c - d + n);
    p.lineTo(c + d);
    p.lineTo(c - d - n);
    return p;
}

}  // namespace

QColor SceneBuilder::routeBrokenColor() { return QColor(0xDC, 0x26, 0x26); }

void SceneBuilder::clearRoutes() {
    removeRouteItems(scene_, routeItems_);
    routeItems_.clear();
    routeItemsById_.clear();
}

void SceneBuilder::refreshRoutes(const core::Map& map) {
    clearRoutes();
    addRoutes(map);
}

void SceneBuilder::addRoutes(const core::Map& map) {
    drawnRoutes_ = map.sidecar.routes;
    drawnRouteSwitchStates_ = map.sidecar.switchStates;
    bool any = routeDraft_.has_value();
    for (const auto& r : map.sidecar.routes) any = any || !hiddenRoutes_.contains(r.id);
    if (!any) return;
    const track::TrackNetwork net = track::trackNetworkOf(map, parts_);
    const double w = kWidthStuds * kPixelsPerStud;
    const auto add = [this](QGraphicsItem* it, double z) {
        it->setZValue(z);
        it->setAcceptedMouseButtons(Qt::NoButton);
        scene_.addItem(it);
        routeItems_ << it;
        return it;
    };
    for (const auto& r : map.sidecar.routes) {
        if (hiddenRoutes_.contains(r.id)) continue;
        const track::RouteStatus st = track::routeStatus(net, r, map.sidecar.switchStates);
        QColor color = st.broken ? routeBrokenColor() : QColor(r.color);
        if (!color.isValid()) color = QColor(0x3B, 0x82, 0xF6);
        color.setAlphaF(0.55f);
        QPen pen(color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (st.broken) pen.setDashPattern({ 1.2, 0.8 });
        QList<QGraphicsItem*>& mine = routeItemsById_[r.id];
        const QPen arrowPen(QColor(255, 255, 255, 242), w * 0.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        for (const auto& line : track::routeLines(net, r)) {
            auto* item = new QGraphicsPathItem(pathOf(line));
            item->setPen(pen);
            item->setData(0, r.id);
            mine << add(item, kZ);
            for (const Arrow& a : arrowsAlong(line)) {
                auto* arrow = new QGraphicsPathItem(chevron(a, w * 0.9));
                arrow->setPen(arrowPen);
                arrow->setData(0, QStringLiteral("route-arrow"));
                arrow->setData(1, r.id);
                add(arrow, kZ + 0.5);
            }
        }
        if (!st.broken)
            for (const QString& key : st.setAgainst)
                if (const track::SwitchPoint* sw = net.switchAt(key))
                    if (const auto at = net.port(sw->brick, sw->heel)) {
                        const QPointF c = at->first * kPixelsPerStud;
                        auto* ring = new QGraphicsEllipseItem(QRectF(c - QPointF(w * 1.3, w * 1.3), QSizeF(w * 2.6, w * 2.6)));
                        ring->setPen(QPen(QColor(0xEA, 0x58, 0x0C), w * 0.45));
                        ring->setBrush(Qt::NoBrush);
                        add(ring, kZ + 1);
                    }
    }
    if (routeDraft_) {
        QPen pen(QColor(0x1D, 0x4E, 0xD8, 180), w, Qt::CustomDashLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setDashPattern({ 1.5, 1.0 });
        for (const auto& line : routeDraft_->lines)
            if (line.size() > 1) {
                auto* item = new QGraphicsPathItem(pathOf(line));
                item->setPen(pen);
                add(item, kZ + 2);
                for (const Arrow& a : arrowsAlong(line)) {
                    auto* arrow = new QGraphicsPathItem(chevron(a, w * 0.9));
                    arrow->setPen(QPen(QColor(0x1D, 0x4E, 0xD8), w * 0.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    arrow->setData(0, QStringLiteral("route-arrow"));
                    arrow->setData(1, QStringLiteral("draft"));
                    add(arrow, kZ + 2.5);
                }
            }
        for (const QPointF& e : routeDraft_->ends) {
            const QPointF c = e * kPixelsPerStud;
            auto* dot = new QGraphicsEllipseItem(QRectF(c - QPointF(w * 1.2, w * 1.2), QSizeF(w * 2.4, w * 2.4)));
            dot->setPen(QPen(QColor(0x1D, 0x4E, 0xD8), w * 0.5));
            dot->setBrush(Qt::white);
            add(dot, kZ + 3);
        }
    }
}

}  // namespace bld::rendering
