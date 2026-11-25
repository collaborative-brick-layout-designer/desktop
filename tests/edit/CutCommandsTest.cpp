// Cars on the track as undo steps (Live Ops design 2.3): an undo puts back
// only what nobody has changed since, and leaves every car standing in one
// place.

#include "edit/CutCommands.h"

#include <gtest/gtest.h>

#include <QUndoStack>

using namespace bld;

namespace {

core::Train cut(const QString& id, const QStringList& cars, double s) {
    core::Train t;
    t.id = id;
    t.cut = true;
    for (const QString& c : cars) t.units.push_back({ c, false });
    core::TrainPosition p;
    p.brick = QStringLiteral("M");
    p.to = 1;
    p.s = s;
    t.position = p;
    return t;
}

void layout(core::Map& map) {
    for (const char* id : { "a", "b", "c" }) map.sidecar.rollingStock.push_back(core::simpleStock(QString::fromLatin1(id), 12));
    for (auto& s : map.sidecar.rollingStock) s.opsCut = QStringLiteral("t1");
    map.sidecar.trains = { cut(QStringLiteral("t1"), { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") }, 30) };
}

track::CutChange split() {
    track::CutChange c;
    c.trains = { cut(QStringLiteral("n1"), { QStringLiteral("c") }, 4), cut(QStringLiteral("t1"), { QStringLiteral("a"), QStringLiteral("b") }, 30) };
    c.cars = { { QStringLiteral("a"), QStringLiteral("t1") }, { QStringLiteral("b"), QStringLiteral("t1") }, { QStringLiteral("c"), QStringLiteral("n1") } };
    return c;
}

QStringList standing(const core::Map& map) {
    QStringList out;
    for (const auto& t : track::standingTrains(map.sidecar)) {
        if (!t.position) continue;
        QStringList cars;
        for (const auto& u : t.units) cars << u.stock;
        out << cars.join(QLatin1Char(' '));
    }
    out.sort();
    return out;
}

}  // namespace

TEST(CutCommands, ASplitIsOneStepUndoneAndRedone) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    undo.push(new edit::CutCommand(map, split(), QStringLiteral("Move cars")));
    EXPECT_EQ(standing(map), (QStringList{ QStringLiteral("a b"), QStringLiteral("c") }));
    undo.undo();
    EXPECT_EQ(standing(map), QStringList{ QStringLiteral("a b c") });
    undo.redo();
    EXPECT_EQ(standing(map), (QStringList{ QStringLiteral("a b"), QStringLiteral("c") }));
}

TEST(CutCommands, AnUndoLeavesWhatSomeoneElseChangedSince) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    undo.push(new edit::CutCommand(map, split(), QStringLiteral("Move cars")));
    // Someone else moves b to the new cut, and loads c.
    for (auto& s : map.sidecar.rollingStock) {
        if (s.id == QLatin1String("b")) s.opsCut = QStringLiteral("n1");
        if (s.id == QLatin1String("c")) s.opsLoad = QStringLiteral("loaded");
    }
    for (auto& t : map.sidecar.trains) {
        if (t.id == QLatin1String("n1")) t.units = { { QStringLiteral("c"), false }, { QStringLiteral("b"), false } };
        if (t.id == QLatin1String("t1")) t.units = { { QStringLiteral("a"), false } };
    }
    undo.undo();
    // c is back, b stands where they put it, nothing is lost.
    EXPECT_EQ(standing(map), (QStringList{ QStringLiteral("a c"), QStringLiteral("b") }));
    EXPECT_EQ(map.sidecar.rollingStock[2].opsLoad, QStringLiteral("loaded"));
    undo.redo();
    EXPECT_EQ(standing(map).join(QLatin1Char(' ')).size(), 5);
}

TEST(CutCommands, ACutSomeoneDeletedIsNotBroughtBackByAnUndo) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    undo.push(new edit::CutCommand(map, split(), QStringLiteral("Move cars")));
    std::erase_if(map.sidecar.trains, [](const core::Train& t) { return t.id == QLatin1String("n1"); });
    undo.undo();
    EXPECT_EQ(standing(map), QStringList{ QStringLiteral("a b c") });
}

TEST(CutCommands, OldEmptyCutsGoUnlessACarNamesThem) {
    core::Map map;
    layout(map);
    for (const char* id : { "e1", "e2" }) {
        core::Train t = cut(QString::fromLatin1(id), {}, 2);
        t.emptiedAt = 1.0;
        map.sidecar.trains.push_back(t);
    }
    map.sidecar.rollingStock.push_back(core::simpleStock(QStringLiteral("d"), 12));
    map.sidecar.rollingStock.back().opsCut = QStringLiteral("e2");
    edit::CutCommand(map, split(), QString()).redo();
    EXPECT_TRUE(std::none_of(map.sidecar.trains.begin(), map.sidecar.trains.end(), [](const core::Train& t) { return t.id == QLatin1String("e1"); }));
    EXPECT_EQ(standing(map), (QStringList{ QStringLiteral("a b"), QStringLiteral("c"), QStringLiteral("d") }));
}

TEST(CutCommands, ALoadIsOneStep) {
    core::Map map;
    layout(map);
    QUndoStack undo;
    undo.push(new edit::SetLoadCommand(map, QStringLiteral("a"), QStringLiteral("empty"), QStringLiteral("Change load")));
    EXPECT_EQ(map.sidecar.rollingStock[0].opsLoad, QStringLiteral("empty"));
    undo.undo();
    EXPECT_EQ(map.sidecar.rollingStock[0].opsLoad, QString());
    EXPECT_EQ(undo.count(), 1);
    undo.push(new edit::SetLoadCommand(map, QStringLiteral("a"), QString(), QStringLiteral("Change load")));
    EXPECT_EQ(undo.count(), 0);
}
