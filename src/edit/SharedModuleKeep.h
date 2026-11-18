#pragma once

// Updates that keep the ids of unchanged parts (docs/shared-modules.md,
// phase 3): another version of a library module goes in place of a placed
// module's parts, changing only the parts that differ. The rule is the
// web's (packages/modules keepIds.ts), which the server applies live
// updates with; shared vectors in fixtures/contracts/shared-module-keep.json.
//
// 1. Match: each of the new version's parts (batch by batch, part by part,
//    already placed, with the sheet it goes on) takes the first of the
//    module's parts on the map (sheets in layer order, each bottom to top)
//    not taken yet that is on that sheet, with the same part number (any
//    case) and height, its pivot within 0.1 studs and its turn within 0.5°.
// 2. Order: on each sheet the matched parts must be in the version's
//    order; the longest run in order stays matched (the same picks as
//    doc-shape z order).
// 3. Sets: a matched part's sets must be the version part's one for one;
//    when any pair disagrees, nothing is matched.
// 4. Change: unmatched parts go; unmatched version parts come in new, each
//    just above the module part before it in the version's order on its
//    sheet, else just below the next one, else on top. Matched parts
//    aren't touched.

#include "ModuleCommands.h"

#include "../core/Brick.h"
#include "../core/Group.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

using LayerBatches = std::vector<ImportBbmAsModuleCommand::LayerBatch>;

// A parts sheet as the plan reads it: its parts bottom to top, and its sets.
struct KeepSheet {
    QString id;
    std::vector<core::Brick> parts;
    std::vector<core::Group> groups;
};

struct KeepPlan {
    QHash<QString, QString> kept;            // version part "b.i" -> the id that stays
    QStringList removed;                     // module parts that go
    QStringList added;                       // version parts that come in new ("b.i"), in order
    QMap<QString, QStringList> sheets;       // each sheet's final order: ids, and "new:b.i"
    QHash<QString, QString> groupMap;        // version set "b/<id>" -> the set on the map
    QHash<QString, QStringList> newGroups;   // each new part's sets, innermost first: an id, or "new:b/<id>"
};

// The longest strictly increasing run of `seq`, as indexes (assignZ's picks).
QSet<int> longestRun(const std::vector<int>& seq);

// The plan for `next` (placed, each batch with its targetLayerGuid) going in
// place of the parts `members` on `sheets`.
KeepPlan planKeep(const std::vector<KeepSheet>& sheets, const QSet<QString>& members, const LayerBatches& next,
                  parts::PartsLibrary& parts);

// The layout's parts sheets, as planKeep reads them.
std::vector<KeepSheet> keepSheets(const core::Map& map);

// Carry out `plan` on `map`: the parts that go are taken out (and the sets
// only they were in), the new ones go in where the plan says, under fresh
// ids, in their sets. Returns the module's parts in the version's order.
QStringList applyKeep(core::Map& map, const KeepPlan& plan, const LayerBatches& next);

}  // namespace bld::edit
