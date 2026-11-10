// Qt's own catalogs are found for every UI language, under whichever name a
// package has them (bug: none loaded on Windows or macOS, nor pt/zh/no on Linux).

#include "core/QtCatalogs.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace bld::core;

namespace {
void touch(const QString& dir, const QString& name) {
    QFile f(QDir(dir).filePath(name));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
}
}  // namespace

TEST(QtCatalogs, NamesForEachLanguage) {
    EXPECT_EQ(qtCatalogNames(QStringLiteral("de"), QStringLiteral("de_DE")),
              (QStringList{ QStringLiteral("qt_de"), QStringLiteral("qtbase_de") }));
    EXPECT_EQ(qtCatalogNames(QStringLiteral("pt"), QStringLiteral("pt_BR")).first(), QStringLiteral("qt_pt_BR"));
    EXPECT_EQ(qtCatalogNames(QStringLiteral("pt"), QStringLiteral("pt_PT")).first(), QStringLiteral("qt_pt_PT"));
    EXPECT_EQ(qtCatalogNames(QStringLiteral("zh"), QStringLiteral("zh_CN")).first(), QStringLiteral("qt_zh_CN"));
    EXPECT_EQ(qtCatalogNames(QStringLiteral("zh"), QStringLiteral("zh_TW")).first(), QStringLiteral("qt_zh_TW"));
    EXPECT_EQ(qtCatalogNames(QStringLiteral("zh"), QStringLiteral("en_US")).first(), QStringLiteral("qt_zh_CN"));
    const QStringList no = qtCatalogNames(QStringLiteral("no"), QStringLiteral("nb_NO"));
    EXPECT_EQ(no.first(), QStringLiteral("qt_nb"));
    EXPECT_TRUE(no.contains(QStringLiteral("qtbase_nn")));
    EXPECT_EQ(uiLanguages().size(), 10);
}

// The Windows package: windeployqt's merged qt_<code>.qm files.
TEST(QtCatalogs, FindsWindeployqtsMergedCatalogs) {
    QTemporaryDir dir;
    for (const char* n : { "qt_de.qm", "qt_es.qm", "qt_fr.qm", "qt_it.qm", "qt_ja.qm", "qt_nl.qm", "qt_nn.qm",
                           "qt_pt_BR.qm", "qt_sv.qm", "qt_zh_CN.qm", "qt_zh_TW.qm" })
        touch(dir.path(), QString::fromLatin1(n));
    for (const QString& code : uiLanguages())
        EXPECT_FALSE(findQtCatalog(code, QStringLiteral("en_US"), { dir.path() }).isEmpty()) << code.toStdString();
    EXPECT_TRUE(findQtCatalog(QStringLiteral("no"), QStringLiteral("nb_NO"), { dir.path() }).endsWith(QStringLiteral("/qt_nn.qm")));
}

// Linux and the macOS bundle: qtbase_<code>.qm, in the second folder.
TEST(QtCatalogs, FindsQtbaseCatalogsInALaterFolder) {
    QTemporaryDir ours, qt;
    for (const char* n : { "qtbase_de.qm", "qtbase_es.qm", "qtbase_fr.qm", "qtbase_it.qm", "qtbase_ja.qm",
                           "qtbase_nl.qm", "qtbase_nn.qm", "qtbase_pt_BR.qm", "qtbase_sv.qm", "qtbase_zh_CN.qm",
                           "qtbase_zh_TW.qm" })
        touch(qt.path(), QString::fromLatin1(n));
    for (const QString& code : uiLanguages())
        EXPECT_FALSE(findQtCatalog(code, QStringLiteral("en_US"), { ours.path(), qt.path() }).isEmpty()) << code.toStdString();
    EXPECT_TRUE(findQtCatalog(QStringLiteral("zh"), QStringLiteral("zh_TW"), { ours.path(), qt.path() })
                    .endsWith(QStringLiteral("/qtbase_zh_TW.qm")));
    EXPECT_TRUE(findQtCatalog(QStringLiteral("ko"), QStringLiteral("ko_KR"), { ours.path(), qt.path() }).isEmpty());
}
