#include "SidecarItems.h"

#include "ModuleMembers.h"

#include <QSet>
#include <QJsonArray>
#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bld::sync {

namespace {

struct Kind {
    const char* field;
    const char* prefix;
};
constexpr Kind kKinds[] = { { "modules", "m:" }, { "anchoredLabels", "l:" } };
constexpr char kBlobKey[] = "__blob";
constexpr char kOrderKey[] = "_order";

// ECMAScript Number::toString: the shortest digits that read back the
// same, laid out plainly from 1e-6 up to 1e21 and in exponent form outside.
QString jsNumber(double d) {
    if (!std::isfinite(d)) return QStringLiteral("null");
    if (d == 0) return QStringLiteral("0");
    QString sign;
    if (d < 0) {
        sign = QStringLiteral("-");
        d = -d;
    }
    // "d.ddde±x": the digits and the exponent.
    const QString e = QString::number(d, 'e', QLocale::FloatingPointShortest);
    const qsizetype at = e.indexOf(QLatin1Char('e'));
    QString digits = e.left(at);
    digits.remove(QLatin1Char('.'));
    const int n = e.mid(at + 1).toInt() + 1;  // where the point goes
    const auto k = static_cast<int>(digits.size());
    QString out;
    if (k <= n && n <= 21) {
        out = digits + QString(n - k, QLatin1Char('0'));
    } else if (0 < n && n <= 21) {
        out = digits.left(n) + QLatin1Char('.') + digits.mid(n);
    } else if (-6 < n && n <= 0) {
        out = QStringLiteral("0.") + QString(-n, QLatin1Char('0')) + digits;
    } else {
        const int x = n - 1;
        out = digits.left(1) + (k > 1 ? QLatin1Char('.') + digits.mid(1) : QString()) + QLatin1Char('e')
              + (x < 0 ? QLatin1Char('-') : QLatin1Char('+')) + QString::number(std::abs(x));
    }
    return sign + out;
}

// JSON.stringify of a string: quotes, backslashes and control characters
// escaped, lone surrogates as \udxxx, the rest as is.
QString jsString(const QString& s) {
    QString out = QStringLiteral("\"");
    for (qsizetype i = 0; i < s.size(); ++i) {
        const char16_t c = s[i].unicode();
        switch (c) {
        case u'"': out += QStringLiteral("\\\""); continue;
        case u'\\': out += QStringLiteral("\\\\"); continue;
        case u'\b': out += QStringLiteral("\\b"); continue;
        case u'\f': out += QStringLiteral("\\f"); continue;
        case u'\n': out += QStringLiteral("\\n"); continue;
        case u'\r': out += QStringLiteral("\\r"); continue;
        case u'\t': out += QStringLiteral("\\t"); continue;
        default: break;
        }
        const bool high = c >= 0xD800 && c <= 0xDBFF;
        const bool low = c >= 0xDC00 && c <= 0xDFFF;
        const bool paired = high && i + 1 < s.size() && s[i + 1].unicode() >= 0xDC00 && s[i + 1].unicode() <= 0xDFFF;
        if (c < 0x20 || (high && !paired) || (low && (i == 0 || !(s[i - 1].unicode() >= 0xD800 && s[i - 1].unicode() <= 0xDBFF)))) {
            out += QStringLiteral("\\u%1").arg(static_cast<int>(c), 4, 16, QLatin1Char('0'));
            continue;
        }
        out += QChar(c);
        if (paired) out += s[++i];
    }
    return out + QLatin1Char('"');
}

}  // namespace

QString stableJson(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: return QStringLiteral("null");
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: return jsNumber(v.toDouble());
    case QJsonValue::String: return jsString(v.toString());
    case QJsonValue::Array: {
        QStringList parts;
        for (const auto& x : v.toArray()) parts << stableJson(x);
        return QLatin1Char('[') + parts.join(QLatin1Char(',')) + QLatin1Char(']');
    }
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        QStringList keys = o.keys();
        std::sort(keys.begin(), keys.end());  // by UTF-16 code unit, as JavaScript's sort
        QStringList parts;
        for (const QString& k : keys) parts << jsString(k) + QLatin1Char(':') + stableJson(o.value(k));
        return QLatin1Char('{') + parts.join(QLatin1Char(',')) + QLatin1Char('}');
    }
    }
    return QStringLiteral("null");
}

QString sidecarBlobHash(const QJsonValue& blob) {
    const QString text = stableJson(blob);
    uint32_t h = 0x811c9dc5U;
    for (const QChar c : text) {
        h ^= c.unicode();
        h *= 0x01000193U;
    }
    return QStringLiteral("%1:%2").arg(text.size()).arg(h, 0, 16);
}

// The items are keyed by id: a sidecar where a module or label has no id,
// or shares one, can't be split into them without losing an entry, so it's
// read as the blob it is (as the web's readSidecarCache does).
bool sidecarIdsUnique(const QJsonObject& blob) {
    for (const Kind& kind : kKinds) {
        const QJsonValue list = blob.value(QString::fromLatin1(kind.field));
        if (!list.isArray()) continue;
        QSet<QString> seen;
        const QJsonArray entries = list.toArray();
        for (const auto& v : entries) {
            const QJsonValue id = v.toObject().value(QLatin1String("id"));
            if (!id.isString() || seen.contains(id.toString())) return false;
            seen.insert(id.toString());
        }
    }
    return true;
}

namespace {

// Every module's part keys in `items`: module id -> part id -> value
// (valid ones only).
QHash<QString, MemberKeys> memberKeysByModule(const QJsonObject& items) {
    QHash<QString, MemberKeys> out;
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (!it.key().startsWith(QLatin1String(kMemberPrefix)) || !validMemberValue(it.value())) continue;
        const auto at = parseMemberKey(it.key());
        if (!at) continue;
        out[at->moduleId].insert(at->partId, it.value());
    }
    return out;
}

// `blob` with its modules and anchored labels as `items` hold them.
QJsonObject withItems(const QJsonObject& blob, const QJsonObject& items) {
    QJsonObject out = blob;
    const QHash<QString, MemberKeys> memberKeys = memberKeysByModule(items);
    for (const Kind& kind : kKinds) {
        struct Entry {
            double order;
            QString key;
            QJsonObject value;
        };
        std::vector<Entry> entries;
        const QString prefix = QString::fromLatin1(kind.prefix);
        const bool modules = prefix == QLatin1String("m:");
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (!it.key().startsWith(prefix) || !it.value().isObject()) continue;
            QJsonObject value = it.value().toObject();
            const double order = value.value(QLatin1String(kOrderKey)).toDouble(0);
            value.remove(QLatin1String(kOrderKey));
            // A module's parts: its starting list with what the `p:` keys changed.
            if (modules) {
                const auto keys = memberKeys.constFind(it.key().mid(prefix.size()));
                if (keys != memberKeys.constEnd())
                    value.insert(QStringLiteral("members"),
                                 QJsonArray::fromStringList(moduleMembers(value.value(QLatin1String("members")), *keys)));
            }
            entries.push_back({ order, it.key(), value });
        }
        const QString field = QString::fromLatin1(kind.field);
        if (entries.empty() && !blob.value(field).isArray()) continue;
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.order != b.order ? a.order < b.order : a.key < b.key; });
        QJsonArray list;
        for (const Entry& e : entries) list.append(e.value);
        out.insert(field, list);
    }
    return out;
}

}  // namespace

QJsonObject mergedSidecarCache(const QJsonObject& blob, const QJsonObject& items) {
    if (items.isEmpty() || items.value(QLatin1String(kBlobKey)).toString() != sidecarBlobHash(blob)
        || !sidecarIdsUnique(blob))
        return blob;
    return withItems(blob, items);
}

QJsonObject sidecarItemsFor(const QJsonObject& blob) {
    QJsonObject out;
    for (const Kind& kind : kKinds) {
        const QJsonArray list = blob.value(QString::fromLatin1(kind.field)).toArray();
        for (qsizetype i = 0; i < list.size(); ++i) {
            QJsonObject v = list[i].toObject();
            const QJsonValue id = v.value(QLatin1String("id"));
            if (!id.isString()) continue;
            v.insert(QLatin1String(kOrderKey), static_cast<double>(i));
            out.insert(QString::fromLatin1(kind.prefix) + id.toString(), v);
        }
    }
    out.insert(QLatin1String(kBlobKey), sidecarBlobHash(blob));
    return out;
}

bool hasMemberKeys(const QJsonObject& items) {
    for (auto it = items.begin(); it != items.end(); ++it)
        if (it.key().startsWith(QLatin1String(kMemberPrefix))) return true;
    return false;
}

SidecarWrite sidecarWriteFor(const QJsonObject& cache, const QJsonObject& curBlob, const QJsonObject& curItems,
                             bool memberKeys) {
    // Each module whole, with its list (the shape before the keys).
    const bool whole = !memberKeys && !hasMemberKeys(curItems);
    // Whether the items are what the layout holds now. When they aren't (a
    // writer that doesn't know them replaced meta.cache, or ids are missing
    // or shared), each module starts again from the list written here.
    const bool splittable = sidecarIdsUnique(cache);
    const bool current = splittable && !curItems.isEmpty()
                         && curItems.value(QLatin1String(kBlobKey)).toString() == sidecarBlobHash(curBlob)
                         && sidecarIdsUnique(curBlob);
    const QHash<QString, MemberKeys> keysByModule = memberKeysByModule(curItems);
    const QString memberPrefix = QLatin1String(kMemberPrefix);
    QJsonObject items;
    // Part keys stay unless a module below drops or changes them (one whose
    // module is gone is ignored by every reader).
    for (auto it = curItems.begin(); it != curItems.end(); ++it)
        if (it.key().startsWith(memberPrefix)) items.insert(it.key(), it.value());
    const auto dropMemberKeys = [&](const QString& moduleId) {
        for (const QString& k : items.keys())
            if (k.startsWith(memberPrefix)) {
                const auto at = parseMemberKey(k);
                if (at && at->moduleId == moduleId) items.remove(k);
            }
    };
    for (const Kind& kind : kKinds) {
        const QString prefix = QString::fromLatin1(kind.prefix);
        const bool modules = prefix == QLatin1String("m:");
        const QJsonArray list = cache.value(QString::fromLatin1(kind.field)).toArray();
        QSet<QString> wanted;
        for (qsizetype i = 0; i < list.size(); ++i) {
            QJsonObject v = list[i].toObject();
            const QJsonValue idValue = v.value(QLatin1String("id"));
            if (!list[i].isObject() || !idValue.isString()) continue;
            const QString id = idValue.toString();
            const QString k = prefix + id;
            wanted.insert(k);
            v.insert(QLatin1String(kOrderKey), static_cast<double>(i));
            if (!modules) {
                items.insert(k, v);
                continue;
            }
            const QJsonValue had = curItems.value(k);
            if (whole || !current || !had.isObject()) {
                // A new module (or one read from meta.cache): the list is its starting list.
                if (keysByModule.contains(id)) dropMemberKeys(id);
                items.insert(k, v);
                continue;
            }
            const QJsonValue start = had.toObject().value(QLatin1String("members"));
            const QJsonValue members = v.value(QLatin1String("members"));
            if (start.isUndefined()) v.remove(QStringLiteral("members"));
            else v.insert(QStringLiteral("members"), start);
            items.insert(k, v);
            if (!members.isArray()) continue;
            QStringList want;
            for (const auto& m : members.toArray())
                if (m.isString()) want << m.toString();
            const MemberWrites writes = memberWrites(start, keysByModule.value(id), want);
            for (const QString& part : writes.remove) items.remove(memberKey(id, part));
            for (const auto& [part, value] : writes.set) items.insert(memberKey(id, part), value);
        }
        // A module that is gone takes its part keys with it.
        if (modules)
            for (auto it = curItems.begin(); it != curItems.end(); ++it)
                if (it.key().startsWith(prefix) && !wanted.contains(it.key())) dropMemberKeys(it.key().mid(prefix.size()));
    }
    SidecarWrite out;
    out.blob = splittable ? withItems(cache, items) : cache;
    items.insert(QLatin1String(kBlobKey), sidecarBlobHash(out.blob));
    out.items = items;
    return out;
}

}  // namespace bld::sync
