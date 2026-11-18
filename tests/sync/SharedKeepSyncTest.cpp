// Undo and redo across a shared-module update that kept the unchanged
// parts (doc shape 5; edit/SharedSettle.h, SyncSession::finishStep): the
// desktop places a shared module and moves it, then the server puts a new
// version in, keeping two of its three parts. Undoing the move takes the
// whole module back (the new part too), as one change on the server;
// undoing placing it removes the whole module; redo puts it back whole.
// Also: the shape-5 stamp and its mark, and a reader of shape 4 refusing it.

#include "FakeSyncServer.h"
#include "Compat.h"
#include "SyncDoc.h"
#include "SyncSession.h"
#include "WebModel.h"

#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/SettledBack.h"
#include "core/SharedModule.h"
#include "edit/SharedSettle.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

using namespace bld;
using namespace bld::synctest;
using Status = sync::SyncClient::Status;

namespace {

const QString kLib = QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b");

core::LayerBrick& firstSheet(core::Map& m) {
    for (auto& L : m.layers())
        if (L && L->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*L);
    throw std::runtime_error("no parts sheet");
}

core::Module* moduleIn(core::Map& m) {
    for (auto& x : m.sidecar.modules)
        if (x.id == QLatin1String("crane")) return &x;
    return nullptr;
}

// The module's parts on the map, by id: where each is.
QHash<QString, QPointF> partsOf(const core::Map& m) {
    QHash<QString, QPointF> out;
    const core::Module* mod = nullptr;
    for (const auto& x : m.sidecar.modules)
        if (x.id == QLatin1String("crane")) mod = &x;
    if (!mod) return out;
    for (const auto& L : m.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (mod->memberIds.contains(b.guid)) out.insert(b.guid, b.displayArea.topLeft());
    }
    return out;
}

core::Brick part(const QString& id, double x) {
    core::Brick b;
    b.guid = id;
    b.partNumber = QStringLiteral("3001.1");
    b.displayArea = QRectF(x, 0, 2, 2);
    return b;
}

}  // namespace

TEST(SharedKeepSync, UndoAndRedoAcrossAnUpdateThatKeptPartsFinishForTheWholeModule) {
    FakeServer server;
    sync::SyncSession session;
    static parts::PartsLibrary none;
    session.setStepFinisher([](const core::Map& before, core::Map& after, core::SettledBack* removed) {
        return edit::finishShared(before, after, none, removed);
    });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced && session.loaded(); }));

    // 1. Placed: three parts, a shared module.
    auto map = session.currentMap();
    auto& sheet = firstSheet(*map);
    const QString a = core::newBbmId(), b = core::newBbmId(), c = core::newBbmId();
    for (const auto& [id, x] : { std::pair{ a, 0.0 }, std::pair{ b, 4.0 }, std::pair{ c, 8.0 } }) sheet.bricks.push_back(part(id, x));
    core::Module mod;
    mod.id = QStringLiteral("crane");
    mod.name = QStringLiteral("Harbor crane");
    mod.memberIds = { a, b, c };
    mod.libraryModuleId = kLib;
    mod.libraryVersion = 1;
    mod.shared = QJsonObject{ { QStringLiteral("by"), QStringLiteral("Alice") },
                              { QStringLiteral("placement"), core::placementJson({ 0, 0, 0, std::nullopt }) } };
    map->sidecar.modules.push_back(mod);
    session.localEdit(*map);
    // 2. Moved by (16, 8).
    map = session.currentMap();
    for (auto& p : firstSheet(*map).bricks)
        if (p.guid == a || p.guid == b || p.guid == c) p.displayArea.translate(16, 8);
    moduleIn(*map)->shared->insert(QStringLiteral("placement"), core::placementJson({ 0, 16, 8, std::nullopt }));
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return partsOf(*sync::mapFromDocJson(server.doc.toJson())).value(a) == QPointF(16, 8); }));

    // 3. The server puts version 2 in: a and b kept, c swapped for d.
    const QString d = core::newBbmId();
    server.remoteEdit([&](core::Map& m) {
        auto& s = firstSheet(m);
        s.bricks.erase(std::remove_if(s.bricks.begin(), s.bricks.end(), [&](const core::Brick& x) { return x.guid == c; }), s.bricks.end());
        core::Brick nd = part(d, 12 + 16);
        nd.displayArea.translate(0, 8);
        s.bricks.push_back(nd);
        moduleIn(m)->memberIds = { a, b, d };
        moduleIn(m)->libraryVersion = 2;
    });
    ASSERT_TRUE(waitFor([&] { return partsOf(*session.currentMap()).contains(d); }));

    // Undo the move: the whole module goes back, d too; the server has it so.
    const int sent = server.updatesReceived;
    ASSERT_TRUE(session.undo());
    const auto here = partsOf(*session.currentMap());
    EXPECT_EQ(here.value(a), QPointF(0, 0));
    EXPECT_EQ(here.value(b), QPointF(4, 0));
    EXPECT_EQ(here.value(d), QPointF(12, 0));
    ASSERT_TRUE(waitFor([&] { return partsOf(*sync::mapFromDocJson(server.doc.toJson())).value(d) == QPointF(12, 0); }));
    EXPECT_EQ(partsOf(*sync::mapFromDocJson(server.doc.toJson())).value(a), QPointF(0, 0));
    EXPECT_EQ(server.updatesReceived - sent, 1) << "the undo and its finish are one change";

    // Undo placing it: the whole module goes.
    ASSERT_TRUE(session.undo());
    {
        const auto gone = session.currentMap();
        EXPECT_EQ(moduleIn(*gone), nullptr);
        for (const auto& p : firstSheet(*gone).bricks) EXPECT_NE(p.guid, d);
    }
    // Redo: back whole, d included.
    ASSERT_TRUE(session.redo());
    const auto again = partsOf(*session.currentMap());
    EXPECT_EQ(again.size(), 3);
    EXPECT_EQ(again.value(d), QPointF(12, 0));
}

TEST(SharedKeepSync, UndoingAChangeOfSheetAfterAnUpdateBringsBackNoPartTheUpdateTookOut) {
    FakeServer server;
    sync::SyncSession session;
    static parts::PartsLibrary none;
    session.setStepFinisher([](const core::Map& before, core::Map& after, core::SettledBack* removed) {
        return edit::finishShared(before, after, none, removed);
    });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced && session.loaded(); }));
    const auto sheetsOf = [](const core::Map& m, const QSet<QString>& ids) {
        QSet<QString> out;
        for (const auto& L : m.layers())
            if (L && L->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                    if (ids.contains(b.guid)) out.insert(L->guid);
        return out;
    };
    const auto alive = [](const core::Map& m, const QString& id) {
        for (const auto& L : m.layers())
            if (L && L->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                    if (b.guid == id) return true;
        return false;
    };

    // Placed on the first sheet, and a second sheet made.
    auto map = session.currentMap();
    const QString a = core::newBbmId(), b = core::newBbmId(), c = core::newBbmId();
    const QString home = firstSheet(*map).guid;
    for (const auto& [id, x] : { std::pair{ a, 0.0 }, std::pair{ b, 4.0 }, std::pair{ c, 8.0 } }) firstSheet(*map).bricks.push_back(part(id, x));
    core::Module mod;
    mod.id = QStringLiteral("crane");
    mod.name = QStringLiteral("Harbor crane");
    mod.memberIds = { a, b, c };
    mod.libraryModuleId = kLib;
    mod.libraryVersion = 1;
    mod.shared = QJsonObject{ { QStringLiteral("by"), QStringLiteral("Alice") } };
    map->sidecar.modules.push_back(mod);
    auto hills = std::make_unique<core::LayerBrick>();
    const QString other = hills->guid = core::newBbmId();
    hills->name = QStringLiteral("Hills");
    map->layers().push_back(std::move(hills));
    session.localEdit(*map);
    // The module put on the other sheet (one step).
    map = session.currentMap();
    std::vector<core::Brick> moving;
    std::erase_if(firstSheet(*map).bricks, [&](const core::Brick& x) {
        if (x.guid != a && x.guid != b && x.guid != c) return false;
        moving.push_back(x);
        return true;
    });
    for (auto& L : map->layers())
        if (L && L->guid == other) static_cast<core::LayerBrick&>(*L).bricks = moving;
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return sheetsOf(*sync::mapFromDocJson(server.doc.toJson()), { a, b, c }) == QSet<QString>{ other }; }));

    // The server puts version 2 in there: c taken out (remembered), d in.
    const QString d = core::newBbmId();
    server.remoteEdit([&](core::Map& m) {
        for (auto& L : m.layers()) {
            if (!L || L->guid != other) continue;
            auto& bricks = static_cast<core::LayerBrick&>(*L).bricks;
            std::erase_if(bricks, [&](const core::Brick& x) { return x.guid == c; });
            bricks.push_back(part(d, 12));
        }
        core::Module* x = moduleIn(m);
        x->memberIds = { a, b, d };
        x->libraryVersion = 2;
        x->shared->insert(QStringLiteral("retired"), QJsonArray{ c });
    });
    ASSERT_TRUE(waitFor([&] { return partsOf(*session.currentMap()).contains(d); }));

    // Undo the change of sheet: c doesn't come back; the whole module is on its first sheet again.
    ASSERT_TRUE(session.undo());
    auto now = session.currentMap();
    EXPECT_FALSE(alive(*now, c));
    EXPECT_EQ(sheetsOf(*now, { a, b, d }), QSet<QString>{ home });
    ASSERT_TRUE(waitFor([&] {
        const auto there = sync::mapFromDocJson(server.doc.toJson());
        return !alive(*there, c) && sheetsOf(*there, { a, b, d }) == QSet<QString>{ home };
    }));
    // Redo: all on the other sheet, still without c.
    ASSERT_TRUE(session.redo());
    now = session.currentMap();
    EXPECT_FALSE(alive(*now, c));
    EXPECT_EQ(sheetsOf(*now, { a, b, d }), QSet<QString>{ other });
}

TEST(SharedKeepSync, ShapeFiveIsStampedWithItsMarkAndAShapeFourReaderRefusesIt) {
    EXPECT_EQ(sync::kDocSchemaVersion, 5);
    EXPECT_TRUE(sync::canReadDoc(5));
    EXPECT_FALSE(sync::canReadDoc(5, 4));
    sync::SyncDoc doc;
    doc.applyUpdate(serverDoc());
    EXPECT_FALSE(doc.stampKeepIds().isEmpty());
    const QJsonObject meta = doc.toJson().value(QLatin1String("meta")).toObject();
    EXPECT_EQ(meta.value(QLatin1String("schemaVersion")).toInt(), 5);
    EXPECT_TRUE(meta.value(QLatin1String(sync::kKeptIdsMark)).toBool());
    EXPECT_TRUE(doc.stampKeepIds().isEmpty()) << "once";
    // A lower stamp written at the same moment: the next write raises it again.
    doc.setMeta(QStringLiteral("schemaVersion"), 4);
    auto map = sync::mapFromDocJson(doc.toJson());
    firstSheet(*map).bricks.push_back(part(core::newBbmId(), 40));
    doc.writeMap(*map);
    EXPECT_EQ(doc.toJson().value(QLatin1String("meta")).toObject().value(QLatin1String("schemaVersion")).toInt(), 5);
}
