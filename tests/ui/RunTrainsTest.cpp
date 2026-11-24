// Running trains on the desktop (trains design Phase 5), on the 96-stud
// stub of 9V track in tight-corner.bbm (from 456,144 down to 456,240). A
// train with a route from its front runs it on Play and stops at the
// route's end, with nothing written to the layout and no undo step;
// Restart and Stop put it back where it stands. Two trains facing each
// other both stop before they touch, with a red ring and a plain sentence
// with Show me. The scale is a layout setting (one undo step); speeds
// show in the account's units. The web's runTrains.spec.ts walks the same.

#include "ui/MapView.h"
#include "ui/RunPanel.h"
#include "ui/RunSpeeds.h"
#include "ui/TrainDialogs.h"
#include "core/Map.h"
#include "edit/SwitchCommands.h"
#include "edit/TrainCommands.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "track/Animation.h"
#include "track/Routes.h"

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsItem>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QUndoStack>

#include <variant>

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

constexpr double kStubX = 456, kStubTop = 144;

class RunTrainsTest : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings().setValue(QStringLiteral("appearance/lengthUnit"), QStringLiteral("ftin"));
        QSettings().remove(QStringLiteral("run"));
        auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
        ASSERT_TRUE(res.map);
        view_ = std::make_unique<ui::MapView>(library());
        view_->resize(1200, 900);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(res.map));
        net_ = track::trackNetworkOf(*view_->currentMap(), library());
        core::RollingStock car = core::simpleStock(QStringLiteral("car"), 40);
        car.number = QStringLiteral("601");
        push(new edit::SetStockCommand(map(), car.id, car, QStringLiteral("car")));
    }

    core::Map& map() { return *view_->currentMap(); }
    void push(QUndoCommand* c) {
        view_->undoStack()->push(c);
        view_->refreshScene();
    }

    // A place on the stub `y` studs down, facing down the stub (or up).
    core::TrainPosition at(double y, bool down) const {
        const auto pos = track::nearestPosition(net_, QPointF(kStubX, y), 2);
        EXPECT_TRUE(pos.has_value());
        core::TrainPosition p;
        static_cast<core::TrackPosition&>(p) = *pos;
        const auto& pts = net_.path(p.brick, p.from, p.to);
        const bool toIsDown = track::pointAt(pts, pts.back().s).p.y() > track::pointAt(pts, 0).p.y();
        p.facingTo = toIsDown == down;
        return p;
    }

    void addTrain(const QString& id, const QString& name, const core::TrainPosition& p) {
        core::Train t;
        t.id = id;
        t.name = name;
        t.units = { { QStringLiteral("car"), false } };
        t.position = p;
        push(new edit::SetTrainCommand(map(), id, t, name));
    }

    // Frames of a sixtieth of a second until every train has stopped (at most `max`).
    void runToStop(int max = 600) {
        for (int f = 0; f < max && view_->runPlaying(); ++f) view_->stepRun(1.0 / 60.0);
    }

    QWidget* row(const QString& trainId) const { return view_->runPanel()->trainRow(trainId); }
    QString state(const QString& trainId) const {
        auto* l = row(trainId) ? row(trainId)->findChild<QLabel*>(QStringLiteral("RunState")) : nullptr;
        return l ? l->property("state").toString() : QString();
    }

    std::unique_ptr<ui::MapView> view_;
    track::TrackNetwork net_;
};

}  // namespace

TEST_F(RunTrainsTest, PlayRunsATrainAlongItsRouteToTheEndAndLeavesTheLayoutAlone) {
    // Its front in the middle of the stub, facing up; a route from there to near the top.
    const auto head = at(kStubTop + 44, false);
    addTrain(QStringLiteral("t1"), QStringLiteral("Daylight"), head);
    const auto end = track::nearestPosition(net_, QPointF(kStubX, kStubTop + 10), 2);
    ASSERT_TRUE(end);
    track::FindOptions up;
    up.facing = track::Facing::To;
    const auto found = track::findRoute(net_, head, *end, up);
    core::Route route;
    route.id = QStringLiteral("r1");
    route.name = QStringLiteral("Up the stub");
    route.train = QStringLiteral("t1");
    route.from = head;
    route.to = *end;
    if (std::holds_alternative<track::RouteFound>(found)) {
        route.steps = std::get<track::RouteFound>(found).steps;
    } else {
        up.facing = track::Facing::From;
        const auto other = track::findRoute(net_, head, *end, up);
        ASSERT_TRUE(std::holds_alternative<track::RouteFound>(other));
        route.steps = std::get<track::RouteFound>(other).steps;
    }
    push(new edit::SetRouteCommand(map(), route.id, route, route.name));
    ASSERT_EQ(view_->builder()->trainItems().value(QStringLiteral("t1")).size(), 1);
    const QPointF standing = view_->builder()->trainItems().value(QStringLiteral("t1")).front()->pos();
    const auto sidecarBefore = map().sidecar.trains;
    const int steps = view_->undoStack()->count();

    // Run: the corner panel, the train on its own route, its speed in mph.
    view_->setRunMode(true);
    ui::RunPanel* panel = view_->runPanel();
    ASSERT_TRUE(panel && panel->isVisible());
    ASSERT_TRUE(row(QStringLiteral("t1")));
    auto* where = row(QStringLiteral("t1"))->findChild<QComboBox*>(QStringLiteral("RunRoute"));
    ASSERT_TRUE(where);
    EXPECT_EQ(where->currentText(), QStringLiteral("Route: Up the stub"));
    EXPECT_EQ(row(QStringLiteral("t1"))->findChild<QLabel*>(QStringLiteral("RunSpeed"))->text(), QStringLiteral("25 mph"));
    panel->playButton()->click();
    EXPECT_TRUE(view_->runPlaying());
    EXPECT_EQ(panel->playButton()->text(), QStringLiteral("❚❚ Pause"));
    // Drawn by the run, not standing.
    EXPECT_FALSE(view_->builder()->trainItems().contains(QStringLiteral("t1")));
    runToStop();
    EXPECT_FALSE(view_->runPlaying());
    EXPECT_EQ(state(QStringLiteral("t1")), QStringLiteral("arrived"));
    ASSERT_FALSE(panel->noteRows().isEmpty());
    EXPECT_EQ(panel->noteRows().front()->property("kind").toString(), QStringLiteral("arrived"));
    EXPECT_TRUE(panel->noteRows().front()->findChild<QLabel*>()->text().contains(QStringLiteral("reached the end of Up the stub")));
    const auto ran = view_->builder()->runItems().value(QStringLiteral("t1"));
    ASSERT_EQ(ran.size(), 1);
    EXPECT_LT(ran.front()->pos().y(), standing.y() - 25 * rendering::SceneBuilder::kPixelsPerStud);
    EXPECT_NEAR(ran.front()->pos().x(), standing.x(), 1.0);

    // Nothing about the run went into the layout, and no undo step.
    EXPECT_EQ(map().sidecar.trains, sidecarBefore);
    EXPECT_EQ(view_->undoStack()->count(), steps);

    // Restart puts it back; Stop leaves Run mode with it standing where it was.
    panel->restartButton()->click();
    EXPECT_TRUE(view_->builder()->runItems().isEmpty());
    EXPECT_EQ(view_->builder()->trainItems().value(QStringLiteral("t1")).size(), 1);
    view_->setRunMode(false);
    EXPECT_FALSE(panel->isVisible());
    EXPECT_EQ(view_->builder()->trainItems().value(QStringLiteral("t1")).front()->pos(), standing);
}

TEST_F(RunTrainsTest, TwoTrainsHeadingForEachOtherBothStopWithARing) {
    // A faces down the stub from near the top; B faces up from near the bottom.
    addTrain(QStringLiteral("a"), QStringLiteral("Down train"), at(kStubTop + 32, true));
    addTrain(QStringLiteral("b"), QStringLiteral("Up train"), at(kStubTop + 64, false));
    view_->setRunMode(true);
    ui::RunPanel* panel = view_->runPanel();
    for (const QString& id : { QStringLiteral("a"), QStringLiteral("b") })
        EXPECT_EQ(row(id)->findChild<QComboBox*>(QStringLiteral("RunRoute"))->currentText(),
                  QStringLiteral("From where it stands, following the switches"));
    panel->playButton()->click();
    runToStop();
    EXPECT_EQ(state(QStringLiteral("a")), QStringLiteral("blocked"));
    EXPECT_EQ(state(QStringLiteral("b")), QStringLiteral("blocked"));
    ASSERT_EQ(panel->noteRows().size(), 1);
    EXPECT_EQ(panel->noteRows().front()->property("kind").toString(), QStringLiteral("collision"));
    EXPECT_EQ(panel->noteRows().front()->findChild<QLabel*>()->text(),
              QStringLiteral("Down train and Up train would collide here, so both stopped."));
    EXPECT_EQ(view_->builder()->collisionMarks(), 1);
    // They stopped short: their cars don't touch.
    EXPECT_FALSE(track::trainsTouch(view_->trainRun()->placedOf(QStringLiteral("a")), view_->trainRun()->placedOf(QStringLiteral("b")),
                                    { { QStringLiteral("car"), map().sidecar.rollingStock.front() } }));
    // Show me brings the spot into view.
    const QTransform before = view_->transform();
    panel->noteRows().front()->findChild<QPushButton*>(QStringLiteral("RunShowMe"))->click();
    EXPECT_NE(view_->transform(), before);
    // Play again: they still meet, so they stay stopped.
    panel->playButton()->click();
    view_->stepRun(1.0 / 60.0);
    EXPECT_FALSE(view_->runPlaying());
    EXPECT_EQ(view_->builder()->collisionMarks(), 1);
    // Up train out of the run: it stands there, and Down train still stops short of it.
    row(QStringLiteral("b"))->findChild<QCheckBox*>(QStringLiteral("RunOn"))->setChecked(false);
    EXPECT_FALSE(view_->trainRun());
    panel->playButton()->click();
    runToStop();
    EXPECT_EQ(state(QStringLiteral("a")), QStringLiteral("blocked"));
    EXPECT_EQ(state(QStringLiteral("b")), QString());
    EXPECT_TRUE(panel->noteRows().front()->findChild<QLabel*>()->text().startsWith(QStringLiteral("Down train and Up train")));
}

TEST_F(RunTrainsTest, TheScaleIsALayoutSettingAndSpeedsShowInYourUnits) {
    view_->setRunMode(true);
    ui::RunPanel* panel = view_->runPanel();
    EXPECT_EQ(panel->scaleBox()->value(), 38);
    const int steps = view_->undoStack()->count();
    panel->scaleBox()->setValue(87);
    EXPECT_EQ(map().sidecar.trainScale, 87);
    EXPECT_EQ(view_->undoStack()->count(), steps + 1);
    view_->undoStack()->undo();
    EXPECT_FALSE(map().sidecar.trainScale.has_value());
    // 1:38 is the default: kept as none.
    view_->setTrainScale(38);
    EXPECT_EQ(view_->undoStack()->count(), steps + 1);

    EXPECT_EQ(ui::RunSpeeds::scaleSpeed(40, core::VenueLengthUnit::FeetInches), QStringLiteral("25 mph"));
    EXPECT_EQ(ui::RunSpeeds::scaleSpeed(40, core::VenueLengthUnit::Metres), QStringLiteral("40 km/h"));
    EXPECT_EQ(ui::RunSpeeds::realSpeed(40, 38, core::VenueLengthUnit::Metres), QStringLiteral("0.29 m/s"));
    EXPECT_EQ(ui::RunSpeeds::realSpeed(40, 38, core::VenueLengthUnit::FeetInches), QStringLiteral("11.5 in/s"));
    EXPECT_EQ(ui::RunSpeeds::realSpeed(40, 38, core::VenueLengthUnit::Studs), QStringLiteral("37 studs/s"));

    // Real speed shows how fast the model goes; the choice is this person's own.
    addTrain(QStringLiteral("t1"), QStringLiteral("Daylight"), at(kStubTop + 44, false));
    panel->realSpeedBox()->setChecked(true);
    EXPECT_EQ(row(QStringLiteral("t1"))->findChild<QLabel*>(QStringLiteral("RunSpeed"))->text(), QStringLiteral("11.5 in/s"));
    EXPECT_TRUE(QSettings().value(QStringLiteral("run/realSpeed")).toBool());
    panel->multiplierBox()->setCurrentIndex(3);
    EXPECT_DOUBLE_EQ(QSettings().value(QStringLiteral("run/multiplier")).toDouble(), 2.0);

    // A train's speed, typed in mph.
    ui::TrainDialog dlg(map().sidecar.trains.front(), map().sidecar.rollingStock);
    EXPECT_EQ(dlg.speedBox()->value(), 25);
    dlg.speedBox()->setValue(50);
    EXPECT_NEAR(dlg.train().speed, 80.47, 0.01);
}

TEST_F(RunTrainsTest, ALayoutWithNoTrainsOffersTheTrainsPanel) {
    view_->setRunMode(true);
    ui::RunPanel* panel = view_->runPanel();
    EXPECT_FALSE(panel->playButton()->isEnabled());
    auto* open = panel->findChild<QPushButton*>(QStringLiteral("RunOpenTrains"));
    ASSERT_TRUE(open && open->isVisibleTo(panel));
    bool asked = false;
    QObject::connect(view_.get(), &ui::MapView::openTrainsRequested, [&] { asked = true; });
    open->click();
    EXPECT_TRUE(asked);
}
