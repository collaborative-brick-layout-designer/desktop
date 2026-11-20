#pragma once

// Named routes (trains design 2.5, 3.4, 4): finding a way along the track
// from one place to another, the switch settings it needs, and checking a
// saved route against the track as it is now. The web's
// packages/parts-catalog/src/track/routes.ts is the twin; both check
// fixtures/contracts/route-search.json.
//
// A search state is a directed step: a way through one brick, entered at
// `from` and left at `to`. From a step, the next ones are the ways through
// the brick linked at `to`, entered there. Cost is length; the shortest way
// wins, ties going to the lower brick id and connection.

#include "TrackNetwork.h"
#include "../core/Route.h"

#include <QMap>
#include <QPointF>
#include <QStringList>

#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace bld::track {

using core::Route;
using core::RouteStep;

struct RouteFound {
    std::vector<RouteStep> steps;
    double length = 0.0;          // studs, start position to end position
    QMap<QString, int> switches;  // switch key -> the exit the route needs
    QStringList turnsAround;      // bricks it runs through both ways round
};

enum class RouteProblem { NoTrack, TurnAround, SamePlace };

struct FindOptions {
    std::vector<TrackPosition> via;
    bool followSwitches = false;  // follow the switches as they are set
    QMap<QString, int> states;
};

// Where a position is along a step, studs from the step's start.
double offsetOnStep(const TrackNetwork& net, const TrackPosition& pos, const RouteStep& step);
// The nearest place on the track to `at` (studs) within `reach`.
std::optional<TrackPosition> nearestPosition(const TrackNetwork& net, QPointF at, double reach);
// The shortest way from `from` through each of `opts.via` to `to`, or what stops it.
std::variant<RouteFound, RouteProblem> findRoute(const TrackNetwork& net, const TrackPosition& from,
                                                const TrackPosition& to, const FindOptions& opts = {});
// The switch settings a way needs: each step that leaves a switch's heel.
QMap<QString, int> routeSwitches(const TrackNetwork& net, const std::vector<RouteStep>& steps);

struct RouteStatus {
    bool broken = false;
    // Broken: the first step that no longer fits, and its brick.
    int at = 0;
    QString brick;
    // OK:
    double length = 0.0;
    QMap<QString, int> switches;
    QStringList setAgainst;  // switches it needs that are set the other way now
    QStringList turnsAround;
};

// A saved route against the track now (`states`: the switch settings).
RouteStatus routeStatus(const TrackNetwork& net, const Route& route, const QMap<QString, int>& states = {});
// The route's line on the map (studs), a run of joined steps a line; a
// broken route as far as it still goes.
std::vector<std::vector<QPointF>> routeLines(const TrackNetwork& net, const Route& route);
// Brick ids in a route renamed.
Route renameRouteBricks(Route route, const std::function<QString(const QString&)>& rename);
// What a search that found nothing says, in plain words.
QString problemText(RouteProblem p);

}  // namespace bld::track
