#include "SidecarIO.h"
#include "VenueJson.h"

#include "../core/DrawnLimits.h"
#include "../core/Places.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <algorithm>

#include <cmath>

namespace bld::saveload {

namespace {

QJsonObject encodeColor(const core::ColorSpec& c) {
    QJsonObject o;
    o[QStringLiteral("known")] = c.isKnown();
    o[QStringLiteral("argb")]  = static_cast<qint64>(c.color.rgba());
    if (c.isKnown()) o[QStringLiteral("name")] = c.knownName;
    return o;
}

core::ColorSpec decodeColor(const QJsonObject& o) {
    core::ColorSpec c;
    c.color = QColor::fromRgba(static_cast<quint32>(o.value(QStringLiteral("argb")).toInteger()));
    if (o.value(QStringLiteral("known")).toBool()) c.knownName = o.value(QStringLiteral("name")).toString();
    return c;
}

QJsonObject encodeFont(const core::FontSpec& f) {
    QJsonObject o;
    o[QStringLiteral("family")] = f.familyName;
    o[QStringLiteral("size")]   = f.sizePt;
    o[QStringLiteral("style")]  = f.styleString;
    return o;
}

core::FontSpec decodeFont(const QJsonObject& o) {
    core::FontSpec f;
    f.familyName  = o.value(QStringLiteral("family")).toString(f.familyName);
    f.sizePt      = static_cast<float>(o.value(QStringLiteral("size")).toDouble(f.sizePt));
    f.styleString = o.value(QStringLiteral("style")).toString(f.styleString);
    return f;
}

QJsonObject encodePoint(QPointF p) {
    QJsonObject o;
    o[QStringLiteral("x")] = p.x();
    o[QStringLiteral("y")] = p.y();
    return o;
}

QPointF decodePoint(const QJsonObject& o) {
    return { o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble() };
}

const QStringList& knownLabelKeys() {
    static const QStringList keys{ QStringLiteral("id"),     QStringLiteral("text"),
                                   QStringLiteral("font"),   QStringLiteral("color"),
                                   QStringLiteral("kind"),   QStringLiteral("targetId"),
                                   QStringLiteral("offset"), QStringLiteral("rot"),
                                   QStringLiteral("minZoom") };
    return keys;
}

// The sidecar's own top-level fields; anything else is kept in `extras`.
const QStringList& knownRootKeys() {
    static const QStringList keys{ QStringLiteral("schemaVersion"),  QStringLiteral("bbmHashSha256"),
                                   QStringLiteral("anchoredLabels"), QStringLiteral("modules"),
                                   QStringLiteral("venue"),          QStringLiteral("backgroundImage"),
                                   QStringLiteral("views"),          QStringLiteral("sets"),
                                   QStringLiteral("budgetLimits"),   QStringLiteral("switchStates"),
                                   QStringLiteral("routes"),         QStringLiteral("rollingStock"),
                                   QStringLiteral("trains"),         QStringLiteral("trainScale"),
                                   QStringLiteral("layoutPlaces"),   QStringLiteral("waybills"),
                                   QStringLiteral("opsJobs") };
    return keys;
}

QJsonObject encodeAnchored(const core::AnchoredLabel& a) {
    QJsonObject o = a.extras;
    o[QStringLiteral("id")] = a.id;
    o[QStringLiteral("text")] = a.text;
    o[QStringLiteral("font")] = encodeFont(a.font);
    o[QStringLiteral("color")] = encodeColor(a.color);
    o[QStringLiteral("kind")] = static_cast<int>(a.kind);
    o[QStringLiteral("targetId")] = a.targetId;
    o[QStringLiteral("offset")] = encodePoint(a.offset);
    o[QStringLiteral("rot")] = a.offsetRotation;
    o[QStringLiteral("minZoom")] = a.minZoom;
    return o;
}

core::AnchoredLabel decodeAnchored(const QJsonObject& o) {
    core::AnchoredLabel a;
    a.id = o.value(QStringLiteral("id")).toString();
    a.text = o.value(QStringLiteral("text")).toString();
    a.font = decodeFont(o.value(QStringLiteral("font")).toObject());
    a.color = decodeColor(o.value(QStringLiteral("color")).toObject());
    a.kind = static_cast<core::AnchorKind>(o.value(QStringLiteral("kind")).toInt());
    a.targetId = o.value(QStringLiteral("targetId")).toString();
    a.offset = decodePoint(o.value(QStringLiteral("offset")).toObject());
    a.offsetRotation = static_cast<float>(o.value(QStringLiteral("rot")).toDouble());
    a.minZoom = o.value(QStringLiteral("minZoom")).toDouble();
    // Any writer may put any number there (a live layout's other people, a
    // file): a font size out of range would draw a label without end.
    core::keepLabelInRange(a);
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (!knownLabelKeys().contains(it.key())) a.extras.insert(it.key(), it.value());
    return a;
}

// The module fields this build reads; anything else is kept in `extras`.
const QStringList& knownModuleKeys() {
    static const QStringList keys{ QStringLiteral("id"),         QStringLiteral("name"),
                                   QStringLiteral("members"),    QStringLiteral("transform"),
                                   QStringLiteral("sourceFile"), QStringLiteral("importedAt"),
                                   QStringLiteral("showName"),   QStringLiteral("outlineColor"),
                                   QStringLiteral("nameColor"),  QStringLiteral("sameColor"),
                                   QStringLiteral("pinned"),     QStringLiteral("libraryModuleId"),
                                   QStringLiteral("libraryVersion"), QStringLiteral("shared"),
                                   QStringLiteral("table"),      QStringLiteral("power"),
                                   QStringLiteral("places") };
    return keys;
}

QJsonObject encodeModule(const core::Module& m) {
    QJsonObject o = m.extras;
    o[QStringLiteral("id")] = m.id;
    o[QStringLiteral("name")] = m.name;
    // In order (numbers by value), so saving the same layout twice gives
    // the same bytes: a set has no order of its own.
    QStringList ids(m.memberIds.cbegin(), m.memberIds.cend());
    std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    o[QStringLiteral("members")] = QJsonArray::fromStringList(ids);
    QJsonArray xform{ m.transform.m11(), m.transform.m12(), m.transform.m13(),
                      m.transform.m21(), m.transform.m22(), m.transform.m23(),
                      m.transform.m31(), m.transform.m32(), m.transform.m33() };
    o[QStringLiteral("transform")] = xform;
    if (!m.sourceFile.isEmpty()) o[QStringLiteral("sourceFile")] = m.sourceFile;
    if (m.importedAt.isValid()) {
        o[QStringLiteral("importedAt")] = m.importedAt.toString(Qt::ISODate);
    }
    // The look: only what differs from the default.
    if (!m.showName) o[QStringLiteral("showName")] = false;
    if (!m.outlineColor.isEmpty()) o[QStringLiteral("outlineColor")] = m.outlineColor;
    if (!m.nameColor.isEmpty()) o[QStringLiteral("nameColor")] = m.nameColor;
    if (!m.sameColor) o[QStringLiteral("sameColor")] = false;
    if (m.pinned) o[QStringLiteral("pinned")] = true;
    if (!m.libraryModuleId.isEmpty()) o[QStringLiteral("libraryModuleId")] = m.libraryModuleId;
    if (m.libraryVersion > 0) o[QStringLiteral("libraryVersion")] = m.libraryVersion;
    if (m.shared) o[QStringLiteral("shared")] = *m.shared;
    // Its table and power (core/ModuleSetup.h), when set.
    if (m.table) o[QStringLiteral("table")] = *m.table;
    if (m.power) o[QStringLiteral("power")] = *m.power;
    if (!m.places.isEmpty()) o[QStringLiteral("places")] = m.places;
    return o;
}

core::Module decodeModule(const QJsonObject& o) {
    core::Module m;
    m.id = o.value(QStringLiteral("id")).toString();
    m.name = o.value(QStringLiteral("name")).toString();
    for (const auto& v : o.value(QStringLiteral("members")).toArray()) {
        m.memberIds.insert(v.toString());
    }
    auto x = o.value(QStringLiteral("transform")).toArray();
    if (x.size() >= 9) {
        m.transform = QTransform(
            x[0].toDouble(), x[1].toDouble(), x[2].toDouble(),
            x[3].toDouble(), x[4].toDouble(), x[5].toDouble(),
            x[6].toDouble(), x[7].toDouble(), x[8].toDouble());
    }
    m.sourceFile = o.value(QStringLiteral("sourceFile")).toString();
    const QString at = o.value(QStringLiteral("importedAt")).toString();
    if (!at.isEmpty()) m.importedAt = QDateTime::fromString(at, Qt::ISODate);
    m.showName = o.value(QStringLiteral("showName")).toBool(true);
    m.outlineColor = o.value(QStringLiteral("outlineColor")).toString();
    m.nameColor = o.value(QStringLiteral("nameColor")).toString();
    m.sameColor = o.value(QStringLiteral("sameColor")).toBool(true);
    m.pinned = o.value(QStringLiteral("pinned")).toBool(false);
    m.libraryModuleId = o.value(QStringLiteral("libraryModuleId")).toString();
    m.libraryVersion = std::max(0, o.value(QStringLiteral("libraryVersion")).toInt(0));
    // Shared (read-only): an object, as the web reads it (packages/ydoc sharedShape.ts).
    if (const QJsonValue shared = o.value(QStringLiteral("shared")); shared.isObject()) m.shared = shared.toObject();
    else if (!shared.isUndefined()) m.extras.insert(QStringLiteral("shared"), shared);
    // Its table and power: objects; anything else is kept as it is.
    for (const QString& key : { QStringLiteral("table"), QStringLiteral("power") }) {
        const QJsonValue v = o.value(key);
        if (v.isObject()) (key == QLatin1String("table") ? m.table : m.power) = v.toObject();
        else if (!v.isUndefined()) m.extras.insert(key, v);
    }
    m.places = core::places::readPlaces(o.value(QStringLiteral("places")));
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (!knownModuleKeys().contains(it.key())) m.extras.insert(it.key(), it.value());
    return m;
}

// A placed set: {id, version, anchor: {brick, x, y, angle}}, as the web writes it.
QJsonObject encodePlacedSet(const core::PlacedSet& s) {
    QJsonObject o = s.extras;
    o[QStringLiteral("id")] = s.groupId;
    o[QStringLiteral("version")] = s.version;
    if (!s.anchorBrick.isEmpty())
        o[QStringLiteral("anchor")] = QJsonObject{ { QStringLiteral("brick"), s.anchorBrick },
                                                   { QStringLiteral("x"), s.anchorPos.x() },
                                                   { QStringLiteral("y"), s.anchorPos.y() },
                                                   { QStringLiteral("angle"), s.anchorAngle } };
    else o.remove(QStringLiteral("anchor"));
    return o;
}

std::optional<core::PlacedSet> decodePlacedSet(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.value(QStringLiteral("id")).isString() || !o.value(QStringLiteral("version")).isString())
        return std::nullopt;
    core::PlacedSet s;
    s.groupId = o.value(QStringLiteral("id")).toString();
    s.version = o.value(QStringLiteral("version")).toString();
    const QJsonObject a = o.value(QStringLiteral("anchor")).toObject();
    if (a.value(QStringLiteral("brick")).isString()) {
        s.anchorBrick = a.value(QStringLiteral("brick")).toString();
        s.anchorPos =
            QPointF(a.value(QStringLiteral("x")).toDouble(), a.value(QStringLiteral("y")).toDouble());
        s.anchorAngle = a.value(QStringLiteral("angle")).toDouble();
    }
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (it.key() != QLatin1String("id") && it.key() != QLatin1String("version")
            && it.key() != QLatin1String("anchor"))
            s.extras.insert(it.key(), it.value());
    return s;
}

// A view's area: {x, y, w, h}, or [x, y, w, h] as the web also accepts.
std::optional<QRectF> decodeViewRect(const QJsonValue& v) {
    double n[4];
    const auto num = [](const QJsonValue& x, double& out) {
        if (!x.isDouble() || !std::isfinite(x.toDouble())) return false;
        out = x.toDouble();
        return true;
    };
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() != 4) return std::nullopt;
        for (int i = 0; i < 4; ++i)
            if (!num(a[i], n[i])) return std::nullopt;
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        const char* keys[] = { "x", "y", "w", "h" };
        for (int i = 0; i < 4; ++i)
            if (!num(o.value(QLatin1String(keys[i])), n[i])) return std::nullopt;
    } else {
        return std::nullopt;
    }
    return QRectF(n[0], n[1], n[2], n[3]);
}

const QStringList& viewKeys() {
    static const QStringList keys{ QStringLiteral("id"),     QStringLiteral("name"),   QStringLiteral("fit"),
                                   QStringLiteral("rect"),   QStringLiteral("sheets"), QStringLiteral("grid"),
                                   QStringLiteral("labels") };
    return keys;
}
}

QJsonObject viewToJson(const core::SavedView& v) {
    QJsonObject o = v.extras;
    o[QStringLiteral("id")] = v.id;
    o[QStringLiteral("name")] = v.name;
    o[QStringLiteral("fit")] = v.fit;
    if (!v.fit && v.rect) {
        QJsonObject r;
        r[QStringLiteral("x")] = v.rect->x();
        r[QStringLiteral("y")] = v.rect->y();
        r[QStringLiteral("w")] = v.rect->width();
        r[QStringLiteral("h")] = v.rect->height();
        o[QStringLiteral("rect")] = r;
    } else {
        o[QStringLiteral("rect")] = QJsonValue::Null;
    }
    if (v.sheets) o[QStringLiteral("sheets")] = QJsonArray::fromStringList(*v.sheets);
    else o[QStringLiteral("sheets")] = QJsonValue::Null;
    o[QStringLiteral("grid")] = v.grid;
    o[QStringLiteral("labels")] = v.labels;
    return o;
}

std::optional<core::SavedView> viewFromJson(const QJsonValue& raw) {
    if (!raw.isObject()) return std::nullopt;
    const QJsonObject o = raw.toObject();
    const QString id = o.value(QLatin1String("id")).toString();
    if (id.isEmpty()) return std::nullopt;
    core::SavedView v;
    v.id = id;
    v.name = o.value(QLatin1String("name")).toString();
    v.rect = decodeViewRect(o.value(QLatin1String("rect")));
    // A view with no area can only be "fit"; one with an area is fit only when it says so.
    const QJsonValue fit = o.value(QLatin1String("fit"));
    v.fit = fit.isBool() ? (fit.toBool() || !v.rect) : !v.rect;
    if (v.fit) v.rect.reset();
    const QJsonValue sheets = o.value(QLatin1String("sheets"));
    if (sheets.isArray()) {
        QStringList list;
        for (const auto& s : sheets.toArray())
            if (s.isString()) list << s.toString();
        v.sheets = list;
    }
    const QJsonValue grid = o.value(QLatin1String("grid"));
    v.grid = grid.isBool() ? grid.toBool() : true;
    const QJsonValue labels = o.value(QLatin1String("labels"));
    v.labels = labels.isBool() ? labels.toBool() : true;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!viewKeys().contains(it.key())) v.extras.insert(it.key(), it.value());
    return v;
}

QString newerSidecarWarning() {
    return QCoreApplication::translate(
        "bld::saveload::SidecarIO", "The labels, modules and venue were saved by a newer version of the app. "
                                    "Anything this version doesn't know is kept as it is, but isn't shown.");
}

QString sidecarPathFor(const QString& bbmPath) {
    return bbmPath + QStringLiteral(".bld");
}

QByteArray sha256Hex(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

SidecarLoadResult readSidecar(const QString& cldPath, const QByteArray& bbmBytes,
                               core::Sidecar& out) {
    SidecarLoadResult r;
    QFile f(cldPath);
    if (!f.open(QIODevice::ReadOnly)) { r.error = f.errorString(); return r; }
    const QByteArray bytes = f.readAll();

    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = pe.errorString();
        return r;
    }
    sidecarFromJson(doc.object(), out);
    r.newerVersion = out.fromNewerVersion();

    if (!bbmBytes.isEmpty() && !out.bbmContentHashSha256.isEmpty()) {
        r.hashMismatch = (sha256Hex(bbmBytes) != out.bbmContentHashSha256);
    }
    r.ok = true;
    return r;
}

QJsonObject sidecarToJson(const core::Sidecar& sidecar) {
    QJsonObject root = sidecar.extras;
    // A newer build's file keeps its version: it is still that build's file.
    root[QStringLiteral("schemaVersion")] = std::max(sidecar.schemaVersion, core::Sidecar::kSchemaVersion);
    root[QStringLiteral("bbmHashSha256")] = QString::fromUtf8(sidecar.bbmContentHashSha256);

    QJsonArray labels;
    for (const auto& a : sidecar.anchoredLabels) labels.append(encodeAnchored(a));
    root[QStringLiteral("anchoredLabels")] = labels;

    QJsonArray modules;
    for (const auto& m : sidecar.modules) modules.append(encodeModule(m));
    root[QStringLiteral("modules")] = modules;

    if (sidecar.venue) { root[QStringLiteral("venue")] = venueToJson(*sidecar.venue); }

    if (!sidecar.backgroundImagePath.isEmpty()) {
        QJsonObject bg;
        bg[QStringLiteral("path")] = sidecar.backgroundImagePath;
        bg[QStringLiteral("opacity")] = sidecar.backgroundImageOpacity;
        if (!sidecar.backgroundImageRectStuds.isNull()) {
            const auto& r = sidecar.backgroundImageRectStuds;
            bg[QStringLiteral("rect")] = QJsonArray{ r.x(), r.y(), r.width(), r.height() };
        }
        root[QStringLiteral("backgroundImage")] = bg;
    }

    if (!sidecar.views.empty()) {
        QJsonArray views;
        for (const auto& v : sidecar.views) views.append(viewToJson(v));
        root[QStringLiteral("views")] = views;
    }
    if (!sidecar.sets.empty()) {
        QJsonArray sets;
        for (const auto& s : sidecar.sets) sets.append(encodePlacedSet(s));
        root[QStringLiteral("sets")] = sets;
    }
    if (sidecar.budgetLimits) root[QStringLiteral("budgetLimits")] = budgetLimitsToJson(*sidecar.budgetLimits);
    if (!sidecar.switchStates.isEmpty()) root[QStringLiteral("switchStates")] = switchStatesToJson(sidecar.switchStates);
    if (!sidecar.routes.empty()) {
        QJsonArray routes;
        for (const auto& r : sidecar.routes) routes.append(routeToJson(r));
        root[QStringLiteral("routes")] = routes;
    }
    if (!sidecar.rollingStock.empty()) {
        QJsonArray stock;
        for (const auto& r : sidecar.rollingStock) stock.append(rollingStockToJson(r));
        root[QStringLiteral("rollingStock")] = stock;
    }
    if (!sidecar.trains.empty()) {
        QJsonArray trains;
        for (const auto& t : sidecar.trains) trains.append(trainToJson(t));
        root[QStringLiteral("trains")] = trains;
    }
    if (sidecar.trainScale) root[QStringLiteral("trainScale")] = *sidecar.trainScale;
    if (!sidecar.layoutPlaces.isEmpty()) root[QStringLiteral("layoutPlaces")] = sidecar.layoutPlaces;
    if (!sidecar.waybills.isEmpty()) root[QStringLiteral("waybills")] = sidecar.waybills;
    if (!sidecar.opsJobs.isEmpty()) root[QStringLiteral("opsJobs")] = sidecar.opsJobs;
    return root;
}

namespace {

QJsonObject positionToJson(const core::TrackPosition& p) {
    return { { QStringLiteral("brick"), p.brick }, { QStringLiteral("from"), p.from },
             { QStringLiteral("to"), p.to }, { QStringLiteral("s"), p.s } };
}

core::TrackPosition positionFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return { o.value(QLatin1String("brick")).toString(), o.value(QLatin1String("from")).toInt(0),
             o.value(QLatin1String("to")).toInt(1), o.value(QLatin1String("s")).toDouble(0) };
}

}  // namespace

QJsonObject routeToJson(const core::Route& r) {
    QJsonObject o = r.extras;
    o[QStringLiteral("id")] = r.id;
    o[QStringLiteral("name")] = r.name;
    o[QStringLiteral("train")] = r.train.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(r.train);
    o[QStringLiteral("from")] = positionToJson(r.from);
    o[QStringLiteral("to")] = positionToJson(r.to);
    QJsonArray via;
    for (const auto& v : r.via)
        via.append(QJsonObject{ { QStringLiteral("at"), positionToJson(v.at) }, { QStringLiteral("reverse"), v.reverse } });
    o[QStringLiteral("via")] = via;
    QJsonArray steps;
    for (const auto& s : r.steps)
        steps.append(QJsonObject{ { QStringLiteral("brick"), s.brick }, { QStringLiteral("from"), s.from }, { QStringLiteral("to"), s.to } });
    o[QStringLiteral("steps")] = steps;
    o[QStringLiteral("color")] = r.color;
    return o;
}

std::optional<core::Route> routeFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    const QString id = o.value(QLatin1String("id")).toString();
    if (id.isEmpty()) return std::nullopt;
    core::Route r;
    r.id = id;
    r.name = o.value(QLatin1String("name")).toString();
    r.train = o.value(QLatin1String("train")).toString();
    r.from = positionFromJson(o.value(QLatin1String("from")));
    r.to = positionFromJson(o.value(QLatin1String("to")));
    for (const auto& x : o.value(QLatin1String("via")).toArray())
        r.via.push_back({ positionFromJson(x.toObject().value(QLatin1String("at"))), x.toObject().value(QLatin1String("reverse")).toBool() });
    for (const auto& x : o.value(QLatin1String("steps")).toArray()) {
        const QJsonObject s = x.toObject();
        r.steps.push_back({ s.value(QLatin1String("brick")).toString(), s.value(QLatin1String("from")).toInt(0), s.value(QLatin1String("to")).toInt(1) });
    }
    if (o.value(QLatin1String("color")).isString()) r.color = o.value(QLatin1String("color")).toString();
    r.extras = o;
    for (const char* k : { "id", "name", "train", "from", "to", "via", "steps", "color" }) r.extras.remove(QLatin1String(k));
    return r;
}

QJsonArray recordsFromJson(const QJsonValue& v) {
    std::map<QString, QJsonObject> byId;
    for (const auto& r : v.toArray()) {
        const QString id = r.toObject().value(QLatin1String("id")).toString();
        if (!id.isEmpty()) byId.emplace(id, r.toObject());
    }
    QJsonArray out;
    for (const auto& [id, r] : byId) out.append(r);
    return out;
}

QJsonObject rollingStockToJson(const core::RollingStock& r) {
    QJsonObject o = r.extras;
    o[QStringLiteral("id")] = r.id;
    o[QStringLiteral("number")] = r.number;
    o[QStringLiteral("name")] = r.name;
    o[QStringLiteral("kind")] = r.kind;
    o[QStringLiteral("type")] = r.type;
    o[QStringLiteral("trackKind")] = r.trackKind;
    o[QStringLiteral("length")] = r.length;
    o[QStringLiteral("width")] = r.width;
    o[QStringLiteral("bodyLength")] = r.bodyLength;
    QJsonArray trucks;
    for (const auto& t : r.trucks) trucks.append(QJsonObject{ { QStringLiteral("at"), t.at }, { QStringLiteral("swivelMax"), t.swivelMax } });
    o[QStringLiteral("trucks")] = trucks;
    const auto coupler = [](const core::Coupler& c) {
        return QJsonObject{ { QStringLiteral("type"), c.type }, { QStringLiteral("swivelMax"), c.swivelMax } };
    };
    o[QStringLiteral("couplers")] = QJsonObject{ { QStringLiteral("front"), coupler(r.front) }, { QStringLiteral("rear"), coupler(r.rear) } };
    o[QStringLiteral("powered")] = r.powered;
    QJsonObject look = r.lookExtras;
    look[QStringLiteral("mode")] = r.lookMode;
    look[QStringLiteral("color")] = r.color;
    if (!r.partKey.isEmpty()) look[QStringLiteral("partKey")] = r.partKey;
    o[QStringLiteral("look")] = look;
    // A roster link only when it has one (a web car without one has neither field).
    if (!r.libraryId.isEmpty()) o[QStringLiteral("libraryId")] = r.libraryId;
    if (r.libraryVersion > 0) o[QStringLiteral("libraryVersion")] = r.libraryVersion;
    o[QStringLiteral("notes")] = r.notes;
    if (r.opsCut) o[QStringLiteral("opsCut")] = *r.opsCut;
    if (!r.opsLoad.isEmpty()) o[QStringLiteral("opsLoad")] = r.opsLoad;
    if (r.opsWaybill) o[QStringLiteral("opsWaybill")] = *r.opsWaybill;
    if (r.opsHome) o[QStringLiteral("opsHome")] = *r.opsHome;
    if (r.opsAt) o[QStringLiteral("opsAt")] = *r.opsAt;
    if (r.opsHold) o[QStringLiteral("opsHold")] = *r.opsHold;
    return o;
}

std::optional<core::RollingStock> rollingStockFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    const QString id = o.value(QLatin1String("id")).toString();
    if (id.isEmpty()) return std::nullopt;
    const auto num = [&o](const char* k, double d) {
        const QJsonValue x = o.value(QLatin1String(k));
        return x.isDouble() && std::isfinite(x.toDouble()) ? x.toDouble() : d;
    };
    core::RollingStock r = core::simpleStock(id, num("length", 32.0));
    r.number = o.value(QLatin1String("number")).toString();
    r.name = o.value(QLatin1String("name")).toString();
    static const QStringList kinds = { QStringLiteral("engine"), QStringLiteral("car"), QStringLiteral("cab"), QStringLiteral("unit"), QStringLiteral("monorail") };
    const QString kind = o.value(QLatin1String("kind")).toString();
    if (kinds.contains(kind)) r.kind = kind;
    r.type = o.value(QLatin1String("type")).toString();
    if (o.value(QLatin1String("trackKind")).isString()) r.trackKind = o.value(QLatin1String("trackKind")).toString();
    r.width = num("width", r.width);
    r.bodyLength = num("bodyLength", r.length - 2.0);
    std::vector<core::Truck> trucks;
    for (const auto& t : o.value(QLatin1String("trucks")).toArray()) {
        const QJsonObject to = t.toObject();
        trucks.push_back({ to.value(QLatin1String("at")).toDouble(0.0), to.value(QLatin1String("swivelMax")).toDouble(30.0) });
    }
    if (trucks.size() >= 2) r.trucks = trucks;
    const QJsonObject couplers = o.value(QLatin1String("couplers")).toObject();
    const auto coupler = [&couplers](const char* end, core::Coupler& c) {
        const QJsonObject x = couplers.value(QLatin1String(end)).toObject();
        if (x.value(QLatin1String("type")).isString()) c.type = x.value(QLatin1String("type")).toString();
        c.swivelMax = x.value(QLatin1String("swivelMax")).toDouble(c.swivelMax);
    };
    coupler("front", r.front);
    coupler("rear", r.rear);
    r.powered = o.value(QLatin1String("powered")).toBool();
    QJsonObject look = o.value(QLatin1String("look")).toObject();
    if (look.value(QLatin1String("mode")).isString()) r.lookMode = look.value(QLatin1String("mode")).toString();
    if (look.value(QLatin1String("color")).isString()) r.color = look.value(QLatin1String("color")).toString();
    r.partKey = look.value(QLatin1String("partKey")).toString();
    for (const char* k : { "mode", "color", "partKey" }) look.remove(QLatin1String(k));
    r.lookExtras = look;
    r.libraryId = o.value(QLatin1String("libraryId")).toString();
    r.libraryVersion = o.value(QLatin1String("libraryVersion")).toInt(0);
    r.notes = o.value(QLatin1String("notes")).toString();
    if (o.value(QLatin1String("opsCut")).isString()) r.opsCut = o.value(QLatin1String("opsCut")).toString();
    r.opsLoad = o.value(QLatin1String("opsLoad")).toString();
    const auto ops = [&o](const char* k) {
        const QJsonValue field = o.value(QLatin1String(k));
        return field.isString() ? std::optional(field.toString()) : std::nullopt;
    };
    r.opsWaybill = ops("opsWaybill");
    r.opsHome = ops("opsHome");
    r.opsAt = ops("opsAt");
    if (o.value(QLatin1String("opsHold")).isBool()) r.opsHold = o.value(QLatin1String("opsHold")).toBool();
    r.extras = o;
    for (const char* k : { "id", "number", "name", "kind", "type", "trackKind", "length", "width", "bodyLength", "trucks", "couplers",
                           "powered", "look", "libraryId", "libraryVersion", "notes", "opsCut", "opsLoad", "opsWaybill", "opsHome", "opsAt", "opsHold" })
        r.extras.remove(QLatin1String(k));
    return r;
}

QJsonObject trainToJson(const core::Train& t) {
    QJsonObject o = t.extras;
    o[QStringLiteral("id")] = t.id;
    o[QStringLiteral("name")] = t.name;
    o[QStringLiteral("trackKind")] = t.trackKind;
    QJsonArray units;
    for (const auto& u : t.units) units.append(QJsonObject{ { QStringLiteral("stock"), u.stock }, { QStringLiteral("reversed"), u.reversed } });
    o[QStringLiteral("units")] = units;
    if (t.position) {
        QJsonObject p = positionToJson(*t.position);
        p[QStringLiteral("facing")] = t.position->facingTo ? QStringLiteral("to") : QStringLiteral("from");
        o[QStringLiteral("position")] = p;
    } else {
        o[QStringLiteral("position")] = QJsonValue(QJsonValue::Null);
    }
    o[QStringLiteral("color")] = t.color;
    o[QStringLiteral("speed")] = t.speed;
    if (t.cut) o[QStringLiteral("cut")] = true;
    if (t.over) o[QStringLiteral("over")] = switchStatesToJson(*t.over);
    if (t.emptiedAt) o[QStringLiteral("emptiedAt")] = *t.emptiedAt;
    return o;
}

std::optional<core::Train> trainFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    const QString id = o.value(QLatin1String("id")).toString();
    if (id.isEmpty()) return std::nullopt;
    core::Train t;
    t.id = id;
    t.name = o.value(QLatin1String("name")).toString();
    if (o.value(QLatin1String("trackKind")).isString()) t.trackKind = o.value(QLatin1String("trackKind")).toString();
    for (const auto& u : o.value(QLatin1String("units")).toArray()) {
        const QJsonObject uo = u.toObject();
        if (!uo.value(QLatin1String("stock")).isString()) continue;
        t.units.push_back({ uo.value(QLatin1String("stock")).toString(), uo.value(QLatin1String("reversed")).toBool() });
    }
    const QJsonValue pv = o.value(QLatin1String("position"));
    if (pv.isObject() && pv.toObject().value(QLatin1String("brick")).isString()) {
        core::TrainPosition p;
        static_cast<core::TrackPosition&>(p) = positionFromJson(pv);
        p.facingTo = pv.toObject().value(QLatin1String("facing")).toString() != QLatin1String("from");
        t.position = p;
    }
    if (o.value(QLatin1String("color")).isString()) t.color = o.value(QLatin1String("color")).toString();
    t.speed = o.value(QLatin1String("speed")).toDouble(40.0);
    t.cut = o.value(QLatin1String("cut")).toBool();
    if (o.value(QLatin1String("over")).isObject()) t.over = switchStatesFromJson(o.value(QLatin1String("over")));
    if (o.value(QLatin1String("emptiedAt")).isDouble()) t.emptiedAt = o.value(QLatin1String("emptiedAt")).toDouble();
    t.extras = o;
    for (const char* k : { "id", "name", "trackKind", "units", "position", "color", "speed", "cut", "over", "emptiedAt" }) t.extras.remove(QLatin1String(k));
    return t;
}

QJsonObject switchStatesToJson(const QMap<QString, int>& states) {
    QJsonObject out;
    for (auto it = states.constBegin(); it != states.constEnd(); ++it)
        if (it.value() >= 0) out.insert(it.key(), it.value());
    return out;
}

QMap<QString, int> switchStatesFromJson(const QJsonValue& v) {
    QMap<QString, int> out;
    static const QRegularExpression keyRe(QStringLiteral("^.+/\\d+$"));
    const QJsonObject o = v.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (!it.value().isDouble() || !keyRe.match(it.key()).hasMatch()) continue;
        const double d = it.value().toDouble();
        if (d < 0 || d != std::floor(d) || d > 1e6) continue;
        out.insert(it.key(), static_cast<int>(d));
    }
    return out;
}

QJsonObject budgetLimitsToJson(const QMap<QString, int>& limits) {
    QJsonObject out;
    for (auto it = limits.constBegin(); it != limits.constEnd(); ++it)
        if (it.value() >= 0) out.insert(it.key(), it.value());
    return out;
}

std::optional<QMap<QString, int>> budgetLimitsFromJson(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    QMap<QString, int> out;
    const QJsonObject o = v.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (!it.value().isDouble()) continue;
        const double d = it.value().toDouble();
        if (d < 0 || !std::isfinite(d)) continue;
        out.insert(it.key().toUpper(), static_cast<int>(d));
    }
    return out;
}

std::optional<int> trainScaleFromJson(const QJsonValue& v) {
    if (!v.isDouble()) return std::nullopt;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d != std::floor(d) || d < 10 || d > 220) return std::nullopt;
    return static_cast<int>(d);
}

void sidecarFromJson(const QJsonObject& root, core::Sidecar& out) {
    out.budgetLimits = budgetLimitsFromJson(root.value(QStringLiteral("budgetLimits")));
    out.trainScale = trainScaleFromJson(root.value(QStringLiteral("trainScale")));
    out.switchStates = switchStatesFromJson(root.value(QStringLiteral("switchStates")));
    out.routes.clear();
    for (const auto& v : root.value(QStringLiteral("routes")).toArray())
        if (auto r = routeFromJson(v)) out.routes.push_back(std::move(*r));
    out.rollingStock.clear();
    for (const auto& v : root.value(QStringLiteral("rollingStock")).toArray())
        if (auto r = rollingStockFromJson(v)) out.rollingStock.push_back(std::move(*r));
    out.trains.clear();
    for (const auto& v : root.value(QStringLiteral("trains")).toArray())
        if (auto t = trainFromJson(v)) out.trains.push_back(std::move(*t));
    out.layoutPlaces = core::places::readPlaces(root.value(QStringLiteral("layoutPlaces")));
    out.waybills = recordsFromJson(root.value(QStringLiteral("waybills")));
    out.opsJobs = recordsFromJson(root.value(QStringLiteral("opsJobs")));
    out.sets.clear();
    for (const auto& v : root.value(QStringLiteral("sets")).toArray())
        if (auto s = decodePlacedSet(v)) out.sets.push_back(std::move(*s));
    out.views.clear();
    for (const auto& v : root.value(QStringLiteral("views")).toArray())
        if (auto view = viewFromJson(v)) out.views.push_back(std::move(*view));

    out.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(core::Sidecar::kSchemaVersion);
    out.extras = QJsonObject();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        if (!knownRootKeys().contains(it.key())) out.extras.insert(it.key(), it.value());
    out.bbmContentHashSha256 = root.value(QStringLiteral("bbmHashSha256")).toString().toUtf8();

    out.anchoredLabels.clear();
    for (const auto& v : root.value(QStringLiteral("anchoredLabels")).toArray()) {
        out.anchoredLabels.push_back(decodeAnchored(v.toObject()));
    }
    out.modules.clear();
    for (const auto& v : root.value(QStringLiteral("modules")).toArray()) {
        out.modules.push_back(decodeModule(v.toObject()));
    }
    out.venue.reset();
    if (root.contains(QStringLiteral("venue"))) {
        out.venue = venueFromJson(root.value(QStringLiteral("venue")).toObject());
    }

    out.backgroundImagePath.clear();
    out.backgroundImageRectStuds = QRectF();
    out.backgroundImageOpacity = 0.5;
    if (root.contains(QStringLiteral("backgroundImage"))) {
        const auto bg = root.value(QStringLiteral("backgroundImage")).toObject();
        out.backgroundImagePath = bg.value(QStringLiteral("path")).toString();
        out.backgroundImageOpacity = bg.value(QStringLiteral("opacity")).toDouble(0.5);
        if (bg.contains(QStringLiteral("rect"))) {
            const auto r = bg.value(QStringLiteral("rect")).toArray();
            if (r.size() == 4) {
                out.backgroundImageRectStuds = QRectF(
                    r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
            }
        }
    }
}

void readModulePlaces(const QString& bbmPath, core::Map& module) {
    core::Sidecar had;
    if (!QFile::exists(sidecarPathFor(bbmPath)) || !readSidecar(sidecarPathFor(bbmPath), {}, had).ok) return;
    QJsonArray all = had.layoutPlaces;
    for (const auto& m : had.modules)
        for (const auto& p : m.places) all.append(p);
    module.sidecar.layoutPlaces = core::places::readPlaces(all);
}

bool writeModulePlaces(const QString& bbmPath, const core::Map& module, QString* error) {
    const QString path = sidecarPathFor(bbmPath);
    core::Sidecar out;
    const bool there = QFile::exists(path);
    if (!there && module.sidecar.layoutPlaces.isEmpty()) return true;
    if (there) readSidecar(path, {}, out);
    out.layoutPlaces = module.sidecar.layoutPlaces;
    // (Modules of a layout kept as a module file: their parts are other ones now.)
    for (auto& m : out.modules) m.places = QJsonArray();
    QFile f(bbmPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    return writeSidecar(path, f.readAll(), out, error);
}

void renameSidecarIds(core::Sidecar& sidecar, const QHash<QString, QString>& renamed) {
    if (renamed.isEmpty()) return;
    const auto rename = [&](QString& id) {
        const auto it = renamed.constFind(id);
        if (it != renamed.constEnd()) id = it.value();
    };
    for (auto& v : sidecar.views)
        if (v.sheets)
            for (QString& id : *v.sheets) rename(id);
    for (auto& l : sidecar.anchoredLabels) rename(l.targetId);
    for (auto& m : sidecar.modules) {
        QSet<QString> members;
        for (QString id : m.memberIds) {
            rename(id);
            members.insert(id);
        }
        m.memberIds = members;
        m.places = core::places::renamed(m.places, [&](QString id) { rename(id); return std::optional(id); });
    }
    // Places name the bricks their spots are on.
    sidecar.layoutPlaces = core::places::renamed(sidecar.layoutPlaces, [&](QString id) { rename(id); return std::optional(id); });
    // Routes name the bricks they run over.
    for (auto& r : sidecar.routes) {
        rename(r.from.brick);
        rename(r.to.brick);
        for (auto& v : r.via) rename(v.at.brick);
        for (auto& s : r.steps) rename(s.brick);
    }
    // Switch settings name their brick: "<brick>/<heel>".
    const auto renameSwitches = [&](QMap<QString, int>& of) {
        QMap<QString, int> states;
        for (auto it = of.constBegin(); it != of.constEnd(); ++it) {
            const qsizetype at = it.key().lastIndexOf(QLatin1Char('/'));
            QString brick = it.key().left(at);
            rename(brick);
            states.insert(brick + it.key().mid(at), it.value());
        }
        of = states;
    };
    renameSwitches(sidecar.switchStates);
    // A train on the track names the brick its head is on, and the switches it stands over.
    for (auto& t : sidecar.trains) {
        if (t.position) rename(t.position->brick);
        if (t.over) renameSwitches(*t.over);
    }
}

bool writeSidecar(const QString& cldPath, const QByteArray& bbmBytes,
                  const core::Sidecar& sidecar, QString* error) {
    QJsonObject root = sidecarToJson(sidecar);
    root[QStringLiteral("bbmHashSha256")] = QString::fromUtf8(sha256Hex(bbmBytes));

    QSaveFile f(cldPath);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

}
