#pragma once

// Waybills, jobs and a car's Live Ops fields (Live Ops design 2.2, 2.4, 2.5;
// track/Ops.h): what the Live Ops panel does, each one undo step. An undo
// puts back only the fields nobody has changed since. The web's
// apps/web/src/editor/ops.ts does the same.

#include "../core/Map.h"
#include "../track/Ops.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QUndoCommand>

#include <optional>
#include <vector>

namespace bld::edit {

// What a car does in this layout.
struct CarOps {
    std::optional<QString> waybill, home, at;
    std::optional<bool> hold;
    QString load;
    bool operator==(const CarOps&) const = default;
};
CarOps carOps(const core::RollingStock& s);

// The records and cars a step changes, as they are to be.
struct OpsChange {
    struct Record {
        bool job = false;  // else a waybill
        QString id;
        std::optional<QJsonObject> after;  // none: deleted
    };
    std::vector<Record> records;
    QMap<QString, CarOps> cars;
    bool isEmpty() const { return records.empty() && cars.isEmpty(); }
};

class OpsCommand : public QUndoCommand {
public:
    OpsCommand(core::Map& map, const OpsChange& change, const QString& text, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    struct RecordStep {
        bool job;
        QString id;
        std::optional<QJsonObject> before, after;
    };
    struct CarStep {
        QString id;
        CarOps before, after;
    };
    void apply(bool forward, bool force);
    core::Map& map_;
    std::vector<RecordStep> records_;
    std::vector<CarStep> cars_;
    bool done_ = false;
};

// ---- what each control does (an empty change: nothing)

std::vector<track::Job> jobsOf(const core::Map& map);
// A new waybill with this id, for a car or (empty) for none.
OpsChange newWaybill(const core::Map& map, const QString& id, const QString& car, const std::vector<track::WaybillLeg>& legs = {}, const QString& from = {});
OpsChange changedWaybill(const core::Map& map, const QString& id, const QJsonObject& fields);
// Put a waybill in a car's pocket (empty: it waits for one); the one that was there waits for a car.
OpsChange givenWaybill(const core::Map& map, const QString& id, const QString& car);
OpsChange removedWaybill(const core::Map& map, const QString& id);
// Delivered: the car's waybill turns to its next leg; `offTo`: it left the layout, for this place.
OpsChange turnedCar(const core::Map& map, const QString& car, const QString& offTo = {});
OpsChange restartedWaybill(const core::Map& map, const QString& id);
OpsChange changedCar(const core::Map& map, const QString& car, const CarOps& after);
// What Make waybills made, for the cars still without one (as many as there is room for).
OpsChange madeWaybills(const core::Map& map, const track::MadeWaybills& made);
// `named`: "Job %1".
OpsChange newJob(const core::Map& map, const QString& id, const QString& named);
OpsChange changedJob(const core::Map& map, const QString& id, const QJsonObject& fields);
OpsChange removedJob(const QString& id);
OpsChange movedJob(const core::Map& map, const QString& id, int d);
OpsChange addedStop(const core::Map& map, const QString& job, const QString& place);
OpsChange movedStop(const core::Map& map, const QString& job, const QString& stop, int d);
OpsChange removedStop(const core::Map& map, const QString& job, const QString& stop);

// What names these places: waybills, jobs, and cars' homes.
struct PlaceUses {
    int waybills = 0;
    int jobs = 0;
    int homes = 0;
};
PlaceUses usesOf(const core::Map& map, const QStringList& places);
// The waybill in a car's pocket (its id), or none.
QString pocketOf(const core::Map& map, const QString& car);

}  // namespace bld::edit
