// The track network against the shared contracts (fixtures/contracts/
// track-kinds.json, track-paths.json, track-network.json): the web's
// packages/parts-catalog/src/track/track.test.ts checks the same files, so
// both apps find the same ways through, the same switches and the same
// network. And against the bundled parts library: the contract's parts are
// the library's, and a real layout's switches are found.

#include "track/TrackNetwork.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

using namespace bld;
using namespace bld::track;

namespace {

QJsonObject readJson(const QString& path) {
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject contract(const char* name) {
    return readJson(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/") + QLatin1String(name));
}

QVector<TrackConn> connsOf(const QJsonArray& a) {
    QVector<TrackConn> out;
    for (const auto& v : a) {
        const QJsonObject c = v.toObject();
        out.append({ c.value(QLatin1String("type")).toString(), c.value(QLatin1String("x")).toDouble(),
                     c.value(QLatin1String("y")).toDouble(), c.value(QLatin1String("angle")).toDouble(),
                     c.value(QLatin1String("plug")).toInt() });
    }
    return out;
}

QString portName(const PortRef& p) { return switchKey(p.brick, p.conn); }

}  // namespace

TEST(TrackTables, AreTheSharedContractsWordForWord) {
    for (const char* name : { "track-kinds.json", "track-paths.json" }) {
        QFile built(QStringLiteral(":/bld/track/") + QLatin1String(name));
        ASSERT_TRUE(built.open(QIODevice::ReadOnly)) << name;
        QFile shared(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/") + QLatin1String(name));
        ASSERT_TRUE(shared.open(QIODevice::ReadOnly)) << name;
        EXPECT_EQ(built.readAll(), shared.readAll()) << name;
    }
}

TEST(TrackTables, SayWhichTypesAreTrackAndWhatKind) {
    EXPECT_EQ(trackKindOf(QStringLiteral("1"), false), QStringLiteral("rail"));
    EXPECT_EQ(trackKindOf(QStringLiteral("mono45"), false), QStringLiteral("monorail"));
    EXPECT_EQ(trackKindOf(QStringLiteral("trixbrix_sw_r40_left_turn"), false), QStringLiteral("rail"));
    EXPECT_EQ(trackKindOf(QStringLiteral("NT_SPLITWYE"), false), QStringLiteral("rail"));
    EXPECT_FALSE(trackKindOf(QStringLiteral("NT_SPLITTER"), true));
    EXPECT_FALSE(trackKindOf(QStringLiteral("magnet"), false));
    EXPECT_FALSE(trackKindOf(QStringLiteral("2"), false));
    EXPECT_FALSE(trackKindOf(QStringLiteral("trixbrix_mils"), true));
    EXPECT_EQ(trackKindOf(QStringLiteral("acme_rail"), true), QStringLiteral("rail"));
    EXPECT_FALSE(trackKindOf(QStringLiteral("acme_rail"), false));
    EXPECT_DOUBLE_EQ(wrapDegrees(-180), 180);
    EXPECT_NEAR(wrapDegrees(-337.5), 22.5, 1e-9);
}

TEST(PartTrack, MatchesTheContractForEveryPart) {
    const QJsonObject parts = contract("track-network.json").value(QLatin1String("parts")).toObject();
    ASSERT_GE(parts.size(), 20);
    for (auto it = parts.begin(); it != parts.end(); ++it) {
        SCOPED_TRACE(it.key().toStdString());
        const QJsonObject v = it.value().toObject();
        const QJsonObject e = v.value(QLatin1String("expect")).toObject();
        const PartTrack t = partTrack(it.key(), connsOf(v.value(QLatin1String("connections")).toArray()));
        QJsonArray kinds;
        for (const auto& k : t.kinds) kinds.append(k ? QJsonValue(*k) : QJsonValue(QJsonValue::Null));
        EXPECT_EQ(kinds, e.value(QLatin1String("kinds")).toArray());
        QJsonArray paths;
        for (const auto& [i, j] : t.paths) paths.append(QJsonArray{ i, j });
        EXPECT_EQ(paths, e.value(QLatin1String("paths")).toArray());
        QJsonArray switches;
        for (const PartSwitch& sw : t.switches) {
            QJsonArray exits;
            for (int x : sw.exits) exits.append(x);
            QJsonObject legs;
            for (auto l = sw.legs.constBegin(); l != sw.legs.constEnd(); ++l) legs.insert(QString::number(l.key()), l.value());
            switches.append(QJsonObject{ { QStringLiteral("heel"), sw.heel },
                                         { QStringLiteral("exits"), exits },
                                         { QStringLiteral("default"), sw.defaultExit },
                                         { QStringLiteral("legs"), legs } });
        }
        EXPECT_EQ(switches, e.value(QLatin1String("switches")).toArray());
    }
}

TEST(PartTrack, NeverGoesFrogToFrog) {
    const QJsonObject parts = contract("track-network.json").value(QLatin1String("parts")).toObject();
    for (const char* key : { "2861.8", "2859.8", "948AC01.1", "2889.7", "6379CX1.8", "TS_WYEDIVERGER.8" }) {
        const PartTrack t = partTrack(QLatin1String(key), connsOf(parts.value(QLatin1String(key)).toObject().value(QLatin1String("connections")).toArray()));
        ASSERT_FALSE(t.switches.isEmpty()) << key;
        const auto& legs = t.switches.front().exits;
        EXPECT_FALSE(t.paths.contains(std::make_pair(legs[0], legs[1]))) << key;
    }
}

TEST(TrackNetwork, MatchesTheContractForEveryLayout) {
    const QJsonObject doc = contract("track-network.json");
    const QJsonObject parts = doc.value(QLatin1String("parts")).toObject();
    for (const auto& lv : doc.value(QLatin1String("layouts")).toArray()) {
        const QJsonObject layout = lv.toObject();
        SCOPED_TRACE(layout.value(QLatin1String("name")).toString().toStdString());
        std::vector<TrackBrickInput> bricks;
        for (const auto& bv : layout.value(QLatin1String("bricks")).toArray()) {
            const QJsonObject b = bv.toObject();
            TrackBrickInput in;
            in.id = b.value(QLatin1String("id")).toString();
            in.partKey = b.value(QLatin1String("part")).toString();
            in.conns = connsOf(parts.value(in.partKey).toObject().value(QLatin1String("connections")).toArray());
            in.orientation = b.value(QLatin1String("orientation")).toDouble();
            const QJsonArray pivot = b.value(QLatin1String("pivot")).toArray();
            const double r = qDegreesToRadians(in.orientation);
            for (const auto& c : in.conns)
                in.points.append(QPointF(pivot[0].toDouble() + c.x * std::cos(r) - c.y * std::sin(r),
                                         pivot[1].toDouble() + c.x * std::sin(r) + c.y * std::cos(r)));
            for (const auto& cv : b.value(QLatin1String("connexions")).toArray())
                in.connexions.append({ cv.toObject().value(QLatin1String("id")).toString(),
                                       cv.toObject().value(QLatin1String("linkedTo")).toString() });
            bricks.push_back(in);
        }
        const TrackNetwork net(bricks);
        const QJsonObject e = layout.value(QLatin1String("expect")).toObject();
        QJsonArray ids;
        for (const auto& id : net.brickIds()) ids.append(id);
        EXPECT_EQ(ids, e.value(QLatin1String("bricks")).toArray());
        QJsonArray switches;
        for (const SwitchPoint& sw : net.switches()) {
            QJsonArray exits;
            for (int x : sw.exits) exits.append(x);
            switches.append(QJsonObject{ { QStringLiteral("key"), sw.key }, { QStringLiteral("exits"), exits }, { QStringLiteral("default"), sw.defaultExit } });
        }
        EXPECT_EQ(switches, e.value(QLatin1String("switches")).toArray());
        const QJsonObject partners = e.value(QLatin1String("partners")).toObject();
        for (auto it = partners.begin(); it != partners.end(); ++it) {
            const auto port = parseSwitchKey(it.key());
            ASSERT_TRUE(port) << it.key().toStdString();
            const auto got = net.partner(*port);
            EXPECT_EQ(got ? QJsonValue(portName(*got)) : QJsonValue(QJsonValue::Null), it.value()) << it.key().toStdString();
        }
        const QJsonObject exits = e.value(QLatin1String("exits")).toObject();
        for (auto it = exits.begin(); it != exits.end(); ++it) {
            const auto port = parseSwitchKey(it.key());
            QJsonArray got;
            for (int x : net.exits(port->brick, port->conn)) got.append(x);
            EXPECT_EQ(got, it.value().toArray()) << it.key().toStdString();
        }
        const QJsonObject ports = e.value(QLatin1String("ports")).toObject();
        int portCount = 0;
        for (const auto& id : net.brickIds())
            for (int i = 0; i < net.trackOf(id)->kinds.size(); ++i) {
                if (!net.trackOf(id)->kinds[i]) continue;
                ++portCount;
                const QJsonArray want = ports.value(switchKey(id, i)).toArray();
                ASSERT_EQ(want.size(), 3) << switchKey(id, i).toStdString();
                const auto at = net.port(id, i);
                EXPECT_NEAR(at->first.x(), want[0].toDouble(), 1e-6);
                EXPECT_NEAR(at->first.y(), want[1].toDouble(), 1e-6);
                EXPECT_NEAR(at->second, want[2].toDouble(), 1e-6);
            }
        EXPECT_EQ(portCount, ports.size());
        const QJsonObject lengths = e.value(QLatin1String("lengths")).toObject();
        int pathCount = 0;
        for (const auto& id : net.brickIds())
            for (const auto& [i, j] : net.trackOf(id)->paths) {
                ++pathCount;
                const QString k = QStringLiteral("%1/%2-%3").arg(id).arg(i).arg(j);
                ASSERT_TRUE(lengths.contains(k)) << k.toStdString();
                EXPECT_NEAR(net.pathLength(id, i, j), lengths.value(k).toDouble(), 1e-6) << k.toStdString();
            }
        EXPECT_EQ(pathCount, lengths.size());
    }
}

TEST(SwitchStates, ReadTheStoredExitOrTheDefaultAndFlipRound) {
    TrackBrickInput p;
    p.id = QStringLiteral("P");
    p.partKey = QStringLiteral("2861.8");
    p.conns = { { QStringLiteral("1"), -16.9375, 6.375, 180, 1 }, { QStringLiteral("1"), 15.0625, 6.375, 0, -1 },
                { QStringLiteral("1"), 15.7552, -6.58018, -22.5, -1 } };
    for (const auto& c : p.conns) p.points.append(QPointF(c.x, c.y));
    const TrackNetwork net({ p });
    const SwitchPoint* sw = net.switchAt(QStringLiteral("P/0"));
    ASSERT_TRUE(sw);
    EXPECT_EQ(net.settingOf(*sw, {}), 1);
    EXPECT_EQ(net.settingOf(*sw, { { QStringLiteral("P/0"), 2 } }), 2);
    EXPECT_EQ(net.settingOf(*sw, { { QStringLiteral("P/0"), 0 } }), 1);
    EXPECT_EQ(nextExit(*sw, 1), 2);
    EXPECT_EQ(nextExit(*sw, 2), 1);
    const QMap<QString, int> states{ { QStringLiteral("P/0"), 2 }, { QStringLiteral("P/1"), 0 }, { QStringLiteral("Gone/0"), 1 } };
    EXPECT_EQ(cleanSwitchStates(states, net), (QMap<QString, int>{ { QStringLiteral("P/0"), 2 } }));
    EXPECT_EQ(withoutGoneSwitches(states, { QStringLiteral("P") }),
              (QMap<QString, int>{ { QStringLiteral("P/0"), 2 }, { QStringLiteral("P/1"), 0 } }));
    EXPECT_EQ(switchKey(QStringLiteral("a/b"), 2), QStringLiteral("a/b/2"));
    EXPECT_EQ(parseSwitchKey(QStringLiteral("a/b/2"))->brick, QStringLiteral("a/b"));
    EXPECT_FALSE(parseSwitchKey(QStringLiteral("nope")));
    EXPECT_FALSE(parseSwitchKey(QStringLiteral("B/01")));
}

// The contract's parts are the bundled library's, connection for connection.
TEST(TrackLibrary, TheContractsPartsAreTheLibrarys) {
    parts::PartsLibrary lib;
    lib.addSearchPath(QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
    lib.scan();
    const QJsonObject parts = contract("track-network.json").value(QLatin1String("parts")).toObject();
    int checked = 0;
    for (auto it = parts.begin(); it != parts.end(); ++it) {
        if (it.key().startsWith(QLatin1String("tb "))) continue;  // TrixBrix: a pack, not bundled
        const auto meta = lib.metadata(it.key());
        ASSERT_TRUE(meta) << it.key().toStdString();
        const auto want = connsOf(it.value().toObject().value(QLatin1String("connections")).toArray());
        ASSERT_EQ(meta->connections.size(), want.size()) << it.key().toStdString();
        for (int i = 0; i < want.size(); ++i) {
            EXPECT_EQ(meta->connections[i].type, want[i].type);
            EXPECT_NEAR(meta->connections[i].position.x(), want[i].x, 1e-9);
            EXPECT_NEAR(meta->connections[i].position.y(), want[i].y, 1e-9);
            EXPECT_NEAR(meta->connections[i].angleDegrees, want[i].angle, 1e-9);
            EXPECT_EQ(meta->connections[i].electricPlug, want[i].plug);
        }
        ++checked;
    }
    EXPECT_GE(checked, 20);
}

// A real layout (the web's tight-corner fixture, also the desktop's): its
// RC crossover and monorail points are switches, and track joins up.
TEST(TrackLibrary, ARealLayoutsSwitchesAreFound) {
    // Where a part's picture sits needs its image read: a GUI app for Qt's image plugins.
    static int argc = 1;
    static char name[] = "bld_track_tests";
    static char* argv[] = { name, nullptr };
    static QGuiApplication* app = QGuiApplication::instance() ? nullptr : new QGuiApplication(argc, argv);
    (void)app;
    parts::PartsLibrary lib;
    lib.addSearchPath(QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
    lib.scan();
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    ASSERT_TRUE(res.map) << res.error.toStdString();
    const TrackNetwork net = trackNetworkOf(*res.map, lib);
    int crossover = 0, monorail = 0;
    for (const SwitchPoint& sw : net.switches()) {
        const PartTrack* t = net.trackOf(sw.brick);
        if (t->kinds[sw.heel] == QStringLiteral("monorail")) ++monorail;
        else ++crossover;
    }
    EXPECT_EQ(crossover, 4);  // 7996: a switch at each end
    EXPECT_EQ(monorail, 4);   // two 2889s and two 2890s
    // Every straight's ends are linked to track (a loop has no free ends there).
    int linked = 0;
    for (const auto& id : net.brickIds())
        for (int i = 0; i < net.trackOf(id)->kinds.size(); ++i)
            if (net.partner({ id, i })) ++linked;
    EXPECT_GT(linked, 100);
}
