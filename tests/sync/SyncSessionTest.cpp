// SyncSession (sync phase P4): the rules the editor follows while a live
// layout is open, against the in-process y-websocket server.

#include "FakeSyncServer.h"
#include "Compat.h"
#include "LayoutMerge.h"
#include "ServerRefusal.h"
#include "SyncSession.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include <optional>
#include <stdexcept>
#include <utility>

using namespace bld;
using namespace bld::synctest;
using namespace std::chrono_literals;
using Status = sync::SyncClient::Status;

TEST(SyncSession, LoadsTheLayoutSendsEditsAndReloadsOnRemoteChanges) {
    FakeServer server;
    sync::SyncSession session;
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    EXPECT_FALSE(session.loaded());
    session.open(server.url(), QStringLiteral("bld_pat_test"), false);

    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    EXPECT_TRUE(session.loaded());
    EXPECT_GT(changed, 0);
    auto map = session.currentMap();
    ASSERT_TRUE(map);
    EXPECT_EQ(firstBrick(*map).displayArea, firstBrickArea(server.doc));

    // Our edit reaches the server.
    firstBrick(*map).displayArea.translate(8, 0);
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == firstBrick(*map).displayArea; }));

    // Someone else's edit: the editor is told to reload, and sees it.
    const int before = changed;
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    ASSERT_TRUE(waitFor([&] { return changed > before; }));
    EXPECT_EQ(firstBrick(*session.currentMap()).orientation, 90.0f);
}

TEST(SyncSession, RemoteChangesWaitWhileHeld) {
    FakeServer server;
    sync::SyncSession session;
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));

    // Mid-drag: the change waits, out of the document and unknown to the
    // editor, until the drag ends.
    session.holdRemoteChanges(true);
    const int before = changed;
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 180.0f; });
    waitFor([] { return false; }, 200);
    EXPECT_EQ(changed, before);
    EXPECT_NE(firstBrick(*session.currentMap()).orientation, 180.0f);
    session.holdRemoteChanges(false);
    EXPECT_EQ(changed, before + 1);
    EXPECT_EQ(firstBrick(*session.currentMap()).orientation, 180.0f);
    // Nothing pending: releasing again says nothing.
    session.holdRemoteChanges(true);
    session.holdRemoteChanges(false);
    EXPECT_EQ(changed, before + 1);
}

// The edit a drag ends with is made on the layout as the editor showed it,
// before what others changed during the drag. It must not take their
// changes back: written into a document that already had them, it did
// (the part someone turned went back, a part someone deleted came back).
TEST(SyncSession, AnEditMadeWhileHeldKeepsWhatOthersChangedMeanwhile) {
    FakeServer server;
    sync::SyncSession session;
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    auto map = session.currentMap();
    ASSERT_TRUE(map);
    const QRectF original = firstBrick(*map).displayArea;
    const auto second = [](core::Map& m) -> core::Brick& {
        for (auto& l : m.layers())
            if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l).bricks[1];
        throw std::runtime_error("no bricks");
    };
    const float turned = second(*map).orientation + 90.0f;

    session.holdRemoteChanges(true);
    server.remoteEdit([&](core::Map& m) { second(m).orientation = turned; });
    waitFor([] { return false; }, 200);
    // The drag ends: the editor's layout, which never heard of the turn, with its own move.
    firstBrick(*map).displayArea.translate(8, 0);
    session.localEdit(*map);
    session.holdRemoteChanges(false);

    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == original.translated(8, 0); }));
    const auto onServer = sync::mapFromDocJson(server.doc.toJson());
    EXPECT_EQ(second(*onServer).orientation, turned) << "the other person's turn was taken back";
    // And the editor now shows both.
    const auto shown = session.currentMap();
    EXPECT_EQ(firstBrick(*shown).displayArea, original.translated(8, 0));
    EXPECT_EQ(second(*shown).orientation, turned);
}

// A layout closed while held (the mouse still down) and another opened:
// the new one's first copy must not wait for a release that already came.
TEST(SyncSession, AHoldLeftOverDoesntKeepTheNextLayoutWaiting) {
    FakeServer server;
    sync::SyncSession session;
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    session.holdRemoteChanges(true);
    session.close();
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced && session.loaded(); }));
    // And someone else's change arrives.
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 270.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 270.0f; }));
}

TEST(SyncSession, UndoRevertsOnlyOurOwnEditOnTheServerToo) {
    FakeServer server;
    sync::SyncSession session;
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const QRectF original = firstBrickArea(server.doc);

    EXPECT_FALSE(session.canUndo());
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 16);
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) != original; }));
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 270.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 270.0f; }));

    ASSERT_TRUE(session.canUndo());
    const int before = changed;
    EXPECT_TRUE(session.undo());
    EXPECT_EQ(changed, before + 1);
    // Our move is undone everywhere; their rotation stays.
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == original; }));
    EXPECT_EQ(firstBrick(*sync::mapFromDocJson(server.doc.toJson())).orientation, 270.0f);
    EXPECT_EQ(firstBrick(*session.currentMap()).displayArea, original);

    EXPECT_TRUE(session.redo());
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) != original; }));
}

TEST(SyncSession, ReadOnlySessionsNeverSend) {
    FakeServer server;
    sync::SyncSession session;
    session.open(server.url(), {}, true);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    EXPECT_TRUE(session.readOnly());

    const QJsonObject before = server.doc.toJson();
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(40, 40);
    session.localEdit(*map);
    EXPECT_FALSE(session.canUndo());
    EXPECT_FALSE(session.undo());
    waitFor([] { return false; }, 150);
    EXPECT_EQ(server.doc.toJson(), before);
    // Our copy is unchanged too: the next reload shows the server's layout.
    EXPECT_EQ(firstBrick(*session.currentMap()).displayArea, firstBrickArea(server.doc));
}

TEST(SyncSession, KeepsOfflineEditsApartUntilTheyAreResolved) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    int ready = 0, changed = 0;
    QObject::connect(&session, &sync::SyncSession::offlineEditsReady, [&] { ++ready; });
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const QRectF original = firstBrickArea(server.doc);

    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    EXPECT_EQ(session.unsyncedEdits(), 2);
    ASSERT_TRUE(session.offlineEdits());
    EXPECT_EQ(firstBrick(*sync::merge::mapOf(session.offlineEdits()->base)).displayArea, original);
    EXPECT_FALSE(session.canUndo());

    // Meanwhile someone else turned the same brick: a clash. Back in step,
    // nothing is merged by itself; the editor is asked instead.
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    ASSERT_TRUE(waitFor([&] { return ready == 1; }));
    EXPECT_EQ(session.status(), Status::Synced);
    EXPECT_EQ(firstBrickArea(server.doc), original);
    EXPECT_EQ(session.unsyncedEdits(), 1);  // the one clash
    // Someone else's edit to another brick meanwhile shows in the editor
    // (full review area 4, Bug 5), while mine stays on the clashing brick.
    const int before = changed;
    const auto bricksOf = [](core::Map& m) -> std::vector<core::Brick>& {
        for (auto& l : m.layers())
            if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l).bricks;
        throw std::runtime_error("no bricks");
    };
    server.remoteEdit([&](core::Map& m) { bricksOf(m)[1].displayArea.translate(0, 24); });
    ASSERT_TRUE(waitFor([&] { return changed > before; }));
    {
        const auto shown = session.editorMap();
        const auto onServer = sync::mapFromDocJson(server.doc.toJson());
        EXPECT_EQ(bricksOf(*shown)[1].displayArea, bricksOf(*onServer)[1].displayArea);
        EXPECT_EQ(firstBrick(*shown).displayArea, firstBrick(*map).displayArea);
    }
    // A further edit to a third brick while the clash waits goes out by itself.
    auto more = session.editorMap();
    bricksOf(*more)[2].displayArea.translate(8, 0);
    const QRectF third = bricksOf(*more)[2].displayArea;
    session.localEdit(*more);
    ASSERT_TRUE(waitFor([&] { return bricksOf(*sync::mapFromDocJson(server.doc.toJson()))[2].displayArea == third; }));
    EXPECT_EQ(session.unsyncedEdits(), 1);
    EXPECT_EQ(firstBrickArea(server.doc), original);

    // Resolved: the result goes in as an ordinary edit.
    auto merged = session.currentMap();
    firstBrick(*merged).displayArea = firstBrick(*map).displayArea;
    session.resolveOffline(merged.get());
    EXPECT_FALSE(session.offlineEdits());
    EXPECT_EQ(session.unsyncedEdits(), 0);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == firstBrick(*map).displayArea; }));
    EXPECT_EQ(firstBrick(*sync::mapFromDocJson(server.doc.toJson())).orientation, 90.0f);
    EXPECT_TRUE(session.canUndo());
}

TEST(SyncSession, DiscardingOfflineEditsLeavesTheServerAlone) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const QRectF original = firstBrickArea(server.doc);
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(8, 0);
    session.localEdit(*map);
    // Someone else turned the same brick: a clash, so it waits (an edit
    // nothing clashes with would go by itself).
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    ASSERT_TRUE(session.offlineEdits());
    session.resolveOffline(nullptr);
    EXPECT_FALSE(session.offlineEdits());
    EXPECT_EQ(firstBrick(*session.editorMap()).displayArea, original);
    waitFor([] { return false; }, 100);
    EXPECT_EQ(firstBrickArea(server.doc), original);
}

TEST(SyncSession, TheCacheOpensTheLayoutOfflineAndKeepsOfflineEdits) {
    QTemporaryDir cache;
    QRectF moved;
    {
        FakeServer server;
        sync::SyncSession session;
        session.setCacheDir(cache.path());
        session.client().setReconnectDelays(50ms, 200ms);
        session.open(server.url(), {}, false);
        ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
        server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 45.0f; });
        ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 45.0f; }));
        server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
        ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
        auto map = session.currentMap();
        firstBrick(*map).displayArea.translate(0, 24);
        moved = firstBrick(*map).displayArea;
        session.localEdit(*map);
        session.close();  // then the app crashes, say
    }
    // No server now: the layout opens from the cache, offline edits and all.
    sync::SyncSession session;
    session.setCacheDir(cache.path());
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(QUrl(QStringLiteral("ws://127.0.0.1:1/ws/layout/L1")), {}, false);
    EXPECT_TRUE(session.loaded());
    ASSERT_TRUE(waitFor([&] { return changed > 0; }));
    EXPECT_EQ(firstBrick(*session.currentMap()).orientation, 45.0f);
    ASSERT_TRUE(session.offlineEdits());
    EXPECT_EQ(session.unsyncedEdits(), 1);
    EXPECT_EQ(firstBrick(*session.editorMap()).displayArea, moved);
    session.close();
}

// The copy on disk (layout.ydoc) holding bytes yrs can't take: it isn't
// read, and the layout comes from the server as if there were no copy.
TEST(SyncSession, RejectsMalformedDocumentsWithoutCrashing) {
    QTemporaryDir cache;
    QFile bad(QStringLiteral(BLD_SOURCE_DIR
                             "/fixtures/fuzz-regressions/ydoc-clock-past-the-limit-after-a-gap.bin"));
    ASSERT_TRUE(bad.open(QIODevice::ReadOnly));
    QFile copy(cache.filePath(QStringLiteral("layout.ydoc")));
    ASSERT_TRUE(copy.open(QIODevice::WriteOnly));
    copy.write(bad.readAll());
    copy.close();

    FakeServer server;
    sync::SyncSession session;
    session.setCacheDir(cache.path());
    session.open(server.url(), {}, false);
    EXPECT_FALSE(session.loaded());
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 45.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 45.0f; }));
    session.close();
}

TEST(SyncSession, TellsTheEditorWhenTheServerEndsIt) {
    FakeServer server;
    sync::SyncSession session;
    int code = 0;
    QObject::connect(&session, &sync::SyncSession::ended, [&](int c, const QString&) { code = c; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.dropAll(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("access_revoked"));
    ASSERT_TRUE(waitFor([&] { return code != 0; }));
    EXPECT_EQ(code, static_cast<int>(QWebSocketProtocol::CloseCodePolicyViolated));
}

TEST(SyncSession, SharesPresenceWithEveryoneElseOnTheLayout) {
    FakeServer server;
    sync::SyncSession a, b;
    a.client().setReconnectDelays(50ms, 200ms);
    b.client().setReconnectDelays(80ms, 200ms);
    int changes = 0;
    QObject::connect(&b, &sync::SyncSession::peersChanged, [&] { ++changes; });
    a.open(server.url(), {}, false);
    b.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return a.status() == Status::Synced && b.status() == Status::Synced; }));
    EXPECT_NE(a.clientId(), b.clientId());

    const QJsonObject alice{
        { QStringLiteral("user"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("Alice") } } },
        { QStringLiteral("cursor"), QJsonObject{ { QStringLiteral("x"), 10 }, { QStringLiteral("y"), 20 } } }
    };
    a.setPresence(alice);
    ASSERT_TRUE(waitFor([&] { return b.peers().contains(a.clientId()); }));
    EXPECT_EQ(b.peers().value(a.clientId()), alice);
    EXPECT_TRUE(a.peers().isEmpty()); // not our own
    EXPECT_GT(changes, 0);

    // After a dropped connection A says where it is again.
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return b.peers().isEmpty(); })); // B went offline too: nobody shown
    ASSERT_TRUE(waitFor([&] { return b.peers().contains(a.clientId()); }, 8000));

    // Leaving takes A off B's map.
    a.setPresence(std::nullopt);
    ASSERT_TRUE(waitFor([&] { return b.peers().isEmpty(); }));
}

// A layout written by a newer app (a document schema this build can't
// read): the session stops instead of syncing it, and says why.
TEST(SyncSession, ALayoutFromANewerVersionIsNotSynced) {
    FakeServer server;
    server.doc.setMeta(QStringLiteral("schemaVersion"), sync::kDocSchemaVersion + 1);
    sync::SyncSession session;
    std::optional<std::pair<int, QString>> ended;
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::ended, [&](int code, const QString& why) { ended = { code, why }; });
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(ended->first, sync::kUnreadableDocCode);
    EXPECT_FALSE(session.loaded());
    EXPECT_EQ(changed, 0);
    EXPECT_TRUE(sync::isKnownLiveClose(ended->first, ended->second));
}

TEST(SyncSession, StopsWhenANewerVersionChangesTheLayoutMidSession) {
    FakeServer server;
    sync::SyncSession session;
    std::optional<int> ended;
    QObject::connect(&session, &sync::SyncSession::ended, [&](int code, const QString&) { ended = code; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.remoteMeta(QStringLiteral("schemaVersion"), 99);
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(*ended, sync::kUnreadableDocCode);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Offline; }));
}

// The server turns away an app older than it allows (close code 4426):
// the session ends for good, with words to show, and doesn't retry.
TEST(SyncSession, AnAppTooOldForTheServerStopsForGood) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(20ms, 50ms);
    std::optional<std::pair<int, QString>> ended;
    QObject::connect(&session, &sync::SyncSession::ended, [&](int code, const QString& why) { ended = { code, why }; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.dropAll(4426, QStringLiteral("update_required"));
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(ended->first, 4426);
    EXPECT_TRUE(sync::liveCloseText(4426, ended->second).contains(QStringLiteral("too old")));
    waitFor([] { return false; }, 200);
    EXPECT_EQ(session.status(), Status::Offline);
    EXPECT_TRUE(server.peers.empty());
}

// Opening one live layout after another: the second is only itself, here
// and on its server (it once had the first merged in).
TEST(SyncSession, OpeningAnotherLayoutDoesNotMergeTheOneOpenBefore) {
    QTemporaryDir cacheA;
    QTemporaryDir cacheB;
    FakeServer first;
    FakeServer second;
    first.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 45.0f; });
    second.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    const QRectF secondArea = firstBrickArea(second.doc);

    sync::SyncSession session;
    session.setCacheDir(cacheA.path());
    session.open(first.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(8, 0);
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(first.doc) == firstBrick(*map).displayArea; }));
    session.close();

    session.setCacheDir(cacheB.path());
    session.open(second.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    waitFor([] { return false; }, 300);  // anything we'd wrongly send has reached the server
    EXPECT_EQ(firstBrick(*session.currentMap()).orientation, 90.0f);
    EXPECT_EQ(firstBrick(*session.currentMap()).displayArea, secondArea);
    EXPECT_EQ(firstBrick(*sync::mapFromDocJson(second.doc.toJson())).orientation, 90.0f);
    EXPECT_EQ(firstBrickArea(second.doc), secondArea);
    session.close();

    // And the first opens from its own cache as itself.
    sync::SyncSession again;
    again.setCacheDir(cacheA.path());
    again.open(QUrl(QStringLiteral("ws://127.0.0.1:1/ws/layout/L1")), {}, false);
    EXPECT_TRUE(again.loaded());
    EXPECT_EQ(firstBrick(*again.currentMap()).orientation, 45.0f);
    again.close();
}

TEST(SyncDoc, ResetForgetsTheDocument) {
    FakeServer server;
    sync::SyncDoc doc;
    ASSERT_TRUE(doc.applyUpdate(server.doc.encodeState()));
    ASSERT_TRUE(sync::mapFromDocJson(doc.toJson()));
    doc.reset();
    sync::SyncDoc fresh;
    EXPECT_EQ(doc.toJson(), fresh.toJson());
    ASSERT_TRUE(doc.applyUpdate(server.doc.encodeState()));
    EXPECT_EQ(firstBrickArea(doc), firstBrickArea(server.doc));
}

// Session lifecycle: closing, opening again, a session the server ends.

namespace {
core::LayerBrick& brickLayer(core::Map& m) {
    for (auto& l : m.layers())
        if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l);
    throw std::runtime_error("no brick layer");
}
int bricksIn(const sync::SyncDoc& d) { return static_cast<int>(brickLayer(*sync::mapFromDocJson(d.toJson())).bricks.size()); }
}  // namespace

// Closed straight after an edit (before the once-a-second save): the edit
// is on disk, and the layout opens offline with it.
TEST(SyncSession, ClosingRightAfterAnEditKeepsItInTheCopyOnDisk) {
    QTemporaryDir cache;
    QRectF moved;
    {
        FakeServer server;
        sync::SyncSession session;
        session.setCacheDir(cache.path());
        session.open(server.url(), {}, false);
        ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
        auto map = session.currentMap();
        firstBrick(*map).displayArea.translate(16, 0);
        moved = firstBrick(*map).displayArea;
        session.localEdit(*map);
        session.close();
    }
    sync::SyncSession again;
    again.setCacheDir(cache.path());
    again.open(QUrl(QStringLiteral("ws://127.0.0.1:1/ws/layout/L1")), {}, false);
    ASSERT_TRUE(again.loaded());
    EXPECT_EQ(firstBrick(*again.currentMap()).displayArea, moved);
    EXPECT_FALSE(again.offlineEdits());
    again.close();
}

// Closed with offline edits that clash and opened again on the same session:
// they come back for review once in step, and nothing reaches the server
// before that.
TEST(SyncSession, OfflineEditsSurviveClosingAndOpeningAgain) {
    QTemporaryDir cache;
    FakeServer server;
    sync::SyncSession session;
    session.setCacheDir(cache.path());
    session.client().setReconnectDelays(50ms, 200ms);
    int ready = 0;
    QObject::connect(&session, &sync::SyncSession::offlineEditsReady, [&] { ++ready; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const QRectF original = firstBrickArea(server.doc);
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    session.close();
    EXPECT_EQ(session.status(), Status::Offline);
    ready = 0;
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });  // a clash

    session.open(server.url(), {}, false);
    ASSERT_TRUE(session.offlineEdits());
    ASSERT_TRUE(waitFor([&] { return ready == 1; }));
    EXPECT_EQ(session.unsyncedEdits(), 1);
    EXPECT_EQ(firstBrick(*session.editorMap()).displayArea, firstBrick(*map).displayArea);
    waitFor([] { return false; }, 100);
    EXPECT_EQ(firstBrickArea(server.doc), original);
    session.resolveOffline(map.get());
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == firstBrick(*map).displayArea; }));
    session.close();
}

// A lot changed offline (a whole yard of new parts) and nothing clashes:
// back in step, all of it reaches the server by itself, in one go.
TEST(SyncSession, ManyOfflineEditsAllReachTheServer) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const int before = bricksIn(server.doc);
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    constexpr int kAdded = 3000;
    for (int i = 0; i < kAdded; ++i) {
        core::Brick b;
        b.guid = QStringLiteral("yard-%1").arg(i);
        b.partNumber = QStringLiteral("3001.1");
        b.displayArea = QRectF(8.0 * (i % 100), 8.0 * (i / 100), 8, 8);
        brickLayer(*map).bricks.push_back(b);
        if (i % 500 == 499) session.localEdit(*map);
    }
    EXPECT_EQ(session.unsyncedEdits(), kAdded / 500);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    ASSERT_TRUE(waitFor([&] { return bricksIn(server.doc) == before + kAdded; }, 15000));
    EXPECT_FALSE(session.offlineEdits());
    EXPECT_EQ(brickLayer(*sync::mapFromDocJson(server.doc.toJson())).bricks.back().guid, QStringLiteral("yard-2999"));
    session.close();
}

// The server ends the session (access taken away) with offline edits
// waiting: they stay on disk, and the session doesn't try again.
TEST(SyncSession, ASessionTheServerEndsKeepsTheOfflineEditsOnDisk) {
    QTemporaryDir cache;
    FakeServer server;
    sync::SyncSession session;
    session.setCacheDir(cache.path());
    session.client().setReconnectDelays(50ms, 200ms);
    int ended = 0;
    QObject::connect(&session, &sync::SyncSession::ended, [&](int, const QString&) { ++ended; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    ASSERT_TRUE(session.offlineEdits());
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });  // a clash: it waits
    // Back on a new connection, then that one is ended.
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.dropAll(4404, QStringLiteral("access_revoked"));
    ASSERT_TRUE(waitFor([&] { return ended == 1; }));
    EXPECT_EQ(session.status(), Status::Offline);
    session.close();
    EXPECT_TRUE(QFile::exists(cache.filePath(QStringLiteral("offline.json"))));
    const std::size_t connections = server.authHeaders.size();
    waitFor([] { return false; }, 300);
    EXPECT_EQ(server.authHeaders.size(), connections);
}

// Access goes down to view only while offline edits wait, and the layout
// is closed before the server's copy is in: the edits stay on disk, and the
// layout opens with them again.
TEST(SyncSession, OfflineEditsStayOnDiskWhenClosedRightAfterGoingViewOnly) {
    QTemporaryDir cache;
    QRectF moved;
    {
        FakeServer server;
        sync::SyncSession session;
        session.setCacheDir(cache.path());
        session.client().setReconnectDelays(50ms, 200ms);
        session.open(server.url(), {}, false);
        ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
        server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
        ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
        auto map = session.currentMap();
        firstBrick(*map).displayArea.translate(0, 8);
        moved = firstBrick(*map).displayArea;
        session.localEdit(*map);
        ASSERT_TRUE(session.offlineEdits());
        session.setReadOnly(true);
        session.close();
    }
    sync::SyncSession again;
    again.setCacheDir(cache.path());
    again.open(QUrl(QStringLiteral("ws://127.0.0.1:1/ws/layout/L1")), {}, true);
    ASSERT_TRUE(again.offlineEdits());
    EXPECT_EQ(firstBrick(*again.editorMap()).displayArea, moved);
    again.close();
}

// Back in step after going view only, the offline edits go to the window
// (editsNotTaken) and leave the disk: they aren't offered a second time.
TEST(SyncSession, OfflineEditsHandedToTheWindowLeaveTheDisk) {
    QTemporaryDir cache;
    FakeServer server;
    sync::SyncSession session;
    session.setCacheDir(cache.path());
    session.client().setReconnectDelays(50ms, 200ms);
    int notTaken = 0;
    QObject::connect(&session, &sync::SyncSession::editsNotTaken, [&] { ++notTaken; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    const QString offline = cache.filePath(QStringLiteral("offline.json"));
    ASSERT_TRUE(QFile::exists(offline));
    session.setReadOnly(true);
    EXPECT_TRUE(QFile::exists(offline));
    ASSERT_TRUE(waitFor([&] { return notTaken == 1; }));
    ASSERT_TRUE(session.notTaken());
    EXPECT_EQ(firstBrick(*sync::merge::mapOf(*session.notTaken())).displayArea, firstBrick(*map).displayArea);
    EXPECT_FALSE(QFile::exists(offline));
    session.close();
}

namespace {

using Budget = QMap<QString, int>;

std::optional<Budget> budgetOn(const sync::SyncDoc& d) { return sync::mapFromDocJson(d.toJson())->sidecar.budgetLimits; }

// The session's layout with one budget limit set (or taken out), as an edit.
void setLimit(sync::SyncSession& session, const QString& part, std::optional<int> limit) {
    auto map = session.editorMap();
    if (!map->sidecar.budgetLimits) map->sidecar.budgetLimits = Budget{};
    if (limit) map->sidecar.budgetLimits->insert(part, *limit);
    else map->sidecar.budgetLimits->remove(part);
    session.localEdit(*map);
}

void goOffline(FakeServer& server, sync::SyncSession& session) {
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
}

}  // namespace

// A budget limit set while offline, and nothing else: back in step it
// reaches the server by itself (it was dropped: the compare didn't see it).
TEST(SyncSession, ABudgetChangedOfflineReachesTheServer) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    ASSERT_FALSE(budgetOn(server.doc));

    goOffline(server, session);
    setLimit(session, QStringLiteral("3001.1"), 5);
    EXPECT_EQ(session.unsyncedEdits(), 1);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    ASSERT_TRUE(waitFor([&] { return budgetOn(server.doc) == Budget{ { QStringLiteral("3001.1"), 5 } }; }));
    EXPECT_FALSE(session.offlineEdits());
    EXPECT_EQ(session.currentMap()->sidecar.budgetLimits, (Budget{ { QStringLiteral("3001.1"), 5 } }));
    session.close();
}

// The same while someone else changed the layout meanwhile: the budget goes
// on top of the server's layout, and their change stays.
TEST(SyncSession, ABudgetChangedOfflineReachesTheServerOthersChangedMeanwhile) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));

    goOffline(server, session);
    setLimit(session, QStringLiteral("3001.1"), 5);
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    ASSERT_TRUE(waitFor([&] { return budgetOn(server.doc) == Budget{ { QStringLiteral("3001.1"), 5 } }; }));
    EXPECT_FALSE(session.offlineEdits());
    EXPECT_EQ(firstBrick(*sync::mapFromDocJson(server.doc.toJson())).orientation, 90.0f);
    EXPECT_EQ(firstBrick(*session.currentMap()).orientation, 90.0f);
    session.close();
}

// Two people on one layout. One sets a limit while offline, the other sets
// another part's limit meanwhile: both limits end up with both of them.
TEST(SyncSession, BudgetLimitsForDifferentPartsBothSurviveAReconnect) {
    FakeServer server;
    sync::SyncSession mine, theirs;
    mine.client().setReconnectDelays(50ms, 200ms);
    mine.open(server.url(), {}, false);  // first, so server.peers.front() is mine
    ASSERT_TRUE(waitFor([&] { return mine.status() == Status::Synced; }));
    theirs.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return theirs.status() == Status::Synced; }));
    setLimit(theirs, QStringLiteral("3001.1"), 2);
    setLimit(theirs, QStringLiteral("3002.1"), 3);
    const Budget start{ { QStringLiteral("3001.1"), 2 }, { QStringLiteral("3002.1"), 3 } };
    ASSERT_TRUE(waitFor([&] { return mine.currentMap()->sidecar.budgetLimits == start; }));

    // Only my connection drops.
    closeWith(server.peers.front(), QWebSocketProtocol::CloseCodeGoingAway, {});
    ASSERT_TRUE(waitFor([&] { return mine.status() != Status::Synced; }));
    ASSERT_EQ(theirs.status(), Status::Synced);
    setLimit(mine, QStringLiteral("3001.1"), 5);
    setLimit(mine, QStringLiteral("3003.1"), 8);  // a new row
    setLimit(theirs, QStringLiteral("3002.1"), 7);

    const Budget both{ { QStringLiteral("3001.1"), 5 }, { QStringLiteral("3002.1"), 7 }, { QStringLiteral("3003.1"), 8 } };
    ASSERT_TRUE(waitFor([&] { return budgetOn(server.doc) == both; }));
    ASSERT_TRUE(waitFor([&] { return theirs.currentMap()->sidecar.budgetLimits == both; }));
    EXPECT_FALSE(mine.offlineEdits());
    EXPECT_EQ(mine.editorMap()->sidecar.budgetLimits, both);
    mine.close();
    theirs.close();
}

// Both set the same part's limit: a clash. Nothing is overwritten on either
// side; the review lists it as a budget change, and what's chosen there
// reaches the other person.
TEST(SyncSession, ABudgetLimitBothChangedWaitsForReview) {
    FakeServer server;
    sync::SyncSession mine, theirs;
    mine.client().setReconnectDelays(50ms, 200ms);
    int ready = 0;
    QObject::connect(&mine, &sync::SyncSession::offlineEditsReady, [&] { ++ready; });
    mine.open(server.url(), {}, false);  // first, so server.peers.front() is mine
    ASSERT_TRUE(waitFor([&] { return mine.status() == Status::Synced; }));
    theirs.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return theirs.status() == Status::Synced; }));
    setLimit(theirs, QStringLiteral("3001.1"), 2);
    setLimit(theirs, QStringLiteral("3002.1"), 3);
    ASSERT_TRUE(waitFor([&] { return mine.currentMap()->sidecar.budgetLimits.has_value()
                                  && mine.currentMap()->sidecar.budgetLimits->size() == 2; }));

    closeWith(server.peers.front(), QWebSocketProtocol::CloseCodeGoingAway, {});
    ASSERT_TRUE(waitFor([&] { return mine.status() != Status::Synced; }));
    setLimit(mine, QStringLiteral("3001.1"), 5);
    setLimit(mine, QStringLiteral("3002.1"), std::nullopt);  // no clash: goes by itself
    setLimit(theirs, QStringLiteral("3001.1"), 9);

    ASSERT_TRUE(waitFor([&] { return ready == 1; }));
    EXPECT_EQ(mine.unsyncedEdits(), 1);
    // The server keeps theirs on the clash; the row I took out is gone.
    ASSERT_TRUE(waitFor([&] { return budgetOn(server.doc) == Budget{ { QStringLiteral("3001.1"), 9 } }; }));
    // My editor still shows mine.
    EXPECT_EQ(mine.editorMap()->sidecar.budgetLimits, (Budget{ { QStringLiteral("3001.1"), 5 } }));

    const auto serverNow = sync::merge::snapshotOf(*mine.currentMap());
    const auto changes = sync::merge::compareLayouts(mine.offlineEdits()->base, mine.offlineEdits()->mine, serverNow);
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0].key, QStringLiteral("budget:3001.1"));
    EXPECT_EQ(changes[0].kind, QStringLiteral("budget"));
    EXPECT_EQ(changes[0].status, sync::merge::Status::Conflict);
    EXPECT_EQ(sync::merge::describe(changes[0]).toStdString(),
              "Budget limit for 3001.1 (yours: 5, the server's: 9): you changed it, the server changed it");

    // "Keep mine" in the review.
    const auto merged = sync::merge::mergeLayouts(mine.offlineEdits()->mine, serverNow, changes,
                                                  { { changes[0].key, sync::merge::Choice::Mine } });
    ASSERT_TRUE(merged);
    mine.resolveOffline(merged.get());
    ASSERT_TRUE(waitFor([&] { return budgetOn(server.doc) == Budget{ { QStringLiteral("3001.1"), 5 } }; }));
    ASSERT_TRUE(waitFor([&] { return theirs.currentMap()->sidecar.budgetLimits == Budget{ { QStringLiteral("3001.1"), 5 } }; }));
    mine.close();
    theirs.close();
}
