// Two desktops on one live layout, and two people editing one module at
// once: each desktop is a MapView with its LiveLayout, both on the
// in-process server (FakeSyncServer.h), which relays what one sends to the
// other as the real one does. The full review (area 4) ran browsers and one
// desktop; these pairs it left out.
//
// What must hold after every exchange: both desktops and the server show
// the same layout, nobody's edit is lost unless someone else changed the
// very same thing, and no part or module is there twice.

#include "ui/LiveLayout.h"
#include "ui/ModuleEditBar.h"
#include "FakeSyncServer.h"

#include "core/ModuleEdit.h"
#include "edit/EditCommands.h"
#include "edit/ModuleCommands.h"
#include "parts/PartsLibrary.h"
#include "ui/MapView.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QMouseEvent>
#include <QUndoStack>

#include <map>

using namespace bld;
using namespace bld::synctest;
using namespace std::chrono_literals;
using Status = sync::SyncClient::Status;

namespace {

core::LayerBrick& bricksOf(core::Map& m) {
    for (auto& l : m.layers())
        if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l);
    throw std::runtime_error("no bricks");
}
int brickLayer(core::Map& m) {
    for (int i = 0; i < static_cast<int>(m.layers().size()); ++i)
        if (m.layers()[i]->kind() == core::LayerKind::Brick) return i;
    return -1;
}
core::Brick* brick(core::Map& m, const QString& guid) {
    for (auto& b : bricksOf(m).bricks)
        if (b.guid == guid) return &b;
    return nullptr;
}

// One desktop: the map view and its live layout.
struct Desk {
    parts::PartsLibrary parts;
    ui::MapView view{ parts };
    ui::LiveLayout live{ view };

    void open(FakeServer& server, const QString& userId, const QString& name) {
        live.open(server.url(), QStringLiteral("bld_pat_test"), false, QStringLiteral("Show 2026"));
        live.setUser(userId, name, QStringLiteral("L1"));
        live.session().client().setReconnectDelays(50ms, 200ms);
        ASSERT_TRUE(waitFor([&] { return live.session().status() == Status::Synced && view.currentMap(); }));
    }
    core::Map& map() { return *view.currentMap(); }
    // A move as a drag commits it (studs).
    void move(const QString& guid, double dx, double dy) {
        core::Map& m = map();
        const QPointF at = brick(m, guid)->displayArea.topLeft();
        view.undoStack()->push(new edit::MoveBricksCommand(m, { { { brickLayer(m), guid }, at, at + QPointF(dx, dy) } }));
    }
    void remove(const QString& guid) {
        core::Map& m = map();
        auto& bricks = bricksOf(m).bricks;
        for (int i = 0; i < static_cast<int>(bricks.size()); ++i)
            if (bricks[i].guid == guid) {
                view.undoStack()->push(new edit::DeleteBricksCommand(m, { { brickLayer(m), i, bricks[i] } }));
                return;
            }
        ADD_FAILURE() << "no part " << guid.toStdString();
    }
    void mouse(QEvent::Type type) {
        QMouseEvent e(type, QPointF(10, 10), view.viewport()->mapToGlobal(QPointF(10, 10)), Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &e);
    }
    const core::Module* module(const QString& id) { return core::findModule(map().sidecar.modules, id); }
};

// A layout as one comparable value: every part's place and turn by id, and
// every module's name and parts by id. A part or module there twice fails.
struct Shown {
    std::map<QString, std::pair<QRectF, float>> parts;
    std::map<QString, std::pair<QString, QStringList>> modules;
    bool operator==(const Shown&) const = default;
};
Shown shown(core::Map& m) {
    Shown s;
    for (auto& l : m.layers()) {
        if (l->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*l).bricks) {
            EXPECT_FALSE(s.parts.contains(b.guid)) << "part " << b.guid.toStdString() << " is there twice";
            s.parts[b.guid] = { b.displayArea, b.orientation };
        }
    }
    for (const auto& mod : m.sidecar.modules) {
        EXPECT_FALSE(s.modules.contains(mod.id)) << "module " << mod.id.toStdString() << " is there twice";
        QStringList members(mod.memberIds.begin(), mod.memberIds.end());
        members.sort();
        s.modules[mod.id] = { mod.name, members };
    }
    return s;
}

class TwoDesktops : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(a.open(server, QStringLiteral("u-ann"), QStringLiteral("Ann")));
        ASSERT_NO_FATAL_FAILURE(b.open(server, QStringLiteral("u-ben"), QStringLiteral("Ben")));
        const auto& bricks = bricksOf(a.map()).bricks;
        ASSERT_GE(bricks.size(), 3U);
        p0 = bricks[0].guid;
        p1 = bricks[1].guid;
        p2 = bricks[2].guid;
        at0 = bricks[0].displayArea;
        at1 = bricks[1].displayArea;
        at2 = bricks[2].displayArea;
        // (Every sheet's parts: shown() counts them all.)
        count = shown(a.map()).parts.size();
    }
    Shown onServer() { return shown(*sync::mapFromDocJson(server.doc.toJson())); }
    // Both desktops and the server show the same layout (and stay so).
    ::testing::AssertionResult settled() {
        const auto same = [&] { return shown(a.map()) == onServer() && shown(b.map()) == onServer(); };
        if (!waitFor(same)) return ::testing::AssertionFailure() << "the desktops and the server don't show the same layout";
        waitFor([] { return false; }, 150);
        if (!same()) return ::testing::AssertionFailure() << "the layouts drifted apart again";
        return ::testing::AssertionSuccess();
    }
    QRectF placeOf(Desk& d, const QString& guid) { return brick(d.map(), guid)->displayArea; }
    // A module of the first part, made on desk A, there on both.
    QString moduleOnBoth() {
        auto* create = new edit::CreateModuleCommand(a.map(), QStringLiteral("Harbour"), { { brickLayer(a.map()), p0 } });
        const QString id = create->moduleId();
        a.view.undoStack()->push(create);
        EXPECT_TRUE(waitFor([&] { return a.module(id) && b.module(id); }));
        return id;
    }

    FakeServer server;
    Desk a, b;
    QString p0, p1, p2;
    QRectF at0, at1, at2;
    std::size_t count = 0;
};

}  // namespace

TEST_F(TwoDesktops, EachShowsWhatTheOtherDoes) {
    a.move(p0, 8, 0);
    ASSERT_TRUE(waitFor([&] { return placeOf(b, p0) == at0.translated(8, 0); }));
    b.move(p1, 0, 16);
    ASSERT_TRUE(waitFor([&] { return placeOf(a, p1) == at1.translated(0, 16); }));
    b.remove(p2);
    ASSERT_TRUE(waitFor([&] { return brick(a.map(), p2) == nullptr; }));
    EXPECT_TRUE(settled());
    EXPECT_EQ(shown(a.map()).parts.size(), count - 1);
}

TEST_F(TwoDesktops, DifferentPartsMovedAtTheSameInstantBothStay) {
    // Before either hears of the other's.
    a.move(p0, 8, 0);
    b.move(p1, 0, 16);
    ASSERT_TRUE(settled());
    EXPECT_EQ(placeOf(a, p0), at0.translated(8, 0));
    EXPECT_EQ(placeOf(a, p1), at1.translated(0, 16));
    EXPECT_EQ(placeOf(b, p0), at0.translated(8, 0));
    EXPECT_EQ(placeOf(b, p1), at1.translated(0, 16));
}

TEST_F(TwoDesktops, TheSamePartMovedAtTheSameInstantEndsTheSameEverywhere) {
    for (int round = 0; round < 5; ++round) {
        a.move(p0, 8, 0);
        b.move(p0, 0, 16);
        ASSERT_TRUE(settled()) << "round " << round;
        // One part, not two, wherever it ended.
        EXPECT_EQ(shown(a.map()).parts.size(), count) << "round " << round;
    }
}

TEST_F(TwoDesktops, OneDeletesAPartTheOtherMovesAtTheSameInstant) {
    a.remove(p0);
    b.move(p0, 0, 16);
    ASSERT_TRUE(settled());
    // Gone or moved, the same for both, and never there twice (shown() checks).
    const Shown s = shown(a.map());
    EXPECT_TRUE(s.parts.size() == count || s.parts.size() == count - 1) << s.parts.size();
    // The other parts weren't touched.
    EXPECT_EQ(placeOf(a, p1), at1);
    EXPECT_EQ(placeOf(a, p2), at2);
}

TEST_F(TwoDesktops, UndoOnOneLeavesTheOthersEdit) {
    a.move(p0, 8, 0);
    ASSERT_TRUE(waitFor([&] { return placeOf(b, p0) == at0.translated(8, 0); }));
    b.move(p1, 0, 16);
    ASSERT_TRUE(waitFor([&] { return placeOf(a, p1) == at1.translated(0, 16); }));
    ASSERT_TRUE(waitFor([&] { return a.live.canUndo(); }));
    EXPECT_TRUE(a.live.undo());
    ASSERT_TRUE(settled());
    EXPECT_EQ(placeOf(b, p0), at0);
    EXPECT_EQ(placeOf(b, p1), at1.translated(0, 16));
    // And B's own undo takes back only B's.
    ASSERT_TRUE(b.live.canUndo());
    EXPECT_TRUE(b.live.undo());
    ASSERT_TRUE(settled());
    EXPECT_EQ(placeOf(a, p1), at1);
}

// Ann holds the mouse down on her map (a drag) while Ben edits: his change
// waits on her screen until she lets go. Her own drag, ending, must not
// take his change back.
TEST_F(TwoDesktops, WhatTheOtherDidDuringMyDragIsStillThereWhenILetGo) {
    a.mouse(QEvent::MouseButtonPress);
    b.move(p1, 0, 16);
    b.remove(p2);
    ASSERT_TRUE(waitFor([&] {
        const Shown s = onServer();
        return s.parts.at(p1).first == at1.translated(0, 16) && !s.parts.contains(p2);
    }));
    // Time for it to reach Ann's desktop; her map waits.
    waitFor([] { return false; }, 200);
    EXPECT_EQ(placeOf(a, p1), at1);
    // Her drag ends: the move is committed, then the button comes up.
    a.move(p0, 8, 0);
    a.mouse(QEvent::MouseButtonRelease);
    ASSERT_TRUE(settled());
    const Shown s = onServer();
    EXPECT_EQ(s.parts.at(p0).first, at0.translated(8, 0)) << "Ann's own move";
    EXPECT_EQ(s.parts.at(p1).first, at1.translated(0, 16)) << "Ben's move was taken back";
    EXPECT_FALSE(s.parts.contains(p2)) << "the part Ben deleted came back";
}

// One desktop loses its connection, both go on editing different parts,
// and it comes back: nothing clashes, so both edits are there without a
// question asked.
TEST_F(TwoDesktops, OneOfflineForAWhileThenBothEditsAreThere) {
    ASSERT_EQ(server.peers.size(), 2U);
    // (A connected first.)
    closeWith(server.peers[0], QWebSocketProtocol::CloseCodeGoingAway, {});
    ASSERT_TRUE(waitFor([&] { return a.live.session().status() != Status::Synced; }));
    a.move(p0, 8, 0);
    b.move(p1, 0, 16);
    ASSERT_TRUE(waitFor([&] { return a.live.session().status() == Status::Synced; }));
    ASSERT_TRUE(waitFor([&] { return a.live.session().unsyncedEdits() == 0; }));
    ASSERT_TRUE(settled());
    EXPECT_EQ(placeOf(b, p0), at0.translated(8, 0));
    EXPECT_EQ(placeOf(a, p1), at1.translated(0, 16));
    EXPECT_EQ(a.live.statusText(), QStringLiteral("Connected"));
}

// ── One module, two people ──

TEST_F(TwoDesktops, BothEditingOneModuleSeeEachOtherThere) {
    const QString id = moduleOnBoth();
    a.view.setEditingModule(id);
    b.view.setEditingModule(id);
    ASSERT_NE(a.view.moduleEditBar(), nullptr);
    ASSERT_NE(b.view.moduleEditBar(), nullptr);
    ASSERT_TRUE(waitFor([&] { return a.view.moduleEditBar()->others() == QStringLiteral("Ben is here too"); }))
        << a.view.moduleEditBar()->others().toStdString();
    ASSERT_TRUE(waitFor([&] { return b.view.moduleEditBar()->others() == QStringLiteral("Ann is here too"); }))
        << b.view.moduleEditBar()->others().toStdString();
    // One leaves: the other's bar says so.
    b.view.setEditingModule({});
    ASSERT_TRUE(waitFor([&] { return a.view.moduleEditBar()->others().isEmpty(); }))
        << a.view.moduleEditBar()->others().toStdString();
}

TEST_F(TwoDesktops, OneAddsAPartToTheModuleTheOtherRenamesIt) {
    const QString id = moduleOnBoth();
    a.view.setEditingModule(id);
    b.view.setEditingModule(id);
    // One after the other, as two people talking would.
    a.view.undoStack()->push(new edit::SetModuleMembersCommand(a.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    ASSERT_TRUE(waitFor([&] { return b.module(id) && b.module(id)->memberIds == QSet<QString>{ p0, p1 }; }));
    b.view.undoStack()->push(new edit::RenameModuleCommand(b.map(), id, QStringLiteral("Harbour East")));
    ASSERT_TRUE(waitFor([&] { return a.module(id) && a.module(id)->name == QStringLiteral("Harbour East"); }));
    ASSERT_TRUE(settled());
    EXPECT_EQ(a.module(id)->memberIds, (QSet<QString>{ p0, p1 }));
    // Both are still editing it: nobody was put out by the other's change.
    EXPECT_EQ(a.view.editingModule(), id);
    EXPECT_EQ(b.view.editingModule(), id);
}

TEST_F(TwoDesktops, AMemberMovedByOneWhileTheOtherAddsAPartBothStay) {
    const QString id = moduleOnBoth();
    a.view.setEditingModule(id);
    b.view.setEditingModule(id);
    // At the same instant: a part's place (the map) and the module's list.
    a.move(p0, 8, 0);
    b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    ASSERT_TRUE(settled());
    EXPECT_EQ(placeOf(b, p0), at0.translated(8, 0));
    EXPECT_EQ(a.module(id)->memberIds, (QSet<QString>{ p0, p1 }));
}

// One deletes a part at the same instant the other adds it to a module
// (the 11-11 checks): the module doesn't go on naming a part that's gone,
// on either desktop or in the layout the server keeps.
TEST_F(TwoDesktops, APartDeletedAsTheOtherAddsItToAModuleDropsOutOfTheModule) {
    const QString id = moduleOnBoth();
    b.view.setEditingModule(id);
    a.remove(p1);
    b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    ASSERT_TRUE(settled());
    for (Desk* d : { &a, &b }) {
        EXPECT_EQ(brick(d->map(), p1), nullptr);
        ASSERT_NE(d->module(id), nullptr);
        EXPECT_EQ(d->module(id)->memberIds, QSet<QString>{ p0 });
    }
    const auto onServer = sync::mapFromDocJson(server.doc.toJson());
    ASSERT_NE(core::findModule(onServer->sidecar.modules, id), nullptr);
    EXPECT_EQ(core::findModule(onServer->sidecar.modules, id)->memberIds, QSet<QString>{ p0 });
    // The next change written from here writes the module without it too.
    a.view.undoStack()->push(new edit::RenameModuleCommand(a.map(), id, QStringLiteral("Harbour East")));
    ASSERT_TRUE(settled());
    const QJsonArray mods = server.doc.toJson().value(QLatin1String("meta")).toObject().value(QLatin1String("cache")).toObject().value(QLatin1String("modules")).toArray();
    ASSERT_EQ(mods.size(), 1);
    EXPECT_EQ(mods[0].toObject().value(QLatin1String("members")).toArray(), QJsonArray{ p0 });
}

// A module's parts merge: each part added to a module or taken out of it is
// a key of its own in the shared layout (sync/ModuleMembers.h, doc shape 3),
// so when two desktops change the same module at the very same instant
// (before either hears of the other), both changes stay. (Before that the
// module was one value, and one of the two changes was lost.)

// Both add a part to the same module at the same instant.
TEST_F(TwoDesktops, BothAddingAPartToTheModuleAtTheSameInstantBothPartsAreInIt) {
    const QString id = moduleOnBoth();
    a.view.setEditingModule(id);
    b.view.setEditingModule(id);
    a.view.undoStack()->push(new edit::SetModuleMembersCommand(a.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p0, p2 }, QStringLiteral("Add to module")));
    ASSERT_TRUE(settled());
    ASSERT_NE(a.module(id), nullptr);
    EXPECT_EQ(a.module(id)->memberIds, (QSet<QString>{ p0, p1, p2 }));
    EXPECT_EQ(b.module(id)->memberIds, (QSet<QString>{ p0, p1, p2 }));
    // Both parts are still on the map, each once.
    EXPECT_EQ(shown(a.map()).parts.size(), count);
    // And both people are still editing the module.
    EXPECT_EQ(a.view.editingModule(), id);
    EXPECT_EQ(b.view.editingModule(), id);
    // The layout is doc shape 3 from here: builds that don't know the keys refuse it.
    EXPECT_EQ(server.doc.toJson().value(QLatin1String("meta")).toObject().value(QLatin1String("schemaVersion")).toInt(), 3);
}

// One adds a part while the other takes one out, at the same instant.
TEST_F(TwoDesktops, OneAddsAPartAsTheOtherTakesOneOutBothHappen) {
    const QString id = moduleOnBoth();
    a.view.undoStack()->push(new edit::SetModuleMembersCommand(a.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    ASSERT_TRUE(waitFor([&] { return b.module(id) && b.module(id)->memberIds == QSet<QString>{ p0, p1 }; }));
    ASSERT_TRUE(settled());
    for (int round = 0; round < 3; ++round) {
        // A takes p0 out while B adds p2; then A puts p0 back while B takes p2 out.
        a.view.undoStack()->push(new edit::SetModuleMembersCommand(a.map(), id, { p1 }, QStringLiteral("Remove from module")));
        b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p0, p1, p2 }, QStringLiteral("Add to module")));
        ASSERT_TRUE(settled()) << "round " << round;
        EXPECT_EQ(a.module(id)->memberIds, (QSet<QString>{ p1, p2 })) << "round " << round;
        a.view.undoStack()->push(new edit::SetModuleMembersCommand(a.map(), id, { p0, p1, p2 }, QStringLiteral("Add to module")));
        b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p1 }, QStringLiteral("Remove from module")));
        ASSERT_TRUE(settled()) << "round " << round;
        EXPECT_EQ(b.module(id)->memberIds, (QSet<QString>{ p0, p1 })) << "round " << round;
    }
    EXPECT_EQ(shown(a.map()).parts.size(), count);
}

// One renames the module while the other adds a part to it, at the same instant.
TEST_F(TwoDesktops, ARenameAndAnAddedPartAtTheSameInstantBothStay) {
    const QString id = moduleOnBoth();
    a.view.undoStack()->push(new edit::RenameModuleCommand(a.map(), id, QStringLiteral("Harbour East")));
    b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    ASSERT_TRUE(settled());
    for (Desk* d : { &a, &b }) {
        ASSERT_NE(d->module(id), nullptr);
        EXPECT_EQ(d->module(id)->name, QStringLiteral("Harbour East"));
        EXPECT_EQ(d->module(id)->memberIds, (QSet<QString>{ p0, p1 }));
    }
}

// Undo on one desktop takes back its own part and leaves the part the other
// added at the same instant; Redo puts it back.
TEST_F(TwoDesktops, UndoOfMyAddedPartLeavesThePartTheOtherAdded) {
    const QString id = moduleOnBoth();
    a.view.undoStack()->push(new edit::SetModuleMembersCommand(a.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p0, p2 }, QStringLiteral("Add to module")));
    ASSERT_TRUE(settled());
    ASSERT_EQ(a.module(id)->memberIds, (QSet<QString>{ p0, p1, p2 }));
    ASSERT_TRUE(waitFor([&] { return a.live.canUndo(); }));
    EXPECT_TRUE(a.live.undo());
    ASSERT_TRUE(settled());
    EXPECT_EQ(a.module(id)->memberIds, (QSet<QString>{ p0, p2 }));
    EXPECT_EQ(b.module(id)->memberIds, (QSet<QString>{ p0, p2 }));
    ASSERT_TRUE(a.live.canRedo());
    EXPECT_TRUE(a.live.redo());
    ASSERT_TRUE(settled());
    EXPECT_EQ(a.module(id)->memberIds, (QSet<QString>{ p0, p1, p2 }));
    EXPECT_EQ(b.module(id)->memberIds, (QSet<QString>{ p0, p1, p2 }));
    // And B's undo takes back only B's.
    ASSERT_TRUE(b.live.canUndo());
    EXPECT_TRUE(b.live.undo());
    ASSERT_TRUE(settled());
    EXPECT_EQ(a.module(id)->memberIds, (QSet<QString>{ p0, p1 }));
}

// One desktop loses its connection, both change the same module's parts,
// and it comes back: both changes are there, without a question asked.
TEST_F(TwoDesktops, OneOfflineWhileBothChangeTheModulesPartsThenBothChangesAreThere) {
    const QString id = moduleOnBoth();
    ASSERT_TRUE(settled());
    ASSERT_EQ(server.peers.size(), 2U);
    // (A connected first.)
    closeWith(server.peers[0], QWebSocketProtocol::CloseCodeGoingAway, {});
    ASSERT_TRUE(waitFor([&] { return a.live.session().status() != Status::Synced; }));
    a.view.undoStack()->push(new edit::SetModuleMembersCommand(a.map(), id, { p0, p1 }, QStringLiteral("Add to module")));
    b.view.undoStack()->push(new edit::SetModuleMembersCommand(b.map(), id, { p0, p2 }, QStringLiteral("Add to module")));
    b.view.undoStack()->push(new edit::RenameModuleCommand(b.map(), id, QStringLiteral("Harbour East")));
    ASSERT_TRUE(waitFor([&] { return a.live.session().status() == Status::Synced; }));
    ASSERT_TRUE(waitFor([&] { return a.live.session().unsyncedEdits() == 0; }));
    ASSERT_TRUE(settled());
    for (Desk* d : { &a, &b }) {
        ASSERT_NE(d->module(id), nullptr);
        EXPECT_EQ(d->module(id)->memberIds, (QSet<QString>{ p0, p1, p2 }));
        EXPECT_EQ(d->module(id)->name, QStringLiteral("Harbour East"));
    }
    EXPECT_EQ(a.live.statusText(), QStringLiteral("Connected"));
}

// One deletes the module while the other is editing it: the other isn't
// left editing a module that no longer exists.
TEST_F(TwoDesktops, TheModuleDeletedUnderSomeoneEditingIt) {
    const QString id = moduleOnBoth();
    b.view.setEditingModule(id);
    ASSERT_NE(b.view.moduleEditBar(), nullptr);
    a.view.undoStack()->push(new edit::DeleteModuleCommand(a.map(), id));
    ASSERT_TRUE(waitFor([&] { return b.module(id) == nullptr; }));
    ASSERT_TRUE(settled());
    EXPECT_TRUE(waitFor([&] { return b.view.editingModule().isEmpty(); })) << "still editing a module that is gone";
    // The parts are still there, or gone, the same for both (settled), and
    // B can go on editing the layout.
    if (brick(b.map(), p1)) {
        b.move(p1, 0, 16);
        ASSERT_TRUE(waitFor([&] { return brick(a.map(), p1) && placeOf(a, p1) == at1.translated(0, 16); }));
    }
}
