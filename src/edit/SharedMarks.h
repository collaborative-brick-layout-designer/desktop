#pragma once

// What an update of a shared module left to look at (docs/shared-modules.md,
// "Collisions after an update"): an update never waits for room, so the
// module then says "overlaps 2 parts" or "2 track ends no longer meet".
// Worked out here from the layout as it is; nothing is stored.

#include <QSet>
#include <QString>

namespace bld::core { class Map; struct Module; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

struct SharedMarks {
    int overlaps = 0;   // parts outside the module, on a sheet it has parts on, that one of its parts lies over
    int looseEnds = 0;  // its free track ends with someone else's free end close by, not joined
    bool any() const { return overlaps > 0 || looseEnds > 0; }
    bool operator==(const SharedMarks&) const = default;
};

// The marks of the module with these parts. (Connections as
// edit::rebuildConnectivity left them.)
SharedMarks sharedMarks(const core::Map& map, const QSet<QString>& members, parts::PartsLibrary& parts);

}  // namespace bld::edit
