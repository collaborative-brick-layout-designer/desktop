// BLD_ABOUT_SHOTS=<dir> BLD_PARTS=<library dirs, ':'-separated>: pictures of
// Help > About (its version line and the third-party licenses link), in the
// light and dark themes, to look at (not compared). BLD_ABOUT_TAG names them
// (about-<tag>-light.png). BLD_ABOUT_VERSION and BLD_ABOUT_CHANNEL stand in
// for the build's version and channel.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Version.h"
#include "parts/PartsLibrary.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

using namespace bld;

TEST(AboutShots, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_ABOUT_SHOTS");
    const QStringList libs = qEnvironmentVariable("BLD_PARTS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (out.isEmpty() || libs.isEmpty()) GTEST_SKIP() << "BLD_ABOUT_SHOTS / BLD_PARTS not set";
    const QString tag = qEnvironmentVariable("BLD_ABOUT_TAG", QStringLiteral("after"));
    QDir().mkpath(out);
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    const QString channelBefore = core::buildChannel();
    const QString versionBefore = QCoreApplication::applicationVersion();
    core::setBuildChannel(qEnvironmentVariable("BLD_ABOUT_CHANNEL"));
    QCoreApplication::setApplicationVersion(qEnvironmentVariable("BLD_ABOUT_VERSION", QStringLiteral("2.0.12")));
    parts::PartsLibrary lib;
    for (const QString& p : libs) lib.addSearchPath(p);
    ASSERT_GT(lib.scan(), 1000);
    ui::theme::loadFonts();
    ui::MainWindow window(lib);
    window.resize(1400, 860);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.openFile(QStringLiteral(BLD_BBM_CORPUS_DIR "/fordyce-2026.bbm")));
    QTest::qWait(1500);
    auto* view = window.findChild<ui::MapView*>();
    int missing = 0;
    for (const auto& L : view->currentMap()->layers()) {
        if (L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (!lib.metadata(b.partNumber) || lib.pixmap(b.partNumber).isNull()) ++missing;
    }
    ASSERT_EQ(missing, 0);

    QAction* about = nullptr;
    for (QAction* a : window.findChildren<QAction*>())
        if (a->text().startsWith(QStringLiteral("&About Collaborative"))) about = a;
    ASSERT_TRUE(about);

    const ui::theme::Accent accent = ui::theme::accent(QString::fromLatin1(ui::theme::kDefaultAccent));
    for (const auto mode : { ui::theme::Mode::Light, ui::theme::Mode::Dark }) {
        QApplication::setPalette(ui::theme::buildPalette(mode, accent));
        qApp->setStyleSheet(ui::theme::buildStyleSheet(mode, accent));
        QTest::qWait(500);
        const QString name = mode == ui::theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        bool grabbed = false;
        QTimer::singleShot(600, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                box->grab().save(out + QStringLiteral("/about-%1-%2.png").arg(tag, name));
                grabbed = true;
                box->accept();
            }
        });
        about->trigger();
        EXPECT_TRUE(grabbed);
    }
    qApp->setStyleSheet(QString());
    core::setBuildChannel(channelBefore);
    QCoreApplication::setApplicationVersion(versionBefore);
    QStandardPaths::setTestModeEnabled(false);
}
