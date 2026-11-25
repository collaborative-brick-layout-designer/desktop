#pragma once

// Rolling stock and trains (trains design 2.3, 2.4): the layout's engines and
// cars, in the sidecar's `rollingStock`, and the trains made of them, in its
// `trains` (live, the web's meta.rollingStock and meta.trains, one Y.Map a
// record). Lengths are studs. A car rides on its trucks; a truck's `at` is
// studs from the car's middle, + towards its front. A train's position is
// its head car's front truck on the track, facing `to` (the rest of the
// train lies back towards `from`) or `from`. Fields this build doesn't know
// are kept. The web's packages/parts-catalog/src/track/trains.ts is the same.

#include "Route.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

#include <optional>
#include <vector>

namespace bld::core {

struct Truck {
    double at = 0.0;
    double swivelMax = 30.0;
    bool operator==(const Truck&) const = default;
};

struct Coupler {
    QString type = QStringLiteral("magnet");  // magnet, fixed or none
    double swivelMax = 37.0;
    bool operator==(const Coupler&) const = default;
};

struct RollingStock {
    QString id;
    QString number;  // road number, shown on the map and in lists
    QString name;
    QString kind = QStringLiteral("car");  // engine, car, cab, unit, monorail
    QString type;                          // free text: boxcar, hopper, steam...
    QString trackKind = QStringLiteral("rail");
    double length = 32.0;  // coupler pivot to coupler pivot
    double width = 6.0;
    double bodyLength = 30.0;
    std::vector<Truck> trucks{ { 8.0, 30.0 }, { -8.0, 30.0 } };
    Coupler front;
    Coupler rear;
    bool powered = false;
    QString lookMode = QStringLiteral("simple");  // simple, part or model
    QString color = QStringLiteral("#B40000");
    QString partKey;  // the part used as its top view (part mode)
    QString libraryId;  // the roster entry it was copied from
    int libraryVersion = 0;
    QString notes;
    // The cut or train on the track it stands in (Live Ops design 2.3, track/Cuts.h);
    // empty: none, unset: never moved. The layout's own, never a roster's.
    std::optional<QString> opsCut;
    QString opsLoad;  // loaded, empty, or none: it doesn't apply
    // Live Ops (track/Ops.h), the layout's own too; unset: never set. The
    // waybill in its pocket (when that names this car as well), the place it
    // goes back to, the place off the layout it is at, and set aside.
    std::optional<QString> opsWaybill;
    std::optional<QString> opsHome;
    std::optional<QString> opsAt;
    std::optional<bool> opsHold;
    // look's fields this build doesn't know (a picture...).
    QJsonObject lookExtras;
    // Fields this build doesn't know (owner, tags, model, maxSpeed...), written back as they were.
    QJsonObject extras;
    bool operator==(const RollingStock&) const = default;
};

// A quick car of `length` studs: trucks a quarter of the way in from each end.
inline RollingStock simpleStock(const QString& id, double length = 32.0) {
    RollingStock s;
    s.id = id;
    s.length = length;
    s.bodyLength = length - 2.0;
    const double at = length / 2.0 - length / 4.0;
    s.trucks = { { at, 30.0 }, { -at, 30.0 } };
    return s;
}

struct TrainPosition : TrackPosition {
    bool facingTo = true;  // facing `to`: the train lies back towards `from`
    bool operator==(const TrainPosition&) const = default;
};

struct TrainUnit {
    QString stock;
    bool reversed = false;
    bool operator==(const TrainUnit&) const = default;
};

struct Train {
    QString id;
    QString name;
    QString trackKind = QStringLiteral("rail");
    std::vector<TrainUnit> units;  // front to back
    std::optional<TrainPosition> position;  // none: not on the map
    QString color = QStringLiteral("#B40000");
    double speed = 40.0;
    bool cut = false;  // cars standing on the track, not a train someone made (track/Cuts.h)
    // How it stands over switches ("<brick>/<heel>" -> the exit its cars are on), however they are set now.
    std::optional<QMap<QString, int>> over;
    std::optional<double> emptiedAt;  // when its last car left it (ms); such a cut is kept a while
    QJsonObject extras;
    bool operator==(const Train&) const = default;
};

}  // namespace bld::core
