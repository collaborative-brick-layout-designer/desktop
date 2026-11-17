#include "SharedModule.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QtMath>

#include <cmath>

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call SharedModuleText::tr().
struct SharedModuleText { Q_DECLARE_TR_FUNCTIONS(SharedModule) };

namespace bld::core {

namespace {

bool finite(const QJsonValue& v) { return v.isDouble() && std::isfinite(v.toDouble()); }

QPointF rotated(QPointF v, double degrees) {
    const double r = qDegreesToRadians(degrees);
    const double c = std::cos(r), s = std::sin(r);
    return { v.x() * c - v.y() * s, v.x() * s + v.y() * c };
}

bool wholeNumber(const QJsonValue& v) {
    return v.isDouble() && std::isfinite(v.toDouble()) && std::floor(v.toDouble()) == v.toDouble();
}

std::optional<SharedReview> readReview(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    const QJsonObject o = v.toObject();
    const QJsonValue to = o.value(QLatin1String("to"));
    if (!wholeNumber(to) || to.toDouble() < 1) return std::nullopt;
    SharedReview r;
    r.to = to.toInt();
    const QJsonValue from = o.value(QLatin1String("from"));
    r.from = wholeNumber(from) && from.toDouble() > 0 ? from.toInt() : 0;
    const QJsonValue at = o.value(QLatin1String("at"));
    r.at = finite(at) ? static_cast<qint64>(at.toDouble()) : 0;
    return r;
}

bool isUuid(const QString& s) {
    static const QRegularExpression re(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return re.match(s).hasMatch();
}

}  // namespace

std::optional<SharedPlacement> readPlacement(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    const QJsonObject o = v.toObject();
    const QJsonValue d = o.value(QLatin1String("degrees")), x = o.value(QLatin1String("x")),
                     y = o.value(QLatin1String("y"));
    if (!finite(d) || !finite(x) || !finite(y)) return std::nullopt;
    SharedPlacement p{ d.toDouble(), x.toDouble(), y.toDouble(), std::nullopt };
    if (const QJsonValue sheets = o.value(QLatin1String("sheets")); sheets.isObject()) {
        QMap<QString, QString> out;
        const QJsonObject so = sheets.toObject();
        for (auto it = so.begin(); it != so.end(); ++it)
            if (it.value().isString() && !it.value().toString().isEmpty()) out.insert(it.key(), it.value().toString());
        p.sheets = out;
    }
    return p;
}

QJsonObject placementJson(const SharedPlacement& p) {
    QJsonObject o{ { QStringLiteral("degrees"), p.degrees }, { QStringLiteral("x"), p.x }, { QStringLiteral("y"), p.y } };
    if (p.sheets) {
        QJsonObject sheets;
        for (auto it = p.sheets->begin(); it != p.sheets->end(); ++it) sheets.insert(it.key(), it.value());
        o.insert(QStringLiteral("sheets"), sheets);
    }
    return o;
}

double norm360(double degrees) {
    const double r = std::fmod(degrees, 360.0);
    return r < 0 ? r + 360.0 : r;
}

QPointF placePoint(const SharedPlacement& p, QPointF at) { return QPointF(p.x, p.y) + rotated(at, p.degrees); }

SharedPlacement movedPlacement(SharedPlacement p, QPointF delta) {
    p.x += delta.x();
    p.y += delta.y();
    return p;
}

SharedPlacement turnedPlacement(SharedPlacement p, double degrees, QPointF about) {
    const QPointF r = rotated(QPointF(p.x, p.y) - about, degrees);
    p.degrees = norm360(p.degrees + degrees);
    p.x = about.x() + r.x();
    p.y = about.y() + r.y();
    return p;
}

SharedPlacement droppedPlacement(QPointF delta, const std::optional<DropTurn>& turn) {
    if (!turn) return { 0.0, delta.x(), delta.y(), std::nullopt };
    const QPointF r = rotated(delta - turn->pivot, turn->degrees);
    return { norm360(turn->degrees), turn->to.x() + r.x(), turn->to.y() + r.y(), std::nullopt };
}

bool hasSharedMark(const Module& m) { return m.shared.has_value() && !m.libraryModuleId.isEmpty(); }

std::optional<SharedPlaced> sharedPlaced(const Module& m) {
    if (!m.shared || !isUuid(m.libraryModuleId)) return std::nullopt;
    SharedPlaced s;
    s.placedId = m.id;
    s.name = m.name;
    s.moduleId = m.libraryModuleId;
    s.version = std::max(0, m.libraryVersion);
    s.by = m.shared->value(QLatin1String("by")).toString();
    s.server = m.shared->value(QLatin1String("server")).toString();
    s.placement = readPlacement(m.shared->value(QLatin1String("placement")));
    s.review = readReview(m.shared->value(QLatin1String("review")));
    s.members = m.memberIds;
    return s;
}

std::vector<SharedPlaced> sharedModulesIn(const std::vector<Module>& modules) {
    std::vector<SharedPlaced> out;
    for (const auto& m : modules)
        if (auto s = sharedPlaced(m)) out.push_back(std::move(*s));
    return out;
}

bool holdsSharedMark(const std::vector<Module>& modules) {
    for (const auto& m : modules)
        if (hasSharedMark(m)) return true;
    return false;
}

Module withPlacement(Module m, const SharedPlacement& p) {
    if (!m.shared) return m;
    m.shared->insert(QStringLiteral("placement"), placementJson(p));
    return m;
}

Module withReview(Module m, const std::optional<SharedReview>& review) {
    if (!m.shared) return m;
    if (!review) {
        m.shared->remove(QStringLiteral("review"));
        return m;
    }
    QJsonObject r{ { QStringLiteral("to"), review->to }, { QStringLiteral("at"), static_cast<double>(review->at) } };
    r.insert(QStringLiteral("from"), review->from > 0 ? QJsonValue(review->from) : QJsonValue(QJsonValue::Null));
    m.shared->insert(QStringLiteral("review"), r);
    return m;
}

Module detached(Module m) {
    m.shared.reset();
    m.libraryModuleId.clear();
    m.libraryVersion = 0;
    return m;
}

Module sharedCopyOf(const Module& m, QSet<QString> members, QString newId, QPointF moved) {
    Module c = copyOfModule(m, std::move(members), std::move(newId));
    const auto s = sharedPlaced(m);
    if (!s) return c;
    c.name = m.name;
    c.libraryModuleId = m.libraryModuleId;
    c.libraryVersion = m.libraryVersion;
    QJsonObject link = m.shared.value_or(QJsonObject{});
    link.remove(QStringLiteral("review"));
    if (s->placement) link.insert(QStringLiteral("placement"), placementJson(movedPlacement(*s->placement, moved)));
    c.shared = link;
    return c;
}

Module copyOfPlaced(const Module& m, QSet<QString> members, QString newId, QPointF moved) {
    return isSharedModule(m) ? sharedCopyOf(m, std::move(members), std::move(newId), moved)
                             : copyOfModule(m, std::move(members), std::move(newId));
}

QString lockMessage(const QString& name, const QString& by, LockedAction what) {
    const QString n = name.isEmpty() ? SharedModuleText::tr("This module") : SharedModuleText::tr("“%1”").arg(name);
    if (by.isEmpty()) {
        switch (what) {
        case LockedAction::Rename:
            return SharedModuleText::tr("%1 is a shared module, so its name is its owner's. You can move it, turn it "
                                        "and change its outline color.")
                .arg(n);
        case LockedAction::Members:
            return SharedModuleText::tr("%1 is a shared module, so parts can't be added to it or taken out of it here.")
                .arg(n);
        case LockedAction::Dissolve:
            return SharedModuleText::tr("%1 is a shared module, so it stays one module. You can remove it from this "
                                        "layout, or its layout's owner can make it theirs.")
                .arg(n);
        case LockedAction::Bend:
            return SharedModuleText::tr("%1 is a shared module, so its track can't be bent here. You can move and "
                                        "turn the whole module.")
                .arg(n);
        case LockedAction::Edit:
        case LockedAction::Parts: break;
        }
        return SharedModuleText::tr("%1 is a shared module, so its parts can't be changed here. You can move it, "
                                    "turn it or remove it as a whole.")
            .arg(n);
    }
    switch (what) {
    case LockedAction::Rename:
        return SharedModuleText::tr("%1 is shared by %2, so its name is %2's. You can move it, turn it and change "
                                    "its outline color.")
            .arg(n, by);
    case LockedAction::Members:
        return SharedModuleText::tr("%1 is shared by %2, so parts can't be added to it or taken out of it here.")
            .arg(n, by);
    case LockedAction::Dissolve:
        return SharedModuleText::tr("%1 is shared by %2, so it stays one module. You can remove it from this layout, "
                                    "or its layout's owner can make it theirs.")
            .arg(n, by);
    case LockedAction::Bend:
        return SharedModuleText::tr("%1 is shared by %2, so its track can't be bent here. You can move and turn the "
                                    "whole module.")
            .arg(n, by);
    case LockedAction::Edit:
    case LockedAction::Parts: break;
    }
    return SharedModuleText::tr("%1 is shared by %2, so its parts can't be changed here. You can move it, turn it or "
                                "remove it as a whole.")
        .arg(n, by);
}

std::optional<SharedPlaced> sharedLockOn(const std::vector<Module>& modules, const QSet<QString>& ids, bool whole) {
    if (ids.isEmpty()) return std::nullopt;
    for (const auto& m : modules) {
        if (!m.shared) continue;
        if (!sharedPlaced(m)) continue;
        qsizetype n = 0;
        for (const auto& id : m.memberIds)
            if (ids.contains(id)) ++n;
        if (n == 0) continue;
        if (whole && n == m.memberIds.size()) continue;
        return sharedPlaced(m);
    }
    return std::nullopt;
}

std::optional<LockedAction> keepsLock(const Module& was, const Module& next) {
    const auto a = sharedPlaced(was);
    if (!a) return std::nullopt;
    const auto b = sharedPlaced(next);
    if (!b || b->moduleId != a->moduleId) return LockedAction::Dissolve;
    if (b->members != a->members) return LockedAction::Members;
    if (next.name != was.name) return LockedAction::Rename;
    return std::nullopt;
}

}  // namespace bld::core
