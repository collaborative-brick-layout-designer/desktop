#pragma once

// Finishing an undo or redo for a shared module (docs/shared-modules.md,
// phase 3; doc shape 5). The server keeps a shared module's unchanged parts
// when it puts a new version in (SharedModuleKeep.h), so an earlier undo
// step can point at some of its parts and not the new ones. Right after an
// undo or redo, in the same change, the module is finished whole:
//
//   0. a part one of its updates took out (`shared.retired`) that the step
//      brought back goes again (undoing a change of sheet made before the
//      update brings back what it took off the old sheet: settleRetired);
//   1. its record went: every part of it still on the map goes too;
//   2. some of its parts went, its record didn't: the rest go, and the
//      record (the module removed whole, as the undo meant);
//   3. some of its parts moved (place, turn or sheet), the rest didn't: the
//      rest move the same way, turned about their pivots and moved by the
//      shift taken from the first part that moved, onto the sheet the moved
//      parts went to from theirs.
//
// The web's rule (packages/modules settle.ts); shared vectors in
// fixtures/contracts/shared-module-settle.json.

#include "../core/SettledBack.h"

#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

struct SettlePart {
    QString id;
    QString sheet;
    QString partNumber;
    QRectF displayArea;
    double orientation = 0.0;
};

struct SettleSide {
    bool record = false;
    std::vector<SettlePart> parts;  // before: in the module's order
};

struct Settled {
    QStringList remove;
    bool removeModule = false;
    std::vector<SettlePart> move;
    bool empty() const { return remove.isEmpty() && !removeModule && move.empty(); }
};

Settled settleShared(const SettleSide& before, const SettleSide& after, parts::PartsLibrary& parts);

// Rule 0: the parts the step brought back (in `after`, in its order, not in
// `before`) that are in `retired` (every shared module's together).
QStringList settleRetired(const QSet<QString>& retired, const QSet<QString>& before, const QStringList& after);

// Finish the step that took the layout from `before` to `after`, for every
// shared module `before` had: changes `after`, and keeps the placement of
// a module it moved in step. What it took away goes in `removed`. Returns
// whether it changed anything.
bool finishShared(const core::Map& before, core::Map& after, parts::PartsLibrary& parts, core::SettledBack* removed);

}  // namespace bld::edit
