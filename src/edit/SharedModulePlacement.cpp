#include "SharedModulePlacement.h"

#include "ModuleSheets.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace bld::edit {

namespace {

struct Item {
    QString key;
    QPointF p;
    double o = 0.0;
};

Item itemOf(const core::Brick& b, parts::PartsLibrary& parts) {
    return { b.partNumber.toLower(), parts::placement::imageCentre(b, parts), core::norm360(b.orientation) };
}

int total(const LayerBatches& version) {
    int n = 0;
    for (const auto& b : version) n += static_cast<int>(b.bricks.size());
    return n;
}

core::SharedPlacement asShared(const Placement& p) { return { p.degrees, p.to.x(), p.to.y(), std::nullopt }; }

}  // namespace

LayerBatches placeVersion(LayerBatches version, const core::SharedPlacement& placement, parts::PartsLibrary& parts) {
    return placeVersion(std::move(version), Placement{ placement.degrees, QPointF(placement.x, placement.y), 0 }, parts);
}

int partsInPlace(const LayerBatches& version, const std::vector<core::Brick>& placed,
                 const core::SharedPlacement& placement, parts::PartsLibrary& parts) {
    std::vector<Item> lib;
    for (const auto& batch : placeVersion(version, placement, parts))
        for (const auto& b : batch.bricks) lib.push_back(itemOf(b, parts));
    std::vector<bool> used(lib.size(), false);
    int n = 0;
    for (const auto& brick : placed) {
        const Item h = itemOf(brick, parts);
        for (std::size_t k = 0; k < lib.size(); ++k) {
            const Item& l = lib[k];
            if (used[k] || l.key != h.key) continue;
            const double turn = std::abs(l.o - h.o);
            if (std::hypot(l.p.x() - h.p.x(), l.p.y() - h.p.y()) >= 0.1 || std::min(turn, 360.0 - turn) >= 0.5) continue;
            used[k] = true;
            ++n;
            break;
        }
    }
    return n;
}

core::SharedPlacement resolvePlacement(const LayerBatches& version, const std::vector<core::Brick>& placed,
                                       const std::optional<core::SharedPlacement>& kept, parts::PartsLibrary& parts) {
    if (kept) {
        const int n = std::min(static_cast<int>(placed.size()), total(version));
        if (n == 0 || partsInPlace(version, placed, *kept, parts) * 2 >= n) return *kept;
    }
    core::SharedPlacement found = asShared(alignToPlaced(version, placed, parts));
    if (kept && kept->sheets) found.sheets = kept->sheets;
    return found;
}

bool matchesVersion(const LayerBatches& version, const std::vector<core::Brick>& placed, parts::PartsLibrary& parts,
                    const std::optional<core::SharedPlacement>& placement) {
    const int n = total(version);
    if (n != static_cast<int>(placed.size())) return false;
    if (placement && partsInPlace(version, placed, *placement, parts) == n) return true;
    return partsInPlace(version, placed, asShared(alignToPlaced(version, placed, parts)), parts) == n;
}

std::optional<PlannedVersion> planVersion(const core::Map& map, const core::Module& mod, LayerBatches next,
                                          const LayerBatches* current, parts::PartsLibrary& parts) {
    // The layout's parts sheets, in order, and how many of the module's parts each has.
    struct Sheet {
        QString id;
        QString key;
        int parts = 0;
    };
    std::vector<Sheet> sheets;
    std::vector<core::Brick> placed;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        Sheet s{ L->guid, sheetKey(L->name), 0 };
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (mod.memberIds.contains(b.guid)) {
                placed.push_back(b);
                ++s.parts;
            }
        sheets.push_back(std::move(s));
    }
    if (sheets.empty()) return std::nullopt;
    const Sheet* most = nullptr;
    for (const auto& s : sheets)
        if (s.parts > 0 && (!most || s.parts > most->parts)) most = &s;
    const QString fallback = most ? most->id : sheets.front().id;

    const auto shared = core::sharedPlaced(mod);
    const std::optional<core::SharedPlacement> kept = shared ? shared->placement : std::nullopt;
    QHash<QString, QString> keptByKey;
    if (kept && kept->sheets)
        for (auto it = kept->sheets->begin(); it != kept->sheets->end(); ++it) keptByKey.insert(sheetKey(it.key()), it.value());

    // Where the module sits: the placement it kept, while that still puts
    // the version it has onto its parts; else worked out from the parts.
    const LayerBatches& basis = current ? *current : next;
    core::SharedPlacement placement;
    if (placed.empty()) placement = kept.value_or(core::SharedPlacement{});
    else if (kept || current) placement = resolvePlacement(basis, placed, kept, parts);
    else placement = asShared(alignToPlaced(next, placed, parts));

    PlannedVersion out;
    out.batches = placeVersion(std::move(next), placement, parts);
    QMap<QString, QString> used;
    for (auto& batch : out.batches) {
        const QString key = sheetKey(batch.layerName);
        QString id;
        if (const QString keptId = keptByKey.value(key); !keptId.isEmpty())
            if (std::any_of(sheets.begin(), sheets.end(), [&](const Sheet& s) { return s.id == keptId; })) id = keptId;
        if (id.isEmpty())
            for (const auto& s : sheets)
                if (s.key == key) {
                    id = s.id;
                    break;
                }
        if (id.isEmpty()) id = fallback;
        batch.targetLayerGuid = id;
        used.insert(batch.layerName, id);
    }
    out.placement = { placement.degrees, placement.x, placement.y, used };
    return out;
}

void moveBetweenPlacements(core::Brick& brick, const core::SharedPlacement& from, const core::SharedPlacement& to,
                           parts::PartsLibrary& parts) {
    const QPointF pivot = parts::placement::imageCentre(brick, parts);
    const QPointF own = parts::placement::rotated(pivot - QPointF(from.x, from.y), -from.degrees);
    brick.orientation = static_cast<float>(core::norm360(brick.orientation + to.degrees - from.degrees));
    parts::placement::placeByImageCentre(brick, core::placePoint(to, own), parts);
}

}  // namespace bld::edit
