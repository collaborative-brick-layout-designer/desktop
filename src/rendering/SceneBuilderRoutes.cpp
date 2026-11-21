// Named routes on the map (trains design 4.2, 4.3, 6.2): each shown route
// as a wide line in its color along the track; a broken one dashed red as
// far as it still goes; a ring on each switch it needs that is set the
// other way. The route being drawn shows dashed, with its ends marked. The
// web draws the same (apps/web/src/editor/render/RouteLayer.tsx).

#include "SceneBuilder.h"

#include "../core/Map.h"
#include "../track/Routes.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPen>

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
        for (const auto& line : track::routeLines(net, r)) {
            auto* item = new QGraphicsPathItem(pathOf(line));
            item->setPen(pen);
            item->setData(0, r.id);
            mine << add(item, kZ);
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
