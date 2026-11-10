#include "WebModel.h"

#include "DocShape.h"

#include "SidecarItems.h"

#include "saveload/SidecarIO.h"

#include "core/DrawnLimits.h"
#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace bld::sync {

namespace {

using core::ColorSpec;

QString str(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toString(); }
double num(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toDouble(); }
float flt(const QJsonObject& o, const char* k) { return static_cast<float>(num(o, k)); }
// A whole number from the document. Any writer may put any number there:
// one an int can't hold (or not a number) is clamped, never cast as it is.
int integer(const QJsonObject& o, const char* k) {
    const double v = num(o, k);
    if (std::isnan(v)) return 0;
    return static_cast<int>(std::clamp(v, static_cast<double>(std::numeric_limits<int>::min()),
                                       static_cast<double>(std::numeric_limits<int>::max())));
}
// A whole number for a drawn setting: not a number, or infinite, gives
// `fallback` (core::keepDrawnSettingsInRange then keeps it in range).
// Not there gives `fallback` too (the server takes a layout without it).
int wholeOr(const QJsonObject& o, const char* k, int fallback) {
    return o.contains(QLatin1String(k)) && std::isfinite(num(o, k)) ? integer(o, k) : fallback;
}
// The same for a drawn setting that isn't a whole number.
float fltOr(const QJsonObject& o, const char* k, float fallback) {
    return o.contains(QLatin1String(k)) && std::isfinite(num(o, k)) ? flt(o, k) : fallback;
}
bool boolean(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toBool(); }
QJsonObject obj(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toObject(); }
QJsonArray arr(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toArray(); }

// { kind: 'known', name } | { kind: 'argb', argb: 'ffd3d3d3' }, resolved like
// the .bbm reader (saveload/XmlPrimitives.cpp readColor).
ColorSpec color(const QJsonObject& c) {
    if (str(c, "kind") == QLatin1String("known")) {
        const QString name = str(c, "name");
        const QColor resolved(name);
        return ColorSpec::fromKnown(resolved.isValid() ? resolved : QColor(Qt::black), name);
    }
    bool ok = false;
    const auto argb = static_cast<quint32>(str(c, "argb").toLongLong(&ok, 16));
    return ok ? ColorSpec::fromArgb(QColor::fromRgba(argb)) : ColorSpec{};
}

core::FontSpec font(const QJsonObject& f) {
    core::FontSpec out;
    out.familyName = str(f, "family");
    out.sizePt = fltOr(f, "size", core::limits::kFontSize.fallback);
    out.styleString = str(f, "style");
    return out;
}

QRectF rect(const QJsonObject& r) { return { num(r, "x"), num(r, "y"), num(r, "width"), num(r, "height") }; }
QPointF point(const QJsonObject& p) { return { num(p, "x"), num(p, "y") }; }

void item(core::LayerItem& it, const QJsonObject& o) {
    it.guid = str(o, "id");
    it.displayArea = rect(obj(o, "displayArea"));
    it.myGroupId = str(o, "myGroup");
}

std::vector<core::Group> groups(const QJsonObject& layer) {
    std::vector<core::Group> out;
    for (const auto& v : arr(layer, "groups")) {
        const QJsonObject g = v.toObject();
        core::Group group;
        group.guid = str(g, "id");
        group.partNumber = str(g, "partNumber");
        group.myGroupId = str(g, "myGroup");
        out.push_back(std::move(group));
    }
    return out;
}

void rulerBase(core::RulerItemBase& r, const QJsonObject& o) {
    item(r, o);
    r.color = color(obj(o, "color"));
    r.lineThickness = fltOr(o, "lineThickness", core::limits::kLineThickness.fallback);
    r.displayDistance = boolean(o, "displayDistance");
    r.displayUnit = boolean(o, "displayUnit");
    r.guidelineColor = color(obj(o, "guidelineColor"));
    r.guidelineThickness = fltOr(o, "guidelineThickness", core::limits::kGuidelineThickness.fallback);
    for (const auto& d : arr(o, "guidelineDashPattern")) r.guidelineDashPattern.push_back(static_cast<float>(d.toDouble()));
    r.unit = wholeOr(o, "unit", core::limits::kRulerUnit.fallback);
    r.measureFont = font(obj(o, "measureFont"));
    r.measureFontColor = color(obj(o, "measureFontColor"));
}

// Entries converted so far (MapReader::entriesConverted).
qint64 entryCount = 0;

core::Brick brickOf(const QJsonObject& b) {
    ++entryCount;
    core::Brick brick;
    item(brick, b);
    brick.partNumber = str(b, "partNumber");
    brick.orientation = flt(b, "orientation");
    brick.activeConnectionPointIndex = integer(b, "activeConnectionPointIndex");
    brick.altitude = flt(b, "altitude");
    for (const auto& c : arr(b, "connexions")) {
        const QJsonObject cp = c.toObject();
        brick.connections.push_back({ str(cp, "id"), str(cp, "linkedTo") });
    }
    return brick;
}

core::TextCell textCellOf(const QJsonObject& t) {
    ++entryCount;
    core::TextCell cell;
    item(cell, t);
    // The cell keeps the doc's id (minted by the web when the .bbm had
    // none): it's what edits are matched by. .bbm files never carry it.
    cell.text = str(t, "text");
    cell.orientation = flt(t, "orientation");
    cell.fontColor = color(obj(t, "fontColor"));
    cell.font = font(obj(t, "font"));
    const QString a = str(t, "textAlignment");
    cell.alignment = a == QLatin1String("Near")  ? core::TextAlignment::Near
                     : a == QLatin1String("Far") ? core::TextAlignment::Far
                                                 : core::TextAlignment::Center;
    return cell;
}

// Cells are kept relative to the sheet's offset (doc shape 2).
core::AreaCell areaCellOf(const QJsonObject& a, const QJsonObject& off) {
    ++entryCount;
    core::AreaCell cell;
    cell.x = integer(a, "x") + integer(off, "x");
    cell.y = integer(a, "y") + integer(off, "y");
    // The .bbm's hex ARGB text, kept as is (LayerIO.cpp).
    cell.color = QColor::fromRgba(static_cast<quint32>(str(a, "color").toLongLong(nullptr, 16)));
    return cell;
}

core::LayerRuler::AnyRuler rulerOf(const QJsonObject& r) {
    ++entryCount;
    core::LayerRuler::AnyRuler any;
    if (str(r, "kind") == QLatin1String("circular")) {
        any.kind = core::RulerKind::Circular;
        rulerBase(any.circular, r);
        any.circular.center = point(obj(r, "center"));
        any.circular.radius = flt(r, "radius");
        any.circular.attachedBrickId = str(r, "attachedBrickId");
    } else {
        any.kind = core::RulerKind::Linear;
        rulerBase(any.linear, r);
        any.linear.point1 = point(obj(r, "point1"));
        any.linear.point2 = point(obj(r, "point2"));
        any.linear.attachedBrick1Id = str(r, "attachedBrick1Id");
        any.linear.attachedBrick2Id = str(r, "attachedBrick2Id");
        any.linear.offsetDistance = flt(r, "offsetDistance");
        any.linear.allowOffset = boolean(r, "allowOffset");
    }
    return any;
}

const QString& rulerId(const core::LayerRuler::AnyRuler& r) {
    return r.kind == core::RulerKind::Circular ? r.circular.guid : r.linear.guid;
}

// A sheet's own fields, without its parts, text cells, areas or rulers.
std::unique_ptr<core::Layer> sheetOf(const QJsonObject& o, QString* error) {
    const QString type = str(o, "type");
    std::unique_ptr<core::Layer> out;
    if (type == QLatin1String("grid")) {
        auto g = std::make_unique<core::LayerGrid>();
        g->gridColor = color(obj(o, "gridColor"));
        g->gridThickness = fltOr(o, "gridThickness", core::limits::kGridThickness.fallback);
        g->subGridColor = color(obj(o, "subGridColor"));
        g->subGridThickness = fltOr(o, "subGridThickness", core::limits::kSubGridThickness.fallback);
        g->gridSizeInStud = wholeOr(o, "gridSizeInStud", core::limits::kGridSizeInStud.fallback);
        g->subDivisionNumber = wholeOr(o, "subDivisionNumber", core::limits::kSubDivisionNumber.fallback);
        g->displayGrid = boolean(o, "displayGrid");
        g->displaySubGrid = boolean(o, "displaySubGrid");
        g->displayCellIndex = boolean(o, "displayCellIndex");
        g->cellIndexFont = font(obj(o, "cellIndexFont"));
        g->cellIndexColor = color(obj(o, "cellIndexColor"));
        // Stored as the .bbm's integer text ("0" letters, "1" numbers).
        const auto indexType = [&o](const char* k, core::CellIndexType fallback) {
            bool ok = false;
            const int v = str(o, k).toInt(&ok);
            return ok ? static_cast<core::CellIndexType>(v) : fallback;
        };
        g->cellIndexColumnType = indexType("cellIndexColumnType", core::CellIndexType::Letters);
        g->cellIndexRowType = indexType("cellIndexRowType", core::CellIndexType::Numbers);
        const QJsonObject corner = obj(o, "cellIndexCorner");
        g->cellIndexCorner = QPoint(wholeOr(corner, "x", 0), wholeOr(corner, "y", 0));
        out = std::move(g);
    } else if (type == QLatin1String("brick")) {
        auto l = std::make_unique<core::LayerBrick>();
        l->displayBrickElevation = boolean(o, "displayBrickElevation");
        l->groups = groups(o);
        out = std::move(l);
    } else if (type == QLatin1String("text")) {
        auto l = std::make_unique<core::LayerText>();
        l->groups = groups(o);
        out = std::move(l);
    } else if (type == QLatin1String("area")) {
        auto l = std::make_unique<core::LayerArea>();
        l->areaCellSizeInStud = wholeOr(o, "areaCellSize", core::limits::kAreaCellSizeInStud.fallback);
        out = std::move(l);
    } else if (type == QLatin1String("ruler")) {
        auto l = std::make_unique<core::LayerRuler>();
        l->groups = groups(o);
        out = std::move(l);
    } else {
        if (error) *error = QStringLiteral("unknown layer type \"%1\"").arg(type);
        return nullptr;
    }
    out->guid = str(o, "id");
    out->name = str(o, "name");
    out->visible = boolean(o, "visible");
    out->transparency = wholeOr(o, "transparency", core::limits::kTransparency.fallback);
    const QJsonObject hull = obj(o, "hullProperties");
    out->hull.displayHulls = boolean(hull, "isVisible");
    out->hull.color = color(obj(hull, "hullColor"));
    out->hull.thickness = wholeOr(hull, "hullThickness", core::limits::kHullThickness.fallback);
    return out;
}

std::unique_ptr<core::Layer> layer(const QJsonObject& o, QString* error) {
    auto out = sheetOf(o, error);
    if (!out) return nullptr;
    switch (out->kind()) {
    case core::LayerKind::Brick: {
        auto& l = static_cast<core::LayerBrick&>(*out);
        // Drawn by z when the sheet has any (doc shape 2, DocShape.h).
        for (const auto& v : inZOrder(arr(o, "bricks"))) l.bricks.push_back(brickOf(v.toObject()));
        break;
    }
    case core::LayerKind::Text:
        for (const auto& v : arr(o, "textCells"))
            static_cast<core::LayerText&>(*out).textCells.push_back(textCellOf(v.toObject()));
        break;
    case core::LayerKind::Area: {
        const QJsonObject off = obj(o, "cellOffset");
        for (const auto& v : arr(o, "areas"))
            static_cast<core::LayerArea&>(*out).cells.push_back(areaCellOf(v.toObject(), off));
        break;
    }
    case core::LayerKind::Ruler: {
        auto& l = static_cast<core::LayerRuler&>(*out);
        // A ruler there twice (two people turned the sheet into Y.Maps at the
        // same moment) reads once; the web's repair removes the copy.
        QSet<QString> seenRulers;
        for (const auto& v : inZOrder(arr(o, "rulerItems"))) {
            auto any = rulerOf(v.toObject());
            const QString& rid = rulerId(any);
            if (!rid.isEmpty() && seenRulers.contains(rid)) continue;
            seenRulers.insert(rid);
            l.rulers.push_back(std::move(any));
        }
        break;
    }
    default: break;
    }
    return out;
}

// The document's header (meta) into `map`; false without one.
bool readHeader(const QJsonObject& meta, core::Map& map, QString* error) {
    if (!meta.contains(QLatin1String("version"))) {
        if (error) *error = QStringLiteral("the document has no layout header");
        return false;
    }
    map.dataVersion = integer(meta, "version");
    map.nbItems = integer(meta, "nbItems");
    map.backgroundColor = color(obj(meta, "backgroundColor"));
    map.author = str(meta, "author");
    map.lug = str(meta, "lug");
    map.event = str(meta, "event");
    const QJsonObject date = obj(meta, "date");
    map.date = QDate(integer(date, "year"), integer(date, "month"), integer(date, "day"));
    map.comment = str(meta, "comment");
    const QJsonObject ex = obj(meta, "exportInfo");
    map.exportInfo.exportPath = str(ex, "exportPath");
    map.exportInfo.fileTypeIndex = integer(ex, "exportFileType");
    map.exportInfo.area = rect(obj(ex, "exportArea"));
    map.exportInfo.scale = num(ex, "exportScale");
    map.exportInfo.watermark = boolean(ex, "exportWatermark");
    map.exportInfo.electricCircuit = boolean(ex, "exportElectricCircuit");
    map.exportInfo.connectionPoints = boolean(ex, "exportConnectionPoints");
    map.selectedLayerIndex = integer(meta, "selectedLayerIndex");
    return true;
}

// Anchored labels, modules and the venue: the web keeps the sidecar in
// meta.cache. Its modules and labels from sidecarItems when they go with
// that cache, so everyone's edits to different ones are kept (SidecarItems.h).
void readSidecar(const QJsonObject& meta, const QJsonObject& items, core::Sidecar& out) {
    saveload::sidecarFromJson(mergedSidecarCache(obj(meta, "cache"), items), out);
}

// The budget is the one sidecar field the web keeps outside meta.cache: a
// Y.Map at meta.budgetLimits (part number -> limit), so edits to different
// rows merge. Absent: no budget.
void readBudget(const QJsonObject& meta, core::Sidecar& out) {
    out.budgetLimits = saveload::budgetLimitsFromJson(meta.value(QLatin1String("budgetLimits")));
}

} // namespace

std::unique_ptr<core::Map> mapFromDocJson(const QJsonObject& doc, QString* error) {
    const QJsonObject meta = obj(doc, "meta");
    auto map = std::make_unique<core::Map>();
    if (!readHeader(meta, *map, error)) return nullptr;
    readSidecar(meta, obj(doc, "sidecarItems"), map->sidecar);
    readBudget(meta, map->sidecar);

    const QJsonObject data = obj(doc, "layerData");
    for (const auto& id : arr(doc, "layers")) {
        const QJsonValue l = data.value(id.toString());
        if (!l.isObject()) continue;  // the web skips ids without data too
        auto out = layer(l.toObject(), error);
        if (!out) return nullptr;
        map->layers().push_back(std::move(out));
    }
    // Drawn settings in range, as a .bbm's are (any writer may put any number there).
    core::keepDrawnSettingsInRange(*map);
    return map;
}

// ------------------------------------------------------------- MapReader

namespace {

template <typename T> struct Entry {
    QJsonValue json; // as it was converted
    T value;
    std::optional<double> z;
};

const QString& idOf(const core::LayerItem& it) {
    return it.guid;
}
const QString& idOf(const core::LayerRuler::AnyRuler& r) {
    return rulerId(r);
}
const QString& idOf(const core::AreaCell&) { // (area cells have none: followed by place)
    static const QString none;
    return none;
}

// One array of a sheet as last read: its entries in the array's order, and
// the order they're drawn in.
template <typename T> struct Kept {
    std::vector<Entry<T>> list;
    QList<std::optional<double>> zs;
    QList<qsizetype> order; // empty: as in the array
};

// The order the kept entries are drawn in: by z when any has one.
template <typename T> void reorder(Kept<T>& kept, bool withZ) {
    QList<std::optional<double>> zs;
    bool anyZ = false;
    if (withZ) {
        zs.reserve(static_cast<qsizetype>(kept.list.size()));
        for (const auto& e : kept.list) {
            zs.append(e.z);
            anyZ = anyZ || e.z.has_value();
        }
    }
    // Drawn by z when the sheet has any (doc shape 2, DocShape.h).
    if (!anyZ) {
        kept.zs.clear();
        kept.order.clear();
    } else if (kept.order.isEmpty() || zs != kept.zs) {
        kept.order = orderByZ(zs);
        kept.zs = std::move(zs);
    }
}

// The array as it is now, entry by entry against what was kept: an entry
// the same as a kept one is taken from it, any other converted with
// `conv`. Kept entries are looked for in place, and by id when entries
// came, went or moved (without ids, by place only).
template <typename T, typename Conv>
void follow(Kept<T>& kept, const QJsonArray& now, const Conv& conv, bool withIds, bool withZ) {
    static const QString kId = QStringLiteral("id");
    // The same entries in the same places, some of them changed (the usual
    // edit): those are converted again where they are.
    if (static_cast<size_t>(now.size()) == kept.list.size()) {
        std::vector<size_t> changed;
        bool inPlace = true;
        for (size_t i = 0; i < kept.list.size() && inPlace; ++i) {
            const QJsonValue v = now.at(static_cast<qsizetype>(i));
            const Entry<T>& e = kept.list[i];
            if (e.json == v) continue;
            if (withIds && v.toObject().value(kId).toString() != idOf(e.value)) inPlace = false;
            else changed.push_back(i);
        }
        if (inPlace) {
            bool zChanged = false;
            for (const size_t i : changed) {
                const QJsonValue v = now.at(static_cast<qsizetype>(i));
                Entry<T>& e = kept.list[i];
                const std::optional<double> z = withZ ? zOf(v) : std::nullopt;
                zChanged = zChanged || z != e.z;
                e = { v, conv(v.toObject()), z };
            }
            if (zChanged) reorder(kept, withZ);
            return;
        }
    }

    std::vector<Entry<T>> out;
    out.reserve(static_cast<size_t>(now.size()));
    std::vector<Entry<T>>& old = kept.list;
    // Where each kept entry went in `out` (an entry there twice is copied).
    constexpr size_t kNot = static_cast<size_t>(-1);
    std::vector<size_t> went(old.size(), kNot);
    const auto take = [&](size_t k) {
        if (went[k] != kNot) {
            out.push_back(out[went[k]]);
            return;
        }
        went[k] = out.size();
        out.push_back(std::move(old[k]));
    };
    // A kept entry, wherever it is now.
    const auto keptAt = [&](size_t k) -> const Entry<T>& { return went[k] == kNot ? old[k] : out[went[k]]; };
    const auto convert = [&](const QJsonValue& v) {
        out.push_back({ v, conv(v.toObject()), withZ ? zOf(v) : std::nullopt });
    };
    std::unordered_map<QString, size_t> byId;
    bool indexed = false;
    size_t j = 0;
    for (qsizetype i = 0; i < now.size(); ++i) {
        const QJsonValue v = now.at(i);
        if (j < old.size() && went[j] == kNot && old[j].json == v) {
            take(j++);
            continue;
        }
        if (!withIds) { // in place, changed
            ++j;
            convert(v);
            continue;
        }
        const QString id = v.toObject().value(kId).toString();
        if (j < old.size() && went[j] == kNot && idOf(old[j].value) == id) { // in place, changed
            ++j;
            convert(v);
            continue;
        }
        if (!indexed) {
            for (size_t k = old.size(); k-- > 0;) byId[idOf(keptAt(k).value)] = k; // (the first of an id)
            indexed = true;
        }
        if (const auto it = byId.find(id); it != byId.end() && keptAt(it->second).json == v) {
            j = it->second + 1;
            take(it->second);
            continue;
        }
        convert(v); // new, or moved and changed
    }
    kept.list = std::move(out);
    reorder(kept, withZ);
}

template <typename T, typename Each> void inOrder(const Kept<T>& kept, const Each& each) {
    if (kept.order.isEmpty()) {
        for (const auto& e : kept.list) each(e.value);
        return;
    }
    for (const qsizetype i : kept.order) each(kept.list[static_cast<size_t>(i)].value);
}

std::unique_ptr<core::Layer> copyOf(const core::Layer& l) {
    switch (l.kind()) {
    case core::LayerKind::Grid:
        return std::make_unique<core::LayerGrid>(static_cast<const core::LayerGrid&>(l));
    case core::LayerKind::Brick:
        return std::make_unique<core::LayerBrick>(static_cast<const core::LayerBrick&>(l));
    case core::LayerKind::Text:
        return std::make_unique<core::LayerText>(static_cast<const core::LayerText&>(l));
    case core::LayerKind::Area:
        return std::make_unique<core::LayerArea>(static_cast<const core::LayerArea&>(l));
    case core::LayerKind::Ruler:
        return std::make_unique<core::LayerRuler>(static_cast<const core::LayerRuler&>(l));
    default: return nullptr; // (sheetOf makes none other)
    }
}

} // namespace

struct MapReader::Impl {
    struct Sheet {
        QJsonObject json;                   // the sheet as last read
        std::unique_ptr<core::Layer> shell; // its own fields, no entries
        Kept<core::Brick> bricks;
        Kept<core::TextCell> textCells;
        Kept<core::AreaCell> areas;
        QJsonValue areaOffset; // what the areas were converted with
        Kept<core::LayerRuler::AnyRuler> rulers;
    };
    std::unordered_map<QString, Sheet> sheets;
    bool haveSidecar = false;
    QJsonValue cache, items; // what the sidecar was read from
    core::Sidecar sidecar;

    // Whether two versions of a sheet have the same own fields: all but its
    // entries, which follow() compares one by one. (Comparing two sheets
    // whole compares their entries in a slower way: on 5,000 parts it took
    // a millisecond to find the one that changed.)
    static bool sameOwnFields(const QJsonObject& was, const QJsonObject& now) {
        static const QStringList entries{ QStringLiteral("bricks"), QStringLiteral("textCells"),
                                          QStringLiteral("areas"), QStringLiteral("rulerItems") };
        if (was.size() != now.size()) return false;
        for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
            const auto at = was.constFind(it.key());
            if (at == was.constEnd()) return false;
            if (!entries.contains(it.key()) && at.value() != it.value()) return false;
        }
        return true;
    }

    // The sheet as it is now: its own fields converted again when they
    // changed, its entries followed one by one.
    static bool update(Sheet& s, const QJsonObject& o, QString* error) {
        if (!s.shell || !sameOwnFields(s.json, o)) {
            auto shell = sheetOf(o, error);
            if (!shell) return false;
            if (s.shell && s.shell->kind() != shell->kind()) s = Sheet();
            s.shell = std::move(shell);
        }
        s.json = o;
        switch (s.shell->kind()) {
        case core::LayerKind::Brick: follow(s.bricks, arr(o, "bricks"), brickOf, true, true); break;
        case core::LayerKind::Text: follow(s.textCells, arr(o, "textCells"), textCellOf, true, false); break;
        case core::LayerKind::Area: {
            const QJsonValue off = o.value(QLatin1String("cellOffset"));
            if (off != s.areaOffset) {
                s.areas = {};
                s.areaOffset = off;
            }
            const QJsonObject at = off.toObject();
            follow(
                s.areas, arr(o, "areas"), [&at](const QJsonObject& a) { return areaCellOf(a, at); }, false,
                false);
            break;
        }
        case core::LayerKind::Ruler: follow(s.rulers, arr(o, "rulerItems"), rulerOf, true, true); break;
        default: break;
        }
        return true;
    }

    static std::unique_ptr<core::Layer> layerOf(const Sheet& s) {
        auto out = copyOf(*s.shell);
        switch (out->kind()) {
        case core::LayerKind::Brick: {
            auto& to = static_cast<core::LayerBrick&>(*out).bricks;
            to.reserve(s.bricks.list.size());
            inOrder(s.bricks, [&to](const core::Brick& b) { to.push_back(b); });
            break;
        }
        case core::LayerKind::Text: {
            auto& to = static_cast<core::LayerText&>(*out).textCells;
            to.reserve(s.textCells.list.size());
            inOrder(s.textCells, [&to](const core::TextCell& t) { to.push_back(t); });
            break;
        }
        case core::LayerKind::Area: {
            auto& to = static_cast<core::LayerArea&>(*out).cells;
            to.reserve(s.areas.list.size());
            inOrder(s.areas, [&to](const core::AreaCell& a) { to.push_back(a); });
            break;
        }
        case core::LayerKind::Ruler: {
            auto& to = static_cast<core::LayerRuler&>(*out).rulers;
            // A ruler there twice reads once (as layer() does).
            QSet<QString> seen;
            inOrder(s.rulers, [&](const core::LayerRuler::AnyRuler& r) {
                const QString& rid = rulerId(r);
                if (!rid.isEmpty() && seen.contains(rid)) return;
                seen.insert(rid);
                to.push_back(r);
            });
            break;
        }
        default: break;
        }
        return out;
    }
};

MapReader::MapReader() : d_(std::make_unique<Impl>()) {
}
MapReader::~MapReader() = default;

void MapReader::clear() {
    *d_ = Impl();
}

qint64 MapReader::entriesConverted() {
    return entryCount;
}

std::unique_ptr<core::Map> MapReader::read(const QJsonObject& doc, QString* error) {
    Impl& d = *d_;
    const QJsonObject meta = obj(doc, "meta");
    auto map = std::make_unique<core::Map>();
    if (!readHeader(meta, *map, error)) return nullptr;
    const QJsonValue cache = meta.value(QLatin1String("cache")),
                     items = doc.value(QLatin1String("sidecarItems"));
    if (!d.haveSidecar || cache != d.cache || items != d.items) {
        d.sidecar = core::Sidecar();
        readSidecar(meta, items.toObject(), d.sidecar);
        d.cache = cache;
        d.items = items;
        d.haveSidecar = true;
    }
    map->sidecar = d.sidecar;
    readBudget(meta, map->sidecar);

    const QJsonObject data = obj(doc, "layerData");
    std::unordered_map<QString, Impl::Sheet> next;
    for (const auto& id : arr(doc, "layers")) {
        const QString key = id.toString();
        const QJsonValue l = data.value(key);
        if (!l.isObject()) continue; // the web skips ids without data too
        auto it = next.find(key);
        if (it == next.end()) { // (a sheet listed twice reads twice, as one)
            const auto kept = d.sheets.find(key);
            Impl::Sheet s;
            if (kept != d.sheets.end()) s = std::move(kept->second);
            it = next.emplace(key, std::move(s)).first;
        }
        Impl::Sheet& s = it->second;
        if (!Impl::update(s, l.toObject(), error)) {
            d.sheets.clear();
            return nullptr;
        }
        map->layers().push_back(Impl::layerOf(s));
    }
    d.sheets = std::move(next);
    // Drawn settings in range, as a .bbm's are (any writer may put any number there).
    core::keepDrawnSettingsInRange(*map);
    return map;
}

}  // namespace bld::sync
