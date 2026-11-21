#include "SwitchCommands.h"

#include "../core/LayerBrick.h"

#include <QHash>

#include <algorithm>
#include <QSet>

namespace bld::edit {

namespace {

QSet<QString> brickIds(const core::Map& map) {
    QSet<QString> out;
    for (const auto& layer : map.layers())
        if (layer && layer->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) out.insert(b.guid);
    return out;
}

}  // namespace

SetSwitchStatesCommand::SetSwitchStatesCommand(core::Map& map, const QMap<QString, std::optional<int>>& changes,
                                               const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map), before_(map.sidecar.switchStates) {
    const QSet<QString> present = brickIds(map);
    for (auto it = before_.constBegin(); it != before_.constEnd(); ++it) {
        const qsizetype at = it.key().lastIndexOf(QLatin1Char('/'));
        if (at > 0 && present.contains(it.key().left(at))) after_.insert(it.key(), it.value());
    }
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const std::optional<int>& exit = it.value();
        if (exit.has_value()) after_.insert(it.key(), exit.value());
        else after_.remove(it.key());
    }
    if (after_ == before_) setObsolete(true);
}

void SetSwitchStatesCommand::redo() { map_.sidecar.switchStates = after_; }

void SetSwitchStatesCommand::undo() { map_.sidecar.switchStates = before_; }

SetRouteCommand::SetRouteCommand(core::Map& map, const QString& id, std::optional<core::Route> after, const QString& text,
                                 QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map), id_(id), after_(std::move(after)) {
    auto& routes = map_.sidecar.routes;
    for (size_t i = 0; i < routes.size(); ++i)
        if (routes[i].id == id_) {
            before_ = routes[i];
            index_ = static_cast<int>(i);
        }
    if (before_ == after_) setObsolete(true);
}

void SetRouteCommand::put(const std::optional<core::Route>& r) {
    auto& routes = map_.sidecar.routes;
    const auto it = std::find_if(routes.begin(), routes.end(), [&](const core::Route& x) { return x.id == id_; });
    if (!r) {
        if (it != routes.end()) routes.erase(it);
        return;
    }
    if (it != routes.end()) *it = *r;
    else if (index_ >= 0 && index_ <= static_cast<int>(routes.size())) routes.insert(routes.begin() + index_, *r);
    else routes.push_back(*r);
}

void SetRouteCommand::redo() { put(after_); }

void SetRouteCommand::undo() { put(before_); }

QMap<QString, int> withCopiedSwitchStates(const QMap<QString, int>& states, const QStringList& from, const QStringList& to) {
    if (states.isEmpty()) return states;
    QHash<QString, QString> newId;
    for (qsizetype i = 0; i < from.size() && i < to.size(); ++i)
        if (!from[i].isEmpty() && !to[i].isEmpty()) newId.insert(from[i], to[i]);
    QMap<QString, int> out = states;
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const qsizetype at = it.key().lastIndexOf(QLatin1Char('/'));
        const auto copy = newId.constFind(it.key().left(at));
        if (copy != newId.constEnd()) out.insert(*copy + it.key().mid(at), it.value());
    }
    return out;
}

}  // namespace bld::edit
