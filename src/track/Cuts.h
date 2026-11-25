#pragma once

// Cars on the track (Live Ops design 2.3): every group of cars coupled
// together is a cut, a train record with `cut`. A car in one is in it
// alone: its own opsCut says which, so two people who move the same car at
// once leave it in one place, and one whose cut's list lost it in a merge
// is still there. Dragging a car pulls it, and the cars on the side it is
// dragged to, off the rest; an engine takes the whole train. A cut that
// meets another couples to it. `over` keeps the way a cut stands over
// switches, so one flipped under it doesn't move its cars.
//
// The web's packages/parts-catalog/src/track/cuts.ts is the twin; both
// check fixtures/contracts/cuts.json.

#include "Trains.h"

#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

namespace bld::core {
struct Sidecar;
}

namespace bld::track {

// What the apps hold a layout to (the server takes twice the counts).
inline constexpr int kMaxUnits = 200;     // cars in one train or cut
inline constexpr int kMaxRecords = 2000;  // cars, and trains (cuts included), in a layout
// An emptied cut is kept this long, ms (someone offline may still couple to it).
inline constexpr double kEmptiedKeepMs = 24.0 * 60 * 60 * 1000;

bool isEngine(const core::RollingStock& s);

// The trains as they stand: each car of a cut in one place only, and none left out.
std::vector<core::Train> settleTrains(const std::vector<core::Train>& trains, const StockById& stock);

// A layout's trains as they stand, without cuts that have no cars left, and its cars.
std::vector<core::Train> standingTrains(const core::Sidecar& sidecar);
StockById stockById(const core::Sidecar& sidecar);

// The cut or train on the track a car is in (settled trains), or null.
const core::Train* standingIn(const QString& car, const std::vector<core::Train>& trains);

// What a drag does: the records to write, whole; the record each of these
// cars is in now (empty: none); and cuts left with no cars.
struct CutChange {
    std::vector<core::Train> trains;
    QMap<QString, QString> cars;
    QStringList emptied;
    bool isEmpty() const { return trains.empty() && cars.isEmpty() && emptied.isEmpty(); }
};

struct CutMove {
    QStringList moved;   // the cars that move
    double by = 0.0;     // how far, studs (+ the way the train faces)
    QString couples;     // the record they couple to
    QString blockedBy;   // they stop against this one and don't couple...
    QString why;         // ...because: trains (both are), coupler (none there), full
    CutChange change;
};

// A car being dragged along the track (startDrag).
class CutDrag {
public:
    CutDrag();
    ~CutDrag();
    CutDrag(CutDrag&&) noexcept;
    CutDrag& operator=(CutDrag&&) noexcept;
    CutDrag(const CutDrag&) = delete;
    CutDrag& operator=(const CutDrag&) = delete;
    // What dropping at `at` (studs) would do; none: nothing.
    std::optional<CutMove> to(QPointF at) const;

    struct State;
    explicit CutDrag(std::unique_ptr<State> s);

private:
    std::unique_ptr<State> s_;
};

// Start dragging the car at `unit` (its index among the placed cars) of
// `trainId`. `trains` are settled; `newId` names the cut a split makes.
// None when it can't be dragged. The network must outlive the drag.
std::optional<CutDrag> startDrag(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                                 const QMap<QString, int>& states, const QString& trainId, int unit, const QString& newId);

// Take a car out of the cut or train it stands in: the cars ahead of it and
// those behind stay where they are (`newId` names the other cut).
CutChange takeOut(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                  const QMap<QString, int>& states, const QString& car, const QString& newId);
// The trains with a change made.
std::vector<core::Train> afterChange(std::vector<core::Train> trains, const CutChange& change);
// Take these cars out of wherever they stand (not out of `except`).
CutChange takeOutAll(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                     const QMap<QString, int>& states, const QStringList& cars, const QString& except = {});

// Cars put on the track at `position` as a new cut.
core::Train newCut(const QString& id, const std::vector<core::RollingStock>& cars, const core::TrainPosition& position);
// How a train standing as it does now passes the switches under it.
QMap<QString, int> standOver(const TrackNetwork& net, const core::Train& train, const StockById& stock, const QMap<QString, int>& states);
// A train turned round (turnedAround), standing over switches as it does.
core::Train turnStanding(const TrackNetwork& net, const core::Train& train, const StockById& stock, const QMap<QString, int>& states);

// Cars put on the track coupled, as one cut, taken out of where they stand.
CutChange putCarsOnTrack(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                         const QMap<QString, int>& states, const QStringList& cars, const core::TrainPosition& position);
// A train put on the track: its cars come off whatever cut they stand in.
CutChange putTrainOnTrack(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                          const QMap<QString, int>& states, const core::Train& train, const core::TrainPosition& position);
// A train as changed in its dialog: cars added to one on the track come off where they stood, cars left out are no longer in it.
CutChange changedTrain(const TrackNetwork& net, const std::vector<core::Train>& trains, const StockById& stock,
                       const QMap<QString, int>& states, const core::Train& train);

}  // namespace bld::track
