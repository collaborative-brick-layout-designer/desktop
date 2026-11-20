#include "ModuleSetupText.h"

#include "../core/Map.h"
#include "../edit/venue/VenueUnits.h"

#include <QRegularExpression>

#include <cmath>

namespace bld::ui {

ModuleSetupText::Unit ModuleSetupText::heightUnit(const core::Map* map) {
    if (map && map->sidecar.venue) return map->sidecar.venue->lengthUnit;
    return Unit::FeetInches;
}

std::optional<double> ModuleSetupText::parseHeightMm(const QString& text, Unit unit) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return std::nullopt;
    static const QRegularExpression bare(QStringLiteral(R"(^\d+(?:\.\d+)?$)"));
    const bool isBare = bare.match(t).hasMatch();
    const std::optional<double> studs = unit == Unit::Metres && isBare
                                            ? edit::venue::parseLength(t + QStringLiteral("cm"), unit)
                                            : edit::venue::parseLength(t, unit);
    if (!studs || !std::isfinite(*studs)) return std::nullopt;
    const double mm = std::round(*studs / edit::venue::kStudsPerMm * 10.0) / 10.0;
    if (mm <= 0.0 || mm > core::kMaxHeightMm) return std::nullopt;
    return mm;
}

QString ModuleSetupText::formatHeight(double mm, Unit unit) {
    if (unit == Unit::Metres) return tr("%1 cm").arg(std::round(mm) / 10.0, 0, 'f', 1);
    const double studs = mm * edit::venue::kStudsPerMm;
    if (unit == Unit::Studs) return tr("%1 studs").arg(std::round(studs * 10.0) / 10.0);
    static const QString fractions[] = { QString(), QStringLiteral("¼"), QStringLiteral("½"), QStringLiteral("¾") };
    const long quarters = std::lround(studs / edit::venue::kStudsPerInch * 4.0);
    return QStringLiteral("%1%2″").arg(quarters / 4).arg(fractions[quarters % 4]);
}

QString ModuleSetupText::unitHint(Unit unit) {
    switch (unit) {
    case Unit::Metres: return tr("In centimeters, like 78 or 78.1 cm");
    case Unit::Studs: return tr("In studs, like 98");
    case Unit::FeetInches: break;
    }
    return tr("In inches, like 30 3/4 or 32\"");
}

QString ModuleSetupText::typeLabel(const QString& type) {
    if (type == QLatin1String("mils")) return tr("MILS");
    if (type == QLatin1String("baseplate")) return tr("Plain baseplate");
    return tr("Custom");
}

QString ModuleSetupText::typeHint(const QString& type) {
    if (type == QLatin1String("mils")) return tr("A MILS module: its own legs or table, to the MILS height.");
    if (type == QLatin1String("baseplate")) return tr("Loose baseplates laid on an ordinary table.");
    return tr("Any other table or stand.");
}

QString ModuleSetupText::sideLabel(const QString& side) {
    if (side == QLatin1String("front")) return tr("At the front");
    if (side == QLatin1String("back")) return tr("At the back");
    if (side == QLatin1String("left")) return tr("On the left");
    return tr("On the right");
}

QString ModuleSetupText::tableText(const core::ModuleTable& t, Unit unit) {
    const QString label = typeLabel(t.type);
    if (!t.heightMm) return label;
    const QString h = formatHeight(*t.heightMm, unit);
    return t.toRail ? tr("%1 · %2 to the rails").arg(label, h) : QStringLiteral("%1 · %2").arg(label, h);
}

QString ModuleSetupText::powerText(const core::ModulePower& p) {
    if (!p.needed) return tr("No power");
    QStringList parts{ tr("%n outlet(s)", nullptr, core::outletsOf(p)) };
    if (p.watts && *p.watts > 0) parts << tr("~%1 W").arg(*p.watts);
    if (!p.plugSide.isEmpty()) {
        if (p.plugSide == QLatin1String("front")) parts << tr("plug at the front");
        else if (p.plugSide == QLatin1String("back")) parts << tr("plug at the back");
        else if (p.plugSide == QLatin1String("left")) parts << tr("plug at the left");
        else parts << tr("plug at the right");
    }
    return tr("Power: %1").arg(parts.join(QStringLiteral(", ")));
}

QString ModuleSetupText::setupLine(const core::ModuleSetup& s, Unit unit) {
    QStringList parts;
    if (s.table) parts << tableText(*s.table, unit);
    if (s.power) parts << powerText(*s.power);
    return parts.join(QStringLiteral(" · "));
}

QString ModuleSetupText::summaryPower(const core::SetupSummary& s) {
    const QString notSet = s.powerNotSet ? QStringLiteral(" ") + tr("(not set for %1)").arg(s.powerNotSet) : QString();
    if (s.powerModules == 0)
        return s.noPower ? tr("Power: none needed") + notSet : tr("Power: not set for any module");
    const QString watts = s.watts > 0 ? tr(", ~%1 W").arg(s.watts) : QString();
    return tr("Power: %n module(s) need %1", nullptr, s.powerModules).arg(tr("%n outlet(s)", nullptr, s.outlets)) + watts + notSet;
}

QStringList ModuleSetupText::summaryTables(const core::SetupSummary& s, Unit unit) {
    QStringList lines;
    for (const auto& g : s.tables) {
        QStringList heights;
        for (const auto& h : g.heights) {
            const QString v = formatHeight(h.heightMm, unit);
            heights << QStringLiteral("%1 (%2)").arg(h.toRail ? tr("%1 to the rails").arg(v) : v).arg(h.count);
        }
        QString line = QStringLiteral("%1: %2").arg(typeLabel(g.type)).arg(g.count);
        if (!heights.isEmpty()) line += QStringLiteral(" — ") + heights.join(QStringLiteral(", "));
        lines << line;
    }
    if (s.tablesNotSet && !s.tables.empty()) lines << tr("Table not set: %1").arg(s.tablesNotSet);
    return lines;
}

}  // namespace bld::ui
