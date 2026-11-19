// libFuzzer harnesses for the website's import (bld-import-server,
// docs/import-server.md): every path the server runs, from the uploaded
// file to the part or layout it makes. Built once per format with
// BLD_FUZZ_FORMAT set:
//   ldraw   .ldr / .mpd / .dat: analyse, convert to a part, place as parts
//   studio  .io: analyse, convert to a part
//   ldd     .lxf / .lxfml: analyse, convert to a part
// Each input runs with a small LDraw library and without one (the parts
// library's pictures), and with the preview's choices taken from its size.

#include "app/importserver/ImportServer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>

#include <cstdint>
#include <cstring>

using namespace bld;

namespace {

QString scratch;
QString ldraw;
QString parts;

bool is(const char* name) { return std::strcmp(BLD_FUZZ_FORMAT, name) == 0; }

void writeFile(const QString& path, const QByteArray& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(body);
}

QString extensionFor(const QByteArray& bytes) {
    if (is("studio")) return QStringLiteral("io");
    if (is("ldd")) return bytes.startsWith("PK") ? QStringLiteral("lxf") : QStringLiteral("lxfml");
    if (bytes.contains("0 FILE")) return QStringLiteral("mpd");
    return bytes.size() % 5 == 0 ? QStringLiteral("dat") : QStringLiteral("ldr");
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    new QApplication(*argc, *argv);  // never destroyed, as in FuzzTarget.cpp
    scratch = QDir::temp().filePath(QStringLiteral("bld-fuzz-import-%1-%2"))
                  .arg(QLatin1String(BLD_FUZZ_FORMAT))
                  .arg(QCoreApplication::applicationPid());
    QDir().mkpath(scratch);
    // A stand-in LDraw library: a brick, a plate and a straight track.
    ldraw = scratch + QStringLiteral("/ldraw");
    writeFile(ldraw + QStringLiteral("/LDConfig.ldr"), "0 stub\n0 !COLOUR Red CODE 4 VALUE #C91A09 EDGE #333333\n");
    writeFile(ldraw + QStringLiteral("/parts/3001.dat"), "4 16 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
    writeFile(ldraw + QStringLiteral("/parts/3023.dat"), "4 16 -20 0 -10 20 0 -10 20 0 10 -20 0 10\n");
    writeFile(ldraw + QStringLiteral("/parts/tt.dat"), "4 16 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
    // A parts library with one track piece, so snap points are found.
    parts = scratch + QStringLiteral("/parts");
    writeFile(parts + QStringLiteral("/TT.7.xml"),
              "<part><ConnexionList>"
              "<connexion><type>1</type><position><x>-2</x><y>0</y></position><angle>180</angle></connexion>"
              "<connexion><type>1</type><position><x>2</x><y>0</y></position><angle>0</angle></connexion>"
              "</ConnexionList><LDraw><Remap><Ldraw>tt</Ldraw></Remap></LDraw></part>");
    QImage sprite(32, 16, QImage::Format_ARGB32);
    sprite.fill(Qt::darkGray);
    sprite.save(parts + QStringLiteral("/TT.7.png"));
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1 << 20) return 0;  // the readers take whole files; keep runs quick
    // LDraw is drawn part by part at 32 px a stud: a 300-part layout as a
    // seed made each run take a third of a second, so the short CI run
    // barely moved. Small models find the same parser and pipeline paths
    // (the readers have their own targets for big files: ldraw_import, ldraw_map).
    if (is("ldraw") && size > 4096) return 0;
    const QByteArray bytes(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const QString input = scratch + QStringLiteral("/in/model.") + extensionFor(bytes);
    QDir(scratch + QStringLiteral("/in")).removeRecursively();
    writeFile(input, bytes);
    const QString out = scratch + QStringLiteral("/out");
    QDir(out).removeRecursively();
    QDir().mkpath(out);

    importserver::Request r;
    r.input = input;
    r.outDir = out;
    r.libraries.parts = { parts };
    if (size % 2 == 0) r.libraries.ldraw = ldraw;
    // LDD without its brick database goes through ldraw.xml (none here), and
    // Studio through Studio's library when one is set.
    if (is("studio") && size % 3 == 0) r.libraries.studioLdraw = ldraw;

    r.command = QStringLiteral("analyse");
    importserver::run(r);

    r.command = QStringLiteral("convert");
    r.mode = QStringLiteral("part");
    r.quarterTurns = static_cast<int>(size % 4);
    r.align = size % 3 == 0 ? QStringLiteral("box") : size % 3 == 1 ? QStringLiteral("bottom") : QString();
    r.nudgeX = (size % 7) * 0.0625;
    r.drop = { 0, static_cast<int>(size % 5) };
    r.designer = QStringLiteral("Fuzz");
    r.designerUrl = QStringLiteral("example.com");
    importserver::run(r);

    if (is("ldraw")) {
        r.mode = QStringLiteral("parts");
        importserver::run(r);
    }
    return 0;
}
