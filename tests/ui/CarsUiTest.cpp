// Cars on the track on the desktop (Live Ops Phase 2): three cars put on a
// stub of track coupled, the last one dragged off with the mouse and Undo,
// dragged again and back to couple, its load from the car's menu, taken off,
// and the track under them deleted and brought back. The web's cars.spec.ts
// walks the same.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/TrainDialogs.h"
#include "ui/TrainsPanel.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "track/Cuts.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QUndoStack>

using namespace bld;

namespace {

parts::PartsLibrary& library() {
    static parts::PartsLibrary* lib = [] {
        auto* l = new parts::PartsLibrary;
        l->addSearchPath(QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
        l->scan();
        return l;
    }();
    return *lib;
}

// A stub of 9V track in tight-corner.bbm: 96 studs from (456, 144) down, open at both ends.
constexpr double kX = 456;
constexpr double kTop = 144;

QStringList standing(const core::Map& map) {
    QStringList out;
    const auto stock = track::stockById(map.sidecar);
    for (const auto& t : track::standingTrains(map.sidecar)) {
        QStringList cars;
        for (const auto& u : t.units) cars << stock[u.stock].number;
        if (t.position && t.cut) out << cars.join(QLatin1Char(' '));
    }
    out.sort();
    return out;
}

struct Fixture {
    ui::MainWindow w{ library() };
    ui::MapView* view = nullptr;
    ui::TrainsPanel* panel = nullptr;
    QMap<QString, QString> ids;  // number -> id
    Fixture() {
        w.resize(1400, 900);
        w.show();
        view = w.findChild<ui::MapView*>();
        panel = w.findChild<ui::TrainsPanel*>();
        auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
        view->loadMap(std::move(res.map));
        int n = 0;
        panel->setEditStock([&n](const core::RollingStock& s, bool) -> std::optional<core::RollingStock> {
            core::RollingStock c = ui::withLength(s, 20);
            c.number = QString::number(101 + n++);
            return c;
        });
        panel->setConfirm([](const QString&) { return true; });
        for (int i = 0; i < 3; ++i) panel->newStock();
        for (const auto& s : map().sidecar.rollingStock) ids.insert(s.number, s.id);
    }
    core::Map& map() const { return *view->currentMap(); }
    void place() {
        for (const QString& id : ids) panel->setTicked(id, true);
        panel->findChild<QPushButton*>(QStringLiteral("carsPlace"))->click();
        ASSERT_TRUE(view->trainPlaceBanner() && view->trainPlaceBanner()->isVisible());
        ASSERT_TRUE(view->trainPlaceClick(QPointF(kX, kTop + 10)));
        view->showSpot(QPointF(kX, kTop + 48));
        QApplication::processEvents();
    }
    QPointF middle(const QString& number) const {
        for (double y = kTop; y < kTop + 96; y += 0.5)
            if (const auto c = view->carAt(QPointF(kX, y)); c && c->stock == ids[number]) return c->middle;
        return {};
    }
    QPoint onScreen(QPointF studs) const { return view->mapFromScene(studs * rendering::SceneBuilder::kPixelsPerStud); }
    void drag(const QString& number, double dy) const {
        const QPointF at = middle(number);
        QTest::mousePress(view->viewport(), Qt::LeftButton, {}, onScreen(at));
        QTest::mouseMove(view->viewport(), onScreen(at + QPointF(0, dy / 2)));
        QTest::mouseMove(view->viewport(), onScreen(at + QPointF(0, dy)));
        EXPECT_FALSE(view->builder()->carGhost().isEmpty());
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, onScreen(at + QPointF(0, dy)));
        EXPECT_TRUE(view->builder()->carGhost().isEmpty());
    }
};

}  // namespace

TEST(CarsUi, PutOnTheTrackDraggedOffUndoneCoupledLoadedAndTakenOff) {
    Fixture f;
    f.place();
    ASSERT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
    const QPointF start = f.middle(QStringLiteral("103"));
    const QPointF front = f.middle(QStringLiteral("101"));

    // The last car, dragged away from the others: it goes alone, as one step.
    const int steps = f.view->undoStack()->count();
    f.drag(QStringLiteral("103"), 14);
    EXPECT_EQ(standing(f.map()), (QStringList{ QStringLiteral("101 102"), QStringLiteral("103") }));
    EXPECT_EQ(f.view->undoStack()->count(), steps + 1);
    EXPECT_NEAR(f.middle(QStringLiteral("103")).y() - start.y(), 14, 1);
    EXPECT_NEAR(f.middle(QStringLiteral("101")).y(), front.y(), 0.1);
    f.view->undoStack()->undo();
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
    f.view->undoStack()->redo();
    // Dragged back: it couples.
    f.drag(QStringLiteral("103"), -20);
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });

    // Esc gives a drag up.
    const QPointF at = f.middle(QStringLiteral("103"));
    QTest::mousePress(f.view->viewport(), Qt::LeftButton, {}, f.onScreen(at));
    QTest::mouseMove(f.view->viewport(), f.onScreen(at + QPointF(0, 12)));
    EXPECT_TRUE(f.view->draggingCar());
    QTest::keyClick(f.view, Qt::Key_Escape);
    EXPECT_FALSE(f.view->draggingCar());
    QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, {}, f.onScreen(at + QPointF(0, 12)));
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });

    // The car's menu: its load shows on the map.
    QMenu* menu = f.view->makeCarMenu(f.ids[QStringLiteral("102")]);
    ASSERT_TRUE(menu);
    menu->findChild<QAction*>(QStringLiteral("CarLoad.loaded"))->trigger();
    EXPECT_EQ(f.map().sidecar.rollingStock.size(), 3u);
    const auto loaded = [&] { return track::stockById(f.map().sidecar)[f.ids[QStringLiteral("102")]].opsLoad; };
    EXPECT_EQ(loaded(), QStringLiteral("loaded"));
    f.view->undoStack()->undo();
    EXPECT_EQ(loaded(), QString());
    // Taken off: the cars on either side stay.
    menu->findChild<QAction*>(QStringLiteral("CarTakeOff"))->trigger();
    EXPECT_EQ(standing(f.map()), (QStringList{ QStringLiteral("101"), QStringLiteral("103") }));
    f.view->undoStack()->undo();
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
    delete menu;
}

TEST(CarsUi, ThePanelListsThemLoadsThemAndTakesThemOff) {
    Fixture f;
    f.place();
    auto* row = f.panel->findChild<QFrame*>(QStringLiteral("stock_") + f.ids[QStringLiteral("101")]);
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("stockFacts"))->text().contains(QStringLiteral("coupled to 2 other")));
    auto* load = row->findChild<QComboBox*>(QStringLiteral("carLoad"));
    load->setCurrentIndex(load->findData(QStringLiteral("empty")));
    emit load->activated(load->currentIndex());
    EXPECT_EQ(track::stockById(f.map().sidecar)[f.ids[QStringLiteral("101")]].opsLoad, QStringLiteral("empty"));
    EXPECT_EQ(f.panel->tabs()->tabText(0), QStringLiteral("Trains (0)"));
    EXPECT_TRUE(f.panel->findChild<QLabel*>(QStringLiteral("cutsTitle")));
    const core::Train cut = track::standingTrains(f.map().sidecar).front();
    auto* cutRow = f.panel->findChild<QFrame*>(QStringLiteral("train_") + cut.id);
    ASSERT_TRUE(cutRow);
    EXPECT_EQ(cutRow->findChild<QLabel*>(QStringLiteral("trainName"))->text(), QStringLiteral("101 and 2 more"));
    // Deleting a car between two leaves them where they stand, in one step.
    f.panel->removeStock(f.ids[QStringLiteral("102")]);
    EXPECT_EQ(standing(f.map()), (QStringList{ QStringLiteral("101"), QStringLiteral("103") }));
    f.view->undoStack()->undo();
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
    EXPECT_EQ(f.map().sidecar.rollingStock.size(), 3u);
    f.panel->takeOff(cut.id);
    EXPECT_TRUE(standing(f.map()).isEmpty());
    f.view->undoStack()->undo();
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
}

TEST(CarsUi, TheTrackUnderThemDeletedKeepsThemAndUndoBringsThemBack) {
    Fixture f;
    f.place();
    const QPointF at = f.middle(QStringLiteral("102"));
    f.view->selectAll();
    f.view->deleteSelected();
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
    const core::Train cut = track::standingTrains(f.map().sidecar).front();
    auto* row = f.panel->findChild<QFrame*>(QStringLiteral("train_") + cut.id);
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("trainState"))->text().startsWith(QStringLiteral("Its track was moved or removed")));
    f.view->undoStack()->undo();
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
    EXPECT_NEAR(f.middle(QStringLiteral("102")).y(), at.y(), 0.1);
}

TEST(CarsUi, AViewOnlyLayoutsCarsStayPut) {
    Fixture f;
    f.place();
    f.view->setRunReadOnly(true);
    EXPECT_FALSE(f.view->startCarDrag(f.middle(QStringLiteral("103")), QPoint()));
    f.view->takeCarOff(f.ids[QStringLiteral("103")]);
    EXPECT_EQ(standing(f.map()), QStringList{ QStringLiteral("101 102 103") });
}
