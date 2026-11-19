#include "ImportServer.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "import/ImportToPart.h"
#include "import/mapformats/LDrawMap.h"
#include "import/zip/SafeZip.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmWriter.h"
#include "ui/BackgroundTask.h"
#include "ui/ImportPipeline.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace bld::importserver {

namespace {

enum class Format { LDraw, LDrawMap, Studio, LDD, Unknown };

Format formatOf(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("ldr") || ext == QLatin1String("mpd")) return Format::LDrawMap;
    if (ext == QLatin1String("dat")) return Format::LDraw;
    if (ext == QLatin1String("io")) return Format::Studio;
    if (ext == QLatin1String("lxf") || ext == QLatin1String("lxfml")) return Format::LDD;
    return Format::Unknown;
}

bool isZip(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("io") || ext == QLatin1String("lxf");
}

QJsonObject failure(const QString& code, const QString& message) {
    return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), code }, { QStringLiteral("message"), message } };
}

QString formatName(Format f) {
    switch (f) {
    case Format::Studio: return QStringLiteral("studio");
    case Format::LDD: return QStringLiteral("ldd");
    default: return QStringLiteral("ldraw");
    }
}

// Every import runs inline: the converter has no UI to keep alive, and the
// server's wall-clock timeout is the Cancel.
const ui::HeavyRunner kInline = [](const QString&, const std::function<void(ui::CancelToken&)>& work) {
    ui::CancelToken token;
    work(token);
    return true;
};

ui::ImportSettings settingsFor(const Libraries& libs) {
    ui::ImportSettings s;  // pxPerStud: the app's default, 32
    s.ldrawLibrary = libs.ldraw;
    s.studioLibrary = libs.studioLdraw;
    s.lddPath = libs.lddAssets;
    s.lddLdrawXml = libs.lddLdrawXml;
    return s;
}

QJsonObject statsJson(const ui::ImportStats& s) {
    return { { QStringLiteral("ldrawResolved"), s.ldrawResolved },
             { QStringLiteral("lddRendered"), s.lddRendered },
             { QStringLiteral("unresolved"), s.unresolved } };
}

QJsonArray connectionsJson(const ui::PreparedPart& p) {
    QJsonArray out;
    for (const auto& c : p.connections)
        out.append(QJsonObject{ { QStringLiteral("type"), c.type },
                                { QStringLiteral("x"), c.xStuds },
                                { QStringLiteral("y"), c.yStuds },
                                { QStringLiteral("angle"), c.angleDeg } });
    return out;
}

QJsonObject partJson(const ui::PreparedPart& p) {
    const QRectF c = p.contentStuds;
    return { { QStringLiteral("widthStuds"), p.widthStuds },
             { QStringLiteral("heightStuds"), p.heightStuds },
             { QStringLiteral("pxPerStud"), p.widthStuds > 0 ? p.sprite.width() / p.widthStuds : 0 },
             { QStringLiteral("contentStuds"), QJsonObject{ { QStringLiteral("x"), c.x() }, { QStringLiteral("y"), c.y() },
                                                            { QStringLiteral("width"), c.width() },
                                                            { QStringLiteral("height"), c.height() } } },
             { QStringLiteral("baseOnGrid"), p.baseOnGrid },
             { QStringLiteral("snapMargin"), QJsonObject{ { QStringLiteral("left"), p.snapMargin.left() },
                                                          { QStringLiteral("top"), p.snapMargin.top() },
                                                          { QStringLiteral("right"), p.snapMargin.right() },
                                                          { QStringLiteral("bottom"), p.snapMargin.bottom() } } },
             { QStringLiteral("connections"), connectionsJson(p) },
             { QStringLiteral("stats"), statsJson(p.stats) },
             { QStringLiteral("warnings"), QJsonArray::fromStringList(p.warnings) } };
}

int countBricks(const core::Map& map, int* sheets) {
    int bricks = 0;
    *sheets = 0;
    for (const auto& l : map.layers()) {
        if (l->kind() != core::LayerKind::Brick) continue;
        ++*sheets;
        bricks += static_cast<int>(static_cast<const core::LayerBrick&>(*l).bricks.size());
    }
    return bricks;
}

std::unique_ptr<parts::PartsLibrary> partsFor(const Libraries& libs) {
    auto lib = std::make_unique<parts::PartsLibrary>();
    for (const QString& dir : libs.parts) lib->addSearchPath(dir);
    lib->scan();
    return lib;
}

ui::ImportAlign alignOf(const QString& a) {
    if (a == QLatin1String("bottom")) return ui::ImportAlign::BottomLayer;
    if (a == QLatin1String("box")) return ui::ImportAlign::BoundingBox;
    return ui::ImportAlign::Automatic;
}

QJsonObject analyse(const Request& r, Format format) {
    auto parts = partsFor(r.libraries);
    QJsonObject out{ { QStringLiteral("ok"), true }, { QStringLiteral("format"), formatName(format) } };

    // The layout File › Open makes of an .ldr / .mpd.
    if (format == Format::LDrawMap) {
        auto map = import::readLDrawMap(r.input, *parts);
        if (map.ok()) {
            int sheets = 0;
            const int bricks = countBricks(*map.map, &sheets);
            out.insert(QStringLiteral("layout"), QJsonObject{ { QStringLiteral("sheets"), sheets },
                                                              { QStringLiteral("bricks"), bricks },
                                                              { QStringLiteral("warnings"), QJsonArray::fromStringList(map.warnings) },
                                                              { QStringLiteral("partlyRead"), map.partlyRead } });
        } else {
            out.insert(QStringLiteral("layoutError"), map.error);
        }
    }

    ui::PreparedPart part = ui::prepareImport(r.input, settingsFor(r.libraries), *parts, kInline);
    out.insert(QStringLiteral("kindLabel"), part.kindLabel);
    if (!part.ok()) {
        out.insert(QStringLiteral("partError"), part.error);
        if (!out.contains(QStringLiteral("layout")))
            return failure(QStringLiteral("nothing_to_import"), part.error.isEmpty() ? QStringLiteral("Nothing could be imported from this file.") : part.error);
        return out;
    }
    const QString preview = QDir(r.outDir).filePath(QStringLiteral("preview.png"));
    if (!part.sprite.save(preview, "PNG")) return failure(QStringLiteral("write_failed"), QStringLiteral("Couldn't write the preview."));
    QJsonObject pj = partJson(part);
    pj.insert(QStringLiteral("sprite"), QStringLiteral("preview.png"));
    out.insert(QStringLiteral("part"), pj);
    out.insert(QStringLiteral("title"), part.title);
    out.insert(QStringLiteral("designer"), part.designer);
    const QString suggested = ui::suggestedImportName(part);
    out.insert(QStringLiteral("suggestedName"), suggested);
    out.insert(QStringLiteral("partKey"), import::importedPartKey(suggested));
    return out;
}

QJsonObject convertPart(const Request& r) {
    auto parts = partsFor(r.libraries);
    ui::PreparedPart prepared = ui::prepareImport(r.input, settingsFor(r.libraries), *parts, kInline);
    if (!prepared.ok())
        return failure(QStringLiteral("nothing_to_import"), prepared.error.isEmpty() ? QStringLiteral("Nothing could be imported from this file.") : prepared.error);
    // As the preview does it: turn the part as prepared, then line it up.
    ui::rotatePart(prepared, r.quarterTurns);
    const ui::PreparedPart shown = ui::alignPart(prepared, alignOf(r.align), QPointF(r.nudgeX, r.nudgeY));
    QVector<bool> keep(shown.connections.size(), true);
    for (int i : r.drop)
        if (i >= 0 && i < keep.size()) keep[i] = false;
    const ui::PreparedPart result = ui::finishPreview(shown, keep, r.designer, r.designerUrl);
    const QString name = r.name.trimmed().isEmpty() ? ui::suggestedImportName(prepared) : r.name.trimmed();

    const QString dir = QDir(r.outDir).filePath(QStringLiteral("part"));
    QDir().mkpath(dir);
    QString err;
    const QString key = ui::writeImportedPart(result, name, dir, r.author, false, &err);
    if (key.isEmpty()) return failure(QStringLiteral("write_failed"), err);
    // The picture the app sends: the hi-res .png when there is one.
    const bool png = QFileInfo::exists(QDir(dir).filePath(key + QStringLiteral(".png")));
    QString shownName = name;
    static const QRegularExpression modelExt(QStringLiteral("\\.(ldr|dat|mpd|io|lxf|lxfml)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    shownName.remove(modelExt);
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("mode"), QStringLiteral("part") },
             { QStringLiteral("partKey"), key },
             { QStringLiteral("displayName"), shownName.trimmed() },
             { QStringLiteral("xml"), QStringLiteral("part/") + key + QStringLiteral(".xml") },
             { QStringLiteral("sprite"), QStringLiteral("part/") + key + (png ? QStringLiteral(".png") : QStringLiteral(".gif")) },
             { QStringLiteral("spriteMime"), png ? QStringLiteral("image/png") : QStringLiteral("image/gif") },
             { QStringLiteral("widthStuds"), result.widthStuds },
             { QStringLiteral("heightStuds"), result.heightStuds },
             { QStringLiteral("connections"), static_cast<int>(result.connections.size()) },
             { QStringLiteral("stats"), statsJson(result.stats) },
             { QStringLiteral("warnings"), QJsonArray::fromStringList(result.warnings) } };
}

QJsonObject convertLayout(const Request& r) {
    auto parts = partsFor(r.libraries);
    auto map = import::readLDrawMap(r.input, *parts);
    if (!map.ok()) return failure(QStringLiteral("nothing_to_import"), map.error);
    const QString bbm = QDir(r.outDir).filePath(QStringLiteral("layout.bbm"));
    const auto written = saveload::writeBbm(*map.map, bbm);
    if (!written.ok) return failure(QStringLiteral("write_failed"), written.error);
    int sheets = 0;
    const int bricks = countBricks(*map.map, &sheets);
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("mode"), QStringLiteral("parts") },
             { QStringLiteral("bbm"), QStringLiteral("layout.bbm") },
             { QStringLiteral("sheets"), sheets },
             { QStringLiteral("bricks"), bricks },
             { QStringLiteral("warnings"), QJsonArray::fromStringList(map.warnings) },
             { QStringLiteral("partlyRead"), map.partlyRead } };
}

}  // namespace

QString checkArchive(const QString& path, const ArchiveLimits& limits) {
    // Headers only: SafeZip reads the central directory; nothing is inflated.
    const auto zip = import::SafeZip::open(path);
    if (!zip || !zip->isValid()) return QStringLiteral("damaged_archive");
    const auto& entries = zip->entries();
    if (entries.size() > limits.maxEntries) return QStringLiteral("too_many_entries");
    qint64 total = 0;
    for (const auto& e : entries) {
        if (e.isSymLink) return QStringLiteral("unsafe_archive");
        const QString n = QString(e.name).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (n.startsWith(QLatin1Char('/')) || (n.size() > 1 && n[1] == QLatin1Char(':'))
            || n.split(QLatin1Char('/')).contains(QStringLiteral("..")))
            return QStringLiteral("unsafe_archive");
        if (e.size < 0 || e.compressedSize < 0) return QStringLiteral("damaged_archive");
        total += e.size;
        if (total > limits.maxTotalBytes) return QStringLiteral("archive_too_big");
        if (e.size > (qint64(1) << 20) && e.size > limits.maxRatio * std::max<qint64>(1, e.compressedSize))
            return QStringLiteral("archive_ratio");
    }
    return {};
}

bool parseRequest(const QJsonObject& j, Request* out, QString* error) {
    Request r;
    r.command = j.value(QStringLiteral("command")).toString();
    r.input = j.value(QStringLiteral("input")).toString();
    r.outDir = j.value(QStringLiteral("outDir")).toString();
    if (r.command != QLatin1String("analyse") && r.command != QLatin1String("convert")) {
        *error = QStringLiteral("command must be analyse or convert");
        return false;
    }
    if (r.input.isEmpty() || r.outDir.isEmpty()) {
        *error = QStringLiteral("input and outDir are required");
        return false;
    }
    const QJsonObject libs = j.value(QStringLiteral("libraries")).toObject();
    for (const auto& v : libs.value(QStringLiteral("parts")).toArray()) r.libraries.parts << v.toString();
    r.libraries.ldraw = libs.value(QStringLiteral("ldraw")).toString();
    r.libraries.studioLdraw = libs.value(QStringLiteral("studioLdraw")).toString();
    r.libraries.lddAssets = libs.value(QStringLiteral("lddAssets")).toString();
    r.libraries.lddLdrawXml = libs.value(QStringLiteral("lddLdrawXml")).toString();
    const QJsonObject limits = j.value(QStringLiteral("limits")).toObject();
    if (limits.contains(QStringLiteral("maxInputBytes")))
        r.maxInputBytes = static_cast<qint64>(limits.value(QStringLiteral("maxInputBytes")).toDouble());
    if (limits.contains(QStringLiteral("maxEntries")))
        r.archive.maxEntries = limits.value(QStringLiteral("maxEntries")).toInt();
    if (limits.contains(QStringLiteral("maxUncompressedBytes")))
        r.archive.maxTotalBytes = static_cast<qint64>(limits.value(QStringLiteral("maxUncompressedBytes")).toDouble());
    if (limits.contains(QStringLiteral("maxRatio")))
        r.archive.maxRatio = limits.value(QStringLiteral("maxRatio")).toDouble();
    const QJsonObject o = j.value(QStringLiteral("options")).toObject();
    r.mode = o.value(QStringLiteral("mode")).toString(QStringLiteral("part"));
    if (r.mode != QLatin1String("part") && r.mode != QLatin1String("parts")) {
        *error = QStringLiteral("mode must be part or parts");
        return false;
    }
    r.quarterTurns = ((o.value(QStringLiteral("quarterTurns")).toInt() % 4) + 4) % 4;
    r.align = o.value(QStringLiteral("align")).toString();
    if (!r.align.isEmpty() && r.align != QLatin1String("bottom") && r.align != QLatin1String("box")) {
        *error = QStringLiteral("align must be empty, bottom or box");
        return false;
    }
    const QJsonArray nudge = o.value(QStringLiteral("nudge")).toArray();
    if (nudge.size() == 2) {
        // The preview moves by 1/16 stud at the finest; more than a few
        // studs is never needed.
        r.nudgeX = std::clamp(nudge[0].toDouble(), -64.0, 64.0);
        r.nudgeY = std::clamp(nudge[1].toDouble(), -64.0, 64.0);
    }
    for (const auto& v : o.value(QStringLiteral("drop")).toArray()) r.drop << v.toInt(-1);
    r.name = o.value(QStringLiteral("name")).toString().left(200);
    r.designer = o.value(QStringLiteral("designer")).toString().left(200);
    r.designerUrl = o.value(QStringLiteral("designerUrl")).toString().left(500);
    r.author = o.value(QStringLiteral("author")).toString().left(200);
    *out = r;
    return true;
}

QJsonObject run(const Request& r) {
    const Format format = formatOf(r.input);
    if (format == Format::Unknown)
        return failure(QStringLiteral("unsupported_type"), QStringLiteral("Only LDraw (.ldr, .mpd, .dat), Studio (.io) and LDD (.lxf, .lxfml) files are imported."));
    const QFileInfo in(r.input);
    if (!in.isFile()) return failure(QStringLiteral("no_input"), QStringLiteral("The uploaded file is missing."));
    if (in.size() > r.maxInputBytes) return failure(QStringLiteral("file_too_large"), QStringLiteral("The file is too big to import."));
    if (!QFileInfo(r.outDir).isDir()) return failure(QStringLiteral("no_out_dir"), QStringLiteral("The output folder is missing."));
    if (isZip(r.input)) {
        if (const QString why = checkArchive(r.input, r.archive); !why.isEmpty())
            return failure(why, QStringLiteral("The file isn't a model this site can open safely (%1).").arg(why));
    }
    if (r.command == QLatin1String("analyse")) return analyse(r, format);
    if (r.mode == QLatin1String("parts")) {
        if (format != Format::LDrawMap)
            return failure(QStringLiteral("mode_unavailable"), QStringLiteral("Only .ldr and .mpd files can be placed as separate parts."));
        return convertLayout(r);
    }
    return convertPart(r);
}

}  // namespace bld::importserver
