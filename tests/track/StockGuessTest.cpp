// The truck guess on real train models (trains design 3.3, Phase 6): the
// shared contract fixtures/contracts/truck-guess.json, three LDraw OMR
// models (an engine on two bogies, a caboose on two axles, a tanker) read
// by this app's own LDraw reader, and hand-made part lists. The web's
// apps/web/src/import/client/test-node/truckGuess.test.ts runs the same.

#include "../../src/track/StockGuess.h"
#include "../../src/import/StockModel.h"
#include "../../src/import/ldraw/LDrawReader.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace bld::track;

namespace {

const QString kDir = QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/");

QJsonObject contract() {
    QFile f(kDir + QStringLiteral("truck-guess.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

QRectF rectOf(const QJsonObject& o) {
    return { o.value(QLatin1String("x")).toDouble(), o.value(QLatin1String("y")).toDouble(),
             o.value(QLatin1String("width")).toDouble(), o.value(QLatin1String("height")).toDouble() };
}

} // namespace

TEST(StockGuess, KeepsTheContractsPartTableWordForWord) {
    EXPECT_EQ(stockPartsTable(), contract().value(QLatin1String("parts")).toObject());
}

TEST(StockGuess, MatchesEveryContractVector) {
    const QJsonArray vectors = contract().value(QLatin1String("vectors")).toArray();
    ASSERT_GE(vectors.size(), 3);
    for (const auto& v : vectors) {
        const QJsonObject c = v.toObject();
        const QString name = c.value(QLatin1String("name")).toString();
        std::vector<GuessRef> refs;
        if (c.contains(QLatin1String("model"))) {
            const auto read = bld::import::readLDraw(kDir + c.value(QLatin1String("model")).toString());
            ASSERT_TRUE(read.ok) << name.toStdString();
            refs = bld::import::topDownRefs(read);
        } else {
            for (const auto& r : c.value(QLatin1String("refs")).toArray()) {
                const QJsonObject o = r.toObject();
                refs.push_back({ o.value(QLatin1String("id")).toString(),
                                 QPointF(o.value(QLatin1String("x")).toDouble(),
                                         o.value(QLatin1String("y")).toDouble()) });
            }
        }
        const QJsonObject centre = c.value(QLatin1String("centre")).toObject();
        const StockGuess g = guessStock(refs, rectOf(c.value(QLatin1String("bounds")).toObject()),
                                        QPointF(centre.value(QLatin1String("x")).toDouble(),
                                                centre.value(QLatin1String("y")).toDouble()));
        const QJsonObject e = c.value(QLatin1String("expect")).toObject();
        SCOPED_TRACE(name.toStdString());
        EXPECT_EQ(g.quarterTurns, e.value(QLatin1String("quarterTurns")).toInt());
        EXPECT_DOUBLE_EQ(g.length, e.value(QLatin1String("length")).toDouble());
        EXPECT_DOUBLE_EQ(g.width, e.value(QLatin1String("width")).toDouble());
        EXPECT_DOUBLE_EQ(g.bodyLength, e.value(QLatin1String("bodyLength")).toDouble());
        const QJsonArray trucks = e.value(QLatin1String("trucks")).toArray();
        EXPECT_DOUBLE_EQ(g.front, trucks.at(0).toDouble());
        EXPECT_DOUBLE_EQ(g.rear, trucks.at(1).toDouble());
        EXPECT_EQ(g.trucksFrom, e.value(QLatin1String("trucksFrom")).toString());
        EXPECT_EQ(g.couplersFrom, e.value(QLatin1String("couplersFrom")).toString());
    }
}

TEST(StockGuess, NamesPartsAsTheTableDoes) {
    EXPECT_EQ(stockPartId(QStringLiteral("S\\2878C01.DAT")), QStringLiteral("2878c01"));
    EXPECT_EQ(stockPartId(QStringLiteral("parts/4092a.dat")), QStringLiteral("4092a"));
    EXPECT_EQ(stockPartId(QStringLiteral("88002c01")), QStringLiteral("88002c01"));
}
