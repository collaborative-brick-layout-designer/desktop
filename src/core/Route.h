#pragma once

// A named route (trains design 2.5): a way along the track the layout keeps,
// in the sidecar's `routes` (live, the web's meta.routes, one Y.Map a
// route). The way is `steps`, exactly as chosen, so two routes of equal
// length never swap; `from`, `to` and `via` are what was clicked, used to
// find a new way when the track under it changes. Whether it is broken is
// worked out (track/Routes.h), never stored. The web's
// packages/parts-catalog/src/track/routes.ts `Route` is the same record.

#include <QJsonObject>
#include <QString>

#include <vector>

namespace bld::core {

// A place on the track: on the way through `brick` from connection `from`
// to `to`, `s` studs from `from`.
struct TrackPosition {
    QString brick;
    int from = 0;
    int to = 1;
    double s = 0.0;
    bool operator==(const TrackPosition&) const = default;
};

// One way through one brick, in the direction the route takes it.
struct RouteStep {
    QString brick;
    int from = 0;
    int to = 1;
    bool operator==(const RouteStep&) const = default;
};

// A stop on the way; `reverse` (the train turns round there) is kept for later.
struct RouteVia {
    TrackPosition at;
    bool reverse = false;
    bool operator==(const RouteVia&) const = default;
};

struct Route {
    QString id;
    QString name;
    QString train;  // empty: none (trains come in a later phase)
    TrackPosition from;
    TrackPosition to;
    std::vector<RouteVia> via;
    std::vector<RouteStep> steps;
    QString color = QStringLiteral("#3B82F6");
    // Fields this build doesn't know, written back as they were.
    QJsonObject extras;
    bool operator==(const Route&) const = default;
};

}  // namespace bld::core
