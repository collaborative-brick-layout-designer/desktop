#pragma once

// Rolling stock and trains (trains design 2.3, 2.4, 6.4): add, change or
// delete one car or one train, each one undo step. Deleting a car takes it
// out of every train it's in, in the same step (the last car out takes the
// train off the track). New records go in id order, as the web lists them.

#include "../core/Map.h"

#include <QString>
#include <QUndoCommand>

#include <optional>

namespace bld::edit {

class SetStockCommand : public QUndoCommand {
public:
    SetStockCommand(core::Map& map, const QString& id, std::optional<core::RollingStock> after, const QString& text,
                    QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    QString id_;
    std::optional<core::RollingStock> before_;
    std::optional<core::RollingStock> after_;
};

class SetTrainCommand : public QUndoCommand {
public:
    SetTrainCommand(core::Map& map, const QString& id, std::optional<core::Train> after, const QString& text,
                    QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    QString id_;
    std::optional<core::Train> before_;
    std::optional<core::Train> after_;
};

// The layout's train scale (trains design 5), 1:N; none: 1:38. One undo step.
class SetTrainScaleCommand : public QUndoCommand {
public:
    SetTrainScaleCommand(core::Map& map, std::optional<int> after, const QString& text, QUndoCommand* parent = nullptr);
    void undo() override { map_.sidecar.trainScale = before_; }
    void redo() override { map_.sidecar.trainScale = after_; }

private:
    core::Map& map_;
    std::optional<int> before_;
    std::optional<int> after_;
};

// Delete a car and take it out of its trains: one undo step.
QUndoCommand* makeRemoveStockCommand(core::Map& map, const QString& id, const QString& text);

}  // namespace bld::edit
