// Shared (read-only) modules (docs/shared-modules.md): the sidecar value
// (core/SharedModule.h, as the web's packages/modules shared.ts reads it),
// the lock under every edit and the placement that follows a move
// (edit/SharedLock.h), and the owner's version put in for a layout that
// isn't live (edit/SharedModulePlacement.h planVersion, with its undo).

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/ModuleEdit.h"
#include "core/SharedModule.h"
#include "edit/EditCommands.h"
#include "edit/ModuleCommands.h"
#include "edit/SharedLock.h"
#include "edit/SharedModulePlacement.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QUndoStack>

using namespace bld;

namespace {

const QString kLib = QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b");

parts::PartsLibrary& noParts() {
    static parts::PartsLibrary lib;
    return lib;
}

core::Brick brick(const QString& guid, const QString& part, double x, double y, float orientation = 0) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = part;
    b.displayArea = QRectF(x - 1, y - 1, 2, 2);
    b.orientation = orientation;
    return b;
}

QJsonObject sharedBy(const QString& by, const core::SharedPlacement& at) {
    return { { QStringLiteral("by"), by },
             { QStringLiteral("server"), QStringLiteral("https://bricks.example") },
             { QStringLiteral("placement"), core::placementJson(at) } };
}

// A layout with two parts sheets, a loose part, and the shared module "m1"
// of two parts on Track (a.1 at 100,50 and b.1 at 110,50), as version 7 of
// kLib placed at (100, 50).
struct Layout {
    core::Map map;
    QString track, scenery;
    Layout() {
        auto t = std::make_unique<core::LayerBrick>();
        t->guid = track = QStringLiteral("L-track");
        t->name = QStringLiteral("Track");
        t->bricks = { brick("p1", "a.1", 100, 50), brick("p2", "b.1", 110, 50), brick("loose", "c.1", 300, 300) };
        map.layers().push_back(std::move(t));
        auto s = std::make_unique<core::LayerBrick>();
        s->guid = scenery = QStringLiteral("L-scenery");
        s->name = QStringLiteral("Scenery");
        map.layers().push_back(std::move(s));
        core::Module m;
        m.id = QStringLiteral("m1");
        m.name = QStringLiteral("Harbor crane");
        m.memberIds = { QStringLiteral("p1"), QStringLiteral("p2") };
        m.libraryModuleId = kLib;
        m.libraryVersion = 7;
        core::SharedPlacement at{ 0, 100, 50, QMap<QString, QString>{ { QStringLiteral("Track"), track } } };
        m.shared = sharedBy(QStringLiteral("Alice"), at);
        map.sidecar.modules.push_back(m);
    }
    core::LayerBrick& sheet(int i) { return static_cast<core::LayerBrick&>(*map.layers()[static_cast<size_t>(i)]); }
    core::Module& mod() { return map.sidecar.modules.front(); }
    core::SharedPlacement placement() { return *core::sharedPlaced(mod())->placement; }
};

edit::LayerBatches version(std::vector<core::Brick> bricks, const QString& sheet = QStringLiteral("Track")) {
    edit::ImportBbmAsModuleCommand::LayerBatch b;
    b.layerName = sheet;
    for (auto& x : bricks) x.guid.clear();
    b.bricks = std::move(bricks);
    return { b };
}

// What MapView does after every step of the undo stack.
struct Guard {
    Layout& l;
    QUndoStack stack;
    edit::SharedLockState lock;
    QString told;
    explicit Guard(Layout& layout) : l(layout) { lock.rebase(l.map, noParts()); }
    // Pushes the step; false (and the layout as it was) when the lock refused it.
    bool push(QUndoCommand* cmd) {
        const bool exempt = edit::exemptFromSharedLock(cmd);
        stack.push(cmd);
        if (!exempt)
            if (const auto broken = lock.check(l.map)) {
                stack.undo();
                told = broken->message();
                return false;
            }
        lock.keepPlacements(l.map, noParts());
        lock.rebase(l.map, noParts());
        return true;
    }
};

}  // namespace

TEST(SharedModule, ReadsTheLinkAsTheWebDoes) {
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Harbor crane");
    m.memberIds = { QStringLiteral("a") };
    EXPECT_FALSE(core::sharedPlaced(m));
    m.shared = QJsonObject{};
    EXPECT_FALSE(core::hasSharedMark(m)) << "shared needs a module to follow";
    m.libraryModuleId = QStringLiteral("lib-1");
    EXPECT_TRUE(core::hasSharedMark(m)) << "the doc is shape 4 with it";
    EXPECT_FALSE(core::sharedPlaced(m)) << "the lock wants a real module id";
    m.libraryModuleId = kLib;
    m.libraryVersion = 7;
    auto s = core::sharedPlaced(m);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->placedId, QStringLiteral("m1"));
    EXPECT_EQ(s->moduleId, kLib);
    EXPECT_EQ(s->version, 7);
    EXPECT_TRUE(s->by.isEmpty());
    EXPECT_FALSE(s->placement);
    EXPECT_FALSE(s->review);

    m.shared = QJsonObject{ { "by", "Alice" },
                            { "server", "https://bricks.example" },
                            { "placement", QJsonObject{ { "degrees", 90 }, { "x", 1 }, { "y", 2 },
                                                        { "sheets", QJsonObject{ { "Track", "l1" }, { "Bad", 7 }, { "Empty", "" } } } } },
                            { "review", QJsonObject{ { "from", 6 }, { "to", 7 }, { "at", 1234.0 } } },
                            { "later", "kept" } };
    s = core::sharedPlaced(m);
    ASSERT_TRUE(s && s->placement && s->review);
    EXPECT_EQ(s->by, QStringLiteral("Alice"));
    EXPECT_EQ(s->placement->degrees, 90);
    ASSERT_TRUE(s->placement->sheets);
    EXPECT_EQ(*s->placement->sheets, (QMap<QString, QString>{ { "Track", "l1" } }));
    EXPECT_EQ(s->review->from, 6);
    EXPECT_EQ(s->review->to, 7);

    // A placement that isn't one is none; a review needs a version.
    for (const QJsonValue& bad : { QJsonValue(), QJsonValue("x"), QJsonValue(7), QJsonValue(QJsonArray{}), QJsonValue(QJsonObject{}),
                                   QJsonValue(QJsonObject{ { "degrees", "90" }, { "x", 1 }, { "y", 2 } }) })
        EXPECT_FALSE(core::readPlacement(bad));
    m.shared->insert("review", QJsonObject{ { "from", 6 } });
    EXPECT_FALSE(core::sharedPlaced(m)->review);

    // OK takes the review away and keeps the rest; Make it mine takes the link and the lock.
    m = core::withReview(m, core::SharedReview{ 0, 8, 5 });
    EXPECT_EQ(core::sharedPlaced(m)->review->to, 8);
    EXPECT_EQ(core::sharedPlaced(m)->review->from, 0);
    m = core::withReview(m, std::nullopt);
    EXPECT_FALSE(m.shared->contains("review"));
    EXPECT_EQ(m.shared->value("later").toString(), QStringLiteral("kept"));
    const core::Module mine = core::detached(m);
    EXPECT_FALSE(mine.shared);
    EXPECT_TRUE(mine.libraryModuleId.isEmpty());
    EXPECT_EQ(mine.libraryVersion, 0);
    EXPECT_EQ(mine.name, m.name);
}

TEST(SharedModule, ACopyIsAnotherSharedPlacement) {
    Layout l;
    l.mod().shared->insert("review", QJsonObject{ { "from", 6 }, { "to", 7 }, { "at", 1.0 } });
    const core::Module c = core::copyOfPlaced(l.mod(), { QStringLiteral("x"), QStringLiteral("y") }, QStringLiteral("m2"), QPointF(40, 0));
    const auto s = core::sharedPlaced(c);
    ASSERT_TRUE(s);
    EXPECT_EQ(c.name, QStringLiteral("Harbor crane")) << "it keeps its owner's name";
    EXPECT_EQ(s->moduleId, kLib);
    EXPECT_EQ(s->version, 7);
    EXPECT_EQ(s->by, QStringLiteral("Alice"));
    EXPECT_FALSE(s->review);
    ASSERT_TRUE(s->placement);
    EXPECT_DOUBLE_EQ(s->placement->x, 140);
    EXPECT_DOUBLE_EQ(s->placement->y, 50);
    // An ordinary module's copy is as before.
    core::Module plain = l.mod();
    plain.shared.reset();
    const core::Module pc = core::copyOfPlaced(plain, { QStringLiteral("x") }, QStringLiteral("m3"), QPointF());
    EXPECT_EQ(pc.name, QStringLiteral("Harbor crane (copy)"));
    EXPECT_TRUE(pc.libraryModuleId.isEmpty());
}

TEST(SharedModule, SaysWhyInThePlainWordsTheWebUses) {
    EXPECT_EQ(core::lockMessage(QStringLiteral("Harbor crane"), QStringLiteral("Alice"), core::LockedAction::Parts),
              QStringLiteral("“Harbor crane” is shared by Alice, so its parts can't be changed here. You can move it, turn it or remove it as a whole."));
    EXPECT_EQ(core::lockMessage(QStringLiteral("Harbor crane"), QStringLiteral("Alice"), core::LockedAction::Rename),
              QStringLiteral("“Harbor crane” is shared by Alice, so its name is Alice's. You can move it, turn it and change its outline color."));
    EXPECT_EQ(core::lockMessage(QString(), QString(), core::LockedAction::Bend),
              QStringLiteral("This module is a shared module, so its track can't be bent here. You can move and turn the whole module."));
    EXPECT_NE(core::lockMessage(QStringLiteral("X"), QStringLiteral("A"), core::LockedAction::Members),
              core::lockMessage(QStringLiteral("X"), QStringLiteral("A"), core::LockedAction::Dissolve));
}

TEST(SharedLock, MovingTurningPinningRecoloringAndRemovingItWholeAreAllowed) {
    Layout l;
    Guard g(l);
    EXPECT_TRUE(g.push(new edit::MoveModuleCommand(l.map, "m1", QPointF(16, -8))));
    EXPECT_TRUE(g.push(new edit::RotateModuleCommand(l.map, noParts(), "m1", 90)));
    EXPECT_TRUE(g.push(new edit::UpdateModuleCommand(l.map, core::withPinned(l.mod(), true), "Pin")));
    core::Module look = l.mod();
    look.outlineColor = QStringLiteral("#ff0000");
    look.showName = false;
    EXPECT_TRUE(g.push(new edit::UpdateModuleCommand(l.map, look, "Colors")));
    EXPECT_TRUE(g.push(new edit::CloneModuleCommand(l.map, "m1", QPointF(40, 0), "ignored")));
    ASSERT_EQ(l.map.sidecar.modules.size(), 2u);
    EXPECT_TRUE(core::isSharedModule(l.map.sidecar.modules[1])) << "a duplicate is shared too";
    EXPECT_EQ(l.map.sidecar.modules[1].name, QStringLiteral("Harbor crane"));
    // Removed with all its parts.
    EXPECT_TRUE(g.push(edit::deleteModuleWithPartsCommand(l.map, "m1")));
    EXPECT_FALSE(core::findModule(l.map.sidecar.modules, "m1"));
    EXPECT_TRUE(g.told.isEmpty());
    // ...and undone: back, still locked.
    g.stack.undo();
    ASSERT_TRUE(core::findModule(l.map.sidecar.modules, "m1"));
    EXPECT_TRUE(core::isSharedModule(*core::findModule(l.map.sidecar.modules, "m1")));
}

TEST(SharedLock, ItsPartsItsNameAndWhatItHoldsAreNot) {
    Layout l;
    Guard g(l);

    // Renaming: it is its owner's name.
    EXPECT_FALSE(g.push(new edit::RenameModuleCommand(l.map, "m1", "My crane")));
    EXPECT_EQ(l.mod().name, QStringLiteral("Harbor crane"));
    EXPECT_TRUE(g.told.contains(QStringLiteral("so its name is Alice's"))) << g.told.toStdString();

    // Ungrouping it, or deleting the module and leaving its parts.
    EXPECT_FALSE(g.push(new edit::FlattenModuleCommand(l.map, "m1")));
    EXPECT_TRUE(g.told.contains(QStringLiteral("it stays one module"))) << g.told.toStdString();
    EXPECT_FALSE(g.push(new edit::DeleteModuleCommand(l.map, "m1")));
    ASSERT_TRUE(core::isSharedModule(l.mod()));

    // Taking a part out, adding one.
    EXPECT_FALSE(g.push(new edit::SetModuleMembersCommand(l.map, "m1", { QStringLiteral("p1") }, "Take out")));
    EXPECT_TRUE(g.told.contains(QStringLiteral("parts can't be added to it or taken out of it"))) << g.told.toStdString();
    EXPECT_FALSE(g.push(new edit::SetModuleMembersCommand(l.map, "m1", { "p1", "p2", "loose" }, "Add")));
    EXPECT_EQ(l.mod().memberIds.size(), 2);

    // Deleting one of its parts.
    edit::DeleteBricksCommand::Entry e;
    e.layerIndex = 0;
    e.indexInLayer = 0;
    e.brick = l.sheet(0).bricks[0];
    EXPECT_FALSE(g.push(new edit::DeleteBricksCommand(l.map, { e })));
    EXPECT_EQ(l.sheet(0).bricks.size(), 3u);
    EXPECT_TRUE(g.told.contains(QStringLiteral("its parts can't be changed here"))) << g.told.toStdString();

    // Another module can't take its parts.
    EXPECT_FALSE(g.push(new edit::CreateModuleCommand(l.map, "Mine", { { 0, "p1" }, { 0, "loose" } })));
    EXPECT_EQ(l.map.sidecar.modules.size(), 1u);

    // Unlocking it by hand (an old build's write): not through an ordinary step.
    EXPECT_FALSE(g.push(new edit::UpdateModuleCommand(l.map, core::detached(l.mod()), "Unlock")));
    EXPECT_TRUE(core::isSharedModule(l.mod()));

    // A part's number (its color is in it).
    l.sheet(0).bricks[0].partNumber = QStringLiteral("a.4");
    ASSERT_TRUE(g.lock.check(l.map));
    EXPECT_EQ(g.lock.check(l.map)->what, core::LockedAction::Parts);
    l.sheet(0).bricks[0].partNumber = QStringLiteral("a.1");
    l.sheet(0).bricks[1].altitude = 3;
    EXPECT_TRUE(g.lock.check(l.map)) << "its height";
    l.sheet(0).bricks[1].altitude = 0;
    EXPECT_FALSE(g.lock.check(l.map));

    // Everything else in the layout is as free as ever.
    edit::DeleteBricksCommand::Entry loose;
    loose.layerIndex = 0;
    loose.indexInLayer = 2;
    loose.brick = l.sheet(0).bricks[2];
    EXPECT_TRUE(g.push(new edit::DeleteBricksCommand(l.map, { loose })));
}

TEST(SharedLock, ThePlacementFollowsAMoveAndATurn) {
    Layout l;
    Guard g(l);
    ASSERT_TRUE(g.push(new edit::MoveModuleCommand(l.map, "m1", QPointF(16, -8))));
    EXPECT_DOUBLE_EQ(l.placement().x, 116);
    EXPECT_DOUBLE_EQ(l.placement().y, 42);
    EXPECT_DOUBLE_EQ(l.placement().degrees, 0);

    // Turned a quarter about its middle (105+16, 50-8): the version's origin (its part a) goes round it.
    ASSERT_TRUE(g.push(new edit::RotateModuleCommand(l.map, noParts(), "m1", 90)));
    const core::SharedPlacement at = l.placement();
    EXPECT_NEAR(at.degrees, 90, 1e-6);
    // The version (a at its origin, b 10 to its right) placed by `at` is the module as it sits.
    const auto placed = edit::placeVersion(version({ brick("", "a.1", 0, 0), brick("", "b.1", 10, 0) }), at, noParts());
    const auto now = edit::placedParts(l.map, l.mod().memberIds);
    ASSERT_EQ(now.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_NEAR(placed[0].bricks[i].displayArea.center().x(), now[i].displayArea.center().x(), 1e-4);
        EXPECT_NEAR(placed[0].bricks[i].displayArea.center().y(), now[i].displayArea.center().y(), 1e-4);
        EXPECT_NEAR(placed[0].bricks[i].orientation, now[i].orientation, 1e-4);
    }
    // Undo takes the placement back with the parts.
    g.stack.undo();
    g.lock.keepPlacements(l.map, noParts());
    g.lock.rebase(l.map, noParts());
    EXPECT_NEAR(l.placement().degrees, 0, 1e-6);
    EXPECT_NEAR(l.placement().x, 116, 1e-6);

    // Its sheets follow when the whole module changes sheet.
    auto& from = l.sheet(0).bricks;
    auto& to = l.sheet(1).bricks;
    for (auto it = from.begin(); it != from.end();)
        if (l.mod().memberIds.contains(it->guid)) {
            to.push_back(*it);
            it = from.erase(it);
        } else ++it;
    EXPECT_FALSE(g.lock.check(l.map)) << "another sheet, whole, is allowed";
    EXPECT_TRUE(g.lock.keepPlacements(l.map, noParts()));
    EXPECT_EQ(l.placement().sheets->value("Track"), l.scenery);
}

TEST(SharedModulePlacement, TheOwnersNewVersionGoesInWhereTheModuleSitsAndUndoPutsTheOldOneBack) {
    Layout l;
    Guard g(l);
    ASSERT_TRUE(g.push(new edit::MoveModuleCommand(l.map, "m1", QPointF(20, 0))));
    ASSERT_TRUE(g.push(new edit::RotateModuleCommand(l.map, noParts(), "m1", 90)));
    const auto v7 = version({ brick("", "a.1", 0, 0), brick("", "b.1", 10, 0) });
    // Version 8: b moved, c added on a sheet the layout has by another name's case.
    edit::LayerBatches v8 = version({ brick("", "a.1", 0, 0), brick("", "b.1", 10, 4) });
    v8.push_back(version({ brick("", "c.1", 0, 10) }, QStringLiteral(" scenery "))[0]);

    const auto plan = edit::planVersion(l.map, l.mod(), v8, &v7, noParts());
    ASSERT_TRUE(plan);
    EXPECT_NEAR(plan->placement.degrees, 90, 1e-6);
    ASSERT_EQ(plan->batches.size(), 2u);
    EXPECT_EQ(plan->batches[0].targetLayerGuid, l.track) << "the sheet the placement kept";
    EXPECT_EQ(plan->batches[1].targetLayerGuid, l.scenery) << "the layout's sheet of the same name";
    EXPECT_EQ(plan->placement.sheets->value(" scenery "), l.scenery);

    core::Module after = core::withReview(core::withPlacement(l.mod(), plan->placement), core::SharedReview{ 7, 8, 1 });
    auto* cmd = new edit::ReplaceModulePartsCommand(l.map, "m1", plan->batches, kLib, 8);
    cmd->setSharedAfter(*after.shared);
    ASSERT_TRUE(edit::exemptFromSharedLock(cmd)) << "the owner's version is the one change to its parts the lock lets through";
    ASSERT_TRUE(g.push(cmd));
    EXPECT_EQ(l.mod().libraryVersion, 8);
    EXPECT_EQ(l.mod().memberIds.size(), 3);
    EXPECT_EQ(l.sheet(1).bricks.size(), 1u);
    EXPECT_EQ(core::sharedPlaced(l.mod())->review->from, 7);
    EXPECT_TRUE(edit::matchesVersion(v8, edit::placedParts(l.map, l.mod().memberIds), noParts(), plan->placement));
    EXPECT_FALSE(edit::matchesVersion(v7, edit::placedParts(l.map, l.mod().memberIds), noParts(), plan->placement));
    // Its a.1 is where the old a.1 was.
    const auto a = [&] {
        for (const auto& b : l.sheet(0).bricks)
            if (b.partNumber == QStringLiteral("a.1")) return b.displayArea.center();
        return QPointF();
    };
    const QPointF where = a();

    g.stack.undo();
    EXPECT_EQ(l.mod().libraryVersion, 7);
    EXPECT_EQ(l.mod().memberIds, (QSet<QString>{ "p1", "p2" }));
    EXPECT_FALSE(core::sharedPlaced(l.mod())->review);
    EXPECT_NEAR(a().x(), where.x(), 1e-6);
    EXPECT_NEAR(a().y(), where.y(), 1e-6);
    EXPECT_TRUE(l.sheet(1).bricks.empty());
    g.stack.redo();
    EXPECT_EQ(l.mod().libraryVersion, 8);
}

TEST(SharedModulePlacement, AModuleAnOlderAppMovedIsFoundByItsParts) {
    Layout l;
    // Moved without the placement being kept (BlueBrick, an older desktop).
    for (auto& b : l.sheet(0).bricks)
        if (l.mod().memberIds.contains(b.guid)) b.displayArea.translate(64, 32);
    const auto v7 = version({ brick("", "a.1", 0, 0), brick("", "b.1", 10, 0) });
    const auto kept = core::sharedPlaced(l.mod())->placement;
    const core::SharedPlacement found = edit::resolvePlacement(v7, edit::placedParts(l.map, l.mod().memberIds), kept, noParts());
    EXPECT_NEAR(found.x, 164, 1e-6);
    EXPECT_NEAR(found.y, 82, 1e-6);
    EXPECT_EQ(found.sheets, kept->sheets);
    // Still its owner's version, wherever it sits.
    EXPECT_TRUE(edit::matchesVersion(v7, edit::placedParts(l.map, l.mod().memberIds), noParts(), kept));
    // A part changed outside the app: not any more.
    l.sheet(0).bricks[1].displayArea.translate(3, 0);
    EXPECT_FALSE(edit::matchesVersion(v7, edit::placedParts(l.map, l.mod().memberIds), noParts(), kept));
}

TEST(SharedModule, MakeItMineUnlocksItAndUndoLocksItAgain) {
    Layout l;
    Guard g(l);
    auto* cmd = new edit::DetachSharedModuleCommand(l.map, "m1");
    ASSERT_TRUE(g.push(cmd));
    EXPECT_FALSE(l.mod().shared);
    EXPECT_TRUE(l.mod().libraryModuleId.isEmpty());
    const QJsonObject based = l.mod().extras.value("basedOn").toObject();
    EXPECT_EQ(based.value("moduleId").toString(), kLib);
    EXPECT_EQ(based.value("version").toInt(), 7);
    EXPECT_EQ(based.value("by").toString(), QStringLiteral("Alice"));
    // An ordinary module now: its parts change like any other's.
    EXPECT_TRUE(g.push(new edit::RenameModuleCommand(l.map, "m1", "My crane")));
    g.stack.undo();
    g.stack.undo();
    g.lock.rebase(l.map, noParts());
    ASSERT_TRUE(core::isSharedModule(l.mod()));
    EXPECT_FALSE(l.mod().extras.contains("basedOn"));
    EXPECT_FALSE(g.push(new edit::RenameModuleCommand(l.map, "m1", "My crane")));
}
