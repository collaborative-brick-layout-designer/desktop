#pragma once

// Running trains (trains design 5): each train runs at its scale speed,
// along a named route or, without one, the way the switches are set, and
// stops at the route's end or the end of the track. Its cars follow the way
// it came, each on its trucks (Trains.h placeOnLine). A train coming out of
// a switch's leg into a switch set the other way pushes through, like a
// spring switch: the switch flips for the run and a note says so. Two
// trains that would touch both stop, with a note where.
//
// Running is each person's own: nothing here writes to the layout. The
// switches a run flips are its own (`overlay`), on top of the layout's.
// The web's packages/parts-catalog/src/track/animation.ts is the twin;
// both check fixtures/contracts/train-animation.json.

#include "Routes.h"
#include "Trains.h"

#include <QHash>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace bld::track {

constexpr double kStudMm = 8.0;
// LEGO track gauge (37.5 mm) against standard gauge (1435 mm).
constexpr int kDefaultScale = 38;
constexpr int kMinScale = 10;
constexpr int kMaxScale = 220;
constexpr double kDefaultSpeed = 40.0;  // scale km/h, when a train has none
// The speed choices for watching (times real speed).
inline const std::vector<double>& speedMultipliers() {
    static const std::vector<double> m { 0.25, 0.5, 1.0, 2.0, 4.0 };
    return m;
}

// A layout's scale read back: a whole number in range, or the default.
int asScale(std::optional<double> v);
// How fast a model runs (studs a second) at `kmh` scale km/h, at 1:`scale`.
double studsPerSecond(double kmh, double scale);
// How fast the model really runs, metres a second.
double realMetresPerSecond(double kmh, double scale);

enum class RunNoteKind { PushedThrough, RouteSet, Collision, Arrived, EndOfTrack };
QString runNoteKindName(RunNoteKind k);  // the contract's words: pushedThrough, routeSet, ...

struct RunNote {
    RunNoteKind kind = RunNoteKind::Arrived;
    QString train;
    QString other;      // the other train (a collision)
    QString switchKey;  // the switch (pushed through, set for the route)
    QPointF at;         // where, studs
};

enum class RunState { Running, Arrived, EndOfTrack, Blocked };
QString runStateName(RunState s);  // running, arrived, endOfTrack, blocked

struct RunPlan {
    core::Train train;
    std::optional<core::Route> route;  // none: the way the switches are set, from where it stands
};

// Whether two placed trains touch: the middle between the two cars that do, or none.
std::optional<QPointF> trainsTouch(const std::optional<PlacedTrain>& a, const std::optional<PlacedTrain>& b, const StockById& stock);

// A run of some trains: step(seconds) moves each one on; placed() is where
// each one's cars are now. `parked` are trains on the map that aren't
// running: a running train stops short of them too.
class TrainRun {
public:
    using Parked = std::vector<std::pair<QString, std::optional<PlacedTrain>>>;
    TrainRun(TrackNetwork net, StockById stock, const std::vector<RunPlan>& plans, QMap<QString, int> states,
             double scale = kDefaultScale, Parked parked = {});

    // The switches as the run has them: the layout's, with the run's own on top.
    QMap<QString, int> states() const;
    const QMap<QString, int>& overlay() const { return overlay_; }
    // The layout's switches changed (someone flipped one): a switch flipped there is the layout's again.
    void setLayoutStates(const QMap<QString, int>& states);
    // Someone clicked a switch: it is set as the layout has it again (the click writes the layout).
    void dropOverlay(const QString& key) { overlay_.remove(key); }

    QStringList trainIds() const;
    std::optional<RunState> stateOf(const QString& id) const;
    double distanceOf(const QString& id) const;
    std::optional<core::TrainPosition> headOf(const QString& id) const;
    std::optional<PlacedTrain> placedOf(const QString& id) const;
    bool moving() const;
    // Trains stopped short of another start again (Play after a collision).
    void resume();
    // Trains that touch another (or a parked one) where they start: they don't move.
    const std::vector<RunNote>& startNotes() const { return startNotes_; }

    // Move every running train on by `seconds` of its speed (already times
    // the viewer's speed choice). Trains that would touch stay where they
    // were and stop. What happened comes back as notes.
    std::vector<RunNote> step(double seconds);

private:
    struct Running {
        core::Train train;
        std::vector<RouteStep> steps;
        std::vector<double> starts;
        double total = 0.0;
        double startAt = 0.0;
        std::optional<double> endAt;
        double at = 0.0;
        TrackLine tail;
        size_t entered = 0;
        bool free = false;
        RunState state = RunState::Running;
        double speed = 0.0;
        double length = 0.0;
        std::optional<PlacedTrain> placed;
    };

    std::optional<Running> start(const RunPlan& plan);
    std::pair<size_t, double> locate(const Running& r, double at) const;
    void enter(Running& r, size_t i, std::vector<RunNote>& notes);
    std::optional<RouteStep> nextStep(const Running& r) const;
    TrackLine lineOf(const Running& r) const;
    std::optional<PlacedTrain> place(const Running& r) const;
    void advance(Running& r, double to, std::vector<RunNote>& notes);
    void collide(const std::vector<size_t>* moved, std::vector<RunNote>& notes,
                 const std::vector<std::pair<double, std::optional<PlacedTrain>>>* before);
    const Running* find(const QString& id) const;

    TrackNetwork net_;  // the track as it was at Play (a copy: the map's may be rebuilt meanwhile)
    StockById stock_;
    QMap<QString, int> layoutStates_;
    QMap<QString, int> overlay_;
    double scale_;
    Parked parked_;
    std::vector<Running> runs_;
    std::vector<RunNote> startNotes_;
};

}  // namespace bld::track
