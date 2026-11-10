// BLD_QT_CATALOG_SHOTS=<dir>: pictures of a message box's standard buttons
// (Qt's own words) in Portuguese, Chinese and Norwegian, with the Qt catalog
// the app used to look for (qtbase_<code>, none there) and the one it finds
// now, in the light and dark themes, to look at (not compared).

#include "core/QtCatalogs.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QTest>
#include <QTranslator>

using namespace bld;

TEST(QtCatalogsShots, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_QT_CATALOG_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_QT_CATALOG_SHOTS not set";
    QDir().mkpath(out);
    ui::theme::loadFonts();
    const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    const ui::theme::Accent accent = ui::theme::accent(QString::fromLatin1(ui::theme::kDefaultAccent));
    for (const QString& code : { QStringLiteral("pt"), QStringLiteral("zh"), QStringLiteral("no") }) {
        for (const bool after : { false, true }) {
            QTranslator qt;
            bool loaded = false;
            if (after) {
                const QString file = core::findQtCatalog(code, QStringLiteral("en_US"), { qtDir });
                loaded = !file.isEmpty() && qt.load(file);
                EXPECT_TRUE(loaded) << code.toStdString();
            } else {
                // What main.cpp did before: qtbase_<code>, with "no" as "nb".
                loaded = qt.load(QStringLiteral("qtbase_") + (code == QLatin1String("no") ? QStringLiteral("nb") : code), qtDir);
            }
            if (loaded) QCoreApplication::installTranslator(&qt);
            for (const auto mode : { ui::theme::Mode::Light, ui::theme::Mode::Dark }) {
                QApplication::setPalette(ui::theme::buildPalette(mode, accent));
                qApp->setStyleSheet(ui::theme::buildStyleSheet(mode, accent));
                QMessageBox box(QMessageBox::Question, QStringLiteral("Collaborative Brick Layout Designer"),
                                QStringLiteral("Qt %1 · %2").arg(code, loaded ? QStringLiteral("Qt catalog loaded") : QStringLiteral("no Qt catalog")),
                                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
                box.show();
                QTest::qWait(200);
                box.grab().save(out + QStringLiteral("/qt-catalog-%1-%2-%3.png")
                                          .arg(after ? QStringLiteral("after") : QStringLiteral("before"), code,
                                               mode == ui::theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light")));
            }
            if (loaded) QCoreApplication::removeTranslator(&qt);
        }
    }
    qApp->setStyleSheet(QString());
}
