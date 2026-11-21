#include "FitChecks.h"

#include "CircuitPath.h"
#include "PartTrack.h"
#include "Routes.h"

#include <QSet>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace bld::track {

namespace {

double dist(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

QString pathKey(const QString& brick, int a, int b) {
    return brick + QLatin1Char('\x01') + QString::number(std::min(a, b)) + QLatin1Char('\x01') + QString::number(std::max(a, b));
}

QPointF closestOnSeg(QPointF p, QPointF q, QPointF t) {
    const double dx = q.x() - p.x();
    const double dy = q.y() - p.y();
    const double l2 = dx * dx + dy * dy;
    const double f = l2 > 0 ? std::clamp(((t.x() - p.x()) * dx + (t.y() - p.y()) * dy) / l2, 0.0, 1.0) : 0.0;
    return { p.x() + dx * f, p.y() + dy * f };
}

struct Rect {
    QPointF c;
    double ux = 1.0;
    double uy = 0.0;
    double half = 0.0;
    double halfW = 0.0;
};

Rect rectOf(const PlacedUnit& u, const core::RollingStock& s) {
    const double rad = qDegreesToRadians(u.angle);
    return { u.centre, std::cos(rad), std::sin(rad), (s.bodyLength > 0 ? s.bodyLength : s.length - 2.0) / 2.0, s.width / 2.0 };
}

std::array<QPointF, 4> corners(const Rect& r) {
    std::array<QPointF, 4> out;
    const int sx[4] = { 1, 1, -1, -1 };
    const int sy[4] = { 1, -1, -1, 1 };
    for (int k = 0; k < 4; ++k)
        out[static_cast<size_t>(k)] = { r.c.x() + r.ux * r.half * sx[k] - r.uy * r.halfW * sy[k],
                                        r.c.y() + r.uy * r.half * sx[k] + r.ux * r.halfW * sy[k] };
    return out;
}

QPointF local(const Rect& r, QPointF p) {
    const double dx = p.x() - r.c.x();
    const double dy = p.y() - r.c.y();
    return { dx * r.ux + dy * r.uy, -dx * r.uy + dy * r.ux };
}

double segRectDistance(const Rect& r, QPointF a, QPointF b) {
    const QPointF p = local(r, a);
    const QPointF q = local(r, b);
    const auto inside = [&](QPointF t) { return std::abs(t.x()) <= r.half && std::abs(t.y()) <= r.halfW; };
    if (inside(p) || inside(q)) return 0.0;
    const std::array<QPointF, 4> box{ QPointF(-r.half, -r.halfW), QPointF(r.half, -r.halfW), QPointF(r.half, r.halfW),
                                      QPointF(-r.half, r.halfW) };
    const auto cross = [](QPointF o, QPointF u, QPointF v) { return (u.x() - o.x()) * (v.y() - o.y()) - (u.y() - o.y()) * (v.x() - o.x()); };
    for (size_t k = 0; k < 4; ++k) {
        const QPointF e1 = box[k];
        const QPointF e2 = box[(k + 1) % 4];
        if (cross(p, q, e1) * cross(p, q, e2) < 0 && cross(e1, e2, p) * cross(e1, e2, q) < 0) return 0.0;
    }
    const auto pointBox = [&](QPointF t) {
        return std::hypot(std::max(0.0, std::abs(t.x()) - r.half), std::max(0.0, std::abs(t.y()) - r.halfW));
    };
    double best = std::min(pointBox(p), pointBox(q));
    for (const QPointF& c : box) best = std::min(best, dist(closestOnSeg(p, q, c), c));
    return best;
}

bool rectsOverlap(const Rect& a, const Rect& b) {
    const auto ca = corners(a);
    const auto cb = corners(b);
    const std::array<std::pair<double, double>, 4> axes{ { { a.ux, a.uy }, { -a.uy, a.ux }, { b.ux, b.uy }, { -b.uy, b.ux } } };
    for (const auto& [ux, uy] : axes) {
        double minA = std::numeric_limits<double>::infinity(), maxA = -minA, minB = minA, maxB = -minA;
        for (const QPointF& p : ca) {
            minA = std::min(minA, p.x() * ux + p.y() * uy);
            maxA = std::max(maxA, p.x() * ux + p.y() * uy);
        }
        for (const QPointF& p : cb) {
            minB = std::min(minB, p.x() * ux + p.y() * uy);
            maxB = std::max(maxB, p.x() * ux + p.y() * uy);
        }
        if (maxA <= minB + 1e-9 || maxB <= minA + 1e-9) return false;
    }
    return true;
}

std::optional<double> headingAt(const TrackLine& line, double d) {
    const auto ahead = lineAt(line, std::max(0.0, d - 0.5));
    auto behind = lineAt(line, d + 0.5);
    if (!behind) behind = lineAt(line, d);
    if (!ahead || !behind || (ahead->x() == behind->x() && ahead->y() == behind->y())) return std::nullopt;
    return qRadiansToDegrees(std::atan2(ahead->y() - behind->y(), ahead->x() - behind->x()));
}

// |a - b| for angles in degrees, folded to [0, 90].
double turn(double a, double b) {
    double d = std::abs(std::fmod(std::fmod(a - b, 360.0) + 540.0, 360.0) - 180.0);
    if (d > 90.0) d = 180.0 - d;
    return d;
}

const core::Coupler& couplerAt(const core::RollingStock& s, bool reversed, bool front) {
    const bool useFront = reversed ? !front : front;
    return useFront ? s.front : s.rear;
}

TrackRun finish(const TrackNetwork& net, std::vector<core::RouteStep> steps, RunEnd a, RunEnd b) {
    TrackRun run;
    for (const auto& s : steps) run.length += net.pathLength(s.brick, s.from, s.to);
    run.usable = a.kind == RunEnd::Kind::Loop ? run.length : std::max(0.0, run.length - a.foul - b.foul);
    run.ends[0] = std::move(a);
    run.ends[1] = std::move(b);
    double left = run.length / 2.0;
    const auto start = net.port(steps.front().brick, steps.front().from);
    run.middle = start ? start->first : QPointF();
    for (const auto& s : steps) {
        const auto& pts = net.path(s.brick, s.from, s.to);
        const double len = pts.empty() ? 0.0 : pts.back().s;
        if (left <= len) {
            run.middle = pointAt(pts, left).p;
            break;
        }
        left -= len;
    }
    run.steps = std::move(steps);
    return run;
}

}  // namespace

Overhang overhang(double radius, double truckSpacing, double bodyLength, double width) {
    const double a = truckSpacing / 2.0;
    const double b = bodyLength / 2.0;
    const double d = std::sqrt(std::max(0.0, radius * radius - a * a));
    return { radius - d, std::hypot(d, b) - radius, std::hypot(d + width / 2.0, b) - (radius + width / 2.0),
             qRadiansToDegrees(std::asin(std::min(1.0, a / radius))) };
}

TrackLine lineAhead(const TrackNetwork& net, const QString& brick, int from, int to, double length, const QMap<QString, int>& states) {
    TrackLine out;
    double d = 0.0;
    QString b = brick;
    int enter = from;
    int leave = to;
    for (int guard = 0; guard < 100000; ++guard) {
        const auto& pts = net.path(b, enter, leave);
        if (pts.size() < 2) {
            out.ended = true;
            return out;
        }
        for (const auto& q : pts) {
            if (!out.pts.empty()) {
                const double step = dist(q.p, out.pts.back().p);
                if (step < 1e-9) continue;
                d += step;
            }
            out.pts.push_back({ q.p, d, b });
            if (d >= length) return out;
        }
        const auto next = net.partner({ b, leave });
        if (!next) {
            out.ended = true;
            return out;
        }
        b = next->brick;
        enter = next->conn;
        const QVector<int> exits = net.exits(b, enter);
        if (exits.isEmpty()) {
            out.ended = true;
            return out;
        }
        const SwitchPoint* sw = net.switchAt(switchKey(b, enter));
        leave = sw ? net.settingOf(*sw, states) : exits.front();
    }
    out.ended = true;
    return out;
}

double foulingBeyond(const TrackNetwork& net, const SwitchPoint& sw, int leg, double clearance, double max) {
    const double legLength = net.pathLength(sw.brick, sw.heel, leg);
    double worst = 0.0;
    for (int other : sw.exits) {
        if (other == leg) continue;
        const TrackLine a = lineAhead(net, sw.brick, sw.heel, leg, max);
        const TrackLine b = lineAhead(net, sw.brick, sw.heel, other, max);
        const double end = std::min(a.pts.empty() ? 0.0 : a.pts.back().d, b.pts.empty() ? 0.0 : b.pts.back().d);
        double foul = max;
        for (int k = 0; k * 0.25 <= end + 1e-9; ++k) {
            const double s = k * 0.25;
            const auto p = lineAt(a, s);
            const auto q = lineAt(b, s);
            if (p && q && dist(*p, *q) >= clearance) {
                foul = s;
                break;
            }
        }
        worst = std::max(worst, foul - legLength);
    }
    return std::max(0.0, worst);
}

std::vector<TrackRun> trackRuns(const TrackNetwork& net) {
    QSet<QString> switchBricks;
    for (const auto& s : net.switches()) switchBricks.insert(s.brick);
    QSet<QString> visited;
    std::vector<TrackRun> runs;
    const auto endAt = [&](const QString& brick, int conn) {
        RunEnd e;
        for (const auto& s : net.switches())
            if (s.brick == brick && s.heel != conn && s.exits.contains(conn)) {
                e.kind = RunEnd::Kind::Leg;
                e.switchKey = s.key;
                e.foul = foulingBeyond(net, s, conn, trackKinds().defaultsOf(net.kindOf(brick)).clearance);
                return e;
            }
        e.kind = RunEnd::Kind::Heel;
        for (const auto& s : net.switches())
            if (s.brick == brick && s.heel == conn) {
                e.switchKey = s.key;
                return e;
            }
        for (const auto& s : net.switches())
            if (s.brick == brick) {
                e.switchKey = s.key;
                break;
            }
        return e;
    };
    const auto walk = [&](const QString& brick, int enter, const RunEnd& start) {
        std::vector<core::RouteStep> steps;
        QString b = brick;
        int e = enter;
        RunEnd end;
        for (int guard = 0; guard < 100000; ++guard) {
            const QVector<int> exits = net.exits(b, e);
            if (exits.isEmpty()) break;
            const int to = exits.front();
            if (visited.contains(pathKey(b, e, to))) {
                end.kind = RunEnd::Kind::Loop;
                break;
            }
            visited.insert(pathKey(b, e, to));
            steps.push_back({ b, e, to });
            const auto p = net.partner({ b, to });
            if (!p) break;
            if (switchBricks.contains(p->brick)) {
                end = endAt(p->brick, p->conn);
                break;
            }
            b = p->brick;
            e = p->conn;
        }
        if (steps.empty()) return;
        if (end.kind == RunEnd::Kind::Loop) {
            RunEnd loop;
            loop.kind = RunEnd::Kind::Loop;
            runs.push_back(finish(net, std::move(steps), loop, end));
        } else {
            runs.push_back(finish(net, std::move(steps), start, end));
        }
    };
    const auto ports = [&](const QString& brick) {
        QVector<int> out;
        for (const auto& [i, j] : net.trackOf(brick)->paths) {
            if (!out.contains(i)) out << i;
            if (!out.contains(j)) out << j;
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    for (const QString& brick : net.brickIds()) {
        if (!switchBricks.contains(brick)) continue;
        for (int c : ports(brick)) {
            const auto q = net.partner({ brick, c });
            if (!q || switchBricks.contains(q->brick)) continue;
            const QVector<int> exits = net.exits(q->brick, q->conn);
            if (exits.isEmpty() || visited.contains(pathKey(q->brick, q->conn, exits.front()))) continue;
            walk(q->brick, q->conn, endAt(brick, c));
        }
    }
    for (const QString& brick : net.brickIds()) {
        if (switchBricks.contains(brick)) continue;
        for (int c : ports(brick)) {
            if (net.partner({ brick, c })) continue;
            const QVector<int> exits = net.exits(brick, c);
            if (exits.isEmpty() || visited.contains(pathKey(brick, c, exits.front()))) continue;
            walk(brick, c, RunEnd{});
        }
    }
    for (const QString& brick : net.brickIds()) {
        if (switchBricks.contains(brick)) continue;
        for (const auto& [i, j] : net.trackOf(brick)->paths) {
            if (visited.contains(pathKey(brick, i, j))) continue;
            RunEnd loop;
            loop.kind = RunEnd::Kind::Loop;
            walk(brick, i, loop);
        }
    }
    return runs;
}

const TrackRun* runAt(const std::vector<TrackRun>& runs, const QString& brick, int from, int to) {
    const QString key = pathKey(brick, from, to);
    for (const auto& r : runs)
        for (const auto& s : r.steps)
            if (pathKey(s.brick, s.from, s.to) == key) return &r;
    return nullptr;
}

std::vector<FitCheck> fitChecks(const TrackNetwork& net, const FitInput& input, const std::vector<TrackRun>* givenRuns) {
    std::vector<TrackRun> ownRuns;
    if (!givenRuns) ownRuns = trackRuns(net);
    const std::vector<TrackRun>& runs = givenRuns ? *givenRuns : ownRuns;
    std::vector<FitCheck> out;
    struct Placed {
        QString train;
        int unit;
        Rect rect;
        QString kind;
    };
    std::vector<Placed> rects;
    const auto add = [&out](const QString& key, const QString& kind, bool error, const QString& train, QPointF at) -> FitCheck& {
        FitCheck c;
        c.key = key;
        c.kind = kind;
        c.error = error;
        c.train = train;
        c.at = at;
        out.push_back(std::move(c));
        return out.back();
    };
    for (const auto& t : input.trains) {
        if (!t.position) continue;
        const auto placed = placeTrain(net, t, input.stock, input.states);
        const auto head = net.pointAtPosition(*t.position);
        if (!placed || !head) {
            add(QStringLiteral("lost:") + t.id, QStringLiteral("lost"), false, t.id, head ? head->p : QPointF());
            continue;
        }
        if (placed->offTrack)
            add(QStringLiteral("offEnd:") + t.id, QStringLiteral("offEnd"), false, t.id,
                placed->units.empty() ? head->p : placed->units.back().rearCoupler);
        const TrackRun* run = runAt(runs, t.position->brick, t.position->from, t.position->to);
        const double length = trainLength(t, input.stock);
        if (run && run->ends[0].kind != RunEnd::Kind::Loop && length > run->usable + 1e-6) {
            FitCheck& c = add(QStringLiteral("siding:") + t.id, QStringLiteral("siding"), false, t.id, run->middle);
            c.values.insert(QStringLiteral("length"), length);
            c.values.insert(QStringLiteral("usable"), run->usable);
        }
        const QString kind = net.kindOf(t.position->brick);
        std::vector<core::TrainUnit> units;
        for (const auto& u : t.units)
            if (input.stock.contains(u.stock)) units.push_back(u);
        const QString base = t.id + QLatin1Char(':');
        for (size_t i = 0; i < placed->units.size(); ++i) {
            const PlacedUnit& u = placed->units[i];
            const core::RollingStock& s = input.stock[u.stock];
            const int n = static_cast<int>(i);
            const QString idx = base + QString::number(n);
            if (!kind.isEmpty() && !s.trackKind.isEmpty() && s.trackKind != kind) {
                FitCheck& c = add(QStringLiteral("trackKind:") + idx, QStringLiteral("trackKind"), true, t.id, u.centre);
                c.stock = s.id;
                c.unit = n;
            }
            double worst = 0.0;
            QPointF at = u.frontTruck;
            for (const auto& [d, p] : { std::pair<double, QPointF>{ u.frontD, u.frontTruck }, std::pair<double, QPointF>{ u.rearD, u.rearTruck } }) {
                const auto h = headingAt(placed->line, d);
                if (!h) continue;
                const double a = turn(*h, u.angle);
                if (a > worst) {
                    worst = a;
                    at = p;
                }
            }
            double limit = std::numeric_limits<double>::infinity();
            for (const auto& tr : s.trucks) limit = std::min(limit, tr.swivelMax);
            if (worst > limit + 1e-6) {
                FitCheck& c = add(QStringLiteral("truck:") + idx, QStringLiteral("truck"), false, t.id, at);
                c.stock = s.id;
                c.unit = n;
                c.values.insert(QStringLiteral("angle"), worst);
                c.values.insert(QStringLiteral("max"), limit);
            }
            if (i > 0) {
                const PlacedUnit& prev = placed->units[i - 1];
                const core::RollingStock& ps = input.stock[prev.stock];
                const core::Coupler& behind = couplerAt(ps, units[i - 1].reversed, false);
                const core::Coupler& ahead = couplerAt(s, units[i].reversed, true);
                const QPointF mid = (prev.rearCoupler + u.frontCoupler) / 2.0;
                if (behind.type == QLatin1String("none") || ahead.type == QLatin1String("none")) {
                    FitCheck& c = add(QStringLiteral("uncoupled:") + idx, QStringLiteral("uncoupled"), false, t.id, mid);
                    c.stock = s.id;
                    c.unit = n;
                } else {
                    const double angle = turn(prev.angle, u.angle) / 2.0;
                    const double max = std::min(behind.swivelMax, ahead.swivelMax);
                    const double gap = dist(prev.rearCoupler, u.frontCoupler);
                    if (angle > max + 1e-6 || gap > 1.0) {
                        FitCheck& c = add(QStringLiteral("coupler:") + idx, QStringLiteral("coupler"), false, t.id, mid);
                        c.stock = s.id;
                        c.unit = n;
                        c.values.insert(QStringLiteral("angle"), angle);
                        c.values.insert(QStringLiteral("max"), max);
                        c.values.insert(QStringLiteral("gap"), gap);
                    }
                }
            }
            rects.push_back({ t.id, n, rectOf(u, s), kind });
        }
        // Clearance: its cars against the other tracks (not the ones it stands on).
        const double lastBack = placed->units.empty() ? 0.0 : placed->units.back().back;
        const auto& pts = placed->line.pts;
        QSet<QString> occupied{ t.position->brick };
        for (size_t k = 1; k < pts.size(); ++k)
            if (pts[k - 1].d <= lastBack + 2.0) occupied.insert(pts[k].brick);
        if (!placed->units.empty()) {
            const core::RollingStock& fs = input.stock[placed->units.front().stock];
            double far = 0.0;
            for (const auto& tr : fs.trucks) far = std::max(far, std::abs(tr.at));
            const double reach = std::max(0.0, fs.length / 2.0 - far) + 2.0;
            const auto& h = *t.position;
            const double len = net.pathLength(h.brick, h.from, h.to) + reach;
            const TrackLine ahead = h.facingTo ? lineAhead(net, h.brick, h.from, h.to, len, input.states)
                                               : lineAhead(net, h.brick, h.to, h.from, len, input.states);
            for (const auto& p : ahead.pts) occupied.insert(p.brick);
        }
        const double band = trackKinds().defaultsOf(kind).carWidth / 2.0 + 0.5;
        for (size_t i = 0; i < placed->units.size(); ++i) {
            const PlacedUnit& u = placed->units[i];
            const core::RollingStock& s = input.stock[u.stock];
            const Rect r = rectOf(u, s);
            const double reach = std::hypot(r.half, r.halfW) + band;
            for (const QString& brick : net.brickIds()) {
                // (Another kind of track is no matter: a monorail runs above the rails.)
                if (occupied.contains(brick) || net.kindOf(brick) != kind) continue;
                std::optional<QPointF> hit;
                for (const auto& [a, b] : net.trackOf(brick)->paths) {
                    const auto& path = net.path(brick, a, b);
                    for (size_t k = 1; k < path.size() && !hit; ++k) {
                        const QPointF p = path[k - 1].p;
                        const QPointF q = path[k].p;
                        if (dist(closestOnSeg(p, q, r.c), r.c) > reach) continue;
                        if (segRectDistance(r, p, q) < band - 1e-6) hit = closestOnSeg(p, q, r.c);
                    }
                    if (hit) break;
                }
                if (hit) {
                    const int n = static_cast<int>(i);
                    FitCheck& c = add(QStringLiteral("clearance:") + base + QString::number(n) + QLatin1Char(':') + brick,
                                      QStringLiteral("clearance"), false, t.id, *hit);
                    c.stock = s.id;
                    c.unit = n;
                    c.other = brick;
                    break;
                }
            }
        }
    }
    for (size_t i = 0; i < rects.size(); ++i)
        for (size_t j = i + 1; j < rects.size(); ++j) {
            const Placed& a = rects[i];
            const Placed& b = rects[j];
            if (a.train == b.train || a.kind != b.kind || !rectsOverlap(a.rect, b.rect)) continue;
            if (std::any_of(out.begin(), out.end(), [&](const FitCheck& c) { return c.kind == QLatin1String("overlap") && c.train == a.train && c.other == b.train; }))
                continue;
            FitCheck& c = add(QStringLiteral("overlap:") + a.train + QLatin1Char(':') + b.train, QStringLiteral("overlap"), true, a.train, a.rect.c);
            c.other = b.train;
            c.unit = a.unit;
        }
    for (const auto& r : input.routes) {
        const RouteStatus st = routeStatus(net, r, input.states);
        if (st.broken) continue;
        for (const QString& brick : st.turnsAround) {
            QPointF at;
            for (const auto& s : r.steps)
                if (s.brick == brick) {
                    if (const auto p = net.pointAtPosition({ brick, s.from, s.to, net.pathLength(brick, s.from, s.to) / 2.0 })) at = p->p;
                    break;
                }
            FitCheck& c = add(QStringLiteral("reverse:") + r.id + QLatin1Char(':') + brick, QStringLiteral("reverse"), false, QString(), at);
            c.route = r.id;
            c.other = brick;
            if (net.isPowered(brick)) {
                FitCheck& p = add(QStringLiteral("power:") + r.id + QLatin1Char(':') + brick, QStringLiteral("power"), false, QString(), at);
                p.route = r.id;
                p.other = brick;
            }
        }
    }
    // Track that nearly meets: joined for trains, but worth closing up.
    for (const TrackGap& g : net.gaps()) {
        FitCheck& c = add(QStringLiteral("gap:") + switchKey(g.a.brick, g.a.conn) + QLatin1Char(':') + switchKey(g.b.brick, g.b.conn), QStringLiteral("gap"), false,
                          QString(), g.at);
        c.other = g.a.brick;
        c.values.insert(QStringLiteral("gap"), g.gap);
    }
    return out;
}

}  // namespace bld::track
