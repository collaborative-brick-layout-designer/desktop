// A damaged or cut-off file, for every reader: it is refused, or opened with
// a warning that says what is missing (and marked so saving over it keeps a
// copy first). Never opened silently as if it were whole: Save would then
// write the part that was read over the good copy. Studio and LDD files are
// in StudioReaderTest and LDDReaderTest (they need the zip writer).

#include "edit/Budget.h"
#include "import/LayoutFile.h"
#include "import/mapformats/FourDBrixMap.h"
#include "import/mapformats/LDrawMap.h"
#include "import/mapformats/TrackDesignerMap.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include "core/Map.h"
#include "core/Sidecar.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace bld;

namespace {

QByteArray bytesOf(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString writeTo(const QTemporaryDir& dir, const QString& name, const QByteArray& bytes) {
    const QString path = dir.filePath(name);
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    return path;
}

QString oracle(const char* name) {
    return QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + QLatin1String(name);
}
QString corpus(const char* name) {
    return QStringLiteral(BLD_BBM_CORPUS_DIR "/") + QLatin1String(name);
}
QString layouts(const char* name) {
    return QStringLiteral(BLD_SOURCE_DIR "/fixtures/layouts/") + QLatin1String(name);
}

class DamagedFiles : public ::testing::Test {
protected:
    void SetUp() override {
        const QString root = QStringLiteral(BLD_PARTS_LIBRARY_ROOT);
        if (QDir(root).exists()) {
            lib_.addSearchPath(root);
            lib_.scan();
        }
    }
    QTemporaryDir dir_;
    parts::PartsLibrary lib_;
};

} // namespace

// The review's case: Fordyce cut off part way opened with 503 of its 940
// parts, and Save wrote that over the whole file. Vanilla refuses it.
TEST_F(DamagedFiles, ACutOffBbmIsRefused) {
    const QByteArray whole = bytesOf(corpus("fordyce-2026.bbm"));
    ASSERT_FALSE(whole.isEmpty());
    ASSERT_TRUE(saveload::readBbm(corpus("fordyce-2026.bbm")).ok());
    for (const double at : { 0.1, 0.5, 0.9, 0.999 }) {
        const QString path =
            writeTo(dir_, QStringLiteral("cut.bbm"), whole.left(qsizetype(whole.size() * at)));
        const auto r = saveload::readBbm(path);
        EXPECT_FALSE(r.ok()) << "cut at " << at;
        EXPECT_TRUE(r.error.contains(QStringLiteral("cut off"))) << r.error.toStdString();
        EXPECT_TRUE(r.error.contains(QStringLiteral("line"))) << r.error.toStdString();
    }
}

TEST_F(DamagedFiles, ADamagedBbmIsRefused) {
    QByteArray bytes = bytesOf(corpus("tight-corner.bbm"));
    const qsizetype at = bytes.indexOf("<Brick ");
    ASSERT_GT(at, 0);
    bytes.replace(at, 7, "<Bri<k ");
    const auto r = saveload::readBbm(writeTo(dir_, QStringLiteral("damaged.bbm"), bytes));
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.error.contains(QStringLiteral("damaged"))) << r.error.toStdString();
}

TEST_F(DamagedFiles, ACutOffLayoutFileIsRefused) {
    const QByteArray whole = bytesOf(layouts("corner-lobby.bld-layout"));
    ASSERT_FALSE(whole.isEmpty());
    ASSERT_TRUE(import::readLayoutFileBytes(whole, dir_.path()).ok());
    for (const double at : { 0.3, 0.9 }) {
        const auto r = import::readLayoutFileBytes(whole.left(qsizetype(whole.size() * at)), dir_.path());
        EXPECT_FALSE(r.ok()) << "cut at " << at;
        EXPECT_FALSE(r.error.isEmpty());
    }
}

// A layout whose map inside is damaged: refused, not opened in part.
TEST_F(DamagedFiles, ALayoutFileWithADamagedMapIsRefused) {
    core::Map map;
    map.author = QStringLiteral("Someone");
    QString err;
    const QByteArray good = import::layoutFileBytes(map, &err, nullptr, {}, {});
    ASSERT_FALSE(good.isEmpty()) << err.toStdString();
    // The map entry is compressed; flip a byte in its data so its checksum fails.
    QByteArray bad = good;
    const qsizetype name = bad.indexOf("layout.bbm");
    ASSERT_GT(name, 0);
    bad[name + 20] = static_cast<char>(bad[name + 20] ^ 0x5a);
    const auto r = import::readLayoutFileBytes(bad, dir_.path());
    EXPECT_FALSE(r.ok());
}

// Its labels, modules and venue damaged: the layout opens, says so, and is
// marked so saving over it keeps a copy first.
TEST_F(DamagedFiles, ALayoutFileWithDamagedLabelsOpensAndSaysSo) {
    core::Map map;
    core::AnchoredLabel label;
    label.id = QStringLiteral("l1");
    label.text = QStringLiteral("Hello");
    map.sidecar.anchoredLabels.push_back(label);
    QString err;
    QByteArray bytes = import::layoutFileBytes(map, &err, nullptr, {}, {});
    ASSERT_FALSE(bytes.isEmpty()) << err.toStdString();
    // The sidecar's local entry: damage its compressed data.
    const qsizetype local = bytes.indexOf("sidecar.json");
    bytes[local + 16] = static_cast<char>(bytes[local + 16] ^ 0x5a);
    const auto r = import::readLayoutFileBytes(bytes, dir_.path());
    ASSERT_TRUE(r.ok()) << r.error.toStdString();
    EXPECT_TRUE(r.partlyRead);
    EXPECT_FALSE(r.warnings.isEmpty());
}

TEST_F(DamagedFiles, ACutOffSidecarIsNotRead) {
    core::Sidecar sidecar;
    core::AnchoredLabel label;
    label.id = QStringLiteral("l1");
    sidecar.anchoredLabels.push_back(label);
    const QString path = dir_.filePath(QStringLiteral("a.bbm.bld"));
    ASSERT_TRUE(saveload::writeSidecar(path, "bbm", sidecar));
    const QByteArray whole = bytesOf(path);
    writeTo(dir_, QStringLiteral("a.bbm.bld"), whole.left(whole.size() / 2));
    core::Sidecar back;
    const auto r = saveload::readSidecar(path, "bbm", back);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
    EXPECT_TRUE(back.anchoredLabels.empty());
}

TEST_F(DamagedFiles, ACutOffVenueIsRefused) {
    const QByteArray whole = bytesOf(QStringLiteral(BLD_SOURCE_DIR "/fixtures/venues/grand-lobby.bld-venue"));
    ASSERT_FALSE(whole.isEmpty());
    QString err;
    EXPECT_FALSE(saveload::readVenueFile(
        writeTo(dir_, QStringLiteral("v.bld-venue"), whole.left(whole.size() / 2)), &err));
    EXPECT_FALSE(err.isEmpty());
}

TEST_F(DamagedFiles, ACutOffBudgetIsRefused) {
    const QByteArray whole = bytesOf(oracle("budget.bbb"));
    ASSERT_FALSE(whole.isEmpty());
    ASSERT_TRUE(edit::Budget::read(oracle("budget.bbb"), nullptr));
    EXPECT_FALSE(
        edit::Budget::read(writeTo(dir_, QStringLiteral("b.bbb"), whole.left(whole.size() / 2)), nullptr));
}

TEST_F(DamagedFiles, ACutOffTrackDesignerMapSaysWhatIsMissing) {
    if (lib_.partCount() == 0) GTEST_SKIP() << "BlueBrickParts submodule missing";
    const QByteArray whole = bytesOf(oracle("tight-corner.tdl"));
    ASSERT_FALSE(whole.isEmpty());
    const auto full = import::readTrackDesignerMap(oracle("tight-corner.tdl"), lib_);
    ASSERT_TRUE(full.ok());
    EXPECT_FALSE(full.partlyRead);
    // Part way through the pieces: what was read opens, and says the rest is missing.
    const auto cut = import::readTrackDesignerMap(
        writeTo(dir_, QStringLiteral("cut.tdl"), whole.left(whole.size() / 2 + 3)), lib_);
    ASSERT_TRUE(cut.ok()) << cut.error.toStdString();
    EXPECT_TRUE(cut.partlyRead);
    EXPECT_TRUE(cut.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("cut off")));
    // Before the first piece: refused.
    const auto header =
        import::readTrackDesignerMap(writeTo(dir_, QStringLiteral("head.tdl"), whole.left(40)), lib_);
    EXPECT_FALSE(header.ok());
    EXPECT_TRUE(header.error.contains(QStringLiteral("cut off"))) << header.error.toStdString();
}

TEST_F(DamagedFiles, ACutOffFourDBrixMapSaysWhatIsMissing) {
    if (lib_.partCount() == 0) GTEST_SKIP() << "BlueBrickParts submodule missing";
    const QByteArray whole = bytesOf(oracle("fourdbrix.ncp"));
    ASSERT_FALSE(whole.isEmpty());
    const auto full = import::readFourDBrixMap(oracle("fourdbrix.ncp"), lib_);
    ASSERT_TRUE(full.ok());
    EXPECT_FALSE(full.partlyRead);
    const auto cut = import::readFourDBrixMap(
        writeTo(dir_, QStringLiteral("cut.ncp"), whole.left(whole.size() / 2)), lib_);
    ASSERT_TRUE(cut.ok()) << cut.error.toStdString();
    EXPECT_TRUE(cut.partlyRead);
    EXPECT_TRUE(cut.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("cut off")));
    // Broken XML part way is refused.
    QByteArray bad = whole;
    bad.insert(whole.size() / 3, "<<");
    EXPECT_FALSE(import::readFourDBrixMap(writeTo(dir_, QStringLiteral("bad.ncp"), bad), lib_).ok());
}

// LDraw is plain lines, so a cut-off file can't be told from a short one:
// it opens what it has, without crashing on the broken last line.
TEST_F(DamagedFiles, ACutOffLDrawMapOpensWhatItHas) {
    if (lib_.partCount() == 0) GTEST_SKIP() << "BlueBrickParts submodule missing";
    for (const char* name : { "tight-corner.ldr", "tight-corner.mpd" }) {
        const QByteArray whole = bytesOf(oracle(name));
        ASSERT_FALSE(whole.isEmpty());
        const auto r = import::readLDrawMap(
            writeTo(dir_, QString::fromLatin1(name), whole.left(whole.size() / 2 + 7)), lib_);
        EXPECT_TRUE(r.ok()) << name << ": " << r.error.toStdString();
    }
}

// A part's own file damaged: the part isn't loaded half read.
TEST_F(DamagedFiles, ADamagedPartFileIsNotLoaded) {
    QDir(dir_.path()).mkpath(QStringLiteral("lib/Test"));
    const QString xml = dir_.filePath(QStringLiteral("lib/Test/broken.1.xml"));
    QFile f(xml);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("<part><Author>me</Author><Description><en>Broken");
    f.close();
    QFile good(dir_.filePath(QStringLiteral("lib/Test/whole.1.xml")));
    ASSERT_TRUE(good.open(QIODevice::WriteOnly));
    good.write("<part><Author>me</Author><Description><en>Whole</en></Description></part>");
    good.close();
    parts::PartsLibrary lib;
    lib.addSearchPath(dir_.filePath(QStringLiteral("lib")));
    lib.scan();
    EXPECT_TRUE(lib.metadata(QStringLiteral("whole.1")).has_value());
    EXPECT_FALSE(lib.metadata(QStringLiteral("broken.1")).has_value());
}
