#pragma once

#include "ColorSpec.h"

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace bld::core {

enum class EdgeKind {
    Wall,
    Door,
    Open,
};

// Venue model v2 (the web repo's references/VENUE-MODEL.md, shared with the
// web editor): studs, x east, y south, angles clockwise from east. Optional
// parts are written only when set; `extras` keeps JSON fields this build
// doesn't know, so an older build doesn't lose what a newer one wrote.

struct VenueEdge {
    QVector<QPointF> polyline; // world coords, in studs
    EdgeKind kind = EdgeKind::Wall;
    double   doorWidthStuds = 0.0;    // only meaningful when kind == Door
    QString  label;
    bool estimated = false; // not measured yet
    QJsonObject extras{};
    // Change detection (SceneBuilder::update redraws only what differs).
    bool operator==(const VenueEdge&) const = default;
};

enum class ObstacleKind { Other, Column, Stairs, Elevator, Counter, Railing };

struct VenueObstacle {
    QVector<QPointF> polygon;          // closed polygon in world studs
    QString label;
    ObstacleKind kind = ObstacleKind::Other;
    std::optional<double> upDegrees{}; // stairs: the way up
    QJsonObject extras{};
    // Change detection (SceneBuilder::update redraws only what differs).
    bool operator==(const VenueObstacle&) const = default;
};

struct VenuePower {
    QPointF pos;
    bool floor = false; // a floor outlet, else on a wall
    QString label;
    double amps = 0.0; // 0 = not given
    double volts = 0.0;
    QJsonObject extras{};
    // Change detection (SceneBuilder::update redraws only what differs).
    bool operator==(const VenuePower&) const = default;
};

struct VenueNote {
    QPointF pos;
    QString text;
    bool estimated = false;
    QJsonObject extras{};
    // Change detection (SceneBuilder::update redraws only what differs).
    bool operator==(const VenueNote&) const = default;
};

struct VenueDimension {
    QPointF from, to;
    QString label;
    bool estimated = false;
    QJsonObject extras{};
    // Change detection (SceneBuilder::update redraws only what differs).
    bool operator==(const VenueDimension&) const = default;
};

// The unit a venue's lengths are shown in: the Venue Designer's Units pick,
// kept with the venue (`lengthUnit`, references/VENUE-MODEL.md) so its wall
// lengths read the same wherever it is drawn. Stored lengths stay in studs.
enum class VenueLengthUnit { FeetInches, Metres, Studs };

// Per-project venue definition. At most one venue per project.
struct Venue {
    QString name;
    QVector<VenueEdge>     edges;
    QVector<VenueObstacle> obstacles;
    QVector<VenuePower> power;
    QVector<VenueNote> notes;
    QVector<VenueDimension> dimensions;
    double minWalkwayStuds = 112.5;   // ~900 mm @ 8 studs/mm
    QRectF layoutBoundsStuds;          // optional reserved layout footprint
    bool   enabled = true;
    VenueLengthUnit lengthUnit = VenueLengthUnit::FeetInches;
    QJsonObject extras{};
    // Change detection (SceneBuilder::update redraws only what differs).
    bool operator==(const Venue&) const = default;
};
}
