#pragma once

// Named routes (trains design 2.5, 3.4, 4): finding a way along the track
// from one place to another, the switch settings it needs, and checking a
// saved route against the track as it is now. The web's
// packages/parts-catalog/src/track/routes.ts is the twin; both check
// fixtures/contracts/route-search.json.
//
// A search state is a directed step: a way through one brick, entered at
// `from` and left at `to`, so the way a train faces is part of it. From a
// step, the next ones are the ways through the brick linked at `to`,
// entered there. A train never turns round on the spot; it may run the
// whole loop round, past its own start, and through a switch more than
// once. Cost is length; the shortest way wins, ties going to the lower
// brick id and connection.

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

// Which way along a position's path: towards its `to`, or its `from` (a train's facing).
enum class Facing { To, From };

struct RouteFound {
    std::vector<RouteStep> steps;
    double length = 0.0;          // studs, start position to end position
    QMap<QString, int> switches;  // switch key -> the exit the route needs (the first, when it needs two)
    QStringList turnsAround;      // bricks it runs through both ways round
    Facing facing = Facing::To;   // which way it leaves the start
    QStringList bothWays;         // switches it leaves by both legs: one has to be flipped on the way
};

enum class RouteProblem { NoTrack, TurnAround, SamePlace };

struct FindOptions {
    std::vector<TrackPosition> via;
    bool followSwitches = false;  // follow the switches as they are set
    QMap<QString, int> states;
    // The way the train leaves the start (a train's own facing, or the person's pick); either when unset.
    std::optional<Facing> facing;
};

// Where a position is along a step, studs from the step's start.
double offsetOnStep(const TrackNetwork& net, const TrackPosition& pos, const RouteStep& step);
// The nearest place on the track to `at` (studs) within `reach`.
std::optional<TrackPosition> nearestPosition(const TrackNetwork& net, QPointF at, double reach);
// A place and the same place on the brick's other ways through it (a
// switch's legs share their heel end), each written so that heading
// towards its `to` is the same way as heading towards `pos.to`.
std::vector<TrackPosition> sameSpot(const TrackNetwork& net, const TrackPosition& pos);
// The shortest way from `from` through each of `opts.via` to `to`, or what
// stops it. The train leaves `from` heading `opts.facing` (either way when
// unset) and never turns round on the spot, so the way may run the whole
// loop round, past its own start and through a switch twice; after a stop
// it carries on the way it arrived. Of the two ways round, one that needs a
// switch set both ways loses to one that doesn't.
std::variant<RouteFound, RouteProblem> findRoute(const TrackNetwork& net, const TrackPosition& from,
                                                const TrackPosition& to, const FindOptions& opts = {});
// The switch settings a way needs: each step that leaves a switch's heel
// (the first, for a switch it leaves both ways).
QMap<QString, int> routeSwitches(const TrackNetwork& net, const std::vector<RouteStep>& steps);
// Switches a way leaves by both legs: someone has to flip them while the train runs.
QStringList routeBothWays(const TrackNetwork& net, const std::vector<RouteStep>& steps);
// Which way a saved route leaves its start.
Facing routeFacing(const Route& route);

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
    QStringList bothWays;    // switches it leaves by both legs
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
