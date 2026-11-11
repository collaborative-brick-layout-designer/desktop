// BLD_THEME_SHOTS=<dir> BLD_PARTS=<library dirs, ':'-separated>: pictures of
// the main window with the Fordyce 2026 layout, and of Settings > Color,
// in the light and dark themes and a few colors, to look at (not
// compared). BLD_THEME_TAG names them (theme-<tag>-<look>-light.png).
// Before LEGO colors (no ui/theme/LegoColors.h) it pictures the original
// look only, so the same test pictures both.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "ui/LoadingCard.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/SettingsDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTest>
#include <QToolButton>

using namespace bld;

TEST(ThemeColorShots, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_THEME_SHOTS");
    const QStringList libs = qEnvironmentVariable("BLD_PARTS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (out.isEmpty() || libs.isEmpty()) GTEST_SKIP() << "BLD_THEME_SHOTS / BLD_PARTS not set";
    const QString tag = qEnvironmentVariable("BLD_THEME_TAG", QStringLiteral("after"));
    QDir().mkpath(out);
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    for (const QString& p : libs) lib.addSearchPath(p);
    ASSERT_GT(lib.scan(), 1000);
    ui::theme::loadFonts();
    const QString group = QStringLiteral("test-theme-shots");
    QSettings().remove(group);
    ui::theme::PrefsStore store(group);
    ui::theme::ThemeManager manager(store);
    ui::MainWindow window(lib);
    window.resize(1600, 900);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.openFile(QStringLiteral(BLD_BBM_CORPUS_DIR "/fordyce-2026.bbm")));
    auto* view = window.findChild<ui::MapView*>();
    QElapsedTimer t;
    t.start();
    while (view->loadingCard() && view->loadingCard()->isVisible() && t.elapsed() < 30000) QTest::qWait(50);
    // Every part in the layout is in the library and has its picture.
    int missing = 0;
    for (const auto& L : view->currentMap()->layers()) {
        if (L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (!lib.metadata(b.partNumber) || lib.pixmap(b.partNumber).isNull()) {
                ++missing;
                ADD_FAILURE() << "missing part " << b.partNumber.toStdString();
            }
    }
    ASSERT_EQ(missing, 0);
    const auto loadingParts = [&window] {
        for (auto* label : window.findChildren<QLabel*>())
            if (label->isVisible() && label->text().startsWith(QStringLiteral("Loading part pictures"))) return true;
        return false;
    };
    struct Look { QString name; QString accent; int lego; bool full; };
    QList<Look> looks{ { QStringLiteral("brick"), QStringLiteral("brick"), 0, false } };
#if __has_include("ui/theme/LegoColors.h")
    looks << Look{ QStringLiteral("earth-blue"), QStringLiteral("brick"), 140, true }
          << Look{ QStringLiteral("bright-yellow"), QStringLiteral("brick"), 24, true }
          << Look{ QStringLiteral("bright-red"), QStringLiteral("brick"), 21, true };
#endif
    for (const auto mode : { ui::theme::ThemeChoice::Light, ui::theme::ThemeChoice::Dark }) {
        const QString m = mode == ui::theme::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        for (const Look& look : looks) {
            ui::theme::AppPrefs p = store.prefs();
            p.theme = mode;
            p.accent = look.accent;
#if __has_include("ui/theme/LegoColors.h")
            p.legoColor = look.lego;
            p.colorStyle = look.full ? QStringLiteral("full") : QStringLiteral("original");
#endif
            store.update(p);
            // A new look draws the part pictures again: wait until they have
            // been loaded for a whole second.
            t.restart();
            QElapsedTimer quiet;
            quiet.start();
            while (quiet.elapsed() < 1000 && t.elapsed() < 180000) {
                QTest::qWait(100);
                if (loadingParts()) quiet.restart();
            }
            ASSERT_FALSE(loadingParts());
            window.statusBar()->clearMessage();
            QTest::qWait(300);
            window.grab().save(out + QStringLiteral("/theme-%1-window-%2-%3.png").arg(tag, look.name, m));
            if (look.name == QLatin1String("brick") || look.name == QLatin1String("earth-blue")) {
                ui::SettingsDialog dialog(store, QString());
                dialog.resize(940, 900);
                dialog.show();
                ASSERT_TRUE(QTest::qWaitForWindowExposed(&dialog));
                if (auto* tile = dialog.findChild<QToolButton*>(QStringLiteral("legoColorTile"));
                    tile && look.lego == 0)
                    tile->click();  // the list open, even before a LEGO color is picked
                if (auto* nav = dialog.findChild<QListWidget*>(QStringLiteral("settingsSections")))
                    nav->setCurrentRow(1);  // Color
                QTest::qWait(300);
                dialog.grab().save(out + QStringLiteral("/theme-%1-settings-%2-%3.png").arg(tag, look.name, m));
            }
        }
    }
    QSettings().remove(group);
}
