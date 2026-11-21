// A module's table and power (core/ModuleSetup.h): the vectors the web
// passes too (fixtures/contracts/module-setup.json, the web's
// packages/modules setup.test.ts), and the sidecar keeping them.

#include "core/Module.h"
#include "core/ModuleSetup.h"
#include "core/Sidecar.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace bld;

namespace {

QJsonObject contract() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/module-setup.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

// The summary as the web's setupSummary writes it.
QJsonObject summaryJson(const core::SetupSummary& s) {
    QJsonArray tables;
    for (const auto& g : s.tables) {
        QJsonArray heights;
        for (const auto& h : g.heights)
            heights.append(QJsonObject{ { QStringLiteral("heightMm"), h.heightMm },
                                        { QStringLiteral("measuredTo"), h.toRail ? QStringLiteral("rail") : QStringLiteral("table") },
                                        { QStringLiteral("count"), h.count } });
        tables.append(QJsonObject{ { QStringLiteral("type"), g.type }, { QStringLiteral("count"), g.count }, { QStringLiteral("heights"), heights } });
    }
    return QJsonObject{
        { QStringLiteral("modules"), s.modules },
        { QStringLiteral("power"), QJsonObject{ { QStringLiteral("modules"), s.powerModules },
                                                { QStringLiteral("outlets"), s.outlets },
                                                { QStringLiteral("watts"), s.watts },
                                                { QStringLiteral("noPower"), s.noPower },
                                                { QStringLiteral("notSet"), s.powerNotSet } } },
        { QStringLiteral("tables"), tables },
        { QStringLiteral("tablesNotSet"), s.tablesNotSet },
    };
}

}  // namespace

TEST(ModuleSetup, ReadsTheContractModules) {
    const QJsonArray cases = contract().value(QStringLiteral("read")).toArray();
    ASSERT_GE(cases.size(), 10);
    for (const auto& c : cases) {
        const QJsonObject o = c.toObject();
        const core::ModuleSetup s = core::setupFromJson(o.value(QStringLiteral("module")).toObject());
        const QJsonValue want = o.value(QStringLiteral("setup"));
        const QJsonValue got = s.empty() ? QJsonValue(QJsonValue::Null) : QJsonValue(core::setupJson(s));
        EXPECT_EQ(got, want) << QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
    }
}

TEST(ModuleSetup, ChecksSetupsAsTheServerDoes) {
    for (const auto& c : contract().value(QStringLiteral("valid")).toArray()) {
        const QJsonObject o = c.toObject();
        EXPECT_EQ(core::validSetup(o.value(QStringLiteral("setup"))), o.value(QStringLiteral("valid")).toBool())
            << QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
    }
}

TEST(ModuleSetup, SumsTheLayoutAsTheWebDoes) {
    for (const auto& c : contract().value(QStringLiteral("summary")).toArray()) {
        const QJsonObject o = c.toObject();
        std::vector<core::ModuleSetup> mods;
        for (const auto& m : o.value(QStringLiteral("modules")).toArray()) mods.push_back(core::setupFromJson(m.toObject()));
        EXPECT_EQ(summaryJson(core::setupSummary(mods)), o.value(QStringLiteral("expect")).toObject())
            << QJsonDocument(summaryJson(core::setupSummary(mods))).toJson(QJsonDocument::Compact).toStdString();
    }
}

TEST(ModuleSetup, SetsClearsAndTakesTheLibrarys) {
    core::Module m;
    m.id = QStringLiteral("m");
    core::ModuleSetup s;
    s.table = core::ModuleTable{ QStringLiteral("mils"), 781.04, false };
    s.power = core::ModulePower{ true, 2, 60, QStringLiteral("back") };
    m = core::withSetup(m, s);
    EXPECT_EQ(m.table->value(QStringLiteral("heightMm")).toDouble(), 781.0);
    EXPECT_EQ(core::setupOf(m).power->outlets, 2);
    // A library module with none set leaves the placed module's own.
    EXPECT_EQ(core::setupOf(core::withLibrarySetup(m, {})), core::setupOf(m));
    core::ModuleSetup lib;
    lib.table = core::ModuleTable{ QStringLiteral("baseplate"), std::nullopt, false };
    const core::Module took = core::withLibrarySetup(m, lib);
    EXPECT_EQ(core::setupOf(took).table->type, QStringLiteral("baseplate"));
    EXPECT_FALSE(took.power.has_value());
    EXPECT_EQ(core::outletsOf(core::ModulePower{ true, std::nullopt, std::nullopt, {} }), 1);
    EXPECT_EQ(core::outletsOf(core::ModulePower{ false, 3, std::nullopt, {} }), 0);
    EXPECT_TRUE(core::setupOf(core::withSetup(m, {})).empty());
}

TEST(ModuleSetup, TheSidecarKeepsThemAndWhatANewerBuildAdded) {
    core::Sidecar sc;
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Depot");
    m.table = QJsonObject{ { QStringLiteral("type"), QStringLiteral("custom") }, { QStringLiteral("heightMm"), 900 },
                           { QStringLiteral("legs"), 4 } };
    m.power = QJsonObject{ { QStringLiteral("needed"), false } };
    sc.modules.push_back(m);
    core::Module plain;
    plain.id = QStringLiteral("m2");
    sc.modules.push_back(plain);
    const QJsonObject json = saveload::sidecarToJson(sc);
    const QJsonArray mods = json.value(QStringLiteral("modules")).toArray();
    EXPECT_EQ(mods[0].toObject().value(QStringLiteral("table")).toObject().value(QStringLiteral("legs")).toInt(), 4);
    EXPECT_EQ(mods[0].toObject().value(QStringLiteral("power")).toObject(), (QJsonObject{ { QStringLiteral("needed"), false } }));
    EXPECT_FALSE(mods[1].toObject().contains(QStringLiteral("table")));
    EXPECT_FALSE(mods[1].toObject().contains(QStringLiteral("power")));

    core::Sidecar back;
    saveload::sidecarFromJson(json, back);
    ASSERT_EQ(back.modules.size(), 2u);
    EXPECT_EQ(back.modules[0].table, m.table);
    EXPECT_EQ(back.modules[0].power, m.power);
    EXPECT_TRUE(back.modules[0].extras.isEmpty());
    EXPECT_EQ(core::setupOf(back.modules[0]).table->heightMm, 900.0);
    // Not an object: kept as it is, not read.
    QJsonObject odd = json;
    QJsonArray oddMods = odd.value(QStringLiteral("modules")).toArray();
    QJsonObject first = oddMods[0].toObject();
    first[QStringLiteral("table")] = QStringLiteral("tall");
    oddMods[0] = first;
    odd[QStringLiteral("modules")] = oddMods;
    core::Sidecar oddBack;
    saveload::sidecarFromJson(odd, oddBack);
    EXPECT_FALSE(oddBack.modules[0].table.has_value());
    EXPECT_EQ(oddBack.modules[0].extras.value(QStringLiteral("table")).toString(), QStringLiteral("tall"));
    EXPECT_EQ(saveload::sidecarToJson(oddBack).value(QStringLiteral("modules")).toArray()[0].toObject().value(QStringLiteral("table")).toString(),
              QStringLiteral("tall"));
}
