#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPolygonF>
#include <QString>
#include <QTemporaryDir>

using namespace bld;

namespace {

QString libraryRoot() {
    // BLD_PARTS_LIBRARY_ROOT is set by CMakeLists to the vendored submodule path.
    return QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
}

bool librarySubmoduleAvailable() {
    return QDir(libraryRoot()).exists();
}

}

TEST(PartsLibrary, ScanVendoredSubmodule) {
    if (!librarySubmoduleAvailable()) {
        GTEST_SKIP() << "BlueBrickParts submodule not present; run git submodule update --init";
    }

    parts::PartsLibrary lib;
    lib.addSearchPath(libraryRoot());
    const int added = lib.scan();

    // BlueBrickParts at the pinned submodule commit has ~700 XML entries; any healthy
    // scan should land comfortably above 100. Specific number isn't pinned so library
    // updates don't force test churn.
    EXPECT_GT(added, 100) << "Scan found suspiciously few parts";
    EXPECT_EQ(lib.partCount(), added);
}

TEST(PartsLibrary, LookupKnownPartMetadata) {
    if (!librarySubmoduleAvailable()) GTEST_SKIP();

    parts::PartsLibrary lib;
    lib.addSearchPath(libraryRoot());
    lib.scan();

    // TS_ADAPTERCONTINUOUSCURVE.8 is a leaf part in 4DBrix; verified present at the
    // pinned submodule revision.
    const auto meta = lib.metadata(QStringLiteral("TS_ADAPTERCONTINUOUSCURVE.8"));
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->partNumber, QStringLiteral("TS_ADAPTERCONTINUOUSCURVE"));
    EXPECT_EQ(meta->colorCode,  QStringLiteral("8"));
    EXPECT_EQ(meta->kind, parts::PartKind::Leaf);
    EXPECT_EQ(meta->author, QStringLiteral("Alban Nanty"));
    EXPECT_FALSE(meta->descriptions.isEmpty());
    bool foundEn = false;
    for (const auto& d : meta->descriptions) {
        if (d.language == QStringLiteral("en") && !d.text.isEmpty()) foundEn = true;
    }
    EXPECT_TRUE(foundEn);
}

TEST(PartsLibrary, ConnectionsParsedForTrackParts) {
    if (!librarySubmoduleAvailable()) GTEST_SKIP();
    parts::PartsLibrary lib;
    lib.addSearchPath(libraryRoot());
    lib.scan();

    // Any rail track should have at least two <connexion> entries. Pick the
    // 4DBrix adapter we already use elsewhere — its XML was verified by hand.
    const auto meta = lib.metadata(QStringLiteral("TS_ADAPTERCONTINUOUSCURVE.8"));
    ASSERT_TRUE(meta.has_value());
    ASSERT_GE(meta->connections.size(), 2);
    const auto& c0 = meta->connections[0];
    // Type strings can be numeric legacy ids ("1"=rail) or new-style names
    // ("rail","road","coaster",...). Just verify it parsed as non-empty.
    EXPECT_FALSE(c0.type.isEmpty());
    EXPECT_NE(c0.position, QPointF());   // non-default position
    // Position and angle values come straight from the XML; we just verify
    // they're plausible (within a reasonable stud range for this 4x4 part).
    EXPECT_LT(std::abs(c0.position.x()), 10.0);
    EXPECT_LT(std::abs(c0.position.y()), 10.0);
}

TEST(PartsLibrary, GroupPartsDetected) {
    if (!librarySubmoduleAvailable()) GTEST_SKIP();

    parts::PartsLibrary lib;
    lib.addSearchPath(libraryRoot());
    lib.scan();

    int groupCount = 0;
    for (const QString& key : lib.keys()) {
        if (auto m = lib.metadata(key); m && m->kind == parts::PartKind::Group) ++groupCount;
    }
    EXPECT_GT(groupCount, 0) << "No .set.xml group parts detected in the library";
}

TEST(PartsLibrary, GifFilePathsResolveAndDecode) {
    // Headless test: we can't exercise the QPixmap cache without a QGuiApplication,
    // but we can verify the library correctly pairs XML files with their GIFs and
    // that Qt decodes them. QImage does not require a GUI app.
    if (!librarySubmoduleAvailable()) GTEST_SKIP();

    parts::PartsLibrary lib;
    lib.addSearchPath(libraryRoot());
    lib.scan();

    int checked = 0;
    for (const QString& key : lib.keys()) {
        auto m = lib.metadata(key);
        if (!m || m->gifFilePath.isEmpty()) continue;
        ASSERT_TRUE(QFile::exists(m->gifFilePath)) << "GIF missing: " << m->gifFilePath.toStdString();
        QImage img(m->gifFilePath);
        EXPECT_FALSE(img.isNull()) << "Failed to decode GIF: " << m->gifFilePath.toStdString();
        EXPECT_GT(img.width(), 0);
        EXPECT_GT(img.height(), 0);
        if (++checked >= 5) break;  // sample — the full library is too big for per-test iteration
    }
    EXPECT_GE(checked, 1) << "No parts with GIFs found in the library";
}

// BlueBrick's <NotListedInLibrary>: a castle set's buildings and the flex
// track halves are only used inside their sets (hidden from the parts list,
// never placed alone); their sets aren't.
TEST(PartsLibrary, PartsOnlyUsedInsideSetsAreMarked) {
    if (!librarySubmoduleAvailable()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    parts::PartsLibrary lib;
    lib.addSearchPath(libraryRoot());
    lib.scan();
    for (const char* key : { "10193-1.tree", "10193-1.house1", "88492.8", "88493.8" }) {
        const auto meta = lib.metadata(QString::fromLatin1(key));
        ASSERT_TRUE(meta) << key;
        EXPECT_TRUE(meta->notListed) << key;
    }
    for (const char* key : { "10193-1", "flex.group" }) {
        const auto meta = lib.metadata(QString::fromLatin1(key));
        ASSERT_TRUE(meta) << key;
        EXPECT_FALSE(meta->notListed) << key;
    }
}

// ---- Parts folders and packages (2026-11-09 review, area 5) -----------------

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

// A part <dir>/<stem>.xml + .png, its author telling the copies apart.
void writePart(const QString& dir, const QString& stem, const QString& author, int pxPerStud = 0,
               QSize size = QSize(16, 8)) {
    QByteArray xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n\t<Author>" + author.toUtf8() + "</Author>\n";
    if (pxPerStud > 0) xml += "\t<PixelsPerStud>" + QByteArray::number(pxPerStud) + "</PixelsPerStud>\n";
    xml += "</part>\n";
    writeFile(dir + QLatin1Char('/') + stem + QStringLiteral(".xml"), xml);
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(dir + QLatin1Char('/') + stem + QStringLiteral(".png")));
}

void writePackage(const QString& dir, const QString& version) {
    writeFile(dir + QStringLiteral("/config/About.txt"), "name=Pack\nversion=" + version.toUtf8() + "\n");
}

} // namespace

TEST(PartsLibrary, PackageVersionsCompareNumberByNumber) {
    using L = parts::PartsLibrary;
    EXPECT_GT(L::comparePackageVersions(QStringLiteral("1.3"), QStringLiteral("1.2")), 0);
    EXPECT_GT(L::comparePackageVersions(QStringLiteral("1.10"), QStringLiteral("1.9")), 0);
    EXPECT_GT(L::comparePackageVersions(QStringLiteral("1.8.5"), QStringLiteral("1.8")), 0);
    EXPECT_EQ(L::comparePackageVersions(QStringLiteral("1.0"), QStringLiteral("1")), 0);
    EXPECT_LT(L::comparePackageVersions(QString(), QStringLiteral("1.0")), 0);
}

// R5.RemovedFolderStillScanned: clear() kept every search path, so a folder
// taken out of Parts Folders… stayed until a restart.
TEST(PartsLibrary, SetSearchPathsReplacesTheFolders) {
    QTemporaryDir a, b;
    writePart(a.path(), QStringLiteral("APART.1"), QStringLiteral("A"));
    writePart(b.path(), QStringLiteral("BPART.1"), QStringLiteral("B"));
    parts::PartsLibrary lib;
    lib.setSearchPaths({ a.path(), b.path() });
    lib.scan();
    ASSERT_TRUE(lib.metadata(QStringLiteral("APART.1")));
    lib.clear();
    lib.setSearchPaths({ b.path() });
    lib.scan();
    EXPECT_FALSE(lib.metadata(QStringLiteral("APART.1")));
    EXPECT_TRUE(lib.metadata(QStringLiteral("BPART.1")));
    EXPECT_EQ(lib.searchPaths(), QStringList{ b.path() });
}

// R5.NewerPackNeverWins: the bundled folder is read first and the first part
// read wins, so a newer Download Center pack never took effect.
TEST(PartsLibrary, ANewerCopyOfAPackageWins) {
    QTemporaryDir bundled, downloads;
    writePackage(bundled.filePath(QStringLiteral("Town")), QStringLiteral("1.2"));
    writePart(bundled.filePath(QStringLiteral("Town")), QStringLiteral("HOUSE.1"), QStringLiteral("old"));
    writePart(bundled.filePath(QStringLiteral("Town")), QStringLiteral("SHED.1"), QStringLiteral("old"));
    writePackage(bundled.filePath(QStringLiteral("Castle")), QStringLiteral("1.1"));
    writePart(bundled.filePath(QStringLiteral("Castle")), QStringLiteral("TOWER.1"),
              QStringLiteral("bundled"));
    writePackage(downloads.filePath(QStringLiteral("Town")), QStringLiteral("1.3"));
    writePart(downloads.filePath(QStringLiteral("Town")), QStringLiteral("HOUSE.1"), QStringLiteral("new"));
    // An older download than the bundled copy loses to it.
    writePackage(downloads.filePath(QStringLiteral("Castle")), QStringLiteral("1.0"));
    writePart(downloads.filePath(QStringLiteral("Castle")), QStringLiteral("TOWER.1"),
              QStringLiteral("older"));
    parts::PartsLibrary lib;
    lib.setSearchPaths({ bundled.path(), downloads.path() });
    lib.scan();
    EXPECT_EQ(lib.metadata(QStringLiteral("HOUSE.1"))->author, QStringLiteral("new"));
    // A part only the older copy has is still there.
    EXPECT_EQ(lib.metadata(QStringLiteral("SHED.1"))->author, QStringLiteral("old"));
    EXPECT_EQ(lib.metadata(QStringLiteral("TOWER.1"))->author, QStringLiteral("bundled"));
}

TEST(PartsLibrary, FoldersWithoutAPackageKeepTheirOrder) {
    QTemporaryDir first, second;
    writePart(first.filePath(QStringLiteral("Mine")), QStringLiteral("SAME.1"), QStringLiteral("first"));
    writePart(second.filePath(QStringLiteral("Mine")), QStringLiteral("SAME.1"), QStringLiteral("second"));
    parts::PartsLibrary lib;
    lib.setSearchPaths({ first.path(), second.path() });
    lib.scan();
    EXPECT_EQ(lib.metadata(QStringLiteral("SAME.1"))->author, QStringLiteral("first"));
}
