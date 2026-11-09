#include "Map.h"

#include "Layer.h"

namespace bld::core {

Map::Map() = default;
Map::~Map() = default;
Map::Map(Map&&) noexcept = default;
Map& Map::operator=(Map&&) noexcept = default;

int Map::selectedLayerIndexToSave() const {
    if (!fileSelection) return selectedLayerIndex;
    const int n = static_cast<int>(layers_.size());
    const FileSelection& f = *fileSelection;
    // The saved index named no sheet: keep it while it still names none.
    if (!f.layer) return f.index < 0 || f.index >= n ? f.index : selectedLayerIndex;
    for (int i = 0; i < n; ++i) {
        const Layer* l = layers_[i].get();
        if (l && (f.guid.isEmpty() ? l == f.layer : l->guid == f.guid)) return i;
    }
    return selectedLayerIndex;  // that sheet was deleted
}

}
