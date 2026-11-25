// Live Ops on the desktop (Live Ops Phase 3): waybills made for every car,
// jobs and their stops, the switch lists that follow a car's move, Delivered,
// and what can't be routed, each change one undo step. The web's
// liveOps.spec.ts walks the same.

#include "core/Map.h"
#include "core/Places.h"
#include "edit/PlaceCommands.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "track/Cuts.h"
#include "ui/LiveOpsPanel.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/OpsText.h"
#include "ui/PlacesPanel.h"
#include "ui/TrainsPanel.h"

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
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

QJsonObject place(const char* id, const char* name, const char* brick, int capacity, const QStringList& ships, const QStringList& accepts) {
    return { { QStringLiteral("id"), QLatin1String(id) },
             { QStringLiteral("name"), QLatin1String(name) },
             { QStringLiteral("kind"), QStringLiteral("industry") },
             { QStringLiteral("spots"), *brick ? QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("s1") }, { QStringLiteral("name"), QStringLiteral("Dock") },
                                                                          { QStringLiteral("track"), QJsonObject{ { QStringLiteral("brick"), QLatin1String(brick) }, { QStringLiteral("from"), 0 }, { QStringLiteral("to"), 1 } } },
                                                                          { QStringLiteral("capacity"), capacity } } }
                                               : QJsonArray() },
             { QStringLiteral("accepts"), QJsonArray::fromStringList(accepts) },
             { QStringLiteral("ships"), QJsonArray::fromStringList(ships) },
             { QStringLiteral("tags"), QJsonArray() },
             { QStringLiteral("notes"), QString() } };
}

// The stub of 9V track from (456, 144) to (456, 240): a cannery at its top, a mill at its bottom, a car at each.
struct App {
    ui::MainWindow w{ library() };
    ui::MapView* view = nullptr;
    ui::LiveOpsPanel* panel = nullptr;
    explicit App(bool withPlaces = true, bool withCars = true) {
        w.resize(1400, 900);
        w.show();
        view = w.findChild<ui::MapView*>();
        panel = w.findChild<ui::LiveOpsPanel*>();
        auto res = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bbm-corpus/tight-corner.bbm"));
        auto& sc = res.map->sidecar;
        if (withPlaces)
            sc.layoutPlaces = QJsonArray{ place("cannery", "Acme Cannery", "141", 1, { QStringLiteral("boxcar") }, { QStringLiteral("tank") }),
                                          place("mill", "Mill", "322", 2, { QStringLiteral("tank") }, { QStringLiteral("Boxcar") }),
                                          place("world", "World", "", 0, {}, {}) };
        if (withCars)
            for (const auto& [id, number, type, brick] : { std::tuple{ "c1", "101", "Boxcar", "1372" }, std::tuple{ "c2", "102", "Tank", "1339" } }) {
                core::RollingStock s = core::simpleStock(QLatin1String(id), 20);
                s.number = QLatin1String(number);
                s.type = QLatin1String(type);
                s.trucks = { { 5, 30 }, { -5, 30 } };
                s.opsCut = QStringLiteral("cut-") + QLatin1String(id);
                sc.rollingStock.push_back(s);
                core::Train t;
                t.id = *s.opsCut;
                t.cut = true;
                t.units = { { s.id, false } };
                core::TrainPosition p;
                p.brick = QLatin1String(brick);
                p.to = 1;
                p.s = 8;
                t.position = p;
                sc.trains.push_back(t);
            }
        view->loadMap(std::move(res.map));
        panel->show();
        panel->setConfirm([](const QString&) { return true; });
        panel->drawNow();
    }
    core::Map& map() { return *view->currentMap(); }
    template <class T> T* find(const QString& name) {
        panel->drawNow();
        return panel->widget()->findChild<T*>(name);
    }
    template <class T> T* in(const QString& box, const QString& name) {
        auto* b = find<QWidget>(box);
        return b ? b->findChild<T*>(name) : nullptr;
    }
    void click(const QString& name) {
        auto* b = find<QPushButton>(name);
        ASSERT_TRUE(b) << name.toStdString();
        b->click();
    }
    void pick(const QString& name, const QString& value) {
        auto* box = find<QComboBox>(name);
        ASSERT_TRUE(box) << name.toStdString();
        const int i = box->findData(value);
        ASSERT_GE(i, 0) << value.toStdString();
        box->setCurrentIndex(i);
        emit box->activated(i);
    }
    QString words(const QString& box) {
        auto* b = find<QWidget>(box);
        if (!b) return {};
        QStringList out;
        for (auto* l : b->findChildren<QLabel*>()) out << l->text();
        return out.join(QLatin1Char('\n'));
    }
    QString job(const QString& name, const QStringList& stops) {
        panel->showTab(ui::LiveOpsPanel::Tab::Jobs);
        click(QStringLiteral("opsNewJob"));
        auto* n = find<QLineEdit>(QStringLiteral("opsJobName"));
        if (!n) return {};
        n->setText(name);
        emit n->editingFinished();
        for (const QString& s : stops) pick(QStringLiteral("opsAddStop"), s);
        return panel->openJobId();
    }
};

const QString kList = QStringLiteral("opsSwitchList");

}  // namespace

TEST(LiveOpsUi, MakeWaybillsAndJobsAndWorkTheSwitchList) {
    App a;
    EXPECT_EQ(a.in<QLabel>(QStringLiteral("opsCar_c1"), QStringLiteral("opsCarAt"))->text(), QStringLiteral("At Acme Cannery, Dock"));
    a.panel->showTab(ui::LiveOpsPanel::Tab::Waybills);
    EXPECT_TRUE(a.find<QLabel>(QStringLiteral("opsNoWaybills")));
    a.click(QStringLiteral("opsMake"));
    EXPECT_TRUE(a.words(QStringLiteral("opsMaking")).contains(QStringLiteral("101: Loaded to Mill")));
    a.click(QStringLiteral("opsMakeAdd"));
    EXPECT_EQ(a.map().sidecar.waybills.size(), 2);
    EXPECT_TRUE(a.words(QStringLiteral("opsCant")).contains(QStringLiteral("101: No job goes from Acme Cannery to Mill.")));
    const QString turn = a.job(QStringLiteral("Mill Turn"), { QStringLiteral("cannery"), QStringLiteral("mill") });
    EXPECT_TRUE(a.words(kList).contains(QStringLiteral("Pick up 101 for Mill")));
    EXPECT_TRUE(a.words(kList).contains(QStringLiteral("Set out 101 at Dock")));
    a.job(QStringLiteral("Cannery Turn"), { QStringLiteral("mill"), QStringLiteral("cannery") });
    EXPECT_TRUE(a.words(kList).contains(QStringLiteral("Pick up 102 for Acme Cannery")));
    EXPECT_FALSE(a.find<QWidget>(QStringLiteral("opsCant")));
    a.panel->showTab(ui::LiveOpsPanel::Tab::Jobs);
    EXPECT_EQ(a.in<QLabel>(QStringLiteral("opsJob_") + turn, QStringLiteral("opsJobMoves"))->text(), QStringLiteral("Local · 2 stops · 2 moves"));

    // 101 is moved to the mill: it is there, and its job's list is done at once.
    core::TrainPosition p;
    p.brick = QStringLiteral("1369");
    p.to = 1;
    p.s = 2;
    for (auto& t : a.map().sidecar.trains)
        if (t.id == QLatin1String("cut-c1")) {
            p.s = 14;
            t.position = p;
        }
    a.panel->openJob(turn);
    if (a.panel->view().at.value(QStringLiteral("c1")).place != QLatin1String("mill")) {
        for (auto& t : a.map().sidecar.trains)
            if (t.id == QLatin1String("cut-c1")) t.position->brick = QStringLiteral("1339");
        a.panel->drawNow();
    }
    EXPECT_EQ(a.panel->view().at.value(QStringLiteral("c1")).place, QStringLiteral("mill"));
    EXPECT_TRUE(a.words(kList).contains(QStringLiteral("Nothing to do")));
    a.panel->showTab(ui::LiveOpsPanel::Tab::Cars);
    EXPECT_TRUE(a.in<QLabel>(QStringLiteral("opsCar_c1"), QStringLiteral("opsCarNext"))->text().contains(QStringLiteral("it’s there")));
    a.in<QPushButton>(QStringLiteral("opsCar_c1"), QStringLiteral("opsDelivered"))->click();
    EXPECT_EQ(a.in<QLabel>(QStringLiteral("opsCar_c1"), QStringLiteral("opsCarNext"))->text(), QStringLiteral("Nowhere to go: it stays."));
    EXPECT_EQ(a.map().sidecar.rollingStock.at(0).opsLoad, QStringLiteral("empty"));
    a.view->undoStack()->undo();
    EXPECT_TRUE(a.in<QLabel>(QStringLiteral("opsCar_c1"), QStringLiteral("opsCarNext"))->text().contains(QStringLiteral("Loaded to Mill")));
}

TEST(LiveOpsUi, AWaybillByHandOneOffTheLayoutAndStartingItAgain) {
    App a;
    a.in<QPushButton>(QStringLiteral("opsCar_c1"), QStringLiteral("opsCarWaybill"))->click();
    const QString id = a.panel->openWaybillId();
    ASSERT_FALSE(id.isEmpty());
    a.pick(QStringLiteral("opsLegTo"), QStringLiteral("world"));
    a.pick(QStringLiteral("opsLegLoad"), QStringLiteral("loaded"));
    auto* text = a.find<QLineEdit>(QStringLiteral("opsLegNote1"));
    text->setText(QStringLiteral("Ice first"));
    emit text->editingFinished();
    const QJsonObject r = a.map().sidecar.waybills.at(0).toObject();
    EXPECT_EQ(r.value(QLatin1String("to1")).toString(), QStringLiteral("world"));
    EXPECT_EQ(r.value(QLatin1String("note1")).toString(), QStringLiteral("Ice first"));
    auto* gone = a.find<QPushButton>(QStringLiteral("opsDelivered"));
    ASSERT_TRUE(gone);
    EXPECT_EQ(gone->text(), QStringLiteral("It has left the layout"));
    gone->click();
    EXPECT_FALSE(track::standingIn(QStringLiteral("c1"), track::standingTrains(a.map().sidecar)));
    a.panel->drawNow();
    EXPECT_EQ(a.panel->view().at.value(QStringLiteral("c1")).place, QStringLiteral("world"));
    a.click(QStringLiteral("opsRestart"));
    EXPECT_EQ(a.map().sidecar.waybills.at(0).toObject().value(QLatin1String("leg")).toInt(), 1);
    a.view->undoStack()->undo();
    // Leaving the layout is one step.
    a.view->undoStack()->undo();
    EXPECT_TRUE(track::standingIn(QStringLiteral("c1"), track::standingTrains(a.map().sidecar)));
    a.pick(QStringLiteral("opsWaybillCar"), QStringLiteral("c2"));
    EXPECT_EQ(edit::pocketOf(a.map(), QStringLiteral("c2")), id);
    a.click(QStringLiteral("opsWaybillDelete"));
    EXPECT_TRUE(a.map().sidecar.waybills.isEmpty());
    a.view->undoStack()->undo();
    EXPECT_EQ(edit::pocketOf(a.map(), QStringLiteral("c2")), id);
}

TEST(LiveOpsUi, DeletingAPlaceSaysWhatNamesItAndLosesNothing) {
    App a;
    a.in<QPushButton>(QStringLiteral("opsCar_c1"), QStringLiteral("opsCarWaybill"))->click();
    a.pick(QStringLiteral("opsLegTo"), QStringLiteral("mill"));
    a.job(QStringLiteral("Turn"), { QStringLiteral("cannery"), QStringLiteral("mill") });
    auto* places = a.w.findChild<ui::PlacesPanel*>();
    QString said;
    places->setConfirm([&](const QString&, const QString& uses) {
        said = uses;
        return true;
    });
    places->openPlace(QString(), QStringLiteral("mill"), false);
    places->drawNow();
    places->removeOpen();
    EXPECT_TRUE(said.startsWith(QStringLiteral("In use by 1 waybill, 1 job:"))) << said.toStdString();
    EXPECT_TRUE(a.words(QStringLiteral("opsCant")).contains(QStringLiteral("101: The place it goes to was deleted.")));
    EXPECT_EQ(a.map().sidecar.waybills.size(), 1);
    a.view->undoStack()->undo();
    EXPECT_FALSE(a.find<QWidget>(QStringLiteral("opsCant")));
    // A car deleted: its waybill waits for another.
    auto* trains = a.w.findChild<ui::TrainsPanel*>();
    QString asked;
    trains->setConfirm([&](const QString& q) {
        asked = q;
        return true;
    });
    trains->removeStock(QStringLiteral("c1"));
    EXPECT_TRUE(asked.contains(QStringLiteral("Its waybill stays")));
    EXPECT_EQ(a.map().sidecar.waybills.size(), 1);
    a.panel->showTab(ui::LiveOpsPanel::Tab::Waybills);
    EXPECT_EQ(a.find<QPushButton>(QStringLiteral("opsOpenWaybill"))->text(), QStringLiteral("Waiting for a car"));
}

TEST(LiveOpsUi, NoDeadEndsACarsMenuAndLookingOnly) {
    App none(false, false);
    EXPECT_TRUE(none.find<QPushButton>(QStringLiteral("opsOpenPlaces")));
    App empty(true, false);
    empty.click(QStringLiteral("opsOpenTrains"));
    EXPECT_TRUE(empty.w.findChild<ui::TrainsPanel*>()->isVisible());
    App a;
    a.panel->hide();
    QMenu* menu = a.view->makeCarMenu(QStringLiteral("c1"));
    ASSERT_TRUE(menu);
    auto* act = menu->findChild<QAction*>(QStringLiteral("CarOps"));
    ASSERT_TRUE(act);
    act->trigger();
    delete menu;
    EXPECT_TRUE(a.panel->isVisible());
    a.panel->setReadOnly(true);
    EXPECT_FALSE(a.in<QCheckBox>(QStringLiteral("opsCar_c1"), QStringLiteral("opsHold"))->isEnabled());
    a.panel->showTab(ui::LiveOpsPanel::Tab::Jobs);
    EXPECT_FALSE(a.find<QPushButton>(QStringLiteral("opsNewJob")));
    a.panel->setReadOnly(false);
    a.panel->showTab(ui::LiveOpsPanel::Tab::Cars);
    a.in<QCheckBox>(QStringLiteral("opsCar_c1"), QStringLiteral("opsHold"))->click();
    EXPECT_EQ(a.map().sidecar.rollingStock.at(0).opsHold, true);
    a.view->undoStack()->undo();
    EXPECT_FALSE(a.map().sidecar.rollingStock.at(0).opsHold.has_value());
}
