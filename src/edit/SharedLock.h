#pragma once

// The lock on shared (read-only) modules, in the editor
// (docs/shared-modules.md, "Keeping it read-only"; the web keeps the same
// rule in editor/mutations.ts, the server in ws/sharedGuard.ts).
//
// Every edit here is an undo-stack step, made by one of many commands. So
// the rule is kept in one place, after the step: SharedLockState remembers
// the layout's shared modules as they were, and check() says whether the
// layout as it is now still keeps the lock. A step that doesn't is taken
// back by whoever ran it (MapView), with the reason told. The tools ask
// first where they can (core::sharedLockOn), to say why before anything
// moves; this is the net under all of them, undo and redo included.
//
// A shared module keeps: its parts (each one's number, color and height),
// which parts it holds, its name, its link. It may be moved and turned as
// a whole, put on another sheet, pinned, recolored (outline and name),
// and removed with all its parts. Another module can't take its parts.
//
// keepPlacements() is the other half: when a step moved or turned a shared
// module as a whole, its `shared.placement` follows, so its owner's next
// version lands exactly where the module sits.

#include "../core/SharedModule.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QString>

#include <optional>
#include <vector>

class QUndoCommand;

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

// A step that changes a shared module on purpose (its owner's new version
// put in, "Make it mine", and their undo): its command, or the macro
// around it, is one of these, and the lock lets it through.
class SharedLockExempt {
public:
    virtual ~SharedLockExempt() = default;
};

// Whether `cmd` (a step of the undo stack) is exempt.
bool exemptFromSharedLock(const QUndoCommand* cmd);

class SharedLockState {
public:
    // Remember the layout's shared modules as they are now.
    void rebase(const core::Map& map, parts::PartsLibrary& parts);
    void clear() { modules_.clear(); }
    bool empty() const { return modules_.empty(); }

    struct Violation {
        core::SharedPlaced module;
        core::LockedAction what = core::LockedAction::Parts;
        QString message() const { return core::lockMessage(module, what); }
    };
    // What `now` breaks of the lock on the modules remembered; nullopt: nothing.
    std::optional<Violation> check(const core::Map& now) const;

    // For each remembered module whose parts all moved or turned together
    // since: its placement moved and turned the same way, and its sheets
    // when all its parts are on one sheet now. True when a module changed.
    bool keepPlacements(core::Map& now, parts::PartsLibrary& parts) const;

private:
    struct Part {
        QString partNumber;
        float altitude = 0.0f;
        float orientation = 0.0f;
        QPointF pivot;  // its sprite centre
        QString layerGuid;
    };
    struct Kept {
        core::Module module;
        core::SharedPlaced shared;
        QHash<QString, Part> parts;
        // The other modules that already held one of its parts (none, in a layout the apps made).
        QSet<QString> overlapping;
    };
    std::vector<Kept> modules_;
};

}  // namespace bld::edit
