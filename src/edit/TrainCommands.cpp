#include "TrainCommands.h"

#include <algorithm>

namespace bld::edit {

namespace {

// Put `r` in place of the record with `id` (or add it in id order); none deletes it.
template <typename T>
void put(std::vector<T>& list, const QString& id, const std::optional<T>& r) {
    const auto it = std::find_if(list.begin(), list.end(), [&](const T& x) { return x.id == id; });
    if (!r) {
        if (it != list.end()) list.erase(it);
        return;
    }
    if (it != list.end()) {
        *it = *r;
        return;
    }
    const auto at = std::find_if(list.begin(), list.end(), [&](const T& x) { return id < x.id; });
    list.insert(at, *r);
}

template <typename T>
std::optional<T> find(const std::vector<T>& list, const QString& id) {
    for (const auto& x : list)
        if (x.id == id) return x;
    return std::nullopt;
}

}  // namespace

SetStockCommand::SetStockCommand(core::Map& map, const QString& id, std::optional<core::RollingStock> after, const QString& text,
                                 QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map), id_(id), before_(find(map.sidecar.rollingStock, id)), after_(std::move(after)) {
    if (before_ == after_) setObsolete(true);
}

void SetStockCommand::redo() { put(map_.sidecar.rollingStock, id_, after_); }
void SetStockCommand::undo() { put(map_.sidecar.rollingStock, id_, before_); }

SetTrainCommand::SetTrainCommand(core::Map& map, const QString& id, std::optional<core::Train> after, const QString& text,
                                 QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map), id_(id), before_(find(map.sidecar.trains, id)), after_(std::move(after)) {
    if (before_ == after_) setObsolete(true);
}

void SetTrainCommand::redo() { put(map_.sidecar.trains, id_, after_); }
void SetTrainCommand::undo() { put(map_.sidecar.trains, id_, before_); }

QUndoCommand* makeRemoveStockCommand(core::Map& map, const QString& id, const QString& text) {
    auto* parent = new QUndoCommand(text);
    for (const auto& t : map.sidecar.trains) {
        if (std::none_of(t.units.begin(), t.units.end(), [&](const core::TrainUnit& u) { return u.stock == id; })) continue;
        core::Train next = t;
        next.units.erase(std::remove_if(next.units.begin(), next.units.end(), [&](const core::TrainUnit& u) { return u.stock == id; }),
                         next.units.end());
        if (next.units.empty()) next.position.reset();
        new SetTrainCommand(map, t.id, next, text, parent);
    }
    new SetStockCommand(map, id, std::nullopt, text, parent);
    return parent;
}

}  // namespace bld::edit
