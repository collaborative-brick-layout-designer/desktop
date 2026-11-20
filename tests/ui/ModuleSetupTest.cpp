// A module's table and power: the Table and power window (each choice
// written at once, typed heights read in the length unit), the Modules
// panel's line per module and its Tables and power summary, and the whole
// way through the app: ⋯ › Table and power… on a placed module, the
// summary following at once, and Undo taking it back.

#include "core/Map.h"
#include "core/Module.h"
#include "core/ModuleSetup.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/ModuleSetupDialog.h"
#include "ui/ModuleSetupText.h"
#include "ui/ModulesPanel.h"
#include "ui/UpdateCheck.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QUndoStack>

#include <memory>

using namespace bld;
using Unit = ui::ModuleSetupText::Unit;

namespace {

struct Harness {
    core::ModuleSetup setup;
    bool gone = false;
    int applied = 0;
    ui::ModuleSetupDialog dlg{ QStringLiteral("Depot"), Unit::FeetInches,
                               [this]() -> std::optional<core::ModuleSetup> {
                                   if (gone) return std::nullopt;
                                   return setup;
                               },
                               [this](const core::ModuleSetup& s, const QString&) {
                                   setup = s;
                                   ++applied;
                               } };
};

// English's plurals, as the app loads them ("1 module needs", not "1 module(s) need").
struct English {
    QTranslator t;
    English() {
        EXPECT_TRUE(t.load(QStringLiteral("bld_en"), QCoreApplication::applicationDirPath() + QStringLiteral("/../..")));
        QCoreApplication::installTranslator(&t);
    }
    ~English() { QCoreApplication::removeTranslator(&t); }
};

void pick(QComboBox* box, const QString& data) {
    const int i = box->findData(data);
    ASSERT_GE(i, 0) << data.toStdString();
    box->setCurrentIndex(i);
}

}  // namespace

TEST(ModuleSetupText, ReadsAndShowsHeightsInTheUnit) {
    const auto mm = ui::ModuleSetupText::parseHeightMm(QStringLiteral("30 3/4"), Unit::FeetInches);
    ASSERT_TRUE(mm);
    EXPECT_NEAR(*mm, 781.0, 0.5);
    EXPECT_EQ(ui::ModuleSetupText::formatHeight(*mm, Unit::FeetInches), QStringLiteral("30¾″"));
    EXPECT_NEAR(*ui::ModuleSetupText::parseHeightMm(QStringLiteral("2' 8\""), Unit::FeetInches), 812.8, 0.5);
    EXPECT_EQ(*ui::ModuleSetupText::parseHeightMm(QStringLiteral("78"), Unit::Metres), 780.0);
    EXPECT_EQ(*ui::ModuleSetupText::parseHeightMm(QStringLiteral("0.78 m"), Unit::Metres), 780.0);
    EXPECT_EQ(*ui::ModuleSetupText::parseHeightMm(QStringLiteral("98"), Unit::Studs), 784.0);
    EXPECT_EQ(ui::ModuleSetupText::formatHeight(780, Unit::Metres), QStringLiteral("78.0 cm"));
    EXPECT_EQ(ui::ModuleSetupText::formatHeight(784, Unit::Studs), QStringLiteral("98 studs"));
    for (const char* bad : { "", "tall", "-3", "0", "400 m" })
        EXPECT_FALSE(ui::ModuleSetupText::parseHeightMm(QString::fromUtf8(bad), Unit::FeetInches)) << bad;
}

TEST(ModuleSetupText, SaysTheLayoutSummaryAsTheWebDoes) {
    English english;
    std::vector<core::ModuleSetup> mods(4);
    mods[0].table = core::ModuleTable{ QStringLiteral("mils"), 781, false };
    mods[0].power = core::ModulePower{ true, 2, 60, {} };
    mods[1].table = core::ModuleTable{ QStringLiteral("mils"), 781, false };
    mods[1].power = core::ModulePower{ true, 9, 390, {} };
    mods[2].table = core::ModuleTable{ QStringLiteral("custom"), 812.8, true };
    mods[2].power = core::ModulePower{};
    const auto s = core::setupSummary(mods);
    EXPECT_EQ(ui::ModuleSetupText::summaryPower(s), QStringLiteral("Power: 2 modules need 11 outlets, ~450 W (not set for 1)"));
    EXPECT_EQ(ui::ModuleSetupText::summaryTables(s, Unit::FeetInches),
              (QStringList{ QStringLiteral("MILS: 2 — 30¾″ (2)"), QStringLiteral("Custom: 1 — 32″ to the rails (1)"),
                            QStringLiteral("Table not set: 1") }));
    EXPECT_EQ(ui::ModuleSetupText::powerText(core::ModulePower{ true, 2, 60, QStringLiteral("back") }),
              QStringLiteral("Power: 2 outlets, ~60 W, plug at the back"));
    EXPECT_EQ(ui::ModuleSetupText::summaryPower(core::setupSummary(std::vector<core::ModuleSetup>(1))),
              QStringLiteral("Power: not set for any module"));
}

TEST(ModuleSetupDialog, WritesEachChoiceAtOnceAndClearAllTakesThemOff) {
    Harness h;
    EXPECT_FALSE(h.dlg.heightEdit()->isEnabled());
    pick(h.dlg.typeBox(), QStringLiteral("mils"));
    ASSERT_TRUE(h.setup.table);
    EXPECT_EQ(h.setup.table->type, QStringLiteral("mils"));
    EXPECT_TRUE(h.dlg.heightEdit()->isEnabled());

    h.dlg.heightEdit()->setText(QStringLiteral("30 3/4"));
    h.dlg.commitHeight();
    ASSERT_TRUE(h.setup.table->heightMm);
    EXPECT_NEAR(*h.setup.table->heightMm, 781.0, 0.5);
    EXPECT_EQ(h.dlg.heightEdit()->text(), QStringLiteral("30¾″"));
    h.dlg.railButton()->setChecked(true);
    EXPECT_TRUE(h.setup.table->toRail);

    // Something it can't read says so and changes nothing.
    const int before = h.applied;
    h.dlg.heightEdit()->setText(QStringLiteral("tall"));
    h.dlg.commitHeight();
    EXPECT_FALSE(h.dlg.heightProblem()->isHidden());
    EXPECT_EQ(h.applied, before);

    pick(h.dlg.powerBox(), QStringLiteral("yes"));
    ASSERT_TRUE(h.setup.power);
    EXPECT_EQ(h.setup.power->outlets, 1);
    h.dlg.outletsBox()->setValue(3);
    h.dlg.wattsBox()->setValue(120);
    pick(h.dlg.sideBox(), QStringLiteral("back"));
    EXPECT_EQ(*h.setup.power, (core::ModulePower{ true, 3, 120, QStringLiteral("back") }));

    pick(h.dlg.powerBox(), QStringLiteral("no"));
    EXPECT_EQ(*h.setup.power, core::ModulePower{});
    h.dlg.clearButton()->click();
    EXPECT_TRUE(h.setup.empty());
    EXPECT_FALSE(h.dlg.clearButton()->isEnabled());
}

TEST(ModuleSetupDialog, FollowsChangesFromElsewhereAndClosesWhenTheModuleIsGone) {
    Harness h;
    h.setup.table = core::ModuleTable{ QStringLiteral("custom"), 900.0, false };
    h.dlg.refresh();
    EXPECT_EQ(h.dlg.typeBox()->currentData().toString(), QStringLiteral("custom"));
    EXPECT_EQ(h.dlg.heightEdit()->text(), QStringLiteral("35½″"));
    EXPECT_EQ(h.applied, 0);  // reading it back writes nothing
    h.dlg.show();
    h.gone = true;
    h.dlg.refresh();
    QCoreApplication::processEvents();
    EXPECT_FALSE(h.dlg.isVisible());
}

TEST(ModulesPanel, ShowsEachModulesTableAndPowerAndTheLayoutSummary) {
    English english;
    core::Map map;
    core::Module a;
    a.id = QStringLiteral("a");
    a.name = QStringLiteral("Depot");
    core::ModuleSetup s;
    s.table = core::ModuleTable{ QStringLiteral("mils"), 781, false };
    s.power = core::ModulePower{ true, 2, 60, QStringLiteral("back") };
    map.sidecar.modules.push_back(core::withSetup(a, s));
    core::Module b;
    b.id = QStringLiteral("b");
    b.name = QStringLiteral("Yard");
    map.sidecar.modules.push_back(b);
    ui::ModulesPanel panel;
    panel.setMap(&map);
    auto* list = panel.findChild<QListWidget*>();
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->item(0)->text().endsWith(QStringLiteral("\nMILS · 30¾″ · Power: 2 outlets, ~60 W, plug at the back")));
    EXPECT_FALSE(list->item(1)->text().contains(QLatin1Char('\n')));
    EXPECT_EQ(panel.summaryText(),
              QStringLiteral("Power: 1 module needs 2 outlets, ~60 W (not set for 1)\nMILS: 1 — 30¾″ (1)\nTable not set: 1"));

    // ⋯ › Table and power… asks for its window.
    list->setCurrentRow(1);
    QString asked;
    QObject::connect(&panel, &ui::ModulesPanel::setupRequested, [&](const QString& id) { asked = id; });
    QMenu menu;
    panel.fillMenu(menu, QStringLiteral("b"));
    QAction* setup = menu.findChild<QAction*>(QStringLiteral("moduleSetup"));
    ASSERT_NE(setup, nullptr);
    EXPECT_EQ(setup->text(), QStringLiteral("Table and power…"));
    setup->trigger();
    EXPECT_EQ(asked, QStringLiteral("b"));
}

// The whole way: a module in the layout, ⋯ › Table and power…, choices
// made in the window, the panel's summary following, and Undo.
TEST(ModuleSetupJourney, SetAModulesTableAndPowerSeeTheSummaryAndUndo) {
    English english;
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    auto window = std::make_unique<ui::MainWindow>(lib);
    window->resize(1200, 800);
    window->ensureDocument();
    window->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(window.get()));
    auto* view = window->findChild<ui::MapView*>();
    auto* panel = window->findChild<ui::ModulesPanel*>();
    ASSERT_TRUE(view && panel);
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Depot");
    view->currentMap()->sidecar.modules.push_back(m);
    panel->setMap(view->currentMap());
    EXPECT_TRUE(panel->summaryText().startsWith(QStringLiteral("Power: not set for any module")));

    bool sawDialog = false;
    QTimer::singleShot(0, [&] {
        auto* dlg = qobject_cast<ui::ModuleSetupDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dlg, nullptr);
        sawDialog = true;
        pick(dlg->typeBox(), QStringLiteral("mils"));
        dlg->heightEdit()->setText(QStringLiteral("30 3/4"));
        dlg->commitHeight();
        pick(dlg->powerBox(), QStringLiteral("yes"));
        dlg->outletsBox()->setValue(2);
        dlg->wattsBox()->setValue(60);
        // Behind the window, the summary has it already.
        EXPECT_TRUE(panel->summaryText().startsWith(QStringLiteral("Power: 1 module needs 2 outlets, ~60 W")));
        dlg->accept();
    });
    emit panel->setupRequested(QStringLiteral("m1"));
    ASSERT_TRUE(sawDialog);
    const core::ModuleSetup got = core::setupOf(view->currentMap()->sidecar.modules[0]);
    ASSERT_TRUE(got.table && got.power);
    EXPECT_EQ(got.table->type, QStringLiteral("mils"));
    EXPECT_EQ(got.power->outlets, 2);
    EXPECT_EQ(panel->summaryText(), QStringLiteral("Power: 1 module needs 2 outlets, ~60 W\nMILS: 1 — 30¾″ (1)"));

    // Each choice is an undo step; undoing them all takes it all back, and the summary follows.
    while (view->undoStack()->canUndo()) view->undoStack()->undo();
    QApplication::processEvents();
    EXPECT_TRUE(core::setupOf(view->currentMap()->sidecar.modules[0]).empty());
    EXPECT_TRUE(panel->summaryText().startsWith(QStringLiteral("Power: not set for any module")));
    view->undoStack()->setClean();
    window.reset();
    QStandardPaths::setTestModeEnabled(false);
}
