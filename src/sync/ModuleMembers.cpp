#include "ModuleMembers.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bld::sync {

namespace {

QStringList strings(const QJsonValue& list) {
    QStringList out;
    if (!list.isArray()) return out;
    const QJsonArray a = list.toArray();
    for (const auto& v : a)
        if (v.isString()) out << v.toString();
    return out;
}

// A number a key may hold (the part is in the module).
bool isPlace(const QJsonValue& v) { return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= 0; }

}  // namespace

QString memberKey(const QString& moduleId, const QString& partId) {
    return QLatin1String(kMemberPrefix) + QString::number(moduleId.size()) + QLatin1Char(':') + moduleId
           + QLatin1Char(':') + partId;
}

std::optional<MemberKey> parseMemberKey(const QString& key) {
    const qsizetype prefix = static_cast<qsizetype>(sizeof(kMemberPrefix)) - 1;
    if (!key.startsWith(QLatin1String(kMemberPrefix))) return std::nullopt;
    const qsizetype colon = key.indexOf(QLatin1Char(':'), prefix);
    if (colon < 0) return std::nullopt;
    // The length as a writer gives it: digits only, no leading zero, no
    // more than a key could hold.
    const QStringView digits = QStringView(key).mid(prefix, colon - prefix);
    if (digits.isEmpty() || digits.size() > 9) return std::nullopt;
    if (digits.size() > 1 && digits[0] == QLatin1Char('0')) return std::nullopt;
    qsizetype n = 0;
    for (const QChar c : digits) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return std::nullopt;
        n = n * 10 + (c.unicode() - u'0');
    }
    const qsizetype start = colon + 1;
    if (key.size() < start + n + 2 || key[start + n] != QLatin1Char(':')) return std::nullopt;
    return MemberKey{ key.mid(start, n), key.mid(start + n + 1) };
}

bool validMemberValue(const QJsonValue& v) { return (v.isBool() && !v.toBool()) || isPlace(v); }

QStringList moduleMembers(const QJsonValue& base, const MemberKeys& keys) {
    const QStringList start = strings(base);
    const QSet<QString> inStart(start.cbegin(), start.cend());
    QStringList out;
    for (const QString& id : start) {
        const auto it = keys.constFind(id);
        if (it != keys.constEnd() && it->isBool() && !it->toBool()) continue;
        out << id;
    }
    struct Added {
        QString id;
        double at;
    };
    std::vector<Added> added;
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        if (isPlace(it.value()) && !inStart.contains(it.key())) added.push_back({ it.key(), it.value().toDouble() });
    // (Ids by UTF-16 code unit, as JavaScript compares strings.)
    std::sort(added.begin(), added.end(),
              [](const Added& a, const Added& b) { return a.at != b.at ? a.at < b.at : a.id < b.id; });
    for (const Added& a : added) out << a.id;
    return out;
}

MemberWrites memberWrites(const QJsonValue& base, const MemberKeys& keys, const QStringList& want) {
    const QStringList start = strings(base);
    const QSet<QString> inStart(start.cbegin(), start.cend());
    const QStringList now = moduleMembers(base, keys);
    const QSet<QString> nowSet(now.cbegin(), now.cend());
    const QSet<QString> wanted(want.cbegin(), want.cend());
    MemberWrites out;
    QSet<QString> seen;
    // (An empty id has no key.)
    for (const QString& id : now) {
        if (id.isEmpty() || seen.contains(id)) continue;
        seen.insert(id);
        if (wanted.contains(id)) continue;
        if (inStart.contains(id)) out.set.append({ id, QJsonValue(false) });
        else out.remove << id;
    }
    double next = 0;
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        if (isPlace(it.value()) && it.value().toDouble() >= next) next = std::floor(it.value().toDouble()) + 1;
    seen.clear();
    for (const QString& id : want) {
        if (id.isEmpty() || seen.contains(id)) continue;
        seen.insert(id);
        if (nowSet.contains(id)) continue;
        if (inStart.contains(id)) out.remove << id;
        else out.set.append({ id, QJsonValue(next++) });
    }
    return out;
}

}  // namespace bld::sync
