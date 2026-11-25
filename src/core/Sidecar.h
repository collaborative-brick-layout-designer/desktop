#pragma once

#include "AnchoredLabel.h"
#include "Module.h"
#include "Route.h"
#include "Train.h"
#include "SavedView.h"
#include "Venue.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

namespace bld::core {

// A custom set placed in the layout (a named group of the .bbm): which
// version of the set it came from (parts::setVersion), so the layout can say
// "Update available" when the set changes (edit/SetUpdates.h). Vanilla
// BlueBrick never sees it; the web writes the same (`sets`, PlacedSet in
// packages/bbm sidecar.ts).
struct PlacedSet {
    QString groupId; // the set's outermost group
    QString version;
    // One of its parts, and where that part sits in the set (studs,
    // degrees), so the set's place and turn can be found however it was
    // moved; empty `anchorBrick` when not known.
    QString anchorBrick;
    QPointF anchorPos;
    double anchorAngle = 0.0;
    QJsonObject extras; // fields a newer build added, kept as they are
};

// Root container for fork-only metadata, serialized to a .bbm.bld sidecar
// beside the .bbm. Vanilla BlueBrick ignores this file; our writer always
// writes both atomically on save.
struct Sidecar {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;

    // SHA-256 of the sibling .bbm bytes at the time of sidecar write. On load,
    // we compare against the current .bbm; drift (e.g. vanilla edited it in
    // between) is flagged so the UI can offer re-link by proximity.
    QByteArray bbmContentHashSha256;

    std::vector<AnchoredLabel> anchoredLabels;
    std::vector<Module>        modules;
    std::optional<Venue>       venue;

    // Optional raster background image painted underneath everything else.
    // Path is absolute; opacity is 0..1. BLD-only feature (vanilla
    // BlueBrick has no background-image support); read/written via the
    // sidecar so we don't disturb .bbm bytes.
    QString backgroundImagePath;
    QRectF  backgroundImageRectStuds;  // null = stretch to scene bounds
    double  backgroundImageOpacity = 0.5;

    // Saved views, in the order the Views panel shows them.
    std::vector<SavedView> views;

    // Custom sets placed in the layout, with the version each came from.
    std::vector<PlacedSet> sets;
    // The layout's budget: how many of each part (by part number, upper
    // case) it may use. Set, even empty, when the layout has a budget;
    // unset when it has none. Shared with everyone who edits the layout
    // (the web keeps it at meta.budgetLimits); .bbb files import and
    // export it.
    std::optional<QMap<QString, int>> budgetLimits;
    // How the layout's switches are set (trains design 2.2): "<brick id>/<heel
    // connection>" to the connection a train leaves by. A switch with no key
    // is set to its default, the way that turns least. The web keeps it at
    // meta.switchStates. Never in a .bbm.
    QMap<QString, int> switchStates;
    // Named routes (trains design 2.5), in the order the layout lists them.
    // The web keeps them at meta.routes. Never in a .bbm.
    std::vector<Route> routes;
    // Rolling stock and trains (trains design 2.3, 2.4), in id order. The
    // web keeps them at meta.rollingStock and meta.trains. Never in a .bbm.
    std::vector<RollingStock> rollingStock;
    std::vector<Train> trains;
    // The scale trains run at (trains design 5): 1:trainScale, a whole number
    // from 10 to 220; unset means 1:38 (LEGO track gauge). The web keeps it at
    // meta.trainScale. Never in a .bbm.
    std::optional<int> trainScale;
    // The layout's own places, on track that is in no module (core/Places.h);
    // a module's are its own. The web keeps them at meta.places. Never in a .bbm.
    QJsonArray layoutPlaces;
    // Waybills and jobs (Live Ops design 2.4, 2.5; track/Ops.h reads them), as
    // they are written, in id order. The web keeps them at meta.waybills and
    // meta.opsJobs. Never in a .bbm.
    QJsonArray waybills;
    QJsonArray opsJobs;
    // Top-level fields this build doesn't know (a newer build's, the web's),
    // written back as they were.
    QJsonObject extras;
    // Saved by a newer build than this one (schemaVersion above ours).
    bool fromNewerVersion() const { return schemaVersion > kSchemaVersion; }

    bool isEmpty() const {
        return anchoredLabels.empty() && modules.empty() && !venue.has_value()
               && backgroundImagePath.isEmpty() && views.empty() && sets.empty() && !budgetLimits.has_value()
               && switchStates.isEmpty() && routes.empty() && rollingStock.empty() && trains.empty() && !trainScale.has_value() && layoutPlaces.isEmpty() && waybills.isEmpty() && opsJobs.isEmpty() && extras.isEmpty();
    }
};

}
