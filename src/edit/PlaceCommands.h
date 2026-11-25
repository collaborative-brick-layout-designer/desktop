#pragma once

// Places (Live Ops design 2.1; core/Places.h): add, change or delete one
// place of the layout's own or of a module's, as one undo step.

#include "../core/Map.h"

#include <QJsonObject>
#include <QString>
#include <QUndoCommand>

#include <optional>

namespace bld::edit {

// The layout's own places (`owner` empty) or a module's; null when that module isn't there.
QJsonArray* placesOf(core::Map& map, const QString& owner);
QJsonArray placesOf(const core::Map& map, const QString& owner);

class SetPlaceCommand : public QUndoCommand {
public:
    // `after` unset: delete.
    SetPlaceCommand(core::Map& map, QString owner, const QString& id, std::optional<QJsonObject> after, const QString& text,
                    QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void put(const std::optional<QJsonObject>& place);
    core::Map& map_;
    QString owner_;
    QString id_;
    std::optional<QJsonObject> before_;
    std::optional<QJsonObject> after_;
};

}  // namespace bld::edit
