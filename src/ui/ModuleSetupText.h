#pragma once

// What a module's table and power (core/ModuleSetup.h) read as: in the
// Modules panel, the Module library and the layout's summary for its
// coordinator. Heights are typed and shown in the length unit (inches,
// centimeters or studs). The web's editor/moduleSetupText.ts says the same.

#include "../core/ModuleSetup.h"
#include "../core/Venue.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <optional>

namespace bld::core { class Map; }

namespace bld::ui {

struct ModuleSetupText {
    Q_DECLARE_TR_FUNCTIONS(ModuleSetupText)
public:
    using Unit = core::VenueLengthUnit;

    // The unit heights are shown in: the account's length unit (Settings).
    static Unit heightUnit(const core::Map* map);

    // A height in mm for what someone typed, or nullopt: inches by default in
    // feet and inches (30 3/4, 32", 2' 8"), centimeters for a bare number in
    // metric (78, also 780 mm, 0.78 m), studs in studs.
    static std::optional<double> parseHeightMm(const QString& text, Unit unit);
    // 30¾″ (inches, to the quarter), 78.1 cm, or 97.6 studs.
    static QString formatHeight(double mm, Unit unit);
    // What to type, for the height box.
    static QString unitHint(Unit unit);

    static QString typeLabel(const QString& type);
    static QString typeHint(const QString& type);
    static QString sideLabel(const QString& side);

    // "MILS · 30¾″", "Custom · 32″ to the rails", "Plain baseplate".
    static QString tableText(const core::ModuleTable& t, Unit unit);
    // "No power", "Power: 2 outlets, ~60 W, plug at the back".
    static QString powerText(const core::ModulePower& p);
    // Both, as one line ("" when neither is set).
    static QString setupLine(const core::ModuleSetup& s, Unit unit);

    // "Power: 7 modules need 11 outlets, ~450 W".
    static QString summaryPower(const core::SetupSummary& s);
    // "MILS: 4 — 30¾″ (2), 32″ to the rails (1)", one per type, then "Table not set: 2".
    static QStringList summaryTables(const core::SetupSummary& s, Unit unit);
};

}  // namespace bld::ui
