#pragma once

// Where a library version of a module sits in a layout
// (docs/shared-modules.md; the web's packages/modules placement.ts, which
// the server applies updates with). A placement {degrees, x, y} turns the
// version's own coordinates by `degrees` about its origin, then moves them
// to (x, y); each part turns about its pivot (its sprite centre).
// Shared vectors: fixtures/contracts/shared-module-placement.json.
//
// The desktop puts a version in itself only where the server can't: a
// layout opened from a file, or a live one while offline (planVersion).

#include "ModuleLibraryLink.h"

#include "../core/SharedModule.h"

#include <optional>
#include <vector>

namespace bld::edit {

// The version's parts where `placement` puts them.
LayerBatches placeVersion(LayerBatches version, const core::SharedPlacement& placement, parts::PartsLibrary& parts);

// How many of the placed parts are one of the version's parts where
// `placement` puts it (same part, place and turn, within a hair).
int partsInPlace(const LayerBatches& version, const std::vector<core::Brick>& placed,
                 const core::SharedPlacement& placement, parts::PartsLibrary& parts);

// The placement to use for a placed module: the one the sidecar kept while
// it still fits the parts (at least half of them are where it puts the
// version's), else the one worked out from the parts (an app that doesn't
// keep the placement may have moved the module since).
core::SharedPlacement resolvePlacement(const LayerBatches& version, const std::vector<core::Brick>& placed,
                                       const std::optional<core::SharedPlacement>& kept, parts::PartsLibrary& parts);

// Whether the placed module's parts are exactly the version's: false when
// it was changed in this layout. `placement` is where the version sits
// when known; else it's worked out from the parts.
bool matchesVersion(const LayerBatches& version, const std::vector<core::Brick>& placed, parts::PartsLibrary& parts,
                    const std::optional<core::SharedPlacement>& placement);

// A version ready to go in where the placed module `mod` sits (as the
// server's applyVersion does it): its parts placed, each batch with the
// sheet it goes on (the one the placement kept for it while it's still a
// parts sheet, else the layout's sheet of the same name, else the sheet
// most of the module's parts are on now, else the first parts sheet), and
// the placement to keep with it.
struct PlannedVersion {
    LayerBatches batches;
    core::SharedPlacement placement;  // with `sheets`
};
// `current`: the version the module has now, when known (its parts tell
// where it sits better than the new version's do). nullopt: the layout has
// no parts sheet.
std::optional<PlannedVersion> planVersion(const core::Map& map, const core::Module& mod, LayerBatches next,
                                          const LayerBatches* current, parts::PartsLibrary& parts);

// One placed part of a module moved from where placement `from` has it to
// where `to` has it: about its pivot, its box its footprint at the new angle.
void moveBetweenPlacements(core::Brick& brick, const core::SharedPlacement& from, const core::SharedPlacement& to,
                           parts::PartsLibrary& parts);

}  // namespace bld::edit
