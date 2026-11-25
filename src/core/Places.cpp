#include "Places.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <cmath>

namespace bld::core::places {

namespace {

QJsonValue get(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)); }

bool isControl(QChar c) { return c.unicode() < 0x20; }

// Without control characters, at most `max` long (never half a character); notes keep their new lines.
QString clip(const QString& v, int max, bool notes = false) {
    QString clean;
    clean.reserve(v.size());
    for (const QChar c : v)
        if (!isControl(c) || (notes && c == QLatin1Char('\n'))) clean.append(c);
    if (clean.size() <= max) return clean;
    return clean.left(clean.at(max - 1).isHighSurrogate() ? max - 1 : max);
}

bool goodText(const QJsonValue& v, int max) {
    const QString s = v.toString();
    return v.isString() && s.size() <= max && std::none_of(s.begin(), s.end(), isControl);
}
bool goodId(const QJsonValue& v, int max) { return goodText(v, max) && !v.toString().isEmpty(); }

bool wholeIn(const QJsonValue& v, int lo, int hi) {
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    return d == std::floor(d) && d >= lo && d <= hi;
}

bool goodTrack(const QJsonValue& v) {
    if (!v.isObject()) return false;
    const QJsonObject o = v.toObject();
    return goodId(get(o, "brick"), kMaxBrick) && wholeIn(get(o, "from"), 0, kMaxConn) && wholeIn(get(o, "to"), 0, kMaxConn)
           && get(o, "from").toInt() != get(o, "to").toInt();
}

bool goodWords(const QJsonValue& v) {
    if (!v.isArray()) return false;
    const QJsonArray a = v.toArray();
    return a.size() <= kMaxWords && std::all_of(a.begin(), a.end(), [](const QJsonValue& x) { return goodId(x, kMaxWord); });
}

QJsonArray readWords(const QJsonValue& v) {
    QJsonArray out;
    const QJsonArray words = v.toArray();
    for (const auto& x : words) {
        if (!x.isString() || out.size() >= kMaxWords) continue;
        const QString word = clip(x.toString(), kMaxWord);
        if (!word.isEmpty()) out.append(word);
    }
    return out;
}

std::optional<QJsonObject> readSpot(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    QJsonObject o = v.toObject();
    if (!goodId(get(o, "id"), kMaxId)) return std::nullopt;
    o.insert(QStringLiteral("name"), clip(get(o, "name").toString(), kMaxName));
    if (const QJsonValue track = get(o, "track"); goodTrack(track)) {
        const QJsonObject t = track.toObject();
        o.insert(QStringLiteral("track"), QJsonObject{ { QStringLiteral("brick"), get(t, "brick") },
                                                       { QStringLiteral("from"), get(t, "from") },
                                                       { QStringLiteral("to"), get(t, "to") } });
    } else {
        o.remove(QStringLiteral("track"));
    }
    if (!wholeIn(get(o, "capacity"), 1, kMaxCapacity)) o.remove(QStringLiteral("capacity"));
    const QString role = clip(get(o, "role").toString(), kMaxKind);
    if (role.isEmpty()) o.remove(QStringLiteral("role"));
    else o.insert(QStringLiteral("role"), role);
    return o;
}

QString idOf(const QJsonValue& place) { return get(place.toObject(), "id").toString(); }
QString sourceOf(const QJsonObject& place) {
    const QString s = get(place, "source").toString();
    return s.isEmpty() ? get(place, "id").toString() : s;
}

}  // namespace

QStringList kinds() {
    return { QStringLiteral("industry"), QStringLiteral("yard"), QStringLiteral("staging"), QStringLiteral("interchange") };
}
QStringList yardRoles() {
    return { QStringLiteral("arrival"), QStringLiteral("departure"), QStringLiteral("class"), QStringLiteral("rip"), QStringLiteral("engine") };
}
QStringList carTypes() {
    return { QStringLiteral("boxcar"), QStringLiteral("hopper"),   QStringLiteral("tank"),      QStringLiteral("flat"),   QStringLiteral("gondola"),
             QStringLiteral("reefer"), QStringLiteral("autorack"), QStringLiteral("passenger"), QStringLiteral("caboose") };
}

int placeSize(const QJsonValue& v) {
    if (v.isString()) return static_cast<int>(v.toString().size()) + 2;
    if (v.isArray()) {
        int n = 2;
        const QJsonArray a = v.toArray();
        for (const auto& x : a) n += placeSize(x) + 1;
        return n;
    }
    if (v.isObject()) {
        int n = 2;
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) n += static_cast<int>(it.key().size()) + 3 + placeSize(it.value());
        return n;
    }
    return 8;
}

bool validPlace(const QJsonValue& v) {
    if (!v.isObject()) return false;
    const QJsonObject o = v.toObject();
    if (!goodId(get(o, "id"), kMaxId) || !goodText(get(o, "name"), kMaxName) || !goodText(get(o, "kind"), kMaxKind)) return false;
    if (!goodWords(get(o, "accepts")) || !goodWords(get(o, "ships")) || !goodWords(get(o, "tags"))) return false;
    const QJsonValue notes = get(o, "notes");
    if (!notes.isString() || notes.toString().size() > kMaxNotes || clip(notes.toString(), kMaxNotes, true) != notes.toString()) return false;
    if (o.contains(QLatin1String("source")) && !goodId(get(o, "source"), kMaxId)) return false;
    if (!get(o, "spots").isArray()) return false;
    const QJsonArray spots = get(o, "spots").toArray();
    if (spots.size() > kMaxSpots) return false;
    for (const auto& sv : spots) {
        if (!sv.isObject()) return false;
        const QJsonObject s = sv.toObject();
        if (!goodId(get(s, "id"), kMaxId) || !goodText(get(s, "name"), kMaxName)) return false;
        if (s.contains(QLatin1String("track")) && !goodTrack(get(s, "track"))) return false;
        if (s.contains(QLatin1String("capacity")) && !wholeIn(get(s, "capacity"), 1, kMaxCapacity)) return false;
        if (s.contains(QLatin1String("role")) && !goodText(get(s, "role"), kMaxKind)) return false;
    }
    return placeSize(v) <= kMaxSize;
}

bool validPlaces(const QJsonValue& v) {
    if (!v.isArray()) return false;
    const QJsonArray a = v.toArray();
    if (a.size() > kMaxPlaces) return false;
    QSet<QString> seen;
    for (const auto& p : a) {
        if (!validPlace(p)) return false;
        seen.insert(idOf(p));
    }
    return seen.size() == a.size();
}

std::optional<QJsonObject> readPlace(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    const QJsonObject o = v.toObject();
    if (!goodId(get(o, "id"), kMaxId)) return std::nullopt;
    QJsonArray spots;
    QSet<QString> seen;
    const QJsonArray stored = get(o, "spots").toArray();
    for (const auto& s : stored) {
        const auto spot = readSpot(s);
        if (!spot || spots.size() >= kMaxSpots || seen.contains(idOf(*spot))) continue;
        seen.insert(idOf(*spot));
        spots.append(*spot);
    }
    const QString kind = clip(get(o, "kind").toString(), kMaxKind);
    QJsonObject known{ { QStringLiteral("id"), get(o, "id") },
                       { QStringLiteral("name"), clip(get(o, "name").toString(), kMaxName) },
                       { QStringLiteral("kind"), kind.isEmpty() ? QStringLiteral("industry") : kind },
                       { QStringLiteral("spots"), spots },
                       { QStringLiteral("accepts"), readWords(get(o, "accepts")) },
                       { QStringLiteral("ships"), readWords(get(o, "ships")) },
                       { QStringLiteral("tags"), readWords(get(o, "tags")) },
                       { QStringLiteral("notes"), clip(get(o, "notes").toString(), kMaxNotes, true) } };
    if (goodId(get(o, "source"), kMaxId)) known.insert(QStringLiteral("source"), get(o, "source"));
    QJsonObject out = o;
    out.remove(QStringLiteral("source"));
    for (auto it = known.constBegin(); it != known.constEnd(); ++it) out.insert(it.key(), it.value());
    if (placeSize(out) <= kMaxSize) return out;
    // Too big: without what this build doesn't know.
    QJsonArray bare;
    for (const auto& sv : std::as_const(spots)) {
        const QJsonObject s = sv.toObject();
        QJsonObject b;
        for (const char* k : { "id", "name", "track", "capacity", "role" })
            if (s.contains(QLatin1String(k))) b.insert(QLatin1String(k), get(s, k));
        bare.append(b);
    }
    known.insert(QStringLiteral("spots"), bare);
    return known;
}

QJsonArray readPlaces(const QJsonValue& v) {
    QJsonArray out;
    QSet<QString> seen;
    const QJsonArray stored = v.toArray();
    for (const auto& x : stored) {
        const auto p = readPlace(x);
        if (!p || out.size() >= kMaxPlaces || seen.contains(idOf(*p))) continue;
        seen.insert(idOf(*p));
        out.append(*p);
    }
    return out;
}

QJsonArray renamed(const QJsonArray& places, const Rename& rename) {
    QJsonArray out;
    for (const auto& pv : places) {
        QJsonObject p = pv.toObject();
        QJsonArray spots;
        const QJsonArray had = get(p, "spots").toArray();
        for (const auto& sv : had) {
            QJsonObject s = sv.toObject();
            if (s.contains(QLatin1String("track"))) {
                QJsonObject t = get(s, "track").toObject();
                if (const auto to = rename(get(t, "brick").toString())) {
                    t.insert(QStringLiteral("brick"), *to);
                    s.insert(QStringLiteral("track"), t);
                } else {
                    s.remove(QStringLiteral("track"));
                }
            }
            spots.append(s);
        }
        p.insert(QStringLiteral("spots"), spots);
        out.append(p);
    }
    return out;
}

QJsonArray copied(const QJsonArray& places, const Rename& rename, const NewId& newId) {
    QJsonArray out;
    const QJsonArray here = renamed(places, rename);
    for (const auto& pv : here) {
        QJsonObject p = pv.toObject();
        p.insert(QStringLiteral("source"), sourceOf(p));
        p.insert(QStringLiteral("id"), newId());
        out.append(p);
    }
    return out;
}

QJsonArray updated(const QJsonArray& current, const QJsonArray& next, const NewId& newId) {
    QHash<QString, QString> bySource;
    for (const auto& p : current) bySource.insert(sourceOf(p.toObject()), idOf(p));
    QSet<QString> used;
    QJsonArray out;
    for (const auto& pv : next) {
        QJsonObject p = pv.toObject();
        const QString key = sourceOf(p);
        QString id = bySource.value(key);
        if (id.isEmpty() || used.contains(id)) id = newId();
        used.insert(id);
        p.insert(QStringLiteral("id"), id);
        p.insert(QStringLiteral("source"), key);
        out.append(p);
    }
    return out;
}

QJsonArray forLibrary(const QJsonArray& places, const QSet<QString>& members) {
    QJsonArray out;
    QSet<QString> seen;
    const QJsonArray own = renamed(places, [&members](const QString& id) { return members.contains(id) ? std::optional(id) : std::nullopt; });
    for (const auto& pv : own) {
        QJsonObject p = pv.toObject();
        const QString id = sourceOf(p);
        if (seen.contains(id)) continue;
        seen.insert(id);
        p.remove(QStringLiteral("source"));
        p.insert(QStringLiteral("id"), id);
        out.append(p);
    }
    return out;
}

QStringList bricksOf(const QJsonObject& place) {
    QStringList out;
    const QJsonArray spots = get(place, "spots").toArray();
    for (const auto& s : spots)
        if (s.toObject().contains(QLatin1String("track"))) out << get(get(s.toObject(), "track").toObject(), "brick").toString();
    return out;
}

Adopted adopted(const QJsonArray& layoutPlaces, const QSet<QString>& members) {
    Adopted out;
    for (const auto& p : layoutPlaces) {
        const QStringList bricks = bricksOf(p.toObject());
        const bool all = !bricks.isEmpty() && std::all_of(bricks.begin(), bricks.end(), [&](const QString& b) { return members.contains(b); });
        if (all && out.module.size() < kMaxPlaces) out.module.append(p);
        else out.layout.append(p);
    }
    return out;
}

QJsonArray released(const QJsonArray& layoutPlaces, const QJsonArray& places, const NewId& newId) {
    QJsonArray out = layoutPlaces;
    for (const auto& pv : places) {
        if (out.size() >= kMaxPlaces) break;
        QJsonObject p = pv.toObject();
        p.remove(QStringLiteral("source"));
        if (indexOf(out, idOf(p)) >= 0) p.insert(QStringLiteral("id"), newId());
        out.append(p);
    }
    return out;
}

QList<std::pair<QString, int>> counts(const QStringList& all) {
    QMap<QString, int> n;  // (others in this order: by name)
    for (const QString& k : all) ++n[k];
    QList<std::pair<QString, int>> out;
    const QStringList known = kinds();
    for (const QString& k : known)
        if (n.contains(k)) out.append({ k, n.take(k) });
    for (auto it = n.constBegin(); it != n.constEnd(); ++it) out.append({ it.key(), it.value() });
    return out;
}

QList<std::pair<QString, int>> countsOf(const QJsonArray& places) {
    QStringList all;
    for (const auto& p : places) all << get(p.toObject(), "kind").toString();
    return counts(all);
}

bool sameCarType(const QString& a, const QString& b) { return a.trimmed().toLower() == b.trimmed().toLower(); }

std::optional<int> spotCapacity(const QJsonObject& spot, std::optional<double> usable) {
    if (get(spot, "capacity").isDouble()) return get(spot, "capacity").toInt();
    if (!usable) return std::nullopt;
    return std::max(0, static_cast<int>(std::floor(*usable / kTypicalCar + 1e-6)));
}

int indexOf(const QJsonArray& places, const QString& id) {
    for (qsizetype i = 0; i < places.size(); ++i)
        if (idOf(places.at(i)) == id) return static_cast<int>(i);
    return -1;
}

QJsonArray withPlace(QJsonArray places, const QJsonObject& place) {
    const int i = indexOf(places, idOf(place));
    if (i >= 0) places.replace(i, place);
    else places.append(place);
    return places;
}

QJsonArray without(QJsonArray places, const QString& id) {
    const int i = indexOf(places, id);
    if (i >= 0) places.removeAt(i);
    return places;
}

}  // namespace bld::core::places
