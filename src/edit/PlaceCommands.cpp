#include "PlaceCommands.h"

#include "../core/Places.h"

namespace bld::edit {

QJsonArray* placesOf(core::Map& map, const QString& owner) {
    if (owner.isEmpty()) return &map.sidecar.layoutPlaces;
    for (auto& m : map.sidecar.modules)
        if (m.id == owner) return &m.places;
    return nullptr;
}

QJsonArray placesOf(const core::Map& map, const QString& owner) {
    if (owner.isEmpty()) return map.sidecar.layoutPlaces;
    for (const auto& m : map.sidecar.modules)
        if (m.id == owner) return m.places;
    return {};
}

SetPlaceCommand::SetPlaceCommand(core::Map& map, QString owner, const QString& id, std::optional<QJsonObject> after, const QString& text,
                                 QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map), owner_(std::move(owner)), id_(id), after_(std::move(after)) {
    const QJsonArray now = placesOf(std::as_const(map), owner_);
    if (const int i = core::places::indexOf(now, id); i >= 0) before_ = now.at(i).toObject();
}

void SetPlaceCommand::put(const std::optional<QJsonObject>& place) {
    QJsonArray* list = placesOf(map_, owner_);
    if (!list) return;
    *list = place ? core::places::withPlace(*list, *place) : core::places::without(*list, id_);
}

void SetPlaceCommand::redo() { put(after_); }
void SetPlaceCommand::undo() { put(before_); }

}  // namespace bld::edit
