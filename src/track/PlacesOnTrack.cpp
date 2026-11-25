#include "PlacesOnTrack.h"

#include "CircuitPath.h"
#include "TrackNetwork.h"
#include "../core/Places.h"

#include <algorithm>
#include <cmath>

namespace bld::track {

namespace {

// The run's line from `lo` to `hi` studs along it (one point when they are the same).
std::vector<QPointF> between(const TrackNetwork& net, const TrackRun& run, double lo, double hi) {
    std::vector<QPointF> out;
    const auto add = [&out](QPointF p) {
        if (out.empty() || std::hypot(out.back().x() - p.x(), out.back().y() - p.y()) > 1e-6) out.push_back(p);
    };
    double at = 0.0;
    for (const auto& s : run.steps) {
        const auto& pts = net.path(s.brick, s.from, s.to);
        const double len = pts.empty() ? 0.0 : pts.back().s;
        if (pts.size() > 1 && at + len >= lo && at <= hi) {
            add(pointAt(pts, std::max(0.0, lo - at)).p);
            for (const auto& p : pts)
                if (at + p.s > lo && at + p.s < hi) add(p.p);
            add(pointAt(pts, std::min(len, hi - at)).p);
        }
        at += len;
    }
    return out;
}

}  // namespace

std::optional<SpotOnTrack> spotOnTrack(const TrackNetwork& net, const std::vector<TrackRun>& runs, const QString& brick, int from, int to,
                                       std::optional<double> wanted) {
    const auto& own = net.path(brick, from, to);
    if (own.size() < 2) return std::nullopt;
    const QPointF ownMiddle = pointAt(own, own.back().s / 2).p;
    SpotOnTrack out;
    const TrackRun* run = runAt(runs, brick, from, to);
    if (!run) {
        for (const auto& p : own) out.line.push_back(p.p);
        out.middle = ownMiddle;
        out.length = own.back().s;
        return out;
    }
    // The piece's middle along the run, and the run's usable part.
    double at = 0.0;
    double mid = 0.0;
    for (const auto& s : run->steps) {
        const double len = net.pathLength(s.brick, s.from, s.to);
        if (s.brick == brick && std::min(s.from, s.to) == std::min(from, to) && std::max(s.from, s.to) == std::max(from, to)) mid = at + len / 2;
        at += len;
    }
    double lo = 0.0;
    double hi = run->length;
    if (run->ends[0].kind != RunEnd::Kind::Loop && run->usable > 0) {
        lo = run->ends[0].foul;
        hi = run->length - run->ends[1].foul;
    }
    if (wanted && *wanted < hi - lo) {
        lo = std::min(std::max(mid - *wanted / 2, lo), hi - *wanted);
        hi = lo + *wanted;
    }
    out.usable = run->usable;
    out.line = between(net, *run, lo, hi);
    const auto middle = between(net, *run, (lo + hi) / 2, (lo + hi) / 2);
    out.middle = middle.empty() ? ownMiddle : middle.front();
    out.length = hi - lo;
    return out;
}

SpotNow spotNow(const TrackNetwork* net, const std::vector<TrackRun>& runs, const QJsonObject& spot) {
    SpotNow out;
    const QJsonObject t = spot.value(QLatin1String("track")).toObject();
    if (net && !t.isEmpty()) {
        std::optional<double> wanted;
        if (spot.value(QLatin1String("capacity")).isDouble()) wanted = spot.value(QLatin1String("capacity")).toInt() * core::places::kTypicalCar;
        out.at = spotOnTrack(*net, runs, t.value(QLatin1String("brick")).toString(), t.value(QLatin1String("from")).toInt(),
                             t.value(QLatin1String("to")).toInt(), wanted);
    }
    out.capacity = core::places::spotCapacity(spot, out.at ? out.at->usable : std::nullopt);
    return out;
}

}  // namespace bld::track
