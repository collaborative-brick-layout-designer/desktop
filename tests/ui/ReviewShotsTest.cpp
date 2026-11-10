// BLD_REVIEW_SHOTS=<dir> BLD_PARTS=<library dirs, ':'-separated>: pictures
// of the desktop review fixes (2026-11-09, area 6) in the light and dark
// themes, to look at for a PR (not compared). BLD_REVIEW_TAG names them
// (review-<tag>-<scene>-light.png). Skipped unless both are set.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "sync/TokenStore.h"
#include "ui/LoadingCard.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/FindDialog.h"
#include "ui/ServerWindow.h"
#include "ui/TouchMode.h"
#include "ui/tours/Tours.h"
#include "ui/LibraryPathsDialog.h"
#include "ui/VenueDimensionsDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/help/ShortcutsDialog.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

using namespace bld;

namespace {

// A window the test can hand a keychain that can't save.
class ShotWindow : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::setTokenStore;
};

QString plain(QString t) { return t.remove(QLatin1Char('&')); }

QAction* actionNamed(QWidget& w, const QString& text) {
    for (QAction* a : w.findChildren<QAction*>())
        if (plain(a->text()) == text) return a;
    return nullptr;
}

// Runs `open`, grabs the modal dialog it shows (after `prepare`), then closes it.
void grabModal(const std::function<void()>& open, const QString& file,
               const std::function<void(QDialog*)>& prepare = {}) {
    QTimer::singleShot(400, [file, prepare] {
        auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!d) return;
        if (prepare) prepare(d);
        QTest::qWait(300);
        d->grab().save(file);
        d->reject();
    });
    open();
}

}  // namespace

TEST(ReviewShots, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    const QStringList libs = qEnvironmentVariable("BLD_PARTS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (out.isEmpty() || libs.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS / BLD_PARTS not set";
    const QString tag = qEnvironmentVariable("BLD_REVIEW_TAG", QStringLiteral("after"));
    QDir().mkpath(out);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    for (const QString& p : libs) lib.addSearchPath(p);
    ASSERT_GT(lib.scan(), 1000);
    ui::theme::PrefsStore store(QStringLiteral("review-shots"));
    ui::theme::ThemeManager themer(store);
    themer.apply();
    ShotWindow window(lib);
    window.resize(1440, 900);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.openFile(QStringLiteral(BLD_BBM_CORPUS_DIR "/fordyce-2026.bbm")));
    auto* view = window.findChild<ui::MapView*>();
    QElapsedTimer t;
    t.start();
    while (view->loadingCard() && view->loadingCard()->isVisible() && t.elapsed() < 30000) QTest::qWait(50);
    int missing = 0;
    for (const auto& L : view->currentMap()->layers()) {
        if (L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (!lib.metadata(b.partNumber) || lib.pixmap(b.partNumber).isNull()) ++missing;
    }
    ASSERT_EQ(missing, 0);

    for (const auto mode : { ui::theme::ThemeChoice::Light, ui::theme::ThemeChoice::Dark }) {
        ui::theme::AppPrefs p = store.prefs();
        p.theme = mode;
        store.update(p);
        QTest::qWait(600);
        const QString name = mode == ui::theme::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        const auto file = [&](const QString& scene) {
            return out + QStringLiteral("/review-%1-%2-%3.png").arg(tag, scene, name);
        };
        window.statusBar()->clearMessage();
        for (QFrame* card : window.findChildren<QFrame*>(QStringLiteral("NoticeCard"))) card->hide();

        // Settings: every setting in one window, with sections down the side.
        {
            ui::SettingsDialog d(store, QString(), {}, &window, {}, [] {});
            d.resize(940, 900);
            d.show();
            QTest::qWait(300);
            d.grab().save(file(QStringLiteral("settings-top")));
            auto* nav = d.findChild<QListWidget*>(QStringLiteral("settingsSections"));
            // The language list, each saying how far it got.
            if (auto* lang = d.findChild<QComboBox*>(QStringLiteral("general/language"))) {
                lang->showPopup();
                QTest::qWait(400);
                if (lang->view() && lang->view()->window()) lang->view()->window()->grab().save(file(QStringLiteral("languages")));
                lang->hidePopup();
            }
            for (const auto& [row, scene] : { std::pair{ 2, "settings-editing" }, std::pair{ 6, "settings-parts" } }) {
                if (!nav || row >= nav->count()) continue;
                nav->setCurrentRow(row);
                QTest::qWait(300);
                d.grab().save(file(QString::fromLatin1(scene)));
            }
        }
        // The window at 1440 px, help buttons on: the task tabs, Help and
        // "✓ Saved" all show; then in touch mode, the layout where it was.
        {
            ui::theme::AppPrefs hp = store.prefs();
            hp.helpIcons = true;
            store.update(hp);
            ui::theme::PrefsStore::instance().update(hp);
            view->fitAll();
            QTest::qWait(400);
            window.grab().save(file(QStringLiteral("main")));
            ui::TouchMode::instance().setActive(true);
            QTest::qWait(600);
            window.grab().save(file(QStringLiteral("main-touch")));
            ui::TouchMode::instance().setActive(false);
            QTest::qWait(400);
        }
        // Find & Replace stepping through matches.
        {
            ui::FindDialog d(*view, &window);
            d.show();
            if (auto* e = d.findChild<QLineEdit*>(QStringLiteral("findText"))) e->setText(QStringLiteral("straight"));
            QTest::qWait(400);
            if (auto* b = d.findChild<QPushButton*>(QStringLiteral("findNext"))) b->click();
            QTest::qWait(300);
            d.grab().save(file(QStringLiteral("find")));
            view->deselectAll();
        }
        // The Clubs tour in Open from Server, with no server yet.
        {
            window.showServerWindow();
            if (auto* sw = window.serverWindow()) {
                QTest::qWait(400);
                if (auto* o = ui::tours::startTour(sw, QStringLiteral("clubs"), store)) {
                    QTest::qWait(400);
                    sw->grab().save(file(QStringLiteral("tour-clubs")));
                    o->deleteLater();
                }
                sw->close();
            }
        }
        // The older dialogs, in plain words.
        {
            ui::LibraryPathsDialog d({ QStringLiteral("/usr/share/collaborative-brick-layout-designer/parts/BlueBrickParts/parts"),
                                       QStringLiteral("/home/someone/.local/share/Collaborative Brick Layout Designer/parts/TrixBrix/parts/with/a/long/path") },
                                     &window);
            d.show();
            QTest::qWait(300);
            d.grab().save(file(QStringLiteral("parts-folders")));
        }
        {
            ui::VenueDimensionsDialog d(&window);
            d.show();
            QTest::qWait(300);
            d.grab().save(file(QStringLiteral("venue-dimensions")));
        }
        if (QAction* a = actionNamed(window, QStringLiteral("Background Image…")))
            grabModal([a] { a->trigger(); }, file(QStringLiteral("background-image")));
        // The Map menu's Venue items, folded into the Venue Designer.
        for (QAction* top : window.menuBar()->actions()) {
            if (plain(top->text()) != QStringLiteral("Map") || !top->menu()) continue;
            for (QAction* a : top->menu()->actions())
                if (a->menu() && plain(a->text()) == QStringLiteral("Venue")) {
                    QMenu* m = a->menu();
                    m->popup(window.mapToGlobal(QPoint(200, 30)));
                    QTest::qWait(400);
                    m->grab().save(file(QStringLiteral("venue-menu")));
                    m->close();
                }
        }
        // Keyboard shortcuts: Undo and Redo once.
        {
            ui::help::ShortcutsDialog d(ui::help::collectShortcuts(window.menuBar()), &window);
            d.show();
            QTest::qWait(300);
            d.grab().save(file(QStringLiteral("shortcuts")));
        }
        // Export as Image with no file: OK waits.
        if (QAction* a = actionNamed(window, QStringLiteral("Export as Image…")))
            grabModal([a] { a->trigger(); }, file(QStringLiteral("export-image")), [](QDialog* d) {
                if (auto* path = d->findChild<QLineEdit*>(QStringLiteral("exportImagePath"))) path->clear();
            });
        // The Edit menu: its letters and Select Connected.
        for (QAction* top : window.menuBar()->actions()) {
            if (plain(top->text()) != QStringLiteral("Edit") || !top->menu()) continue;
            QMenu* m = top->menu();
            m->popup(window.mapToGlobal(QPoint(60, 30)));
            QTest::qWait(400);
            m->grab().save(file(QStringLiteral("edit-menu")));
            m->close();
        }
        // A sign-in the keychain couldn't keep.
        {
            auto tokens = std::make_shared<sync::MemoryTokenStore>();
            tokens->saveError = QStringLiteral("The name org.freedesktop.secrets was not provided by any .service files");
            window.setTokenStore(tokens);
            tokens->save(QUrl(QStringLiteral("https://layouts.club.example")), QStringLiteral("bld_pat_x"));
            QTest::qWait(400);
            window.grab().save(file(QStringLiteral("keychain")));
        }
    }
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
}
