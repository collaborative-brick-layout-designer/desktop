#pragma once

// A place's spots on the track (Live Ops design 2.1; core/Places.h has the
// places themselves): the run of track a spot is on, how long a train can
// stand there (the fit checks' usable length) and the stretch of track the
// map marks for it. Worked out from the network, never stored. The web's
// packages/parts-catalog/src/track/places.ts is the twin; both tests use
// fixtures/contracts/places.json.

#include "FitChecks.h"

#include <QJsonObject>
#include <QPointF>

#include <optional>
#include <vector>

namespace bld::track {

struct SpotOnTrack {
    std::optional<double> usable;  // what a train can stand in there, studs; none on a switch
    std::vector<QPointF> line;     // the stretch the map marks, studs, along the track
    QPointF middle;                // its middle, for the label
    double length = 0.0;           // the stretch's length, studs
};

// Where a spot is: the piece it was picked on (from -> to through `brick`),
// `wanted` how much track it takes up (studs; none: the whole run). None when
// the piece is no longer on the map or no longer joins those two ends. The
// stretch is the run's usable part, or that much of it around the piece's middle.
std::optional<SpotOnTrack> spotOnTrack(const TrackNetwork& net, const std::vector<TrackRun>& runs, const QString& brick, int from, int to,
                                       std::optional<double> wanted);

// The same for a place's spot as stored (core/Places.h), and the cars it holds.
struct SpotNow {
    std::optional<SpotOnTrack> at;
    std::optional<int> capacity;
};
SpotNow spotNow(const TrackNetwork* net, const std::vector<TrackRun>& runs, const QJsonObject& spot);

}  // namespace bld::track
