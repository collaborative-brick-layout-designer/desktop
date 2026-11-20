// Fit checks against the shared contract (fixtures/contracts/
// train-geometry.json: `overhang`, `runs` and `checks`, on route-search.json's
// network): the web's packages/parts-catalog/src/track/fitChecks.test.ts
// checks the same, so both apps find the same problems with the same numbers.

#include "track/FitChecks.h"
#include "track/Routes.h"
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

core::TrackPosition pos(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return { o.value(QLatin1String("brick")).toString(), o.value(QLatin1String("from")).toInt(), o.value(QLatin1String("to")).toInt(),
             o.value(QLatin1String("s")).toDouble() };
}

double r4(double v) {
    const double x = std::round(v * 1e4) / 1e4;
    return x == 0.0 ? 0.0 : x;
}

QString endKind(RunEnd::Kind k) {
    switch (k) {
    case RunEnd::Kind::End: return QStringLiteral("end");
    case RunEnd::Kind::Heel: return QStringLiteral("heel");
    case RunEnd::Kind::Leg: return QStringLiteral("leg");
    case RunEnd::Kind::Loop: return QStringLiteral("loop");
    }
    return {};
}

}  // namespace

TEST(FitChecks, OverhangOnACurve) {
    const QJsonObject d = load("train-geometry.json");
    const QJsonArray cases = d.value(QLatin1String("overhang")).toArray();
    ASSERT_GE(cases.size(), 3);
    for (const auto& cv : cases) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        const Overhang h = overhang(c.value(QLatin1String("radius")).toDouble(), c.value(QLatin1String("spacing")).toDouble(),
                                    c.value(QLatin1String("body")).toDouble(), c.value(QLatin1String("width")).toDouble());
        const QJsonObject w = c.value(QLatin1String("expect")).toObject();
        EXPECT_NEAR(h.middle, w.value(QLatin1String("middle")).toDouble(), 1e-4);
        EXPECT_NEAR(h.end, w.value(QLatin1String("end")).toDouble(), 1e-4);
        EXPECT_NEAR(h.corner, w.value(QLatin1String("corner")).toDouble(), 1e-4);
        EXPECT_NEAR(h.swivel, w.value(QLatin1String("swivel")).toDouble(), 1e-4);
    }
    // The design's example: a 6-wide car, 28-stud body, trucks 20 apart, on R40.
    const Overhang r40 = overhang(40, 20, 28, 6);
    EXPECT_NEAR(r40.middle, 1.27, 0.005);
    EXPECT_NEAR(r40.end, 1.18, 0.005);
    EXPECT_NEAR(r40.corner, 1.02, 0.005);
    EXPECT_NEAR(r40.swivel, 14.48, 0.005);
}

TEST(FitChecks, TrackRunsMatchTheContract) {
    const QJsonObject d = load("train-geometry.json");
    const TrackNetwork net = network();
    const auto runs = trackRuns(net);
    const QJsonArray want = d.value(QLatin1String("runs")).toArray();
    ASSERT_EQ(runs.size(), static_cast<size_t>(want.size()));
    for (qsizetype i = 0; i < want.size(); ++i) {
        const QJsonObject w = want[i].toObject();
        const TrackRun& r = runs[static_cast<size_t>(i)];
        QStringList steps;
        for (const auto& s : r.steps) steps << QStringLiteral("%1/%2/%3").arg(s.brick).arg(s.from).arg(s.to);
        QStringList wantSteps;
        for (const auto& s : w.value(QLatin1String("steps")).toArray()) wantSteps << s.toString();
        EXPECT_EQ(steps, wantSteps);
        EXPECT_NEAR(r.length, w.value(QLatin1String("length")).toDouble(), 1e-4);
        EXPECT_NEAR(r.usable, w.value(QLatin1String("usable")).toDouble(), 1e-4);
        const QJsonArray ends = w.value(QLatin1String("ends")).toArray();
        for (int k = 0; k < 2; ++k) {
            const QJsonObject e = ends[k].toObject();
            EXPECT_EQ(endKind(r.ends[k].kind), e.value(QLatin1String("kind")).toString());
            EXPECT_EQ(r.ends[k].switchKey, e.value(QLatin1String("switch")).toString());
            EXPECT_NEAR(r.ends[k].foul, e.value(QLatin1String("foul")).toDouble(), 1e-4);
        }
    }
    const SwitchPoint* sw = net.switchAt(QStringLiteral("P/0"));
    ASSERT_TRUE(sw);
    EXPECT_DOUBLE_EQ(foulingBeyond(net, *sw, 1, 8), 0.0);
    EXPECT_NEAR(foulingBeyond(net, *sw, 1, 10, 50), 50 - net.pathLength(QStringLiteral("P"), 0, 1), 1e-6);
}

TEST(FitChecks, ChecksMatchTheContract) {
    const QJsonObject d = load("train-geometry.json");
    const TrackNetwork net = network();
    StockById stock;
    const QJsonObject so = d.value(QLatin1String("stock")).toObject();
    for (auto it = so.begin(); it != so.end(); ++it) {
        QJsonObject r = it.value().toObject();
        r.insert(QStringLiteral("id"), it.key());
        stock.insert(it.key(), *saveload::rollingStockFromJson(r));
    }
    const QJsonArray cases = d.value(QLatin1String("checks")).toArray();
    ASSERT_GE(cases.size(), 6);
    for (const auto& cv : cases) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        FitInput in;
        in.stock = stock;
        const QJsonObject states = c.value(QLatin1String("states")).toObject();
        for (auto it = states.begin(); it != states.end(); ++it) in.states.insert(it.key(), it.value().toInt());
        for (const auto& tv : c.value(QLatin1String("trains")).toArray()) {
            QJsonObject t = tv.toObject();
            in.trains.push_back(*saveload::trainFromJson(t));
        }
        for (const auto& rv : c.value(QLatin1String("routes")).toArray()) {
            const QJsonObject ro = rv.toObject();
            FindOptions opts;
            for (const auto& v : ro.value(QLatin1String("via")).toArray()) opts.via.push_back(pos(v));
            core::Route route;
            route.id = ro.value(QLatin1String("id")).toString();
            route.from = pos(ro.value(QLatin1String("from")));
            route.to = pos(ro.value(QLatin1String("to")));
            const auto found = findRoute(net, route.from, route.to, opts);
            ASSERT_TRUE(std::holds_alternative<RouteFound>(found));
            route.steps = std::get<RouteFound>(found).steps;
            in.routes.push_back(route);
        }
        const auto got = fitChecks(net, in);
        const QJsonArray want = c.value(QLatin1String("expect")).toArray();
        ASSERT_EQ(got.size(), static_cast<size_t>(want.size()));
        for (qsizetype i = 0; i < want.size(); ++i) {
            const QJsonObject w = want[i].toObject();
            const FitCheck& k = got[static_cast<size_t>(i)];
            EXPECT_EQ(k.key, w.value(QLatin1String("key")).toString());
            EXPECT_EQ(k.kind, w.value(QLatin1String("kind")).toString());
            EXPECT_EQ(k.error ? QStringLiteral("error") : QStringLiteral("warning"), w.value(QLatin1String("severity")).toString());
            EXPECT_NEAR(r4(k.at.x()), w.value(QLatin1String("at")).toArray()[0].toDouble(), 1e-3) << k.key.toStdString();
            EXPECT_NEAR(r4(k.at.y()), w.value(QLatin1String("at")).toArray()[1].toDouble(), 1e-3) << k.key.toStdString();
            const QJsonObject values = w.value(QLatin1String("values")).toObject();
            EXPECT_EQ(k.values.size(), values.size());
            for (auto it = values.begin(); it != values.end(); ++it)
                EXPECT_NEAR(k.values.value(it.key()), it.value().toDouble(), 1e-3) << it.key().toStdString();
        }
    }
}
