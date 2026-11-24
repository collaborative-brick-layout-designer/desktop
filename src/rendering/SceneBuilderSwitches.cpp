// The switches' arrows (trains design 6.3): on each switch, a short arrow
// along the way it is set, and the other ways faint, so anyone can see
// which way a train will go. The web draws the same
// (apps/web/src/editor/render/SwitchLayer.tsx).

#include "SceneBuilder.h"
#include "SceneBuilderInternal.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../track/TrackNetwork.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QPainterPath>
#include <QPen>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace bld::rendering {

namespace {

constexpr double kArrowStuds = 10.0;  // how long the arrow is (shorter on a short leg)
constexpr double kWidthStuds = 1.1;   // its width
constexpr double kZ = 1.0e7;          // over every sheet (each sheet is 1000 above the one below)

void removeAll(QGraphicsScene& scene, const QList<QGraphicsItem*>& items) {
    for (auto* it : items) {
        scene.removeItem(it);
        delete it;
    }
}

QPainterPath along(const std::vector<track::CircuitPathPoint>& pts, double length) {
    QPainterPath path;
    const double px = SceneBuilder::kPixelsPerStud;
    path.moveTo(pts.front().p * px);
    for (const auto& p : pts) {
        if (p.s > length) break;
        path.lineTo(p.p * px);
    }
    path.lineTo(track::pointAt(pts, length).p * px);
    return path;
}

}  // namespace

QColor SceneBuilder::switchSetColor() { return QColor(0x16, 0xA3, 0x4A); }
QColor SceneBuilder::switchTurnedColor() { return QColor(0xEA, 0x58, 0x0C); }

void SceneBuilder::clearSwitches() {
    removeAll(scene_, switchItems_);
    switchItems_.clear();
    switchArrowByKey_.clear();
}

void SceneBuilder::refreshSwitches(const core::Map& map) {
    clearSwitches();
    addSwitches(map);
}

void SceneBuilder::addSwitches(const core::Map& map) {
    drawnSwitchStates_ = map.sidecar.switchStates;
    if (!switchesShown_) return;
    // Parts on hidden sheets show no arrow.
    QSet<QString> hidden;
    for (const auto& layer : map.layers())
        if (layer && layer->kind() == core::LayerKind::Brick && !layer->visible)
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) hidden.insert(b.guid);
    const track::TrackNetwork net = track::trackNetworkOf(map, parts_);
    const double px = kPixelsPerStud;
    const double w = kWidthStuds * px;
    for (const track::SwitchPoint& sw : net.switches()) {
        if (hidden.contains(sw.brick)) continue;
        const int setExit = net.settingOf(sw, switchStatesShown_ ? *switchStatesShown_ : map.sidecar.switchStates);
        double len = kArrowStuds;
        for (int exit : sw.exits) {
            const auto& pts = net.path(sw.brick, sw.heel, exit);
            if (pts.size() > 1) len = std::min(len, pts.back().s * 0.6);
        }
        const auto& set = net.path(sw.brick, sw.heel, setExit);
        if (set.size() < 2) continue;
        const QColor color = setExit == sw.defaultExit ? switchSetColor() : switchTurnedColor();
        // The other ways, faint and dashed.
        for (int exit : sw.exits) {
            if (exit == setExit) continue;
            const auto& pts = net.path(sw.brick, sw.heel, exit);
            if (pts.size() < 2) continue;
            QPen pen(QColor(0x9C, 0xA3, 0xAF, 204), w * 0.45, Qt::CustomDashLine, Qt::RoundCap);
            pen.setDashPattern({ 2.2, 2.2 });
            auto* other = new QGraphicsPathItem(along(pts, len));
            other->setPen(pen);
            other->setZValue(kZ);
            scene_.addItem(other);
            switchItems_ << other;
        }
        // The set way: a white outline, the colored line, and its head.
        const QPainterPath line = along(set, std::max(0.0, len - 1.2));
        auto* halo = new QGraphicsPathItem(along(set, len));
        halo->setPen(QPen(Qt::white, w * 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        halo->setZValue(kZ + 1);
        scene_.addItem(halo);
        auto* arrow = new QGraphicsPathItem(line);
        arrow->setPen(QPen(color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        arrow->setZValue(kZ + 2);
        arrow->setData(0, sw.key);
        scene_.addItem(arrow);
        const auto tip = track::pointAt(set, len);
        const auto back = track::pointAt(set, std::max(0.0, len - 0.5));
        QPointF dir = tip.p - back.p;
        const double d = std::hypot(dir.x(), dir.y());
        dir = d > 0 ? dir / d : QPointF(1, 0);
        const QPointF side(-dir.y(), dir.x());
        const QPointF t = tip.p * px;
        auto* head = new QGraphicsPolygonItem(QPolygonF{ t, t - dir * (w * 1.8) + side * (w * 1.1), t - dir * (w * 1.8) - side * (w * 1.1) });
        head->setPen(QPen(color, w * 0.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        head->setBrush(color);
        head->setZValue(kZ + 2);
        scene_.addItem(head);
        const QPointF heel = set.front().p * px;
        auto* dot = new QGraphicsEllipseItem(QRectF(heel - QPointF(w * 0.9, w * 0.9), QSizeF(w * 1.8, w * 1.8)));
        dot->setPen(QPen(color, w * 0.5));
        dot->setBrush(Qt::white);
        dot->setZValue(kZ + 3);
        scene_.addItem(dot);
        for (QGraphicsItem* it : { static_cast<QGraphicsItem*>(halo), static_cast<QGraphicsItem*>(arrow),
                                   static_cast<QGraphicsItem*>(head), static_cast<QGraphicsItem*>(dot) }) {
            it->setAcceptedMouseButtons(Qt::NoButton);
            switchItems_ << it;
        }
        switchArrowByKey_.insert(sw.key, arrow);
    }
    for (QGraphicsItem* it : switchItems_) it->setAcceptedMouseButtons(Qt::NoButton);
}

}  // namespace bld::rendering
