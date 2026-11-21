// Train placement against the shared contract (fixtures/contracts/
// train-geometry.json, on route-search.json's network): the web's
// packages/parts-catalog/src/track/trains.test.ts checks the same, so a
// train sits the same way in both apps. And the records: reading back,
// unknown fields kept, a part's length from its couplers.

#include "track/Trains.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

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

core::Train trainOf(const QJsonObject& c) {
    QJsonObject t{ { QStringLiteral("id"), QStringLiteral("t") }, { QStringLiteral("units"), c.value(QLatin1String("units")) },
                   { QStringLiteral("position"), c.value(QLatin1String("position")) } };
    return *saveload::trainFromJson(t);
}

QMap<QString, int> statesOf(const QJsonObject& c) {
    QMap<QString, int> out;
    const QJsonObject s = c.value(QLatin1String("states")).toObject();
    for (auto it = s.begin(); it != s.end(); ++it) out.insert(it.key(), it.value().toInt());
    return out;
}

void near(QPointF got, const QJsonValue& want) {
    EXPECT_NEAR(got.x(), want.toArray()[0].toDouble(), 1e-4);
    EXPECT_NEAR(got.y(), want.toArray()[1].toDouble(), 1e-4);
}

}  // namespace

TEST(TrainPlacement, MatchesTheContract) {
    const QJsonObject d = load("train-geometry.json");
    const TrackNetwork net = network();
    const StockById stock = stockOf(d);
    const QJsonArray cases = d.value(QLatin1String("cases")).toArray();
    ASSERT_GE(cases.size(), 3);
    for (const auto& cv : cases) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        const core::Train train = trainOf(c);
        const QMap<QString, int> states = statesOf(c);
        const auto placed = placeTrain(net, train, stock, states);
        ASSERT_TRUE(placed.has_value());
        const QJsonObject want = c.value(QLatin1String("expect")).toObject();
        EXPECT_EQ(placed->offTrack, want.value(QLatin1String("offTrack")).toBool());
        const QJsonArray units = want.value(QLatin1String("units")).toArray();
        ASSERT_EQ(placed->units.size(), static_cast<size_t>(units.size()));
        for (qsizetype i = 0; i < units.size(); ++i) {
            const QJsonObject w = units[i].toObject();
            const PlacedUnit& u = placed->units[static_cast<size_t>(i)];
            EXPECT_EQ(u.stock, w.value(QLatin1String("stock")).toString());
            near(u.frontTruck, w.value(QLatin1String("frontTruck")));
            near(u.rearTruck, w.value(QLatin1String("rearTruck")));
            near(u.centre, w.value(QLatin1String("centre")));
            near(u.frontCoupler, w.value(QLatin1String("frontCoupler")));
            near(u.rearCoupler, w.value(QLatin1String("rearCoupler")));
            EXPECT_NEAR(u.angle, w.value(QLatin1String("angle")).toDouble(), 1e-3);
        }
        const core::Train t = turnedAround(net, train, stock, states);
        ASSERT_TRUE(t.position.has_value());
        const QJsonObject tw = want.value(QLatin1String("turned")).toObject();
        EXPECT_EQ(t.position->brick, tw.value(QLatin1String("brick")).toString());
        EXPECT_EQ(t.position->from, tw.value(QLatin1String("from")).toInt());
        EXPECT_EQ(t.position->to, tw.value(QLatin1String("to")).toInt());
        EXPECT_NEAR(t.position->s, tw.value(QLatin1String("s")).toDouble(), 1e-3);
        EXPECT_EQ(t.position->facingTo, tw.value(QLatin1String("facing")).toString() == QLatin1String("to"));
        // Turned round: the units in reverse order, each the other way.
        ASSERT_EQ(t.units.size(), train.units.size());
        if (!t.units.empty()) {
            EXPECT_EQ(t.units.front().stock, train.units.back().stock);
            EXPECT_NE(t.units.front().reversed, train.units.back().reversed);
        }
    }
}

TEST(TrainPlacement, TrucksStayTheirSpacingApart) {
    const QJsonObject d = load("train-geometry.json");
    const TrackNetwork net = network();
    const StockById stock = stockOf(d);
    const QJsonObject c = d.value(QLatin1String("cases")).toArray()[1].toObject();
    const auto placed = placeTrain(net, trainOf(c), stock, statesOf(c));
    ASSERT_TRUE(placed.has_value());
    for (const auto& u : placed->units) {
        const auto& s = stock.value(u.stock);
        EXPECT_NEAR(std::hypot(u.frontTruck.x() - u.rearTruck.x(), u.frontTruck.y() - u.rearTruck.y()),
                    s.trucks[0].at - s.trucks[1].at, 1e-4);
    }
    // Not on the map: nowhere.
    core::Train off = trainOf(c);
    off.position.reset();
    EXPECT_FALSE(placeTrain(net, off, stock).has_value());
    EXPECT_DOUBLE_EQ(trainLength(trainOf(c), stock), 32 + 24 + 32);
}

TEST(TrainRecords, ReadBackWithDefaultsAndKeepUnknownFields) {
    const QJsonObject web{ { QStringLiteral("id"), QStringLiteral("s1") },
                           { QStringLiteral("number"), QStringLiteral("4021") },
                           { QStringLiteral("length"), 28 },
                           { QStringLiteral("look"), QJsonObject{ { QStringLiteral("mode"), QStringLiteral("part") }, { QStringLiteral("partKey"), QStringLiteral("x.1") }, { QStringLiteral("picture"), QStringLiteral("p.png") } } },
                           { QStringLiteral("tags"), QJsonArray{ QStringLiteral("freight") } },
                           { QStringLiteral("libraryId"), QStringLiteral("L") },
                           { QStringLiteral("libraryVersion"), 3 } };
    const auto s = saveload::rollingStockFromJson(web);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->number, QStringLiteral("4021"));
    EXPECT_DOUBLE_EQ(s->length, 28);
    // No trucks given: a quarter in from each end.
    ASSERT_EQ(s->trucks.size(), 2u);
    EXPECT_DOUBLE_EQ(s->trucks[0].at, 7);
    EXPECT_DOUBLE_EQ(s->bodyLength, 26);
    EXPECT_EQ(s->lookMode, QStringLiteral("part"));
    EXPECT_EQ(s->partKey, QStringLiteral("x.1"));
    EXPECT_EQ(s->libraryVersion, 3);
    const QJsonObject back = saveload::rollingStockToJson(*s);
    EXPECT_EQ(back.value(QLatin1String("tags")).toArray().size(), 1);
    EXPECT_EQ(back.value(QLatin1String("look")).toObject().value(QLatin1String("picture")).toString(), QStringLiteral("p.png"));
    EXPECT_EQ(*saveload::rollingStockFromJson(back), *s);
    EXPECT_FALSE(saveload::rollingStockFromJson(QJsonObject{ { QStringLiteral("number"), QStringLiteral("x") } }).has_value());

    const QJsonObject train{ { QStringLiteral("id"), QStringLiteral("t1") },
                             { QStringLiteral("units"), QJsonArray{ QJsonObject{ { QStringLiteral("stock"), QStringLiteral("s1") } }, QJsonObject{ { QStringLiteral("nope"), 1 } } } },
                             { QStringLiteral("position"), QJsonValue(QJsonValue::Null) },
                             { QStringLiteral("later"), 7 } };
    const auto t = saveload::trainFromJson(train);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->units.size(), 1u);
    EXPECT_FALSE(t->position.has_value());
    const QJsonObject tb = saveload::trainToJson(*t);
    EXPECT_EQ(tb.value(QLatin1String("later")).toInt(), 7);
    EXPECT_TRUE(tb.value(QLatin1String("position")).isNull());
}

TEST(TrainRecords, PartLengthIsBetweenItsCouplers) {
    EXPECT_NEAR(*partStockLength({ { QStringLiteral("magnet"), QPointF(-14, 0) }, { QStringLiteral("magnet"), QPointF(14, 0) } }), 28.0, 1e-9);
    EXPECT_FALSE(partStockLength({ { QStringLiteral("rail"), QPointF(0, 0) }, { QStringLiteral("magnet"), QPointF(1, 0) } }).has_value());
}
