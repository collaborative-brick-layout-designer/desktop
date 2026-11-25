#include "ModuleCommands.h"

#include <QJsonArray>
#include "SharedModuleKeep.h"

#include "../core/SharedModule.h"
#include "EditCommands.h"
#include "ModuleSheets.h"

#include "../core/Groups.h"

#include "../core/Places.h"
#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"
#include "../parts/BrickPlacement.h"

#include <QDateTime>
#include <QObject>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace bld::edit {

namespace {

core::LayerBrick* brickLayer(core::Map& map, int idx) {
    if (idx < 0 || idx >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Brick)
        ? static_cast<core::LayerBrick*>(L)
        : nullptr;
}

int findModuleIndex(const core::Map& map, const QString& id) {
    for (int i = 0; i < static_cast<int>(map.sidecar.modules.size()); ++i) {
        if (map.sidecar.modules[i].id == id) return i;
    }
    return -1;
}

}

// ----- CreateModuleCommand -----

CreateModuleCommand::CreateModuleCommand(core::Map& map, QString name,
                                         std::vector<Member> members,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      moduleId_(core::newBbmId()),
      name_(std::move(name)), members_(std::move(members)) {
    setText(QObject::tr("Group as module %1 (%2 parts)").arg(name_).arg(members_.size()));
}

void CreateModuleCommand::redo() {
    core::Module m;
    m.id = moduleId_;
    m.name = name_;
    for (const auto& mem : members_) m.memberIds.insert(mem.guid);
    const auto split = core::places::adopted(map_.sidecar.layoutPlaces, m.memberIds);
    adopted_ = split.module;
    map_.sidecar.layoutPlaces = split.layout;
    m.places = adopted_;
    map_.sidecar.modules.push_back(std::move(m));
}

void CreateModuleCommand::undo() {
    for (const auto& p : std::as_const(adopted_))
        map_.sidecar.layoutPlaces = core::places::withPlace(map_.sidecar.layoutPlaces, p.toObject());
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

// ----- AddModuleCommand -----

AddModuleCommand::AddModuleCommand(core::Map& map, core::Module module, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), module_(std::move(module)) {
    setText(QObject::tr("Add module %1").arg(module_.name));
}

void AddModuleCommand::redo() {
    map_.sidecar.modules.push_back(module_);
}

void AddModuleCommand::undo() {
    const int i = findModuleIndex(map_, module_.id);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

QUndoCommand* deleteModuleWithPartsCommand(core::Map& map, const QString& moduleId) {
    const int mi = findModuleIndex(map, moduleId);
    if (mi < 0) return nullptr;
    const QSet<QString> members = map.sidecar.modules[mi].memberIds;
    // Every part, in sheet and then sheet order, so Undo puts each back where it was.
    // Deleting all of a module's parts removes the module too (DeleteBricksCommand).
    std::vector<DeleteBricksCommand::Entry> entries;
    for (int li = 0; li < static_cast<int>(map.layers().size()); ++li) {
        const auto* L = brickLayer(map, li);
        if (!L) continue;
        for (int i = 0; i < static_cast<int>(L->bricks.size()); ++i) {
            if (!members.contains(L->bricks[i].guid)) continue;
            DeleteBricksCommand::Entry e;
            e.layerIndex = li;
            e.indexInLayer = i;
            e.brick = L->bricks[i];
            entries.push_back(std::move(e));
        }
    }
    auto* parent = new QUndoCommand(QObject::tr("Delete module %1").arg(map.sidecar.modules[mi].name));
    if (!entries.empty()) new DeleteBricksCommand(map, std::move(entries), parent);
    // A module with no parts left on the map (or none at all) still goes.
    new DeleteModuleCommand(map, moduleId, parent);
    return parent;
}

// ----- DeleteModuleCommand -----

DeleteModuleCommand::DeleteModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)) {
    setText(QObject::tr("Delete module"));
}

void DeleteModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) { removed_.reset(); insertIndex_ = -1; return; }
    removed_ = map_.sidecar.modules[i];
    insertIndex_ = i;
    map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

void DeleteModuleCommand::undo() {
    if (!removed_) return;
    const int idx = std::min<int>(insertIndex_, static_cast<int>(map_.sidecar.modules.size()));
    map_.sidecar.modules.insert(map_.sidecar.modules.begin() + idx, *removed_);
}

// ----- MoveModuleCommand -----

MoveModuleCommand::MoveModuleCommand(core::Map& map, QString moduleId, QPointF deltaStuds,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)), delta_(deltaStuds) {
    setText(QObject::tr("Move module"));
}

void MoveModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];
    for (auto& layerPtr : map_.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*layerPtr).bricks) {
            if (mod.memberIds.contains(b.guid)) {
                b.displayArea.translate(delta_);
            }
        }
    }
}

void MoveModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];
    for (auto& layerPtr : map_.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*layerPtr).bricks) {
            if (mod.memberIds.contains(b.guid)) {
                b.displayArea.translate(-delta_);
            }
        }
    }
}

// ----- RotateModuleCommand -----

RotateModuleCommand::RotateModuleCommand(core::Map& map, parts::PartsLibrary& lib, QString moduleId,
                                         double degrees, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), lib_(lib), moduleId_(std::move(moduleId)), degrees_(degrees) {
    setText(QObject::tr("Turn module %1°").arg(degrees_, 0, 'f', 1));
}

void RotateModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];

    // Snapshot every member brick's pre-state the first time we redo, so
    // undo restores exact positions even for fractional angles.
    if (!captured_) {
        before_.clear();
        for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
            auto* L = brickLayer(map_, li);
            if (!L) continue;
            for (const auto& b : L->bricks) {
                if (mod.memberIds.contains(b.guid)) {
                    before_.push_back({ li, b.guid, b.displayArea, b.orientation });
                }
            }
        }
        captured_ = true;
    }
    if (before_.empty()) return;

    // Module centre: centroid of member sprite centres (stud coords).
    QPointF centroid(0, 0);
    int count = 0;
    for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
        auto* L = brickLayer(map_, li);
        if (!L) continue;
        for (const auto& b : L->bricks) {
            if (mod.memberIds.contains(b.guid)) {
                centroid += parts::placement::imageCentre(b, lib_);
                ++count;
            }
        }
    }
    if (count == 0) return;
    centroid /= count;

    const double rad = degrees_ * M_PI / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);

    for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
        auto* L = brickLayer(map_, li);
        if (!L) continue;
        for (auto& b : L->bricks) {
            if (!mod.memberIds.contains(b.guid)) continue;
            // Turn the brick's sprite centre around the module centroid;
            // its displayArea follows the rotated hull.
            const QPointF rel = parts::placement::imageCentre(b, lib_) - centroid;
            const QPointF rotated(rel.x() * c - rel.y() * s, rel.x() * s + rel.y() * c);
            b.orientation = std::fmod(b.orientation + static_cast<float>(degrees_), 360.0f);
            parts::placement::placeByImageCentre(b, centroid + rotated, lib_);
        }
    }
}

void RotateModuleCommand::undo() {
    for (const auto& s : before_) {
        auto* L = brickLayer(map_, s.layerIndex);
        if (!L) continue;
        for (auto& b : L->bricks) {
            if (b.guid == s.guid) {
                b.displayArea = s.area;
                b.orientation = s.orientation;
                break;
            }
        }
    }
}

// ----- RenameModuleCommand -----

RenameModuleCommand::RenameModuleCommand(core::Map& map, QString moduleId, QString newName,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      moduleId_(std::move(moduleId)), newName_(std::move(newName)) {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) oldName_ = map_.sidecar.modules[i].name;
    setText(QObject::tr("Rename module"));
}

void RenameModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].name = newName_;
}

void RenameModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].name = oldName_;
}

// ----- UpdateModuleCommand -----

UpdateModuleCommand::UpdateModuleCommand(core::Map& map, core::Module updated, const QString& text, int mergeKey,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), after_(std::move(updated)), mergeKey_(mergeKey) {
    const int i = findModuleIndex(map_, after_.id);
    if (i >= 0) before_ = map_.sidecar.modules[i];
    setText(text);
}

void UpdateModuleCommand::redo() {
    const int i = findModuleIndex(map_, after_.id);
    if (i < 0) return;
    // The members may have changed since (a part added): keep them.
    core::Module next = after_;
    next.memberIds = map_.sidecar.modules[i].memberIds;
    map_.sidecar.modules[i] = next;
}

void UpdateModuleCommand::undo() {
    const int i = findModuleIndex(map_, before_.id);
    if (i < 0) return;
    core::Module prev = before_;
    prev.memberIds = map_.sidecar.modules[i].memberIds;
    map_.sidecar.modules[i] = prev;
}

bool UpdateModuleCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = dynamic_cast<const UpdateModuleCommand*>(other);
    if (!o || o->after_.id != after_.id || mergeKey_ < 0) return false;
    after_ = o->after_;
    return true;
}

// ----- SetModuleMembersCommand -----

SetModuleMembersCommand::SetModuleMembersCommand(core::Map& map, QString moduleId, QSet<QString> members,
                                                 const QString& text, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)), after_(std::move(members)) {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) before_ = map_.sidecar.modules[i].memberIds;
    setText(text);
}

void SetModuleMembersCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].memberIds = after_;
}

void SetModuleMembersCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].memberIds = before_;
}

// ----- CloneModuleCommand -----

CloneModuleCommand::CloneModuleCommand(core::Map& map, QString sourceModuleId,
                                       QPointF offsetStuds, QString newName,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      sourceModuleId_(std::move(sourceModuleId)),
      offsetStuds_(offsetStuds),
      newName_(std::move(newName)),
      newModuleId_(core::newBbmId()) {
    setText(QObject::tr("Duplicate module"));
}

void CloneModuleCommand::redo() {
    const int srcIdx = findModuleIndex(map_, sourceModuleId_);
    if (srcIdx < 0) return;
    const core::Module srcMod = map_.sidecar.modules[srcIdx];  // copy

    // First redo: duplicate the source module's members on THEIR ORIGINAL
    // layers with fresh guids and the offset, with the sets (groups) they
    // are in. Later redos put back exactly those copies.
    if (!captured_) {
        clones_.clear();
        for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
            auto* L = brickLayer(map_, li);
            if (!L) continue;
            LayerClone clone;
            clone.layerIndex = li;
            for (const auto& b : L->bricks) {
                if (!srcMod.memberIds.contains(b.guid)) continue;
                core::Brick copy = b;
                copy.guid = core::newBbmId();
                cloneOf_.insert(b.guid, copy.guid);
                copy.displayArea.translate(offsetStuds_);
                // Clones shouldn't inherit the source's connection links —
                // new bricks are un-linked and snap fresh.
                for (auto& c : copy.connections) {
                    c.guid = core::newBbmId();
                    c.linkedToId.clear();
                }
                clone.bricks.push_back(std::move(copy));
            }
            if (clone.bricks.empty()) continue;
            clone.groups = core::cloneGroups(*L, clone.bricks, [] { return core::newBbmId(); });
            clones_.push_back(std::move(clone));
        }
        captured_ = true;
    }
    appliedBricks_.clear();
    for (const auto& clone : clones_) {
        auto* L = brickLayer(map_, clone.layerIndex);
        if (!L) continue;
        for (const auto& b : clone.bricks) {
            appliedBricks_.push_back({ clone.layerIndex, b.guid });
            L->bricks.push_back(b);
        }
        for (const auto& g : clone.groups) L->groups.push_back(g);
    }

    // Its look comes along; not its library link or pin.
    QSet<QString> members;
    for (const auto& a : appliedBricks_) members.insert(a.guid);
    // (A shared module's copy is another shared placement, under its owner's name.)
    core::Module m = core::copyOfPlaced(srcMod, std::move(members), newModuleId_, offsetStuds_, cloneOf_);
    // The same ids again on redo.
    if (!clonePlaces_) clonePlaces_ = m.places;
    m.places = *clonePlaces_;
    if (!newName_.isEmpty() && !core::isSharedModule(srcMod)) m.name = newName_;
    map_.sidecar.modules.push_back(std::move(m));
}

void CloneModuleCommand::undo() {
    // Remove cloned bricks and their groups.
    for (const auto& clone : clones_) {
        auto* L = brickLayer(map_, clone.layerIndex);
        if (!L) continue;
        QSet<QString> bricks, groups;
        for (const auto& b : clone.bricks) bricks.insert(b.guid);
        for (const auto& g : clone.groups) groups.insert(g.guid);
        L->bricks.erase(std::remove_if(L->bricks.begin(), L->bricks.end(),
                                       [&](const core::Brick& b) { return bricks.contains(b.guid); }),
                        L->bricks.end());
        L->groups.erase(std::remove_if(L->groups.begin(), L->groups.end(),
                                       [&](const core::Group& g) { return groups.contains(g.guid); }),
                        L->groups.end());
    }
    // Remove cloned module entry.
    const int i = findModuleIndex(map_, newModuleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

// ----- FlattenModuleCommand -----

FlattenModuleCommand::FlattenModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)) {
    setText(QObject::tr("Ungroup module"));
}

void FlattenModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) { removed_.reset(); insertIndex_ = -1; return; }
    removed_ = map_.sidecar.modules[i];
    insertIndex_ = i;
    map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
    if (!released_) {
        const QJsonArray& had = map_.sidecar.layoutPlaces;
        const QJsonArray all = core::places::released(had, removed_->places, [] { return core::newBbmId(); });
        released_ = QJsonArray();
        for (qsizetype k = had.size(); k < all.size(); ++k) released_->append(all.at(k));
    }
    for (const auto& p : std::as_const(*released_))
        map_.sidecar.layoutPlaces = core::places::withPlace(map_.sidecar.layoutPlaces, p.toObject());
}

void FlattenModuleCommand::undo() {
    if (!removed_) return;
    if (released_)
        for (const auto& p : std::as_const(*released_))
            map_.sidecar.layoutPlaces = core::places::without(map_.sidecar.layoutPlaces, p.toObject().value(QLatin1String("id")).toString());
    const int idx = std::min<int>(insertIndex_, static_cast<int>(map_.sidecar.modules.size()));
    map_.sidecar.modules.insert(map_.sidecar.modules.begin() + idx, *removed_);
}

// ----- A library version's places -----

QJsonArray placesOn(const std::vector<ImportBbmAsModuleCommand::LayerBatch>& batches, const QStringList& guids) {
    QJsonArray all;
    QHash<QString, QString> to;
    qsizetype at = 0;
    for (const auto& batch : batches) {
        for (const auto& p : batch.places) all.append(p);
        for (qsizetype i = 0; i < static_cast<qsizetype>(batch.bricks.size()); ++i, ++at)
            if (i < batch.sourceIds.size() && at < guids.size()) to.insert(batch.sourceIds[i], guids[at]);
    }
    if (all.isEmpty()) return {};
    return core::places::renamed(all, [&to](const QString& id) { return to.contains(id) ? std::optional(to.value(id)) : std::nullopt; });
}

QJsonArray placesInModule(const core::Map& module) {
    QJsonArray all = module.sidecar.layoutPlaces;
    for (const auto& m : module.sidecar.modules)
        for (const auto& p : m.places) all.append(p);
    return core::places::readPlaces(all);
}

void ReplaceModulePartsCommand::setPlaces(core::Module& mod, const QStringList& members) {
    if (!placesAfter_) {
        const QJsonArray version = placesOn(batches_, members);
        if (!version.isEmpty()) {
            placesAfter_ = core::places::updated(mod.places, version, [] { return core::newBbmId(); });
        } else if (core::isSharedModule(mod)) {
            // Exactly its owner's.
            placesAfter_ = QJsonArray();
        } else {
            const QSet<QString> here(members.begin(), members.end());
            placesAfter_ = core::places::renamed(mod.places, [&here](const QString& id) { return here.contains(id) ? std::optional(id) : std::nullopt; });
        }
    }
    mod.places = *placesAfter_;
}

// ----- ReplaceModulePartsCommand -----

ReplaceModulePartsCommand::ReplaceModulePartsCommand(core::Map& map, QString moduleId,
                                                     std::vector<ImportBbmAsModuleCommand::LayerBatch> batches,
                                                     QString libraryId, int libraryVersion, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)), batches_(std::move(batches)),
      libraryId_(std::move(libraryId)), libraryVersion_(libraryVersion) {
    // Fresh guids once, so redo after undo puts back the same parts.
    for (auto& batch : batches_)
        for (auto& b : batch.bricks)
            if (b.guid.isEmpty()) b.guid = core::newBbmId();
    setText(QObject::tr("Update module from the Module library"));
}

QSet<QString> ReplaceModulePartsCommand::newMembers() const {
    if (keepParts_ && keepAfter_) return keepMembers_;
    QSet<QString> out;
    for (const auto& batch : batches_)
        for (const auto& b : batch.bricks) out.insert(b.guid);
    return out;
}

QHash<QString, ReplaceModulePartsCommand::SheetState> ReplaceModulePartsCommand::sheetsNow() const {
    QHash<QString, SheetState> out;
    for (const auto& L : map_.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& layer = static_cast<const core::LayerBrick&>(*L);
        out.insert(L->guid, { layer.bricks, layer.groups });
    }
    return out;
}

void ReplaceModulePartsCommand::restoreSheets(const QHash<QString, SheetState>& sheets) {
    for (const auto& L : map_.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick || !sheets.contains(L->guid)) continue;
        auto& layer = static_cast<core::LayerBrick&>(*L);
        layer.bricks = sheets.value(L->guid).bricks;
        layer.groups = sheets.value(L->guid).groups;
    }
}

void ReplaceModulePartsCommand::redo() {
    const int mi = findModuleIndex(map_, moduleId_);
    if (mi < 0) return;
    core::Module& mod = map_.sidecar.modules[mi];
    before_ = mod;
    if (keepParts_) {
        // Only the parts that differ change (SharedModuleKeep.h); the first
        // redo works it out, later ones put the same result back.
        if (!keepAfter_) {
            keepBefore_ = sheetsNow();
            const KeepPlan plan = planKeep(keepSheets(map_), mod.memberIds, batches_, *keepParts_);
            kept_ = static_cast<int>(plan.kept.size());
            // A shared module remembers the parts its updates took out (SharedSettle.h, rule 0).
            if (sharedAfter_ && !plan.removed.isEmpty()) {
                QStringList had;
                for (const auto& v : sharedAfter_->value(QLatin1String("retired")).toArray())
                    if (v.isString()) had << v.toString();
                sharedAfter_->insert(QStringLiteral("retired"), QJsonArray::fromStringList(core::moreRetired(had, plan.removed)));
            }
            const QStringList members = applyKeep(map_, plan, batches_);
            keepOrder_ = members;
            keepMembers_ = QSet<QString>(members.begin(), members.end());
            keepAfter_ = sheetsNow();
        } else {
            restoreSheets(*keepAfter_);
        }
        core::Module& m = map_.sidecar.modules[findModuleIndex(map_, moduleId_)];
        m.memberIds = keepMembers_;
        m.libraryModuleId = libraryId_;
        m.libraryVersion = libraryVersion_;
        if (sharedAfter_) m.shared = *sharedAfter_;
        setPlaces(m, keepOrder_);
        return;
    }
    removed_.clear();
    for (auto& L : map_.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        auto& bricks = static_cast<core::LayerBrick&>(*L).bricks;
        // Each part's index before any were taken out (undo puts them back in this order).
        int taken = 0;
        for (int i = 0; i < static_cast<int>(bricks.size());) {
            if (mod.memberIds.contains(bricks[i].guid)) {
                removed_.push_back({ L->guid, i + taken, bricks[i] });
                bricks.erase(bricks.begin() + i);
                ++taken;
            } else {
                ++i;
            }
        }
    }
    for (const auto& batch : batches_) {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(map_.layers().size()); ++i)
            if (map_.layers()[i] && map_.layers()[i]->kind() == core::LayerKind::Brick && map_.layers()[i]->guid == batch.targetLayerGuid) idx = i;
        auto* BL = brickLayer(map_, idx);
        if (!BL) continue;
        for (const auto& b : batch.bricks) BL->bricks.push_back(b);
        for (const auto& g : batch.groups) BL->groups.push_back(g);
    }
    mod.memberIds = newMembers();
    mod.libraryModuleId = libraryId_;
    mod.libraryVersion = libraryVersion_;
    if (sharedAfter_) mod.shared = *sharedAfter_;
    QStringList order;
    for (const auto& batch : batches_)
        for (const auto& b : batch.bricks) order << b.guid;
    setPlaces(mod, order);
}

void ReplaceModulePartsCommand::undo() {
    if (keepParts_ && keepBefore_) {
        restoreSheets(*keepBefore_);
        const int mi = findModuleIndex(map_, moduleId_);
        if (mi >= 0 && before_) map_.sidecar.modules[mi] = *before_;
        return;
    }
    const QSet<QString> fresh = newMembers();
    QSet<QString> groups;
    for (const auto& batch : batches_)
        for (const auto& g : batch.groups) groups.insert(g.guid);
    for (auto& L : map_.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        auto& BL = static_cast<core::LayerBrick&>(*L);
        BL.bricks.erase(std::remove_if(BL.bricks.begin(), BL.bricks.end(), [&](const core::Brick& b) { return fresh.contains(b.guid); }),
                        BL.bricks.end());
        BL.groups.erase(std::remove_if(BL.groups.begin(), BL.groups.end(), [&](const core::Group& g) { return groups.contains(g.guid); }),
                        BL.groups.end());
    }
    // Back where they were, in the order they were taken out.
    for (const auto& r : removed_) {
        for (auto& L : map_.layers()) {
            if (!L || L->kind() != core::LayerKind::Brick || L->guid != r.layerGuid) continue;
            auto& bricks = static_cast<core::LayerBrick&>(*L).bricks;
            const int at = std::clamp(r.index, 0, static_cast<int>(bricks.size()));
            bricks.insert(bricks.begin() + at, r.brick);
            break;
        }
    }
    const int mi = findModuleIndex(map_, moduleId_);
    if (mi >= 0 && before_) map_.sidecar.modules[mi] = *before_;
}

// ----- ImportBbmAsModuleCommand -----

ImportBbmAsModuleCommand::ImportBbmAsModuleCommand(core::Map& map,
                                                   QString sourcePath, QString moduleName,
                                                   std::vector<LayerBatch> batches,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      sourcePath_(std::move(sourcePath)),
      moduleId_(core::newBbmId()),
      name_(std::move(moduleName)), batches_(std::move(batches)) {
    int total = 0; for (const auto& b : batches_) total += static_cast<int>(b.bricks.size());
    setText(QObject::tr("Import module (%1 parts)").arg(total));
}

// Back-compat single-layer ctor: wraps the args into a one-batch array so
// the multi-layer redo/undo path is the single code path.
ImportBbmAsModuleCommand::ImportBbmAsModuleCommand(core::Map& map, int targetLayerIndex,
                                                   QString sourcePath, QString moduleName,
                                                   std::vector<core::Brick> bricks,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      sourcePath_(std::move(sourcePath)),
      moduleId_(core::newBbmId()),
      name_(std::move(moduleName)) {
    // Pick the layer's current name (if any) so undo/redo stays stable.
    QString layerName = QStringLiteral("Bricks");
    if (targetLayerIndex >= 0 && targetLayerIndex < static_cast<int>(map.layers().size())) {
        layerName = map.layers()[targetLayerIndex]->name;
    }
    LayerBatch batch;
    batch.layerName = layerName;
    batch.bricks = std::move(bricks);
    batches_.push_back(std::move(batch));
    setText(QObject::tr("Import module (%1 parts)").arg(batches_.front().bricks.size()));
}

namespace {
int findBrickLayerByGuid(core::Map& map, const QString& guid) {
    if (guid.isEmpty()) return -1;
    for (int i = 0; i < static_cast<int>(map.layers().size()); ++i) {
        auto* L = map.layers()[i].get();
        if (L && L->kind() == core::LayerKind::Brick && L->guid == guid) return i;
    }
    return -1;
}
}

void ImportBbmAsModuleCommand::redo() {
    // First redo: resolve each batch's target layer (the brick layer with
    // the same name; else the one the person chose; else a new brick
    // layer with the batch's name), then insert bricks. Later redos
    // (after an undo) replay the same resolution captured in applied_
    // (one entry per batch, in order) so the layer set is the same every time.
    if (!captured_) {
        applied_.clear();
        for (auto& batch : batches_) {
            AppliedLayer a;
            a.layerName = batch.layerName;
            int idx = findPartsSheet(map_, a.layerName);
            if (idx < 0) idx = findBrickLayerByGuid(map_, batch.targetLayerGuid);
            if (idx < 0) {
                auto L = std::make_unique<core::LayerBrick>();
                L->guid = core::newBbmId();
                L->name = a.layerName.isEmpty() ? QStringLiteral("Module") : a.layerName;
                idx = static_cast<int>(map_.layers().size());
                map_.layers().push_back(std::move(L));
                a.wasCreated = true;
            }
            a.layerIndex = idx;
            a.layerGuid = map_.layers()[idx]->guid;
            auto* BL = brickLayer(map_, idx);
            for (auto& b : batch.bricks) {
                if (b.guid.isEmpty()) b.guid = core::newBbmId();
                a.addedGuids.append(b.guid);
                if (BL) BL->bricks.push_back(b);
            }
            if (BL)
                for (const auto& g : batch.groups) BL->groups.push_back(g);
            applied_.push_back(std::move(a));
        }
        captured_ = true;
    } else {
        // Re-apply: recreate any layers we had created, re-insert bricks.
        for (std::size_t i = 0; i < applied_.size() && i < batches_.size(); ++i) {
            auto& a = applied_[i];
            if (a.wasCreated) {
                auto L = std::make_unique<core::LayerBrick>();
                L->guid = a.layerGuid.isEmpty() ? core::newBbmId() : a.layerGuid;
                L->name = a.layerName.isEmpty() ? QStringLiteral("Module") : a.layerName;
                a.layerIndex = static_cast<int>(map_.layers().size());
                map_.layers().push_back(std::move(L));
            } else {
                a.layerIndex = findBrickLayerByGuid(map_, a.layerGuid);
                if (a.layerIndex < 0) a.layerIndex = findPartsSheet(map_, a.layerName);
            }
            auto* BL = brickLayer(map_, a.layerIndex);
            if (!BL) continue;
            for (const auto& b : batches_[i].bricks) BL->bricks.push_back(b);
            for (const auto& g : batches_[i].groups) BL->groups.push_back(g);
        }
    }

    core::Module mod;
    mod.id = moduleId_;
    mod.name = name_;
    mod.sourceFile = sourcePath_;
    mod.importedAt = QDateTime::currentDateTimeUtc();
    mod.libraryModuleId = libraryId_;
    mod.libraryVersion = libraryVersion_;
    if (shared_) {
        // Shared, read-only: each of the version's sheets with the sheet it went on.
        mod.shared = *shared_;
        if (auto p = core::readPlacement(mod.shared->value(QLatin1String("placement")))) {
            QMap<QString, QString> sheets;
            for (const auto& a : applied_) sheets.insert(a.layerName, a.layerGuid);
            p->sheets = sheets;
            mod.shared->insert(QStringLiteral("placement"), core::placementJson(*p));
        }
    }
    for (const auto& a : applied_)
        for (const QString& g : a.addedGuids) mod.memberIds.insert(g);
    mod = core::withLibrarySetup(std::move(mod), setup_);
    if (!places_) {
        QStringList order;
        for (const auto& a : applied_) order << a.addedGuids;
        places_ = core::places::copied(placesOn(batches_, order), [](const QString& id) { return std::optional(id); }, [] { return core::newBbmId(); });
    }
    mod.places = *places_;
    map_.sidecar.modules.push_back(std::move(mod));
}

void ImportBbmAsModuleCommand::undo() {
    // Remove every brick we added; then pop any layers we created (in
    // reverse order so their indices stay valid); then drop the module.
    for (auto& a : applied_) {
        if (auto* BL = brickLayer(map_, a.layerIndex)) {
            QSet<QString> toRemove;
            for (const QString& g : a.addedGuids) toRemove.insert(g);
            BL->bricks.erase(
                std::remove_if(BL->bricks.begin(), BL->bricks.end(),
                               [&](const core::Brick& b) { return toRemove.contains(b.guid); }),
                BL->bricks.end());
            QSet<QString> groups;
            const std::size_t i = static_cast<std::size_t>(&a - applied_.data());
            if (i < batches_.size())
                for (const auto& g : batches_[i].groups) groups.insert(g.guid);
            BL->groups.erase(std::remove_if(BL->groups.begin(), BL->groups.end(),
                                            [&](const core::Group& g) { return groups.contains(g.guid); }),
                             BL->groups.end());
        }
    }
    // Walk applied_ in reverse; for layers we created, drop them so the
    // host map returns to its pre-import layer count.
    for (auto it = applied_.rbegin(); it != applied_.rend(); ++it) {
        if (!it->wasCreated) continue;
        if (it->layerIndex < 0 ||
            it->layerIndex >= static_cast<int>(map_.layers().size())) continue;
        map_.layers().erase(map_.layers().begin() + it->layerIndex);
    }
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

// ----- DetachSharedModuleCommand -----

DetachSharedModuleCommand::DetachSharedModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)) {
    setText(QObject::tr("Make shared module mine"));
}

void DetachSharedModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    core::Module& mod = map_.sidecar.modules[i];
    const auto shared = core::sharedPlaced(mod);
    if (!shared) return;
    before_ = mod;
    QJsonObject basedOn{ { QStringLiteral("moduleId"), shared->moduleId } };
    if (shared->version > 0) basedOn.insert(QStringLiteral("version"), shared->version);
    if (!shared->by.isEmpty()) basedOn.insert(QStringLiteral("by"), shared->by);
    mod = core::detached(mod);
    mod.extras.insert(QStringLiteral("basedOn"), basedOn);
}

void DetachSharedModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0 || !before_) return;
    // (Its look or pin may have changed since; the link and the lock come back.)
    core::Module& mod = map_.sidecar.modules[i];
    mod.shared = before_->shared;
    mod.libraryModuleId = before_->libraryModuleId;
    mod.libraryVersion = before_->libraryVersion;
    mod.extras.remove(QStringLiteral("basedOn"));
}

}
