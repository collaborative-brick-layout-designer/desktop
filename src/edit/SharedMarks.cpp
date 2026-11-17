#include "SharedMarks.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"

#include <QPolygonF>
#include <QTransform>

#include <cmath>
#include <vector>

namespace bld::edit {

namespace {

// A part's outline on the map: its hull, turned and put at its sprite centre
// (its box when the part has no picture).
QPolygonF outlineOf(const core::Brick& b, parts::PartsLibrary& parts) {
    const QPolygonF hull = parts.hullPolygonStuds(b.partNumber.toLower());
    if (hull.size() < 3) return QPolygonF(b.displayArea);
    const QPointF c = parts::placement::imageCentre(b, parts);
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(b.orientation);
    return t.map(hull);
}

double areaOf(const QPolygonF& p) {
    double a = 0.0;
    for (qsizetype i = 0; i < p.size(); ++i) {
        const QPointF& u = p[i];
        const QPointF& v = p[(i + 1) % p.size()];
        a += u.x() * v.y() - v.x() * u.y();
    }
    return std::abs(a) / 2.0;
}

// Lying over each other by more than this (square studs) counts: parts
// that only touch along an edge don't.
constexpr double kOverlapArea = 1.0;
// A free end this close (studs) to a free end of the module was meant to meet it.
constexpr double kNearEnd = 8.0;
// ...and closer than this they do meet (the snap's own reach decides the link).
constexpr double kJoined = 0.05;

struct End {
    QPointF at;
    QString type;
};

void freeEnds(const core::Brick& b, parts::PartsLibrary& parts, std::vector<End>& out) {
    const auto meta = parts.metadata(b.partNumber);
    if (!meta) return;
    for (int i = 0; i < static_cast<int>(meta->connections.size()); ++i) {
        if (i < static_cast<int>(b.connections.size()) && !b.connections[static_cast<size_t>(i)].linkedToId.isEmpty())
            continue;
        out.push_back({ parts::placement::connectionWorld(b, i, parts), meta->connections[i].type });
    }
}

}  // namespace

SharedMarks sharedMarks(const core::Map& map, const QSet<QString>& members, parts::PartsLibrary& parts) {
    SharedMarks marks;
    if (members.isEmpty()) return marks;
    std::vector<End> mine, theirs;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& bricks = static_cast<const core::LayerBrick&>(*L).bricks;
        std::vector<const core::Brick*> in, out;
        for (const auto& b : bricks) (members.contains(b.guid) ? in : out).push_back(&b);
        if (in.empty()) continue;
        QRectF box;
        std::vector<QPolygonF> shapes;
        shapes.reserve(in.size());
        for (const core::Brick* b : in) {
            box = box.isNull() ? b->displayArea : box.united(b->displayArea);
            shapes.push_back(outlineOf(*b, parts));
            freeEnds(*b, parts, mine);
        }
        const QRectF reach = box.adjusted(-kNearEnd, -kNearEnd, kNearEnd, kNearEnd);
        for (const core::Brick* o : out) {
            if (!reach.intersects(o->displayArea)) continue;
            freeEnds(*o, parts, theirs);
            if (!box.intersects(o->displayArea)) continue;
            const QPolygonF shape = outlineOf(*o, parts);
            for (std::size_t i = 0; i < in.size(); ++i) {
                if (!in[i]->displayArea.intersects(o->displayArea)) continue;
                if (areaOf(shapes[i].intersected(shape)) > kOverlapArea) {
                    ++marks.overlaps;
                    break;
                }
            }
        }
    }
    for (const End& e : mine) {
        for (const End& o : theirs) {
            if (o.type != e.type) continue;
            const double d = std::hypot(o.at.x() - e.at.x(), o.at.y() - e.at.y());
            if (d > kJoined && d <= kNearEnd) {
                ++marks.looseEnds;
                break;
            }
        }
    }
    return marks;
}

}  // namespace bld::edit
