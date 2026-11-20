#include "LayoutMerge.h"

#include "core/SharedModule.h"

#include <QtMath>

#include <optional>

#include <algorithm>

#include "WebModel.h"
#include "WebModelWriter.h"

#include "../core/Map.h"
#include "../saveload/SidecarIO.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QUuid>

#include <cmath>

namespace bld::sync::merge {

namespace {

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call Text::tr().
struct Text { Q_DECLARE_TR_FUNCTIONS(bld::sync::merge) };

const QStringList kItemLists{ QStringLiteral("bricks"), QStringLiteral("groups"), QStringLiteral("textCells"),
                              QStringLiteral("areas"), QStringLiteral("rulerItems") };
const QStringList kHeader{ QStringLiteral("author"), QStringLiteral("lug"), QStringLiteral("event"),
                           QStringLiteral("date"), QStringLiteral("comment"), QStringLiteral("backgroundColor") };

struct Item {
    QString kind, layerId;
    QJsonValue value;
    QString layerName;
};
using Items = QMap<QString, Item>;

QString rulerHash(QJsonObject ruler) {
    ruler.remove(QStringLiteral("id"));
    // QJsonObject keeps its keys sorted, so equal rulers give equal bytes.
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(ruler).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex()
            .left(16));
}

QString textKey(const QString& layer, const QJsonObject& cell, int index) {
    const QString id = cell.value(QLatin1String("id")).toString();
    return QStringLiteral("text:%1:%2").arg(layer, id.isEmpty() ? QStringLiteral("#%1").arg(index) : id);
}

Items itemsOf(const Snapshot& s) {
    Items out;
    const QJsonObject meta = s.doc.value(QLatin1String("meta")).toObject();
    QJsonObject header;
    for (const auto& k : kHeader) header.insert(k, meta.value(k));
    out.insert(QStringLiteral("map"), { QStringLiteral("map"), {}, header, {} });
    const QJsonObject data = s.doc.value(QLatin1String("layerData")).toObject();
    for (const auto& lv : s.doc.value(QLatin1String("layers")).toArray()) {
        const QString lid = lv.toString();
        QJsonObject props = data.value(lid).toObject();
        const QJsonObject layer = props;
        const QString name = layer.value(QLatin1String("name")).toString();
        for (const auto& k : kItemLists) props.remove(k);
        out.insert(QStringLiteral("layer:") + lid, { QStringLiteral("layer"), lid, props, name });
        for (const auto& g : layer.value(QLatin1String("groups")).toArray())
            out.insert(QStringLiteral("group:%1:%2").arg(lid, g.toObject().value(QLatin1String("id")).toString()),
                       { QStringLiteral("group"), lid, g, name });
        for (const auto& b : layer.value(QLatin1String("bricks")).toArray()) {
            QJsonObject brick = b.toObject();
            brick.remove(QStringLiteral("connexions"));  // links follow positions: noise
            out.insert(QStringLiteral("brick:%1:%2").arg(lid, brick.value(QLatin1String("id")).toString()),
                       { QStringLiteral("brick"), lid, brick, name });
        }
        const QJsonArray cells = layer.value(QLatin1String("textCells")).toArray();
        for (int i = 0; i < cells.size(); ++i)
            out.insert(textKey(lid, cells[i].toObject(), i), { QStringLiteral("text"), lid, cells[i], name });
        for (const auto& a : layer.value(QLatin1String("areas")).toArray()) {
            const QJsonObject cell = a.toObject();
            out.insert(QStringLiteral("area:%1:%2,%3")
                           .arg(lid)
                           .arg(cell.value(QLatin1String("x")).toInt())
                           .arg(cell.value(QLatin1String("y")).toInt()),
                       { QStringLiteral("area"), lid, cell.value(QLatin1String("color")), name });
        }
        for (const auto& r : layer.value(QLatin1String("rulerItems")).toArray()) {
            QJsonObject ruler = r.toObject();
            ruler.remove(QStringLiteral("id"));
            out.insert(QStringLiteral("ruler:%1:%2").arg(lid, rulerHash(ruler)), { QStringLiteral("ruler"), lid, ruler, name });
        }
    }
    for (const auto& l : s.sidecar.value(QLatin1String("anchoredLabels")).toArray())
        out.insert(QStringLiteral("label:") + l.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("label"), {}, l, {} });
    for (const auto& m : s.sidecar.value(QLatin1String("modules")).toArray())
        out.insert(QStringLiteral("module:") + m.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("module"), {}, m, {} });
    for (const auto& v : s.sidecar.value(QLatin1String("views")).toArray())
        out.insert(QStringLiteral("view:") + v.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("view"), {}, v, {} });
    for (const auto& p : s.sidecar.value(QLatin1String("sets")).toArray())
        out.insert(QStringLiteral("set:") + p.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("set"), {}, p, {} });
    // The budget, one row a part: limits set for different parts merge.
    const QJsonObject limits = s.sidecar.value(QLatin1String("budgetLimits")).toObject();
    for (auto it = limits.begin(); it != limits.end(); ++it)
        out.insert(QStringLiteral("budget:") + it.key(), { QStringLiteral("budget"), {}, it.value(), {} });
    // Named routes, one row a route (a route is taken whole).
    for (const auto& r : s.sidecar.value(QLatin1String("routes")).toArray())
        out.insert(QStringLiteral("route:") + r.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("route"), {}, r, {} });
    // Rolling stock and trains, one row each (a record is taken whole).
    for (const auto& r : s.sidecar.value(QLatin1String("rollingStock")).toArray())
        out.insert(QStringLiteral("stock:") + r.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("stock"), {}, r, {} });
    for (const auto& r : s.sidecar.value(QLatin1String("trains")).toArray())
        out.insert(QStringLiteral("train:") + r.toObject().value(QLatin1String("id")).toString(),
                   { QStringLiteral("train"), {}, r, {} });
    // Switch settings, one row a switch: flips of different switches merge.
    const QJsonObject switches = s.sidecar.value(QLatin1String("switchStates")).toObject();
    for (auto it = switches.begin(); it != switches.end(); ++it)
        out.insert(QStringLiteral("switch:") + it.key(), { QStringLiteral("switch"), {}, it.value(), {} });
    if (s.sidecar.contains(QLatin1String("venue")))
        out.insert(QStringLiteral("venue"), { QStringLiteral("venue"), {}, s.sidecar.value(QLatin1String("venue")), {} });
    if (s.sidecar.contains(QLatin1String("backgroundImage")))
        out.insert(QStringLiteral("background"),
                   { QStringLiteral("background"), {}, s.sidecar.value(QLatin1String("backgroundImage")), {} });
    return out;
}

Side sideChange(const Item* base, const Item* side) {
    if (!base && !side) return Side::Unchanged;
    if (!base) return Side::Added;
    if (!side) return Side::Deleted;
    return base->value == side->value ? Side::Unchanged : Side::Edited;
}

// A module both sides changed. Its parts merge: each part I added or took
// out offline is put on top of the server's list (as the live layout does,
// one key per part: ModuleMembers.h), and so does every other field only
// one side changed. `c.mineValue` becomes that merged module. Only a field
// both sides changed to different values is left as a clash (keeping mine
// then keeps my fields with the merged parts).
void mergeModuleChange(ItemChange& c) {
    const QJsonObject base = c.base.toObject(), mine = c.mineValue.toObject(), server = c.serverValue.toObject();
    QJsonObject merged = server;
    QJsonObject mineKept = mine;
    bool clash = false;
    QSet<QString> keys;
    for (const QJsonObject* o : { &base, &mine, &server })
        for (auto it = o->begin(); it != o->end(); ++it) keys.insert(it.key());
    const QString membersKey = QStringLiteral("members");
    for (const QString& k : keys) {
        if (k == membersKey) continue;
        const QJsonValue b = base.value(k), m = mine.value(k), s = server.value(k);
        if (m == b) {
            // Only the server changed it (or nobody): the server's stands on my side too.
            if (s.isUndefined()) mineKept.remove(k);
            else mineKept.insert(k, s);
            continue;
        }
        if (s == b) {
            if (m.isUndefined()) merged.remove(k);
            else merged.insert(k, m);
        } else if (s != m) {
            clash = true;
        }
    }
    const auto setOf = [&](const QJsonObject& o) {
        QSet<QString> out;
        for (const auto& v : o.value(membersKey).toArray()) out.insert(v.toString());
        return out;
    };
    const QSet<QString> b = setOf(base), m = setOf(mine);
    QSet<QString> parts = setOf(server);
    for (const QString& id : b)
        if (!m.contains(id)) parts.remove(id);
    for (const QString& id : m)
        if (!b.contains(id)) parts.insert(id);
    // In order (numbers by value), as a saved sidecar lists them.
    QStringList ids(parts.cbegin(), parts.cend());
    std::sort(ids.begin(), ids.end(),
              [](const QString& x, const QString& y) { return x.size() != y.size() ? x.size() < y.size() : x < y; });
    const QJsonArray list = QJsonArray::fromStringList(ids);
    if (clash) {
        mineKept.insert(membersKey, list);
        c.mineValue = mineKept;
        return;
    }
    merged.insert(membersKey, list);
    c.mineValue = merged;
    QJsonObject serverSorted = server;
    QStringList theirs;
    for (const auto& v : server.value(membersKey).toArray()) theirs << v.toString();
    std::sort(theirs.begin(), theirs.end(),
              [](const QString& x, const QString& y) { return x.size() != y.size() ? x.size() < y.size() : x < y; });
    serverSorted.insert(membersKey, QJsonArray::fromStringList(theirs));
    // Nothing of mine left to put in: the server's already says it.
    c.status = merged == serverSorted ? Status::Same : Status::Mine;
}

int indexOf(const QJsonArray& list, const QString& value) {
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toString() == value) return i;
    return -1;
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// The item list of a layer that holds `kind`.
QString listOf(const QString& kind) {
    if (kind == QLatin1String("brick")) return QStringLiteral("bricks");
    if (kind == QLatin1String("group")) return QStringLiteral("groups");
    if (kind == QLatin1String("text")) return QStringLiteral("textCells");
    if (kind == QLatin1String("area")) return QStringLiteral("areas");
    return QStringLiteral("rulerItems");
}

// The index in `list` of the item `key` names, or -1.
int findItem(const QJsonArray& list, const QString& kind, const QString& key) {
    const QString last = key.section(QLatin1Char(':'), 2);
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject o = list[i].toObject();
        if (kind == QLatin1String("area")) {
            if (QStringLiteral("%1,%2").arg(o.value(QLatin1String("x")).toInt()).arg(o.value(QLatin1String("y")).toInt()) == last)
                return i;
        } else if (kind == QLatin1String("ruler")) {
            if (rulerHash(o) == last) return i;
        } else if (kind == QLatin1String("text") && last.startsWith(QLatin1Char('#'))) {
            if (i == last.mid(1).toInt() && o.value(QLatin1String("id")).toString().isEmpty()) return i;
        } else if (o.value(QLatin1String("id")).toString() == last) {
            return i;
        }
    }
    return -1;
}

class Merger {
public:
    Merger(const Snapshot& mine, const Snapshot& server) : mine_(mine), doc_(server.doc), sidecar_(server.sidecar) {}

    void take(const ItemChange& c, Choice choice) {
        if (choice == Choice::Server) return;
        const bool copy = choice == Choice::Both && c.allowsBoth();
        if (c.kind == QLatin1String("map")) return takeHeader(c);
        if (c.kind == QLatin1String("layer")) return takeLayer(c);
        if (c.kind == QLatin1String("label")) return takeSidecarItem(c, QStringLiteral("anchoredLabels"), copy);
        if (c.kind == QLatin1String("module")) return takeSidecarItem(c, QStringLiteral("modules"), copy);
        // A view is replaced whole: the last edit of a view wins.
        if (c.kind == QLatin1String("view")) return takeSidecarItem(c, QStringLiteral("views"), copy);
        if (c.kind == QLatin1String("set")) return takeSidecarItem(c, QStringLiteral("sets"), false);
        if (c.kind == QLatin1String("route")) return takeSidecarItem(c, QStringLiteral("routes"), false);
        if (c.kind == QLatin1String("stock")) return takeSidecarItem(c, QStringLiteral("rollingStock"), false);
        if (c.kind == QLatin1String("train")) return takeSidecarItem(c, QStringLiteral("trains"), false);
        if (c.kind == QLatin1String("budget")) return takeBudgetRow(c);
        if (c.kind == QLatin1String("switch")) return takeSwitchRow(c);
        if (c.kind == QLatin1String("venue") || c.kind == QLatin1String("background")) {
            const QString k = c.kind == QLatin1String("venue") ? QStringLiteral("venue") : QStringLiteral("backgroundImage");
            if (c.mineValue.isUndefined()) sidecar_.remove(k);
            else sidecar_.insert(k, mine_.sidecar.value(k));
            return;
        }
        takeLayerItem(c, copy);
    }

    Snapshot result() const { return { doc_, sidecar_ }; }

private:
    QJsonObject data() const { return doc_.value(QLatin1String("layerData")).toObject(); }
    void setData(const QJsonObject& d) { doc_.insert(QStringLiteral("layerData"), d); }

    void takeHeader(const ItemChange& c) {
        QJsonObject meta = doc_.value(QLatin1String("meta")).toObject();
        const QJsonObject mine = c.mineValue.toObject();
        for (const auto& k : kHeader) meta.insert(k, mine.value(k));
        doc_.insert(QStringLiteral("meta"), meta);
    }

    // Makes sure the layer is there, taking mine's properties when it isn't.
    void ensureLayer(const QString& lid) {
        QJsonObject d = data();
        if (d.contains(lid)) return;
        QJsonObject layer = mine_.doc.value(QLatin1String("layerData")).toObject().value(lid).toObject();
        for (const auto& k : kItemLists)
            if (layer.contains(k)) layer.insert(k, QJsonArray{});
        d.insert(lid, layer);
        setData(d);
        // Where mine has it: after the nearest layer before it that the server has.
        QJsonArray order = doc_.value(QLatin1String("layers")).toArray();
        const QJsonArray mineOrder = mine_.doc.value(QLatin1String("layers")).toArray();
        int at = 0;
        for (int i = indexOf(mineOrder, lid) - 1; i >= 0; --i) {
            const int prev = indexOf(order, mineOrder[i].toString());
            if (prev >= 0) {
                at = prev + 1;
                break;
            }
        }
        order.insert(at, lid);
        doc_.insert(QStringLiteral("layers"), order);
    }

    void takeLayer(const ItemChange& c) {
        if (c.mineValue.isUndefined()) {
            QJsonObject d = data();
            d.remove(c.layerId);
            setData(d);
            QJsonArray order = doc_.value(QLatin1String("layers")).toArray();
            for (int i = order.size() - 1; i >= 0; --i)
                if (order[i].toString() == c.layerId) order.removeAt(i);
            doc_.insert(QStringLiteral("layers"), order);
            return;
        }
        ensureLayer(c.layerId);
        QJsonObject d = data();
        QJsonObject layer = d.value(c.layerId).toObject();
        const QJsonObject props = c.mineValue.toObject();
        for (auto it = props.begin(); it != props.end(); ++it) layer.insert(it.key(), it.value());
        d.insert(c.layerId, layer);
        setData(d);
    }

    // Mine's whole item (with what the compare leaves out, such as links).
    QJsonValue mineItem(const ItemChange& c) const {
        const QJsonArray list = mine_.doc.value(QLatin1String("layerData"))
                                    .toObject()
                                    .value(c.layerId)
                                    .toObject()
                                    .value(listOf(c.kind))
                                    .toArray();
        const int i = findItem(list, c.kind, c.key);
        return i >= 0 ? list[i] : QJsonValue();
    }

    void takeLayerItem(const ItemChange& c, bool copy) {
        const QString listName = listOf(c.kind);
        if (c.mineValue.isUndefined()) {
            QJsonObject d = data();
            QJsonObject layer = d.value(c.layerId).toObject();
            QJsonArray list = layer.value(listName).toArray();
            const int i = findItem(list, c.kind, c.key);
            if (i >= 0) list.removeAt(i);
            layer.insert(listName, list);
            if (d.contains(c.layerId)) d.insert(c.layerId, layer);
            setData(d);
            return;
        }
        ensureLayer(c.layerId);
        QJsonObject d = data();
        QJsonObject layer = d.value(c.layerId).toObject();
        QJsonArray list = layer.value(listName).toArray();
        QJsonObject item = c.mineWhole.isObject() ? c.mineWhole.toObject() : mineItem(c).toObject();
        const int i = findItem(list, c.kind, c.key);
        if (copy) {
            item.insert(QStringLiteral("id"), newId());
            item.insert(QStringLiteral("myGroup"), QString());
            if (item.contains(QLatin1String("connexions"))) {
                QJsonArray links;
                const qsizetype count = item.value(QLatin1String("connexions")).toArray().size();
                for (qsizetype link = 0; link < count; ++link)
                    links.append(QJsonObject{ { QStringLiteral("id"), newId() }, { QStringLiteral("linkedTo"), QString() } });
                item.insert(QStringLiteral("connexions"), links);
            }
            list.append(item);
        } else if (i >= 0) {
            list.replace(i, item);
        } else {
            list.append(item);
        }
        layer.insert(listName, list);
        d.insert(c.layerId, layer);
        setData(d);
    }

    void takeSidecarItem(const ItemChange& c, const QString& listName, bool copy) {
        QJsonArray list = sidecar_.value(listName).toArray();
        const QString id = c.key.section(QLatin1Char(':'), 1);
        int at = -1;
        for (int i = 0; i < list.size(); ++i)
            if (list[i].toObject().value(QLatin1String("id")).toString() == id) at = i;
        if (c.mineValue.isUndefined()) {
            if (at >= 0) list.removeAt(at);
        } else if (copy) {
            QJsonObject item = c.mineValue.toObject();
            item.insert(QStringLiteral("id"), newId());
            list.append(item);
        } else if (at >= 0) {
            list.replace(at, c.mineValue);
        } else {
            list.append(c.mineValue);
        }
        sidecar_.insert(listName, list);
    }

    // One switch's setting (none: its default).
    void takeSwitchRow(const ItemChange& c) {
        const QString key = QStringLiteral("switchStates");
        QJsonObject states = sidecar_.value(key).toObject();
        const QString sw = c.key.section(QLatin1Char(':'), 1);
        if (c.mineValue.isUndefined()) states.remove(sw);
        else states.insert(sw, c.mineValue);
        if (states.isEmpty()) sidecar_.remove(key);
        else sidecar_.insert(key, states);
    }

    // One part's limit. The budget itself comes with its first row, and
    // goes with its last one when mine has no budget any more.
    void takeBudgetRow(const ItemChange& c) {
        const QString key = QStringLiteral("budgetLimits");
        QJsonObject limits = sidecar_.value(key).toObject();
        const QString part = c.key.section(QLatin1Char(':'), 1);
        if (c.mineValue.isUndefined()) limits.remove(part);
        else limits.insert(part, c.mineValue);
        if (limits.isEmpty() && !mine_.sidecar.contains(key)) sidecar_.remove(key);
        else sidecar_.insert(key, limits);
    }

    const Snapshot& mine_;
    QJsonObject doc_;
    QJsonObject sidecar_;
};

// One whole sentence per case ("%1" is the item), so no language has to build
// "you … changed it" from translated pieces.
QString youDid(Side s) {
    switch (s) {
    case Side::Added: return Text::tr("%1: you added it");
    case Side::Edited: return Text::tr("%1: you changed it");
    case Side::Deleted: return Text::tr("%1: you deleted it");
    case Side::Unchanged: break;
    }
    return Text::tr("%1: you left it as it was");
}

QString serverDid(Side s) {
    switch (s) {
    case Side::Added: return Text::tr("%1: the server added it");
    case Side::Edited: return Text::tr("%1: the server changed it");
    case Side::Deleted: return Text::tr("%1: the server deleted it");
    case Side::Unchanged: break;
    }
    return Text::tr("%1: the server left it as it was");
}

QString bothDid(Side s) {
    switch (s) {
    case Side::Added: return Text::tr("%1: you and the server both added it");
    case Side::Edited: return Text::tr("%1: you and the server both changed it");
    case Side::Deleted: return Text::tr("%1: you and the server both deleted it");
    case Side::Unchanged: break;
    }
    return Text::tr("%1: you and the server both left it as it was");
}

// A clash: each side did something different.
QString eachDid(Side mine, Side server) {
    if (mine == Side::Unchanged) return serverDid(server);
    if (server == Side::Unchanged) return youDid(mine);
    switch (mine) {
    case Side::Added:
        if (server == Side::Added) return Text::tr("%1: you added it, the server added it");
        if (server == Side::Edited) return Text::tr("%1: you added it, the server changed it");
        return Text::tr("%1: you added it, the server deleted it");
    case Side::Edited:
        if (server == Side::Added) return Text::tr("%1: you changed it, the server added it");
        if (server == Side::Edited) return Text::tr("%1: you changed it, the server changed it");
        return Text::tr("%1: you changed it, the server deleted it");
    default:
        if (server == Side::Added) return Text::tr("%1: you deleted it, the server added it");
        if (server == Side::Edited) return Text::tr("%1: you deleted it, the server changed it");
        return Text::tr("%1: you deleted it, the server deleted it");
    }
}

}  // namespace

bool ItemChange::allowsBoth() const {
    static const QSet<QString> copyable{ QStringLiteral("brick"), QStringLiteral("text"), QStringLiteral("label"),
                                         QStringLiteral("module"), QStringLiteral("view") };
    return copyable.contains(kind) && !mineValue.isUndefined() && !serverValue.isUndefined();
}

Snapshot snapshotOf(const core::Map& map) {
    QJsonObject sidecar = saveload::sidecarToJson(map.sidecar);
    sidecar.remove(QStringLiteral("bbmHashSha256"));
    sidecar.remove(QStringLiteral("schemaVersion"));
    return { docJsonFromMap(map), sidecar };
}

std::unique_ptr<core::Map> mapOf(const Snapshot& snapshot, QString* error) {
    auto map = mapFromDocJson(snapshot.doc, error);
    if (map) saveload::sidecarFromJson(snapshot.sidecar, map->sidecar);
    return map;
}

QJsonObject toJson(const Snapshot& snapshot) {
    return { { QStringLiteral("doc"), snapshot.doc }, { QStringLiteral("sidecar"), snapshot.sidecar } };
}

Snapshot snapshotFromJson(const QJsonObject& o) {
    return { o.value(QLatin1String("doc")).toObject(), o.value(QLatin1String("sidecar")).toObject() };
}

namespace {

SharedMover& sharedMover() {
    static SharedMover mover;
    return mover;
}

// A part moved by its box (no picture geometry here): its box's middle goes
// where the placement takes it, and a quarter turn swaps the box's sides.
QJsonObject moveByBox(const QJsonObject& brick, const core::SharedPlacement& from, const core::SharedPlacement& to) {
    QJsonObject out = brick;
    QJsonObject a = brick.value(QLatin1String("displayArea")).toObject();
    double w = a.value(QLatin1String("width")).toDouble(), h = a.value(QLatin1String("height")).toDouble();
    const QPointF centre(a.value(QLatin1String("x")).toDouble() + w / 2, a.value(QLatin1String("y")).toDouble() + h / 2);
    const double turn = core::norm360(to.degrees - from.degrees);
    const double back = qDegreesToRadians(-from.degrees);
    const QPointF d = centre - QPointF(from.x, from.y);
    const QPointF own(d.x() * std::cos(back) - d.y() * std::sin(back), d.x() * std::sin(back) + d.y() * std::cos(back));
    const QPointF now = core::placePoint(to, own);
    if (std::abs(std::fmod(turn, 180.0) - 90.0) < 1e-6) std::swap(w, h);
    a.insert(QStringLiteral("x"), now.x() - w / 2);
    a.insert(QStringLiteral("y"), now.y() - h / 2);
    a.insert(QStringLiteral("width"), w);
    a.insert(QStringLiteral("height"), h);
    out.insert(QStringLiteral("displayArea"), a);
    if (turn != 0.0) out.insert(QStringLiteral("orientation"), core::norm360(brick.value(QLatin1String("orientation")).toDouble() + turn));
    return out;
}

QHash<QString, QJsonObject> modulesOf(const Snapshot& s) {
    QHash<QString, QJsonObject> out;
    for (const auto& v : s.sidecar.value(QLatin1String("modules")).toArray())
        out.insert(v.toObject().value(QLatin1String("id")).toString(), v.toObject());
    return out;
}

QSet<QString> membersOf(const QJsonObject& module) {
    QSet<QString> out;
    for (const auto& v : module.value(QLatin1String("members")).toArray()) out.insert(v.toString());
    return out;
}

bool sharedValue(const QJsonObject& module) {
    return module.value(QLatin1String("shared")).isObject() && !module.value(QLatin1String("libraryModuleId")).toString().isEmpty();
}

std::optional<core::SharedPlacement> placementIn(const QJsonObject& module) {
    return core::readPlacement(module.value(QLatin1String("shared")).toObject().value(QLatin1String("placement")));
}

// The rule for shared modules the server updated while I was offline (LayoutMerge.h).
void settleSharedModules(QList<ItemChange>& out, const Snapshot& base, const Snapshot& mine, const Snapshot& server) {
    const auto serverMods = modulesOf(server);
    if (std::none_of(serverMods.begin(), serverMods.end(), sharedValue)) return;
    const auto baseMods = modulesOf(base), mineMods = modulesOf(mine);
    const auto find = [&](const QString& key) {
        for (qsizetype i = 0; i < out.size(); ++i)
            if (out[i].key == key) return i;
        return qsizetype(-1);
    };
    for (auto sm = serverMods.begin(); sm != serverMods.end(); ++sm) {
        const QJsonObject& serverMod = sm.value();
        const auto bm = baseMods.constFind(sm.key());
        if (!sharedValue(serverMod) || bm == baseMods.constEnd()) continue;
        const QSet<QString> was = membersOf(*bm), now = membersOf(serverMod);
        if (was == now && bm->value(QLatin1String("libraryVersion")) == serverMod.value(QLatin1String("libraryVersion"))) continue;

        // Its old parts: the server took them out; whatever I did to them offline goes with them.
        QSet<QString> old = was;
        const auto mm = mineMods.constFind(sm.key());
        if (mm != mineMods.constEnd()) old.unite(membersOf(*mm));
        for (qsizetype i = out.size() - 1; i >= 0; --i) {
            ItemChange& c = out[i];
            if (c.kind != QLatin1String("brick") || c.mine == Side::Unchanged) continue;
            if (!old.contains(c.key.section(QLatin1Char(':'), 2)) || now.contains(c.key.section(QLatin1Char(':'), 2))) continue;
            if (c.server == Side::Unchanged && c.serverValue.isUndefined()) {
                out.removeAt(i);  // (a part only I had: it isn't the module's any more)
                continue;
            }
            c.mine = Side::Unchanged;
            c.mineValue = c.base;
            c.status = Status::Server;
            if (c.server == Side::Unchanged) out.removeAt(i);
        }

        // The module itself: the server's, and my offline move of it on top.
        const auto baseAt = placementIn(*bm), serverAt = placementIn(serverMod);
        const auto mineAt = mm != mineMods.constEnd() ? placementIn(*mm) : std::nullopt;
        bool movedByMe = false;
        core::SharedPlacement from, to;
        if (baseAt && serverAt && mineAt && !(mineAt->degrees == baseAt->degrees && mineAt->x == baseAt->x && mineAt->y == baseAt->y)) {
            movedByMe = true;
            from = *serverAt;
            const double turn = mineAt->degrees - baseAt->degrees;
            core::SharedPlacement mineOnly{ turn, mineAt->x, mineAt->y, std::nullopt };
            const QPointF xy = core::placePoint(mineOnly, QPointF(serverAt->x - baseAt->x, serverAt->y - baseAt->y));
            to = core::SharedPlacement{ core::norm360(serverAt->degrees + turn), xy.x(), xy.y(), serverAt->sheets };
        }
        const QString moduleKey = QStringLiteral("module:") + sm.key();
        qsizetype mi = find(moduleKey);
        if (!movedByMe) {
            if (mi >= 0) {
                ItemChange& c = out[mi];
                c.mine = Side::Unchanged;
                c.mineValue = c.base;
                c.status = Status::Server;
                if (c.server == Side::Unchanged) out.removeAt(mi);
            }
            continue;
        }
        QJsonObject moved = serverMod;
        QJsonObject shared = moved.value(QLatin1String("shared")).toObject();
        shared.insert(QStringLiteral("placement"), core::placementJson(to));
        moved.insert(QStringLiteral("shared"), shared);
        ItemChange mc;
        mc.key = moduleKey;
        mc.kind = QStringLiteral("module");
        mc.status = Status::Mine;
        mc.mine = Side::Edited;
        mc.server = Side::Unchanged;
        mc.base = serverMod;
        mc.serverValue = serverMod;
        mc.mineValue = moved;
        if (mi >= 0) out[mi] = mc;
        else out << mc;
        // Its new parts, moved the way I moved the module.
        const QJsonObject data = server.doc.value(QLatin1String("layerData")).toObject();
        for (auto layer = data.begin(); layer != data.end(); ++layer) {
            const QJsonObject l = layer.value().toObject();
            for (const auto& b : l.value(QLatin1String("bricks")).toArray()) {
                const QJsonObject brick = b.toObject();
                const QString id = brick.value(QLatin1String("id")).toString();
                if (!now.contains(id)) continue;
                const QJsonObject there = sharedMover() ? sharedMover()(brick, from, to) : moveByBox(brick, from, to);
                QJsonObject shown = there, serverShown = brick;
                shown.remove(QStringLiteral("connexions"));
                serverShown.remove(QStringLiteral("connexions"));
                ItemChange c;
                c.key = QStringLiteral("brick:%1:%2").arg(layer.key(), id);
                c.kind = QStringLiteral("brick");
                c.layerId = layer.key();
                c.layerName = l.value(QLatin1String("name")).toString();
                c.status = Status::Mine;
                c.mine = Side::Edited;
                c.server = Side::Unchanged;
                c.base = serverShown;
                c.serverValue = serverShown;
                c.mineValue = shown;
                c.mineWhole = there;
                const qsizetype at = find(c.key);
                if (at >= 0) out[at] = c;
                else out << c;
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const ItemChange& a, const ItemChange& b) { return a.key < b.key; });
}

}  // namespace

void setSharedMover(SharedMover mover) { sharedMover() = std::move(mover); }

QList<ItemChange> compareLayouts(const Snapshot& base, const Snapshot& mine, const Snapshot& server) {
    const Items b = itemsOf(base), m = itemsOf(mine), s = itemsOf(server);
    QSet<QString> keys;
    for (const auto* items : { &b, &m, &s })
        for (auto it = items->begin(); it != items->end(); ++it) keys.insert(it.key());
    QStringList sorted(keys.begin(), keys.end());
    sorted.sort();
    QList<ItemChange> out;
    for (const auto& key : sorted) {
        const auto find = [&](const Items& items) -> const Item* {
            const auto it = items.constFind(key);
            return it == items.constEnd() ? nullptr : &it.value();
        };
        const Item *bi = find(b), *mi = find(m), *si = find(s);
        ItemChange c;
        c.key = key;
        c.mine = sideChange(bi, mi);
        c.server = sideChange(bi, si);
        if (c.mine == Side::Unchanged && c.server == Side::Unchanged) continue;
        const bool same = (mi && si) ? mi->value == si->value : (!mi && !si);
        c.status = c.mine == Side::Unchanged     ? Status::Server
                 : c.server == Side::Unchanged ? Status::Mine
                 : same                        ? Status::Same
                                               : Status::Conflict;
        const Item* any = mi ? mi : si ? si : bi;
        c.kind = any->kind;
        c.layerId = any->layerId;
        c.layerName = any->layerName;
        if (bi) c.base = bi->value;
        if (mi) c.mineValue = mi->value;
        if (si) c.serverValue = si->value;
        if (c.kind == QLatin1String("module") && c.status == Status::Conflict && c.mine == Side::Edited
            && c.server == Side::Edited)
            mergeModuleChange(c);
        out << c;
    }
    settleSharedModules(out, base, mine, server);
    return out;
}

std::unique_ptr<core::Map> mergeLayouts(const Snapshot& mine, const Snapshot& server,
                                        const QList<ItemChange>& changes, const QHash<QString, Choice>& choices,
                                        QString* error) {
    Merger merger(mine, server);
    // Layers first, so items find the layer they go in.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& c : changes)
            if ((c.kind == QLatin1String("layer")) == (pass == 0)) merger.take(c, choices.value(c.key, c.defaultChoice()));
    return mapOf(merger.result(), error);
}

namespace {

// A stud coordinate, to a tenth at most: 120, 47.5.
QString studs(double v) { return QString::number(std::round(v * 10) / 10 + 0.0); }  // + 0.0: no "-0"

// " at (x, y)" for an item with a display area, else nothing.
QString at(const QJsonObject& v) {
    const QJsonObject area = v.value(QLatin1String("displayArea")).toObject();
    if (area.isEmpty()) return {};
    return Text::tr(" at (%1, %2)")
        .arg(studs(area.value(QLatin1String("x")).toDouble()), studs(area.value(QLatin1String("y")).toDouble()));
}

// " on "Layer"" for an item in a named layer, else nothing.
QString on(const ItemChange& c) { return c.layerName.isEmpty() ? QString() : Text::tr(" on \"%1\"").arg(c.layerName); }

}  // namespace

QString describe(const ItemChange& c) {
    const QJsonObject v = (c.mineValue.isUndefined() ? (c.serverValue.isUndefined() ? c.base : c.serverValue)
                                                     : c.mineValue)
                              .toObject();
    QString what;
    if (c.kind == QLatin1String("map")) what = Text::tr("Layout details");
    else if (c.kind == QLatin1String("layer")) what = Text::tr("Sheet \"%1\"").arg(v.value(QLatin1String("name")).toString());
    else if (c.kind == QLatin1String("brick"))
        what = Text::tr("Brick %1").arg(v.value(QLatin1String("partNumber")).toString()) + at(v) + on(c);
    else if (c.kind == QLatin1String("group")) {
        const QString part = v.value(QLatin1String("partNumber")).toString();
        what = (part.isEmpty() ? Text::tr("Group") : Text::tr("Group %1").arg(part)) + on(c);
    } else if (c.kind == QLatin1String("text"))
        what = Text::tr("Text \"%1\"").arg(v.value(QLatin1String("text")).toString().left(40)) + at(v) + on(c);
    else if (c.kind == QLatin1String("area")) what = Text::tr("Area cell %1").arg(c.key.section(QLatin1Char(':'), 2)) + on(c);
    else if (c.kind == QLatin1String("ruler")) what = Text::tr("Ruler") + on(c);
    else if (c.kind == QLatin1String("label")) what = Text::tr("Label \"%1\"").arg(v.value(QLatin1String("text")).toString().left(40));
    else if (c.kind == QLatin1String("module")) what = Text::tr("Module \"%1\"").arg(v.value(QLatin1String("name")).toString());
    else if (c.kind == QLatin1String("view")) what = Text::tr("View \"%1\"").arg(v.value(QLatin1String("name")).toString());
    else if (c.kind == QLatin1String("set")) what = Text::tr("Custom set");
    else if (c.kind == QLatin1String("budget")) {
        // The limit that side wants, so a clash reads without opening the budget.
        const auto limit = [](const QJsonValue& v) { return QString::number(v.toInt()); };
        const QString part = c.key.section(QLatin1Char(':'), 1);
        if (c.status == Status::Conflict && !c.mineValue.isUndefined() && !c.serverValue.isUndefined())
            what = Text::tr("Budget limit for %1 (yours: %2, the server's: %3)")
                       .arg(part, limit(c.mineValue), limit(c.serverValue));
        else what = Text::tr("Budget limit for %1").arg(part);
    } else if (c.kind == QLatin1String("switch")) what = Text::tr("A switch's setting");
    else if (c.kind == QLatin1String("route")) what = Text::tr("Route \"%1\"").arg(v.value(QLatin1String("name")).toString());
    else if (c.kind == QLatin1String("stock")) {
        const QString n = QStringList{ v.value(QLatin1String("number")).toString(), v.value(QLatin1String("name")).toString() }.join(QLatin1Char(' ')).trimmed();
        what = Text::tr("Car \"%1\"").arg(n);
    } else if (c.kind == QLatin1String("train")) what = Text::tr("Train \"%1\"").arg(v.value(QLatin1String("name")).toString());
    else if (c.kind == QLatin1String("venue")) what = Text::tr("Venue");
    else what = Text::tr("Background image");
    if (c.status == Status::Same) return bothDid(c.mine).arg(what);
    if (c.status == Status::Mine) return youDid(c.mine).arg(what);
    if (c.status == Status::Server) return serverDid(c.server).arg(what);
    return eachDid(c.mine, c.server).arg(what);
}

}  // namespace bld::sync::merge
