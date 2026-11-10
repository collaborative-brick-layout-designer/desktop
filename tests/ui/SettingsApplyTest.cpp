// What Settings changes reaches the window at once (no restart, no reopen):
// the zoom speed the wheel uses, the snap and turn steps the toolbar shows,
// and "Open the layout I had open last time" at the next start. These were
// saved but never read before (Bug 3).

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/SettingsDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include <cmath>

using namespace bld;

namespace {

parts::PartsLibrary& library() {
    static parts::PartsLibrary lib;
    return lib;
}

// One wheel notch on the view: how much it zoomed.
double notch(ui::MapView& view) {
    const double before = view.transform().m11();
    const QPointF at = view.viewport()->rect().center();
    QWheelEvent e(at, view.viewport()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                  Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &e);
    return view.transform().m11() / before;
}

class SettingsApply : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        for (const char* k : kKeys) QSettings().remove(QString::fromLatin1(k));
    }
    void TearDown() override {
        for (const char* k : kKeys) QSettings().remove(QString::fromLatin1(k));
        QStandardPaths::setTestModeEnabled(false);
    }
    static constexpr const char* kKeys[] = { "general/wheelZoomFactor", "editing/snapStepStuds",
                                             "editing/rotationStepDegrees", "general/reopenLastFile",
                                             "recent/lastFile" };
};

// Opens Settings from the window, runs `change` in it, and closes it.
void inSettings(ui::MainWindow& w, const std::function<void(ui::SettingsDialog&)>& change) {
    QTimer::singleShot(0, [change] {
        auto* d = qobject_cast<ui::SettingsDialog*>(QApplication::activeModalWidget());
        ASSERT_TRUE(d);
        change(*d);
        d->accept();
    });
    w.findChild<QAction*>(QStringLiteral("action.settings"))->trigger();
}

}  // namespace

TEST_F(SettingsApply, ZoomSpeedIsWhatTheWheelUses) {
    ui::MainWindow w(library());
    w.resize(1000, 700);
    w.show();
    w.ensureDocument();
    auto* view = w.findChild<ui::MapView*>();
    ASSERT_TRUE(view);
    view->setTransform(QTransform::fromScale(0.5, 0.5));
    EXPECT_NEAR(notch(*view), 1.2, 1e-3);  // Normal, as before
    inSettings(w, [](ui::SettingsDialog& d) {
        auto* zoom = d.findChild<QComboBox*>(QStringLiteral("general/wheelZoomFactor"));
        zoom->setCurrentIndex(zoom->findData(ui::settings::kZoomFast));
    });
    EXPECT_NEAR(notch(*view), 1.4, 1e-3);
    inSettings(w, [](ui::SettingsDialog& d) {
        auto* zoom = d.findChild<QComboBox*>(QStringLiteral("general/wheelZoomFactor"));
        zoom->setCurrentIndex(zoom->findData(ui::settings::kZoomGentle));
    });
    EXPECT_NEAR(notch(*view), 1.1, 1e-3);
    // And the next window starts with it.
    ui::MainWindow next(library());
    EXPECT_NEAR(next.findChild<ui::MapView*>()->wheelZoomFactor(), 1.1, 1e-9);
}

TEST_F(SettingsApply, SnapAndTurnStepsShowOnTheToolbarAtOnce) {
    ui::MainWindow w(library());
    w.show();
    inSettings(w, [](ui::SettingsDialog& d) {
        auto* snap = d.findChild<QComboBox*>(QStringLiteral("editing/snapStepStuds"));
        snap->setCurrentIndex(snap->findData(8.0));
        auto* turn = d.findChild<QComboBox*>(QStringLiteral("editing/rotationStepDegrees"));
        turn->setCurrentIndex(turn->findData(45.0));
    });
    auto* view = w.findChild<ui::MapView*>();
    EXPECT_EQ(view->snapStepStuds(), 8.0);
    EXPECT_EQ(view->rotationStepDegrees(), 45.0);
    auto* snapBtn = w.findChild<QToolButton*>(QStringLiteral("tool.snap"));
    auto* turnBtn = w.findChild<QToolButton*>(QStringLiteral("tool.angle"));
    ASSERT_TRUE(snapBtn && turnBtn);
    EXPECT_TRUE(snapBtn->isChecked());
    EXPECT_EQ(snapBtn->text(), QStringLiteral("Snap 8"));
    EXPECT_EQ(turnBtn->text(), QStringLiteral("Turn 45°"));
    inSettings(w, [](ui::SettingsDialog& d) {
        auto* snap = d.findChild<QComboBox*>(QStringLiteral("editing/snapStepStuds"));
        snap->setCurrentIndex(snap->findData(0.0));
    });
    EXPECT_FALSE(snapBtn->isChecked());
    EXPECT_EQ(snapBtn->text(), QStringLiteral("Snap off"));
}

TEST_F(SettingsApply, TheLastLayoutReopensOnlyWhileSettingsSaysSo) {
    QTemporaryDir dir;
    const QString last = dir.filePath(QStringLiteral("show.bbm"));
    QFile f(last);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();
    QSettings().setValue(QStringLiteral("recent/lastFile"), last);
    EXPECT_EQ(ui::MainWindow::fileToReopen(), last);  // on unless turned off
    QSettings().setValue(QStringLiteral("general/reopenLastFile"), false);
    EXPECT_TRUE(ui::MainWindow::fileToReopen().isEmpty());
    QSettings().setValue(QStringLiteral("general/reopenLastFile"), true);
    QFile::remove(last);
    EXPECT_TRUE(ui::MainWindow::fileToReopen().isEmpty());  // gone since
}
