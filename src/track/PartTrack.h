#pragma once

// The ways a train can go through one part (trains design 3.2), and where
// a part is a switch. The web's packages/parts-catalog/src/track/
// partTrack.ts is the twin; both check fixtures/contracts/track-network.json.
//
// Rules, in order:
//   1. track-paths.json names the part: exactly its paths.
//   2. Electric circuits (two connections with opposite plugs) whose ends
//      are both track of one kind. They already leave out frog to frog.
//   3. Two track connections of one kind: one path.
//   4. Three or more track connections, no circuits (monorail points,
//      Duplo, 4.5V/12V): i to j when entering at i and leaving at j turns
//      no more than maxTurnDegrees.
// Every path can be used both ways.

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <utility>

namespace bld::track {

// A part's connection as the track needs it: local studs, outward angle in
// degrees, electric plug.
struct TrackConn {
    QString type;
    double x = 0.0;
    double y = 0.0;
    double angle = 0.0;
    int plug = 0;
};

// track-kinds.json: which connection types are track, and what kind.
struct TrackKindsTable {
    QHash<QString, QString> types;          // type -> kind
    QVector<std::pair<QString, QString>> prefixes;  // prefix -> kind, in file order
    QStringList notTrack;
    QString fallback = QStringLiteral("rail");
    double maxTurnDegrees = 60.0;
    QStringList kinds;                      // every kind the table names
};

// track-paths.json: the parts whose ways through are given outright
// (keys lower case).
struct TrackPathsTable {
    QHash<QString, QVector<std::pair<int, int>>> parts;
};

// The tables built in (src/track/data/, the shared contracts word for word).
const TrackKindsTable& trackKinds();
const TrackPathsTable& trackPaths();
// The same, read from JSON (tests read the contracts' own copies).
TrackKindsTable trackKindsFromJson(const QJsonObject& o);
TrackPathsTable trackPathsFromJson(const QJsonObject& o);

// A switch: a connection a train can leave from by more than one way.
struct PartSwitch {
    int heel = 0;
    QVector<int> exits;        // lowest first
    int defaultExit = 0;       // the exit that turns least
    QMap<int, QString> legs;   // exit -> "straight", "left", "right" ("left 2" for a second)
    bool operator==(const PartSwitch&) const = default;
};

struct PartTrack {
    QVector<std::optional<QString>> kinds;  // each connection's kind, nullopt when not track
    QVector<std::pair<int, int>> paths;     // i < j, sorted
    QVector<PartSwitch> switches;           // by heel
};

// An angle in degrees, wrapped into (-180, 180].
double wrapDegrees(double a);
// The kind of a connection type, or nullopt when it isn't track.
std::optional<QString> trackKindOf(const QString& type, bool inCircuit, const TrackKindsTable& table = trackKinds());
// Pairs of connections with opposite, non-zero plugs (BlueBrick's circuits).
QVector<std::pair<int, int>> electricCircuits(const QVector<TrackConn>& conns);
// How much a train turns entering at `from` and leaving at `to` (degrees, +
// is to the right on the map, y down).
double turnThrough(const QVector<TrackConn>& conns, int from, int to);
// The ways through a part (`key`: its library key, part number and color).
PartTrack partTrack(const QString& key, const QVector<TrackConn>& conns, const TrackKindsTable& kinds = trackKinds(),
                    const TrackPathsTable& paths = trackPaths());

}  // namespace bld::track
