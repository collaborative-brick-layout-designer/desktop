#include "Presence.h"

#include <QColor>

#include <cmath>

#include <QJsonArray>
#include <QSet>

#include <algorithm>

#include <cstdint>
#include <cstdlib>

namespace bld::sync::presence {

namespace {
const char* const kPalette[] = { "#f87171", "#fb923c", "#facc15", "#4ade80",
                                 "#22d3ee", "#60a5fa", "#a78bfa", "#f472b6" };
constexpr int kColors = 8;

// FNV-1a over the UTF-16 code units, then murmur3's finalizer (the web's mix()).
std::uint32_t mix(const QString& seed) {
    std::uint32_t h = 0x811c9dc5u;
    for (const QChar c : seed) {
        h ^= c.unicode();
        h *= 0x01000193u;
    }
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}
} // namespace

int firstChoice(const QString& userId, const QString& layoutId) {
    return static_cast<int>(mix(userId + QLatin1Char(':') + layoutId) % kColors);
}

namespace {
double relativeLuminance(const QString& hex) {
    const QColor c(hex);
    const auto chan = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * chan(c.redF()) + 0.7152 * chan(c.greenF()) + 0.0722 * chan(c.blueF());
}
} // namespace

double contrastRatio(const QString& a, const QString& b) {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QString textColorOn(const QString& color) {
    return contrastRatio(color, QLatin1String(kTextDark)) >= contrastRatio(color, QLatin1String(kTextLight))
               ? QString::fromLatin1(kTextDark)
               : QString::fromLatin1(kTextLight);
}

QString colorFor(const QString& userId, const QString& layoutId) {
    return QString::fromLatin1(kPalette[firstChoice(userId, layoutId)]);
}

QHash<QString, QString> colorsFor(const QStringList& userIds, const QString& layoutId) {
    QStringList ids = userIds;
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end()); // UTF-16 code units, as the web's sort
    QSet<int> taken;
    QHash<QString, QString> out;
    for (const QString& id : ids) {
        const int first = firstChoice(id, layoutId);
        int pick = first;
        if (taken.size() < kColors)
            for (int k = 0; k < kColors; ++k)
                if (const int c = (first + k) % kColors; !taken.contains(c)) {
                    pick = c;
                    break;
                }
        taken.insert(pick);
        out.insert(id, QString::fromLatin1(kPalette[pick]));
    }
    return out;
}

QJsonObject state(const User& user, std::optional<QPointF> cursor, const QStringList& brickIds,
                  qint64 nowMs, const QString& editingModule) {
    QJsonArray ids;
    for (const auto& id : brickIds) ids.append(id);
    return {
        { QStringLiteral("user"), QJsonObject{ { QStringLiteral("id"), user.id },
                                               { QStringLiteral("displayName"), user.displayName },
                                               { QStringLiteral("avatarUrl"), QJsonValue::Null },
                                               { QStringLiteral("color"), user.color } } },
        { QStringLiteral("cursor"),
          cursor ? QJsonValue(QJsonObject{ { QStringLiteral("x"), cursor->x() },
                                           { QStringLiteral("y"), cursor->y() },
                                           { QStringLiteral("layerId"), QJsonValue::Null } })
                 : QJsonValue(QJsonValue::Null) },
        { QStringLiteral("selection"), QJsonObject{ { QStringLiteral("brickIds"), ids } } },
        { QStringLiteral("tool"), QStringLiteral("select") },
        { QStringLiteral("editingModule"), editingModule.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(editingModule) },
        { QStringLiteral("lastActivityMs"), static_cast<double>(nowMs) },
    };
}

Peer peerFrom(const QJsonObject& s) {
    Peer p;
    const QJsonObject user = s.value(QLatin1String("user")).toObject();
    p.userId = user.value(QLatin1String("id")).toString();
    p.name = user.value(QLatin1String("displayName")).toString();
    // A person's: a name, a selection and when they last moved (the web's readPeerPresence).
    p.person = user.value(QLatin1String("displayName")).isString()
               && s.value(QLatin1String("selection")).toObject().value(QLatin1String("brickIds")).isArray()
               && s.value(QLatin1String("lastActivityMs")).isDouble();
    p.color = user.value(QLatin1String("color")).toString(QStringLiteral("#888888"));
    const QJsonValue c = s.value(QLatin1String("cursor"));
    if (c.isObject()) {
        const QJsonObject o = c.toObject();
        const QJsonValue x = o.value(QLatin1String("x")), y = o.value(QLatin1String("y"));
        if (x.isDouble() && y.isDouble()) p.cursor = QPointF(x.toDouble(), y.toDouble());
    }
    for (const auto& v :
         s.value(QLatin1String("selection")).toObject().value(QLatin1String("brickIds")).toArray())
        if (v.isString()) p.brickIds << v.toString();
    p.editingModule = s.value(QLatin1String("editingModule")).toString();
    return p;
}

QString label(const QString& name) {
    constexpr int kMax = 20;
    return name.size() > kMax ? name.left(kMax - 1) + QStringLiteral("…") : name;
}

} // namespace bld::sync::presence
