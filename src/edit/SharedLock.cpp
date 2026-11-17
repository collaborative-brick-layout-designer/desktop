#include "SharedLock.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/ModuleEdit.h"
#include "../parts/BrickPlacement.h"

#include <QUndoCommand>

#include <cmath>

namespace bld::edit {

namespace {

struct Found {
    const core::Brick* brick = nullptr;
    QString layerGuid;
};

// The parts `wanted` names, where they are now.
QHash<QString, Found> findParts(const core::Map& map, const QSet<QString>& wanted) {
    QHash<QString, Found> out;
    if (wanted.isEmpty()) return out;
    out.reserve(wanted.size());
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (wanted.contains(b.guid)) out.insert(b.guid, { &b, L->guid });
    }
    return out;
}

double turnGap(double a, double b) {
    const double d = std::abs(core::norm360(a) - core::norm360(b));
    return std::min(d, 360.0 - d);
}

}  // namespace

bool exemptFromSharedLock(const QUndoCommand* cmd) {
    if (!cmd) return false;
    if (dynamic_cast<const SharedLockExempt*>(cmd)) return true;
    // A macro of nothing but exempt steps.
    if (cmd->childCount() == 0) return false;
    for (int i = 0; i < cmd->childCount(); ++i)
        if (!exemptFromSharedLock(cmd->child(i))) return false;
    return true;
}

void SharedLockState::rebase(const core::Map& map, parts::PartsLibrary& parts) {
    modules_.clear();
    const auto& modules = map.sidecar.modules;
    QSet<QString> wanted;
    for (const auto& m : modules)
        if (m.shared)
            if (auto s = core::sharedPlaced(m)) {
                wanted.unite(s->members);
                modules_.push_back({ m, std::move(*s), {}, {} });
            }
    if (modules_.empty()) return;
    const auto found = findParts(map, wanted);
    for (auto& k : modules_) {
        for (const auto& id : k.shared.members) {
            const auto it = found.constFind(id);
            if (it == found.constEnd()) continue;
            const core::Brick& b = *it->brick;
            k.parts.insert(id, { b.partNumber, b.altitude, b.orientation, parts::placement::imageCentre(b, parts),
                                 it->layerGuid });
        }
        for (const auto& m : modules)
            if (m.id != k.module.id && m.memberIds.intersects(k.shared.members)) k.overlapping.insert(m.id);
    }
}

std::optional<SharedLockState::Violation> SharedLockState::check(const core::Map& now) const {
    if (modules_.empty()) return std::nullopt;
    QSet<QString> wanted;
    for (const auto& k : modules_)
        for (auto it = k.parts.constBegin(); it != k.parts.constEnd(); ++it) wanted.insert(it.key());
    const auto found = findParts(now, wanted);
    for (const auto& k : modules_) {
        const core::Module* m = core::findModule(now.sidecar.modules, k.module.id);
        if (!m) {
            // Removed: with every part, or not at all.
            for (auto it = k.parts.constBegin(); it != k.parts.constEnd(); ++it)
                if (found.contains(it.key())) return Violation{ k.shared, core::LockedAction::Dissolve };
            continue;
        }
        const auto broken = core::keepsLock(k.module, *m);
        if (broken == core::LockedAction::Dissolve) return Violation{ k.shared, *broken };
        // A part deleted (it leaves the module's list too): its parts can't be changed.
        for (auto it = k.parts.constBegin(); it != k.parts.constEnd(); ++it)
            if (!found.contains(it.key())) return Violation{ k.shared, core::LockedAction::Parts };
        if (broken) return Violation{ k.shared, *broken };
        for (auto it = k.parts.constBegin(); it != k.parts.constEnd(); ++it) {
            const auto f = found.constFind(it.key());
            const core::Brick& b = *f->brick;
            if (b.partNumber != it->partNumber || b.altitude != it->altitude)
                return Violation{ k.shared, core::LockedAction::Parts };
        }
        for (const auto& other : now.sidecar.modules)
            if (other.id != m->id && !k.overlapping.contains(other.id) && other.memberIds.intersects(k.shared.members))
                return Violation{ k.shared, core::LockedAction::Members };
    }
    return std::nullopt;
}

bool SharedLockState::keepPlacements(core::Map& now, parts::PartsLibrary& parts) const {
    bool changed = false;
    for (const auto& k : modules_) {
        if (!k.shared.placement || k.parts.isEmpty()) continue;
        core::Module* m = nullptr;
        for (auto& x : now.sidecar.modules)
            if (x.id == k.module.id) m = &x;
        if (!m || !m->shared) continue;
        // (Someone else's write, or an update, put another placement in: theirs stands.)
        const auto cur = core::readPlacement(m->shared->value(QLatin1String("placement")));
        if (!cur || !(*cur == *k.shared.placement)) continue;
        const auto found = findParts(now, k.shared.members);
        if (found.size() != k.parts.size()) continue;

        // The move of the first part (by id, so every app picks the same)...
        QStringList ids = k.parts.keys();
        ids.sort();
        const Part& was0 = k.parts[ids.first()];
        const core::Brick& now0 = *found[ids.first()].brick;
        const double turn = core::norm360(now0.orientation - was0.orientation);
        const QPointF to0 = parts::placement::imageCentre(now0, parts);
        // ...is the whole module's when every other part made it too.
        bool rigid = true;
        QString oneSheet = found[ids.first()].layerGuid;
        bool moved = turnGap(turn, 0.0) > 1e-6 || std::hypot(to0.x() - was0.pivot.x(), to0.y() - was0.pivot.y()) > 1e-6;
        for (const QString& id : ids) {
            const Part& was = k.parts[id];
            const Found& f = found[id];
            if (f.layerGuid != oneSheet) oneSheet.clear();
            if (turnGap(f.brick->orientation - was.orientation, turn) > 0.01) rigid = false;
            const QPointF want = to0 + parts::placement::rotated(was.pivot - was0.pivot, turn);
            const QPointF got = parts::placement::imageCentre(*f.brick, parts);
            if (std::hypot(want.x() - got.x(), want.y() - got.y()) > 0.01) rigid = false;
            if (!rigid) break;
        }
        core::SharedPlacement next = *cur;
        if (rigid && moved)
            next = core::movedPlacement(core::turnedPlacement(next, turn, was0.pivot), to0 - was0.pivot);
        // On one sheet now, and not the one(s) it was on: every sheet of the version goes there.
        if (rigid && !oneSheet.isEmpty() && next.sheets) {
            bool sheetChanged = false;
            for (const QString& id : ids)
                if (k.parts[id].layerGuid != oneSheet) sheetChanged = true;
            if (sheetChanged)
                for (auto it = next.sheets->begin(); it != next.sheets->end(); ++it) it.value() = oneSheet;
        }
        if (next == *cur) continue;
        *m = core::withPlacement(*m, next);
        changed = true;
    }
    return changed;
}

}  // namespace bld::edit
