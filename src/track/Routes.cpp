#include "Routes.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace bld::track {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(Routes)
};

QString keyOf(const RouteStep& s) { return s.brick + QChar(0) + QString::number(s.from) + QChar(0) + QString::number(s.to); }

bool onStep(const TrackPosition& p, const RouteStep& s) {
    return p.brick == s.brick && ((p.from == s.from && p.to == s.to) || (p.from == s.to && p.to == s.from));
}

// One way a search can be at a place: on `step`, `s` studs along it, `cost` studs from the start, by `steps`.
struct Arrival {
    RouteStep step;
    double s = 0.0;
    double cost = 0.0;
    std::vector<RouteStep> steps;
};

// How close (studs) another way through a brick must pass to count as the same place.
constexpr double kSameSpot = 0.5;

// Where `at` falls on a path: the distance to it and how far along (studs).
std::pair<double, double> projectOnPath(const std::vector<CircuitPathPoint>& pts, QPointF at) {
    double bestD = std::numeric_limits<double>::infinity(), bestS = 0.0;
    for (size_t k = 1; k < pts.size(); ++k) {
        const auto& a = pts[k - 1];
        const auto& b = pts[k];
        const double dx = b.p.x() - a.p.x(), dy = b.p.y() - a.p.y();
        const double l2 = dx * dx + dy * dy;
        const double t = l2 > 0 ? std::clamp(((at.x() - a.p.x()) * dx + (at.y() - a.p.y()) * dy) / l2, 0.0, 1.0) : 0.0;
        const double d = std::hypot(a.p.x() + dx * t - at.x(), a.p.y() + dy * t - at.y());
        if (d < bestD - 1e-9) {
            bestD = d;
            bestS = a.s + (b.s - a.s) * t;
        }
    }
    return { bestD, bestS };
}

// One leg of a search: from each of `sources` (a train on its step, heading
// towards the step's `to`) to any of `targets`. Every way it can arrive
// comes back, one per step, in the order first found: the next leg starts
// from each. A source is its own search node (only part of its step is
// ahead of it), so the way may come round and run the whole of that step
// again. `last`: only the cheapest arrival matters, so stop once it is sure.
std::vector<Arrival> searchLeg(const TrackNetwork& net, const std::vector<Arrival>& sources, const std::vector<TrackPosition>& targets,
                               const FindOptions& opts, bool last) {
    // A node: a source (index) or a step entered at its start.
    struct Node {
        int src = -1;
        RouteStep step;
    };
    struct Arrived {
        RouteStep step;
        double s;
        double cost;
        std::optional<Node> prev;  // nullopt: there at once, on source `src`'s own step
        int src;
    };
    std::vector<Arrived> arrivals;
    QHash<QString, size_t> arrivalAt;
    double best = std::numeric_limits<double>::infinity();
    const auto arrive = [&](const QString& key, const RouteStep& step, double s, double cost, const std::optional<Node>& prev, int src) {
        const auto it = arrivalAt.constFind(key);
        if (it != arrivalAt.constEnd()) {
            if (cost >= arrivals[*it].cost - 1e-9) return;
            arrivals[*it] = { step, s, cost, prev, src };
        } else {
            arrivalAt.insert(key, arrivals.size());
            arrivals.push_back({ step, s, cost, prev, src });
        }
        if (cost < best) best = cost;
    };
    const auto targetOn = [&](const RouteStep& step) -> const TrackPosition* {
        for (const auto& t : targets)
            if (onStep(t, step)) return &t;
        return nullptr;
    };
    QHash<QString, double> dist;
    QHash<QString, Node> prev;
    struct Open {
        Node node;
        QString key;
        double d;
    };
    std::vector<Open> open;
    for (size_t i = 0; i < sources.size(); ++i) {
        const Arrival& src = sources[i];
        // The target is on the source's step, ahead: there at once.
        if (const TrackPosition* t = targetOn(src.step)) {
            const double off = offsetOnStep(net, *t, src.step);
            if (off >= src.s - 1e-9) arrive(keyOf(src.step), src.step, off, src.cost + off - src.s, std::nullopt, static_cast<int>(i));
        }
        open.push_back({ Node{ static_cast<int>(i), {} }, keyOf(src.step) + QChar(1) + QString::number(i),
                         src.cost + net.pathLength(src.step.brick, src.step.from, src.step.to) - src.s });
    }
    QSet<QString> done;
    while (!open.empty()) {
        size_t bi = 0;
        for (size_t i = 1; i < open.size(); ++i) {
            const Open& a = open[i];
            const Open& b = open[bi];
            if (a.d < b.d - 1e-9 || (std::abs(a.d - b.d) <= 1e-9 && a.key < b.key)) bi = i;
        }
        const Open cur = open[bi];
        open.erase(open.begin() + static_cast<std::ptrdiff_t>(bi));
        if (done.contains(cur.key)) continue;
        done.insert(cur.key);
        if (last && cur.d >= best) break;
        const RouteStep& step = cur.node.src >= 0 ? sources[static_cast<size_t>(cur.node.src)].step : cur.node.step;
        const auto linked = net.partner({ step.brick, step.to });
        if (!linked) continue;
        const PortRef next = linked.value_or(PortRef{});
        QVector<int> exits = net.exits(next.brick, next.conn);
        if (opts.followSwitches)
            if (const SwitchPoint* sw = net.switchAt(switchKey(next.brick, next.conn))) exits = { net.settingOf(*sw, opts.states) };
        for (int x : exits) {
            const RouteStep ns{ next.brick, next.conn, x };
            const QString nk = keyOf(ns);
            if (const TrackPosition* t = targetOn(ns)) {
                const double off = offsetOnStep(net, *t, ns);
                arrive(nk, ns, off, cur.d + off, cur.node, -1);
                continue;
            }
            const double nd = cur.d + net.pathLength(ns.brick, ns.from, ns.to);
            if (nd < dist.value(nk, std::numeric_limits<double>::infinity()) - 1e-9) {
                dist.insert(nk, nd);
                prev.insert(nk, cur.node);
                open.push_back({ Node{ -1, ns }, nk, nd });
            }
        }
    }
    std::vector<Arrival> out;
    for (const Arrived& a : arrivals) {
        if (!a.prev) {
            out.push_back({ a.step, a.s, a.cost, sources[static_cast<size_t>(a.src)].steps });
            continue;
        }
        std::deque<RouteStep> tail{ a.step };
        Node n = *a.prev;
        while (n.src < 0) {
            tail.push_front(n.step);
            n = prev.value(keyOf(n.step));
        }
        std::vector<RouteStep> steps = sources[static_cast<size_t>(n.src)].steps;
        steps.insert(steps.end(), tail.begin(), tail.end());
        out.push_back({ a.step, a.s, a.cost, std::move(steps) });
    }
    return out;
}

bool joined(const TrackNetwork& net, const QString& a, const QString& b) {
    QSet<QString> seen{ a };
    std::deque<QString> queue{ a };
    while (!queue.empty()) {
        const QString brick = queue.front();
        queue.pop_front();
        if (brick == b) return true;
        const PartTrack* t = net.trackOf(brick);
        if (!t) continue;
        for (int c = 0; c < t->kinds.size(); ++c)
            if (const auto p = net.partner({ brick, c }); p && !seen.contains(p->brick)) {
                seen.insert(p->brick);
                queue.push_back(p->brick);
            }
    }
    return false;
}

QStringList turnsAroundIn(const std::vector<RouteStep>& steps) {
    QSet<QString> seen;
    for (const auto& s : steps) seen.insert(keyOf(s));
    QSet<QString> out;
    for (const auto& s : steps)
        if (seen.contains(keyOf({ s.brick, s.to, s.from }))) out.insert(s.brick);
    QStringList list(out.begin(), out.end());
    list.sort();
    return list;
}

}  // namespace

double offsetOnStep(const TrackNetwork& net, const TrackPosition& pos, const RouteStep& step) {
    const double len = net.pathLength(step.brick, step.from, step.to);
    const double s = std::clamp(pos.s, 0.0, len);
    return pos.from == step.from ? s : len - s;
}

std::optional<TrackPosition> nearestPosition(const TrackNetwork& net, QPointF at, double reach) {
    std::optional<TrackPosition> best;
    double bestD = reach;
    for (const QString& id : net.brickIds())
        for (const auto& [i, j] : net.trackOf(id)->paths) {
            const auto& pts = net.path(id, i, j);
            for (size_t k = 1; k < pts.size(); ++k) {
                const auto& a = pts[k - 1];
                const auto& b = pts[k];
                const double dx = b.p.x() - a.p.x(), dy = b.p.y() - a.p.y();
                const double l2 = dx * dx + dy * dy;
                const double t = l2 > 0 ? std::clamp(((at.x() - a.p.x()) * dx + (at.y() - a.p.y()) * dy) / l2, 0.0, 1.0) : 0.0;
                const double d = std::hypot(a.p.x() + dx * t - at.x(), a.p.y() + dy * t - at.y());
                if (d < bestD - 1e-9) {
                    bestD = d;
                    best = TrackPosition{ id, i, j, a.s + (b.s - a.s) * t };
                }
            }
        }
    return best;
}

QMap<QString, int> routeSwitches(const TrackNetwork& net, const std::vector<RouteStep>& steps) {
    QMap<QString, int> out;
    for (const auto& s : steps)
        if (const SwitchPoint* sw = net.switchAt(switchKey(s.brick, s.from)); sw && sw->exits.contains(s.to) && !out.contains(sw->key))
            out.insert(sw->key, s.to);
    return out;
}

QStringList routeBothWays(const TrackNetwork& net, const std::vector<RouteStep>& steps) {
    QMap<QString, int> first;
    QSet<QString> out;
    for (const auto& s : steps) {
        const SwitchPoint* sw = net.switchAt(switchKey(s.brick, s.from));
        if (!sw || !sw->exits.contains(s.to)) continue;
        if (!first.contains(sw->key)) first.insert(sw->key, s.to);
        else if (first.value(sw->key) != s.to) out.insert(sw->key);
    }
    QStringList list(out.begin(), out.end());
    list.sort();
    return list;
}

Facing routeFacing(const Route& route) {
    if (route.steps.empty()) return Facing::To;
    const RouteStep& first = route.steps.front();
    // The start's own path, or another leg of a switch sharing an end with it (sameSpot).
    return first.brick == route.from.brick && (first.from == route.from.to || first.to == route.from.from) ? Facing::From : Facing::To;
}

std::vector<TrackPosition> sameSpot(const TrackNetwork& net, const TrackPosition& pos) {
    std::vector<TrackPosition> out{ pos };
    const PartTrack* t = net.trackOf(pos.brick);
    const auto at = net.pointAtPosition(pos);
    if (!t || !at) return out;
    for (const auto& [i, j] : t->paths) {
        if ((i == pos.from && j == pos.to) || (i == pos.to && j == pos.from)) continue;
        const int shared = i == pos.from || i == pos.to ? i : j == pos.from || j == pos.to ? j : -1;
        if (shared < 0) continue;
        const int other = shared == i ? j : i;
        TrackPosition alt = shared == pos.to ? TrackPosition{ pos.brick, other, shared, 0.0 } : TrackPosition{ pos.brick, shared, other, 0.0 };
        const auto [d, s] = projectOnPath(net.path(alt.brick, alt.from, alt.to), at->p);
        if (d <= kSameSpot) {
            alt.s = s;
            out.push_back(alt);
        }
    }
    return out;
}

std::variant<RouteFound, RouteProblem> findRoute(const TrackNetwork& net, const TrackPosition& from, const TrackPosition& to,
                                                const FindOptions& opts) {
    std::vector<TrackPosition> points{ from };
    points.insert(points.end(), opts.via.begin(), opts.via.end());
    points.push_back(to);
    for (const auto& p : points)
        if (net.pathLength(p.brick, p.from, p.to) <= 0) return RouteProblem::NoTrack;
    if (points.size() == 2 && from.brick == to.brick && from.from == to.from && from.to == to.to && std::abs(from.s - to.s) < 0.5)
        return RouteProblem::SamePlace;
    std::vector<std::vector<TrackPosition>> spots;
    spots.reserve(points.size());
    for (const auto& p : points) spots.push_back(sameSpot(net, p));
    std::vector<Facing> facings = opts.facing ? std::vector<Facing>{ *opts.facing } : std::vector<Facing>{ Facing::To, Facing::From };
    std::optional<RouteFound> best;
    for (const Facing facing : facings) {
        std::vector<Arrival> at;
        for (const auto& p : spots.front()) {
            const RouteStep step = facing == Facing::To ? RouteStep{ p.brick, p.from, p.to } : RouteStep{ p.brick, p.to, p.from };
            at.push_back({ step, offsetOnStep(net, p, step), 0.0, { step } });
        }
        for (size_t i = 1; i < points.size() && !at.empty(); ++i) at = searchLeg(net, at, spots[i], opts, i + 1 == points.size());
        const Arrival* end = nullptr;
        for (const auto& a : at)
            if (!end || a.cost < end->cost - 1e-9) end = &a;
        if (!end) continue;
        RouteFound found{ end->steps, end->cost, routeSwitches(net, end->steps), turnsAroundIn(end->steps), facing, routeBothWays(net, end->steps) };
        // Better: needs no switch set both ways when the other does, else shorter (a tie keeps the first, the way as clicked).
        const bool better = !best || (found.bothWays.isEmpty() != best->bothWays.isEmpty() ? !best->bothWays.isEmpty()
                                                                                           : found.length < best->length - 1e-9);
        if (better) best = std::move(found);
    }
    if (!best) {
        for (const auto& p : points)
            if (!joined(net, from.brick, p.brick)) return RouteProblem::NoTrack;
        return RouteProblem::TurnAround;
    }
    return *best;
}

RouteStatus routeStatus(const TrackNetwork& net, const Route& route, const QMap<QString, int>& states) {
    RouteStatus st;
    const auto& steps = route.steps;
    const auto broken = [&](int at, const QString& brick) {
        st.broken = true;
        st.at = at;
        st.brick = brick;
        return st;
    };
    if (steps.empty()) return broken(0, route.from.brick);
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& s = steps[i];
        if (!net.exits(s.brick, s.from).contains(s.to)) return broken(static_cast<int>(i), s.brick);
        if (i + 1 < steps.size()) {
            const auto& next = steps[i + 1];
            const auto p = net.partner({ s.brick, s.to });
            if (!p || p->brick != next.brick || p->conn != next.from) return broken(static_cast<int>(i + 1), next.brick);
        }
    }
    const auto& first = steps.front();
    const auto& last = steps.back();
    if (!onStep(route.from, first)) return broken(0, first.brick);
    if (!onStep(route.to, last)) return broken(static_cast<int>(steps.size() - 1), last.brick);
    double length = 0;
    for (const auto& s : steps) length += net.pathLength(s.brick, s.from, s.to);
    length -= offsetOnStep(net, route.from, first);
    length -= net.pathLength(last.brick, last.from, last.to) - offsetOnStep(net, route.to, last);
    st.length = std::max(0.0, length);
    st.switches = routeSwitches(net, steps);
    for (auto it = st.switches.constBegin(); it != st.switches.constEnd(); ++it)
        if (const SwitchPoint* sw = net.switchAt(it.key()); sw && net.settingOf(*sw, states) != it.value()) st.setAgainst << it.key();
    st.turnsAround = turnsAroundIn(steps);
    st.bothWays = routeBothWays(net, steps);
    return st;
}

std::vector<std::vector<QPointF>> routeLines(const TrackNetwork& net, const Route& route) {
    std::vector<std::vector<QPointF>> lines;
    std::vector<QPointF> line;
    const auto& steps = route.steps;
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& s = steps[i];
        const auto& pts = net.path(s.brick, s.from, s.to);
        if (pts.size() < 2) {
            if (line.size() > 1) lines.push_back(line);
            line.clear();
            continue;
        }
        const double len = pts.back().s;
        const double a = i == 0 && onStep(route.from, s) ? offsetOnStep(net, route.from, s) : 0.0;
        const double b = i + 1 == steps.size() && onStep(route.to, s) ? offsetOnStep(net, route.to, s) : len;
        if (i > 0) {
            const auto p = net.partner({ steps[i - 1].brick, steps[i - 1].to });
            if (!p || p->brick != s.brick || p->conn != s.from) {
                if (line.size() > 1) lines.push_back(line);
                line.clear();
            }
        }
        line.push_back(pointAt(pts, a).p);
        for (const auto& p : pts)
            if (p.s > a && p.s < b) line.push_back(p.p);
        line.push_back(pointAt(pts, b).p);
    }
    if (line.size() > 1) lines.push_back(line);
    return lines;
}

Route renameRouteBricks(Route route, const std::function<QString(const QString&)>& rename) {
    route.from.brick = rename(route.from.brick);
    route.to.brick = rename(route.to.brick);
    for (auto& v : route.via) v.at.brick = rename(v.at.brick);
    for (auto& s : route.steps) s.brick = rename(s.brick);
    return route;
}

QString problemText(RouteProblem p) {
    switch (p) {
    case RouteProblem::NoTrack: return Text::tr("There’s no track joining these two places.");
    case RouteProblem::TurnAround: return Text::tr("The only way needs the train to turn around.");
    case RouteProblem::SamePlace: break;
    }
    return Text::tr("Pick an end somewhere else along the track.");
}

}  // namespace bld::track
