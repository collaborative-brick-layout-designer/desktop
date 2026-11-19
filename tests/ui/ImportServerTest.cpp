// The website's import (bld-import-server, docs/import-server.md) against
// the app's: the same file and choices give the same part, byte for byte,
// whether the preview dialog made it or the converter did. Plus the
// converter's own checks: archive limits, the sandbox, bad requests.
//
// Real Studio and LDD files stay opt-in, with the same variables as
// ImportPipelineTest: BLD_STUDIO_IO (+ BLD_STUDIO_LDRAW, BLD_LDRAW) and
// BLD_LDD_IMPORT (+ BLD_LDD_ROOT, BLD_LDD_LDRAW_XML, BLD_LDRAW, BLD_PARTS).

#include "app/importserver/ImportServer.h"
#include "app/importserver/Sandbox.h"
#include "core/Map.h"
#include "import/mapformats/LDrawMap.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmWriter.h"
#include "ui/BackgroundTask.h"
#include "ui/ImportPipeline.h"
#include "ui/ImportPreviewDialog.h"
#include "../import/ZipCryptoTestZip.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>

using namespace bld;

namespace {

void writeFile(const QString& path, const QByteArray& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(body);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const ui::HeavyRunner kInline = [](const QString&, const std::function<void(ui::CancelToken&)>& work) {
    ui::CancelToken token;
    work(token);
    return true;
};

struct Choices {
    int turns = 0;  // clockwise
    ui::ImportAlign align = ui::ImportAlign::Automatic;
    QPointF nudge;
    QList<int> drop;
    QString name;  // empty: as suggested
    QString designer;
    QString designerUrl;
};

Choices choose(int turns, ui::ImportAlign align = ui::ImportAlign::Automatic, QPointF nudge = {}, QList<int> drop = {},
               const QString& name = {}, const QString& designer = {}, const QString& designerUrl = {}) {
    Choices c;
    c.turns = turns;
    c.align = align;
    c.nudge = nudge;
    c.drop = std::move(drop);
    c.name = name;
    c.designer = designer;
    c.designerUrl = designerUrl;
    return c;
}

QString alignWord(ui::ImportAlign a) {
    return a == ui::ImportAlign::BottomLayer ? QStringLiteral("bottom")
         : a == ui::ImportAlign::BoundingBox ? QStringLiteral("box")
                                             : QString();
}

// The app: Tools › Import's preview dialog with these choices, then the
// part written as Import writes it. Returns the part key.
QString viaTheApp(const QString& model, const ui::ImportSettings& settings, parts::PartsLibrary& lib,
                  const Choices& c, const QString& outDir) {
    ui::PreparedPart part = ui::prepareImport(model, settings, lib, kInline);
    EXPECT_TRUE(part.ok()) << part.error.toStdString();
    ui::ImportPreviewDialog dlg(part, { QStringLiteral("imports") }, QStringLiteral("imports"));
    for (int i = 0; i < c.turns; ++i)
        for (auto* b : dlg.findChildren<QPushButton*>())
            if (b->text() == ui::ImportPreviewDialog::tr("Turn ⟳")) b->click();
    dlg.setAlign(c.align);
    if (!c.nudge.isNull()) dlg.nudge(c.nudge);
    auto* list = dlg.findChild<QListWidget*>();
    for (int i : c.drop) list->item(i)->setCheckState(Qt::Unchecked);
    if (!c.name.isEmpty()) dlg.findChild<QLineEdit*>(QStringLiteral("partName"))->setText(c.name);
    dlg.findChild<QLineEdit*>(QStringLiteral("designer"))->setText(c.designer);
    dlg.findChild<QLineEdit*>(QStringLiteral("designerUrl"))->setText(c.designerUrl);
    QString err;
    const QString key = ui::writeImportedPart(dlg.result(), dlg.partName(), outDir, QStringLiteral("Sam"), false, &err);
    EXPECT_FALSE(key.isEmpty()) << err.toStdString();
    return key;
}

importserver::Request requestFor(const QString& model, const ui::ImportSettings& settings, const QStringList& partsDirs,
                                 const Choices& c, const QString& outDir) {
    importserver::Request r;
    r.command = QStringLiteral("convert");
    r.input = model;
    r.outDir = outDir;
    r.libraries.parts = partsDirs;
    r.libraries.ldraw = settings.ldrawLibrary;
    r.libraries.studioLdraw = settings.studioLibrary;
    r.libraries.lddAssets = settings.lddPath;
    r.libraries.lddLdrawXml = settings.lddLdrawXml;
    r.quarterTurns = c.turns;
    r.align = alignWord(c.align);
    r.nudgeX = c.nudge.x();
    r.nudgeY = c.nudge.y();
    r.drop = c.drop;
    r.name = c.name;
    r.designer = c.designer;
    r.designerUrl = c.designerUrl;
    r.author = QStringLiteral("Sam");
    return r;
}

// The same file and choices through the app and through the converter: the
// same part files, byte for byte.
void expectSameAsTheApp(const QString& model, const ui::ImportSettings& settings, const QStringList& partsDirs,
                        const Choices& c) {
    SCOPED_TRACE(model.toStdString());
    parts::PartsLibrary lib;
    for (const QString& d : partsDirs) lib.addSearchPath(d);
    lib.scan();
    QTemporaryDir app, server;
    const QString key = viaTheApp(model, settings, lib, c, app.path());
    ASSERT_FALSE(key.isEmpty());

    const QJsonObject out = importserver::run(requestFor(model, settings, partsDirs, c, server.path()));
    ASSERT_TRUE(out.value(QStringLiteral("ok")).toBool()) << QJsonDocument(out).toJson().toStdString();
    EXPECT_EQ(out.value(QStringLiteral("partKey")).toString(), key);
    for (const char* ext : { ".xml", ".png", ".gif" }) {
        const QString a = QDir(app.path()).filePath(key + QLatin1String(ext));
        const QString b = QDir(server.path()).filePath(QStringLiteral("part/") + key + QLatin1String(ext));
        ASSERT_EQ(QFileInfo::exists(a), QFileInfo::exists(b)) << ext;
        if (QFileInfo::exists(a)) {
            EXPECT_TRUE(readFile(a) == readFile(b)) << "differs: " << ext;
        }
    }
    // The picture the server stores is the one Send to Server would send.
    const QString sprite = out.value(QStringLiteral("sprite")).toString();
    EXPECT_TRUE(QFileInfo::exists(QDir(server.path()).filePath(sprite))) << sprite.toStdString();
}

// Parts library with "TT.7": a 4 x 2 stud straight, connections at the ends.
struct TrackLibrary {
    QTemporaryDir dir;
    TrackLibrary() {
        writeFile(dir.filePath(QStringLiteral("TT.7.xml")),
            "<part><ConnexionList>"
            "<connexion><type>1</type><position><x>-2</x><y>0</y></position><angle>180</angle></connexion>"
            "<connexion><type>1</type><position><x>2</x><y>0</y></position><angle>0</angle></connexion>"
            "</ConnexionList></part>");
        QImage sprite(32, 16, QImage::Format_ARGB32);
        sprite.fill(Qt::darkGray);
        sprite.save(dir.filePath(QStringLiteral("TT.7.png")));
    }
};

QString twoStraights(QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("loop.ldr"));
    writeFile(path, "0 Two straights\n0 Author: Robin\n"
                    "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n"
                    "1 7 80 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    return path;
}

struct StubLDraw {
    QTemporaryDir dir;
    StubLDraw() {
        writeFile(dir.filePath(QStringLiteral("LDConfig.ldr")), "0 stub\n");
        writeFile(dir.filePath(QStringLiteral("parts/tt.dat")), "4 16 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
        writeFile(dir.filePath(QStringLiteral("parts/3001.dat")), "4 16 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
        writeFile(dir.filePath(QStringLiteral("parts/3023.dat")), "4 16 -20 0 -10 20 0 -10 20 0 10 -20 0 10\n");
    }
};

importserver::Request analyseRequest(const QString& model, const QString& outDir) {
    importserver::Request r;
    r.command = QStringLiteral("analyse");
    r.input = model;
    r.outDir = outDir;
    return r;
}

}  // namespace

TEST(ImportServer, LDrawGeometryMatchesTheApp) {
    TrackLibrary track;
    StubLDraw ldraw;
    QTemporaryDir models;
    ui::ImportSettings s;
    s.ldrawLibrary = ldraw.dir.path();
    const QString model = twoStraights(models);
    expectSameAsTheApp(model, s, { track.dir.path() }, {});
    expectSameAsTheApp(model, s, { track.dir.path() },
                       choose(1, ui::ImportAlign::BoundingBox, QPointF(0.25, -0.0625), { 0 }, QStringLiteral("Loop"),
                         QStringLiteral("Robin"), QStringLiteral("example.com/robin")));
    expectSameAsTheApp(model, s, { track.dir.path() }, choose(3, ui::ImportAlign::BottomLayer, QPointF(0.5, 0.5), { 1 }));
}

TEST(ImportServer, PartsLibraryPictureMatchesTheApp) {
    // No LDraw library: drawn from BlueBrickParts' own pictures.
    TrackLibrary track;
    QTemporaryDir models;
    expectSameAsTheApp(twoStraights(models), {}, { track.dir.path() }, choose(2, ui::ImportAlign::Automatic, {}, { 0, 1 }));
}

TEST(ImportServer, ARealLayoutMatchesTheApp) {
    const QStringList parts{ QStringLiteral(BLD_PARTS_LIBRARY_ROOT) };
    for (const char* f : { "tight-corner.ldr", "tight-corner.mpd" })
        expectSameAsTheApp(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + QLatin1String(f), {}, parts,
                           choose(1, ui::ImportAlign::BoundingBox, QPointF(0.125, 0), { 0, 3 }, QStringLiteral("Tight corner")));
}

TEST(ImportServer, StudioMatchesTheApp) {
    TrackLibrary track;
    StubLDraw ldraw;
    ui::ImportSettings s;
    s.studioLibrary = ldraw.dir.path();
    expectSameAsTheApp(QStringLiteral(BLD_SOURCE_DIR "/fixtures/studio/zipcrypto-small.io"), s, { track.dir.path() },
                       choose(1, ui::ImportAlign::Automatic, {}, {}, {}, QStringLiteral("Sam")));
}

TEST(ImportServer, PlacedPartsAreWhatFileOpenMakes) {
    // .ldr / .mpd as separate parts: File › Open's map, saved as a .bbm.
    const QString model = QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/tight-corner.mpd");
    parts::PartsLibrary lib;
    lib.addSearchPath(QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
    lib.scan();
    QTemporaryDir app, server;
    auto opened = import::readLDrawMap(model, lib);
    ASSERT_TRUE(opened.ok());
    ASSERT_TRUE(saveload::writeBbm(*opened.map, app.filePath(QStringLiteral("layout.bbm"))).ok);

    importserver::Request r = requestFor(model, {}, { QStringLiteral(BLD_PARTS_LIBRARY_ROOT) }, {}, server.path());
    r.mode = QStringLiteral("parts");
    const QJsonObject out = importserver::run(r);
    ASSERT_TRUE(out.value(QStringLiteral("ok")).toBool()) << QJsonDocument(out).toJson().toStdString();
    EXPECT_EQ(out.value(QStringLiteral("sheets")).toInt(), 4);
    EXPECT_EQ(out.value(QStringLiteral("bricks")).toInt(), 300);
    // Every id is new each time a map is read: compared by where each first appears.
    const auto sameIds = [](QByteArray bbm) {
        static const QRegularExpression id(QStringLiteral("\\b\\d{10,}\\b"));
        QString text = QString::fromUtf8(bbm);
        QHash<QString, QString> seen;
        QString out;
        qsizetype at = 0;
        for (auto it = id.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            out += text.mid(at, m.capturedStart() - at);
            if (!seen.contains(m.captured())) seen.insert(m.captured(), QStringLiteral("#%1").arg(seen.size()));
            out += seen.value(m.captured());
            at = m.capturedEnd();
        }
        return out + text.mid(at);
    };
    EXPECT_EQ(sameIds(readFile(app.filePath(QStringLiteral("layout.bbm")))),
              sameIds(readFile(server.filePath(QStringLiteral("layout.bbm")))));

    // Only LDraw layouts can be placed part by part.
    QTemporaryDir models;
    importserver::Request io = requestFor(twoStraights(models), {}, {}, {}, server.path());
    io.input = QStringLiteral(BLD_SOURCE_DIR "/fixtures/studio/zipcrypto-small.io");
    io.mode = QStringLiteral("parts");
    EXPECT_EQ(importserver::run(io).value(QStringLiteral("error")).toString(), QStringLiteral("mode_unavailable"));
}

TEST(ImportServer, AnalyseShowsWhatThePreviewShows) {
    TrackLibrary track;
    StubLDraw ldraw;
    QTemporaryDir models, out;
    importserver::Request r = analyseRequest(twoStraights(models), out.path());
    r.libraries.parts = { track.dir.path() };
    r.libraries.ldraw = ldraw.dir.path();
    const QJsonObject a = importserver::run(r);
    ASSERT_TRUE(a.value(QStringLiteral("ok")).toBool()) << QJsonDocument(a).toJson().toStdString();
    EXPECT_EQ(a.value(QStringLiteral("format")).toString(), QStringLiteral("ldraw"));
    EXPECT_EQ(a.value(QStringLiteral("suggestedName")).toString(), QStringLiteral("Two straights"));
    EXPECT_EQ(a.value(QStringLiteral("designer")).toString(), QStringLiteral("Robin"));
    const QJsonObject part = a.value(QStringLiteral("part")).toObject();
    EXPECT_EQ(part.value(QStringLiteral("widthStuds")).toInt(), 8);
    EXPECT_EQ(part.value(QStringLiteral("heightStuds")).toInt(), 2);
    EXPECT_EQ(part.value(QStringLiteral("pxPerStud")).toInt(), 32);
    EXPECT_EQ(part.value(QStringLiteral("connections")).toArray().size(), 2);
    EXPECT_EQ(part.value(QStringLiteral("stats")).toObject().value(QStringLiteral("ldrawResolved")).toInt(), 2);
    EXPECT_FALSE(QImage(out.filePath(QStringLiteral("preview.png"))).isNull());
    // An .ldr is also a layout (File › Open).
    EXPECT_EQ(a.value(QStringLiteral("layout")).toObject().value(QStringLiteral("bricks")).toInt(), 2);
}

TEST(ImportServer, RefusesWhatItShouldnt) {
    QTemporaryDir dir, out;
    const QString exe = dir.filePath(QStringLiteral("x.exe"));
    writeFile(exe, "MZ");
    EXPECT_EQ(importserver::run(analyseRequest(exe, out.path())).value(QStringLiteral("error")).toString(),
              QStringLiteral("unsupported_type"));
    const QString big = dir.filePath(QStringLiteral("big.ldr"));
    writeFile(big, QByteArray(2048, '0'));
    importserver::Request r = analyseRequest(big, out.path());
    r.maxInputBytes = 1024;
    EXPECT_EQ(importserver::run(r).value(QStringLiteral("error")).toString(), QStringLiteral("file_too_large"));
    EXPECT_EQ(importserver::run(analyseRequest(dir.filePath(QStringLiteral("gone.ldr")), out.path()))
                  .value(QStringLiteral("error")).toString(),
              QStringLiteral("no_input"));
    // A damaged .io is the reader's error, in words.
    const QString junk = dir.filePath(QStringLiteral("junk.io"));
    writeFile(junk, "not a zip");
    EXPECT_EQ(importserver::run(analyseRequest(junk, out.path())).value(QStringLiteral("error")).toString(),
              QStringLiteral("damaged_archive"));

    importserver::Request bad;
    QString err;
    EXPECT_FALSE(importserver::parseRequest(QJsonObject{ { QStringLiteral("command"), QStringLiteral("rm") } }, &bad, &err));
    EXPECT_FALSE(importserver::parseRequest(
        QJsonObject{ { QStringLiteral("command"), QStringLiteral("convert") }, { QStringLiteral("input"), QStringLiteral("a.ldr") },
                     { QStringLiteral("outDir"), QStringLiteral("o") },
                     { QStringLiteral("options"), QJsonObject{ { QStringLiteral("align"), QStringLiteral("../x") } } } },
        &bad, &err));
}

TEST(ImportServer, ZipBombsAndPathsOutOfTheFolderAreRefused) {
    QTemporaryDir dir;
    const auto zip = [&](const QString& name, const QList<test::CryptEntry>& entries) {
        const QString path = dir.filePath(name);
        writeFile(path, test::buildZip(entries));
        return path;
    };
    const importserver::ArchiveLimits limits{ 3, qint64(4) << 20, 100.0 };
    EXPECT_EQ(importserver::checkArchive(zip(QStringLiteral("ok.io"), { { QStringLiteral("model.ldr"), "0 x\n" } }), limits), QString());
    EXPECT_EQ(importserver::checkArchive(
                  zip(QStringLiteral("many.io"), { { QStringLiteral("a"), "1" }, { QStringLiteral("b"), "2" },
                                                   { QStringLiteral("c"), "3" }, { QStringLiteral("d"), "4" } }),
                  limits),
              QStringLiteral("too_many_entries"));
    EXPECT_EQ(importserver::checkArchive(zip(QStringLiteral("up.io"), { { QStringLiteral("CustomParts/../../evil.dat"), "x" } }), limits),
              QStringLiteral("unsafe_archive"));
    EXPECT_EQ(importserver::checkArchive(zip(QStringLiteral("abs.lxf"), { { QStringLiteral("/etc/passwd"), "x" } }), limits),
              QStringLiteral("unsafe_archive"));
    // 3 MB of zeros squeeze to a few KB: far over 100 to 1.
    EXPECT_EQ(importserver::checkArchive(zip(QStringLiteral("bomb.io"), { { QStringLiteral("model.ldr"), QByteArray(3 << 20, '0') } }), limits),
              QStringLiteral("archive_ratio"));
    // More than the limit in all, even when it doesn't squeeze at all.
    QByteArray noise(5 << 20, Qt::Uninitialized);
    QRandomGenerator rng(7);
    for (char& c : noise) c = static_cast<char>(rng.bounded(256));
    test::CryptEntry big{ QStringLiteral("model.ldr"), noise };
    big.deflate = false;
    EXPECT_EQ(importserver::checkArchive(zip(QStringLiteral("big.io"), { big }), limits), QStringLiteral("archive_too_big"));
    // Headers whose sizes don't add up aren't read at all.
    test::CryptEntry liar{ QStringLiteral("model.ldr"), "0 x\n" };
    liar.claimedSize = 5u << 20;
    EXPECT_NE(importserver::checkArchive(zip(QStringLiteral("liar.io"), { liar }), limits), QString());
}

// The program runs on the server only (Linux): its lock-down and limits are Linux's.
#if defined(BLD_IMPORT_SERVER_EXE) && defined(__linux__)
namespace {
QJsonObject runExe(const QStringList& args, int* exitCode = nullptr) {
    QProcess p;
    // As the server runs it: nothing in the environment.
    p.setProcessEnvironment(QProcessEnvironment());
    p.start(QStringLiteral(BLD_IMPORT_SERVER_EXE), args);
    EXPECT_TRUE(p.waitForFinished(120000));
    if (exitCode) *exitCode = p.exitStatus() == QProcess::CrashExit ? -1 : p.exitCode();
    return QJsonDocument::fromJson(p.readAllStandardOutput()).object();
}
}  // namespace

TEST(ImportServer, TheProgramGivesTheSameResult) {
    TrackLibrary track;
    StubLDraw ldraw;
    QTemporaryDir models, viaLib, viaExe;
    const QString model = twoStraights(models);
    ui::ImportSettings s;
    s.ldrawLibrary = ldraw.dir.path();
    const Choices c = choose(1, ui::ImportAlign::BoundingBox, QPointF(0.25, 0), { 0 }, QStringLiteral("Loop"));
    const QJsonObject lib = importserver::run(requestFor(model, s, { track.dir.path() }, c, viaLib.path()));
    ASSERT_TRUE(lib.value(QStringLiteral("ok")).toBool());

    const QString req = models.filePath(QStringLiteral("request.json"));
    writeFile(req, QJsonDocument(QJsonObject{
        { QStringLiteral("command"), QStringLiteral("convert") }, { QStringLiteral("input"), model },
        { QStringLiteral("outDir"), viaExe.path() },
        { QStringLiteral("libraries"), QJsonObject{ { QStringLiteral("parts"), QJsonArray{ track.dir.path() } },
                                                    { QStringLiteral("ldraw"), ldraw.dir.path() } } },
        { QStringLiteral("options"), QJsonObject{ { QStringLiteral("quarterTurns"), 1 }, { QStringLiteral("align"), QStringLiteral("box") },
                                                  { QStringLiteral("nudge"), QJsonArray{ 0.25, 0 } }, { QStringLiteral("drop"), QJsonArray{ 0 } },
                                                  { QStringLiteral("name"), QStringLiteral("Loop") }, { QStringLiteral("author"), QStringLiteral("Sam") } } } }).toJson());
    int code = -1;
    const QJsonObject exe = runExe({ req }, &code);
    ASSERT_EQ(code, 0) << QJsonDocument(exe).toJson().toStdString();
    EXPECT_EQ(exe.value(QStringLiteral("partKey")), lib.value(QStringLiteral("partKey")));
    const QString key = lib.value(QStringLiteral("partKey")).toString();
    for (const char* ext : { ".xml", ".png", ".gif" })
        EXPECT_TRUE(readFile(viaLib.filePath(QStringLiteral("part/") + key + QLatin1String(ext)))
                    == readFile(viaExe.filePath(QStringLiteral("part/") + key + QLatin1String(ext))))
            << ext;
    EXPECT_TRUE(exe.value(QStringLiteral("sandbox")).toObject().value(QStringLiteral("rlimits")).toBool());
}

#if defined(__x86_64__) || defined(__aarch64__)
TEST(ImportServer, TheSandboxBlocksNetworkProgramsAndProcesses) {
    const QJsonObject out = runExe({ QStringLiteral("--sandbox-check") });
    const QJsonObject s = out.value(QStringLiteral("sandbox")).toObject();
    EXPECT_TRUE(s.value(QStringLiteral("seccomp")).toBool()) << QJsonDocument(out).toJson().toStdString();
    EXPECT_TRUE(s.value(QStringLiteral("socketBlocked")).toBool());
    EXPECT_TRUE(s.value(QStringLiteral("execBlocked")).toBool());
    EXPECT_TRUE(s.value(QStringLiteral("forkBlocked")).toBool());
    EXPECT_TRUE(s.value(QStringLiteral("rlimits")).toBool());
}
#endif

TEST(ImportServer, TooLittleMemoryEndsTheRunInsteadOfHanging) {
    if (importserver::builtWithAsan()) GTEST_SKIP() << "AddressSanitizer needs more address space than any limit";
    QTemporaryDir models, out;
    const QString req = models.filePath(QStringLiteral("request.json"));
    writeFile(req, QJsonDocument(QJsonObject{ { QStringLiteral("command"), QStringLiteral("analyse") },
                                              { QStringLiteral("input"), twoStraights(models) },
                                              { QStringLiteral("outDir"), out.path() } }).toJson());
    int code = 0;
    runExe({ QStringLiteral("--memory-mb"), QStringLiteral("16"), req }, &code);
    EXPECT_NE(code, 0);
    EXPECT_EQ(runExe({ QStringLiteral("--cpu"), QStringLiteral("0"), req }, &code), QJsonObject());
    EXPECT_EQ(code, 2);  // a limit of 0 is a usage error, never "no limit"
}
#endif

// ---- Real files, opt-in (never committed) ----

TEST(ImportServer, RealStudioFileMatchesTheApp) {
    const QString path = qEnvironmentVariable("BLD_STUDIO_IO");
    if (path.isEmpty()) GTEST_SKIP() << "set BLD_STUDIO_IO (and BLD_STUDIO_LDRAW, BLD_LDRAW)";
    ui::ImportSettings s;
    s.studioLibrary = qEnvironmentVariable("BLD_STUDIO_LDRAW");
    s.ldrawLibrary = qEnvironmentVariable("BLD_LDRAW");
    expectSameAsTheApp(path, s, { QStringLiteral(BLD_PARTS_LIBRARY_ROOT) }, choose(1, ui::ImportAlign::Automatic, {}, { 0 }));
}

TEST(ImportServer, RealLddFilesMatchTheApp) {
    const QStringList paths = qEnvironmentVariable("BLD_LDD_IMPORT").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (paths.isEmpty()) GTEST_SKIP() << "set BLD_LDD_IMPORT (and BLD_LDD_ROOT, BLD_LDD_LDRAW_XML, BLD_LDRAW)";
    ui::ImportSettings s;
    s.lddPath = qEnvironmentVariable("BLD_LDD_ROOT");
    s.lddLdrawXml = qEnvironmentVariable("BLD_LDD_LDRAW_XML");
    s.ldrawLibrary = qEnvironmentVariable("BLD_LDRAW");
    const QString real = qEnvironmentVariable("BLD_PARTS");
    for (const QString& path : paths)
        expectSameAsTheApp(path, s, { real.isEmpty() ? QStringLiteral(BLD_PARTS_LIBRARY_ROOT) : real },
                           choose(3, ui::ImportAlign::BoundingBox, QPointF(0, 0.25), {}));
}
