// Places on the desktop (Live Ops Phase 1): New place in the Places panel and
// a click on the track make one, with the cars it holds, drawn on the map;
// each change is one undo step and follows at once; View > Places hides them;
// a module's Places… shows only its own; a shared module's can't be changed.
// The web's places.spec.ts walks the same.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Places.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "track/TrackNetwork.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/PlacesPanel.h"
#include "ui/PlacesText.h"
#include "edit/ModuleCommands.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
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

// The stub of 9V track from (456, 144) to (456, 240): its middle, clear of the monorail.
const QPointF kStub(456, 152);

struct App {
    ui::MainWindow w{ library() };
    ui::MapView* view = nullptr;
    ui::PlacesPanel* panel = nullptr;
    App() {
        QSettings().remove(QStringLiteral("view/places"));
        w.resize(1400, 900);
        w.show();
        view = w.findChild<ui::MapView*>();
        panel = w.findChild<ui::PlacesPanel*>();
        auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
        view->loadMap(std::move(res.map));
        panel->show();
        panel->drawNow();
    }
    core::Map& map() { return *view->currentMap(); }
    template <class T> T* find(const QString& name) {
        panel->drawNow();
        return panel->findChild<T*>(name);
    }
    QJsonObject first() { return map().sidecar.layoutPlaces.at(0).toObject(); }
    // The places' words on the map.
    QStringList labels() { return view->builder()->placeLabels(); }
    void makeCannery() {
        find<QPushButton>(QStringLiteral("placesNew"))->click();
        ASSERT_TRUE(view->pickingPlace());
        ASSERT_TRUE(view->placePickClick(kStub, ui::MapView::PlacePick{}));
        auto* name = find<QLineEdit>(QStringLiteral("placeName"));
        ASSERT_TRUE(name);
        name->setText(QStringLiteral("Acme Cannery"));
        emit name->editingFinished();
    }
};

}  // namespace

TEST(PlacesUi, NewPlaceIsPutOnTheTrackAndChangedAtOnceWithUndo) {
    App a;
    EXPECT_TRUE(a.find<QLabel>(QStringLiteral("placesEmpty"))->text().startsWith(QStringLiteral("No places yet.")));
    a.makeCannery();
    ASSERT_EQ(a.map().sidecar.layoutPlaces.size(), 1);
    EXPECT_TRUE(core::places::validPlaces(a.map().sidecar.layoutPlaces));
    EXPECT_EQ(a.first().value(QLatin1String("name")).toString(), QStringLiteral("Acme Cannery"));
    EXPECT_FALSE(a.view->pickingPlace());
    const QString spot = a.first().value(QLatin1String("spots")).toArray().at(0).toObject().value(QLatin1String("id")).toString();
    EXPECT_EQ(a.find<QLabel>(QStringLiteral("spotInfo_") + spot)->text(), QStringLiteral("3 cars (from the track’s length)"));
    EXPECT_EQ(a.labels(), QStringList{ QStringLiteral("Acme Cannery\n3 cars") });

    // Typed capacity wins, at once, on the map too; undo takes it back.
    a.find<QSpinBox>(QStringLiteral("spotCars_") + spot)->setValue(2);
    EXPECT_EQ(a.find<QLabel>(QStringLiteral("spotInfo_") + spot)->text(), QStringLiteral("2 cars"));
    EXPECT_EQ(a.labels(), QStringList{ QStringLiteral("Acme Cannery\n2 cars") });
    a.view->undoStack()->undo();
    EXPECT_EQ(a.labels(), QStringList{ QStringLiteral("Acme Cannery\n3 cars") });
    EXPECT_EQ(a.find<QSpinBox>(QStringLiteral("spotCars_") + spot)->value(), 0);

    // A yard's spots have uses; words are kept once each.
    auto* kind = a.find<QComboBox>(QStringLiteral("placeKind"));
    kind->setCurrentIndex(kind->findData(QStringLiteral("yard")));
    emit kind->activated(kind->currentIndex());
    EXPECT_TRUE(a.find<QComboBox>(QStringLiteral("spotRole_") + spot));
    auto* takes = a.find<QLineEdit>(QStringLiteral("placeAccepts"));
    takes->setText(QStringLiteral("reefer, Reefer , milk tank,"));
    emit takes->editingFinished();
    EXPECT_EQ(a.first().value(QLatin1String("accepts")).toArray(), (QJsonArray{ QStringLiteral("reefer"), QStringLiteral("milk tank") }));

    // View > Places.
    auto* act = a.w.findChild<QAction*>(QStringLiteral("view.places"));
    ASSERT_TRUE(act);
    act->setChecked(false);
    EXPECT_TRUE(a.labels().isEmpty());
    act->setChecked(true);
    EXPECT_EQ(a.labels().size(), 1);

    // Deleted after asking; undo brings it back.
    a.panel->setConfirm([](const QString&) { return false; });
    a.panel->removeOpen();
    EXPECT_EQ(a.map().sidecar.layoutPlaces.size(), 1);
    a.panel->setConfirm([](const QString&) { return true; });
    a.panel->removeOpen();
    EXPECT_TRUE(a.map().sidecar.layoutPlaces.isEmpty());
    EXPECT_TRUE(a.labels().isEmpty());
    a.view->undoStack()->undo();
    EXPECT_EQ(a.labels().size(), 1);
}

TEST(PlacesUi, TrackThatGoesIsSaidAndPickedAgain) {
    App a;
    a.makeCannery();
    const QJsonObject spot = a.first().value(QLatin1String("spots")).toArray().at(0).toObject();
    const QString id = spot.value(QLatin1String("id")).toString();
    const QString gone = spot.value(QLatin1String("track")).toObject().value(QLatin1String("brick")).toString();
    for (auto& l : a.map().layers())
        if (l->kind() == core::LayerKind::Brick) {
            auto& bricks = static_cast<core::LayerBrick&>(*l).bricks;
            bricks.erase(std::remove_if(bricks.begin(), bricks.end(), [&](const core::Brick& b) { return b.guid == gone; }), bricks.end());
        }
    a.view->refreshScene();
    a.panel->setMap(&a.map());
    EXPECT_EQ(a.find<QLabel>(QStringLiteral("spotInfo_") + id)->text(), QStringLiteral("Its track was moved or removed. Pick it again."));
    a.find<QPushButton>(QStringLiteral("spotPick_") + id)->click();
    ASSERT_TRUE(a.view->pickingPlace());
    QTest::keyClick(a.view, Qt::Key_Escape);
    EXPECT_FALSE(a.view->pickingPlace());
    a.find<QPushButton>(QStringLiteral("spotPick_") + id)->click();
    ASSERT_TRUE(a.view->placePickClick(QPointF(456, 224), { QString(), a.panel->openId(), id }));
    EXPECT_TRUE(a.find<QLabel>(QStringLiteral("spotInfo_") + id)->text().contains(QStringLiteral("from the track’s length")));
}

TEST(PlacesUi, AModuleHasItsOwnAndASharedOneCantBeChanged) {
    App a;
    a.makeCannery();
    const QString brick = core::places::bricksOf(a.first()).at(0);
    auto* make = new edit::CreateModuleCommand(a.map(), QStringLiteral("Depot"), { { 0, brick } });
    const QString id = make->moduleId();
    a.view->undoStack()->push(make);
    ASSERT_EQ(a.map().sidecar.modules.back().places.size(), 1);
    EXPECT_TRUE(a.map().sidecar.layoutPlaces.isEmpty());
    EXPECT_EQ(a.labels(), QStringList{ QStringLiteral("Acme Cannery\n3 cars") });
    emit a.view->modulePlacesRequested(id);
    EXPECT_TRUE(a.find<QPushButton>(QStringLiteral("placesShowAll")));
    // Track that isn't the module's is refused; its own adds a place to it.
    EXPECT_FALSE(a.view->placePickClick(QPointF(456, 232), { id, QString(), QString() }));
    EXPECT_TRUE(a.view->placePickClick(kStub, { id, QString(), QString() }));
    EXPECT_EQ(a.map().sidecar.modules.back().places.size(), 2);
    EXPECT_EQ(ui::PlacesText::places(a.map().sidecar.modules.back().places), QStringLiteral("2 industries"));
    // New places on its track are its own without asking for it.
    EXPECT_TRUE(a.view->placePickClick(kStub, ui::MapView::PlacePick{}));
    EXPECT_EQ(a.map().sidecar.modules.back().places.size(), 3);

    for (auto& m : a.map().sidecar.modules)
        if (m.id == id) {
            m.libraryModuleId = QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b");
            m.libraryVersion = 1;
            m.shared = QJsonObject{ { QStringLiteral("by"), QStringLiteral("Alice") } };
        }
    a.panel->setMap(&a.map());
    EXPECT_FALSE(a.view->placePickClick(kStub, ui::MapView::PlacePick{}));
    EXPECT_EQ(a.map().sidecar.modules.back().places.size(), 3);
    a.panel->openPlace(id, a.map().sidecar.modules.back().places.at(0).toObject().value(QLatin1String("id")).toString(), false);
    EXPECT_TRUE(a.find<QLineEdit>(QStringLiteral("placeName"))->isReadOnly());
    EXPECT_FALSE(a.find<QPushButton>(QStringLiteral("placeDelete")));
}

TEST(PlacesUi, OffTheLayoutAndForAViewer) {
    App a;
    a.find<QPushButton>(QStringLiteral("placesNewOff"))->click();
    ASSERT_EQ(a.map().sidecar.layoutPlaces.size(), 1);
    EXPECT_EQ(a.first().value(QLatin1String("kind")).toString(), QStringLiteral("staging"));
    EXPECT_TRUE(a.first().value(QLatin1String("spots")).toArray().isEmpty());
    a.panel->setReadOnly(true);
    EXPECT_TRUE(a.find<QLineEdit>(QStringLiteral("placeName"))->isReadOnly());
    a.find<QPushButton>(QStringLiteral("placeBack"))->click();
    EXPECT_FALSE(a.find<QPushButton>(QStringLiteral("placesNew")));
}
