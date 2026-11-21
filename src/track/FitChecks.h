#pragma once

// Fit checks (trains design 3.3, 3.5): computed from the track and the trains
// on it, never stored. Track runs and their usable lengths (between fouling
// points, bumpers and switches: the siding lengths), and, for each train on
// the map, whether it fits its run, whether its trucks and couplers can take
// the curves, whether its cars clear the other tracks and trains, and whether
// it is on its kind of track; for each route, whether it turns the train
// around (and on powered track, needs a polarity switch). The words are each
// app's own. The web's packages/parts-catalog/src/track/fitChecks.ts is the
// twin; both check fixtures/contracts/train-geometry.json.

#include "../core/Route.h"
#include "Trains.h"

#include <QMap>
#include <QPointF>
#include <QString>

#include <optional>
#include <vector>

namespace bld::track {

struct Overhang {
    double middle = 0.0;  // how far the body's middle swings inside the track, studs
    double end = 0.0;     // how far its ends swing outside
    double corner = 0.0;  // how far its outer corners pass the track's outer edge
    double swivel = 0.0;  // how far each truck turns under the body, degrees
};

// A car of bodyLength x width on trucks truckSpacing apart, on a curve of `radius` (studs).
Overhang overhang(double radius, double truckSpacing, double bodyLength, double width);

// The track ahead from the start of the way from -> to through `brick`, `length` studs, through switches as set.
TrackLine lineAhead(const TrackNetwork& net, const QString& brick, int from, int to, double length,
                    const QMap<QString, int>& states = {});

// How far past the end of a switch's leg its fouling point is (0 inside the switch; `max` when never).
double foulingBeyond(const TrackNetwork& net, const SwitchPoint& sw, int leg, double clearance, double max = 200.0);

struct RunEnd {
    enum class Kind { End, Heel, Leg, Loop };
    Kind kind = Kind::End;
    QString switchKey;  // heel and leg
    double foul = 0.0;  // how much of the run its fouling point takes (legs)
};

struct TrackRun {
    std::vector<core::RouteStep> steps;
    double length = 0.0;
    double usable = 0.0;  // the length less the fouling points at switch legs
    RunEnd ends[2];
    QPointF middle;       // for its label (studs)
};

// The network's runs of track between switches, bumpers and open ends (and rings with no switch).
std::vector<TrackRun> trackRuns(const TrackNetwork& net);
// The run a place on the track is in (none on a switch).
const TrackRun* runAt(const std::vector<TrackRun>& runs, const QString& brick, int from, int to);

struct FitCheck {
    // siding, truck, coupler, uncoupled, clearance, overlap, trackKind, offEnd, lost, reverse, power, gap
    QString key;
    QString kind;
    bool error = false;  // else a warning
    QString train;
    QString route;
    QString stock;
    int unit = -1;
    QString other;  // the other train (overlap), or the brick (clearance, reverse, power, gap: one of the two)
    QPointF at;
    QMap<QString, double> values;  // lengths in studs, angles in degrees
};

struct FitInput {
    std::vector<core::Train> trains;
    StockById stock;
    std::vector<core::Route> routes;
    QMap<QString, int> states;
};

// Every fit check for the trains on the map and the routes, in a stable order (`runs`: worked out already, or none).
std::vector<FitCheck> fitChecks(const TrackNetwork& net, const FitInput& input, const std::vector<TrackRun>* runs = nullptr);

}  // namespace bld::track
