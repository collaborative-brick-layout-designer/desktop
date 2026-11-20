#pragma once

// A module's table and power: what it stands on and what it needs from the
// wall, so a layout coordinator can plan tables and power-strip runs. Store
// and show only: no riser math, no derived heights, no warnings.
//
// A placed module keeps them in the sidecar (BlueBrick never sees them):
//
//   table: { type: "mils" | "baseplate" | "custom",
//            heightMm?: number,            // what was entered, in mm
//            measuredTo?: "rail" }          // absent: the table top
//   power: { needed: bool,
//            outlets?: int,                 // when needed (absent: 1)
//            watts?: number,                // rough, when needed
//            plugSide?: "front" | "back" | "left" | "right" }
//
// A library module keeps the same two on the server (its `setup`), so they
// travel with it: adding it to a layout, or updating a placed copy, copies
// them onto the placed module. The web's packages/modules setup.ts is the
// twin of this file; both tests read fixtures/contracts/module-setup.json.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace bld::core {

struct Module;

constexpr double kMaxHeightMm = 5000.0;
constexpr int kMaxOutlets = 50;
constexpr double kMaxWatts = 100000.0;

// "mils", "baseplate", "custom", in the order lists show them.
const QStringList& tableTypes();
// "front", "back", "left", "right".
const QStringList& plugSides();

struct ModuleTable {
    QString type;                    // one of tableTypes()
    std::optional<double> heightMm;  // what was entered
    bool toRail = false;             // measured to the top of the rails, not the table top
    bool operator==(const ModuleTable&) const = default;
};

struct ModulePower {
    bool needed = false;
    std::optional<int> outlets;   // when needed; absent counts as one
    std::optional<int> watts;     // rough
    QString plugSide;             // empty: not said
    bool operator==(const ModulePower&) const = default;
};

struct ModuleSetup {
    std::optional<ModuleTable> table;
    std::optional<ModulePower> power;
    bool empty() const { return !table && !power; }
    bool operator==(const ModuleSetup&) const = default;
};

// The table / power as stored, or nullopt when it isn't one this build can read.
std::optional<ModuleTable> readTable(const QJsonValue& v);
std::optional<ModulePower> readPower(const QJsonValue& v);

// {table?, power?} as stored (and sent to the server).
QJsonObject tableJson(const ModuleTable& t);
QJsonObject powerJson(const ModulePower& p);
QJsonObject setupJson(const ModuleSetup& s);

// The setup an object with `table` / `power` holds (a sidecar module, the server's `setup`).
ModuleSetup setupFromJson(const QJsonObject& o);
// A placed module's.
ModuleSetup setupOf(const Module& m);

// Whether `v` may be stored as a library module's setup (the server's rule):
// null, or an object with only `table` and `power`, each absent, null or well formed.
bool validSetup(const QJsonValue& v);

// `m` with its table and power set (an empty part: not set).
Module withSetup(Module m, const ModuleSetup& s);
// A placed module given a library module's setup: what the library says
// replaces what it had; a library module with none set leaves the placed module's own.
Module withLibrarySetup(Module m, const ModuleSetup& library);

// How many outlets a module needing power uses: what it says, else one.
int outletsOf(const ModulePower& p);

// ---- The layout's summary, for its coordinator ---------------------------

struct HeightCount {
    double heightMm = 0;
    bool toRail = false;
    int count = 0;
};

struct TableGroup {
    QString type;
    int count = 0;
    std::vector<HeightCount> heights;  // lowest first, table tops before rails
};

struct SetupSummary {
    int modules = 0;
    int powerModules = 0;
    int outlets = 0;
    double watts = 0;
    int noPower = 0;
    int powerNotSet = 0;
    std::vector<TableGroup> tables;  // in tableTypes() order, used ones only
    int tablesNotSet = 0;
};

SetupSummary setupSummary(const std::vector<ModuleSetup>& modules);
SetupSummary setupSummary(const std::vector<Module>& modules);

}  // namespace bld::core
