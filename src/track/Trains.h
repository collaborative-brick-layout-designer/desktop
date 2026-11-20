#pragma once

// Where a train's cars sit on the track (trains design 3.3). A car rides on
// its trucks: the front truck is on the track, the rear truck is on the
// track a chord (the truck spacing) behind it, and the body lies along the
// line between them; the next car's front coupler meets this car's rear
// coupler. The web's packages/parts-catalog/src/track/trains.ts is the twin;
// both check fixtures/contracts/train-geometry.json.

#include "../core/Train.h"
#include "TrackNetwork.h"

#include <QHash>
#include <QMap>
#include <QPointF>

#include <optional>
#include <vector>

namespace bld::track {

// The track behind a train's head: points with their distance back from the head.
struct LinePoint {
    QPointF p;
    double d = 0.0;  // distance back from the head
    QString brick;   // the brick it's on
};
struct TrackLine {
    std::vector<LinePoint> pts;
    bool ended = false;  // the track ran out before the length asked for
};

// Walk back from `head` along the track for `length` studs, through switches as they are set.
TrackLine trackBehind(const TrackNetwork& net, const core::TrainPosition& head, double length,
                      const QMap<QString, int>& states = {});

struct PlacedUnit {
    QString stock;
    QPointF frontTruck;
    QPointF rearTruck;
    QPointF centre;
    double angle = 0.0;  // degrees: the body's heading, rear to front (y down)
    QPointF frontCoupler;
    QPointF rearCoupler;
    double back = 0.0;  // the rear coupler's distance back from the head along the track
    double frontD = 0.0;  // the trucks' distances back from the head (the fit checks read the track there)
    double rearD = 0.0;
};

struct PlacedTrain {
    std::vector<PlacedUnit> units;
    bool offTrack = false;  // some cars didn't fit on the track behind the head
    TrackLine line;         // the track behind the head the cars were placed on
};

// The point on the line at distance `d` from its start (none past its end).
std::optional<QPointF> lineAt(const TrackLine& line, double d);

using StockById = QHash<QString, core::RollingStock>;

// Where each unit sits; none when the train isn't on the map or its head's track is gone.
std::optional<PlacedTrain> placeTrain(const TrackNetwork& net, const core::Train& train, const StockById& stock,
                                      const QMap<QString, int>& states = {});

// A train's coupler-to-coupler length, studs.
double trainLength(const core::Train& train, const StockById& stock);

// The train turned round: its head moves to its tail (the last unit's rear
// truck), facing the other way, and its units reverse.
core::Train turnedAround(const TrackNetwork& net, const core::Train& train, const StockById& stock,
                         const QMap<QString, int>& states = {});

// A part used as a car's top view: the distance between its two magnet connections (none otherwise).
std::optional<double> partStockLength(const std::vector<std::pair<QString, QPointF>>& conns);

}  // namespace bld::track
