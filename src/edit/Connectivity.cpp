#include "Connectivity.h"

#include "../core/Brick.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QHash>
#include <QSet>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace bld::edit {

using core::Brick;
using core::LayerBrick;
using core::LayerKind;
using core::Map;

namespace {

QPointF rotatePoint(QPointF p, double degrees) {
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { p.x() * c - p.y() * s, p.x() * s + p.y() * c };
}

struct WorldConn {
    Brick*   brick = nullptr;
    int      connIdx = -1;
    QPointF  worldPos;
    QString  type;
};

// BlueBrick's arePositionsEqual: within half a stud on each axis.
constexpr double kTolStuds = 0.5;
// A bucket's key, one integer (hashed far faster than a pair).
quint64 bucketKey(int x, int y) {
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32) | static_cast<quint32>(y);
}

QPair<int, int> bucketOf(QPointF p) {
    // Clamped: a corrupt file can put a brick anywhere (or at NaN), and
    // converting an out-of-range double to int is undefined.
    const auto cell = [](double v) {
        if (!(v > -1e9)) return -1000000000;
        if (!(v < 1e9)) return 1000000000;
        return static_cast<int>(std::floor(v));
    };
    return { cell(p.x()), cell(p.y()) };
}

// BlueBrick's ConnectionPoint.ConnectionLink setter, when a link is made:
// if it lands on the brick's active connection, the active one moves to
// that connection's <nextConnexionPreference>, or failing that to the next
// free connection (wrapping; unchanged if none is free).
void onLinked(Brick& brick, int connIdx, const parts::PartMetadata& meta) {
    int& active = brick.activeConnectionPointIndex;
    if (active != connIdx) return;
    const int n = static_cast<int>(brick.connections.size());
    const int preferred = connIdx < meta.connections.size() ? meta.connections[connIdx].nextPreferredIndex : 0;
    active = std::clamp(preferred, 0, n - 1);
    // For a brick in a group BlueBrick moves the group's active connection
    // instead of looking for the brick's next free one.
    if (!brick.myGroupId.isEmpty()) return;
    if (brick.connections[active].linkedToId.isEmpty()) return;
    for (int step = 1; step < n; ++step) {
        const int candidate = (active + step) % n;
        if (brick.connections[candidate].linkedToId.isEmpty()) { active = candidate; return; }
    }
}

}  // namespace

void rebuildConnectivity(Map& map, parts::PartsLibrary& lib) {
    // Links are recomputed from positions, the way BlueBrick's
    // updateFullBrickConnectivity does it (per layer), but the previous
    // links are remembered so the active-connection rules only react to
    // links that actually changed.
    // Every connection id once in the map: a part pasted or inserted twice
    // used to keep its connection ids, so links named the wrong part (and a
    // bend followed them round in a circle). A repeat gets a new id; the
    // links are made again from positions below anyway.
    // Each part's metadata once per part number (a lookup copies it), and
    // its picture offset once per turn: this runs after every edit.
    QHash<QString, std::optional<parts::PartMetadata>> metas;
    const auto metaOf = [&](const QString& part) -> const parts::PartMetadata* {
        auto it = metas.constFind(part);
        if (it == metas.constEnd()) it = metas.insert(part, lib.metadata(part));
        return it->has_value() ? &**it : nullptr;
    };
    QHash<QPair<QString, float>, QPointF> offsets;
    const auto offsetOf = [&](const Brick& b) {
        const QPair<QString, float> key(b.partNumber, b.orientation);
        auto it = offsets.constFind(key);
        if (it == offsets.constEnd()) it = offsets.insert(key, lib.imageOffset(b.partNumber, b.orientation));
        return *it;
    };
    // (Sized up front: growing these tables was a good part of the time.)
    QSet<QString> ids;
    {
        qsizetype n = 0;
        for (auto& layerPtr : map.layers())
            if (layerPtr && layerPtr->kind() == LayerKind::Brick)
                for (const auto& brick : static_cast<LayerBrick&>(*layerPtr).bricks)
                    n += static_cast<qsizetype>(brick.connections.size());
        ids.reserve(n);
    }
    for (auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != LayerKind::Brick) continue;
        for (auto& brick : static_cast<LayerBrick&>(*layerPtr).bricks)
            for (auto& cp : brick.connections) {
                if (cp.guid.isEmpty()) continue;
                if (ids.contains(cp.guid)) {
                    cp.guid = core::newBbmId();
                    cp.linkedToId.clear();
                }
                ids.insert(cp.guid);
            }
    }

    for (auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != LayerKind::Brick) continue;
        auto& layer = static_cast<LayerBrick&>(*layerPtr);

        QHash<QString, QString> previous;  // connection guid -> old partner connection guid
        previous.reserve(static_cast<qsizetype>(layer.bricks.size()) * 2);
        std::vector<WorldConn> all;
        all.reserve(layer.bricks.size() * 2);
        // The connections of parts this library doesn't know: their points
        // can't be placed, so a link to one is kept as the file has it (it
        // was dropped, so opening a layout without that library changed it).
        QSet<QString> unknown;
        for (const auto& brick : layer.bricks)
            if (!metaOf(brick.partNumber))
                for (const auto& cp : brick.connections)
                    if (!cp.guid.isEmpty()) unknown.insert(cp.guid);
        for (auto& brick : layer.bricks) {
            const parts::PartMetadata* meta = metaOf(brick.partNumber);
            if (!meta) continue;
            const int n = meta->connections.size();
            while (static_cast<int>(brick.connections.size()) < n) brick.connections.push_back({});
            // Connection positions are relative to the sprite centre, which
            // is off the displayArea centre for parts with an XML hull.
            const QPointF centre = brick.displayArea.center() + offsetOf(brick);
            for (int i = 0; i < n; ++i) {
                auto& cp = brick.connections[i];
                // <LinkedTo> in a .bbm names the partner's connection, so
                // every connection point needs an id.
                if (cp.guid.isEmpty()) cp.guid = core::newBbmId();
                if (!cp.linkedToId.isEmpty() && unknown.contains(cp.linkedToId)) continue;
                if (!cp.linkedToId.isEmpty()) previous.insert(cp.guid, cp.linkedToId);
                cp.linkedToId.clear();
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                all.push_back({ &brick, i, centre + rotatePoint(c.position, brick.orientation), c.type });
            }
        }

        // BlueBrick walks bricks in order and links each free connection to
        // the FIRST free connection of the same type (in the same order) at
        // an equal position. Buckets make that O(N) without changing which
        // one wins: the lowest index among the candidates.
        QHash<quint64, std::vector<int>> bucket;
        bucket.reserve(static_cast<qsizetype>(all.size()));
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            const auto b = bucketOf(all[i].worldPos);
            bucket[bucketKey(b.first, b.second)].push_back(i);
        }

        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            auto& a = all[i];
            if (!a.brick->connections[a.connIdx].linkedToId.isEmpty()) continue;
            const auto ab = bucketOf(a.worldPos);
            int bestJ = -1;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const auto it = bucket.constFind(bucketKey(ab.first + dx, ab.second + dy));
                    if (it == bucket.constEnd()) continue;
                    for (int j : *it) {
                        if (j == i || (bestJ >= 0 && j >= bestJ)) continue;
                        const auto& b = all[j];
                        if (a.brick == b.brick || a.type != b.type) continue;
                        if (!b.brick->connections[b.connIdx].linkedToId.isEmpty()) continue;
                        if (std::abs(a.worldPos.x() - b.worldPos.x()) >= kTolStuds) continue;
                        if (std::abs(a.worldPos.y() - b.worldPos.y()) >= kTolStuds) continue;
                        bestJ = j;
                    }
                }
            }
            if (bestJ < 0) continue;
            auto& b = all[bestJ];
            auto& ca = a.brick->connections[a.connIdx];
            auto& cb = b.brick->connections[b.connIdx];
            // BlueBrick sets the two links one after the other, and hands
            // the active connection over BEFORE storing each link (so the
            // connection being linked still counts as free). Only new links
            // trigger the hand-over.
            const bool isNew = previous.value(ca.guid) != cb.guid;
            if (isNew)
                if (const auto* m = metaOf(a.brick->partNumber)) onLinked(*a.brick, a.connIdx, *m);
            ca.linkedToId = cb.guid;
            if (isNew)
                if (const auto* m = metaOf(b.brick->partNumber)) onLinked(*b.brick, b.connIdx, *m);
            cb.linkedToId = ca.guid;
        }

        // A link that broke frees its connection; if the brick's active
        // connection is taken, the freed one becomes active (BlueBrick).
        for (const auto& wc : all) {
            const auto& cp = wc.brick->connections[wc.connIdx];
            if (!previous.contains(cp.guid) || !cp.linkedToId.isEmpty()) continue;
            int& active = wc.brick->activeConnectionPointIndex;
            if (active >= 0 && active < static_cast<int>(wc.brick->connections.size())
                && !wc.brick->connections[active].linkedToId.isEmpty())
                active = wc.connIdx;
        }
    }
}

}  // namespace bld::edit
