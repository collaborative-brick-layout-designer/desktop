#pragma once

// The track network (trains design 2.1, 3.4): derived from the bricks and
// the parts library, never stored or synced, rebuilt when bricks change.
// Every client builds the same network from the same bricks. The web's
// packages/parts-catalog/src/track/network.ts is the twin; both check
// fixtures/contracts/track-network.json.
//
// A node is one connection of one brick (PortRef). Inside a brick a train
// follows the part's paths (PartTrack.h); between bricks it crosses a link
// (linkedToId, which connectivity re-derives from positions). A switch is
// a (brick, heel) a train can leave by more than one way; its setting is
// stored in the sidecar's switchStates, the default being the exit that
// turns least.
//
// Two loose ends of one kind of track that nearly meet, facing each other
// (a gap: within kGapReach, not half a stud, so not linked) are joined for
// trains too: real track flexes that far, and a loop with a hairline gap
// is still a loop. gaps() lists them so the editor can say so.

#include "CircuitPath.h"
#include "PartTrack.h"
#include "../core/Route.h"

#include <QHash>
#include <QMap>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include <optional>
#include <vector>

namespace bld::core {
class Map;
}
namespace bld::parts {
class PartsLibrary;
}

namespace bld::track {

// One brick as the network reads it.
struct TrackBrickInput {
    QString id;
    QString partKey;               // library key, for track-paths.json
    QVector<TrackConn> conns;      // the part's connections (local)
    QVector<QPointF> points;       // where each connection is on the map (studs)
    double orientation = 0.0;
    QVector<std::pair<QString, QString>> connexions;  // (id, linkedTo) per connection
};

// One connection of one brick.
struct PortRef {
    QString brick;
    int conn = 0;
    bool operator==(const PortRef&) const = default;
};

// Two ends of track that nearly meet, joined for trains though not linked.
struct TrackGap {
    PortRef a;
    PortRef b;
    QPointF at;        // halfway between them, studs
    double gap = 0.0;  // how far apart, studs
};

// How far apart (studs) two facing loose ends may be and still join for trains. Links are made within half a stud.
inline constexpr double kGapReach = 1.0;
// How far (degrees) from facing each other they may be turned.
inline constexpr double kGapTurn = 15.0;

// A switch on the map.
struct SwitchPoint : PartSwitch {
    QString brick;
    QString key;  // "<brick>/<heel>": its key in switchStates
};

// A place on the track (core/Route.h): train heads, route ends and (later)
// cars placed by Live Ops are all positions; one moves with its brick.
using core::TrackPosition;

// A switch's key in switchStates: "<brick id>/<heel connection>".
QString switchKey(const QString& brick, int heel);
// The brick and heel a switchStates key names.
std::optional<PortRef> parseSwitchKey(const QString& key);

class TrackNetwork {
public:
    TrackNetwork() = default;
    explicit TrackNetwork(const std::vector<TrackBrickInput>& bricks);

    // Bricks with at least one way through, in input order.
    const QStringList& brickIds() const { return order_; }
    bool has(const QString& brick) const { return bricks_.contains(brick); }
    const PartTrack* trackOf(const QString& brick) const;

    // The connection a port is linked to, when that is track of the same kind,
    // or the loose end it nearly meets (a gap).
    std::optional<PortRef> partner(const PortRef& port) const;
    // Loose ends that nearly meet, joined for trains (in brick id order).
    const QVector<TrackGap>& gaps() const { return gaps_; }
    // The connections a train coming in at `entry` can leave `brick` by.
    QVector<int> exits(const QString& brick, int entry) const;
    // Every switch, in brick order then heel.
    const QVector<SwitchPoint>& switches() const { return switches_; }
    const SwitchPoint* switchAt(const QString& key) const;
    // Where the switch is set: the stored exit when it is one of its exits, else its default.
    int settingOf(const SwitchPoint& sw, const QMap<QString, int>& states) const;

    // Where a connection is on the map (studs), and its outward angle (degrees).
    std::optional<std::pair<QPointF, double>> port(const QString& brick, int conn) const;
    // The centreline from `from` to `to` through `brick`, empty when that isn't a way through.
    const std::vector<CircuitPathPoint>& path(const QString& brick, int from, int to) const;
    double pathLength(const QString& brick, int from, int to) const;
    // The point at a track position, or nullopt when its brick or path is gone.
    std::optional<CircuitPathPoint> pointAtPosition(const TrackPosition& pos) const;
    // The kind of track a brick is (its first track connection's), empty when none.
    QString kindOf(const QString& brick) const;
    // Whether the brick's track carries power: it has an electric circuit (9V).
    bool isPowered(const QString& brick) const;

private:
    struct BrickTrack {
        TrackBrickInput input;
        PartTrack track;
    };
    QHash<QString, BrickTrack> bricks_;
    QStringList order_;
    QHash<QString, PortRef> owner_;  // connection id -> port
    QVector<SwitchPoint> switches_;
    QHash<QString, int> switchIndex_;
    mutable QHash<QString, std::vector<CircuitPathPoint>> paths_;
    QHash<QString, PortRef> bridges_;  // "<brick>/<conn>" -> the loose end it nearly meets
    QVector<TrackGap> gaps_;

    std::optional<PortRef> linkedPartner(const PortRef& port) const;
    void findGaps();
};

// switchStates with only the keys that name a switch here and one of its exits.
QMap<QString, int> cleanSwitchStates(const QMap<QString, int>& states, const TrackNetwork& net);
// switchStates without the keys whose brick is no longer on the map.
QMap<QString, int> withoutGoneSwitches(const QMap<QString, int>& states, const QSet<QString>& bricks);
// The next exit round a switch from its setting (a click in Run mode).
int nextExit(const SwitchPoint& sw, int current);

// The bricks of every brick layer, as the network reads them (connections
// where connectivity places them: the sprite centre plus the turned local point).
std::vector<TrackBrickInput> trackBricks(const core::Map& map, parts::PartsLibrary& lib);
TrackNetwork trackNetworkOf(const core::Map& map, parts::PartsLibrary& lib);
// Every brick id on the map's brick layers.
QSet<QString> brickIdsOf(const core::Map& map);

}  // namespace bld::track
