// The main window's shell, from the desktop UI review (area 6):
//   - the task tabs and Help stay on the toolbar at 1440 px with help
//     buttons on (the tools show only icons when the labels don't fit);
//   - switching touch mode keeps what you were looking at in view;
//   - View > Reset Panel Layout brings back panels floated away or closed;
//   - touch mode's buttons are a comfortable size, the toolbar's » too;
//   - "Browse the catalog on the web…" only while there is a server;
//   - the status bar says whether the layout is saved (the editor tour's
//     "Saving" step points at it);
//   - the Clubs tour, in Open from Server, only has steps that point at
//     something there;
//   - Map > Venue is the Venue Designer, the library and Remove Venue.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/ServerWindow.h"
#include "ui/TouchMode.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"
#include "ui/tours/Tours.h"
#include "parts/PartsLibrary.h"
#include "sync/ServerList.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>

using namespace bld;

namespace {

parts::PartsLibrary& library() {
    static parts::PartsLibrary lib;
    static bool scanned = false;
    if (!scanned) {
        const QString root = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        if (QDir(root).exists()) {
            lib.addSearchPath(root);
            lib.scan();
        }
        scanned = true;
    }
    return lib;
}

QString plain(QString t) { return t.remove(QLatin1Char('&')); }

class Shell : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QSettings().remove(QStringLiteral("ui"));
    }
    void TearDown() override {
        ui::TouchMode::instance().setActive(false);
        QSettings().remove(QStringLiteral("ui"));
        QStandardPaths::setTestModeEnabled(false);
    }
};

// Shown on the toolbar itself (not folded into its "»").
bool onToolbar(QToolBar* bar, QWidget* w) {
    return w && w->isVisible() && bar->rect().contains(QRect(w->mapTo(bar, QPoint(0, 0)), w->size()));
}

}  // namespace

TEST_F(Shell, TheTaskTabsAndHelpStayOnTheToolbar) {
    ui::theme::PrefsStore& prefs = ui::theme::PrefsStore::instance();
    ui::theme::AppPrefs p = prefs.prefs();
    const bool helpIcons = p.helpIcons;
    p.helpIcons = true;
    prefs.update(p);
    ui::MainWindow w(library());
    w.show();
    auto* bar = w.findChild<QToolBar*>(QStringLiteral("toolbar.main"));
    ASSERT_TRUE(bar);
    for (const int width : { 1920, 1440, 1100 }) {
        w.resize(width, 900);
        QTest::qWait(150);
        for (const char* name : { "task.build", "task.room", "task.notes", "task.parts" })
            EXPECT_TRUE(onToolbar(bar, w.findChild<QToolButton*>(QString::fromLatin1(name)))) << name << " at " << width;
        EXPECT_TRUE(onToolbar(bar, bar->findChild<QToolButton*>(QStringLiteral("HelpButton")))) << "Help at " << width;
    }
    // Wide, the tools keep their names under the icons; narrow, icons only.
    // (Wide enough only where the screen lets the window be that wide, and
    // the font fits: Windows' offscreen screen and fonts differ.)
    w.resize(2600, 900);
    QTest::qWait(150);
    if (w.width() >= 2400) {
        EXPECT_EQ(bar->toolButtonStyle(), Qt::ToolButtonTextUnderIcon) << "at " << w.width();
    }
    w.resize(1100, 900);
    QTest::qWait(150);
    EXPECT_EQ(bar->toolButtonStyle(), Qt::ToolButtonIconOnly);
    // Touch mode's bigger buttons: still there (the toolbar's "?" step aside).
    ui::theme::PrefsStore store(QStringLiteral("shell-test-theme"));
    ui::theme::ThemeManager themer(store);
    themer.apply();
    w.resize(1440, 900);
    ui::TouchMode::instance().setActive(true);
    QTest::qWait(300);
    for (const char* name : { "task.build", "task.room", "task.notes", "task.parts" })
        EXPECT_TRUE(onToolbar(bar, w.findChild<QToolButton*>(QString::fromLatin1(name)))) << name << " in touch mode";
    EXPECT_TRUE(onToolbar(bar, bar->findChild<QToolButton*>(QStringLiteral("HelpButton")))) << "Help in touch mode";
    ui::TouchMode::instance().setActive(false);
    qApp->setStyleSheet(QString());
    p.helpIcons = helpIcons;
    prefs.update(p);
}

TEST_F(Shell, TouchModeKeepsTheLayoutInView) {
    ui::theme::PrefsStore store(QStringLiteral("shell-test-theme"));
    ui::theme::ThemeManager themer(store);
    themer.apply();
    ui::MainWindow w(library());
    w.resize(1440, 900);
    w.show();
    ASSERT_TRUE(w.openFile(QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm")));
    auto* view = w.findChild<ui::MapView*>();
    view->fitAll();
    QTest::qWait(200);
    const QPointF before = view->mapToScene(view->viewport()->rect().center());
    ui::TouchMode::instance().setActive(true);
    QTest::qWait(400);
    const QPointF after = view->mapToScene(view->viewport()->rect().center());
    EXPECT_LT(QLineF(before, after).length() * view->transform().m11(), 4.0) << "moved on screen";
    ui::TouchMode::instance().setActive(false);
    QTest::qWait(400);
    const QPointF back = view->mapToScene(view->viewport()->rect().center());
    EXPECT_LT(QLineF(before, back).length() * view->transform().m11(), 4.0);
    qApp->setStyleSheet(QString());
}

TEST_F(Shell, TouchModeButtonsAreAComfortableSize) {
    ui::theme::PrefsStore store(QStringLiteral("shell-test-theme"));
    ui::theme::ThemeManager themer(store);
    ui::TouchMode::instance().setActive(true);
    themer.apply();
    QPushButton b(QStringLiteral("OK"));
    b.ensurePolished();
    EXPECT_GE(b.sizeHint().height(), ui::TouchMode::kComfortTarget);
    ui::MainWindow w(library());
    w.resize(700, 600);  // narrow: the toolbar's » shows
    w.show();
    QTest::qWait(200);
    auto* bar = w.findChild<QToolBar*>(QStringLiteral("toolbar.main"));
    auto* more = bar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
    ASSERT_TRUE(more);
    if (more->isVisible()) {
        EXPECT_GE(more->width(), ui::TouchMode::kComfortTarget);
    }
    ui::TouchMode::instance().setActive(false);
    qApp->setStyleSheet(QString());
}

TEST_F(Shell, ResetPanelLayoutBringsBackPanelsFloatedAwayOrClosed) {
    ui::MainWindow w(library());
    w.resize(1440, 900);
    w.show();
    QTest::qWait(100);
    QList<QDockWidget*> docks;
    for (auto* d : w.findChildren<QDockWidget*>())
        if (!d->isHidden()) docks << d;
    ASSERT_GE(docks.size(), 2);
    docks[0]->setFloating(true);
    docks[0]->move(-5000, -5000);
    docks[1]->hide();
    QAction* reset = w.findChild<QAction*>(QStringLiteral("view.resetPanels"));
    ASSERT_TRUE(reset);
    reset->trigger();
    QTest::qWait(100);
    EXPECT_FALSE(docks[0]->isFloating());
    EXPECT_FALSE(docks[0]->isHidden());
    EXPECT_FALSE(docks[1]->isHidden());
}

TEST_F(Shell, TheCatalogLinkShowsOnlyWithAServer) {
    ui::MainWindow w(library());
    w.show();
    QPushButton* link = nullptr;
    for (auto* b : w.findChildren<QPushButton*>(QStringLiteral("browseCatalog")))
        if (b->text().startsWith(QStringLiteral("Browse the catalog")) && b->toolTip().contains(QStringLiteral("Parts")))
            link = b;
    ASSERT_TRUE(link);
    EXPECT_TRUE(link->isHidden());  // no server: nothing to browse
}

TEST_F(Shell, TheStatusBarSaysWhetherTheLayoutIsSaved) {
    ui::MainWindow w(library());
    w.show();
    w.ensureDocument();
    auto* label = w.findChild<QLabel*>(QStringLiteral("SaveStatus"));
    ASSERT_TRUE(label);
    EXPECT_EQ(label->text(), QStringLiteral("Not saved yet"));
    ASSERT_TRUE(w.openFile(QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm")));
    EXPECT_EQ(label->text(), QStringLiteral("✓ Saved"));
    auto* view = w.findChild<ui::MapView*>();
    view->selectAll();
    view->nudgeSelected(1, 0);
    EXPECT_EQ(label->text(), QStringLiteral("● Not saved"));
    view->undoStack()->undo();
    EXPECT_EQ(label->text(), QStringLiteral("✓ Saved"));
    // The editor tour's "Saving" step points at it.
    EXPECT_EQ(ui::tours::findTarget(&w, QStringLiteral("topbar.saveStatus")), label);
}

TEST_F(Shell, TheClubsTourOnlyPointsAtWhatOpenFromServerHas) {
    ui::theme::PrefsStore store(QStringLiteral("shell-test-tours"));
    ui::MainWindow w(library());
    w.show();
    w.showServerWindow();
    ui::ServerWindow* sw = w.serverWindow();
    ASSERT_TRUE(sw);
    QTest::qWait(100);
    auto* o = ui::tours::startTour(sw, QStringLiteral("clubs"), store);
    ASSERT_TRUE(o);
    const auto& all = ui::tours::findTour(QStringLiteral("clubs"))->steps;
    EXPECT_LT(o->tour().steps.size(), all.size());  // no club yet: some are left out
    for (const auto& step : o->tour().steps)
        EXPECT_TRUE(ui::tours::findTarget(sw, step.target)) << step.target.toStdString();
    o->deleteLater();
    sw->close();
}

TEST_F(Shell, MapVenueIsTheDesignerTheLibraryAndRemove) {
    ui::MainWindow w(library());
    QStringList items;
    for (QAction* top : w.menuBar()->actions())
        if (plain(top->text()) == QStringLiteral("Map"))
            for (QAction* a : top->menu()->actions())
                if (a->menu() && plain(a->text()) == QStringLiteral("Venue"))
                    for (QAction* v : a->menu()->actions())
                        if (!v->isSeparator()) items << plain(v->text());
    EXPECT_EQ(items, (QStringList{ QStringLiteral("Open Venue Designer…"), QStringLiteral("Remove Venue…"),
                                   QStringLiteral("Venue library…"), QStringLiteral("Load Venue from File…") }));
}
