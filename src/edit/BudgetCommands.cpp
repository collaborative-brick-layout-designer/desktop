#include "BudgetCommands.h"

#include "../core/Map.h"
#include "../core/Sidecar.h"

namespace bld::edit {

SetBudgetCommand::SetBudgetCommand(core::Map& map, std::optional<QMap<QString, int>> next, const QString& text,
                                   QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map), before_(map.sidecar.budgetLimits), after_(std::move(next)) {}

void SetBudgetCommand::redo() { map_.sidecar.budgetLimits = after_; }
void SetBudgetCommand::undo() { map_.sidecar.budgetLimits = before_; }

}  // namespace bld::edit
