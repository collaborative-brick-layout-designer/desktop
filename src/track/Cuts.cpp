#include "Cuts.h"

#include "CircuitPath.h"

#include "../core/Ids.h"
#include "../core/Sidecar.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::track {

namespace {

constexpr double kCoupleReach = 1.0;  // couplers this close (studs) couple
constexpr double kReach = 1000.0;     // how far ahead of and behind a train it can be dragged at once
constexpr double kOnTrack = 1.5;      // how near the track a truck or coupler must be to stand on it
constexpr double kMinMove = 0.5;      // a shorter drag does nothing
constexpr int kMaxSegs = 4000;

using States = QMap<QString, int>;

// One brick of the way, from far ahead to far behind: its start's distance (0: the head's front truck, - ahead).
struct Seg {
    QString brick;
    int enter = 0;
    int leave = 0;
    double d0 = 0.0;
    double len = 0.0;
    std::vector<CircuitPathPoint> pts;  // (a copy: the network's own may move as it works out more)
};

struct Way {
    std::vector<Seg> segs;
    double min = 0.0;
    double max = 0.0;
    bool endAhead = false;  // the track ends there (else the reach did)
    bool endBehind = false;
};

// The track through a train, ahead of it and behind it, through switches as they are set.
std::optional<Way> wayThrough(const TrackNetwork& net, const core::TrainPosition& at, double behind, const States& states) {
    const int enter = at.facingTo ? at.to : at.from;
    const int leave = at.facingTo ? at.from : at.to;
    const auto& pts = net.path(at.brick, enter, leave);
    if (pts.size() < 2) return std::nullopt;
    const double len = pts.back().s;
    const double s = std::max(0.0, std::min(at.s, len));
    const double start = at.facingTo ? len - s : s;
    const Seg first{ at.brick, enter, leave, -start, len, pts };
    const auto exitOf = [&](const PortRef& p) -> std::optional<int> {
        const auto exits = net.exits(p.brick, p.conn);
        if (exits.isEmpty()) return std::nullopt;
        const SwitchPoint* sw = net.switchAt(switchKey(p.brick, p.conn));
        return sw ? net.settingOf(*sw, states) : exits.front();
    };
    Way way;
    std::vector<Seg> back;
    way.max = first.d0 + len;
    for (Seg cur = first; way.max < behind + kReach;) {
        const auto next = net.partner({ cur.brick, cur.leave });
        const auto out = next ? exitOf(*next) : std::nullopt;
        if (!next || !out || back.size() >= kMaxSegs) {
            way.endBehind = true;
            break;
        }
        const auto& p = net.path(next->brick, next->conn, *out);
        cur = { next->brick, next->conn, *out, way.max, p.back().s, p };
        back.push_back(cur);
        way.max += cur.len;
    }
    std::vector<Seg> front;
    way.min = first.d0;
    for (Seg cur = first; way.min > -kReach;) {
        const auto next = net.partner({ cur.brick, cur.enter });
        const auto out = next ? exitOf(*next) : std::nullopt;
        if (!next || !out || front.size() >= kMaxSegs) {
            way.endAhead = true;
            break;
        }
        const auto& p = net.path(next->brick, *out, next->conn);
        cur = { next->brick, *out, next->conn, way.min - p.back().s, p.back().s, p };
        front.push_back(cur);
        way.min -= cur.len;
    }
    way.segs.assign(front.rbegin(), front.rend());
    way.segs.push_back(first);
    way.segs.insert(way.segs.end(), back.begin(), back.end());
    return way;
}

struct Hit {
    double d = 0.0;
    double off = 0.0;
};

// Everywhere the way passes within `reach` of a point: the nearest spot on each brick that does.
std::vector<Hit> project(const Way& way, QPointF at, double reach) {
    std::vector<Hit> out;
    for (const Seg& g : way.segs) {
        const QPointF p0 = g.pts.front().p;
        // (No point of a brick's way is further from its start than its length.)
        if (std::hypot(p0.x() - at.x(), p0.y() - at.y()) > g.len + reach) continue;
        double best = reach;
        bool near = false;
        double d = 0.0;
        for (size_t k = 1; k < g.pts.size(); ++k) {
            const auto& a = g.pts[k - 1];
            const auto& b = g.pts[k];
            const double dx = b.p.x() - a.p.x();
            const double dy = b.p.y() - a.p.y();
            const double l2 = dx * dx + dy * dy;
            const double t = l2 > 0 ? std::clamp(((at.x() - a.p.x()) * dx + (at.y() - a.p.y()) * dy) / l2, 0.0, 1.0) : 0.0;
            const double off = std::hypot(a.p.x() + dx * t - at.x(), a.p.y() + dy * t - at.y());
            if (off <= best) {
                best = off;
                near = true;
                d = g.d0 + a.s + (b.s - a.s) * t;
            }
        }
        if (near) out.push_back({ d, best });
    }
    return out;
}

// The position `d` along the way, facing ahead (or back).
core::TrainPosition positionAt(const Way& way, double d, bool ahead) {
    const auto it = std::find_if(way.segs.begin(), way.segs.end(), [d](const Seg& x) { return d <= x.d0 + x.len + 1e-9; });
    const Seg& g = it != way.segs.end() ? *it : way.segs.back();
    const double s = std::clamp(d - g.d0, 0.0, g.len);
    core::TrainPosition p;
    p.brick = g.brick;
    p.from = std::min(g.enter, g.leave);
    p.to = std::max(g.enter, g.leave);
    p.s = g.enter == p.from ? s : g.len - s;
    p.facingTo = (ahead ? g.enter : g.leave) == p.to;
    return p;
}

// How the switches between `d0` and `d1` are passed by a train standing there.
States overAt(const TrackNetwork& net, const Way& way, double d0, double d1, bool ahead) {
    States out;
    for (const Seg& g : way.segs) {
        if (g.d0 + g.len < d0 || g.d0 > d1) continue;
        const QString key = switchKey(g.brick, ahead ? g.enter : g.leave);
        if (net.switchAt(key)) out.insert(key, ahead ? g.leave : g.enter);
    }
    return out;
}

struct Other {
    core::Train train;
    PlacedTrain placed;
    double lo = 0.0;  // the stretch of the way it stands on
    double hi = 0.0;
};

double frontMost(const core::RollingStock& s) {
    return std::max_element(s.trucks.begin(), s.trucks.end(), [](const auto& a, const auto& b) { return a.at < b.at; })->at;
}
double rearMost(const core::RollingStock& s) {
    return std::min_element(s.trucks.begin(), s.trucks.end(), [](const auto& a, const auto& b) { return a.at < b.at; })->at;
}
// A unit's front truck from its middle, the way it runs; and its rear one.
double frontTruckOf(const core::RollingStock& s, bool reversed) { return reversed ? -rearMost(s) : frontMost(s); }
double rearTruckOf(const core::RollingStock& s, bool reversed) { return reversed ? -frontMost(s) : rearMost(s); }

bool hasCoupler(const core::RollingStock& s, bool reversed, bool front) {
    return (front != reversed ? s.front : s.rear).type != QLatin1String("none");
}

using Units = std::vector<core::TrainUnit>;

Units turnedUnits(const Units& units) {
    Units out(units.rbegin(), units.rend());
    for (auto& u : out) u.reversed = !u.reversed;
    return out;
}

bool anyEngine(const Units& units, const StockById& stock) {
    return std::any_of(units.begin(), units.end(), [&](const core::TrainUnit& u) {
        const auto it = stock.constFind(u.stock);
        return it != stock.constEnd() && isEngine(*it);
    });
}

core::Train blankCut(const QString& id, const core::Train& like) {
    core::Train t;
    t.id = id;
    t.trackKind = like.trackKind;
    t.color = like.color;
    t.speed = like.speed;
    t.cut = true;
    return t;
}

// `mover` (its head at `head`, its rear coupler at `tail`) and the train it meets, as one; or why not.
std::optional<core::Train> coupled(const TrackNetwork& net, const Way& way, const StockById& stock, const core::Train& mover, const Other& hit,
                                   double head, double tail, bool ahead, QString* why) {
    const core::Train& other = hit.train;
    if (!mover.cut && !other.cut) {
        *why = QStringLiteral("trains");
        return std::nullopt;
    }
    if (mover.units.size() + other.units.size() > static_cast<size_t>(kMaxUnits)) {
        *why = QStringLiteral("full");
        return std::nullopt;
    }
    const PlacedUnit& o0 = hit.placed.units.front();
    const PlacedUnit& oN = hit.placed.units.back();
    const double edge = ahead ? hit.hi : hit.lo;
    const auto near = [&](QPointF p) {
        double best = std::numeric_limits<double>::infinity();
        for (const Hit& x : project(way, p, kOnTrack)) best = std::min(best, std::abs(x.d - edge));
        return best;
    };
    const double headOff = near(o0.frontCoupler);
    const double tailOff = near(oN.rearCoupler);
    if (std::min(headOff, tailOff) > kCoupleReach) return std::nullopt;
    // Which of its ends is met: ahead of us its tail means it faces our way.
    const bool meetsHead = headOff < tailOff;
    const bool faces = ahead ? !meetsHead : meetsHead;
    if (!meetsHead && hit.placed.offTrack) return std::nullopt;
    const auto known = [&](const Units& us) {
        Units out;
        for (const auto& u : us)
            if (stock.contains(u.stock)) out.push_back(u);
        return out;
    };
    const Units mine = known(mover.units);
    const Units theirs = known(other.units);
    const core::TrainUnit& myEnd = ahead ? mine.front() : mine.back();
    const core::TrainUnit& theirEnd = meetsHead ? theirs.front() : theirs.back();
    if (!hasCoupler(stock[myEnd.stock], myEnd.reversed, ahead) || !hasCoupler(stock[theirEnd.stock], theirEnd.reversed, meetsHead)) {
        *why = QStringLiteral("coupler");
        return std::nullopt;
    }
    // The whole, front to back the way we face.
    const Units oriented = faces ? other.units : turnedUnits(other.units);
    Units units = ahead ? oriented : mover.units;
    const Units& after = ahead ? mover.units : oriented;
    units.insert(units.end(), after.begin(), after.end());
    const double lo = std::min(head, hit.lo);
    const double hi = std::max(tail, hit.hi);
    // Whose record it is: a train's before a cut's, then the one with an engine, then the one that stood still.
    const bool keepMover = !mover.cut || (other.cut && anyEngine(mover.units, stock) && !anyEngine(other.units, stock));
    const bool flip = !keepMover && !faces;
    core::TrainPosition position;
    if (!flip) {
        if (!ahead) position = mover.position.value_or(position);
        else if (faces) position = other.position.value_or(position);
        else {
            // Where its rear truck is on the way, in the stretch it takes up.
            std::optional<Hit> best;
            for (const Hit& q : project(way, oN.rearTruck, kOnTrack))
                if (q.d >= hit.lo - kCoupleReach && q.d <= hit.hi + kCoupleReach && (!best || q.off < best->off)) best = q;
            if (!best) return std::nullopt;
            position = positionAt(way, best->d, true);
        }
    } else if (ahead) {
        // Turned to face as the other does: its head is at our tail.
        const core::TrainUnit& u = mine.back();
        const core::RollingStock s = stock[u.stock];
        position = positionAt(way, tail - (s.length / 2.0 + rearTruckOf(s, u.reversed)), false);
    } else {
        position = other.position.value_or(position);
    }
    States over = (keepMover ? other : mover).over.value_or(States());
    over.insert((keepMover ? mover : other).over.value_or(States()));
    over.insert(overAt(net, way, lo, hi, !flip));
    core::Train out = keepMover ? mover : other;
    out.units = flip ? turnedUnits(units) : units;
    out.position = position;
    out.over = over;
    return out;
}

}  // namespace

bool isEngine(const core::RollingStock& s) { return s.kind == QLatin1String("engine") || s.powered; }

std::vector<core::Train> settleTrains(const std::vector<core::Train>& trains, const StockById& stock) {
    QHash<QString, const core::Train*> byId;
    QHash<QString, std::vector<const core::Train*>> listers;
    for (const auto& t : trains) {
        byId.insert(t.id, &t);
        if (!t.position) continue;
        for (const auto& u : t.units) {
            auto& l = listers[u.stock];
            if (l.empty() || l.back() != &t) l.push_back(&t);
        }
    }
    // Which record a car standing in a cut is in (unset: not in any, or only in plain trains).
    QHash<QString, QString> home;
    QHash<QString, QSet<QString>> missing;
    for (auto it = stock.constBegin(); it != stock.constEnd(); ++it) {
        const core::Train* own = it->opsCut ? byId.value(*it->opsCut, nullptr) : nullptr;
        QString at;
        if (own && own->position) {
            at = own->id;
        } else {
            auto l = listers.value(it.key());
            if (std::none_of(l.begin(), l.end(), [](const core::Train* t) { return t->cut; })) continue;
            at = (*std::min_element(l.begin(), l.end(), [](const core::Train* a, const core::Train* b) {
                     return a->cut != b->cut ? !a->cut : a->id < b->id;
                 }))->id;
        }
        home.insert(it.key(), at);
        missing[at].insert(it.key());
    }
    std::vector<core::Train> out = trains;
    if (home.isEmpty()) return out;
    for (auto& t : out) {
        if (!t.position) continue;
        auto& left = missing[t.id];
        Units units;
        for (const auto& u : t.units) {
            const auto h = home.constFind(u.stock);
            if (h == home.constEnd() || (*h == t.id && left.remove(u.stock))) units.push_back(u);
        }
        QStringList lost(left.begin(), left.end());
        lost.sort();
        for (const QString& car : lost) units.push_back({ car, false });
        t.units = units;
    }
    return out;
}

StockById stockById(const core::Sidecar& sidecar) {
    StockById out;
    for (const auto& s : sidecar.rollingStock) out.insert(s.id, s);
    return out;
}

std::vector<core::Train> standingTrains(const core::Sidecar& sidecar) {
    auto out = settleTrains(sidecar.trains, stockById(sidecar));
    std::erase_if(out, [](const core::Train& t) { return t.cut && t.units.empty(); });
    return out;
}

const core::Train* standingIn(const QString& car, const std::vector<core::Train>& trains) {
    for (const auto& t : trains)
        if (t.position && std::any_of(t.units.begin(), t.units.end(), [&](const core::TrainUnit& u) { return u.stock == car; })) return &t;
    return nullptr;
}

struct CutDrag::State {
    const TrackNetwork* net = nullptr;
    StockById stock;
    core::Train train;
    QSet<QString> ids;  // every record there is
    QString newId;
    PlacedTrain placed;
    Way way;
    std::vector<size_t> at;  // the placed cars' indexes among the train's units
    std::vector<Other> others;
    double longest = 8.0;
    double mid = 0.0;
    int unit = 0;
    bool whole = false;

    double front(size_t i) const {
        const core::RollingStock s = stock.value(placed.units[i].stock);
        return placed.units[i].frontD - (s.length / 2.0 - frontTruckOf(s, train.units[at[i]].reversed));
    }
    double rear(size_t i) const { return placed.units[i].back; }
    std::optional<CutMove> to(QPointF p) const;
};

CutDrag::CutDrag() = default;
CutDrag::~CutDrag() = default;
CutDrag::CutDrag(CutDrag&&) noexcept = default;
CutDrag& CutDrag::operator=(CutDrag&&) noexcept = default;
CutDrag::CutDrag(std::unique_ptr<State> s) : s_(std::move(s)) {}
std::optional<CutMove> CutDrag::to(QPointF at) const { return s_ ? s_->to(at) : std::nullopt; }

std::optional<CutDrag> startDrag(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                                 const QMap<QString, int>& states, const QString& trainId, int unit, const QString& newId) {
    const auto it = std::find_if(trains.begin(), trains.end(), [&](const core::Train& t) { return t.id == trainId; });
    if (it == trains.end() || !it->position) return std::nullopt;
    auto d = std::make_unique<CutDrag::State>();
    d->net = &net;
    d->stock = stock;
    d->train = *it;
    d->newId = newId;
    d->unit = unit;
    // The switches as the trains standing over them leave them.
    States walk = states;
    for (const auto& t : trains) {
        d->ids.insert(t.id);
        if (t.position && t.id != trainId && t.over) walk.insert(*t.over);
    }
    if (it->over) walk.insert(*it->over);
    const auto placed = placeTrain(net, *it, stock, states);
    if (!placed || unit < 0 || static_cast<size_t>(unit) >= placed->units.size()) return std::nullopt;
    d->placed = *placed;
    const size_t n = placed->units.size();
    auto way = wayThrough(net, it->position.value_or(core::TrainPosition()), placed->units.back().back, walk);
    if (!way) return std::nullopt;
    d->way = std::move(*way);
    for (size_t i = 0; i < it->units.size() && d->at.size() < n; ++i)
        if (stock.contains(it->units[i].stock)) d->at.push_back(i);
    for (const auto& s : stock) d->longest = std::max(d->longest, s.length);
    d->longest += 2.0;
    // The other trains standing on the way: the stretches they take up.
    for (const auto& t : trains) {
        if (!t.position || t.id == trainId) continue;
        const auto p = placeTrain(net, t, stock, states);
        if (!p || p->units.empty()) continue;
        std::vector<double> ds;
        const auto add = [&](QPointF q) {
            for (const Hit& x : project(d->way, q, kOnTrack)) ds.push_back(x.d);
        };
        for (const auto& u : p->units) {
            add(u.frontTruck);
            add(u.rearTruck);
        }
        add(p->units.front().frontCoupler);
        add(p->units.back().rearCoupler);
        if (ds.empty()) continue;
        std::sort(ds.begin(), ds.end());
        double lo = ds.front();
        double hi = lo;
        for (const double x : ds) {
            if (x - hi > d->longest) {
                d->others.push_back({ t, *p, lo, hi });
                lo = x;
            }
            hi = x;
        }
        d->others.push_back({ t, *p, lo, hi });
    }
    const PlacedUnit& u = placed->units[static_cast<size_t>(unit)];
    d->mid = (u.frontD + u.rearD) / 2.0;
    d->whole = n == 1 || isEngine(stock[u.stock]);
    return CutDrag(std::move(d));
}

std::optional<CutMove> CutDrag::State::to(QPointF p) const {
    const size_t n = placed.units.size();
    const auto& units = train.units;
    // Where along the way the pointer is: the nearest spot, and of two as near (a loop), the one nearest the car.
    std::optional<double> best;
    double bestCost = std::numeric_limits<double>::infinity();
    for (const Hit& x : project(way, p, std::numeric_limits<double>::infinity())) {
        const double cost = x.off + 0.02 * std::abs(x.d - mid);
        if (cost < bestCost - 1e-9) {
            bestCost = cost;
            best = x.d;
        }
    }
    if (!best) return std::nullopt;
    const double want = *best - mid;  // - ahead
    if (std::abs(want) < kMinMove) return std::nullopt;
    const bool ahead = want < 0;
    // The cars that come: those on the side it is dragged to (all of them with an engine, or when the rest isn't on the track).
    const size_t first = whole || ahead ? 0 : static_cast<size_t>(unit);
    size_t last = whole || !ahead ? n - 1 : static_cast<size_t>(unit);
    if (ahead && last + 1 < n && last + 1 >= at.size()) last = n - 1;
    const bool all = first == 0 && last == n - 1;
    // How far they can go: to the end of the track, or to another train.
    double free = 0.0;
    const Other* hit = nullptr;
    if (ahead) {
        const double edge = front(first);
        const double room = placed.units[first].frontD - way.min;
        free = way.endAhead ? room : std::max(0.0, room - longest);
        for (const Other& o : others) {
            if (o.hi > edge + kCoupleReach || o.hi < -kReach) continue;
            const double gap = std::max(0.0, edge - o.hi);
            if (gap < free + kCoupleReach && (!hit || o.hi > hit->hi)) {
                hit = &o;
                free = std::min(free, gap);
            }
        }
    } else {
        const double edge = rear(last);
        const double room = way.max - placed.units[last].rearD;
        free = way.endBehind ? room : std::max(0.0, room - longest);
        for (const Other& o : others) {
            if (o.lo < edge - kCoupleReach) continue;
            const double gap = std::max(0.0, o.lo - edge);
            if (gap < free + kCoupleReach && (!hit || o.lo < hit->lo)) {
                hit = &o;
                free = std::min(free, gap);
            }
        }
    }
    const bool meets = hit && free - std::abs(want) <= kCoupleReach;
    const double dist = meets ? free : std::min(std::abs(want), free);
    const double m = ahead ? -dist : dist;
    if (dist < 1e-6 && !meets) return std::nullopt;
    // A split that moves nothing isn't one.
    if (!all && dist < kMinMove && !meets) return std::nullopt;

    const size_t u0 = all ? 0 : at[first];
    const size_t u1 = all || last == n - 1 ? units.size() : at[last + 1];  // past the last
    const auto it = [&](size_t i) { return units.begin() + static_cast<std::ptrdiff_t>(i); };
    const Units movedUnits(it(u0), it(u1));
    Units rest(units.begin(), it(u0));
    rest.insert(rest.end(), it(u1), units.end());
    CutMove out;
    for (const auto& u : movedUnits) out.moved << u.stock;
    out.by = -m;
    const double head = placed.units[first].frontD + m;
    const double tail = rear(last) + m;
    // Whose record stays: the part with the engine, else the cars left standing.
    const bool movedKeeps = all || (anyEngine(movedUnits, stock) && !anyEngine(rest, stock));
    core::Train mover = movedKeeps ? train : blankCut(newId, train);
    mover.units = movedUnits;
    mover.position = positionAt(way, head, true);
    mover.over = overAt(*net, way, head, tail, true);
    std::optional<core::Train> left;
    if (!all) {
        const size_t restFirst = ahead ? last + 1 : 0;
        const size_t restLast = ahead ? n - 1 : first - 1;
        left = movedKeeps ? blankCut(newId, train) : train;
        left->units = rest;
        left->position = restFirst == 0 ? train.position : std::optional(positionAt(way, placed.units[restFirst].frontD, true));
        left->over = overAt(*net, way, placed.units[restFirst].frontD, rear(restLast), true);
    }
    if (meets) {
        QString why;
        if (auto one = coupled(*net, way, stock, mover, *hit, head, tail, ahead, &why)) {
            out.couples = hit->train.id;
            // A cut coupled away is left with no cars, not deleted: someone coupling to it now still finds it.
            const QString gone = one->id == mover.id ? hit->train.id : mover.id;
            if (ids.contains(gone)) out.change.emptied << gone;
            mover = std::move(*one);
        } else {
            out.blockedBy = hit->train.id;
            out.why = why;
            if (dist < 1e-6) {
                out.moved.clear();
                out.by = 0.0;
                return out;
            }
        }
    }
    out.change.trains.push_back(mover);
    if (left) out.change.trains.push_back(*left);
    if (!all || !out.couples.isEmpty())
        for (const auto& t : out.change.trains)
            for (const auto& u : t.units) out.change.cars.insert(u.stock, t.id);
    return out;
}

CutChange takeOut(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                  const QMap<QString, int>& states, const QString& car, const QString& newId) {
    CutChange change;
    change.cars.insert(car, QString());
    const core::Train* train = standingIn(car, trains);
    if (!train) return change;
    const auto at = std::find_if(train->units.begin(), train->units.end(), [&](const core::TrainUnit& u) { return u.stock == car; });
    const Units ahead(train->units.begin(), at);
    const Units behind(at + 1, train->units.end());
    if (ahead.empty() && behind.empty()) {
        if (train->cut) {
            change.emptied << train->id;
        } else {
            core::Train t = *train;
            t.units.clear();
            t.position.reset();
            change.trains.push_back(t);
        }
        return change;
    }
    const auto placed = placeTrain(net, *train, stock, states);
    States as = states;
    if (train->over) as.insert(*train->over);
    const auto way = placed && train->position ? wayThrough(net, *train->position, placed->units.empty() ? 0.0 : placed->units.back().back, as) : std::nullopt;
    std::optional<double> nextD;
    if (placed && !behind.empty())
        for (const auto& u : placed->units)
            if (u.stock == behind.front().stock) {
                nextD = u.frontD;
                break;
            }
    if (!way || !nextD) {
        // Nothing placed behind it: the rest keeps its place.
        core::Train t = *train;
        t.units = ahead;
        t.units.insert(t.units.end(), behind.begin(), behind.end());
        change.trains.push_back(t);
        return change;
    }
    const auto there = [&](core::Train t) {
        t.units = behind;
        t.position = positionAt(*way, *nextD, true);
        t.over = overAt(net, *way, *nextD, placed->units.back().back, true);
        return t;
    };
    if (ahead.empty()) {
        change.trains.push_back(there(*train));
        return change;
    }
    const bool backKeeps = anyEngine(behind, stock) && !anyEngine(ahead, stock);
    core::Train f = backKeeps ? blankCut(newId, *train) : *train;
    f.units = ahead;
    f.position = train->position;
    f.over = train->over.value_or(States());
    change.trains.push_back(f);
    change.trains.push_back(there(backKeeps ? *train : blankCut(newId, *train)));
    for (const auto& t : change.trains)
        for (const auto& u : t.units) change.cars.insert(u.stock, t.id);
    return change;
}

std::vector<core::Train> afterChange(std::vector<core::Train> trains, const CutChange& change) {
    const auto find = [&](const QString& id) { return std::find_if(trains.begin(), trains.end(), [&](const core::Train& t) { return t.id == id; }); };
    for (const auto& t : change.trains) {
        const auto it = find(t.id);
        if (it != trains.end()) *it = t;
        else trains.push_back(t);
    }
    for (const QString& id : change.emptied)
        if (const auto it = find(id); it != trains.end()) it->units.clear();
    return trains;
}

namespace {

// Fold a later change into an earlier one.
void merge(CutChange& a, const CutChange& b) {
    for (const auto& t : b.trains) {
        a.emptied.removeAll(t.id);
        const auto it = std::find_if(a.trains.begin(), a.trains.end(), [&](const core::Train& x) { return x.id == t.id; });
        if (it != a.trains.end()) *it = t;
        else a.trains.push_back(t);
    }
    for (const QString& id : b.emptied) {
        if (!a.emptied.contains(id)) a.emptied << id;
        std::erase_if(a.trains, [&](const core::Train& t) { return t.id == id; });
    }
    a.cars.insert(b.cars);
}

// The cars of `units` that stand in a cut (or a train that has been one) other than `train`.
QStringList standingElsewhere(const std::vector<core::Train>& trains, const StockById& stock, const core::Train& train) {
    QStringList out;
    for (const auto& u : train.units) {
        const core::Train* at = standingIn(u.stock, trains);
        if (at && at->id != train.id && (at->cut || !stock.value(u.stock).opsCut.value_or(QString()).isEmpty())) out << u.stock;
    }
    return out;
}

void nameCars(CutChange& change, const StockById& stock, const core::Train& train, const QStringList& taken) {
    for (const auto& u : train.units)
        if (taken.contains(u.stock) || !stock.value(u.stock).opsCut.value_or(QString()).isEmpty()) change.cars.insert(u.stock, train.id);
}

}  // namespace

CutChange takeOutAll(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                     const QMap<QString, int>& states, const QStringList& cars, const QString& except) {
    CutChange change;
    std::vector<core::Train> now = trains;
    for (const QString& car : cars) {
        const core::Train* at = standingIn(car, now);
        if (!at || at->id == except) continue;
        const CutChange c = takeOut(net, now, stock, states, car, core::newBbmId());
        merge(change, c);
        now = afterChange(std::move(now), c);
    }
    return change;
}

core::Train newCut(const QString& id, const std::vector<core::RollingStock>& cars, const core::TrainPosition& position) {
    core::Train t;
    t.id = id;
    t.cut = true;
    t.position = position;
    t.over = States();
    if (!cars.empty()) {
        t.trackKind = cars.front().trackKind;
        t.color = cars.front().color;
    }
    for (const auto& s : cars) t.units.push_back({ s.id, false });
    return t;
}

namespace {

std::optional<States> overNow(const TrackNetwork& net, const core::Train& train, const StockById& stock, const States& states, bool ahead) {
    const auto placed = placeTrain(net, train, stock, states);
    if (!train.position || !placed || placed->units.empty()) return std::nullopt;
    States as = states;
    if (train.over) as.insert(*train.over);
    const double tail = placed->units.back().back;
    const auto way = wayThrough(net, *train.position, tail, as);
    if (!way) return std::nullopt;
    return overAt(net, *way, 0.0, tail, ahead);
}

}  // namespace

QMap<QString, int> standOver(const TrackNetwork& net, const core::Train& train, const StockById& stock, const QMap<QString, int>& states) {
    return overNow(net, train, stock, states, true).value_or(States());
}

core::Train turnStanding(const TrackNetwork& net, const core::Train& train, const StockById& stock, const QMap<QString, int>& states) {
    core::Train out = turnedAround(net, train, stock, states);
    if (!train.over) return out;
    if (auto over = overNow(net, train, stock, states, false)) out.over = std::move(*over);
    return out;
}

CutChange putCarsOnTrack(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                         const QMap<QString, int>& states, const QStringList& cars, const core::TrainPosition& position) {
    std::vector<core::RollingStock> list;
    QStringList ids;
    for (const QString& id : cars)
        if (stock.contains(id) && list.size() < static_cast<size_t>(kMaxUnits)) {
            list.push_back(stock[id]);
            ids << id;
        }
    if (list.empty()) return {};
    CutChange change = takeOutAll(net, trains, stock, states, ids);
    core::Train cut = newCut(core::newBbmId(), list, position);
    cut.over = standOver(net, cut, stock, states);
    for (const QString& id : ids) change.cars.insert(id, cut.id);
    change.trains.push_back(std::move(cut));
    return change;
}

CutChange putTrainOnTrack(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                          const QMap<QString, int>& states, const core::Train& train, const core::TrainPosition& position) {
    // (Two plain trains may share a car, as before.)
    const QStringList taken = standingElsewhere(trains, stock, train);
    CutChange change = takeOutAll(net, trains, stock, states, taken, train.id);
    core::Train t = train;
    t.position = position;
    t.over = States();
    t.over = standOver(net, t, stock, states);
    nameCars(change, stock, t, taken);
    std::erase_if(change.trains, [&](const core::Train& x) { return x.id == t.id; });
    change.trains.push_back(std::move(t));
    return change;
}

CutChange changedTrain(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                       const QMap<QString, int>& states, const core::Train& train) {
    CutChange change;
    const auto before = std::find_if(trains.begin(), trains.end(), [&](const core::Train& t) { return t.id == train.id; });
    if (train.position) {
        const QStringList taken = standingElsewhere(trains, stock, train);
        change = takeOutAll(net, trains, stock, states, taken, train.id);
        nameCars(change, stock, train, taken);
    }
    if (before != trains.end())
        for (const auto& u : before->units)
            if (std::none_of(train.units.begin(), train.units.end(), [&](const core::TrainUnit& x) { return x.stock == u.stock; })
                && stock.value(u.stock).opsCut == train.id)
                change.cars.insert(u.stock, QString());
    core::Train t = train;
    if (t.units.empty()) t.position.reset();
    change.trains.push_back(std::move(t));
    return change;
}

}  // namespace bld::track
