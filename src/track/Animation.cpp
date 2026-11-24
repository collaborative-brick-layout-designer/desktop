#include "Animation.h"

#include "CircuitPath.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace bld::track {

int asScale(std::optional<double> v) {
    if (!v || !std::isfinite(*v) || *v < kMinScale || *v > kMaxScale) return kDefaultScale;
    return static_cast<int>(std::lround(*v));
}

double studsPerSecond(double kmh, double scale) { return ((kmh / 3.6) * 1000.0) / scale / kStudMm; }

double realMetresPerSecond(double kmh, double scale) { return kmh / 3.6 / scale; }

QString runNoteKindName(RunNoteKind k) {
    switch (k) {
    case RunNoteKind::PushedThrough: return QStringLiteral("pushedThrough");
    case RunNoteKind::RouteSet: return QStringLiteral("routeSet");
    case RunNoteKind::Collision: return QStringLiteral("collision");
    case RunNoteKind::Arrived: return QStringLiteral("arrived");
    case RunNoteKind::EndOfTrack: return QStringLiteral("endOfTrack");
    }
    return {};
}

QString runStateName(RunState s) {
    switch (s) {
    case RunState::Running: return QStringLiteral("running");
    case RunState::Arrived: return QStringLiteral("arrived");
    case RunState::EndOfTrack: return QStringLiteral("endOfTrack");
    case RunState::Blocked: return QStringLiteral("blocked");
    }
    return {};
}

namespace {

// A car's body as a box: middle, unit heading, half length, half width.
struct Box {
    QPointF c;
    double ux = 1.0;
    double uy = 0.0;
    double hl = 0.0;
    double hw = 0.0;
};

std::vector<Box> boxesOf(const std::optional<PlacedTrain>& placed, const StockById& stock) {
    std::vector<Box> out;
    if (!placed) return out;
    for (const auto& u : placed->units) {
        const auto it = stock.constFind(u.stock);
        if (it == stock.constEnd()) continue;
        const double a = qDegreesToRadians(u.angle);
        const double body = it->bodyLength > 0 ? it->bodyLength : it->length - 2.0;
        out.push_back({ u.centre, std::cos(a), std::sin(a), body / 2.0, it->width / 2.0 });
    }
    return out;
}

// Whether two boxes overlap (separating axes).
bool boxesTouch(const Box& a, const Box& b) {
    const double dx = b.c.x() - a.c.x();
    const double dy = b.c.y() - a.c.y();
    const double axes[4][2] = { { a.ux, a.uy }, { -a.uy, a.ux }, { b.ux, b.uy }, { -b.uy, b.ux } };
    for (const auto& ax : axes) {
        const double ra = a.hl * std::abs(a.ux * ax[0] + a.uy * ax[1]) + a.hw * std::abs(-a.uy * ax[0] + a.ux * ax[1]);
        const double rb = b.hl * std::abs(b.ux * ax[0] + b.uy * ax[1]) + b.hw * std::abs(-b.uy * ax[0] + b.ux * ax[1]);
        if (std::abs(dx * ax[0] + dy * ax[1]) > ra + rb) return false;
    }
    return true;
}

bool onStep(const core::TrackPosition& pos, const RouteStep& step) {
    return pos.brick == step.brick && ((pos.from == step.from && pos.to == step.to) || (pos.from == step.to && pos.to == step.from));
}

}  // namespace

std::optional<QPointF> trainsTouch(const std::optional<PlacedTrain>& a, const std::optional<PlacedTrain>& b, const StockById& stock) {
    const auto ba = boxesOf(a, stock);
    const auto bb = boxesOf(b, stock);
    if (ba.empty() || bb.empty()) return std::nullopt;
    // Quick: far apart.
    const auto reach = [](const std::vector<Box>& bs) {
        double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
        for (const auto& b : bs) {
            const double r = b.hl + b.hw;
            x0 = std::min(x0, b.c.x() - r);
            y0 = std::min(y0, b.c.y() - r);
            x1 = std::max(x1, b.c.x() + r);
            y1 = std::max(y1, b.c.y() + r);
        }
        return std::array<double, 4> { x0, y0, x1, y1 };
    };
    const auto ra = reach(ba);
    const auto rb = reach(bb);
    if (ra[2] < rb[0] || rb[2] < ra[0] || ra[3] < rb[1] || rb[3] < ra[1]) return std::nullopt;
    for (const auto& p : ba)
        for (const auto& q : bb)
            if (boxesTouch(p, q)) return QPointF((p.c.x() + q.c.x()) / 2.0, (p.c.y() + q.c.y()) / 2.0);
    return std::nullopt;
}

TrainRun::TrainRun(TrackNetwork net, StockById stock, const std::vector<RunPlan>& plans, QMap<QString, int> states, double scale,
                   Parked parked)
    : net_(std::move(net)), stock_(std::move(stock)), layoutStates_(std::move(states)), scale_(scale), parked_(std::move(parked)) {
    for (const auto& plan : plans) {
        auto r = start(plan);
        if (r) runs_.push_back(std::move(*r));
    }
    // Trains that start touching each other (or a parked one) don't move.
    collide(nullptr, startNotes_, nullptr);
}

QMap<QString, int> TrainRun::states() const {
    QMap<QString, int> out = layoutStates_;
    for (auto it = overlay_.cbegin(); it != overlay_.cend(); ++it) out.insert(it.key(), it.value());
    return out;
}

void TrainRun::setLayoutStates(const QMap<QString, int>& states) {
    for (const QString& key : overlay_.keys()) {
        const auto was = layoutStates_.constFind(key);
        const auto now = states.constFind(key);
        const bool same = (was == layoutStates_.constEnd()) == (now == states.constEnd()) &&
                          (now == states.constEnd() || was.value() == now.value());
        if (!same) overlay_.remove(key);
    }
    layoutStates_ = states;
}

QStringList TrainRun::trainIds() const {
    QStringList out;
    for (const auto& r : runs_) out << r.train.id;
    return out;
}

const TrainRun::Running* TrainRun::find(const QString& id) const {
    for (const auto& r : runs_)
        if (r.train.id == id) return &r;
    return nullptr;
}

std::optional<RunState> TrainRun::stateOf(const QString& id) const {
    const auto* r = find(id);
    return r ? std::optional<RunState>(r->state) : std::nullopt;
}

double TrainRun::distanceOf(const QString& id) const {
    const auto* r = find(id);
    return r ? r->at - r->startAt : 0.0;
}

std::optional<core::TrainPosition> TrainRun::headOf(const QString& id) const {
    const auto* r = find(id);
    if (!r) return std::nullopt;
    const auto [i, s] = locate(*r, r->at);
    const auto& st = r->steps[i];
    const double len = net_.pathLength(st.brick, st.from, st.to);
    // Written with from < to where it can be, like a click on the track.
    core::TrainPosition p;
    p.brick = st.brick;
    if (st.from < st.to) {
        p.from = st.from;
        p.to = st.to;
        p.s = s;
        p.facingTo = true;
    } else {
        p.from = st.to;
        p.to = st.from;
        p.s = len - s;
        p.facingTo = false;
    }
    return p;
}

std::optional<PlacedTrain> TrainRun::placedOf(const QString& id) const {
    const auto* r = find(id);
    return r ? r->placed : std::nullopt;
}

bool TrainRun::moving() const {
    return std::any_of(runs_.begin(), runs_.end(), [](const Running& r) { return r.state == RunState::Running; });
}

void TrainRun::resume() {
    for (auto& r : runs_)
        if (r.state == RunState::Blocked) r.state = RunState::Running;
}

std::optional<TrainRun::Running> TrainRun::start(const RunPlan& plan) {
    const auto& train = plan.train;
    const double length = trainLength(train, stock_);
    const double speed = studsPerSecond(train.speed > 0 ? train.speed : kDefaultSpeed, scale_);
    core::TrainPosition head;
    std::vector<RouteStep> steps;
    double startAt = 0.0;
    std::optional<double> endAt;
    if (plan.route) {
        const auto& route = *plan.route;
        if (route.steps.empty()) return std::nullopt;
        for (const auto& s : route.steps)
            if (net_.pathLength(s.brick, s.from, s.to) <= 0) return std::nullopt;
        steps = route.steps;
        const auto& first = steps.front();
        const auto& last = steps.back();
        startAt = onStep(route.from, first) ? offsetOnStep(net_, route.from, first) : 0.0;
        const double lastLen = net_.pathLength(last.brick, last.from, last.to);
        const double endOff = onStep(route.to, last) ? offsetOnStep(net_, route.to, last) : lastLen;
        double before = 0.0;
        for (size_t i = 0; i + 1 < steps.size(); ++i) before += net_.pathLength(steps[i].brick, steps[i].from, steps[i].to);
        endAt = before + endOff;
        const double len0 = net_.pathLength(first.brick, first.from, first.to);
        head.brick = first.brick;
        if (first.from < first.to) {
            head.from = first.from;
            head.to = first.to;
            head.s = startAt;
            head.facingTo = true;
        } else {
            head.from = first.to;
            head.to = first.from;
            head.s = len0 - startAt;
            head.facingTo = false;
        }
    } else {
        if (!train.position) return std::nullopt;
        const auto& p = *train.position;
        const double len = net_.pathLength(p.brick, p.from, p.to);
        if (len <= 0) return std::nullopt;
        const double s = std::max(0.0, std::min(p.s, len));
        steps = { p.facingTo ? RouteStep { p.brick, p.from, p.to } : RouteStep { p.brick, p.to, p.from } };
        startAt = p.facingTo ? s : len - s;
        head = p;
        head.s = s;
    }
    Running r;
    r.train = train;
    r.steps = std::move(steps);
    for (const auto& s : r.steps) {
        r.starts.push_back(r.total);
        r.total += net_.pathLength(s.brick, s.from, s.to);
    }
    r.startAt = startAt;
    r.endAt = endAt;
    r.at = startAt;
    r.tail = trackBehind(net_, head, length + 64.0, states());
    r.free = !plan.route;
    r.speed = speed;
    r.length = length;
    // The step it starts on: its switch is set as the route needs (a free train is already on its way).
    std::vector<RunNote> none;
    if (plan.route) enter(r, 0, none);
    r.entered = 1;
    r.placed = place(r);
    return r;
}

std::pair<size_t, double> TrainRun::locate(const Running& r, double at) const {
    size_t lo = 0;
    size_t hi = r.steps.size() - 1;
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (r.starts[mid] <= at) lo = mid;
        else hi = mid - 1;
    }
    const auto& st = r.steps[lo];
    return { lo, std::max(0.0, std::min(at - r.starts[lo], net_.pathLength(st.brick, st.from, st.to))) };
}

void TrainRun::enter(Running& r, size_t i, std::vector<RunNote>& notes) {
    const RouteStep st = r.steps[i];
    const auto now = states();
    if (const auto* facing = net_.switchAt(switchKey(st.brick, st.from));
        facing && facing->exits.contains(st.to) && net_.settingOf(*facing, now) != st.to) {
        overlay_.insert(facing->key, st.to);
        notes.push_back({ RunNoteKind::RouteSet, r.train.id, {}, facing->key, net_.port(st.brick, st.from).value_or(std::pair<QPointF, double>()).first });
    }
    if (const auto* trailing = net_.switchAt(switchKey(st.brick, st.to));
        trailing && trailing->exits.contains(st.from) && net_.settingOf(*trailing, now) != st.from) {
        overlay_.insert(trailing->key, st.from);
        notes.push_back({ RunNoteKind::PushedThrough, r.train.id, {}, trailing->key, net_.port(st.brick, st.to).value_or(std::pair<QPointF, double>()).first });
    }
}

std::optional<RouteStep> TrainRun::nextStep(const Running& r) const {
    const auto& last = r.steps.back();
    const auto next = net_.partner({ last.brick, last.to });
    if (!next) return std::nullopt;
    const QVector<int> exits = net_.exits(next->brick, next->conn);
    if (exits.isEmpty()) return std::nullopt;
    const auto* sw = net_.switchAt(switchKey(next->brick, next->conn));
    const int to = sw ? net_.settingOf(*sw, states()) : exits.front();
    return RouteStep { next->brick, next->conn, to };
}

TrackLine TrainRun::lineOf(const Running& r) const {
    const double need = r.length + 64.0;
    const auto [i, s] = locate(r, r.at);
    TrackLine out;
    double d = 0.0;
    const auto push = [&](QPointF p, const QString& brick) {
        if (!out.pts.empty()) d += std::hypot(p.x() - out.pts.back().p.x(), p.y() - out.pts.back().p.y());
        out.pts.push_back({ p, d, brick });
    };
    for (long k = static_cast<long>(i); k >= 0 && d < need; --k) {
        const auto& st = r.steps[static_cast<size_t>(k)];
        const auto& pts = net_.path(st.brick, st.from, st.to);
        if (pts.size() < 2) break;
        const double hi = k == static_cast<long>(i) ? s : pts.back().s;
        const double lo = k == 0 ? std::min(r.startAt, hi) : 0.0;
        if (out.pts.empty()) push(pointAt(pts, hi).p, st.brick);
        for (long q = static_cast<long>(pts.size()) - 1; q >= 0 && d < need; --q) {
            const auto& pt = pts[static_cast<size_t>(q)];
            if (pt.s >= hi || pt.s <= lo) continue;
            push(pt.p, st.brick);
        }
        // Its start: the joint with the step before (or the run's start).
        if (d < need) push(pointAt(pts, lo).p, st.brick);
    }
    if (d < need) {
        const double base = d;
        for (size_t q = 1; q < r.tail.pts.size(); ++q) {
            const auto& t = r.tail.pts[q];
            out.pts.push_back({ t.p, base + t.d, t.brick });
            if (base + t.d >= need) break;
        }
        d = out.pts.back().d;
    }
    out.ended = d < need && r.tail.ended;
    return out;
}

std::optional<PlacedTrain> TrainRun::place(const Running& r) const {
    TrackLine line = lineOf(r);
    if (line.pts.size() < 2) return std::nullopt;
    return placeOnLine(std::move(line), r.train.units, stock_);
}

void TrainRun::advance(Running& r, double to, std::vector<RunNote>& notes) {
    double target = r.endAt ? std::min(to, *r.endAt) : to;
    for (;;) {
        if (target < r.total - 1e-9 || !r.free) break;
        const auto next = nextStep(r);
        if (!next) {
            target = std::min(target, r.total);
            break;
        }
        r.starts.push_back(r.total);
        r.steps.push_back(*next);
        r.total += net_.pathLength(next->brick, next->from, next->to);
    }
    target = std::min(target, r.total);
    // Each step the head comes into.
    while (r.entered < r.steps.size() && r.starts[r.entered] < target - 1e-9) {
        enter(r, r.entered, notes);
        ++r.entered;
    }
    r.at = target;
    const auto headPoint = [&] {
        const auto h = headOf(r.train.id);
        const auto p = h ? net_.pointAtPosition(*h) : std::nullopt;
        return p ? p->p : QPointF();
    };
    if (r.endAt && r.at >= *r.endAt - 1e-9) {
        r.state = RunState::Arrived;
        notes.push_back({ RunNoteKind::Arrived, r.train.id, {}, {}, headPoint() });
    } else if (r.free && r.at >= r.total - 1e-9 && !nextStep(r)) {
        r.state = RunState::EndOfTrack;
        notes.push_back({ RunNoteKind::EndOfTrack, r.train.id, {}, {}, headPoint() });
    }
}

std::vector<RunNote> TrainRun::step(double seconds) {
    std::vector<RunNote> notes;
    std::vector<size_t> moved;
    std::vector<std::pair<double, std::optional<PlacedTrain>>> before(runs_.size());
    for (size_t i = 0; i < runs_.size(); ++i) {
        auto& r = runs_[i];
        if (r.state != RunState::Running || seconds <= 0) continue;
        moved.push_back(i);
        before[i] = { r.at, r.placed };
        advance(r, r.at + r.speed * seconds, notes);
        r.placed = place(r);
    }
    if (!moved.empty()) collide(&moved, notes, &before);
    return notes;
}

void TrainRun::collide(const std::vector<size_t>* moved, std::vector<RunNote>& notes,
                       const std::vector<std::pair<double, std::optional<PlacedTrain>>>* before) {
    const auto didMove = [&](size_t i) { return !moved || std::find(moved->begin(), moved->end(), i) != moved->end(); };
    struct Hit {
        size_t a;
        std::optional<size_t> b;
        QString other;
        QPointF at;
    };
    std::vector<Hit> hits;
    for (size_t i = 0; i < runs_.size(); ++i) {
        for (size_t j = i + 1; j < runs_.size(); ++j) {
            if (moved && !didMove(i) && !didMove(j)) continue;
            if (const auto at = trainsTouch(runs_[i].placed, runs_[j].placed, stock_)) hits.push_back({ i, j, runs_[j].train.id, *at });
        }
        if (moved && !didMove(i)) continue;
        for (const auto& [id, p] : parked_) {
            if (find(id)) continue;
            if (const auto at = trainsTouch(runs_[i].placed, p, stock_)) hits.push_back({ i, std::nullopt, id, *at });
        }
    }
    for (const auto& h : hits) {
        for (const std::optional<size_t> k : { std::optional<size_t>(h.a), h.b }) {
            if (!k) continue;
            auto& r = runs_[*k];
            if (before && moved && didMove(*k)) {
                // It moved into the other: back where it was, stopped (not at an end it never reached).
                r.at = (*before)[*k].first;
                r.placed = (*before)[*k].second;
                r.state = RunState::Blocked;
            } else if (r.state == RunState::Running) {
                r.state = RunState::Blocked;
            }
        }
        // A route's own end, or the track's, isn't news when the train stopped short of another.
        const QString ta = runs_[h.a].train.id;
        const QString tb = h.b.has_value() ? runs_[h.b.value()].train.id : QString();
        notes.erase(std::remove_if(notes.begin(), notes.end(),
                                   [&](const RunNote& n) {
                                       return (n.kind == RunNoteKind::Arrived || n.kind == RunNoteKind::EndOfTrack) &&
                                              (n.train == ta || (!tb.isEmpty() && n.train == tb));
                                   }),
                    notes.end());
        notes.push_back({ RunNoteKind::Collision, ta, h.other, {}, h.at });
    }
}

}  // namespace bld::track
