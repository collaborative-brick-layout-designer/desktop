#pragma once

// Which sheet new parts go on: the active sheet when it holds parts, else
// the first parts sheet (BlueBrick's selectedLayerIndex, with the fallback
// MapView::addPartAtScenePos uses). Shared by placing, the drop hint, the
// "Adding to" button and the Parts panel's title, so they always agree.

#include "../core/Layer.h"
#include "../core/Map.h"

namespace bld::ui {

// The index of the sheet a placed part goes on, or -1 when there is no parts sheet.
inline int placementLayer(const core::Map& map) {
    const auto& layers = map.layers();
    const int n = static_cast<int>(layers.size());
    const int active = map.selectedLayerIndex;
    if (active >= 0 && active < n && layers[active] && layers[active]->kind() == core::LayerKind::Brick)
        return active;
    for (int i = 0; i < n; ++i)
        if (layers[i] && layers[i]->kind() == core::LayerKind::Brick) return i;
    return -1;
}

// The sheet a file opens on, the same rule as the web: the sheet saved as
// selected when it holds parts, else the topmost parts sheet. -1 when the
// map has no parts sheet (the saved one is kept then).
inline int startingSheet(const core::Map& map) {
    const auto& layers = map.layers();
    const int n = static_cast<int>(layers.size());
    const int saved = map.selectedLayerIndex;
    if (saved >= 0 && saved < n && layers[saved] && layers[saved]->kind() == core::LayerKind::Brick) return saved;
    for (int i = n - 1; i >= 0; --i)
        if (layers[i] && layers[i]->kind() == core::LayerKind::Brick) return i;
    return -1;
}

}  // namespace bld::ui
