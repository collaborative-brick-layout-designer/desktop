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

}  // namespace bld::ui
