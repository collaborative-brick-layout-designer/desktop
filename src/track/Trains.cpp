#include "Trains.h"

#include "CircuitPath.h"

#include <QtMath>

#include <algorithm>
#include <utility>
#include <cmath>
#include <limits>

namespace bld::track {

namespace {

double dist(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

std::optional<QPointF> at(const TrackLine& line, double d) { return lineAt(line, d); }

// The first distance past `from` where the line is `chord` studs (straight
// line) from `q`, by stepping along the line then halving; none when the
// line ends first.
std::optional<double> chordBehind(const TrackLine& line, QPointF q, double from, double chord) {
    const double end = line.pts.empty() ? 0.0 : line.pts.back().d;
    const auto distAt = [&](double d) { return dist(*at(line, d), q); };
    double lo = from;
    for (double d = from + 0.5;; d += 0.5) {
        const double hi = std::min(d, end);
        if (distAt(hi) >= chord) {
            double a = lo;
            double b = hi;
            for (int k = 0; k < 50; ++k) {
                const double m = (a + b) / 2;
                if (distAt(m) >= chord) b = m;
                else a = m;
            }
            return b;
        }
        if (hi >= end) return std::nullopt;
        lo = hi;
    }
}

struct Running {
    double front = 0.0;
    double rear = 0.0;
    double half = 0.0;
};

// A unit's trucks and couplers in the order it runs (turned round: mirrored).
Running runningGeometry(const core::RollingStock& s, bool reversed) {
    std::vector<double> sorted;
    sorted.reserve(s.trucks.size());
    for (const auto& t : s.trucks) sorted.push_back(t.at);
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    const double front = reversed ? -sorted.back() : sorted.front();
    const double rear = reversed ? -sorted.front() : sorted.back();
    return { front, rear, s.length / 2.0 };
}

}  // namespace

std::optional<QPointF> lineAt(const TrackLine& line, double d) {
    const auto& pts = line.pts;
    if (pts.empty() || d > pts.back().d + 1e-9) return std::nullopt;
    if (d <= pts.front().d) return pts.front().p;
    // The first point at or past d (distances only grow along the line).
    size_t lo = 1;
    size_t hi = pts.size() - 1;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (pts[mid].d >= d) hi = mid;
        else lo = mid + 1;
    }
    const auto& a = pts[lo - 1];
    const auto& b = pts[lo];
    const double f = b.d > a.d ? (d - a.d) / (b.d - a.d) : 0.0;
    return QPointF(a.p.x() + (b.p.x() - a.p.x()) * f, a.p.y() + (b.p.y() - a.p.y()) * f);
}

TrackLine trackBehind(const TrackNetwork& net, const core::TrainPosition& head, double length, const QMap<QString, int>& states) {
    TrackLine out;
    double d = 0.0;
    // The way back through the head's brick: towards `from` when facing `to`.
    QString brick = head.brick;
    int enter = head.facingTo ? head.to : head.from;
    int leave = head.facingTo ? head.from : head.to;
    const std::vector<CircuitPathPoint>* pts = &net.path(brick, enter, leave);
    if (pts->size() < 2) {
        out.ended = true;
        return out;
    }
    const double len0 = pts->back().s;
    double start = head.facingTo ? len0 - std::max(0.0, std::min(head.s, len0)) : std::max(0.0, std::min(head.s, len0));
    for (int guard = 0; guard < 100000; ++guard) {
        if (out.pts.empty()) out.pts.push_back({ pointAt(*pts, start).p, 0.0, brick });
        for (const auto& q : *pts) {
            if (q.s <= start) continue;
            const QPointF prev = out.pts.back().p;
            d += dist(q.p, prev);
            out.pts.push_back({ q.p, d, brick });
            if (d >= length) return out;
        }
        const auto next = net.partner({ brick, leave });
        if (!next) {
            out.ended = true;
            return out;
        }
        brick = next->brick;
        enter = next->conn;
        const SwitchPoint* sw = net.switchAt(switchKey(brick, enter));
        const QVector<int> exits = net.exits(brick, enter);
        if (exits.isEmpty()) {
            out.ended = true;
            return out;
        }
        leave = sw ? net.settingOf(*sw, states) : exits.front();
        pts = &net.path(brick, enter, leave);
        start = 0.0;
    }
    out.ended = true;
    return out;
}

std::optional<PlacedTrain> placeTrain(const TrackNetwork& net, const core::Train& train, const StockById& stock,
                                      const QMap<QString, int>& states) {
    if (!train.position) return std::nullopt;
    TrackLine line = trackBehind(net, *train.position, trainLength(train, stock) + 64.0, states);
    if (line.pts.size() < 2) return std::nullopt;
    return placeOnLine(std::move(line), train.units, stock);
}

PlacedTrain placeOnLine(TrackLine line, const std::vector<core::TrainUnit>& trainUnits, const StockById& stock) {
    std::vector<std::pair<const core::TrainUnit*, const core::RollingStock*>> units;
    for (const auto& u : trainUnits) {
        const auto it = stock.constFind(u.stock);
        if (it == stock.constEnd()) continue;
        units.push_back({ &u, &it.value() });
    }
    PlacedTrain out;
    out.line = line;
    std::optional<QPointF> couplerAt;
    double along = 0.0;
    // A unit placed with its front truck `frontD` along the line: none when its rear truck runs off the end.
    const auto place = [&line](const core::RollingStock& s, const Running& g, double frontD) -> std::optional<PlacedUnit> {
        const double spacing = g.front - g.rear;
        const auto frontTruck = at(line, frontD);
        if (!frontTruck) return std::nullopt;
        const auto rearD = chordBehind(line, *frontTruck, frontD, spacing);
        if (!rearD) return std::nullopt;
        const QPointF rearTruck = *at(line, *rearD);
        const double ux = (frontTruck->x() - rearTruck.x()) / spacing;
        const double uy = (frontTruck->y() - rearTruck.y()) / spacing;
        const QPointF centre(frontTruck->x() - ux * g.front, frontTruck->y() - uy * g.front);
        PlacedUnit p;
        p.stock = s.id;
        p.frontTruck = *frontTruck;
        p.rearTruck = rearTruck;
        p.centre = centre;
        p.angle = qRadiansToDegrees(std::atan2(uy, ux));
        p.frontCoupler = QPointF(centre.x() + ux * g.half, centre.y() + uy * g.half);
        p.rearCoupler = QPointF(centre.x() - ux * g.half, centre.y() - uy * g.half);
        p.back = *rearD + (g.half + g.rear);
        p.frontD = frontD;
        p.rearD = *rearD;
        return p;
    };
    for (const auto& [u, s] : units) {
        const Running g = runningGeometry(*s, u->reversed);
        std::optional<PlacedUnit> unit;
        if (!couplerAt) {
            unit = place(*s, g, 0.0);
        } else {
            // The front truck where this unit's front coupler meets the last one's
            // rear coupler: scanned a quarter stud at a time, then narrowed.
            const QPointF r = *couplerAt;
            const auto gap = [&](double d) {
                const auto p = place(*s, g, d);
                return p ? dist(p->frontCoupler, r) : std::numeric_limits<double>::infinity();
            };
            const double first = std::max(0.0, along);
            double bestD = first;
            double best = gap(first);
            // (counted in quarter studs: the web adds 0.25 each step, the same places to 1e-12)
            const int steps = static_cast<int>(std::floor(((g.half - g.front) + 8.0) / 0.25 + 1e-9));
            for (int k = 0; k <= steps; ++k) {
                const double d = first + k * 0.25;
                const double v = gap(d);
                if (v < best) {
                    best = v;
                    bestD = d;
                }
            }
            double lo = std::max(0.0, bestD - 0.25);
            double hi = bestD + 0.25;
            for (int k = 0; k < 60; ++k) {
                const double m1 = lo + (hi - lo) / 3;
                const double m2 = hi - (hi - lo) / 3;
                if (gap(m1) <= gap(m2)) hi = m2;
                else lo = m1;
            }
            if (!std::isinf(best)) unit = place(*s, g, (lo + hi) / 2);
        }
        if (!unit) {
            out.offTrack = true;
            return out;
        }
        out.units.push_back(*unit);
        couplerAt = unit->rearCoupler;
        along = unit->back;
    }
    return out;
}

double trainLength(const core::Train& train, const StockById& stock) {
    double n = 0.0;
    for (const auto& u : train.units) {
        const auto it = stock.constFind(u.stock);
        if (it != stock.constEnd()) n += it->length;
    }
    return n;
}

core::Train turnedAround(const TrackNetwork& net, const core::Train& train, const StockById& stock, const QMap<QString, int>& states) {
    core::Train out = train;
    out.units.assign(train.units.rbegin(), train.units.rend());
    for (auto& u : out.units) u.reversed = !u.reversed;
    const auto placed = placeTrain(net, train, stock, states);
    if (!placed || placed->offTrack || placed->units.empty() || !train.position) return out;
    const QPointF tail = placed->units.back().rearTruck;
    const QPointF ahead = placed->units.back().frontTruck;
    // The tail's place on the track, and which way is "behind" for the new head: away from the rest of the train.
    std::optional<core::TrainPosition> best;
    double bestD = 1.0;
    for (const QString& id : net.brickIds()) {
        for (const auto& [i, j] : net.trackOf(id)->paths) {
            const auto& pts = net.path(id, i, j);
            for (size_t k = 1; k < pts.size(); ++k) {
                const auto& a = pts[k - 1];
                const auto& b = pts[k];
                const double dx = b.p.x() - a.p.x();
                const double dy = b.p.y() - a.p.y();
                const double l2 = dx * dx + dy * dy;
                const double t = l2 > 0 ? std::clamp(((tail.x() - a.p.x()) * dx + (tail.y() - a.p.y()) * dy) / l2, 0.0, 1.0) : 0.0;
                const double dd = std::hypot(a.p.x() + dx * t - tail.x(), a.p.y() + dy * t - tail.y());
                if (dd < bestD) {
                    bestD = dd;
                    // The new head's cars lie towards the old head: "behind" (facing 'from' looks back towards `to`) is that way.
                    const bool towards = (ahead.x() - tail.x()) * dx + (ahead.y() - tail.y()) * dy > 0;
                    core::TrainPosition p;
                    p.brick = id;
                    p.from = i;
                    p.to = j;
                    p.s = a.s + (b.s - a.s) * t;
                    p.facingTo = !towards;
                    best = p;
                }
            }
        }
    }
    if (best) out.position = std::move(best);
    return out;
}

std::optional<double> partStockLength(const std::vector<std::pair<QString, QPointF>>& conns) {
    std::vector<QPointF> magnets;
    for (const auto& [type, p] : conns)
        if (type == QLatin1String("magnet")) magnets.push_back(p);
    if (magnets.size() != 2) return std::nullopt;
    return dist(magnets[0], magnets[1]);
}

}  // namespace bld::track
