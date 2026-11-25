#pragma once

// Live Ops worked out for one layout (Live Ops design 3): every place by the
// name that tells it apart, the waybill in each car's pocket, where each car
// is and goes, and the routing. The web's apps/web/src/editor/ops.ts
// (opsView) is the same.

#include "Cuts.h"
#include "Ops.h"
#include "PlacesOnTrack.h"

#include "../core/Map.h"

namespace bld::track {

struct NamedPlace {
    QString id;
    QString name;    // with its module's when two have the same
    QJsonObject place;
    std::optional<int> capacity;  // cars all its spots hold
};

struct OpsView {
    std::vector<NamedPlace> places;  // by name
    QMap<QString, Waybill> waybills;
    std::vector<Job> jobs;  // in order
    std::vector<core::Train> trains;
    StockById stock;
    QMap<QString, QString> pockets;  // car -> the waybill in its pocket
    QMap<QString, SpotRef> at;
    QMap<QString, Destination> to;
    QMap<QString, QPointF> points;  // where each car on the track is, studs
    QSet<QString> onTrack;
    std::vector<OpsPlace> opsPlaces;
    OpsRouting routing;

    const NamedPlace* place(const QString& id) const;
    const Job* job(const QString& id) const;
};

// `moduleName`: what a module with no name is called, `noName`: a place with none.
OpsView opsView(const core::Map& map, const TrackNetwork& net, const std::vector<TrackRun>& runs, const QString& moduleName = QStringLiteral("Module"),
                const QString& noName = QStringLiteral("(no name)"));

// What Make waybills would make from a seed: for every car without one.
MadeWaybills previewWaybills(const OpsView& view, quint32 seed);

}  // namespace bld::track
