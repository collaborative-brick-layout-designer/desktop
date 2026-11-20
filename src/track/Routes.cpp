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

struct Leg {
    std::vector<RouteStep> steps;
    double cost = 0.0;
};

std::optional<Leg> searchLeg(const TrackNetwork& net, const RouteStep& start, double s0, const TrackPosition& target,
                             const FindOptions& opts) {
    if (onStep(target, start) && offsetOnStep(net, target, start) >= s0 - 1e-9)
        return Leg{ { start }, offsetOnStep(net, target, start) - s0 };
    QHash<QString, double> dist;
    QHash<QString, std::optional<RouteStep>> prev;
    const double startLen = net.pathLength(start.brick, start.from, start.to);
    dist.insert(keyOf(start), startLen - s0);
    prev.insert(keyOf(start), std::nullopt);
    struct Open {
        RouteStep step;
        double d;
    };
    std::vector<Open> open{ { start, startLen - s0 } };
    QSet<QString> done;
    std::optional<std::pair<RouteStep, double>> found;
    while (!open.empty()) {
        size_t bi = 0;
        for (size_t i = 1; i < open.size(); ++i) {
            const Open& a = open[i];
            const Open& b = open[bi];
            if (a.d < b.d - 1e-9 || (std::abs(a.d - b.d) <= 1e-9 && keyOf(a.step) < keyOf(b.step))) bi = i;
        }
        const Open cur = open[bi];
        open.erase(open.begin() + static_cast<std::ptrdiff_t>(bi));
        const QString key = keyOf(cur.step);
        if (done.contains(key)) continue;
        done.insert(key);
        if (found && cur.d >= found->second) break;
        const auto next = net.partner({ cur.step.brick, cur.step.to });
        if (!next) continue;
        QVector<int> exits = net.exits(next->brick, next->conn);
        if (opts.followSwitches)
            if (const SwitchPoint* sw = net.switchAt(switchKey(next->brick, next->conn))) exits = { net.settingOf(*sw, opts.states) };
        for (int x : exits) {
            const RouteStep ns{ next->brick, next->conn, x };
            const QString nk = keyOf(ns);
            if (onStep(target, ns)) {
                const double cost = cur.d + offsetOnStep(net, target, ns);
                if (!found || cost < found->second - 1e-9) {
                    found = std::make_pair(ns, cost);
                    prev.insert(nk, cur.step);
                }
                continue;
            }
            const double nd = cur.d + net.pathLength(ns.brick, ns.from, ns.to);
            if (nd < dist.value(nk, std::numeric_limits<double>::infinity()) - 1e-9) {
                dist.insert(nk, nd);
                prev.insert(nk, cur.step);
                open.push_back({ ns, nd });
            }
        }
    }
    if (!found) return std::nullopt;
    Leg leg;
    leg.cost = found->second;
    std::deque<RouteStep> steps{ found->first };
    std::optional<RouteStep> p = prev.value(keyOf(found->first));
    while (p) {
        steps.push_front(*p);
        p = prev.value(keyOf(*p));
    }
    leg.steps.assign(steps.begin(), steps.end());
    return leg;
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
        if (const SwitchPoint* sw = net.switchAt(switchKey(s.brick, s.from)); sw && sw->exits.contains(s.to)) out.insert(sw->key, s.to);
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
    std::optional<Leg> best;
    for (const RouteStep& start : { RouteStep{ from.brick, from.from, from.to }, RouteStep{ from.brick, from.to, from.from } }) {
        std::vector<RouteStep> steps;
        double cost = 0;
        RouteStep at = start;
        double s0 = offsetOnStep(net, from, start);
        bool ok = true;
        for (size_t i = 1; i < points.size(); ++i) {
            const auto leg = searchLeg(net, at, s0, points[i], opts);
            if (!leg) {
                ok = false;
                break;
            }
            if (steps.empty()) steps = leg->steps;
            else steps.insert(steps.end(), leg->steps.begin() + 1, leg->steps.end());
            cost += leg->cost;
            at = leg->steps.back();
            s0 = offsetOnStep(net, points[i], at);
        }
        if (ok && (!best || cost < best->cost - 1e-9)) best = Leg{ steps, cost };
    }
    if (!best) {
        for (const auto& p : points)
            if (!joined(net, from.brick, p.brick)) return RouteProblem::NoTrack;
        return RouteProblem::TurnAround;
    }
    return RouteFound{ best->steps, best->cost, routeSwitches(net, best->steps), turnsAroundIn(best->steps) };
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
