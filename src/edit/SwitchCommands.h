#pragma once

// Setting switches (trains design 2.2, 6.3): one undo step, in Run mode
// too (Aaron, 2026-11-19). The settings live in the sidecar's
// switchStates ("<brick>/<heel>" -> exit); a switch set back to its
// default loses its key. Keys of parts no longer on the map are dropped as
// it writes, as the web does.

#include "../core/Map.h"

#include <QMap>
#include <QString>
#include <QUndoCommand>

#include <optional>

namespace bld::edit {

class SetSwitchStatesCommand : public QUndoCommand {
public:
    // `changes`: key -> exit, or nullopt to put the switch back to its default.
    SetSwitchStatesCommand(core::Map& map, const QMap<QString, std::optional<int>>& changes, const QString& text,
                           QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    QMap<QString, int> before_;
    QMap<QString, int> after_;
};

// Copies of parts keep their originals' switch settings: `from[i]` was
// copied as `to[i]`. Returns the settings with the copies' keys added.
QMap<QString, int> withCopiedSwitchStates(const QMap<QString, int>& states, const QStringList& from, const QStringList& to);

}  // namespace bld::edit
