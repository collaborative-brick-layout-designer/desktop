// Other people's edits on a live layout reach the map converting only what
// they changed: the shared document's parts, text, areas and rulers that
// are as they were keep what they turned into last time (sync::MapReader).
// The tests count the entries converted per update (never wall time); the
// bench at the end prints milliseconds per remote edit when BLD_BENCH is set.

#include "FakeSyncServer.h"
#include "ui/LiveLayout.h"

#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QGraphicsScene>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <functional>
#include <vector>

using namespace bld;
using namespace bld::synctest;
using Status = sync::SyncClient::Status;

namespace {

core::LayerBrick* biggestBrickLayer(core::Map& map) {
    core::LayerBrick* best = nullptr;
    for (auto& L : map.layers()) {
        if (L->kind() != core::LayerKind::Brick) continue;
        auto* bl = static_cast<core::LayerBrick*>(L.get());
        if (!best || bl->bricks.size() > best->bricks.size()) best = bl;
    }
    return best;
}

int brickCount(const core::Map& map) {
    int n = 0;
    for (const auto& L : map.layers())
        if (L->kind() == core::LayerKind::Brick)
            n += static_cast<int>(static_cast<const core::LayerBrick&>(*L).bricks.size());
    return n;
}

// The Fordyce 2026 layout (940 parts, text and a ruler) made `n` parts big:
// its biggest sheet copied side by side, or cut short.
std::unique_ptr<core::Map> fordyceOf(int n) {
    auto loaded = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/fordyce-2026.bbm"));
    if (!loaded.ok()) return nullptr;
    auto map = std::move(loaded.map);
    core::LayerBrick* layer = biggestBrickLayer(*map);
    if (!layer) return nullptr;
    const std::vector<core::Brick> original = layer->bricks;
    QRectF box;
    for (const auto& b : original) box = box.united(b.displayArea);
    for (int copy = 1; brickCount(*map) < n; ++copy) {
        const QString tag = QStringLiteral("-c%1").arg(copy);
        for (const auto& b : original) {
            if (brickCount(*map) >= n) break;
            core::Brick c = b;
            c.guid += tag;
            c.myGroupId.clear();
            c.displayArea.translate((box.width() + 64.0) * copy, 0.0);
            for (auto& cp : c.connections) {
                if (!cp.guid.isEmpty()) cp.guid += tag;
                if (!cp.linkedToId.isEmpty()) cp.linkedToId += tag;
            }
            layer->bricks.push_back(std::move(c));
        }
    }
    while (brickCount(*map) > n && !layer->bricks.empty()) layer->bricks.pop_back();
    return map;
}

double threadCpuMs();

// A live layout of `n` parts open in a map view, and the server's side of
// it: edits made there (as another person) reach the view.
struct Live {
    FakeServer server;
    parts::PartsLibrary parts;
    ui::MapView view{ parts };
    ui::LiveLayout live{ view };
    std::unique_ptr<core::Map> theirs; // the other person's copy
    int reloads = 0;
    // From sending the last change to the view showing it: on the clock,
    // and on this thread's CPU (not counting the time it waited for one on
    // a busy machine).
    double lastMs = 0, lastCpuMs = 0;

    explicit Live(int n, bool scanParts = false) {
        if (scanParts) {
            parts.addSearchPath(QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts"));
            parts.scan();
        }
        QObject::connect(&live, &ui::LiveLayout::mapReloaded, [this] { ++reloads; });
        auto map = fordyceOf(n);
        if (!map) return;
        server.doc.reset();
        server.doc.writeMap(*map);
        theirs = sync::mapFromDocJson(server.doc.toJson());
    }
    bool open() {
        view.resize(1200, 800);
        live.open(server.url(), QStringLiteral("bld_pat_test"), false, QStringLiteral("Big"));
        return waitFor([&] { return live.session().status() == Status::Synced && view.currentMap(); }, 30000);
    }
    // The other person's change to their copy, sent; true once the view
    // has reloaded with it.
    bool send(const QByteArray& update) {
        if (update.isEmpty()) return false;
        // What the last change left for later (the scene's index, timers)
        // done first, so it isn't counted for this one.
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 30) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        const int before = reloads;
        const QByteArray message = protocol::encode(Kind::Update, update);
        QElapsedTimer t;
        const double cpu0 = threadCpuMs();
        t.start();
        for (QWebSocket* p : server.peers) p->sendBinaryMessage(message);
        while (reloads == before) {
            if (t.elapsed() > 10000) return false;
            QCoreApplication::processEvents(QEventLoop::AllEvents);
        }
        lastMs = static_cast<double>(t.nsecsElapsed()) / 1e6;
        lastCpuMs = threadCpuMs() - cpu0;
        return true;
    }
    bool edit(const std::function<void(core::Map&)>& fn) {
        fn(*theirs);
        return send(server.doc.writeMap(*theirs));
    }
    bool undo() {
        const QByteArray u = server.doc.undo();
        theirs = sync::mapFromDocJson(server.doc.toJson());
        return send(u);
    }
    core::Map& shown() { return *view.currentMap(); }
};

// `count` new parts copied from the first ones of the biggest sheet, a
// little to the right, with new ids (a paste).
void pasteInto(core::Map& m, int count, const QString& tag) {
    core::LayerBrick* l = biggestBrickLayer(m);
    std::vector<core::Brick> added;
    for (int i = 0; i < count && i < static_cast<int>(l->bricks.size()); ++i) {
        core::Brick b = l->bricks[static_cast<size_t>(i)];
        b.guid = QStringLiteral("%1-%2").arg(tag).arg(i);
        b.myGroupId.clear();
        b.displayArea.translate(0, 400);
        for (auto& c : b.connections) {
            c.guid = QStringLiteral("%1-%2-%3").arg(tag).arg(i).arg(c.guid);
            c.linkedToId.clear();
        }
        added.push_back(std::move(b));
    }
    l->bricks.insert(l->bricks.end(), added.begin(), added.end());
}

double threadCpuMs() {
#if defined(Q_OS_UNIX)
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * 1e3 + static_cast<double>(ts.tv_nsec) / 1e6;
#else
    static QElapsedTimer since; // (the clock where there's no thread CPU time)
    if (!since.isValid()) since.start();
    return static_cast<double>(since.nsecsElapsed()) / 1e6;
#endif
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

} // namespace

namespace {

QGraphicsItem* partItem(ui::MapView& view, const QString& guid) {
    for (QGraphicsItem* it : view.scene()->items())
        if (ui::detail::isBrickItem(it) && it->data(ui::detail::kBrickDataGuid).toString() == guid) return it;
    return nullptr;
}

const core::Brick* brickIn(const core::Map& m, const QString& guid) {
    for (const auto& L : m.layers())
        if (L->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                if (b.guid == guid) return &b;
    return nullptr;
}

template <typename T> T* firstOf(core::Map& m, core::LayerKind kind) {
    for (auto& L : m.layers())
        if (L->kind() == kind) return static_cast<T*>(L.get());
    return nullptr;
}

} // namespace

// Another person's move, paste, undo and delete each show on the map, and
// each converts only the entries it changed: one part, the hundred pasted,
// none for parts taken away. The document is never read whole for them.
TEST(RemoteEdit, EachRemoteEditShowsOnTheMapConvertingOnlyWhatItChanged) {
    Live l(1000);
    ASSERT_TRUE(l.theirs) << "fordyce-2026.bbm missing";
    ASSERT_TRUE(l.open());
    ASSERT_EQ(brickCount(l.shown()), 1000);
    const int wholeReads = sync::SyncDoc::wholeReads();
    qint64 converted = 0;
    const auto counted = [&](const std::function<bool()>& step) {
        const qint64 before = sync::MapReader::entriesConverted();
        const bool ok = step();
        converted = sync::MapReader::entriesConverted() - before;
        return ok;
    };
    const double px = rendering::SceneBuilder::kPixelsPerStud;

    // A part moved.
    const QString moved = biggestBrickLayer(*l.theirs)->bricks[500].guid;
    ASSERT_TRUE(counted([&] {
        return l.edit([&](core::Map& m) { biggestBrickLayer(m)->bricks[500].displayArea.translate(8, 0); });
    }));
    EXPECT_EQ(converted, 1) << "the part moved, not the whole layout";
    const core::Brick* shown = brickIn(l.shown(), moved);
    ASSERT_NE(shown, nullptr);
    EXPECT_EQ(shown->displayArea, brickIn(*l.theirs, moved)->displayArea);
    ASSERT_NE(partItem(l.view, moved), nullptr);
    EXPECT_EQ(partItem(l.view, moved)->pos(), shown->displayArea.center() * px) << "drawn where it is now";

    // A hundred parts pasted.
    ASSERT_TRUE(
        counted([&] { return l.edit([&](core::Map& m) { pasteInto(m, 100, QStringLiteral("paste")); }); }));
    EXPECT_EQ(converted, 100);
    EXPECT_EQ(brickCount(l.shown()), 1100);
    for (int i = 0; i < 100; ++i) {
        const QString guid = QStringLiteral("paste-%1").arg(i);
        ASSERT_NE(brickIn(l.shown(), guid), nullptr) << i;
        EXPECT_NE(partItem(l.view, guid), nullptr) << "pasted part " << i << " is drawn";
    }

    // Their undo takes the paste away again.
    const QByteArray undo = l.server.doc.undo();
    ASSERT_TRUE(counted([&] { return l.send(undo); }));
    EXPECT_EQ(converted, 0);
    l.theirs = sync::mapFromDocJson(l.server.doc.toJson());
    EXPECT_EQ(brickCount(l.shown()), 1000);
    EXPECT_EQ(brickIn(l.shown(), QStringLiteral("paste-0")), nullptr);
    EXPECT_EQ(partItem(l.view, QStringLiteral("paste-0")), nullptr);

    // A part deleted.
    const QString gone = biggestBrickLayer(*l.theirs)->bricks[300].guid;
    ASSERT_TRUE(counted([&] {
        return l.edit([&](core::Map& m) {
            auto& bricks = biggestBrickLayer(m)->bricks;
            bricks.erase(bricks.begin() + 300);
        });
    }));
    EXPECT_EQ(converted, 0);
    EXPECT_EQ(brickCount(l.shown()), 999);
    EXPECT_EQ(brickIn(l.shown(), gone), nullptr);
    EXPECT_EQ(partItem(l.view, gone), nullptr) << "no longer drawn";
    EXPECT_NE(partItem(l.view, moved), nullptr) << "the others stay";

    // A text cell and a ruler changed: just that one each.
    if (firstOf<core::LayerText>(*l.theirs, core::LayerKind::Text)
        && !firstOf<core::LayerText>(*l.theirs, core::LayerKind::Text)->textCells.empty()) {
        ASSERT_TRUE(counted([&] {
            return l.edit([](core::Map& m) {
                firstOf<core::LayerText>(m, core::LayerKind::Text)->textCells[0].text =
                    QStringLiteral("Changed by Sam");
            });
        }));
        EXPECT_EQ(converted, 1);
        EXPECT_EQ(firstOf<core::LayerText>(l.shown(), core::LayerKind::Text)->textCells[0].text,
                  QStringLiteral("Changed by Sam"));
    }
    if (firstOf<core::LayerRuler>(*l.theirs, core::LayerKind::Ruler)
        && !firstOf<core::LayerRuler>(*l.theirs, core::LayerKind::Ruler)->rulers.empty()) {
        ASSERT_TRUE(counted([&] {
            return l.edit([](core::Map& m) {
                auto& r = firstOf<core::LayerRuler>(m, core::LayerKind::Ruler)->rulers[0];
                r.linear.offsetDistance += 2;
                r.circular.radius += 1;
            });
        }));
        EXPECT_EQ(converted, 1);
    }
    EXPECT_EQ(sync::SyncDoc::wholeReads(), wholeReads) << "the kept document JSON follows every change";
}

// Milliseconds per remote edit at 1,000 and 5,000 parts (BLD_BENCH=1 to
// run), from the server sending the change to the map showing it: a part
// moved, a 100-part paste, its undo, a part deleted. Each on the clock and
// on this thread's CPU (on a busy machine the clock counts waiting for one
// too). "whole convert" is turning the whole document into a map once, as
// every remote edit did before MapReader.
TEST(RemoteEditBench, MillisecondsPerRemoteEdit) {
    if (qEnvironmentVariableIsEmpty("BLD_BENCH")) GTEST_SKIP() << "set BLD_BENCH=1 to print the timings";
    for (const int n : { 1000, 5000 }) {
        Live l(n, true);
        ASSERT_TRUE(l.theirs) << "fordyce-2026.bbm missing";
        ASSERT_TRUE(l.open());
        ASSERT_EQ(brickCount(l.shown()), n);
        std::vector<double> move, paste, undo, remove, convert;
        std::vector<double> moveCpu, pasteCpu, undoCpu, removeCpu;
        const auto took = [&l](std::vector<double>& wall, std::vector<double>& cpu) {
            wall.push_back(l.lastMs);
            cpu.push_back(l.lastCpuMs);
        };
        for (int rep = 0; rep < 15; ++rep) {
            core::LayerBrick* tl = biggestBrickLayer(*l.theirs);
            const size_t at = tl->bricks.size() / 3 + static_cast<size_t>(rep);
            ASSERT_TRUE(
                l.edit([&](core::Map& m) { biggestBrickLayer(m)->bricks[at].displayArea.translate(8, 0); }));
            took(move, moveCpu);
            ASSERT_TRUE(l.edit([&](core::Map& m) { pasteInto(m, 100, QStringLiteral("p%1").arg(rep)); }));
            took(paste, pasteCpu);
            ASSERT_TRUE(l.undo());
            took(undo, undoCpu);
            ASSERT_TRUE(l.edit([&](core::Map& m) {
                auto& bricks = biggestBrickLayer(m)->bricks;
                bricks.erase(bricks.begin() + static_cast<std::ptrdiff_t>(bricks.size() / 2));
            }));
            took(remove, removeCpu);
            const QJsonObject doc = l.server.doc.toJson();
            const double cpu0 = threadCpuMs();
            (void)sync::mapFromDocJson(doc);
            convert.push_back(threadCpuMs() - cpu0);
        }
        std::printf(
            "[bench] %d parts, ms on the clock (on the CPU): remote move %.2f (%.2f), paste 100 %.2f (%.2f), "
            "undo paste %.2f (%.2f), delete %.2f (%.2f) | whole convert (%.2f)\n",
            brickCount(l.shown()), median(move), median(moveCpu), median(paste), median(pasteCpu),
            median(undo), median(undoCpu), median(remove), median(removeCpu), median(convert));
        std::fflush(stdout);
    }
}

// For a profiler: many remote moves at 5,000 parts (BLD_BENCH=profile-remote).
TEST(RemoteEditBench, ManyRemoteMovesForAProfiler) {
    if (qEnvironmentVariable("BLD_BENCH") != QLatin1String("profile-remote")) GTEST_SKIP();
    Live l(5000, true);
    ASSERT_TRUE(l.theirs) << "fordyce-2026.bbm missing";
    ASSERT_TRUE(l.open());
    std::vector<double> ms;
    for (int rep = 0; rep < 200; ++rep) {
        const size_t at = static_cast<size_t>(rep * 13 % 4000);
        ASSERT_TRUE(
            l.edit([&](core::Map& m) { biggestBrickLayer(m)->bricks[at].displayArea.translate(8, 0); }));
        ms.push_back(l.lastCpuMs);
    }
    std::printf("[bench] profile-remote: move %.2f ms on the CPU\n", median(ms));
}
