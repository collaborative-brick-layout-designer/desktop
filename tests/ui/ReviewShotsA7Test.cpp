// BLD_REVIEW_SHOTS=<dir> BLD_PARTS=<library dirs, ':'-separated>: pictures
// of the Budget menu, the Help menu and the Budget window, light and dark,
// for a PR (skipped otherwise). Finds everything by its menu text, so the
// same file takes the "before" pictures on an older build.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QMenu>
#include <QMenuBar>
#include <QStandardPaths>
#include <QStatusBar>

using namespace bld;

namespace {

void settle(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
}

QAction* byText(QWidget& w, const QString& text) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == text) return a;
    return nullptr;
}

QMenu* topMenu(QMenuBar* bar, const QString& text) {
    for (QAction* a : bar->actions())
        if (a->text() == text) return a->menu();
    return nullptr;
}

}  // namespace

TEST(ReviewShotsA7, BudgetAndHelpPictures) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    const QStringList libs = qEnvironmentVariable("BLD_PARTS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (out.isEmpty() || libs.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS / BLD_PARTS not set";
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary parts;
    for (const QString& p : libs) parts.addSearchPath(p);
    ASSERT_GT(parts.scan(), 1000);
    ui::theme::PrefsStore store(QStringLiteral("review-a7-shots"));
    ui::theme::ThemeManager themer(store);
    ui::MainWindow window(parts);
    window.resize(1440, 900);
    window.show();
    settle(300);
    ASSERT_TRUE(window.openFile(QStringLiteral(BLD_BBM_CORPUS_DIR) + QStringLiteral("/fordyce-2026.bbm")));
    auto* view = window.findChild<ui::MapView*>();
    ASSERT_NE(view, nullptr);
    settle(500);
    int missing = 0;
    for (const auto& L : view->currentMap()->layers())
        if (L->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                if (!parts.metadata(b.partNumber) || parts.pixmap(b.partNumber).isNull()) ++missing;
    ASSERT_EQ(missing, 0);

    for (const auto mode : { ui::theme::ThemeChoice::Light, ui::theme::ThemeChoice::Dark }) {
        ui::theme::AppPrefs p = store.prefs();
        p.theme = mode;
        store.update(p);
        settle(600);
        const QString suffix = mode == ui::theme::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        window.statusBar()->clearMessage();
        for (const auto& [title, name] : { std::pair{ QStringLiteral("&Budget"), QStringLiteral("budget-menu") },
                                           std::pair{ QStringLiteral("&Help"), QStringLiteral("help-menu") } }) {
            QMenu* menu = topMenu(window.menuBar(), title);
            ASSERT_NE(menu, nullptr) << title.toStdString();
            menu->popup(window.mapToGlobal(QPoint(200, 40)));
            settle(300);
            menu->grab().save(out + QStringLiteral("/%1-%2.png").arg(name, suffix));
            menu->hide();
            settle(100);
        }
        // The Budget window, with a budget (New Budget first, when there is none yet).
        if (QAction* newAct = byText(window, QStringLiteral("&New Budget")); newAct && newAct->isEnabled()) {
            QAction* edit = byText(window, QStringLiteral("&Edit Budget…"));
            if (edit && !edit->isEnabled()) newAct->trigger();
        }
        QAction* edit = byText(window, QStringLiteral("&Edit Budget…"));
        ASSERT_NE(edit, nullptr);
        edit->trigger();
        settle(500);
        QDialog* dlg = nullptr;
        for (QDialog* d : window.findChildren<QDialog*>())
            if (d->isVisible() && d->windowTitle().startsWith(QStringLiteral("Budget"))) dlg = d;
        ASSERT_NE(dlg, nullptr);
        dlg->grab().save(out + QStringLiteral("/budget-dialog-%1.png").arg(suffix));
        dlg->close();
        settle(100);
        window.statusBar()->clearMessage();
        window.grab().save(out + QStringLiteral("/main-window-%1.png").arg(suffix));
    }
}
