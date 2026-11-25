#include "CutCommands.h"

#include <QDateTime>
#include <QSet>

#include <algorithm>

namespace bld::edit {

namespace {

core::Train* trainOf(core::Map& map, const QString& id) {
    for (auto& t : map.sidecar.trains)
        if (t.id == id) return &t;
    return nullptr;
}

core::RollingStock* carOf(core::Map& map, const QString& id) {
    for (auto& s : map.sidecar.rollingStock)
        if (s.id == id) return &s;
    return nullptr;
}

void add(core::Map& map, const core::Train& t) {
    auto& list = map.sidecar.trains;
    list.insert(std::find_if(list.begin(), list.end(), [&](const core::Train& x) { return t.id < x.id; }), t);
}

}  // namespace

CutCommand::CutCommand(core::Map& map, const track::CutChange& change, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map) {
    const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    const auto standing = track::settleTrains(map.sidecar.trains, track::stockById(map.sidecar));
    const auto was = [&](const QString& id) -> const core::Train* {
        for (const auto& t : standing)
            if (t.id == id) return &t;
        return nullptr;
    };
    for (const auto& t : change.trains) {
        const core::Train* b = was(t.id);
        core::Train before = b ? *b : t;
        if (!b) {
            // A new one: before, it is there with no cars.
            before.units.clear();
            before.emptiedAt = now;
        }
        trains_.push_back({ before, t });
    }
    for (const QString& id : change.emptied)
        if (const core::Train* b = was(id)) {
            core::Train after = *b;
            after.units.clear();
            after.emptiedAt = now;
            trains_.push_back({ *b, after });
        }
    for (auto it = change.cars.constBegin(); it != change.cars.constEnd(); ++it) {
        const core::Train* at = track::standingIn(it.key(), standing);
        cars_.push_back({ it.key(), at ? at->id : QString(), it.value() });
    }
    if (trains_.empty() && cars_.empty()) setObsolete(true);
}

void CutCommand::apply(bool forward, bool force) {
    for (const auto& step : trains_) {
        const core::Train& from = forward ? step.before : step.after;
        const core::Train& to = forward ? step.after : step.before;
        core::Train* t = trainOf(map_, to.id);
        if (!t) {
            // (Deleted by someone else since: an undo doesn't bring it back without its cars.)
            if (!force) continue;
            add(map_, to);
            continue;
        }
        const auto field = [&](auto core::Train::*f) {
            if (force || t->*f == from.*f) t->*f = to.*f;
        };
        field(&core::Train::name);
        field(&core::Train::trackKind);
        field(&core::Train::color);
        field(&core::Train::speed);
        field(&core::Train::units);
        field(&core::Train::position);
        field(&core::Train::over);
        field(&core::Train::emptiedAt);
    }
    for (const auto& c : cars_) {
        core::RollingStock* s = carOf(map_, c.id);
        if (!s) continue;
        const QString& from = forward ? c.before : c.after;
        if (force || s->opsCut.value_or(QString()) == from) s->opsCut = forward ? c.after : c.before;
    }
}

void CutCommand::redo() {
    if (!done_) {
        // Cuts left empty a day ago or more, that no car names, go.
        const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        QSet<QString> named;
        for (const auto& s : map_.sidecar.rollingStock)
            if (s.opsCut) named.insert(*s.opsCut);
        std::erase_if(map_.sidecar.trains, [&](const core::Train& t) {
            return t.cut && t.units.empty() && t.emptiedAt && now - *t.emptiedAt > track::kEmptiedKeepMs && !named.contains(t.id);
        });
    }
    apply(true, !done_);
    done_ = true;
}

void CutCommand::undo() { apply(false, false); }

SetLoadCommand::SetLoadCommand(core::Map& map, const QString& car, const QString& load, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent), map_(map), id_(car), after_(load) {
    const core::RollingStock* s = carOf(map, car);
    if (s) before_ = s->opsLoad;
    if (!s || before_ == after_) setObsolete(true);
}

void SetLoadCommand::set(const QString& from, const QString& to) {
    if (core::RollingStock* s = carOf(map_, id_); s && s->opsLoad == from) s->opsLoad = to;
}

void SetLoadCommand::redo() { set(before_, after_); }

track::CutChange takeCutOff(const core::Sidecar& sidecar, const QString& id) {
    track::CutChange change;
    for (const auto& t : track::standingTrains(sidecar))
        if (t.id == id) {
            for (const auto& u : t.units) change.cars.insert(u.stock, QString());
            change.emptied << id;
        }
    return change;
}

}  // namespace bld::edit
