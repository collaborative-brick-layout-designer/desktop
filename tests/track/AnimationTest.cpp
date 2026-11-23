// Running trains against the shared contract (fixtures/contracts/
// train-animation.json, on route-search.json's network): the web's
// packages/parts-catalog/src/track/animation.test.ts checks the same, so a
// train runs the same way in both apps: its scale speed, a trailing point
// pushed through, a route's switches, its end, and two trains that would
// touch both stopping.

#include "track/Animation.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QElapsedTimer>
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

QMap<QString, int> statesOf(const QJsonObject& c) {
    QMap<QString, int> out;
    const QJsonObject s = c.value(QLatin1String("states")).toObject();
    for (auto it = s.begin(); it != s.end(); ++it) out.insert(it.key(), it.value().toInt());
    return out;
}

std::vector<RunPlan> plansOf(const QJsonObject& c) {
    std::vector<RunPlan> out;
    const QJsonObject routes = c.value(QLatin1String("routes")).toObject();
    for (const auto& tv : c.value(QLatin1String("trains")).toArray()) {
        RunPlan p;
        p.train = *saveload::trainFromJson(tv);
        if (routes.contains(p.train.id)) {
            QJsonObject r = routes.value(p.train.id).toObject();
            r.insert(QStringLiteral("name"), QStringLiteral("r"));
            p.route = saveload::routeFromJson(r);
        }
        out.push_back(p);
    }
    return out;
}

const double kDt60 = 1.0 / 60.0;

}  // namespace

TEST(TrainAnimation, SpeedsMatchTheContract) {
    const QJsonObject d = load("train-animation.json");
    for (const auto& sv : d.value(QLatin1String("speeds")).toArray()) {
        const QJsonObject s = sv.toObject();
        const double kmh = s.value(QLatin1String("kmh")).toDouble();
        const double scale = s.value(QLatin1String("scale")).toDouble();
        EXPECT_NEAR(studsPerSecond(kmh, scale), s.value(QLatin1String("studsPerSecond")).toDouble(), 1e-5);
        EXPECT_NEAR(realMetresPerSecond(kmh, scale), s.value(QLatin1String("realMetresPerSecond")).toDouble(), 1e-5);
    }
    // The design's example: 40 km/h at 1:38 is about 36.5 studs a second.
    EXPECT_NEAR(studsPerSecond(40, 38), 36.55, 0.01);
    EXPECT_EQ(asScale(87.0), 87);
    EXPECT_EQ(asScale(std::nullopt), kDefaultScale);
    EXPECT_EQ(asScale(2.0), kDefaultScale);
    EXPECT_EQ(asScale(64.4), 64);
}

TEST(TrainAnimation, RunsMatchTheContract) {
    const QJsonObject d = load("train-animation.json");
    const TrackNetwork net = network();
    const StockById stock = stockOf(d);
    const QJsonArray runs = d.value(QLatin1String("runs")).toArray();
    ASSERT_GE(runs.size(), 6);
    for (const auto& cv : runs) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        const QMap<QString, int> states = statesOf(c);
        TrainRun::Parked parked;
        for (const auto& pv : c.value(QLatin1String("parked")).toArray()) {
            const core::Train t = *saveload::trainFromJson(pv);
            parked.push_back({ t.id, placeTrain(net, t, stock, states) });
        }
        TrainRun run(net, stock, plansOf(c), states, c.value(QLatin1String("scale")).toDouble(), parked);
        const QJsonObject want = c.value(QLatin1String("expect")).toObject();
        const QJsonArray wantNotes = want.value(QLatin1String("notes")).toArray();
        const QJsonArray wantChecks = want.value(QLatin1String("checkpoints")).toArray();
        const QJsonArray checkpoints = c.value(QLatin1String("checkpoints")).toArray();
        int noteAt = 0;
        int checkAt = 0;
        const int frames = c.value(QLatin1String("frames")).toInt();
        const double dt = c.value(QLatin1String("dt")).toDouble();
        for (int f = 1; f <= frames; ++f) {
            for (const auto& n : run.step(dt)) {
                ASSERT_LT(noteAt, wantNotes.size()) << "an extra note on frame " << f;
                const QJsonObject w = wantNotes[noteAt++].toObject();
                EXPECT_EQ(f, w.value(QLatin1String("frame")).toInt());
                EXPECT_EQ(runNoteKindName(n.kind), w.value(QLatin1String("kind")).toString());
                EXPECT_EQ(n.train, w.value(QLatin1String("train")).toString());
                EXPECT_EQ(n.other, w.value(QLatin1String("other")).toString());
                EXPECT_EQ(n.switchKey, w.value(QLatin1String("switchKey")).toString());
                EXPECT_NEAR(n.at.x(), w.value(QLatin1String("at")).toArray()[0].toDouble(), 1e-3);
                EXPECT_NEAR(n.at.y(), w.value(QLatin1String("at")).toArray()[1].toDouble(), 1e-3);
            }
            if (checkpoints.contains(f)) {
                const QJsonObject w = wantChecks[checkAt++].toObject().value(QLatin1String("distances")).toObject();
                for (const QString& id : run.trainIds()) EXPECT_NEAR(run.distanceOf(id), w.value(id).toDouble(), 1e-4) << "frame " << f;
            }
        }
        EXPECT_EQ(noteAt, wantNotes.size());
        const QJsonObject overlay = want.value(QLatin1String("overlay")).toObject();
        EXPECT_EQ(run.overlay().size(), overlay.size());
        for (auto it = overlay.begin(); it != overlay.end(); ++it) EXPECT_EQ(run.overlay().value(it.key(), -1), it.value().toInt());
        const QJsonObject trains = want.value(QLatin1String("trains")).toObject();
        for (const QString& id : run.trainIds()) {
            const QJsonObject w = trains.value(id).toObject();
            EXPECT_EQ(runStateName(*run.stateOf(id)), w.value(QLatin1String("state")).toString());
            EXPECT_NEAR(run.distanceOf(id), w.value(QLatin1String("distance")).toDouble(), 1e-4);
            const auto h = run.headOf(id);
            ASSERT_TRUE(h.has_value());
            const QJsonObject wh = w.value(QLatin1String("head")).toObject();
            EXPECT_EQ(h->brick, wh.value(QLatin1String("brick")).toString());
            EXPECT_EQ(h->from, wh.value(QLatin1String("from")).toInt());
            EXPECT_EQ(h->to, wh.value(QLatin1String("to")).toInt());
            EXPECT_EQ(h->facingTo, wh.value(QLatin1String("facing")).toString() == QLatin1String("to"));
            EXPECT_NEAR(h->s, wh.value(QLatin1String("s")).toDouble(), 1e-4);
        }
    }
}

TEST(TrainAnimation, TouchMatchesTheContract) {
    const QJsonObject d = load("train-animation.json");
    const TrackNetwork net = network();
    const StockById stock = stockOf(d);
    for (const auto& cv : d.value(QLatin1String("touch")).toArray()) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        QJsonObject ja = c.value(QLatin1String("a")).toObject();
        ja.insert(QStringLiteral("id"), QStringLiteral("a"));
        QJsonObject jb = c.value(QLatin1String("b")).toObject();
        jb.insert(QStringLiteral("id"), QStringLiteral("b"));
        const auto pa = placeTrain(net, *saveload::trainFromJson(ja), stock);
        const auto pb = placeTrain(net, *saveload::trainFromJson(jb), stock);
        EXPECT_EQ(trainsTouch(pa, pb, stock).has_value(), c.value(QLatin1String("expect")).toObject().value(QLatin1String("touch")).toBool());
    }
}

TEST(TrainAnimation, NeverChangesTheLayoutsSwitches) {
    const QJsonObject d = load("train-animation.json");
    const TrackNetwork net = network();
    const QJsonObject c = d.value(QLatin1String("runs")).toArray()[1].toObject();
    TrainRun run(net, stockOf(d), plansOf(c), {}, 38);
    for (int f = 0; f < 120; ++f) run.step(kDt60);
    EXPECT_EQ(run.overlay().value(QStringLiteral("Q/0"), -1), 2);
    EXPECT_EQ(run.states().value(QStringLiteral("Q/0"), -1), 2);
    // Someone flips it on the layout: the layout's setting is the one again.
    run.setLayoutStates({ { QStringLiteral("Q/0"), 1 } });
    EXPECT_FALSE(run.overlay().contains(QStringLiteral("Q/0")));
    EXPECT_EQ(run.states().value(QStringLiteral("Q/0")), 1);
}

TEST(TrainAnimation, StartsWhereTheMapPutsTheTrain) {
    const QJsonObject d = load("train-animation.json");
    const TrackNetwork net = network();
    const StockById stock = stockOf(d);
    const auto plans = plansOf(d.value(QLatin1String("runs")).toArray()[0].toObject());
    TrainRun run(net, stock, plans, {}, 38);
    const auto still = placeTrain(net, plans[0].train, stock);
    const auto moving = run.placedOf(QStringLiteral("t1"));
    ASSERT_TRUE(still && moving);
    for (size_t i = 0; i < still->units.size(); ++i) {
        EXPECT_NEAR(moving->units[i].frontTruck.x(), still->units[i].frontTruck.x(), 1e-4);
        EXPECT_NEAR(moving->units[i].rearTruck.x(), still->units[i].rearTruck.x(), 1e-4);
        EXPECT_NEAR(moving->units[i].rearTruck.y(), still->units[i].rearTruck.y(), 1e-4);
    }
}

TEST(TrainAnimation, PlayAfterACollisionStopsAgainIfTheyStillMeet) {
    const QJsonObject d = load("train-animation.json");
    const TrackNetwork net = network();
    TrainRun run(net, stockOf(d), plansOf(d.value(QLatin1String("runs")).toArray()[4].toObject()), {}, 38);
    bool hit = false;
    for (int f = 0; f < 240 && !hit; ++f)
        for (const auto& n : run.step(kDt60)) hit = hit || n.kind == RunNoteKind::Collision;
    ASSERT_TRUE(hit);
    EXPECT_EQ(run.stateOf(QStringLiteral("east")), RunState::Blocked);
    EXPECT_FALSE(run.moving());
    run.resume();
    EXPECT_TRUE(run.moving());
    const auto notes = run.step(kDt60);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].kind, RunNoteKind::Collision);
}

TEST(TrainAnimation, AFewLongTrainsRunWellInsideAFrame) {
    const QJsonObject d = load("train-animation.json");
    const TrackNetwork net = network();
    std::vector<RunPlan> plans;
    for (int k = 0; k < 3; ++k) {
        RunPlan p;
        p.train.id = QStringLiteral("t%1").arg(k);
        for (int u = 0; u < 6; ++u) p.train.units.push_back({ QStringLiteral("boxcar"), false });
        core::TrainPosition pos;
        pos.brick = QStringLiteral("E");
        pos.s = 4 + k;
        p.train.position = pos;
        plans.push_back(p);
    }
    TrainRun run(net, stockOf(d), plans, {}, 38);
    QElapsedTimer t;
    t.start();
    for (int f = 0; f < 60; ++f) run.step(kDt60);
    EXPECT_LT(t.elapsed() / 60.0, 8.0);
}
