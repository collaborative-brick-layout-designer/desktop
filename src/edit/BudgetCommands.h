#pragma once

#include <QMap>
#include <QString>
#include <QUndoCommand>

#include <optional>

namespace bld::core { class Map; }

namespace bld::edit {

// Replace the layout's budget (sidecar.budgetLimits) with `next`, or take
// it away with nullopt: one undoable step, which a live layout sends to
// everyone else.
class SetBudgetCommand : public QUndoCommand {
public:
    SetBudgetCommand(core::Map& map, std::optional<QMap<QString, int>> next, const QString& text,
                     QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::optional<QMap<QString, int>> before_;
    std::optional<QMap<QString, int>> after_;
};

}  // namespace bld::edit
