// Cars on the track against the shared contract (fixtures/contracts/
// cuts.json, on route-search.json's network): the web's
// packages/parts-catalog/src/track/cuts.test.ts checks the same, so a drag
// does the same in both apps, and a layout two people changed at once
// reads the same.

#include "core/Sidecar.h"
#include "track/Cuts.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace bld;
using namespace bld::track;

namespace {

QJsonObject load(const char* name) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/") + QLatin1String(name));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

TrackNetwork network() {
    const QJsonObject d = load("route-search.json");
    const QJsonObject parts = d.value(QLatin1String("parts")).toObject();
    std::vector<TrackBrickInput> bricks;
    for (const auto& bv : d.value(QLatin1String("bricks")).toArray()) {
        const QJsonObject b = bv.toObject();
        TrackBrickInput in;
        in.id = b.value(QLatin1String("id")).toString();
        in.partKey = b.value(QLatin1String("part")).toString();
        for (const auto& cv : parts.value(in.partKey).toArray()) {
            const QJsonObject c = cv.toObject();
            in.conns.append({ c.value(QLatin1String("type")).toString(), c.value(QLatin1String("x")).toDouble(),
                              c.value(QLatin1String("y")).toDouble(), c.value(QLatin1String("angle")).toDouble(),
                              c.value(QLatin1String("plug")).toInt() });
        }
        for (const auto& pv : b.value(QLatin1String("points")).toArray())
            in.points.append(QPointF(pv.toArray()[0].toDouble(), pv.toArray()[1].toDouble()));
        for (const auto& cv : b.value(QLatin1String("connexions")).toArray())
            in.connexions.append({ cv.toObject().value(QLatin1String("id")).toString(), cv.toObject().value(QLatin1String("linkedTo")).toString() });
        bricks.push_back(in);
    }
    return TrackNetwork(bricks);
}

StockById stockOf(const QJsonObject& d) {
    StockById out;
    const QJsonObject s = d.value(QLatin1String("stock")).toObject();
    for (auto it = s.begin(); it != s.end(); ++it) {
        QJsonObject r = it.value().toObject();
        r.insert(QStringLiteral("id"), it.key());
        out.insert(it.key(), *saveload::rollingStockFromJson(r));
    }
    return out;
}

std::vector<core::Train> trainsOf(const QJsonObject& c) {
    std::vector<core::Train> out;
    for (const auto& t : c.value(QLatin1String("trains")).toArray()) out.push_back(*saveload::trainFromJson(t));
    return out;
}

QMap<QString, int> statesOf(const QJsonObject& c) { return saveload::switchStatesFromJson(c.value(QLatin1String("states"))); }

QJsonArray unitsText(const core::Train& t) {
    QJsonArray out;
    for (const auto& u : t.units) out.append((u.reversed ? QStringLiteral("-") : QString()) + u.stock);
    return out;
}

void sameChange(const CutChange& got, const QJsonObject& want) {
    const QJsonArray trains = want.value(QLatin1String("trains")).toArray();
    ASSERT_EQ(got.trains.size(), static_cast<size_t>(trains.size()));
    for (qsizetype i = 0; i < trains.size(); ++i) {
        const QJsonObject w = trains[i].toObject();
        const core::Train& t = got.trains[static_cast<size_t>(i)];
        EXPECT_EQ(t.id, w.value(QLatin1String("id")).toString());
        EXPECT_EQ(t.name, w.value(QLatin1String("name")).toString());
        EXPECT_EQ(t.cut, w.value(QLatin1String("cut")).toBool());
        EXPECT_EQ(unitsText(t), w.value(QLatin1String("units")).toArray());
        EXPECT_EQ(saveload::switchStatesToJson(t.over.value_or(QMap<QString, int>())), w.value(QLatin1String("over")).toObject());
        const QJsonObject p = w.value(QLatin1String("position")).toObject();
        ASSERT_EQ(t.position.has_value(), !p.isEmpty());
        if (!t.position) continue;
        EXPECT_EQ(t.position->brick, p.value(QLatin1String("brick")).toString());
        EXPECT_EQ(t.position->from, p.value(QLatin1String("from")).toInt());
        EXPECT_EQ(t.position->to, p.value(QLatin1String("to")).toInt());
        EXPECT_NEAR(t.position->s, p.value(QLatin1String("s")).toDouble(), 2e-3);
        EXPECT_EQ(t.position->facingTo, p.value(QLatin1String("facing")).toString() == QLatin1String("to"));
    }
    const QJsonObject cars = want.value(QLatin1String("cars")).toObject();
    EXPECT_EQ(QStringList(got.cars.keys()), cars.keys());
    for (auto it = got.cars.constBegin(); it != got.cars.constEnd(); ++it) EXPECT_EQ(it.value(), cars.value(it.key()).toString());
    EXPECT_EQ(QJsonArray::fromStringList(got.emptied), want.value(QLatin1String("emptied")).toArray());
}

}  // namespace

TEST(Cuts, WhatPeopleChangedAtOnceReadsAsTheContractSays) {
    const QJsonObject d = load("cuts.json");
    for (const auto& cv : d.value(QLatin1String("settle")).toArray()) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        StockById stock = stockOf(d);
        const QJsonObject cars = c.value(QLatin1String("cars")).toObject();
        for (auto it = cars.begin(); it != cars.end(); ++it) stock[it.key()].opsCut = it.value().toString();
        const auto out = settleTrains(trainsOf(c), stock);
        const QJsonObject want = c.value(QLatin1String("expect")).toObject();
        for (const auto& t : out) EXPECT_EQ(unitsText(t), want.value(t.id).toArray()) << t.id.toStdString();
        EXPECT_EQ(settleTrains(out, stock), out);
    }
}

TEST(Cuts, DragsDoAsTheContractSays) {
    const QJsonObject d = load("cuts.json");
    const TrackNetwork net = network();
    const StockById stock = stockOf(d);
    const QJsonArray moves = d.value(QLatin1String("moves")).toArray();
    ASSERT_GE(moves.size(), 20);
    for (const auto& cv : moves) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        const QJsonArray to = c.value(QLatin1String("to")).toArray();
        const auto drag = startDrag(net, trainsOf(c), stock, statesOf(c), c.value(QLatin1String("train")).toString(), c.value(QLatin1String("unit")).toInt(), QStringLiteral("new"));
        const auto m = drag ? drag->to(QPointF(to[0].toDouble(), to[1].toDouble())) : std::nullopt;
        const QJsonValue wantV = c.value(QLatin1String("expect"));
        ASSERT_EQ(m.has_value(), wantV.isObject());
        if (!m) continue;
        const QJsonObject want = wantV.toObject();
        EXPECT_EQ(QJsonArray::fromStringList(m->moved), want.value(QLatin1String("moved")).toArray());
        EXPECT_NEAR(m->by, want.value(QLatin1String("by")).toDouble(), 2e-3);
        EXPECT_EQ(m->couples, want.value(QLatin1String("couples")).toString());
        EXPECT_EQ(m->blockedBy, want.value(QLatin1String("blockedBy")).toString());
        EXPECT_EQ(m->why, want.value(QLatin1String("why")).toString());
        sameChange(m->change, want.value(QLatin1String("change")).toObject());
    }
}

TEST(Cuts, TakingACarOutLeavesTheOthersWhereTheyStand) {
    const QJsonObject d = load("cuts.json");
    const TrackNetwork net = network();
    for (const auto& cv : d.value(QLatin1String("takeOuts")).toArray()) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        sameChange(takeOut(net, trainsOf(c), stockOf(d), {}, c.value(QLatin1String("car")).toString(), QStringLiteral("new")), c.value(QLatin1String("expect")).toObject());
    }
}

TEST(Cuts, TheLimitsAreTheContracts) {
    const QJsonObject l = load("cuts.json").value(QLatin1String("limits")).toObject();
    EXPECT_EQ(kMaxUnits, l.value(QLatin1String("units")).toInt());
    EXPECT_EQ(kMaxRecords, l.value(QLatin1String("records")).toInt());
}

TEST(Cuts, ASwitchFlippedUnderAStandingCutDoesNotMoveIt) {
    core::Train t;
    t.id = QStringLiteral("t");
    t.cut = true;
    t.units = { { QStringLiteral("a"), false }, { QStringLiteral("b"), false } };
    core::TrainPosition p;
    p.brick = QStringLiteral("M");
    p.from = 0;
    p.to = 1;
    p.s = 4;
    t.position = p;
    const TrackNetwork net = network();
    const StockById stock = stockOf(load("cuts.json"));
    t.over = standOver(net, t, stock, { { QStringLiteral("P/0"), 1 } });
    const auto a = placeTrain(net, t, stock, { { QStringLiteral("P/0"), 1 } });
    const auto b = placeTrain(net, t, stock, { { QStringLiteral("P/0"), 2 } });
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->units.back().centre, b->units.back().centre);
    // Turned round, it stands over them still.
    EXPECT_TRUE(turnStanding(net, t, stock, {}).over.has_value());
}

TEST(Cuts, ReadAndWrittenAsTheWebDoesAndRenamedWithTheirTrack) {
    const QJsonObject car{ { QStringLiteral("id"), QStringLiteral("c") }, { QStringLiteral("opsCut"), QStringLiteral("t") }, { QStringLiteral("opsLoad"), QStringLiteral("loaded") }, { QStringLiteral("later"), 1 } };
    const auto s = saveload::rollingStockFromJson(car);
    ASSERT_TRUE(s);
    const QJsonObject back = saveload::rollingStockToJson(*s);
    for (const char* k : { "opsCut", "opsLoad", "later" }) EXPECT_EQ(back.value(QLatin1String(k)), car.value(QLatin1String(k))) << k;
    // Taken off the track ("none"), it says so still; never on it, it has no key.
    QJsonObject off = car;
    off.insert(QStringLiteral("opsCut"), QString());
    EXPECT_TRUE(saveload::rollingStockToJson(*saveload::rollingStockFromJson(off)).value(QLatin1String("opsCut")).isString());
    EXPECT_FALSE(saveload::rollingStockToJson(core::simpleStock(QStringLiteral("x"))).contains(QLatin1String("opsCut")));
    const QJsonObject train{ { QStringLiteral("id"), QStringLiteral("t") }, { QStringLiteral("cut"), true }, { QStringLiteral("emptiedAt"), 1.7e12 },
                             { QStringLiteral("over"), QJsonObject{ { QStringLiteral("P/0"), 2 } } },
                             { QStringLiteral("position"), QJsonObject{ { QStringLiteral("brick"), QStringLiteral("P") }, { QStringLiteral("from"), 0 }, { QStringLiteral("to"), 1 }, { QStringLiteral("s"), 2 } } } };
    const auto t = saveload::trainFromJson(train);
    ASSERT_TRUE(t);
    for (const char* k : { "cut", "emptiedAt", "over" }) EXPECT_EQ(saveload::trainToJson(*t).value(QLatin1String(k)), train.value(QLatin1String(k))) << k;
    core::Sidecar sc;
    sc.trains = { *t };
    saveload::renameSidecarIds(sc, { { QStringLiteral("P"), QStringLiteral("P2") } });
    EXPECT_EQ(sc.trains[0].position->brick, QStringLiteral("P2"));
    EXPECT_EQ(sc.trains[0].over->value(QStringLiteral("P2/0")), 2);
}
