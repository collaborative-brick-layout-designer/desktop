// A live edit redraws only what it changed: an edit, an undo or redo, or a
// newer copy of the layout from the live server touches the scene items of
// the parts, text, areas and rulers that changed, not every item on the map.
// The tests count the items made and removed (never wall time); the bench
// at the end prints milliseconds per edit when BLD_BENCH is set.

#include "core/AnchoredLabel.h"
#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "edit/EditCommands.h"
#include "edit/LabelCommands.h"
#include "edit/RulerCommands.h"
#include "parts/BrickPlacement.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QTest>
#include <QUndoStack>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

using namespace bld;

namespace {

void scanParts(parts::PartsLibrary& parts) {
    const QString partsRoot = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
    if (QDir(partsRoot).exists() && parts.keys().isEmpty()) {
        parts.addSearchPath(partsRoot);
        parts.scan();
    }
}

core::LayerBrick* biggestBrickLayer(core::Map& map, int* index = nullptr) {
    core::LayerBrick* best = nullptr;
    for (int i = 0; i < static_cast<int>(map.layers().size()); ++i) {
        auto& L = map.layers()[static_cast<size_t>(i)];
        if (L->kind() != core::LayerKind::Brick) continue;
        auto* bl = static_cast<core::LayerBrick*>(L.get());
        if (!best || bl->bricks.size() > best->bricks.size()) {
            best = bl;
            if (index) *index = i;
        }
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
// its biggest sheet copied side by side, or cut short. The same `n` gives the
// same layout, guids and all, every time.
std::unique_ptr<core::Map> fordyceOf(parts::PartsLibrary& parts, int n) {
    const QString fordyce = QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/fordyce-2026.bbm");
    if (!QFile::exists(fordyce)) return nullptr;
    scanParts(parts);
    auto loaded = saveload::readBbm(fordyce);
    if (!loaded.ok()) return nullptr;
    auto map = std::move(loaded.map);
    core::LayerBrick* layer = biggestBrickLayer(*map);
    if (!layer) return nullptr;
    const std::vector<core::Brick> original = layer->bricks;
    QRectF box;
    for (const auto& b : original) box = box.united(b.displayArea);
    for (int copy = 1; brickCount(*map) < n; ++copy) {
        const QString tag = QStringLiteral("-c%1").arg(copy);
        const QPointF shift((box.width() + 64.0) * copy, 0.0);
        for (const auto& b : original) {
            if (brickCount(*map) >= n) break;
            core::Brick c = b;
            c.guid += tag;
            c.myGroupId.clear();
            c.displayArea.translate(shift);
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

// What the live server sends back: the layout as shown (its parts'
// connection ids and links, and the ids reading the file gave text and
// rulers that had none), as a copy of its own.
std::unique_ptr<core::Map> newerCopyOf(parts::PartsLibrary& parts, int n, const core::Map& shown) {
    auto copy = fordyceOf(parts, n);
    for (size_t i = 0; i < copy->layers().size() && i < shown.layers().size(); ++i) {
        core::Layer& to = *copy->layers()[i];
        const core::Layer& from = *shown.layers()[i];
        if (to.kind() == core::LayerKind::Brick) {
            static_cast<core::LayerBrick&>(to).bricks = static_cast<const core::LayerBrick&>(from).bricks;
            static_cast<core::LayerBrick&>(to).groups = static_cast<const core::LayerBrick&>(from).groups;
        } else if (to.kind() == core::LayerKind::Text) {
            static_cast<core::LayerText&>(to).textCells = static_cast<const core::LayerText&>(from).textCells;
        } else if (to.kind() == core::LayerKind::Ruler) {
            static_cast<core::LayerRuler&>(to).rulers = static_cast<const core::LayerRuler&>(from).rulers;
        }
    }
    return copy;
}

struct Shown {
    parts::PartsLibrary& parts;
    ui::MapView view{ parts };
    explicit Shown(parts::PartsLibrary& p) : parts(p) {
        view.resize(1200, 800);
        view.show();
        (void)QTest::qWaitForWindowExposed(&view);
    }
};

double msOf(const std::function<void()>& fn) {
    QElapsedTimer t;
    t.start();
    fn();
    return static_cast<double>(t.nsecsElapsed()) / 1e6;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

} // namespace

namespace {

QGraphicsItem* partItem(QGraphicsScene& scene, const QString& guid) {
    for (QGraphicsItem* it : scene.items())
        if (ui::detail::isBrickItem(it) && it->data(ui::detail::kBrickDataGuid).toString() == guid) return it;
    return nullptr;
}

QPointF drawnAt(const core::Brick& b) {
    return b.displayArea.center() * rendering::SceneBuilder::kPixelsPerStud;
}

int connDots(const QGraphicsItem* item) {
    int n = 0;
    for (const QGraphicsItem* c : item->childItems())
        if (c->data(ui::detail::kBrickDataKind).toString() == QLatin1String("connDot")) ++n;
    return n;
}

// A view showing Fordyce made `n` parts big, and the work counters before an edit.
struct Fixture {
    parts::PartsLibrary parts;
    std::unique_ptr<Shown> shown;
    ui::MapView* view = nullptr;
    int sheet = -1;
    quint64 drawn = 0;
    int builds = 0, updates = 0, indexBuilds = 0;

    bool open(int n) {
        auto map = fordyceOf(parts, n);
        if (!map) return false;
        shown = std::make_unique<Shown>(parts);
        view = &shown->view;
        view->loadMap(std::move(map));
        QApplication::processEvents();
        mark();
        return true;
    }
    core::Map& map() { return *view->currentMap(); }
    core::LayerBrick& layer() { return *biggestBrickLayer(map(), &sheet); }
    // A part of the copies (no ruler, label or group on it).
    core::Brick& copied(int k = 0) {
        for (auto& b : layer().bricks)
            if (b.guid.endsWith(QLatin1String("-c1")) && k-- == 0) return b;
        return layer().bricks.back();
    }
    void mark() {
        drawn = view->builder()->drawnTotal();
        builds = view->sceneBuilds();
        updates = view->sceneUpdates();
        indexBuilds = view->pickIndexBuilds();
    }
    quint64 drawnSince() const { return view->builder()->drawnTotal() - drawn; }
    std::unique_ptr<core::Map> newerCopy(int n) { return newerCopyOf(parts, n, map()); }
    void move(const QString& guid, QPointF by) {
        core::Brick* b = nullptr;
        for (auto& x : layer().bricks)
            if (x.guid == guid) b = &x;
        ASSERT_NE(b, nullptr);
        const QPointF at = b->displayArea.topLeft();
        view->undoStack()->push(new edit::MoveBricksCommand(map(), { { { sheet, guid }, at, at + by } }));
    }
};

} // namespace

// A part moved: only it is drawn again; every other item stays as it was
// (the same item), and nothing is built whole.
TEST(LiveRedraw, AMoveDrawsOnlyThatPartAgain) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString guid = f.copied(3).guid;
    const QString other = f.copied(4).guid;
    QGraphicsItem* otherItem = partItem(*f.view->scene(), other);
    const int items = static_cast<int>(f.view->scene()->items().size());
    f.move(guid, QPointF(16, 0));
    EXPECT_EQ(f.drawnSince(), 1u);
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    EXPECT_EQ(f.view->sceneUpdates(), f.updates + 1);
    EXPECT_EQ(partItem(*f.view->scene(), other), otherItem) << "an untouched part keeps its item";
    EXPECT_EQ(static_cast<int>(f.view->scene()->items().size()), items);
    const core::Brick& moved = f.copied(3);
    ASSERT_EQ(moved.guid, guid);
    EXPECT_EQ(partItem(*f.view->scene(), guid)->pos(), drawnAt(moved)) << "the part shows where it is now";
}

// Undo and redo draw again only the part they put back.
TEST(LiveRedraw, UndoAndRedoDrawOnlyWhatTheyChange) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString guid = f.copied(5).guid;
    const QPointF was = drawnAt(f.copied(5));
    f.move(guid, QPointF(0, 24));
    f.mark();
    f.view->undoStack()->undo();
    EXPECT_EQ(f.drawnSince(), 1u);
    EXPECT_EQ(partItem(*f.view->scene(), guid)->pos(), was);
    f.mark();
    f.view->undoStack()->redo();
    EXPECT_EQ(f.drawnSince(), 1u);
    EXPECT_EQ(partItem(*f.view->scene(), guid)->pos(), was + QPointF(0, 24 * 8));
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
}

// A paste of 100 parts draws those 100; its undo takes them away and draws nothing.
TEST(LiveRedraw, APasteDrawsWhatItAddsAndItsUndoNothing) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    QSet<QString> hundred;
    for (int k = 0; k < 50; ++k) hundred.insert(f.copied(k).guid);
    for (size_t i = 0; i < 50; ++i) hundred.insert(f.layer().bricks[i].guid);
    f.view->selectParts(hundred);
    f.view->copySelection();
    const int bricks = static_cast<int>(f.layer().bricks.size());
    f.mark();
    f.view->pasteClipboard();
    EXPECT_EQ(static_cast<int>(f.layer().bricks.size()), bricks + 100);
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    // The pasted parts, and the parts they joined (their free ends changed).
    EXPECT_GE(f.drawnSince(), 100u);
    EXPECT_LE(f.drawnSince(), 110u);
    EXPECT_EQ(f.view->pickIndexBuilds(), f.indexBuilds) << "the pick index is patched, not built again";
    // The pasted parts are picked.
    int picked = 0;
    for (QGraphicsItem* it : f.view->scene()->selectedItems()) picked += ui::detail::isBrickItem(it) ? 1 : 0;
    EXPECT_EQ(picked, 100);

    f.mark();
    f.view->undoStack()->undo();
    EXPECT_EQ(static_cast<int>(f.layer().bricks.size()), bricks);
    EXPECT_LE(f.drawnSince(), 10u) << "only the parts whose free ends came back";
    int shown = 0;
    for (QGraphicsItem* it : f.view->scene()->items()) shown += ui::detail::isBrickItem(it) ? 1 : 0;
    EXPECT_EQ(shown, brickCount(f.map()));
}

// The pick stays (a picked part drawn again is picked again), and its
// group comes with it: a set picked and moved stays picked whole.
TEST(LiveRedraw, APickedSetMovedStaysPickedWhole) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    // Five copied parts made into a set.
    std::vector<edit::BrickRef> refs;
    QSet<QString> set;
    for (int k = 10; k < 15; ++k) {
        refs.push_back({ f.sheet, f.copied(k).guid });
        set.insert(f.copied(k).guid);
    }
    f.view->undoStack()->push(new edit::GroupBricksCommand(f.map(), refs));
    // The set as made (a part already in a group elsewhere stays out).
    set.clear();
    QString groupId;
    for (const auto& b : f.layer().bricks)
        if (!b.myGroupId.isEmpty() && b.guid.endsWith(QLatin1String("-c1")) && groupId.isEmpty())
            groupId = b.myGroupId;
    for (const auto& b : f.layer().bricks)
        if (b.myGroupId == groupId) set.insert(b.guid);
    ASSERT_GE(set.size(), 2);
    // Picking one part picks the set.
    f.view->selectParts({ *set.cbegin() });
    QSet<QString> picked;
    for (QGraphicsItem* it : f.view->scene()->selectedItems())
        if (ui::detail::isBrickItem(it)) picked.insert(it->data(ui::detail::kBrickDataGuid).toString());
    ASSERT_EQ(picked, set);
    f.mark();
    f.view->nudgeSelected(8, 0);
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    EXPECT_EQ(f.drawnSince(), static_cast<quint64>(set.size()));
    picked.clear();
    for (QGraphicsItem* it : f.view->scene()->selectedItems())
        if (ui::detail::isBrickItem(it)) picked.insert(it->data(ui::detail::kBrickDataGuid).toString());
    EXPECT_EQ(picked, set) << "the set stays picked";
    for (const auto& b : f.layer().bricks)
        if (set.contains(b.guid)) { EXPECT_EQ(partItem(*f.view->scene(), b.guid)->pos(), drawnAt(b)); }
    EXPECT_EQ(f.view->pickIndexBuilds(), f.indexBuilds);
}

// Bring to Front draws nothing again: the part's item goes on top of its sheet.
// A deleted part brought back by Undo goes back where it was in the order.
TEST(LiveRedraw, TheSheetsOrderIsKept) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString first = f.layer().bricks.front().guid;
    f.view->selectParts({ first });
    f.view->bringSelectionToFront();
    f.view->deselectAll();
    const auto zOf = [&](const QString& guid) { return partItem(*f.view->scene(), guid)->zValue(); };
    const auto inOrder = [&] {
        double prev = -1e300;
        for (const auto& b : f.layer().bricks) {
            const double z = zOf(b.guid);
            if (z <= prev) return false;
            prev = z;
        }
        return true;
    };
    ASSERT_EQ(f.layer().bricks.back().guid, first);
    EXPECT_TRUE(inOrder());
    // Delete one from the middle, then Undo.
    const QString middle = f.layer().bricks[f.layer().bricks.size() / 2].guid;
    f.view->selectParts({ middle });
    f.view->deleteSelected();
    f.mark();
    f.view->undoStack()->undo();
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    EXPECT_TRUE(inOrder());
}

// A ruler fixed to a part follows it, and a part moved off its neighbour
// shows its free end again (connection points are drawn from the links).
TEST(LiveRedraw, RulersAndFreeEndsFollowAMovedPart) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    // Two straights joined end to end, away from the rest.
    if (!f.parts.metadata(QStringLiteral("2865.8"))) GTEST_SKIP() << "no parts library";
    std::vector<core::Brick> pair(2);
    for (int k = 0; k < 2; ++k) {
        pair[static_cast<size_t>(k)].guid = QStringLiteral("straight-%1").arg(k);
        pair[static_cast<size_t>(k)].partNumber = QStringLiteral("2865.8");
        pair[static_cast<size_t>(k)].connections.resize(2);
    }
    parts::placement::placeByImageCentre(pair[0], QPointF(-2000, -2000), f.parts);
    // The second's first end on the first's second end.
    parts::placement::placeByConnection(pair[1], 0, parts::placement::connectionWorld(pair[0], 1, f.parts),
                                        f.parts);
    f.layer();
    const int sheet = f.sheet;
    f.view->undoStack()->push(new edit::AddBricksCommand(f.map(), sheet, pair));
    const auto find = [&](const QString& guid) -> core::Brick* {
        for (auto& x : f.layer().bricks)
            if (x.guid == guid) return &x;
        return nullptr;
    };
    core::Brick* a = find(QStringLiteral("straight-0"));
    core::Brick* b = find(QStringLiteral("straight-1"));
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(std::any_of(b->connections.begin(), b->connections.end(), [](const core::ConnectionPoint& c) {
        return !c.linkedToId.isEmpty();
    })) << "the straights should be joined";
    const QString ga = a->guid, gb = b->guid;
    // A ruler from part a to nowhere.
    int rl = -1;
    for (int i = 0; i < static_cast<int>(f.map().layers().size()); ++i)
        if (f.map().layers()[static_cast<size_t>(i)]->kind() == core::LayerKind::Ruler) rl = i;
    ASSERT_GE(rl, 0);
    core::LayerRuler::AnyRuler r;
    r.kind = core::RulerKind::Linear;
    r.linear.guid = QStringLiteral("ruler-on-a");
    r.linear.attachedBrick1Id = ga;
    r.linear.point2 = QPointF(0, 0);
    r.linear.displayDistance = false;
    f.view->undoStack()->push(new edit::AddRulerItemCommand(f.map(), rl, r));
    const int dotsBefore = connDots(partItem(*f.view->scene(), gb));
    f.mark();
    const QPointF from = a->displayArea.topLeft();
    f.view->undoStack()->push(
        new edit::MoveBricksCommand(f.map(), { { { sheet, ga }, from, from + QPointF(0, 40) } }));
    // a, its old neighbour b (a free end now) and the ruler.
    EXPECT_EQ(f.drawnSince(), 3u);
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    EXPECT_GT(connDots(partItem(*f.view->scene(), gb)), dotsBefore) << "b's free end shows";
    const core::Brick* movedA = nullptr;
    movedA = find(ga);
    ASSERT_NE(movedA, nullptr);
    const QPointF pivot = parts::placement::imageCentre(*movedA, f.parts) * 8;
    bool found = false;
    for (QGraphicsItem* it : f.view->scene()->items())
        if (it->data(ui::detail::kBrickDataGuid).toString() == QLatin1String("ruler-on-a")
            && it->data(rendering::SceneBuilder::kRulerBandRole).isValid()) {
            found = true;
            EXPECT_EQ(it->data(rendering::SceneBuilder::kRulerBandRole).toLineF().p1(), pivot);
        }
    EXPECT_TRUE(found);
}

// A part moved to another sheet: drawn there once (and taken off the first),
// in that sheet's place in the order, and found there by a pick.
TEST(LiveRedraw, APartMovedToAnotherSheetIsDrawnThereOnce) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString guid = f.copied(50).guid;
    int other = -1;
    for (int i = 0; i < static_cast<int>(f.map().layers().size()); ++i)
        if (f.map().layers()[static_cast<size_t>(i)]->kind() == core::LayerKind::Brick && i != f.sheet)
            other = i;
    if (other < 0) GTEST_SKIP() << "one part sheet only";
    f.view->selectParts({ guid });
    f.mark();
    f.view->moveSelectionToSheet(other);
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    EXPECT_EQ(f.drawnSince(), 1u);
    int items = 0;
    for (QGraphicsItem* it : f.view->scene()->items())
        if (ui::detail::isBrickItem(it) && it->data(ui::detail::kBrickDataGuid).toString() == guid) {
            ++items;
            EXPECT_EQ(it->data(ui::detail::kBrickDataLayerIndex).toInt(), other);
            EXPECT_GT(it->zValue(), other * 1000.0 - 1.0) << "on the other sheet's level";
        }
    EXPECT_EQ(items, 1);
    f.view->deselectAll();
    f.view->selectParts({ guid });
    EXPECT_EQ(f.view->scene()->selectedItems().size(), 1);
    f.view->undoStack()->undo();
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    EXPECT_EQ(partItem(*f.view->scene(), guid)->data(ui::detail::kBrickDataLayerIndex).toInt(), f.sheet);
}

// A label on a part: it moves with the part (it is the part's child), and
// when the part goes and comes back (delete, Undo) the label is drawn
// again on it, and a pick of it still works.
TEST(LiveRedraw, ALabelOnAPartFollowsItThroughDeleteAndUndo) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString guid = f.copied(60).guid;
    core::AnchoredLabel label;
    label.id = QStringLiteral("label-on-part");
    label.text = QStringLiteral("Station");
    label.kind = core::AnchorKind::Brick;
    label.targetId = guid;
    f.view->undoStack()->push(new edit::AddAnchoredLabelCommand(f.map(), label));
    const auto labelItem = [&]() -> QGraphicsItem* {
        for (QGraphicsItem* it : f.view->scene()->items())
            if (ui::detail::isLabelItem(it) && it->data(ui::detail::kBrickDataGuid).toString() == label.id)
                return it;
        return nullptr;
    };
    ASSERT_NE(labelItem(), nullptr);
    EXPECT_EQ(labelItem()->parentItem(), partItem(*f.view->scene(), guid));
    f.move(guid, QPointF(8, 0));
    EXPECT_EQ(labelItem()->parentItem(), partItem(*f.view->scene(), guid)) << "it moves with the part";

    f.view->selectParts({ guid });
    f.view->deleteSelected();
    ASSERT_NE(labelItem(), nullptr) << "a label whose part is gone stays on the map";
    EXPECT_EQ(labelItem()->parentItem(), nullptr);
    labelItem()->setSelected(true); // its glow, from the pick index
    QApplication::processEvents();
    f.mark();
    f.view->undoStack()->undo();
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    ASSERT_NE(labelItem(), nullptr);
    EXPECT_EQ(labelItem()->parentItem(), partItem(*f.view->scene(), guid)) << "back on its part";
    f.view->deselectAll();
    labelItem()->setSelected(true);
    QApplication::processEvents();
    EXPECT_TRUE(labelItem()->isSelected());
}

// A part Qt moved (a drag) with the layout unchanged goes back where it is.
TEST(LiveRedraw, APartDraggedWithoutAnEditGoesBack) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString guid = f.copied(20).guid;
    QGraphicsItem* item = partItem(*f.view->scene(), guid);
    f.view->selectParts({ guid });
    item = partItem(*f.view->scene(), guid);
    const QPointF at = item->pos();
    item->setPos(at + QPointF(50, 50));
    f.mark();
    f.view->refreshScene();
    EXPECT_EQ(partItem(*f.view->scene(), guid)->pos(), at);
    EXPECT_EQ(f.drawnSince(), 1u);
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
}

// A new sheet is structural: the scene is built whole.
TEST(LiveRedraw, ANewSheetBuildsTheSceneWhole) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    auto sheet = std::make_unique<core::LayerBrick>();
    sheet->guid = QStringLiteral("new-sheet");
    f.map().layers().push_back(std::move(sheet));
    f.view->refreshScene();
    EXPECT_EQ(f.view->sceneBuilds(), f.builds + 1);
}

// A newer copy of the layout (the live server's) with one part moved: only
// it is drawn again, and the view keeps its zoom and pick.
TEST(LiveRedraw, ANewerCopyDrawsOnlyWhatChanged) {
    Fixture f;
    if (!f.open(1000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString picked = f.copied(30).guid;
    f.view->selectParts({ picked });
    f.view->scale(2, 2);
    const QTransform zoom = f.view->transform();
    auto newer = f.newerCopy(1000);
    core::Brick* moved = nullptr;
    for (auto& b : biggestBrickLayer(*newer)->bricks)
        if (b.guid == f.copied(31).guid) moved = &b;
    ASSERT_NE(moved, nullptr);
    moved->displayArea.translate(0, 32);
    const QString guid = moved->guid;
    const QPointF to = drawnAt(*moved);
    f.mark();
    f.view->updateMap(std::move(newer));
    EXPECT_EQ(f.view->sceneBuilds(), f.builds);
    EXPECT_EQ(f.drawnSince(), 1u);
    EXPECT_EQ(partItem(*f.view->scene(), guid)->pos(), to);
    EXPECT_EQ(f.view->transform(), zoom);
    QStringList pick;
    for (QGraphicsItem* it : f.view->scene()->selectedItems())
        if (ui::detail::isBrickItem(it)) pick << it->data(ui::detail::kBrickDataGuid).toString();
    EXPECT_EQ(pick, QStringList{ picked });
    EXPECT_EQ(f.view->pickIndexBuilds(), f.indexBuilds);
}

// Perf budget, in work done (not time) at 5,000 parts: a move, its undo
// and a remote edit each draw one part again, a 100-part paste about 100,
// and none of them builds the scene or the pick index whole.
TEST(LiveRedrawBudget, AnEditAt5000PartsDrawsOnlyWhatItChanged) {
    Fixture f;
    if (!f.open(5000)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    const QString guid = f.copied(40).guid;
    f.view->selectParts({ f.copied(41).guid });
    f.mark();
    f.move(guid, QPointF(8, 8));
    EXPECT_EQ(f.drawnSince(), 1u);
    f.mark();
    f.view->undoStack()->undo();
    EXPECT_EQ(f.drawnSince(), 1u);

    auto newer = f.newerCopy(5000);
    biggestBrickLayer(*newer)->bricks[4000].displayArea.translate(8, 0);
    f.mark();
    f.view->updateMap(std::move(newer));
    EXPECT_EQ(f.drawnSince(), 1u);

    QSet<QString> hundred;
    for (int k = 0; k < 100; ++k) hundred.insert(f.copied(k).guid);
    f.view->selectParts(hundred);
    f.view->copySelection();
    f.mark();
    f.view->pasteClipboard();
    EXPECT_GE(f.drawnSince(), 100u);
    EXPECT_LE(f.drawnSince(), 110u);
    f.view->undoStack()->undo();
    EXPECT_LE(f.drawnSince(), 120u);
    EXPECT_EQ(f.view->sceneBuilds(), f.builds) << "nothing built the scene whole";
    EXPECT_EQ(f.view->pickIndexBuilds(), f.indexBuilds) << "nor the pick index";
}

// Milliseconds per live edit at 1,000 and 5,000 parts (BLD_BENCH=1 to run):
// a part moved, its undo, a remote copy of the layout with one part moved,
// a 100-part paste and its undo.
TEST(LiveRedrawBench, MillisecondsPerEdit) {
    if (qEnvironmentVariableIsEmpty("BLD_BENCH")) GTEST_SKIP() << "set BLD_BENCH=1 to print the timings";
    parts::PartsLibrary parts;
    for (int n : { 1000, 5000 }) {
        auto map = fordyceOf(parts, n);
        if (!map) GTEST_SKIP() << "fordyce-2026.bbm missing";
        Shown s(parts);
        ui::MapView& view = s.view;
        view.loadMap(std::move(map));
        QApplication::processEvents();

        std::vector<double> move, undo, remote, paste, pasteUndo, connectivity, build;
        for (int rep = 0; rep < 7; ++rep) {
            core::Map& m = *view.currentMap();
            int li = -1;
            core::LayerBrick* layer = biggestBrickLayer(m, &li);
            ASSERT_NE(layer, nullptr);
            const core::Brick& b = layer->bricks[layer->bricks.size() / 2 + static_cast<size_t>(rep)];
            const QPointF at = b.displayArea.topLeft();
            const QString guid = b.guid;
            move.push_back(msOf([&] {
                view.undoStack()->push(
                    new edit::MoveBricksCommand(m, { { { li, guid }, at, at + QPointF(8, 0) } }));
            }));
            undo.push_back(msOf([&] { view.undoStack()->undo(); }));
            connectivity.push_back(msOf([&] { edit::rebuildConnectivity(m, parts); }));
            build.push_back(msOf([&] { view.builder()->build(m); }));
            view.rebuildScene();

            // Another person moved a part: the live layout shows the newer copy.
            auto newer = newerCopyOf(parts, n, *view.currentMap());
            core::LayerBrick* nl = biggestBrickLayer(*newer);
            nl->bricks[nl->bricks.size() / 3 + static_cast<size_t>(rep)].displayArea.translate(8, 0);
            remote.push_back(msOf([&] { view.updateMap(std::move(newer)); }));
            layer = biggestBrickLayer(*view.currentMap(), &li);
            ASSERT_NE(layer, nullptr);

            QSet<QString> hundred;
            for (size_t i = 0; i < 100; ++i) hundred.insert(layer->bricks[i].guid);
            view.selectParts(hundred);
            view.copySelection();
            paste.push_back(msOf([&] { view.pasteClipboard(); }));
            pasteUndo.push_back(msOf([&] { view.undoStack()->undo(); }));
            view.deselectAll();
            view.undoStack()->clear();
        }
        std::printf(
            "[bench] %d parts (%d scene items): move %.2f ms, undo %.2f ms, remote edit %.2f ms, "
            "paste 100 %.2f ms, undo paste %.2f ms | whole connectivity %.2f ms, whole scene build %.2f ms\n",
            brickCount(*view.currentMap()), static_cast<int>(view.scene()->items().size()), median(move),
            median(undo), median(remote), median(paste), median(pasteUndo), median(connectivity),
            median(build));
        std::fflush(stdout);
    }
}

// For a profiler: many moves and undos at 5,000 parts (BLD_BENCH=profile).
TEST(LiveRedrawBench, ManyMovesForAProfiler) {
    if (qEnvironmentVariable("BLD_BENCH") != QLatin1String("profile")) GTEST_SKIP();
    parts::PartsLibrary parts;
    auto map = fordyceOf(parts, 5000);
    if (!map) GTEST_SKIP() << "fordyce-2026.bbm missing";
    Shown s(parts);
    s.view.loadMap(std::move(map));
    core::Map& m = *s.view.currentMap();
    int li = -1;
    core::LayerBrick* layer = biggestBrickLayer(m, &li);
    std::vector<double> ms;
    for (int rep = 0; rep < 300; ++rep) {
        const core::Brick& b = layer->bricks[static_cast<size_t>(rep * 7 % 4000)];
        const QPointF at = b.displayArea.topLeft();
        const QString guid = b.guid;
        ms.push_back(msOf([&] {
            s.view.undoStack()->push(
                new edit::MoveBricksCommand(m, { { { li, guid }, at, at + QPointF(8, 0) } }));
        }));
        if (rep % 2) s.view.undoStack()->undo();
    }
    std::printf("[bench] profile: move %.2f ms\n", median(ms));
}

// For a profiler: remote copies and pastes at 5,000 parts
// (BLD_BENCH=profile-remote or profile-paste).
TEST(LiveRedrawBench, RemoteAndPasteForAProfiler) {
    const QString mode = qEnvironmentVariable("BLD_BENCH");
    if (mode != QLatin1String("profile-remote") && mode != QLatin1String("profile-paste")) GTEST_SKIP();
    parts::PartsLibrary parts;
    auto map = fordyceOf(parts, 5000);
    if (!map) GTEST_SKIP() << "fordyce-2026.bbm missing";
    Shown s(parts);
    s.view.loadMap(std::move(map));
    std::vector<std::unique_ptr<core::Map>> copies;
    if (mode == QLatin1String("profile-remote"))
        for (int rep = 0; rep < 60; ++rep) {
            copies.push_back(newerCopyOf(parts, 5000, *s.view.currentMap()));
            core::LayerBrick* nl = biggestBrickLayer(*copies.back());
            nl->bricks[static_cast<size_t>(rep * 13)].displayArea.translate(8, 0);
        }
    std::vector<double> ms;
    for (int rep = 0; rep < 60; ++rep) {
        if (mode == QLatin1String("profile-remote")) {
            ms.push_back(msOf([&] { s.view.updateMap(std::move(copies[static_cast<size_t>(rep)])); }));
            continue;
        }
        core::LayerBrick* layer = biggestBrickLayer(*s.view.currentMap());
        QSet<QString> hundred;
        for (size_t i = 0; i < 100; ++i) hundred.insert(layer->bricks[i].guid);
        s.view.selectParts(hundred);
        s.view.copySelection();
        ms.push_back(msOf([&] { s.view.pasteClipboard(); }));
        s.view.undoStack()->undo();
        s.view.deselectAll();
    }
    std::printf("[bench] profile %s: %.2f ms\n", qPrintable(mode), median(ms));
}
