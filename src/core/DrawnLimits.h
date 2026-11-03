#pragma once

// The ranges of the settings the map is drawn from (grid, area, ruler,
// every sheet's own, and the anchored labels'), kept when a layout is read:
// from a .bbm or .bld-layout (saveload::readBbm, and the labels wherever a
// sidecar is read: saveload::sidecarFromJson) and from a live layout's
// document (sync::mapFromDocJson). A value out of range is brought to the
// nearest end; one that isn't a number (NaN, infinite) gets the default.
// Nothing a layout made in either app holds is changed.
//
// Every range is inside what the server takes (fixtures/contracts/
// update-guard.json › drawingSettings.limits, checked by ContractsTest), so
// a value brought into range here, and written back to a live layout with
// the next edit, is never refused. (The server doesn't check the labels' at
// all; the web draws their font sizes in the same range, labelFontPx.)

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

namespace bld::core {

class Map;
struct AnchoredLabel;

namespace limits {

template <typename T> struct Range {
    T min, max, fallback;
};

// Every sheet.
inline constexpr Range<int> kTransparency{ 0, 100, 100 }; // percent
inline constexpr Range<int> kHullThickness{ 1, 100, 1 };  // px
// Grid.
inline constexpr Range<int> kGridSizeInStud{ 1, std::numeric_limits<int>::max(), 32 };
inline constexpr Range<int> kSubDivisionNumber{ 2, 100, 4 };
inline constexpr Range<float> kGridThickness{ 0.0f, 100.0f, 2.0f };    // px
inline constexpr Range<float> kSubGridThickness{ 0.0f, 100.0f, 1.0f }; // px
inline constexpr Range<int> kCellIndexCorner{ -1000000, 1000000, 0 };  // cells, x and y
// Cell index types: 0 letters, 1 numbers; anything else gets the default
// (columns letters, rows numbers).
// Area.
inline constexpr Range<int> kAreaCellSizeInStud{ 1, 1024, 32 };
// Rulers.
inline constexpr Range<float> kLineThickness{ 0.0f, 100.0f, 1.0f };      // px
inline constexpr Range<float> kGuidelineThickness{ 0.0f, 100.0f, 1.0f }; // px
// Guideline dash pattern: at most 64 entries, each 0 to 1000, adding up to
// 0.01 or more; any other pattern is dropped (a solid guideline).
inline constexpr int kMaxDashEntries = 64;
inline constexpr float kMaxDash = 1000.0f;
inline constexpr double kMinDashTotal = 0.01;
// Unit: 0 studs, 1 LDU, 2 tracks, 3 modules, 4 meters, 5 feet; else studs.
inline constexpr Range<int> kRulerUnit{ 0, 5, 0 };
inline constexpr Range<double> kRulerCoordinate{ -1e7, 1e7, 0.0 }; // studs: points, centre, offset
inline constexpr Range<float> kRulerRadius{ 0.0f, 1e7f, 0.0f };    // studs
// Fonts of the grid's cell index, the rulers' measure and the anchored
// labels, in points.
inline constexpr Range<float> kFontSize{ 1.0f, 500.0f, 8.25f };
// Anchored labels: where they sit (studs, x and y) as rulers' points; a turn
// past ten full ones either way is taken round to under one (it draws the
// same); the zoom below which one hides, 0 for always shown.
inline constexpr Range<double> kLabelOffset{ -1e7, 1e7, 0.0 };
inline constexpr float kMaxLabelTurn = 3600.0f; // degrees
inline constexpr Range<double> kLabelMinZoom{ 0.0, 1e6, 0.0 };

// `v` in range: the nearest end, or the default when it isn't a number.
template <typename T> T inRange(T v, const Range<T>& r) {
    if constexpr (std::is_floating_point_v<T>)
        if (!std::isfinite(v)) return r.fallback;
    return std::clamp(v, r.min, r.max);
}

} // namespace limits

// Brings every drawn setting of `map` into its range (see above).
void keepDrawnSettingsInRange(Map& map);

// The same for one anchored label: its font size, offset, turn and zoom
// cutoff.
void keepLabelInRange(AnchoredLabel& label);

// A dash pattern typed in the ruler dialog, as the server takes it: lengths
// that aren't numbers dropped, each brought to 0..kMaxDash, at most
// kMaxDashEntries of them; one adding up to less than kMinDashTotal is none
// (a solid guideline).
std::vector<float> dashPatternInRange(const std::vector<float>& typed);

} // namespace bld::core
