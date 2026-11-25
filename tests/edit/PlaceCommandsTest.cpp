// Places (core/Places.h) as modules are made, taken apart, copied, added from
// the Module library and updated from it: each one undo step; a shared
// module's are its owner's.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/ModuleEdit.h"
#include "core/Places.h"
#include "core/SharedModule.h"
#include "edit/ModuleCommands.h"
#include "edit/ModuleLibraryLink.h"
#include "edit/PlaceCommands.h"
#include "edit/SharedLock.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QUndoStack>

using namespace bld;
namespace cp = core::places;

namespace {

core::Brick brick(const QString& guid, double x) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = QStringLiteral("2865.8");
    b.displayArea = QRectF(x, 0, 16, 8);
    return b;
}

QJsonObject place(const QString& id, const QString& brickId, const QString& name = QStringLiteral("Acme Cannery")) {
    QJsonArray spots;
    if (!brickId.isEmpty())
        spots.append(QJsonObject{ { QStringLiteral("id"), QStringLiteral("s1") },
                                  { QStringLiteral("name"), QStringLiteral("Dock") },
                                  { QStringLiteral("track"), QJsonObject{ { QStringLiteral("brick"), brickId }, { QStringLiteral("from"), 0 }, { QStringLiteral("to"), 1 } } } });
    return { { QStringLiteral("id"), id },          { QStringLiteral("name"), name },          { QStringLiteral("kind"), QStringLiteral("industry") },
             { QStringLiteral("spots"), spots },     { QStringLiteral("accepts"), QJsonArray() }, { QStringLiteral("ships"), QJsonArray() },
             { QStringLiteral("tags"), QJsonArray() }, { QStringLiteral("notes"), QString() } };
}

std::unique_ptr<core::Map> layout() {
    auto map = std::make_unique<core::Map>();
    auto l = std::make_unique<core::LayerBrick>();
    l->guid = QStringLiteral("L");
    l->name = QStringLiteral("Track");
    l->bricks = { brick(QStringLiteral("a"), 0), brick(QStringLiteral("b"), 16) };
    map->layers().push_back(std::move(l));
    return map;
}

QStringList ids(const QJsonArray& places) {
    QStringList out;
    for (const QJsonValue& p : places) out << p.toObject().value(QLatin1String("id")).toString();
    out.sort();
    return out;
}
QString brickOf(const QJsonArray& places) { return cp::bricksOf(places.at(0).toObject()).value(0); }

}  // namespace

TEST(PlaceCommands, APlaceIsSetAndDeletedAsOneStepEach) {
    auto map = layout();
    QUndoStack undo;
    undo.push(new edit::SetPlaceCommand(*map, {}, QStringLiteral("p1"), place(QStringLiteral("p1"), QStringLiteral("a")), QStringLiteral("New place")));
    undo.push(new edit::SetPlaceCommand(*map, {}, QStringLiteral("p1"), place(QStringLiteral("p1"), QStringLiteral("a"), QStringLiteral("Mill")), QStringLiteral("Rename")));
    EXPECT_EQ(map->sidecar.layoutPlaces.at(0).toObject().value(QLatin1String("name")).toString(), QStringLiteral("Mill"));
    // Someone else's place arrives meanwhile: undo leaves it.
    map->sidecar.layoutPlaces = cp::withPlace(map->sidecar.layoutPlaces, place(QStringLiteral("p2"), {}));
    undo.undo();
    undo.undo();
    EXPECT_EQ(ids(map->sidecar.layoutPlaces), QStringList{ QStringLiteral("p2") });
    undo.redo();
    EXPECT_EQ(ids(map->sidecar.layoutPlaces), (QStringList{ QStringLiteral("p1"), QStringLiteral("p2") }));
}

TEST(PlaceCommands, MakingAModuleTakesThePlacesOnItsTrackAndUngroupingGivesThemBack) {
    auto map = layout();
    map->sidecar.layoutPlaces = { place(QStringLiteral("p1"), QStringLiteral("a")), place(QStringLiteral("p2"), {}) };
    QUndoStack undo;
    auto* make = new edit::CreateModuleCommand(*map, QStringLiteral("Depot"), { { 0, QStringLiteral("a") }, { 0, QStringLiteral("b") } });
    const QString id = make->moduleId();
    undo.push(make);
    EXPECT_EQ(ids(map->sidecar.modules.at(0).places), QStringList{ QStringLiteral("p1") });
    EXPECT_EQ(ids(map->sidecar.layoutPlaces), QStringList{ QStringLiteral("p2") });
    undo.push(new edit::FlattenModuleCommand(*map, id));
    EXPECT_EQ(ids(map->sidecar.layoutPlaces), (QStringList{ QStringLiteral("p1"), QStringLiteral("p2") }));
    undo.undo();
    EXPECT_EQ(ids(map->sidecar.modules.at(0).places), QStringList{ QStringLiteral("p1") });
    EXPECT_EQ(ids(map->sidecar.layoutPlaces), QStringList{ QStringLiteral("p2") });
    undo.undo();
    EXPECT_EQ(ids(map->sidecar.layoutPlaces), (QStringList{ QStringLiteral("p1"), QStringLiteral("p2") }));
}

TEST(PlaceCommands, ADuplicatesPlacesAreOnItsOwnTrackUnderIdsOfTheirOwn) {
    auto map = layout();
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Depot");
    m.memberIds = { QStringLiteral("a"), QStringLiteral("b") };
    m.places = { place(QStringLiteral("p1"), QStringLiteral("b")) };
    map->sidecar.modules.push_back(m);
    QUndoStack undo;
    undo.push(new edit::CloneModuleCommand(*map, m.id, QPointF(40, 0), QString()));
    const core::Module copy = map->sidecar.modules.at(1);
    ASSERT_EQ(copy.places.size(), 1);
    EXPECT_EQ(copy.places.at(0).toObject().value(QLatin1String("source")).toString(), QStringLiteral("p1"));
    EXPECT_NE(ids(copy.places), QStringList{ QStringLiteral("p1") });
    EXPECT_TRUE(copy.memberIds.contains(brickOf(copy.places)));
    undo.undo();
    undo.redo();
    EXPECT_EQ(map->sidecar.modules.at(1).places, copy.places);  // the same ids again
}

TEST(PlaceCommands, FromTheModuleLibraryAndUpdatedFromIt) {
    auto map = layout();
    // The library's module: its own parts, its place on the second.
    core::Map lib;
    auto l = std::make_unique<core::LayerBrick>();
    l->guid = QStringLiteral("X");
    l->name = QStringLiteral("Track");
    l->bricks = { brick(QStringLiteral("x1"), 0), brick(QStringLiteral("x2"), 16) };
    lib.layers().push_back(std::move(l));
    lib.sidecar.layoutPlaces = { place(QStringLiteral("lib-1"), QStringLiteral("x2")) };

    QUndoStack undo;
    auto* import = new edit::ImportBbmAsModuleCommand(*map, {}, QStringLiteral("Depot"), edit::batchesOfModule(lib));
    import->setLibrary(QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b"), 1);
    const QString id = import->moduleId();
    undo.push(import);
    const QJsonArray first = core::findModule(map->sidecar.modules, id)->places;
    ASSERT_EQ(first.size(), 1);
    const QString here = ids(first).at(0);
    EXPECT_NE(here, QStringLiteral("lib-1"));
    EXPECT_TRUE(core::findModule(map->sidecar.modules, id)->memberIds.contains(brickOf(first)));
    undo.undo();
    undo.redo();
    EXPECT_EQ(core::findModule(map->sidecar.modules, id)->places, first);

    // Version 2 renames it: the same place here, on the new parts; undo brings version 1's back.
    lib.sidecar.layoutPlaces = { place(QStringLiteral("lib-1"), QStringLiteral("x1"), QStringLiteral("Cannery Row")) };
    auto batches = edit::batchesOfModule(lib);
    for (auto& b : batches) b.targetLayerGuid = QStringLiteral("L");
    undo.push(new edit::ReplaceModulePartsCommand(*map, id, batches, QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b"), 2));
    const QJsonArray second = core::findModule(map->sidecar.modules, id)->places;
    EXPECT_EQ(ids(second), QStringList{ here });
    EXPECT_EQ(second.at(0).toObject().value(QLatin1String("name")).toString(), QStringLiteral("Cannery Row"));
    EXPECT_TRUE(core::findModule(map->sidecar.modules, id)->memberIds.contains(brickOf(second)));
    undo.undo();
    EXPECT_EQ(core::findModule(map->sidecar.modules, id)->places, first);

    // A version without any: an ordinary module keeps its own (without the track that went); a shared one has none.
    lib.sidecar.layoutPlaces = {};
    batches = edit::batchesOfModule(lib);
    for (auto& b : batches) b.targetLayerGuid = QStringLiteral("L");
    undo.push(new edit::ReplaceModulePartsCommand(*map, id, batches, QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b"), 3));
    EXPECT_EQ(ids(core::findModule(map->sidecar.modules, id)->places), QStringList{ here });
    EXPECT_TRUE(brickOf(core::findModule(map->sidecar.modules, id)->places).isEmpty());
    undo.undo();
    for (auto& m : map->sidecar.modules)
        if (m.id == id) m.shared = QJsonObject{ { QStringLiteral("by"), QStringLiteral("Alice") } };
    undo.push(new edit::ReplaceModulePartsCommand(*map, id, batches, QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b"), 3));
    EXPECT_TRUE(core::findModule(map->sidecar.modules, id)->places.isEmpty());
}

TEST(PlaceCommands, ASharedModulesPlacesKeepTheLock) {
    core::Module was;
    was.id = QStringLiteral("m1");
    was.name = QStringLiteral("Crane");
    was.memberIds = { QStringLiteral("a") };
    was.libraryModuleId = QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b");
    was.libraryVersion = 1;
    was.shared = QJsonObject{ { QStringLiteral("by"), QStringLiteral("Alice") } };
    was.places = { place(QStringLiteral("p1"), QStringLiteral("a")) };
    core::Module next = was;
    EXPECT_FALSE(core::keepsLock(was, next).has_value());
    next.places = {};
    EXPECT_EQ(core::keepsLock(was, next), core::LockedAction::Places);
    EXPECT_TRUE(core::lockMessage(QStringLiteral("Crane"), QStringLiteral("Alice"), core::LockedAction::Places).contains(QStringLiteral("places are Alice's")));
}
