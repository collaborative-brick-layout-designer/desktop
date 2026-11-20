// Rolling stock and trains on the desktop (trains design Phase 3): three
// quick cars in the Trains panel, ticked and made a train, put on the
// track with a click in the middle of a monorail corner: each car's body
// lies on the chord between its trucks, so the head car's middle is inside
// the curve. Turn around, Take off the track and Undo; deleting a car takes
// it out of its train; the roster says how to sign in when signed out.
// The web's trains.spec.ts walks the same.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/TrainDialogs.h"
#include "ui/TrainsPanel.h"
#include "ui/FitChecksList.h"
#include "ui/tours/Tours.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QUndoStack>

#include <cmath>

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

// The top-left monorail corner of tight-corner.bbm (a 30 x 30 stud box at
// 110,110): round a centre at (140, 140), radius 28; its middle at 45°.
constexpr double kCx = 140, kCy = 140, kR = 28;

}  // namespace

TEST(TrainsUi, QuickCarsATrainOnACurveTurnedTakenOffAndUndone) {
    ui::MainWindow w(library());
    w.resize(1400, 900);
    w.show();
    auto* view = w.findChild<ui::MapView*>();
    auto* panel = w.findChild<ui::TrainsPanel*>();
    ASSERT_TRUE(view && panel);
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    ASSERT_TRUE(res.map);
    view->loadMap(std::move(res.map));
    // No cars yet: the panel opens on Rolling stock and says what to do.
    EXPECT_EQ(panel->tabs()->currentIndex(), 1);
    EXPECT_TRUE(panel->findChild<QLabel*>(QStringLiteral("stockEmpty"))->text().startsWith(QStringLiteral("No rolling stock yet")));

    // Three quick cars: a number and a length.
    int n = 0;
    panel->setEditStock([&n](const core::RollingStock& s, bool isNew) -> std::optional<core::RollingStock> {
        EXPECT_TRUE(isNew);
        core::RollingStock c = ui::withLength(s, 24);
        c.number = QString::number(101 + n++);
        return c;
    });
    const int steps = view->undoStack()->count();
    for (int i = 0; i < 3; ++i) panel->findChild<QPushButton*>(QStringLiteral("stockNew"))->click();
    EXPECT_EQ(view->undoStack()->count(), steps + 3);
    const auto& stock = view->currentMap()->sidecar.rollingStock;
    ASSERT_EQ(stock.size(), 3u);
    EXPECT_DOUBLE_EQ(stock[0].trucks[0].at, 6);  // a quarter in from each end

    // Ticked and made a train, in the order ticked.
    QStringList order;
    for (const char* number : { "101", "102", "103" })
        for (const auto& s : stock)
            if (s.number == QLatin1String(number)) order << s.id;
    for (const QString& id : order) panel->setTicked(id, true);
    auto* make = panel->findChild<QPushButton*>(QStringLiteral("trainFromTicked"));
    ASSERT_TRUE(make && make->isVisibleTo(panel));
    EXPECT_EQ(make->text(), QStringLiteral("Make a train of these 3 cars"));
    make->click();
    ASSERT_EQ(view->currentMap()->sidecar.trains.size(), 1u);
    const core::Train train = view->currentMap()->sidecar.trains[0];
    ASSERT_EQ(train.units.size(), 3u);
    EXPECT_EQ(train.units[0].stock, order[0]);
    EXPECT_EQ(panel->tabs()->currentIndex(), 0);
    auto* row = panel->findChild<QFrame*>(QStringLiteral("train_") + train.id);
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("trainState"))->text().startsWith(QStringLiteral("Not on the map")));

    // Put on the track: a click in the middle of the corner.
    row->findChild<QPushButton*>(QStringLiteral("trainPlace"))->click();
    EXPECT_EQ(view->placingTrain(), train.id);
    ASSERT_TRUE(view->trainPlaceBanner() && view->trainPlaceBanner()->isVisible());
    EXPECT_FALSE(view->trainPlaceClick(QPointF(-500, -500)));  // off the track: still waiting
    EXPECT_EQ(view->placingTrain(), train.id);
    const int before = view->undoStack()->count();
    ASSERT_TRUE(view->trainPlaceClick(QPointF(kCx - kR * M_SQRT1_2, kCy - kR * M_SQRT1_2)));
    EXPECT_TRUE(view->placingTrain().isEmpty());
    EXPECT_EQ(view->undoStack()->count(), before + 1);
    ASSERT_TRUE(view->currentMap()->sidecar.trains[0].position.has_value());
    const auto cars = view->builder()->trainItems().value(train.id);
    ASSERT_EQ(cars.size(), 3);
    const QPointF head = cars[0]->pos() / rendering::SceneBuilder::kPixelsPerStud;
    const double fromCentre = std::hypot(head.x() - kCx, head.y() - kCy);
    EXPECT_LT(fromCentre, kR - 0.5);  // on the chord: inside the curve
    EXPECT_GT(fromCentre, kR - 6);
    EXPECT_GT(std::abs(cars[0]->rotation() - cars[2]->rotation()), 1.0);  // round the corner
    row = panel->findChild<QFrame*>(QStringLiteral("train_") + train.id);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("trainState"))->text().startsWith(QStringLiteral("On the track")));

    // Turn around: the last car is the front now, and the cars stay put.
    const QPointF tail = cars[2]->pos();
    row->findChild<QPushButton*>(QStringLiteral("trainTurn"))->click();
    const core::Train turned = view->currentMap()->sidecar.trains[0];
    EXPECT_EQ(turned.units[0].stock, order[2]);
    EXPECT_TRUE(turned.units[0].reversed);
    const auto turnedCars = view->builder()->trainItems().value(train.id);
    ASSERT_EQ(turnedCars.size(), 3);
    EXPECT_LT(QLineF(turnedCars[0]->pos(), tail).length() / rendering::SceneBuilder::kPixelsPerStud, 1.0);

    // Take off the track; Undo puts it back.
    row = panel->findChild<QFrame*>(QStringLiteral("train_") + train.id);
    row->findChild<QPushButton*>(QStringLiteral("trainTakeOff"))->click();
    EXPECT_FALSE(view->currentMap()->sidecar.trains[0].position.has_value());
    EXPECT_TRUE(view->builder()->trainItems().value(train.id).isEmpty());
    view->undoStack()->undo();
    EXPECT_TRUE(view->currentMap()->sidecar.trains[0].position.has_value());
    EXPECT_EQ(view->builder()->trainItems().value(train.id).size(), 3);

    // Deleting a car asks first and takes it out of its train, in one step.
    panel->setConfirm([](const QString& q) { return q.contains(QStringLiteral("take it out of")); });
    const int beforeDelete = view->undoStack()->index();
    EXPECT_TRUE(panel->removeStock(order[1]));
    EXPECT_EQ(view->undoStack()->index(), beforeDelete + 1);
    EXPECT_EQ(view->currentMap()->sidecar.rollingStock.size(), 2u);
    EXPECT_EQ(view->currentMap()->sidecar.trains[0].units.size(), 2u);
    view->undoStack()->undo();
    EXPECT_EQ(view->currentMap()->sidecar.rollingStock.size(), 3u);
    EXPECT_EQ(view->currentMap()->sidecar.trains[0].units.size(), 3u);

    // Signed out: the roster says how to sign in, not nothing.
    panel->openRoster();
    EXPECT_TRUE(panel->message().contains(QStringLiteral("Sign in"))) << panel->message().toStdString();
}

TEST(TrainsUi, TheCarDialogKeepsItSimpleUntilMoreDetails) {
    ui::StockDialog d(core::simpleStock(QStringLiteral("s"), 32), true, library());
    EXPECT_FALSE(d.moreFields()->isVisibleTo(&d));
    d.lengthEdit()->setValue(48);
    // The trucks keep their share in from each end.
    EXPECT_DOUBLE_EQ(d.stock().trucks[0].at, 12);
    EXPECT_DOUBLE_EQ(d.stock().trucks[1].at, -12);
    EXPECT_DOUBLE_EQ(d.stock().bodyLength, 46);
    d.moreButton()->click();
    EXPECT_TRUE(d.moreFields()->isVisibleTo(&d));
    // Trucks that don't fit the car: Save waits.
    d.frontTruckEdit()->setValue(0);
    d.rearTruckEdit()->setValue(0);
    EXPECT_FALSE(d.saveButton()->isEnabled());
    d.frontTruckEdit()->setValue(10);
    EXPECT_TRUE(d.saveButton()->isEnabled());
}

TEST(TrainsUi, TheTrainsTourPointsAtThePanel) {
    ui::MainWindow w(library());
    w.resize(1400, 900);
    w.show();
    auto* panel = w.findChild<ui::TrainsPanel*>();
    ASSERT_TRUE(panel);
    auto* view = w.findChild<ui::MapView*>();
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    view->loadMap(std::move(res.map));
    panel->show();
    QApplication::processEvents();
    const auto* tour = ui::tours::findTour(QStringLiteral("trains"));
    ASSERT_TRUE(tour);
    // An empty layout opens on Rolling stock: the panel, its tabs, New car and the roster are there.
    for (const char* target : { "panel.trains", "trainsPanel.tabs", "trains.newStock", "trains.roster" })
        EXPECT_TRUE(ui::tours::findTarget(&w, QString::fromLatin1(target))) << target;
}

// Fit checks (trains design Phase 4): a two-car train at the top of a 96-stud
// stub of 9V track fits; a third car makes it longer than the stub, and the
// Checks list says so at once, with a ring on the map; Show me rings the
// spot; taking the car out clears it. View > Siding Lengths labels the runs.
TEST(TrainsUi, ATrainTooLongForItsSidingIsFlaggedAndShorteningClearsIt) {
    ui::MainWindow w(library());
    w.resize(1400, 900);
    w.show();
    auto* view = w.findChild<ui::MapView*>();
    auto* panel = w.findChild<ui::TrainsPanel*>();
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    ASSERT_TRUE(res.map);
    view->loadMap(std::move(res.map));
    int n = 0;
    panel->setEditStock([&n](const core::RollingStock& s, bool) -> std::optional<core::RollingStock> {
        core::RollingStock c = ui::withLength(s, 40);
        c.number = QString::number(501 + n++);
        return c;
    });
    for (int i = 0; i < 3; ++i) panel->newStock();
    const auto stock = view->currentMap()->sidecar.rollingStock;
    QStringList ids;
    for (const char* number : { "501", "502", "503" })
        for (const auto& s : stock)
            if (s.number == QLatin1String(number)) ids << s.id;
    panel->setTicked(ids[0], true);
    panel->setTicked(ids[1], true);
    const QString trainId = panel->makeTrainOfTicked();
    view->startTrainPlacing(trainId);
    ASSERT_TRUE(view->trainPlaceClick(QPointF(456, 148)));
    auto* checks = panel->checksList();
    ASSERT_TRUE(checks);
    EXPECT_EQ(checks->count(), 0);
    EXPECT_TRUE(checks->findChild<QLabel*>(QStringLiteral("fitChecksClear"))->text().startsWith(QStringLiteral("Every train on the track fits")));
    EXPECT_EQ(view->builder()->checkRingCount(), 0);

    // A third car: longer than the stub.
    panel->setEditTrain([&ids](const core::Train& t) -> std::optional<core::Train> {
        core::Train out = t;
        out.units.push_back({ ids[2], false });
        return out;
    });
    panel->changeTrain(trainId);
    ASSERT_GE(checks->count(), 1);  // (and it runs off the end)
    EXPECT_TRUE(checks->lines().join(QString()).contains(QStringLiteral("is longer than the track it stands in"))) << checks->lines().join(QString()).toStdString();
    EXPECT_GT(view->builder()->checkRingCount(), 0);
    checks->findChild<QPushButton*>(QStringLiteral("fitCheckShow"))->click();
    EXPECT_TRUE(view->builder()->spotlightShown());

    // Shorter again: cleared, nothing to refresh.
    panel->setEditTrain([](const core::Train& t) -> std::optional<core::Train> {
        core::Train out = t;
        out.units.pop_back();
        return out;
    });
    panel->changeTrain(trainId);
    EXPECT_EQ(checks->count(), 0);
    EXPECT_EQ(view->builder()->checkRingCount(), 0);

    // View > Siding Lengths.
    auto* act = w.findChild<QAction*>(QStringLiteral("view.sidingLengths"));
    ASSERT_TRUE(act);
    act->setChecked(true);
    EXPECT_GT(view->builder()->sidingLabelCount(), 5);
    act->setChecked(false);
    EXPECT_EQ(view->builder()->sidingLabelCount(), 0);
}
