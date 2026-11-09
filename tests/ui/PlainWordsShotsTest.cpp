// BLD_WORDS_SHOTS=<dir> BLD_PARTS=<library dirs, ':'-separated>: pictures of
// the words the desktop shares with the website (the sheets' Opacity, Find
// & Replace, Venue by dimensions, the Venue library), in the light and dark
// themes, to look at (not compared). BLD_WORDS_TAG names them
// (words-<tag>-<scene>-light.png).

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "ui/FindDialog.h"
#include "ui/LoadingCard.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"
#include "ui/VenueDimensionsDialog.h"
#include "ui/VenueLibraryPanel.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTest>

using namespace bld;

TEST(PlainWordsShots, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_WORDS_SHOTS");
    const QStringList libs = qEnvironmentVariable("BLD_PARTS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (out.isEmpty() || libs.isEmpty()) GTEST_SKIP() << "BLD_WORDS_SHOTS / BLD_PARTS not set";
    const QString tag = qEnvironmentVariable("BLD_WORDS_TAG", QStringLiteral("after"));
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
    int missing = 0;
    for (const auto& L : view->currentMap()->layers()) {
        if (L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (!lib.metadata(b.partNumber) || lib.pixmap(b.partNumber).isNull()) ++missing;
    }
    ASSERT_EQ(missing, 0);
    window.statusBar()->clearMessage();

    const ui::theme::Accent accent = ui::theme::accent(QString::fromLatin1(ui::theme::kDefaultAccent));
    for (const auto mode : { ui::theme::Mode::Light, ui::theme::Mode::Dark }) {
        QApplication::setPalette(ui::theme::buildPalette(mode, accent));
        qApp->setStyleSheet(ui::theme::buildStyleSheet(mode, accent));
        QTest::qWait(800);
        const QString name = mode == ui::theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        const auto file = [&](const QString& scene) { return out + QStringLiteral("/words-%1-%2-%3.png").arg(tag, scene, name); };
        window.grab().save(file(QStringLiteral("window")));
        {
            ui::FindDialog d(*view);
            d.show();
            QTest::qWait(300);
            d.grab().save(file(QStringLiteral("find")));
        }
        {
            ui::VenueDimensionsDialog d;
            d.show();
            QTest::qWait(300);
            d.grab().save(file(QStringLiteral("venue-dimensions")));
        }
        {
            ui::VenueLibraryPanel panel;
            panel.resize(360, 420);
            panel.show();
            QTest::qWait(300);
            panel.grab().save(file(QStringLiteral("venue-library")));
        }
    }
    qApp->setStyleSheet(QString());
    QStandardPaths::setTestModeEnabled(false);
}
