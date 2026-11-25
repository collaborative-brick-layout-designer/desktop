// Waybills, jobs and a car's Live Ops fields as undo steps (Live Ops design
// 2.2, 2.4, 2.5): an undo puts back only what nobody has changed since.

#include "edit/OpsCommands.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QUndoStack>

using namespace bld;

namespace {

const QString kA = QStringLiteral("a");
const QString kB = QStringLiteral("b");
const QString kW = QStringLiteral("w");
const QString kMill = QStringLiteral("mill");

void layout(core::Map& map) {
    for (const QString& id : { kA, kB }) map.sidecar.rollingStock.push_back(core::simpleStock(id, 12));
}
track::WaybillLeg leg(const QString& to, const QString& load) { return { to, QString(), load, QString() }; }
core::RollingStock& car(core::Map& map, const QString& id) {
    for (auto& s : map.sidecar.rollingStock)
        if (s.id == id) return s;
    return map.sidecar.rollingStock.front();
}
track::Waybill waybill(const core::Map& map, qsizetype i = 0) { return track::asWaybill(map.sidecar.waybills.at(i).toObject()); }
void push(QUndoStack& undo, core::Map& map, const edit::OpsChange& c) { undo.push(new edit::OpsCommand(map, c, QStringLiteral("Step"))); }

}  // namespace

TEST(OpsCommands, AWaybillIsGivenTurnedAndDeletedEachUndone) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    push(undo, map, edit::newWaybill(map, kW, kA, { leg(kMill, QStringLiteral("loaded")), leg(QStringLiteral("world"), QStringLiteral("empty")) }));
    EXPECT_EQ(edit::pocketOf(map, kA), kW);
    EXPECT_EQ(car(map, kA).opsLoad, QStringLiteral("loaded"));
    push(undo, map, edit::turnedCar(map, kA));
    EXPECT_EQ(waybill(map).leg, 2);
    EXPECT_EQ(car(map, kA).opsLoad, QStringLiteral("empty"));
    push(undo, map, edit::turnedCar(map, kA, QStringLiteral("world")));
    EXPECT_EQ(waybill(map).leg, 0);
    EXPECT_EQ(car(map, kA).opsAt, QStringLiteral("world"));
    EXPECT_TRUE(edit::turnedCar(map, kA).isEmpty());
    push(undo, map, edit::restartedWaybill(map, kW));
    EXPECT_EQ(waybill(map).leg, 1);
    push(undo, map, edit::givenWaybill(map, kW, kB));
    EXPECT_EQ(edit::pocketOf(map, kB), kW);
    EXPECT_TRUE(edit::pocketOf(map, kA).isEmpty());
    push(undo, map, edit::removedWaybill(map, kW));
    EXPECT_TRUE(map.sidecar.waybills.isEmpty());
    EXPECT_EQ(car(map, kB).opsWaybill, QString());
    while (undo.canUndo()) undo.undo();
    EXPECT_TRUE(map.sidecar.waybills.isEmpty());
    EXPECT_FALSE(car(map, kA).opsWaybill.has_value());
    EXPECT_FALSE(car(map, kA).opsAt.has_value());
    while (undo.canRedo()) undo.redo();
    EXPECT_TRUE(map.sidecar.waybills.isEmpty());
}

TEST(OpsCommands, GivingACarAnotherWaybillLeavesTheFirstWaiting) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    push(undo, map, edit::newWaybill(map, kW, kA));
    push(undo, map, edit::newWaybill(map, QStringLiteral("w2"), QString()));
    push(undo, map, edit::givenWaybill(map, QStringLiteral("w2"), kA));
    EXPECT_EQ(edit::pocketOf(map, kA), QStringLiteral("w2"));
    EXPECT_TRUE(waybill(map, 0).car.isEmpty());
    EXPECT_EQ(map.sidecar.waybills.size(), 2);
}

TEST(OpsCommands, AnUndoLeavesWhatSomeoneElseChangedSince) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    push(undo, map, edit::newWaybill(map, kW, kA));
    push(undo, map, edit::changedWaybill(map, kW, { { QStringLiteral("to1"), kMill }, { QStringLiteral("note1"), QStringLiteral("x") } }));
    // Someone else changes the note, and the car's home.
    QJsonObject r = map.sidecar.waybills.at(0).toObject();
    r.insert(QStringLiteral("note1"), QStringLiteral("theirs"));
    map.sidecar.waybills.replace(0, r);
    undo.undo();
    EXPECT_TRUE(waybill(map).legs[0] == std::nullopt);
    EXPECT_EQ(map.sidecar.waybills.at(0).toObject().value(QLatin1String("note1")).toString(), QStringLiteral("theirs"));
    undo.redo();
    edit::CarOps ops = edit::carOps(car(map, kA));
    ops.hold = true;
    push(undo, map, edit::changedCar(map, kA, ops));
    car(map, kA).opsHold = false;
    car(map, kA).opsHome = kMill;
    undo.undo();
    EXPECT_EQ(car(map, kA).opsHold, false);
    EXPECT_EQ(car(map, kA).opsHome, kMill);
    // Deleted by them: an undo doesn't bring a part of it back.
    map.sidecar.waybills = QJsonArray();
    undo.undo();
    EXPECT_TRUE(map.sidecar.waybills.isEmpty());
}

TEST(OpsCommands, JobsAndStopsMoveAnyNumberOfTimes) {
    core::Map map;
    QUndoStack undo;
    const QString j1 = QStringLiteral("j1");
    for (const char* id : { "j1", "j2", "j3" }) push(undo, map, edit::newJob(map, QLatin1String(id), QStringLiteral("Job %1")));
    EXPECT_EQ(edit::jobsOf(map).at(2).name, QStringLiteral("Job 3"));
    for (int i = 0; i < 130; ++i) push(undo, map, edit::movedJob(map, edit::jobsOf(map).at(2).id, -1));
    EXPECT_EQ(edit::jobsOf(map).size(), 3u);
    const QString first = edit::jobsOf(map).front().id;
    EXPECT_TRUE(edit::movedJob(map, first, -1).isEmpty());
    push(undo, map, edit::movedJob(map, first, 1));
    EXPECT_EQ(edit::jobsOf(map).at(1).id, first);
    for (const char* p : { "cannery", "mill", "world" }) push(undo, map, edit::addedStop(map, j1, QLatin1String(p)));
    const auto stops = [&] {
        QStringList out;
        for (const auto& j : edit::jobsOf(map))
            if (j.id == j1)
                for (const auto& s : j.stops) out << s.place;
        return out;
    };
    const auto stopAt = [&](size_t i) {
        for (const auto& j : edit::jobsOf(map))
            if (j.id == j1) return j.stops.at(i).id;
        return QString();
    };
    push(undo, map, edit::movedStop(map, j1, stopAt(2), -1));
    EXPECT_EQ(stops(), (QStringList{ QStringLiteral("cannery"), QStringLiteral("world"), QStringLiteral("mill") }));
    push(undo, map, edit::movedStop(map, j1, stopAt(0), 1));
    push(undo, map, edit::removedStop(map, j1, stopAt(1)));
    EXPECT_EQ(stops(), (QStringList{ QStringLiteral("world"), QStringLiteral("mill") }));
    undo.undo();
    EXPECT_EQ(stops().size(), 3);
    push(undo, map, edit::changedJob(map, j1, { { QStringLiteral("name"), QStringLiteral("Turn") } }));
    push(undo, map, edit::removedJob(j1));
    EXPECT_EQ(edit::jobsOf(map).size(), 2u);
    undo.undo();
    EXPECT_EQ(stops().size(), 3);
    for (int i = 0; i < track::kMaxStops + 5; ++i) push(undo, map, edit::addedStop(map, j1, kMill));
    EXPECT_EQ(stops().size(), track::kMaxStops);
}

TEST(OpsCommands, MadeWaybillsSkipCarsGivenOneSinceAndReplaceFinishedOnes) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    push(undo, map, edit::newWaybill(map, kW, kA, { leg(kMill, QStringLiteral("loaded")) }));
    push(undo, map, edit::turnedCar(map, kA));
    track::MadeWaybills made;
    made.made = { { kA, kMill, { leg(kMill, QStringLiteral("loaded")) } }, { kB, kMill, { leg(kMill, QStringLiteral("empty")) } } };
    push(undo, map, edit::newWaybill(map, QStringLiteral("w9"), kB, { leg(kMill, QString()) }));
    push(undo, map, edit::madeWaybills(map, made));
    EXPECT_EQ(map.sidecar.waybills.size(), 2);
    EXPECT_NE(edit::pocketOf(map, kA), kW);
    EXPECT_FALSE(edit::pocketOf(map, kA).isEmpty());
    EXPECT_EQ(edit::pocketOf(map, kB), QStringLiteral("w9"));
    undo.undo();
    EXPECT_EQ(edit::pocketOf(map, kA), kW);
}

TEST(OpsCommands, WhatNamesAPlaceIsCounted) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    push(undo, map, edit::newWaybill(map, kW, kA, { leg(kMill, QString()) }));
    push(undo, map, edit::newJob(map, QStringLiteral("j"), QStringLiteral("Job %1")));
    push(undo, map, edit::addedStop(map, QStringLiteral("j"), kMill));
    edit::CarOps ops;
    ops.home = kMill;
    push(undo, map, edit::changedCar(map, kB, ops));
    const auto u = edit::usesOf(map, { kMill });
    EXPECT_EQ(u.waybills, 1);
    EXPECT_EQ(u.jobs, 1);
    EXPECT_EQ(u.homes, 1);
    EXPECT_EQ(edit::usesOf(map, { QStringLiteral("other") }).jobs, 0);
}
