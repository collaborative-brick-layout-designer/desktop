// BLD_SHEET_SHOTS=<dir> BLD_PARTS=<library dirs, ':'-separated>: pictures of
// the main window with the Fordyce 2026 layout and its "Tables" sheet
// active, in the light and dark themes, to look at (not compared).
// BLD_SHEET_TAG names them (desk-<tag>-light.png). Uses only what the app
// had before the "Adding to" button, so the same test pictures both.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/LoadingCard.h"
#include "ui/UpdateCheck.h"
#include "ui/NoticeArea.h"
#include "ui/PartsBrowser.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUndoStack>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTest>

using namespace bld;

TEST(ActiveSheetShots, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_SHEET_SHOTS");
    const QStringList libs = qEnvironmentVariable("BLD_PARTS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (out.isEmpty() || libs.isEmpty()) GTEST_SKIP() << "BLD_SHEET_SHOTS / BLD_PARTS not set";
    const QString tag = qEnvironmentVariable("BLD_SHEET_TAG", QStringLiteral("after"));
    QDir().mkpath(out);
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    for (const QString& p : libs) lib.addSearchPath(p);
    ASSERT_GT(lib.scan(), 1000);
    ui::theme::loadFonts();
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
    // The Parts panel shows the tables, with every picture loaded.
    for (auto* edit : window.findChildren<QLineEdit*>())
        if (edit->placeholderText().startsWith(QStringLiteral("Search parts"))) edit->setText(QStringLiteral("table"));
    const auto loadingParts = [&window] {
        for (auto* label : window.findChildren<QLabel*>())
            if (label->isVisible() && label->text().startsWith(QStringLiteral("Loading part pictures"))) return true;
        return false;
    };
    t.restart();
    QTest::qWait(500);
    while (loadingParts() && t.elapsed() < 180000) QTest::qWait(200);
    ASSERT_FALSE(loadingParts());
    window.statusBar()->clearMessage();
    auto* list = window.findChild<QListWidget*>(QStringLiteral("sheetList"));
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == QStringLiteral("Tables")) list->setCurrentRow(i);
    const ui::theme::Accent accent = ui::theme::accent(QString::fromLatin1(ui::theme::kDefaultAccent));
    const auto useTheme = [&](ui::theme::Mode mode) {
        QApplication::setPalette(ui::theme::buildPalette(mode, accent));
        qApp->setStyleSheet(ui::theme::buildStyleSheet(mode, accent));
        QTest::qWait(500);
        // A new theme draws the part pictures again.
        t.restart();
        while (loadingParts() && t.elapsed() < 180000) QTest::qWait(200);
        QTest::qWait(300);
        return mode == ui::theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
    };
    // The plain pictures first, before anything is dragged or placed.
    for (const auto mode : { ui::theme::Mode::Light, ui::theme::Mode::Dark }) {
        const QString name = useTheme(mode);
        ASSERT_FALSE(loadingParts());
        window.grab().save(out + QStringLiteral("/desk-%1-%2.png").arg(tag, name));
    }
#if __has_include("ui/ActiveSheetButton.h")
    // A part dragged over the map, and a part placed on a hidden sheet.
    for (const auto mode : { ui::theme::Mode::Light, ui::theme::Mode::Dark }) {
        const QString name = useTheme(mode);
        ASSERT_FALSE(loadingParts());
        QMimeData mime;
        mime.setData(QByteArray(ui::PartsBrowser::kPartMimeType), QByteArray("2865.8"));
        const QPoint at = view->viewport()->rect().center();
        // Newer Qt wants a QPointF here; older Qt only has the QPoint form.
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QT_WARNING_POP
        QApplication::sendEvent(view->viewport(), &enter);
        QTest::qWait(300);
        window.grab().save(out + QStringLiteral("/desk-%1-%2-dragging.png").arg(tag, name));
        QDragLeaveEvent leave;
        QApplication::sendEvent(view->viewport(), &leave);
        int tables = -1;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text() == QStringLiteral("Tables")) tables = i;
        list->item(tables)->setCheckState(Qt::Unchecked);
        view->addPartAtScenePos(QStringLiteral("2865.8"), view->mapToScene(at));
        QTest::qWait(300);
        window.grab().save(out + QStringLiteral("/desk-%1-%2-hidden.png").arg(tag, name));
        view->undoStack()->undo();
        list->item(tables)->setCheckState(Qt::Checked);
        window.notices()->hideNotice(QStringLiteral("sheet.hidden"));
        QTest::qWait(200);
    }
#endif
    qApp->setStyleSheet(QString());
    QStandardPaths::setTestModeEnabled(false);
}
