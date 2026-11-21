#include "TrackNetwork.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::track {

QString switchKey(const QString& brick, int heel) { return brick + QLatin1Char('/') + QString::number(heel); }

std::optional<PortRef> parseSwitchKey(const QString& key) {
    const qsizetype at = key.lastIndexOf(QLatin1Char('/'));
    if (at <= 0) return std::nullopt;
    const QString tail = key.mid(at + 1);
    bool ok = false;
    const int heel = tail.toInt(&ok);
    if (!ok || heel < 0 || QString::number(heel) != tail) return std::nullopt;
    return PortRef{ key.left(at), heel };
}

TrackNetwork::TrackNetwork(const std::vector<TrackBrickInput>& bricks) {
    QHash<QString, PartTrack> memo;
    for (const TrackBrickInput& input : bricks) {
        if (input.conns.isEmpty()) continue;
        const QString memoKey = input.partKey.toLower() + QChar(0) + QString::number(input.conns.size());
        auto it = memo.constFind(memoKey);
        if (it == memo.constEnd()) it = memo.insert(memoKey, partTrack(input.partKey, input.conns));
        const PartTrack& track = *it;
        if (track.paths.isEmpty()) continue;
        bricks_.insert(input.id, BrickTrack{ input, track });
        order_ << input.id;
        for (int i = 0; i < input.connexions.size() && i < input.conns.size(); ++i)
            if (!input.connexions[i].first.isEmpty()) owner_.insert(input.connexions[i].first, PortRef{ input.id, i });
        for (const PartSwitch& sw : track.switches) {
            SwitchPoint point;
            static_cast<PartSwitch&>(point) = sw;
            point.brick = input.id;
            point.key = switchKey(input.id, sw.heel);
            switchIndex_.insert(point.key, static_cast<int>(switches_.size()));
            switches_.append(point);
        }
    }
    findGaps();
}

// Pair up loose ends that nearly meet: in brick id then connection order,
// each with the nearest one after it (ties: the first), of the same kind,
// another brick, within kGapReach and facing it.
void TrackNetwork::findGaps() {
    struct Loose {
        PortRef port;
        QPointF p;
        double angle;
        QString kind;
    };
    std::vector<Loose> loose;
    QStringList ids = order_;
    ids.sort();
    for (const QString& brick : ids) {
        const PartTrack& t = bricks_.value(brick).track;
        for (int conn = 0; conn < t.kinds.size(); ++conn) {
            const auto& kind = t.kinds[conn];
            if (!kind || linkedPartner({ brick, conn })) continue;
            if (const auto at = port(brick, conn)) loose.push_back({ { brick, conn }, at->first, at->second, *kind });
        }
    }
    std::vector<bool> used(loose.size(), false);
    for (size_t i = 0; i < loose.size(); ++i) {
        if (used[i]) continue;
        const Loose& a = loose[i];
        std::optional<size_t> best;
        double bestD = std::numeric_limits<double>::infinity();
        for (size_t j = i + 1; j < loose.size(); ++j) {
            const Loose& b = loose[j];
            if (used[j] || b.port.brick == a.port.brick || b.kind != a.kind) continue;
            const double d = std::hypot(b.p.x() - a.p.x(), b.p.y() - a.p.y());
            if (d > kGapReach || std::abs(wrapDegrees(a.angle - b.angle - 180.0)) > kGapTurn) continue;
            if (d < bestD - 1e-9) {
                best = j;
                bestD = d;
            }
        }
        if (!best) continue;
        const Loose& b = loose[*best];
        used[i] = true;
        used[*best] = true;
        bridges_.insert(switchKey(a.port.brick, a.port.conn), b.port);
        bridges_.insert(switchKey(b.port.brick, b.port.conn), a.port);
        gaps_.append({ a.port, b.port, (a.p + b.p) / 2.0, bestD });
    }
}

const PartTrack* TrackNetwork::trackOf(const QString& brick) const {
    const auto it = bricks_.constFind(brick);
    return it == bricks_.constEnd() ? nullptr : &it->track;
}

std::optional<PortRef> TrackNetwork::partner(const PortRef& port) const {
    if (auto linked = linkedPartner(port)) return linked;
    const auto it = bridges_.constFind(switchKey(port.brick, port.conn));
    if (it != bridges_.constEnd()) return *it;
    return std::nullopt;
}

std::optional<PortRef> TrackNetwork::linkedPartner(const PortRef& port) const {
    const auto b = bricks_.constFind(port.brick);
    if (b == bricks_.constEnd() || port.conn < 0 || port.conn >= b->input.connexions.size()) return std::nullopt;
    const QString& link = b->input.connexions[port.conn].second;
    if (link.isEmpty()) return std::nullopt;
    const auto other = owner_.constFind(link);
    if (other == owner_.constEnd()) return std::nullopt;
    const auto ob = bricks_.constFind(other->brick);
    if (ob == bricks_.constEnd()) return std::nullopt;
    const auto& kind = b->track.kinds[port.conn];
    const auto& otherKind = ob->track.kinds[other->conn];
    if (!kind || kind != otherKind) return std::nullopt;
    return *other;
}

QVector<int> TrackNetwork::exits(const QString& brick, int entry) const {
    const PartTrack* t = trackOf(brick);
    QVector<int> out;
    if (!t) return out;
    for (const auto& [i, j] : t->paths) {
        if (i == entry) out << j;
        else if (j == entry) out << i;
    }
    std::sort(out.begin(), out.end());
    return out;
}

const SwitchPoint* TrackNetwork::switchAt(const QString& key) const {
    const auto it = switchIndex_.constFind(key);
    return it == switchIndex_.constEnd() ? nullptr : &switches_[*it];
}

int TrackNetwork::settingOf(const SwitchPoint& sw, const QMap<QString, int>& states) const {
    const auto it = states.constFind(sw.key);
    return it != states.constEnd() && sw.exits.contains(*it) ? *it : sw.defaultExit;
}

std::optional<std::pair<QPointF, double>> TrackNetwork::port(const QString& brick, int conn) const {
    const auto b = bricks_.constFind(brick);
    if (b == bricks_.constEnd() || conn < 0 || conn >= b->input.conns.size() || conn >= b->input.points.size())
        return std::nullopt;
    return std::make_pair(b->input.points[conn], b->input.orientation + b->input.conns[conn].angle);
}

const std::vector<CircuitPathPoint>& TrackNetwork::path(const QString& brick, int from, int to) const {
    static const std::vector<CircuitPathPoint> none;
    const QString id = brick + QChar(0) + QString::number(from) + QChar(0) + QString::number(to);
    if (const auto it = paths_.constFind(id); it != paths_.constEnd()) return *it;
    if (!exits(brick, from).contains(to)) return none;
    const auto a = port(brick, from);
    const auto b = port(brick, to);
    if (!a || !b) return none;
    return *paths_.insert(id, circuitPath(a->first, a->second, b->first, b->second));
}

double TrackNetwork::pathLength(const QString& brick, int from, int to) const {
    const auto& pts = path(brick, from, to);
    return pts.empty() ? 0.0 : pts.back().s;
}

std::optional<CircuitPathPoint> TrackNetwork::pointAtPosition(const TrackPosition& pos) const {
    const auto& pts = path(pos.brick, pos.from, pos.to);
    if (pts.empty()) return std::nullopt;
    return pointAt(pts, std::clamp(pos.s, 0.0, pts.back().s));
}

QMap<QString, int> cleanSwitchStates(const QMap<QString, int>& states, const TrackNetwork& net) {
    QMap<QString, int> out;
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const SwitchPoint* sw = net.switchAt(it.key());
        if (sw && sw->exits.contains(it.value())) out.insert(it.key(), it.value());
    }
    return out;
}

QMap<QString, int> withoutGoneSwitches(const QMap<QString, int>& states, const QSet<QString>& bricks) {
    QMap<QString, int> out;
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const qsizetype at = it.key().lastIndexOf(QLatin1Char('/'));
        if (at > 0 && bricks.contains(it.key().left(at))) out.insert(it.key(), it.value());
    }
    return out;
}

int nextExit(const SwitchPoint& sw, int current) {
    const qsizetype i = sw.exits.indexOf(current);
    return sw.exits[(i + 1) % sw.exits.size()];
}

std::vector<TrackBrickInput> trackBricks(const core::Map& map, parts::PartsLibrary& lib) {
    std::vector<TrackBrickInput> out;
    QHash<QString, std::optional<parts::PartMetadata>> metas;
    QHash<QString, QVector<TrackConn>> convs;
    for (const auto& layer : map.layers()) {
        if (!layer || layer->kind() != core::LayerKind::Brick) continue;
        for (const core::Brick& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
            auto it = metas.constFind(b.partNumber);
            if (it == metas.constEnd()) it = metas.insert(b.partNumber, lib.metadata(b.partNumber));
            if (!it->has_value() || (*it)->connections.isEmpty()) continue;
            const parts::PartMetadata& meta = **it;
            auto conv = convs.constFind(b.partNumber);
            if (conv == convs.constEnd()) {
                QVector<TrackConn> conns;
                for (const auto& c : meta.connections)
                    conns.append({ c.type, c.position.x(), c.position.y(), c.angleDegrees, c.electricPlug });
                conv = convs.insert(b.partNumber, conns);
            }
            TrackBrickInput in;
            in.id = b.guid;
            in.partKey = meta.partNumber + QLatin1Char('.') + meta.colorCode;
            in.conns = *conv;
            in.orientation = b.orientation;
            const QPointF centre = parts::placement::imageCentre(b, lib);
            for (const auto& c : meta.connections) in.points.append(centre + parts::placement::rotated(c.position, b.orientation));
            for (const auto& cp : b.connections) in.connexions.append({ cp.guid, cp.linkedToId });
            out.push_back(std::move(in));
        }
    }
    return out;
}

TrackNetwork trackNetworkOf(const core::Map& map, parts::PartsLibrary& lib) { return TrackNetwork(trackBricks(map, lib)); }

QSet<QString> brickIdsOf(const core::Map& map) {
    QSet<QString> out;
    for (const auto& layer : map.layers())
        if (layer && layer->kind() == core::LayerKind::Brick)
            for (const core::Brick& b : static_cast<const core::LayerBrick&>(*layer).bricks) out.insert(b.guid);
    return out;
}

QString TrackNetwork::kindOf(const QString& brick) const {
    const auto it = bricks_.constFind(brick);
    if (it == bricks_.constEnd()) return {};
    for (const auto& k : it->track.kinds)
        if (k) return *k;
    return {};
}

bool TrackNetwork::isPowered(const QString& brick) const {
    const auto it = bricks_.constFind(brick);
    return it != bricks_.constEnd() && !electricCircuits(it->input.conns).isEmpty();
}

}  // namespace bld::track
