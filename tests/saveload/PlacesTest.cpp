// Places (core/Places.h): the vectors the web passes too
// (fixtures/contracts/places.json, the web's packages/bbm places.test.ts),
// and the sidecar keeping them.

#include "core/Map.h"
#include "core/Places.h"
#include "core/Sidecar.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

using namespace bld;
namespace cp = core::places;

namespace {

QJsonObject contract() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/places.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonArray section(const char* name) {
    const QJsonArray a = contract().value(QLatin1String(name)).toArray();
    EXPECT_FALSE(a.isEmpty()) << name;
    return a;
}

std::string text(const QJsonValue& v) {
    return QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact).left(300).toStdString();
}

// New ids in the vectors: n1, n2, ...
cp::NewId newIds() {
    return [n = 0]() mutable { return QStringLiteral("n%1").arg(++n); };
}

QSet<QString> strings(const QJsonValue& v) {
    QSet<QString> out;
    const QJsonArray a = v.toArray();
    for (const auto& x : a) out.insert(x.toString());
    return out;
}

QJsonArray idsOf(const QJsonArray& places) {
    QJsonArray out;
    for (const auto& p : places) out.append(p.toObject().value(QLatin1String("id")));
    return out;
}

QJsonObject cannery(const QString& id) {
    return { { QStringLiteral("id"), id },
             { QStringLiteral("name"), QStringLiteral("Acme Cannery") },
             { QStringLiteral("kind"), QStringLiteral("industry") },
             { QStringLiteral("spots"),
               QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("s1") },
                                        { QStringLiteral("name"), QStringLiteral("Dock") },
                                        { QStringLiteral("track"), QJsonObject{ { QStringLiteral("brick"), QStringLiteral("B1") },
                                                                                { QStringLiteral("from"), 0 },
                                                                                { QStringLiteral("to"), 1 } } } } } },
             { QStringLiteral("accepts"), QJsonArray{ QStringLiteral("reefer") } },
             { QStringLiteral("ships"), QJsonArray() },
             { QStringLiteral("tags"), QJsonArray() },
             { QStringLiteral("notes"), QString() },
             { QStringLiteral("later"), QJsonObject{ { QStringLiteral("x"), 1 } } } };
}

}  // namespace

TEST(Places, TheLimitsAreTheWebs) {
    const QJsonObject l = contract().value(QLatin1String("limits")).toObject();
    const QHash<QString, int> here{ { QStringLiteral("places"), cp::kMaxPlaces }, { QStringLiteral("spots"), cp::kMaxSpots },
                                    { QStringLiteral("id"), cp::kMaxId },         { QStringLiteral("brick"), cp::kMaxBrick },
                                    { QStringLiteral("conn"), cp::kMaxConn },     { QStringLiteral("name"), cp::kMaxName },
                                    { QStringLiteral("kind"), cp::kMaxKind },     { QStringLiteral("word"), cp::kMaxWord },
                                    { QStringLiteral("words"), cp::kMaxWords },   { QStringLiteral("notes"), cp::kMaxNotes },
                                    { QStringLiteral("capacity"), cp::kMaxCapacity }, { QStringLiteral("size"), cp::kMaxSize } };
    EXPECT_EQ(l.size(), here.size());
    for (auto it = l.constBegin(); it != l.constEnd(); ++it) EXPECT_EQ(here.value(it.key()), it.value().toInt()) << it.key().toStdString();
}

TEST(Places, ReadsThemAsTheWebDoes) {
    for (const auto& v : section("read")) {
        const QJsonObject c = v.toObject();
        const QJsonObject want = c.value(QLatin1String("expect")).toObject();
        const auto got = cp::readPlace(c.value(QLatin1String("value")));
        EXPECT_EQ(got ? QJsonValue(*got) : QJsonValue(QJsonValue::Null), want.value(QLatin1String("place"))) << text(c.value(QLatin1String("value")));
        if (!got) continue;
        EXPECT_EQ(cp::placeSize(*got), want.value(QLatin1String("size")).toInt());
        // (What is read is always something the server takes.)
        EXPECT_TRUE(cp::validPlace(*got));
    }
    for (const auto& v : section("readList"))
        EXPECT_EQ(idsOf(cp::readPlaces(v.toObject().value(QLatin1String("value")))), v.toObject().value(QLatin1String("expect")).toArray());
}

TEST(Places, ChecksThemAsTheServerDoes) {
    for (const auto& v : section("valid"))
        EXPECT_EQ(cp::validPlace(v.toObject().value(QLatin1String("value"))), v.toObject().value(QLatin1String("expect")).toBool())
            << text(v.toObject().value(QLatin1String("value")));
    for (const auto& v : section("validList"))
        EXPECT_EQ(cp::validPlaces(v.toObject().value(QLatin1String("value"))), v.toObject().value(QLatin1String("expect")).toBool());
}

TEST(Places, WhatHappensToThemIsTheSameInBothApps) {
    for (const auto& v : section("copy")) {
        const QJsonObject c = v.toObject();
        const QJsonObject to = c.value(QLatin1String("rename")).toObject();
        const auto rename = [&to](const QString& id) { return to.contains(id) ? std::optional(to.value(id).toString()) : std::nullopt; };
        EXPECT_EQ(cp::copied(c.value(QLatin1String("places")).toArray(), rename, newIds()), c.value(QLatin1String("expect")).toArray());
    }
    for (const auto& v : section("update")) {
        const QJsonObject c = v.toObject();
        EXPECT_EQ(cp::updated(c.value(QLatin1String("current")).toArray(), c.value(QLatin1String("next")).toArray(), newIds()),
                  c.value(QLatin1String("expect")).toArray());
    }
    for (const auto& v : section("library")) {
        const QJsonObject c = v.toObject();
        EXPECT_EQ(cp::forLibrary(c.value(QLatin1String("places")).toArray(), strings(c.value(QLatin1String("members")))),
                  c.value(QLatin1String("expect")).toArray());
    }
    for (const auto& v : section("adopt")) {
        const QJsonObject c = v.toObject();
        const auto split = cp::adopted(c.value(QLatin1String("layout")).toArray(), strings(c.value(QLatin1String("members"))));
        EXPECT_EQ(idsOf(split.module), c.value(QLatin1String("expect")).toObject().value(QLatin1String("module")).toArray());
        EXPECT_EQ(idsOf(split.layout), c.value(QLatin1String("expect")).toObject().value(QLatin1String("layout")).toArray());
    }
    for (const auto& v : section("release")) {
        const QJsonObject c = v.toObject();
        const QJsonArray all = cp::released(c.value(QLatin1String("layout")).toArray(), c.value(QLatin1String("places")).toArray(), newIds());
        QJsonArray last;
        for (qsizetype i = std::max<qsizetype>(0, all.size() - 3); i < all.size(); ++i) {
            const QJsonObject p = all.at(i).toObject();
            last.append(QJsonArray{ p.value(QLatin1String("id")), p.contains(QLatin1String("source")) ? p.value(QLatin1String("source")) : QJsonValue() });
        }
        EXPECT_EQ(last, c.value(QLatin1String("expect")).toArray());
    }
    for (const auto& v : section("counts")) {
        const QJsonObject c = v.toObject();
        QStringList kinds;
        const QJsonArray given = c.value(QLatin1String("kinds")).toArray();
        for (const auto& k : given) kinds << k.toString();
        QJsonArray got;
        for (const auto& [kind, n] : cp::counts(kinds)) got.append(QJsonObject{ { QStringLiteral("kind"), kind }, { QStringLiteral("count"), n } });
        EXPECT_EQ(got, c.value(QLatin1String("expect")).toArray());
    }
    for (const auto& v : section("carTypes"))
        EXPECT_EQ(cp::sameCarType(v.toObject().value(QLatin1String("a")).toString(), v.toObject().value(QLatin1String("b")).toString()),
                  v.toObject().value(QLatin1String("expect")).toBool());
    for (const auto& v : section("capacity")) {
        const QJsonObject c = v.toObject();
        QJsonObject spot;
        if (!c.value(QLatin1String("capacity")).isNull()) spot.insert(QStringLiteral("capacity"), c.value(QLatin1String("capacity")));
        const QJsonValue usable = c.value(QLatin1String("usable"));
        const auto got = cp::spotCapacity(spot, usable.isDouble() ? std::optional(usable.toDouble()) : std::nullopt);
        EXPECT_EQ(got ? QJsonValue(*got) : QJsonValue(QJsonValue::Null), c.value(QLatin1String("expect")));
    }
}

TEST(Places, TheSidecarKeepsThemOnAModuleAndOnTheLayout) {
    core::Sidecar sc;
    core::Module m;
    m.id = QStringLiteral("m1");
    m.places = QJsonArray{ cannery(QStringLiteral("pl-1")) };
    sc.modules.push_back(m);
    sc.layoutPlaces = QJsonArray{ cannery(QStringLiteral("pl-2")) };
    EXPECT_FALSE(sc.isEmpty());
    core::Sidecar back;
    saveload::sidecarFromJson(saveload::sidecarToJson(sc), back);
    EXPECT_EQ(back.modules.at(0).places, m.places);
    EXPECT_EQ(back.layoutPlaces, sc.layoutPlaces);
    EXPECT_TRUE(back.extras.isEmpty());
    // A file's ids renumbered: the spots follow.
    saveload::renameSidecarIds(back, { { QStringLiteral("B1"), QStringLiteral("B9") } });
    EXPECT_EQ(cp::bricksOf(back.layoutPlaces.at(0).toObject()), QStringList{ QStringLiteral("B9") });
    EXPECT_EQ(cp::bricksOf(back.modules.at(0).places.at(0).toObject()), QStringList{ QStringLiteral("B9") });
    // Outside the limits: put within them, and a module without any has no field.
    QJsonObject big = cannery(QStringLiteral("pl-3"));
    big.insert(QStringLiteral("name"), QString(500, QLatin1Char('x')));
    saveload::sidecarFromJson({ { QStringLiteral("layoutPlaces"), QJsonArray{ big, 5 } },
                                { QStringLiteral("modules"), QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("m") }, { QStringLiteral("places"), QStringLiteral("junk") } } } } },
                              back);
    EXPECT_TRUE(cp::validPlaces(back.layoutPlaces));
    EXPECT_EQ(back.layoutPlaces.size(), 1);
    EXPECT_FALSE(saveload::sidecarToJson(back).value(QLatin1String("modules")).toArray().at(0).toObject().contains(QLatin1String("places")));
}

TEST(Places, AModuleFileKeepsThemNextToIt) {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("depot.bbm"));
    {
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<Map/>");
    }
    core::Map m;
    EXPECT_TRUE(saveload::writeModulePlaces(file, m));
    EXPECT_FALSE(QFile::exists(saveload::sidecarPathFor(file)));  // none: no file made
    m.sidecar.layoutPlaces = QJsonArray{ cannery(QStringLiteral("pl-1")) };
    ASSERT_TRUE(saveload::writeModulePlaces(file, m));
    core::Map back;
    saveload::readModulePlaces(file, back);
    EXPECT_EQ(back.sidecar.layoutPlaces, m.sidecar.layoutPlaces);
}
