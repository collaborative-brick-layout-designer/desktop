#pragma once

// What finishing an undo for a shared module took away (edit/SharedSettle.h):
// kept with the step, so redoing it puts them back (sync/SyncDoc.h).

#include "Brick.h"
#include "LayerBrick.h"
#include "Map.h"
#include "Module.h"

#include <QString>

#include <utility>
#include <vector>

namespace bld::core {

struct SettledBack {
    std::vector<std::pair<QString, Brick>> bricks;  // the sheet each was on, and the part
    std::vector<Module> modules;                    // module records taken away
    bool empty() const { return bricks.empty() && modules.empty(); }
};

// Put them back: each part on its sheet (on top; skipped when that id is
// there already), each module record as it was then (in place of the one
// there, which the redo may have brought back from before the update).
inline void putBack(Map& map, const SettledBack& back) {
    for (const auto& [sheet, brick] : back.bricks) {
        bool there = false;
        LayerBrick* to = nullptr;
        for (const auto& L : map.layers()) {
            if (!L || L->kind() != LayerKind::Brick) continue;
            auto& layer = static_cast<LayerBrick&>(*L);
            for (const auto& b : layer.bricks)
                if (b.guid == brick.guid) there = true;
            if (L->guid == sheet) to = &layer;
        }
        if (!there && to) to->bricks.push_back(brick);
    }
    for (const auto& m : back.modules) {
        bool there = false;
        for (auto& had : map.sidecar.modules)
            if (had.id == m.id) {
                had = m;
                there = true;
            }
        if (!there) map.sidecar.modules.push_back(m);
    }
}

}  // namespace bld::core
