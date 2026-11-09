// Headless map conversion and comparison, as the app does it:
//
//   bld-convert [-p <parts dir>] <input> <output>
//       Read .bld-layout / .bbm / .ldr / .mpd / .tdl / .ncp and write any of them (by
//       extension). Like opening the file in the app, bricks are relinked
//       and stale sizes fixed before writing.
//
//   bld-convert [-p <parts dir>] --compare <a> <b> [--no-links] [--known-only] [--everything]
//       Compare two maps brick by brick (part, position, size and angle
//       within 0.02 studs / 0.05 degrees, and how many of each brick's
//       connections are linked). --known-only skips parts the library
//       lacks (BlueBrick redraws their placeholders at another size), and
//       links to them (BlueBrick drops those on reading, we keep them).
//       --everything compares the rest too: the map's details (author,
//       club, event, date, comment, background, export settings), every
//       sheet's settings, text, rulers, areas, the grid and groups.
//       Exit status 1 and a list when they differ.
//
// Used by scripts/bluebrick-oracle/compat.sh to check files against
// vanilla BlueBrick.

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "import/LayoutFile.h"
#include "import/mapformats/FourDBrixMap.h"
#include "import/mapformats/LDrawMap.h"
#include "import/mapformats/TrackDesignerMap.h"
#include "parts/BrickPlacement.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace bld;

namespace {

QString suffixOf(const QString& path) { return QFileInfo(path).suffix().toLower(); }

std::unique_ptr<core::Map> readMap(const QString& path, parts::PartsLibrary& lib, QString* error) {
    const QString s = suffixOf(path);
    if (import::isLayoutFile(path)) {
        auto r = import::readLayoutFile(path, QDir::temp().filePath(QStringLiteral("bld-layout-assets")));
        if (!r.ok()) { *error = r.error; return nullptr; }
        return std::move(r.map);
    }
    if (s == QLatin1String("bbm")) {
        auto r = saveload::readBbm(path);
        if (!r.ok()) { *error = r.error; return nullptr; }
        return std::move(r.map);
    }
    import::MapReadResult r;
    if (s == QLatin1String("ldr") || s == QLatin1String("mpd")) r = import::readLDrawMap(path, lib);
    else if (s == QLatin1String("tdl")) r = import::readTrackDesignerMap(path, lib);
    else if (s == QLatin1String("ncp")) r = import::readFourDBrixMap(path, lib);
    else { *error = QStringLiteral("unknown map format: %1").arg(path); return nullptr; }
    if (!r.ok()) *error = r.error;
    return std::move(r.map);
}

bool writeMap(const core::Map& map, const QString& path, parts::PartsLibrary& lib, QString* error) {
    const QString s = suffixOf(path);
    if (import::isLayoutFile(path)) return import::writeLayoutFile(map, path, error);
    if (s == QLatin1String("bbm")) {
        const auto r = saveload::writeBbm(map, path);
        if (!r.ok) *error = r.error;
        return r.ok;
    }
    if (s == QLatin1String("ldr") || s == QLatin1String("mpd")) return import::writeLDrawMap(map, path, lib, error);
    if (s == QLatin1String("tdl")) return import::writeTrackDesignerMap(map, path, lib, error);
    if (s == QLatin1String("ncp")) return import::writeFourDBrixMap(map, path, lib, error);
    *error = QStringLiteral("unknown map format: %1").arg(path);
    return false;
}

struct Key {
    QString part;
    double x, y, w, h, angle;
    int links;
    QString text() const {
        return QStringLiteral("%1 at (%2, %3) %4x%5 @%6, %7 linked")
            .arg(part).arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(w, 0, 'f', 2).arg(h, 0, 'f', 2)
            .arg(angle, 0, 'f', 2).arg(links);
    }
};

std::vector<Key> keysOf(const core::Map& map, const parts::PartsLibrary* knownOnly) {
    std::vector<Key> out;
    // With --known-only, a link to a part the library lacks is skipped too:
    // we keep such a link as the file has it, BlueBrick drops it on reading.
    QSet<QString> unknown;
    if (knownOnly)
        for (const auto& layer : map.layers())
            if (layer->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
                    if (!knownOnly->metadata(b.partNumber))
                        for (const auto& c : b.connections) unknown.insert(c.guid);
    for (const auto& layer : map.layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
            if (knownOnly && !knownOnly->metadata(b.partNumber)) continue;
            int links = 0;
            for (const auto& c : b.connections) links += !c.linkedToId.isEmpty() && !unknown.contains(c.linkedToId);
            const QPointF c = b.displayArea.center();
            out.push_back({ b.partNumber.toUpper(), c.x(), c.y(), b.displayArea.width(), b.displayArea.height(),
                            std::remainder(static_cast<double>(b.orientation), 360.0), links });
        }
    }
    return out;
}

int compare(const core::Map& a, const core::Map& b, bool links, const parts::PartsLibrary* knownOnly) {
    auto ka = keysOf(a, knownOnly), kb = keysOf(b, knownOnly);
    const auto same = [&](const Key& p, const Key& q) {
        return p.part == q.part && std::abs(p.x - q.x) < 0.02 && std::abs(p.y - q.y) < 0.02
            && std::abs(p.w - q.w) < 0.02 && std::abs(p.h - q.h) < 0.02
            && std::abs(std::remainder(p.angle - q.angle, 360.0)) < 0.05 && (!links || p.links == q.links);
    };
    const size_t total = ka.size();
    for (auto it = ka.begin(); it != ka.end();) {
        const auto m = std::find_if(kb.begin(), kb.end(), [&](const Key& k) { return same(*it, k); });
        if (m != kb.end()) { kb.erase(m); it = ka.erase(it); } else { ++it; }
    }
    if (ka.empty() && kb.empty()) {
        std::printf("same: %zu bricks\n", total);
        return 0;
    }
    std::printf("DIFFERENT: %zu brick(s) only in the first, %zu only in the second\n", ka.size(), kb.size());
    for (size_t i = 0; i < ka.size() && i < 10; ++i) std::printf("  first only:  %s\n", qPrintable(ka[i].text()));
    for (size_t i = 0; i < kb.size() && i < 10; ++i) std::printf("  second only: %s\n", qPrintable(kb[i].text()));
    return 1;
}

// Everything but the bricks, one line each, rounded so that what a float
// round trip changes doesn't count: compared as two sorted lists.
QString num(double v) {
    return QString::number(std::abs(v) < 0.005 ? 0.0 : v, 'f', 2);
}
QString color(const core::ColorSpec& c) {
    return c.isKnown() ? c.knownName : QStringLiteral("#%1").arg(c.color.rgba(), 8, 16, QLatin1Char('0'));
}
// Without the family: BlueBrick swaps a font the machine it runs on lacks
// for one it has (Arial becomes Tahoma under Wine), and the byte-exact
// round-trip tests already keep the family.
QString font(const core::FontSpec& f) {
    return QStringLiteral("%1 %2").arg(num(f.sizePt), f.styleString);
}
QString rect(const QRectF& r) {
    return QStringLiteral("(%1,%2 %3x%4)").arg(num(r.x()), num(r.y()), num(r.width()), num(r.height()));
}
QString point(const QPointF& p) {
    return QStringLiteral("(%1,%2)").arg(num(p.x()), num(p.y()));
}
QString groups(const std::vector<core::Group>& gs, const QString& sheet) {
    QStringList out;
    for (const auto& g : gs)
        out << QStringLiteral("%1 group %2 part '%3' in %4").arg(sheet, g.guid, g.partNumber, g.myGroupId);
    out.sort();
    return out.join(QLatin1Char('\n'));
}

QStringList everythingElse(const core::Map& map) {
    QStringList out;
    out << QStringLiteral("author '%1', club '%2', event '%3', date %4")
               .arg(map.author, map.lug, map.event, map.date.toString(Qt::ISODate))
        << QStringLiteral("comment '%1'").arg(map.comment)
        << QStringLiteral("background %1").arg(color(map.backgroundColor))
        << QStringLiteral("export '%1' type %2 area %3 scale %4 watermark %5 circuit %6 points %7")
               .arg(map.exportInfo.exportPath)
               .arg(map.exportInfo.fileTypeIndex)
               .arg(rect(map.exportInfo.area), num(map.exportInfo.scale))
               .arg(map.exportInfo.watermark)
               .arg(map.exportInfo.electricCircuit)
               .arg(map.exportInfo.connectionPoints)
        << QStringLiteral("picked sheet %1").arg(map.selectedLayerIndexToSave());
    int index = 0;
    for (const auto& layer : map.layers()) {
        const QString sheet = QStringLiteral("sheet %1 '%2'").arg(index++).arg(layer->name);
        out << QStringLiteral("%1 kind %2 %3 transparency %4 hull %5 %6 %7")
                   .arg(sheet)
                   .arg(static_cast<int>(layer->kind()))
                   .arg(layer->visible ? QStringLiteral("shown") : QStringLiteral("hidden"))
                   .arg(layer->transparency)
                   .arg(layer->hull.displayHulls)
                   .arg(color(layer->hull.color))
                   .arg(layer->hull.thickness);
        switch (layer->kind()) {
        case core::LayerKind::Grid: {
            const auto& g = static_cast<const core::LayerGrid&>(*layer);
            out << QStringLiteral("%1 grid %2 %3 %4, sub %5 %6 %7, every %8 studs / %9")
                       .arg(sheet, color(g.gridColor), num(g.gridThickness))
                       .arg(g.displayGrid)
                       .arg(color(g.subGridColor), num(g.subGridThickness))
                       .arg(g.displaySubGrid)
                       .arg(g.gridSizeInStud)
                       .arg(g.subDivisionNumber)
                << QStringLiteral("%1 cell index %2 %3 %4 %5/%6 at %7,%8")
                       .arg(sheet)
                       .arg(g.displayCellIndex)
                       .arg(font(g.cellIndexFont), color(g.cellIndexColor))
                       .arg(static_cast<int>(g.cellIndexColumnType))
                       .arg(static_cast<int>(g.cellIndexRowType))
                       .arg(g.cellIndexCorner.x())
                       .arg(g.cellIndexCorner.y());
            break;
        }
        case core::LayerKind::Brick:
            if (const QString g = groups(static_cast<const core::LayerBrick&>(*layer).groups, sheet);
                !g.isEmpty())
                out << g;
            break;
        case core::LayerKind::Text: {
            const auto& t = static_cast<const core::LayerText&>(*layer);
            // Where a text is comes in separately (textPlaces): BlueBrick
            // sizes it to the font it found, which moves it a little.
            for (const auto& c : t.textCells)
                out << QStringLiteral("%1 text '%2' @%3 %4 %5 align %6 group '%7'")
                           .arg(sheet, c.text, num(std::remainder(c.orientation, 360.0)), font(c.font),
                                color(c.fontColor))
                           .arg(static_cast<int>(c.alignment))
                           .arg(c.myGroupId);
            if (const QString g = groups(t.groups, sheet); !g.isEmpty()) out << g;
            break;
        }
        case core::LayerKind::Area: {
            const auto& a = static_cast<const core::LayerArea&>(*layer);
            out << QStringLiteral("%1 area cells of %2 studs").arg(sheet).arg(a.areaCellSizeInStud);
            for (const auto& c : a.cells)
                out << QStringLiteral("%1 cell %2,%3 #%4")
                           .arg(sheet)
                           .arg(c.x)
                           .arg(c.y)
                           .arg(c.color.rgba(), 8, 16, QLatin1Char('0'));
            break;
        }
        case core::LayerKind::Ruler: {
            const auto& r = static_cast<const core::LayerRuler&>(*layer);
            for (const auto& any : r.rulers) {
                const core::RulerItemBase& b = any.kind == core::RulerKind::Linear
                                                   ? static_cast<const core::RulerItemBase&>(any.linear)
                                                   : static_cast<const core::RulerItemBase&>(any.circular);
                const QString look =
                    QStringLiteral("%1 %2 distance %3 unit %4/%5 guide %6 %7 font %8 %9 group '%10'")
                        .arg(color(b.color), num(b.lineThickness))
                        .arg(b.displayDistance)
                        .arg(b.displayUnit)
                        .arg(b.unit)
                        .arg(color(b.guidelineColor), num(b.guidelineThickness))
                        .arg(font(b.measureFont), color(b.measureFontColor), b.myGroupId);
                if (any.kind == core::RulerKind::Linear)
                    out << QStringLiteral("%1 line %2-%3 offset %4 %5 to '%6'/'%7' %8")
                               .arg(sheet, point(any.linear.point1), point(any.linear.point2),
                                    num(any.linear.offsetDistance))
                               .arg(any.linear.allowOffset)
                               .arg(any.linear.attachedBrick1Id, any.linear.attachedBrick2Id, look);
                else
                    out << QStringLiteral("%1 circle %2 r %3 to '%4' %5")
                               .arg(sheet, point(any.circular.center), num(any.circular.radius),
                                    any.circular.attachedBrickId, look);
            }
            if (const QString g = groups(r.groups, sheet); !g.isEmpty()) out << g;
            break;
        }
        default: break;
        }
    }
    out.sort();
    return out;
}

// Each text's middle, by sheet and words: within 4 studs counts as the same
// place (the font BlueBrick finds sizes the text box).
struct TextPlace {
    QString key;
    QPointF centre;
};
std::vector<TextPlace> textPlaces(const core::Map& map) {
    std::vector<TextPlace> out;
    int index = 0;
    for (const auto& layer : map.layers()) {
        const int sheet = index++;
        if (layer->kind() != core::LayerKind::Text) continue;
        for (const auto& c : static_cast<const core::LayerText&>(*layer).textCells)
            out.push_back(
                { QStringLiteral("sheet %1 text '%2'").arg(sheet).arg(c.text), c.displayArea.center() });
    }
    return out;
}

// The lines only one side has, at most ten of each.
int compareEverythingElse(const core::Map& a, const core::Map& b) {
    auto pa = textPlaces(a), pb = textPlaces(b);
    for (auto it = pa.begin(); it != pa.end();) {
        const auto m = std::find_if(pb.begin(), pb.end(), [&](const TextPlace& q) {
            return q.key == it->key && std::abs(q.centre.x() - it->centre.x()) < 4
                   && std::abs(q.centre.y() - it->centre.y()) < 4;
        });
        if (m != pb.end()) {
            pb.erase(m);
            it = pa.erase(it);
        } else {
            ++it;
        }
    }
    QStringList la = everythingElse(a), lb = everythingElse(b);
    for (const auto& p : pa) la << QStringLiteral("%1 at %2").arg(p.key, point(p.centre));
    for (const auto& p : pb) lb << QStringLiteral("%1 at %2").arg(p.key, point(p.centre));
    for (auto it = la.begin(); it != la.end();) {
        const int at = lb.indexOf(*it);
        if (at >= 0) {
            lb.removeAt(at);
            it = la.erase(it);
        } else {
            ++it;
        }
    }
    if (la.isEmpty() && lb.isEmpty()) return 0;
    std::printf("DIFFERENT: %lld line(s) only in the first, %lld only in the second\n",
                static_cast<long long>(la.size()), static_cast<long long>(lb.size()));
    for (qsizetype i = 0; i < la.size() && i < 10; ++i) std::printf("  first only:  %s\n", qPrintable(la[i]));
    for (qsizetype i = 0; i < lb.size() && i < 10; ++i) std::printf("  second only: %s\n", qPrintable(lb[i]));
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);  // part sprites give the footprints

    QStringList args = app.arguments().mid(1);
    QString partsDir;
    const int p = args.indexOf(QStringLiteral("-p"));
    if (p >= 0 && p + 1 < args.size()) {
        partsDir = args[p + 1];
        args.remove(p, 2);
    } else {
        const QString exe = QCoreApplication::applicationDirPath();
        for (const QString& rel : { QStringLiteral("/../../../parts/BlueBrickParts/parts"),
                                     QStringLiteral("/../share/brick-layout-designer/parts/BlueBrickParts/parts"),
                                     QStringLiteral("/parts/BlueBrickParts/parts") })
            if (QDir(exe + rel).exists()) { partsDir = exe + rel; break; }
    }
    const bool compareMode = args.removeAll(QStringLiteral("--compare")) > 0;
    const bool links = args.removeAll(QStringLiteral("--no-links")) == 0;
    const bool knownOnly = args.removeAll(QStringLiteral("--known-only")) > 0;
    const bool everything = args.removeAll(QStringLiteral("--everything")) > 0;
    if (args.size() != 2) {
        std::fprintf(stderr, "Usage: bld-convert [-p <parts dir>] <input> <output>\n"
                             "       bld-convert [-p <parts dir>] --compare <a> <b> [--no-links] "
                             "[--known-only] [--everything]\n");
        return 2;
    }
    parts::PartsLibrary lib;
    if (!partsDir.isEmpty()) {
        lib.addSearchPath(partsDir);
        lib.scan();
    }

    QString error;
    auto first = readMap(args[0], lib, &error);
    if (!first) { std::fprintf(stderr, "%s: %s\n", qPrintable(args[0]), qPrintable(error)); return 2; }
    if (compareMode) {
        auto second = readMap(args[1], lib, &error);
        if (!second) { std::fprintf(stderr, "%s: %s\n", qPrintable(args[1]), qPrintable(error)); return 2; }
        const int bricks = compare(*first, *second, links, knownOnly ? &lib : nullptr);
        const int rest = everything ? compareEverythingElse(*first, *second) : 0;
        if (everything && rest == 0) std::printf("same: text, rulers, areas, grid, groups and details\n");
        return bricks | rest;
    }
    // As the app does on opening a map.
    parts::placement::fixStaleAreas(*first, lib);
    edit::rebuildConnectivity(*first, lib);
    if (!writeMap(*first, args[1], lib, &error)) {
        std::fprintf(stderr, "%s: %s\n", qPrintable(args[1]), qPrintable(error));
        return 2;
    }
    return 0;
}
