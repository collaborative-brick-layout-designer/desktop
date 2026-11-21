// Named routes on the desktop (trains design Phase 2): New route in the
// Routes panel, two clicks on the track, a name, Save: one undo step, drawn
// on the map, listed with its length and OK. The eye hides it on this
// person's map only, kept for next time. Taking a piece out from under it
// turns it Broken (kept, dashed red); Find a new way goes round the other
// side of the loop. Set switches sets its switches; Delete asks first.
// Go the other way sends the train the whole loop round (it never turns
// around on the spot), arrows showing which way; a route started on a
// train's front goes the way the train faces. The web's routes.spec.ts
// walks the same.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/RoutesPanel.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "track/Routes.h"
#include "edit/SwitchCommands.h"

#include <gtest/gtest.h>

#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
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

// Two monorail straights far apart on the monorail loop (loose parts, not in sets).
std::pair<QPointF, QPointF> twoPlaces(const core::Map& map) {
    const track::TrackNetwork net = track::trackNetworkOf(map, library());
    std::vector<QPointF> at;
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (b.partNumber == QLatin1String("2671.7") && b.myGroupId.isEmpty())
                    if (const auto p = net.pointAtPosition({ b.guid, 0, 1, 8 })) at.push_back(p->p);
    std::sort(at.begin(), at.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
    return { at[1], at[at.size() - 2] };
}

// A loose monorail straight on the loop.
QString aStraight(const core::Map& map) {
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (b.partNumber == QLatin1String("2671.7") && b.myGroupId.isEmpty()) return b.guid;
    return {};
}

// The direction arrows drawn: on the route being drawn ("draft"), or on a saved route (its id).
int arrows(ui::MapView* view, const QString& on) {
    int n = 0;
    for (QGraphicsItem* it : view->scene()->items())
        if (it->data(0).toString() == QLatin1String("route-arrow") && it->data(1).toString() == on) ++n;
    return n;
}

}  // namespace

TEST(RoutesUi, GoTheOtherWayRunsTheWholeLoopRound) {
    ui::MainWindow w(library());
    w.resize(1400, 900);
    w.show();
    auto* view = w.findChild<ui::MapView*>();
    auto* panel = w.findChild<ui::RoutesPanel*>();
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    view->loadMap(std::move(res.map));
    const track::TrackNetwork net = track::trackNetworkOf(*view->currentMap(), library());
    const QString straight = aStraight(*view->currentMap());
    ASSERT_FALSE(straight.isEmpty());
    const QPointF a = net.pointAtPosition({ straight, 0, 1, 5 })->p;
    const QPointF b = net.pointAtPosition({ straight, 0, 1, 11 })->p;

    // Two places close together on one piece: the shortest way runs straight there.
    panel->findChild<QPushButton*>(QStringLiteral("routesNew"))->click();
    auto* text = view->routeDraftBanner()->findChild<QLabel*>(QStringLiteral("RouteDraftText"));
    auto* flip = view->routeDraftBanner()->findChild<QPushButton*>(QStringLiteral("RouteDraftFlip"));
    view->routeDraftClick(a, false);
    view->routeDraftClick(b, false);
    EXPECT_TRUE(text->text().contains(QStringLiteral("the way the arrows point"))) << text->text().toStdString();
    EXPECT_FALSE(flip->isHidden());
    EXPECT_GE(arrows(view, QStringLiteral("draft")), 1);
    const auto shortWay = track::findRoute(net, *view->routeDraft()->from, *view->routeDraft()->to);
    ASSERT_TRUE(std::holds_alternative<track::RouteFound>(shortWay));
    const track::RouteFound& near = std::get<track::RouteFound>(shortWay);
    EXPECT_LT(near.length, 10.0);

    // Go the other way: the whole loop round, back to the end from the other side.
    flip->click();
    ASSERT_TRUE(view->routeDraft()->facing);
    EXPECT_NE(*view->routeDraft()->facing, near.facing);
    EXPECT_TRUE(text->text().contains(QStringLiteral("the way the arrows point"))) << text->text().toStdString();
    EXPECT_GT(arrows(view, QStringLiteral("draft")), 1);
    view->routeDraftBanner()->findChild<QLineEdit*>(QStringLiteral("RouteDraftName"))->setText(QStringLiteral("Long way round"));
    view->routeDraftBanner()->findChild<QPushButton*>(QStringLiteral("RouteDraftSave"))->click();
    ASSERT_EQ(view->currentMap()->sidecar.routes.size(), 1u);
    const core::Route route = view->currentMap()->sidecar.routes[0];
    EXPECT_GT(route.steps.size(), 3u);
    EXPECT_EQ(route.steps.front().brick, route.steps.back().brick);  // round past its start, back onto the same piece
    EXPECT_EQ(track::routeFacing(route), near.facing == track::Facing::To ? track::Facing::From : track::Facing::To);
    const track::RouteStatus st = track::routeStatus(net, route);
    EXPECT_FALSE(st.broken);
    EXPECT_GT(st.length, 50.0);
    EXPECT_GT(arrows(view, route.id), 1);
    auto* row = panel->findChild<QFrame*>(QStringLiteral("route_") + route.id);
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("routeStatus"))->text().startsWith(QStringLiteral("OK")));
}

TEST(RoutesUi, ARouteStartedOnATrainGoesTheWayItFaces) {
    ui::MainWindow w(library());
    w.resize(1400, 900);
    w.show();
    auto* view = w.findChild<ui::MapView*>();
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    view->loadMap(std::move(res.map));
    const track::TrackNetwork net = track::trackNetworkOf(*view->currentMap(), library());
    const QString straight = aStraight(*view->currentMap());
    core::RollingStock car = core::simpleStock(QStringLiteral("car1"), 16);
    car.trackKind = QStringLiteral("monorail");
    core::Train t;
    t.id = QStringLiteral("t1");
    t.name = QStringLiteral("Shuttle");
    t.trackKind = QStringLiteral("monorail");
    t.units = { { car.id, false } };
    core::TrainPosition p;
    static_cast<core::TrackPosition&>(p) = { straight, 0, 1, 8 };
    p.facingTo = false;
    t.position = p;
    view->currentMap()->sidecar.rollingStock.push_back(car);
    view->currentMap()->sidecar.trains.push_back(t);
    view->startRouteDraft();
    view->routeDraftClick(net.pointAtPosition(p)->p, false);
    ASSERT_TRUE(view->routeDraft()->from);
    EXPECT_EQ(view->routeDraft()->train, QStringLiteral("t1"));
    ASSERT_TRUE(view->routeDraft()->facing);
    EXPECT_EQ(*view->routeDraft()->facing, track::Facing::From);
    auto* text = view->routeDraftBanner()->findChild<QLabel*>(QStringLiteral("RouteDraftText"));
    EXPECT_TRUE(text->text().contains(QStringLiteral("starts at Shuttle, the way it faces"))) << text->text().toStdString();
    // An end just ahead of it the other way: it goes round the loop, the way it faces, not backwards.
    view->routeDraftClick(net.pointAtPosition({ straight, 0, 1, 12 })->p, false);
    EXPECT_TRUE(view->saveRouteDraft(QStringLiteral("Shuttle run")));
    const core::Route route = view->currentMap()->sidecar.routes.at(0);
    EXPECT_EQ(route.train, QStringLiteral("t1"));
    EXPECT_EQ(track::routeFacing(route), track::Facing::From);
    EXPECT_GT(route.steps.size(), 3u);
    view->cancelRouteDraft();
}

TEST(RoutesUi, DrawKeepHideBreakMendAndDelete) {
    QSettings().remove(QStringLiteral("routes"));
    ui::MainWindow w(library());
    w.resize(1400, 900);
    w.show();
    auto* view = w.findChild<ui::MapView*>();
    auto* panel = w.findChild<ui::RoutesPanel*>();
    ASSERT_TRUE(view && panel);
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    ASSERT_TRUE(res.map);
    view->loadMap(std::move(res.map));
    const auto [a, b] = twoPlaces(*view->currentMap());
    EXPECT_TRUE(panel->findChild<QLabel*>(QStringLiteral("routesEmpty"))->text().startsWith(QStringLiteral("No routes yet")));

    // New route: two clicks on the track, a name, Save.
    panel->findChild<QPushButton*>(QStringLiteral("routesNew"))->click();
    ASSERT_TRUE(view->drawingRoute());
    ASSERT_TRUE(view->routeDraftBanner());
    auto* text = view->routeDraftBanner()->findChild<QLabel*>(QStringLiteral("RouteDraftText"));
    EXPECT_TRUE(text->text().contains(QStringLiteral("where it starts")));
    EXPECT_FALSE(panel->findChild<QPushButton*>(QStringLiteral("routesNew"))->isEnabled());
    view->routeDraftClick(QPointF(-500, -500), false);  // off the track: still waiting for the start
    EXPECT_FALSE(view->routeDraft()->from);
    view->routeDraftClick(a, false);
    EXPECT_TRUE(text->text().contains(QStringLiteral("where it ends")));
    view->routeDraftClick(b, false);
    EXPECT_TRUE(text->text().contains(QStringLiteral("along the track"))) << text->text().toStdString();
    const int steps = view->undoStack()->count();
    view->routeDraftBanner()->findChild<QLineEdit*>(QStringLiteral("RouteDraftName"))->setText(QStringLiteral("Monorail run"));
    view->routeDraftBanner()->findChild<QPushButton*>(QStringLiteral("RouteDraftSave"))->click();
    EXPECT_FALSE(view->drawingRoute());
    EXPECT_EQ(view->undoStack()->count(), steps + 1);
    ASSERT_EQ(view->currentMap()->sidecar.routes.size(), 1u);
    const core::Route route = view->currentMap()->sidecar.routes[0];
    EXPECT_EQ(route.name, QStringLiteral("Monorail run"));
    EXPECT_FALSE(view->builder()->routeItems().value(route.id).isEmpty());
    auto* row = panel->findChild<QFrame*>(QStringLiteral("route_") + route.id);
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("routeStatus"))->text().startsWith(QStringLiteral("OK")));

    // Hidden on my map only, and kept for next time.
    panel->setShown(route.id, false);
    EXPECT_TRUE(view->builder()->routeItems().value(route.id).isEmpty());
    EXPECT_FALSE(QSettings().value(QStringLiteral("routes/hidden/untitled")).toStringList().isEmpty());
    panel->setShown(route.id, true);
    EXPECT_FALSE(view->builder()->routeItems().value(route.id).isEmpty());

    // A piece taken out from under it: Broken, kept, drawn dashed red.
    const QString victim = route.steps[route.steps.size() / 2].brick;
    for (QGraphicsItem* it : view->scene()->items())
        it->setSelected(it->data(ui::detail::kBrickDataGuid).toString() == victim);
    view->deleteSelected();
    ASSERT_EQ(view->currentMap()->sidecar.routes.size(), 1u);
    row = panel->findChild<QFrame*>(QStringLiteral("route_") + route.id);
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("routeStatus"))->text().startsWith(QStringLiteral("Broken")));
    const auto drawn = view->builder()->routeItems().value(route.id);
    ASSERT_FALSE(drawn.isEmpty());
    for (QGraphicsItem* it : drawn)
        EXPECT_EQ(static_cast<QGraphicsPathItem*>(it)->pen().color().rgb(), rendering::SceneBuilder::routeBrokenColor().rgb());

    // Find a new way: round the other side of the loop.
    ASSERT_TRUE(row->findChild<QPushButton*>(QStringLiteral("routeFindNewWay")));
    row->findChild<QPushButton*>(QStringLiteral("routeFindNewWay"))->click();
    EXPECT_TRUE(panel->message().startsWith(QStringLiteral("Found a new way"))) << panel->message().toStdString();
    row = panel->findChild<QFrame*>(QStringLiteral("route_") + route.id);
    EXPECT_FALSE(row->findChild<QLabel*>(QStringLiteral("routeStatus"))->text().startsWith(QStringLiteral("Broken")));
    EXPECT_NE(view->currentMap()->sidecar.routes[0].steps, route.steps);

    // Delete asks first; No keeps it, Yes deletes it, Undo brings it back.
    bool asked = false;
    panel->setConfirm([&](const QString&) { return asked = true, false; });
    EXPECT_FALSE(panel->remove(route.id));
    EXPECT_TRUE(asked);
    panel->setConfirm([](const QString&) { return true; });
    EXPECT_TRUE(panel->remove(route.id));
    EXPECT_TRUE(view->currentMap()->sidecar.routes.empty());
    view->undoStack()->undo();
    EXPECT_EQ(view->currentMap()->sidecar.routes.size(), 1u);
    QSettings().remove(QStringLiteral("routes"));
}

TEST(RoutesUi, SetSwitchesSetsTheRoutesSwitchesInOneStep) {
    ui::MainWindow w(library());
    w.resize(1400, 900);
    w.show();
    auto* view = w.findChild<ui::MapView*>();
    auto* panel = w.findChild<ui::RoutesPanel*>();
    auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
    view->loadMap(std::move(res.map));
    // A route through the RC crossover's diverging way.
    const track::TrackNetwork net = track::trackNetworkOf(*view->currentMap(), library());
    const track::SwitchPoint* sw = nullptr;
    for (const auto& s : net.switches())
        if (net.trackOf(s.brick)->kinds[s.heel] == QStringLiteral("rail")) {
            sw = &s;
            break;
        }
    ASSERT_TRUE(sw);
    int other = sw->exits.front() == sw->defaultExit ? sw->exits.back() : sw->exits.front();
    core::Route r;
    r.id = QStringLiteral("r1");
    r.name = QStringLiteral("Across");
    r.from = { sw->brick, sw->heel, other, 1 };
    r.to = { sw->brick, sw->heel, other, 20 };
    r.steps = { { sw->brick, sw->heel, other } };
    view->undoStack()->push(new edit::SetRouteCommand(*view->currentMap(), r.id, r, QStringLiteral("test")));
    auto* row = panel->findChild<QFrame*>(QStringLiteral("route_r1"));
    ASSERT_TRUE(row);
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("routeStatus"))->text().contains(QStringLiteral("set the other way")));
    const int steps = view->undoStack()->count();
    row->findChild<QPushButton*>(QStringLiteral("routeSetSwitches"))->click();
    EXPECT_EQ(view->undoStack()->count(), steps + 1);
    EXPECT_EQ(view->currentMap()->sidecar.switchStates.value(sw->key, -1), other);
    row = panel->findChild<QFrame*>(QStringLiteral("route_r1"));
    EXPECT_TRUE(row->findChild<QLabel*>(QStringLiteral("routeStatus"))->text().startsWith(QStringLiteral("OK")));
}
