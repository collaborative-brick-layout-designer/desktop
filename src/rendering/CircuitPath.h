#pragma once

// The centreline through a part moved to the track library
// (track/CircuitPath.h), where the track network uses it too; the electric
// overlay keeps its names here.

#include "../track/CircuitPath.h"

namespace bld::rendering {

using track::CircuitPathPoint;
using track::circuitPath;
using track::offsetPath;
using track::offsetPathBetween;
using track::pointAt;
using track::kCircuitRailOffset;
using track::kCircuitPenWidth;
using track::kCircuitCutterGap;
using track::kCircuitCutterPen;
using track::kCircuitShortcutSize;

}  // namespace bld::rendering
