// Route search against the shared contract (fixtures/contracts/
// route-search.json): the web's packages/parts-catalog/src/track/
// routes.test.ts checks the same file, so both apps find the same way,
// length and switch settings, and say the same when there is none.

#include "track/Routes.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

using namespace bld;
using namespace bld::track;

namespace {

QJsonObject doc() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/route-search.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

TrackNetwork network(const QJsonObject& d, const QStringList& without = {}, const QString& list = QStringLiteral("bricks")) {
    const QJsonObject parts = d.value(QLatin1String("parts")).toObject();
    std::vector<TrackBrickInput> bricks;
    for (const auto& bv : d.value(list).toArray()) {
        const QJsonObject b = bv.toObject();
        if (without.contains(b.value(QLatin1String("id")).toString())) continue;
        TrackBrickInput in;
        in.id = b.value(QLatin1String("id")).toString();
        in.partKey = b.value(QLatin1String("part")).toString();
        in.orientation = b.value(QLatin1String("orientation")).toDouble(0.0);
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

TrackPosition pos(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return { o.value(QLatin1String("brick")).toString(), o.value(QLatin1String("from")).toInt(), o.value(QLatin1String("to")).toInt(),
             o.value(QLatin1String("s")).toDouble() };
}

QString problemId(RouteProblem p) {
    return p == RouteProblem::NoTrack ? QStringLiteral("noTrack") : p == RouteProblem::TurnAround ? QStringLiteral("turnAround") : QStringLiteral("samePlace");
}

}  // namespace

QString facingId(Facing f) { return f == Facing::To ? QStringLiteral("to") : QStringLiteral("from"); }

TEST(RouteSearch, MatchesTheContract) {
    const QJsonObject d = doc();
    const TrackNetwork main = network(d);
    const TrackNetwork withoutM = network(d, { QStringLiteral("M") });
    const TrackNetwork loops = network(d, {}, QStringLiteral("loopBricks"));
    const QJsonArray cases = d.value(QLatin1String("cases")).toArray();
    ASSERT_GE(cases.size(), 7);
    for (const auto& cv : cases) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        const bool onLoops = c.value(QLatin1String("network")).toString() == QLatin1String("loops");
        const TrackNetwork& net = onLoops ? loops : main;
        FindOptions opts;
        if (c.contains(QLatin1String("facing")))
            opts.facing = c.value(QLatin1String("facing")).toString() == QLatin1String("from") ? Facing::From : Facing::To;
        for (const auto& v : c.value(QLatin1String("via")).toArray()) opts.via.push_back(pos(v));
        opts.followSwitches = c.value(QLatin1String("followSwitches")).toBool();
        const QJsonObject states = c.value(QLatin1String("states")).toObject();
        for (auto it = states.begin(); it != states.end(); ++it) opts.states.insert(it.key(), it.value().toInt());
        const TrackPosition from = pos(c.value(QLatin1String("from")));
        const TrackPosition to = pos(c.value(QLatin1String("to")));
        const auto got = findRoute(net, from, to, opts);
        const QJsonValue want = c.value(QLatin1String("expect"));
        if (want.isString()) {
            ASSERT_TRUE(std::holds_alternative<RouteProblem>(got));
            EXPECT_EQ(problemId(std::get<RouteProblem>(got)), want.toString());
            continue;
        }
        ASSERT_TRUE(std::holds_alternative<RouteFound>(got));
        const RouteFound& f = std::get<RouteFound>(got);
        const QJsonObject w = want.toObject();
        QJsonArray steps;
        for (const auto& s : f.steps)
            steps.append(QJsonObject{ { QStringLiteral("brick"), s.brick }, { QStringLiteral("from"), s.from }, { QStringLiteral("to"), s.to } });
        EXPECT_EQ(steps, w.value(QLatin1String("steps")).toArray());
        EXPECT_NEAR(f.length, w.value(QLatin1String("length")).toDouble(), 1e-5);
        QJsonObject switches;
        for (auto it = f.switches.constBegin(); it != f.switches.constEnd(); ++it) switches.insert(it.key(), it.value());
        EXPECT_EQ(switches, w.value(QLatin1String("switches")).toObject());
        EXPECT_EQ(QJsonArray::fromStringList(f.turnsAround), w.value(QLatin1String("turnsAround")).toArray());
        EXPECT_EQ(facingId(f.facing), w.value(QLatin1String("facing")).toString());
        EXPECT_EQ(QJsonArray::fromStringList(f.bothWays), w.value(QLatin1String("bothWays")).toArray());

        Route r;
        r.steps = f.steps;
        r.from = from;
        r.to = to;
        EXPECT_EQ(routeFacing(r), f.facing);
        const RouteStatus st = routeStatus(onLoops ? loops : withoutM, r);
        EXPECT_EQ(st.bothWays, f.bothWays);
        const QJsonObject ws = c.value(QLatin1String("status")).toObject();
        if (ws.value(QLatin1String("status")).toString() == QLatin1String("broken")) {
            EXPECT_TRUE(st.broken);
            EXPECT_EQ(st.at, ws.value(QLatin1String("at")).toInt());
            EXPECT_EQ(st.brick, ws.value(QLatin1String("brick")).toString());
        } else {
            EXPECT_FALSE(st.broken);
            EXPECT_NEAR(st.length, ws.value(QLatin1String("length")).toDouble(), 1e-5);
        }
    }
}

TEST(RouteSearch, JoinsLooseEndsThatNearlyMeet) {
    const QJsonObject d = doc();
    EXPECT_TRUE(network(d).gaps().isEmpty());
    const TrackNetwork loops = network(d, {}, QStringLiteral("loopBricks"));
    const QJsonArray want = d.value(QLatin1String("gaps")).toArray();
    ASSERT_EQ(loops.gaps().size(), want.size());
    for (qsizetype i = 0; i < want.size(); ++i) {
        const QJsonObject w = want[i].toObject();
        const TrackGap& g = loops.gaps()[i];
        EXPECT_EQ(switchKey(g.a.brick, g.a.conn), w.value(QLatin1String("a")).toString());
        EXPECT_EQ(switchKey(g.b.brick, g.b.conn), w.value(QLatin1String("b")).toString());
        EXPECT_NEAR(g.at.x(), w.value(QLatin1String("at")).toArray()[0].toDouble(), 1e-5);
        EXPECT_NEAR(g.at.y(), w.value(QLatin1String("at")).toArray()[1].toDouble(), 1e-5);
        EXPECT_NEAR(g.gap, w.value(QLatin1String("gap")).toDouble(), 1e-5);
        EXPECT_EQ(loops.partner(g.a), g.b);
        EXPECT_EQ(loops.partner(g.b), g.a);
    }
}

TEST(RouteSearch, AClickOnASwitchHeelIsOnEachLeg) {
    const QJsonObject d = doc();
    const TrackNetwork net = network(d);
    const auto spots = sameSpot(net, { QStringLiteral("P"), 0, 1, 0.2 });
    ASSERT_EQ(spots.size(), 2u);
    EXPECT_EQ(spots[1].from, 0);
    EXPECT_EQ(spots[1].to, 2);
    EXPECT_EQ(sameSpot(net, { QStringLiteral("P"), 0, 1, 12 }).size(), 1u);
}

TEST(RouteSearch, ASavedRouteKnowsItsSwitchesAndLinesAndFollowsRenames) {
    const QJsonObject d = doc();
    const TrackNetwork net = network(d);
    const QJsonObject c = d.value(QLatin1String("cases")).toArray()[0].toObject();
    Route r;
    r.from = pos(c.value(QLatin1String("from")));
    r.to = pos(c.value(QLatin1String("to")));
    r.steps = std::get<RouteFound>(findRoute(net, r.from, r.to)).steps;
    RouteStatus st = routeStatus(net, r);
    EXPECT_FALSE(st.broken);
    EXPECT_EQ(st.switches.keys(), QStringList{ QStringLiteral("P/0") });
    EXPECT_TRUE(st.setAgainst.isEmpty());
    st = routeStatus(net, r, { { QStringLiteral("P/0"), 2 } });
    EXPECT_EQ(st.setAgainst, QStringList{ QStringLiteral("P/0") });
    EXPECT_EQ(routeLines(net, r).size(), 1u);
    EXPECT_EQ(routeLines(network(d, { QStringLiteral("M") }), r).size(), 2u);
    const auto near = nearestPosition(net, QPointF(50, 7.5), 2);
    ASSERT_TRUE(near);
    EXPECT_EQ(near->brick, QStringLiteral("B"));
    EXPECT_NEAR(near->s, 18, 1e-6);
    EXPECT_FALSE(nearestPosition(net, QPointF(50, 30), 2));
    const Route renamed = renameRouteBricks(r, [](const QString& id) { return id == QLatin1String("A") ? QStringLiteral("A2") : id; });
    EXPECT_EQ(renamed.from.brick, QStringLiteral("A2"));
    EXPECT_EQ(renamed.steps.front().brick, QStringLiteral("A2"));
    EXPECT_EQ(renamed.to.brick, QStringLiteral("E"));
}
