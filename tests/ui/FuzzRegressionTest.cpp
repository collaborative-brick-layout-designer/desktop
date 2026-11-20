// Inputs the parser fuzzers (fuzz/README.md) once broke, replayed on
// every build. Each must be handled quickly without undefined behaviour
// (the sanitizer job runs these too).

#include "app/importserver/ImportServer.h"
#include "import/mapformats/TrackDesignerMap.h"
#include "import/studio/StudioReader.h"
#include "parts/PartsLibrary.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

using namespace bld;

namespace {
QStringList regressions(const QString& pattern) {
    const QDir dir(QStringLiteral(BLD_SOURCE_DIR "/fixtures/fuzz-regressions"));
    QStringList out;
    for (const QString& f : dir.entryList({ pattern }, QDir::Files)) out << dir.filePath(f);
    return out;
}
}  // namespace

TEST(FuzzRegressions, TrackDesignerMapsWithAbsurdCoordinates) {
    parts::PartsLibrary lib;
    lib.addSearchPath(QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts"));
    lib.scan();
    QTemporaryDir tmp;
    const QStringList files = regressions(QStringLiteral("*.tdl"));
    ASSERT_FALSE(files.isEmpty());
    for (const QString& f : files) {
        SCOPED_TRACE(f.toStdString());
        auto r = import::readTrackDesignerMap(f, lib);
        if (r.map) import::writeTrackDesignerMap(*r.map, tmp.filePath(QStringLiteral("out.tdl")), lib);
    }
}

TEST(FuzzRegressions, StudioArchiveClaimingAHugeModel) {
    const QStringList files = regressions(QStringLiteral("*.io"));
    ASSERT_FALSE(files.isEmpty());
    for (const QString& f : files) {
        QElapsedTimer t;
        t.start();
        import::readStudioIo(f);
        EXPECT_LT(t.elapsed(), 5000) << f.toStdString();
    }
}

// LDraw models whose parts are scaled or moved far beyond any layout (a
// matrix entry of 1.8e67): the importer's picture overflowed turning sizes
// into whole numbers. Now there's no picture, and nothing undefined.
TEST(FuzzRegressions, LDrawModelsFarBeyondAnyLayout) {
    QTemporaryDir tmp;
    const QString ldraw = tmp.filePath(QStringLiteral("ldraw"));
    QDir().mkpath(ldraw + QStringLiteral("/parts"));
    const auto write = [](const QString& path, const QByteArray& body) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(body);
    };
    // The fuzzer's stand-in library (fuzz/ImportServerFuzz.cpp).
    write(ldraw + QStringLiteral("/LDConfig.ldr"), "0 stub\n0 !COLOUR Red CODE 4 VALUE #C91A09 EDGE #333333\n");
    for (const char* part : { "3001", "tt" })
        write(ldraw + QStringLiteral("/parts/%1.dat").arg(QLatin1String(part)), "4 16 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
    const QStringList files = regressions(QStringLiteral("*.ldr"));
    ASSERT_FALSE(files.isEmpty());
    for (const QString& f : files) {
        SCOPED_TRACE(f.toStdString());
        const QString out = tmp.filePath(QStringLiteral("out"));
        QDir().mkpath(out);
        importserver::Request r;
        r.input = f;
        r.outDir = out;
        r.libraries.ldraw = ldraw;
        r.command = QStringLiteral("analyse");
        importserver::run(r);
        r.command = QStringLiteral("convert");
        r.mode = QStringLiteral("part");
        r.align = QStringLiteral("box");
        importserver::run(r);
        QDir(out).removeRecursively();
    }
}
