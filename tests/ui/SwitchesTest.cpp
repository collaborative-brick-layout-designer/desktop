// Switches on the map (trains design Phase 1): in Run mode a click on a
// switch flips it, its arrow says so, and Undo takes it back (Aaron: flips
// are always undoable); parts can't be picked or moved and there is no
// right-click menu. While building, the right-click menu sets a switch, and
// View > Switches shows the arrows. The web's switches.spec.ts walks the same.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "track/TrackNetwork.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGraphicsPathItem>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QTimer>
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

constexpr double kPx = rendering::SceneBuilder::kPixelsPerStud;

class SwitchesTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
        ASSERT_TRUE(res.map);
        view_ = std::make_unique<ui::MapView>(library());
        view_->resize(1200, 900);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(res.map));
        net_ = track::trackNetworkOf(*view_->currentMap(), library());
        ASSERT_EQ(net_.switches().size(), 8);
        // The RC crossover's first switch, zoomed in on.
        sw_ = net_.switches().front();
        for (const auto& s : net_.switches())
            if (net_.trackOf(s.brick)->kinds[s.heel] == QStringLiteral("rail")) {
                sw_ = s;
                break;
            }
        view_->setTransform(QTransform::fromScale(1.5, 1.5));
        view_->centerOn(heel() * kPx);
    }

    QPointF heel() const { return net_.port(sw_.brick, sw_.heel)->first; }
    QPoint screen(QPointF studs) const { return view_->mapFromScene(studs * kPx); }
    const QMap<QString, int>& states() const { return view_->currentMap()->sidecar.switchStates; }
    QColor arrowColor() const {
        const auto it = view_->builder()->switchArrows().constFind(sw_.key);
        if (it == view_->builder()->switchArrows().constEnd()) return {};
        return static_cast<QGraphicsPathItem*>(*it)->pen().color();
    }
    QStringList brickPoses() const {
        QStringList out;
        for (const auto& l : view_->currentMap()->layers())
            if (l->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                    out << QStringLiteral("%1 %2 %3 %4").arg(b.guid).arg(b.displayArea.x()).arg(b.displayArea.y()).arg(b.orientation);
        return out;
    }
    // Right-clicks at `studs`: the menu's switch entries (name [x] when ticked);
    // triggers the entry named `pick`.
    QStringList switchMenu(QPointF studs, const QString& pick = {}) {
        QStringList out;
        bool shown = false;
        QTimer closer;
        closer.setInterval(20);
        QObject::connect(&closer, &QTimer::timeout, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                shown = true;
                QAction* chosen = nullptr;
                for (QAction* a : menu->actions())
                    if (a->objectName().startsWith(QLatin1String("switch_"))) {
                        out << a->objectName() + (a->isChecked() ? QStringLiteral(" [x]") : QString());
                        if (a->objectName() == pick) chosen = a;
                    }
                closer.stop();
                menu->close();
                if (chosen) chosen->trigger();
            }
        });
        closer.start();
        const QPoint at = screen(studs);
        QContextMenuEvent ce(QContextMenuEvent::Mouse, at, view_->viewport()->mapToGlobal(at));
        QApplication::sendEvent(view_->viewport(), &ce);
        QTest::qWait(60);
        closer.stop();
        if (!shown) out << QStringLiteral("(no menu)");
        return out;
    }

    std::unique_ptr<ui::MapView> view_;
    track::TrackNetwork net_;
    track::SwitchPoint sw_;
};

}  // namespace

TEST_F(SwitchesTest, RunModeFlipsASwitchAndUndoTakesItBack) {
    EXPECT_TRUE(view_->builder()->switchArrows().isEmpty());  // building: no arrows
    view_->setRunMode(true);
    ASSERT_TRUE(view_->runModeBanner());
    EXPECT_TRUE(view_->runModeBanner()->isVisible());
    EXPECT_EQ(view_->builder()->switchArrows().size(), 8);
    EXPECT_EQ(arrowColor(), rendering::SceneBuilder::switchSetColor());
    // Drawn over every sheet, so it's seen.
    const auto top = view_->scene()->items(heel() * kPx, Qt::IntersectsItemShape, Qt::DescendingOrder);
    ASSERT_FALSE(top.isEmpty());
    EXPECT_GE(top.front()->zValue(), 1.0e7);

    const int steps = view_->undoStack()->count();
    QTest::mouseClick(view_->viewport(), Qt::LeftButton, {}, screen(heel()));
    ASSERT_TRUE(states().contains(sw_.key));
    EXPECT_NE(states().value(sw_.key), sw_.defaultExit);
    EXPECT_EQ(view_->undoStack()->count(), steps + 1);
    EXPECT_EQ(arrowColor(), rendering::SceneBuilder::switchTurnedColor());

    view_->undoStack()->undo();
    EXPECT_FALSE(states().contains(sw_.key));
    EXPECT_EQ(arrowColor(), rendering::SceneBuilder::switchSetColor());
    view_->undoStack()->redo();
    EXPECT_TRUE(states().contains(sw_.key));
    // Again: round to the usual way, which drops the key.
    QTest::mouseClick(view_->viewport(), Qt::LeftButton, {}, screen(heel()));
    EXPECT_FALSE(states().contains(sw_.key));

    // Far from any switch: nothing changes.
    const auto before = states();
    QTest::mouseClick(view_->viewport(), Qt::LeftButton, {}, screen(heel() + QPointF(0, 300)));
    EXPECT_EQ(states(), before);

    // Escape goes back to building; the arrows go.
    QTest::keyClick(view_->viewport(), Qt::Key_Escape);
    EXPECT_FALSE(view_->runMode());
    EXPECT_FALSE(view_->runModeBanner()->isVisible());
    EXPECT_TRUE(view_->builder()->switchArrows().isEmpty());
}

TEST_F(SwitchesTest, RunModeKeepsPartsWhereTheyAre) {
    view_->setRunMode(true);
    const QStringList before = brickPoses();
    QWidget* vp = view_->viewport();
    const QPointF from = heel() + QPointF(-6, 0);
    QTest::mousePress(vp, Qt::LeftButton, {}, screen(from));
    for (int i = 1; i <= 10; ++i) {
        const QPoint p = screen(from + QPointF(4.0 * i, 3.0 * i));
        QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &move);
    }
    QTest::mouseRelease(vp, Qt::LeftButton, {}, screen(from + QPointF(40, 30)));
    EXPECT_EQ(brickPoses(), before);
    EXPECT_TRUE(view_->scene()->selectedItems().isEmpty());
    // Select All picks nothing, and keys don't touch parts.
    for (QGraphicsItem* it : view_->scene()->items())
        if (it->flags() & QGraphicsItem::ItemIsSelectable) it->setSelected(true);
    EXPECT_TRUE(view_->scene()->selectedItems().isEmpty());
    QTest::keyClick(vp, Qt::Key_Delete);
    EXPECT_EQ(brickPoses(), before);
    // No right-click menu.
    EXPECT_EQ(switchMenu(heel()), QStringList{ QStringLiteral("(no menu)") });
}

TEST_F(SwitchesTest, TheMenuSetsASwitchWhileBuildingAndViewSwitchesShowsThem) {
    const QString usual = QStringLiteral("switch_%1_%2").arg(sw_.heel).arg(sw_.defaultExit);
    int other = -1;
    for (int e : sw_.exits)
        if (e != sw_.defaultExit) other = e;
    const QString turned = QStringLiteral("switch_%1_%2").arg(sw_.heel).arg(other);
    const QStringList items = switchMenu(heel(), turned);
    EXPECT_TRUE(items.contains(usual + QStringLiteral(" [x]"))) << items.join(QLatin1Char(',')).toStdString();
    EXPECT_TRUE(items.contains(turned)) << items.join(QLatin1Char(',')).toStdString();
    // The RC crossover has a switch at each end: every end's ways are listed.
    EXPECT_EQ(items.size(), 8);
    EXPECT_EQ(states().value(sw_.key, -1), other);
    EXPECT_TRUE(switchMenu(heel()).contains(turned + QStringLiteral(" [x]")));

    // View > Switches shows the arrows while building, this one turned.
    EXPECT_TRUE(view_->builder()->switchArrows().isEmpty());
    view_->setShowSwitches(true);
    EXPECT_EQ(view_->builder()->switchArrows().size(), 8);
    EXPECT_EQ(arrowColor(), rendering::SceneBuilder::switchTurnedColor());
    view_->undoStack()->undo();
    EXPECT_EQ(arrowColor(), rendering::SceneBuilder::switchSetColor());
    view_->setShowSwitches(false);
    EXPECT_TRUE(view_->builder()->switchArrows().isEmpty());
}

// The window: View > Run Trains, the map's "Run trains" button and the
// banner's Stop, and View > Switches, kept for next time.
TEST(SwitchesWindow, RunButtonAndViewSwitches) {
    QSettings().remove(QStringLiteral("view/switches"));
    ui::MainWindow w(library());
    w.resize(1200, 800);
    w.show();
    auto* run = w.findChild<QAction*>(QStringLiteral("action.runTrains"));
    auto* view = w.findChild<QAction*>(QStringLiteral("view.switches"));
    auto* map = w.findChild<ui::MapView*>();
    ASSERT_TRUE(run && view && map);
    map->loadMap(std::make_unique<core::Map>());  // a layout open: the button is there
    ASSERT_TRUE(map->runTrainsButton());
    EXPECT_TRUE(map->runTrainsButton()->isVisible());
    EXPECT_FALSE(view->isChecked());
    // The map's button: Run mode; the banner's Stop: back to building.
    QTest::mouseClick(map->runTrainsButton(), Qt::LeftButton);
    EXPECT_TRUE(map->runMode());
    EXPECT_TRUE(run->isChecked());
    EXPECT_FALSE(map->runTrainsButton()->isVisible());
    auto* stop = map->runModeBanner()->findChild<QPushButton*>(QStringLiteral("RunModeStop"));
    ASSERT_TRUE(stop);
    stop->click();
    EXPECT_FALSE(map->runMode());
    EXPECT_FALSE(run->isChecked());
    EXPECT_TRUE(map->runTrainsButton()->isVisible());
    // The menu item does the same.
    run->trigger();
    EXPECT_TRUE(map->runMode());
    run->trigger();
    EXPECT_FALSE(map->runMode());
    view->trigger();
    EXPECT_TRUE(map->showSwitches());
    EXPECT_TRUE(QSettings().value(QStringLiteral("view/switches")).toBool());
    QSettings().remove(QStringLiteral("view/switches"));
}
