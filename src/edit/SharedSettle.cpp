#include "SharedSettle.h"

#include "SharedLock.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/SharedModule.h"
#include "../parts/BrickPlacement.h"

#include <algorithm>
#include <cmath>

namespace bld::edit {

namespace {

constexpr double kSame = 1e-9;

bool samePose(const SettlePart& a, const SettlePart& b) {
    return a.sheet == b.sheet && std::abs(a.displayArea.x() - b.displayArea.x()) < kSame
           && std::abs(a.displayArea.y() - b.displayArea.y()) < kSame
           && std::abs(a.displayArea.width() - b.displayArea.width()) < kSame
           && std::abs(a.displayArea.height() - b.displayArea.height()) < kSame
           && std::abs(core::norm360(a.orientation) - core::norm360(b.orientation)) < kSame;
}

core::Brick brickOf(const SettlePart& p) {
    core::Brick b;
    b.partNumber = p.partNumber;
    b.displayArea = p.displayArea;
    b.orientation = static_cast<float>(p.orientation);
    return b;
}

QPointF pivotOf(const SettlePart& p, parts::PartsLibrary& parts) { return parts::placement::imageCentre(brickOf(p), parts); }

// The module's parts on `map`, sheets in layer order, each bottom to top.
std::vector<SettlePart> partsOn(const core::Map& map, const QSet<QString>& ids) {
    std::vector<SettlePart> out;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (ids.contains(b.guid)) out.push_back({ b.guid, L->guid, b.partNumber, b.displayArea, b.orientation });
    }
    return out;
}

}  // namespace

Settled settleShared(const SettleSide& before, const SettleSide& after, parts::PartsLibrary& parts) {
    if (!before.record || before.parts.empty()) return {};
    QHash<QString, const SettlePart*> now;
    for (const auto& p : after.parts) now.insert(p.id, &p);
    std::vector<const SettlePart*> alive;
    for (const auto& p : before.parts)
        if (now.contains(p.id)) alive.push_back(&p);
    Settled out;
    // 1. Its record went.
    if (!after.record) {
        for (const auto* p : alive) out.remove << p->id;
        return out;
    }
    // 2. Some of its parts went.
    if (alive.size() < before.parts.size()) {
        for (const auto* p : alive) out.remove << p->id;
        out.removeModule = true;
        return out;
    }
    // 3. Some of its parts moved.
    std::vector<const SettlePart*> moved;
    for (const auto& p : before.parts)
        if (!samePose(p, *now.value(p.id))) moved.push_back(&p);
    if (moved.empty() || moved.size() == before.parts.size()) return {};
    const SettlePart& first = *moved.front();
    const SettlePart& to = *now.value(first.id);
    const double turn = to.orientation - first.orientation;
    const QPointF r = parts::placement::rotated(pivotOf(first, parts), turn);
    const QPointF shift = pivotOf(to, parts) - r;
    QHash<QString, QString> sheetTo;
    for (const auto* p : moved)
        if (!sheetTo.contains(p->sheet)) sheetTo.insert(p->sheet, now.value(p->id)->sheet);
    for (const auto& p : before.parts) {
        if (std::find(moved.begin(), moved.end(), &p) != moved.end()) continue;
        core::Brick b = brickOf(p);
        const QPointF pivot = parts::placement::rotated(parts::placement::imageCentre(b, parts), turn) + shift;
        // (in float, as the desktop keeps a part's turn)
        b.orientation = static_cast<float>(core::norm360(p.orientation + turn));
        parts::placement::placeByImageCentre(b, pivot, parts);
        out.move.push_back({ p.id, sheetTo.value(p.sheet, p.sheet), p.partNumber, b.displayArea, b.orientation });
    }
    return out;
}

bool finishShared(const core::Map& before, core::Map& after, parts::PartsLibrary& parts, core::SettledBack* removed) {
    const auto placed = core::sharedModulesIn(before.sidecar.modules);
    if (placed.empty()) return false;
    SharedLockState lock;
    lock.rebase(before, parts);
    bool changed = false;
    for (const auto& m : placed) {
        SettleSide was{ true, partsOn(before, m.members) };
        const bool record = std::any_of(after.sidecar.modules.begin(), after.sidecar.modules.end(),
                                        [&](const core::Module& x) { return x.id == m.placedId; });
        SettleSide is{ record, partsOn(after, m.members) };
        const Settled s = settleShared(was, is, parts);
        if (s.empty()) continue;
        changed = true;
        const QSet<QString> gone(s.remove.begin(), s.remove.end());
        QHash<QString, const SettlePart*> moves;
        for (const auto& p : s.move) moves.insert(p.id, &p);
        for (const auto& L : after.layers()) {
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            auto& layer = static_cast<core::LayerBrick&>(*L);
            for (auto it = layer.bricks.begin(); it != layer.bricks.end();) {
                if (gone.contains(it->guid)) {
                    if (removed) removed->bricks.emplace_back(L->guid, *it);
                    it = layer.bricks.erase(it);
                    continue;
                }
                if (const SettlePart* mv = moves.value(it->guid); mv && mv->sheet == L->guid) {
                    it->displayArea = mv->displayArea;
                    it->orientation = static_cast<float>(mv->orientation);
                }
                ++it;
            }
        }
        // Parts that go to another sheet: taken out of theirs, put on top of it.
        for (const auto& mv : s.move) {
            core::LayerBrick* from = nullptr;
            core::LayerBrick* to = nullptr;
            for (const auto& L : after.layers()) {
                if (!L || L->kind() != core::LayerKind::Brick) continue;
                auto& layer = static_cast<core::LayerBrick&>(*L);
                if (L->guid == mv.sheet) to = &layer;
                if (std::any_of(layer.bricks.begin(), layer.bricks.end(), [&](const core::Brick& b) { return b.guid == mv.id; }))
                    from = &layer;
            }
            if (!from || !to || from == to) continue;
            const auto it = std::find_if(from->bricks.begin(), from->bricks.end(), [&](const core::Brick& b) { return b.guid == mv.id; });
            core::Brick b = *it;
            from->bricks.erase(it);
            b.displayArea = mv.displayArea;
            b.orientation = static_cast<float>(mv.orientation);
            to->bricks.push_back(std::move(b));
        }
        if (s.removeModule) {
            auto& mods = after.sidecar.modules;
            const auto it = std::find_if(mods.begin(), mods.end(), [&](const core::Module& x) { return x.id == m.placedId; });
            if (it != mods.end()) {
                // (As it was before the step: what is read now lists only the parts still there.)
                if (removed) {
                    const auto was = std::find_if(before.sidecar.modules.begin(), before.sidecar.modules.end(),
                                                  [&](const core::Module& x) { return x.id == m.placedId; });
                    removed->modules.push_back(was != before.sidecar.modules.end() ? *was : *it);
                }
                mods.erase(it);
            }
        }
    }
    // A module moved whole now: its placement follows.
    if (lock.keepPlacements(after, parts)) changed = true;
    return changed;
}

}  // namespace bld::edit
