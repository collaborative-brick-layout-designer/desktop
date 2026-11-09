// Switching light / dark with a layout open: the Sheets panel showing, the
// "Adding to" menu built and the Parts panel full of pictures. Every widget
// redraws its icons for the new theme; nothing may crash or go missing.

#include "parts/PartsLibrary.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/LayerPanel.h"
#include "ui/LoadingCard.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPalette>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

using namespace bld;

TEST(ThemeSwitch, BackAndForthWithALayoutOpen) {
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    const QPalette palette = QApplication::palette();
    const QFont font = QApplication::font();
    const QString css = qApp->styleSheet();
    constexpr const char* kGroup = "test-theme-switch";
    QSettings().remove(QString::fromLatin1(kGroup));
    {
        parts::PartsLibrary lib;
        lib.addSearchPath(QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
        ASSERT_GT(lib.scan(), 100);
        ui::theme::loadFonts();
        ui::theme::PrefsStore store(QString::fromLatin1(kGroup));
        ui::theme::ThemeManager manager(store);
        // No theme yet: the first switch also sets the app's style, as the shots test did.
        ui::MainWindow window(lib);
        window.resize(1400, 900);
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        ASSERT_TRUE(window.openFile(QStringLiteral(BLD_BBM_CORPUS_DIR "/fordyce-2026.bbm")));
        auto* view = window.findChild<ui::MapView*>();
        QElapsedTimer t;
        t.start();
        while (view->loadingCard() && view->loadingCard()->isVisible() && t.elapsed() < 30000) QTest::qWait(50);
        // The Sheets panel shows, and the Parts panel lists pictures.
        auto* sheets = window.findChild<QListWidget*>(QStringLiteral("sheetList"));
        ASSERT_NE(sheets, nullptr);
        for (QWidget* w = sheets; w; w = w->parentWidget())
            if (auto* dock = qobject_cast<QDockWidget*>(w)) dock->show();
        ASSERT_TRUE(sheets->isVisible());
        const int sheetRows = sheets->count();
        ASSERT_GT(sheetRows, 0);
        for (auto* edit : window.findChildren<QLineEdit*>())
            if (edit->placeholderText().startsWith(QStringLiteral("Search parts"))) edit->setText(QStringLiteral("straight"));
        QTest::qWait(500);
        // The "Adding to" menu is built.
        QToolButton* adding = nullptr;
        for (auto* b : window.findChildren<QToolButton*>())
            if (b->text().startsWith(QStringLiteral("Adding to"))) adding = b;
        ASSERT_NE(adding, nullptr);
        ASSERT_NE(adding->menu(), nullptr);
        const int menuItems = static_cast<int>(adding->menu()->actions().size());
        ASSERT_GT(menuItems, sheetRows);

        // A theme change only redraws: the menu's own widgets stay. (Its
        // icons used to look like a show / hide to the Sheets panel, which
        // rebuilt this menu while Qt was still walking every widget.)
        QPointer<QCheckBox> fade = adding->menu()->findChild<QCheckBox*>(QStringLiteral("fadeOtherSheets"));
        ASSERT_TRUE(fade);
        int shownOrHidden = 0;
        QObject::connect(window.findChild<ui::LayerPanel*>(), &ui::LayerPanel::layerVisibilityChanged, [&] { ++shownOrHidden; });

        ui::theme::AppPrefs p = store.prefs();
        const auto use = [&](ui::theme::ThemeChoice choice, int waitMs) {
            p.theme = choice;
            store.update(p);  // the manager follows the store, as from Settings
            QTest::qWait(waitMs);
        };
        // Slowly, and then quickly while the pictures are still being drawn.
        for (int i = 0; i < 3; ++i) {
            use(ui::theme::ThemeChoice::Dark, 400);
            EXPECT_EQ(manager.mode(), ui::theme::Mode::Dark);
            use(ui::theme::ThemeChoice::Light, 400);
            EXPECT_EQ(manager.mode(), ui::theme::Mode::Light);
        }
        for (int i = 0; i < 6; ++i) {
            use(ui::theme::ThemeChoice::Dark, 0);
            use(ui::theme::ThemeChoice::Light, 0);
        }
        EXPECT_TRUE(fade) << "a theme change rebuilt the Adding to menu";
        EXPECT_EQ(shownOrHidden, 0) << "a theme change showed or hid a sheet";
        // And with a sheet picked and the "Adding to" menu rebuilt in between.
        for (int i = 0; i < sheets->count(); ++i) {
            sheets->setCurrentRow(i);
            use(i % 2 ? ui::theme::ThemeChoice::Light : ui::theme::ThemeChoice::Dark, 50);
        }
        use(ui::theme::ThemeChoice::Light, 300);
        EXPECT_EQ(sheets->count(), sheetRows);
        EXPECT_EQ(static_cast<int>(adding->menu()->actions().size()), menuItems);
        EXPECT_TRUE(sheets->isVisible());
        EXPECT_FALSE(window.grab().isNull());
    }
    QSettings().remove(QString::fromLatin1(kGroup));
    qApp->setStyleSheet(css);
    QApplication::setPalette(palette);
    QApplication::setFont(font);
    QStandardPaths::setTestModeEnabled(false);
}
