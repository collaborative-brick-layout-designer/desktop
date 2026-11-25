// A spot on the track against the shared contract (fixtures/contracts/
// places.json `onTrack`, on route-search.json's network): the web's
// packages/parts-catalog/src/track/places.test.ts checks the same.

#include "track/PlacesOnTrack.h"
#include "track/TrackNetwork.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

void nearPoint(QPointF got, const QJsonValue& want) {
    EXPECT_NEAR(got.x(), want.toArray()[0].toDouble(), 1e-3);
    EXPECT_NEAR(got.y(), want.toArray()[1].toDouble(), 1e-3);
}

}  // namespace

TEST(PlacesOnTrack, WhereASpotIsAndWhatCanStandThere) {
    const TrackNetwork net = network();
    const auto runs = trackRuns(net);
    const QJsonArray cases = load("places.json").value(QLatin1String("onTrack")).toArray();
    ASSERT_GE(cases.size(), 20);
    for (const auto& cv : cases) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(QJsonDocument(c).toJson(QJsonDocument::Compact).toStdString());
        const QJsonObject t = c.value(QLatin1String("track")).toObject();
        const QJsonValue wanted = c.value(QLatin1String("wanted"));
        const auto s = spotOnTrack(net, runs, t.value(QLatin1String("brick")).toString(), t.value(QLatin1String("from")).toInt(),
                                   t.value(QLatin1String("to")).toInt(), wanted.isDouble() ? std::optional(wanted.toDouble()) : std::nullopt);
        ASSERT_EQ(s.has_value(), c.value(QLatin1String("expect")).isObject());
        if (!s) continue;
        const QJsonObject w = c.value(QLatin1String("expect")).toObject();
        ASSERT_EQ(s->usable.has_value(), w.value(QLatin1String("usable")).isDouble());
        if (s->usable) {
            EXPECT_NEAR(*s->usable, w.value(QLatin1String("usable")).toDouble(), 1e-3);
        }
        EXPECT_NEAR(s->length, w.value(QLatin1String("length")).toDouble(), 1e-3);
        nearPoint(s->middle, w.value(QLatin1String("middle")));
        nearPoint(s->line.front(), w.value(QLatin1String("from")));
        nearPoint(s->line.back(), w.value(QLatin1String("to")));
        EXPECT_EQ(static_cast<int>(s->line.size()), w.value(QLatin1String("points")).toInt());
    }
}
