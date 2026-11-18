#include "SharedModuleKeep.h"

#include "../core/Groups.h"
#include "../core/Ids.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/SharedModule.h"
#include "../parts/BrickPlacement.h"

#include <algorithm>
#include <cmath>

namespace bld::edit {

namespace {

constexpr double kShift = 0.1;
constexpr double kTurn = 0.5;

QString versionKey(int b, int i) { return QStringLiteral("%1.%2").arg(b).arg(i); }

double turnApart(double a, double b) {
    const double d = std::abs(core::norm360(a) - core::norm360(b));
    return std::min(d, 360.0 - d);
}

// `id` and its parents among `groups`, innermost first.
QStringList chainIn(const std::vector<core::Group>& groups, QString id) {
    QStringList chain;
    while (!id.isEmpty() && !chain.contains(id) && chain.size() < 64) {
        const auto it = std::find_if(groups.begin(), groups.end(), [&](const core::Group& g) { return g.guid == id; });
        if (it == groups.end()) break;
        chain << id;
        id = it->myGroupId;
    }
    return chain;
}

}  // namespace

QSet<int> longestRun(const std::vector<int>& seq) {
    std::vector<int> tails;
    std::vector<int> prev(seq.size(), -1);
    for (int i = 0; i < static_cast<int>(seq.size()); ++i) {
        int lo = 0, hi = static_cast<int>(tails.size());
        while (lo < hi) {
            const int mid = (lo + hi) >> 1;
            if (seq[tails[mid]] < seq[i]) lo = mid + 1;
            else hi = mid;
        }
        if (lo > 0) prev[i] = tails[lo - 1];
        if (lo == static_cast<int>(tails.size())) tails.push_back(i);
        else tails[lo] = i;
    }
    QSet<int> keep;
    for (int k = tails.empty() ? -1 : tails.back(); k >= 0; k = prev[k]) keep.insert(k);
    return keep;
}

KeepPlan planKeep(const std::vector<KeepSheet>& sheets, const QSet<QString>& members, const LayerBatches& next,
                  parts::PartsLibrary& parts) {
    struct Here {
        const core::Brick* part;
        QString sheet;
        int at;
        QPointF pivot;
        double o;
    };
    std::vector<Here> here;
    QHash<QString, const std::vector<core::Group>*> groupsOf;
    for (const auto& s : sheets) {
        groupsOf.insert(s.id, &s.groups);
        for (int at = 0; at < static_cast<int>(s.parts.size()); ++at) {
            const core::Brick& p = s.parts[at];
            if (!members.contains(p.guid)) continue;
            here.push_back({ &p, s.id, at, parts::placement::imageCentre(p, parts), core::norm360(p.orientation) });
        }
    }
    // 1. Match.
    struct Pair {
        QString key;
        int batch, index;
        QString sheet;
        const Here* there;
    };
    std::vector<Pair> pairs;
    std::vector<bool> used(here.size(), false);
    for (int b = 0; b < static_cast<int>(next.size()); ++b) {
        const auto& batch = next[b];
        for (int i = 0; i < static_cast<int>(batch.bricks.size()); ++i) {
            const core::Brick& brick = batch.bricks[i];
            const QPointF p = parts::placement::imageCentre(brick, parts);
            const double o = core::norm360(brick.orientation);
            const QString pn = brick.partNumber.toLower();
            for (std::size_t k = 0; k < here.size(); ++k) {
                const Here& h = here[k];
                if (used[k] || h.sheet != batch.targetLayerGuid || h.part->partNumber.toLower() != pn) continue;
                if (std::abs(static_cast<double>(h.part->altitude) - static_cast<double>(brick.altitude)) >= 1e-6) continue;
                if (std::hypot(h.pivot.x() - p.x(), h.pivot.y() - p.y()) >= kShift || turnApart(h.o, o) >= kTurn) continue;
                used[k] = true;
                pairs.push_back({ versionKey(b, i), b, i, batch.targetLayerGuid, &h });
                break;
            }
        }
    }
    // 2. Order, sheet by sheet.
    std::vector<Pair> ordered;
    {
        QSet<QString> keep;
        for (const auto& s : sheets) {
            std::vector<const Pair*> on;
            for (const auto& p : pairs)
                if (p.sheet == s.id) on.push_back(&p);
            std::vector<int> seq;
            seq.reserve(on.size());
            for (const auto* p : on) seq.push_back(p->there->at);
            const QSet<int> run = longestRun(seq);
            for (int i = 0; i < static_cast<int>(on.size()); ++i)
                if (run.contains(i)) keep.insert(on[i]->key);
        }
        for (const auto& p : pairs)
            if (keep.contains(p.key)) ordered.push_back(p);
    }
    // 3. Sets, one for one.
    KeepPlan out;
    QHash<QString, QString> back;
    bool agree = true;
    for (const auto& p : ordered) {
        const auto* mineGroups = groupsOf.value(p.sheet);
        const QStringList mine = mineGroups ? chainIn(*mineGroups, p.there->part->myGroupId) : QStringList();
        const QStringList theirs = chainIn(next[p.batch].groups, next[p.batch].bricks[p.index].myGroupId);
        if (mine.size() != theirs.size()) {
            agree = false;
            break;
        }
        for (int k = 0; k < mine.size() && agree; ++k) {
            const QString v = QStringLiteral("%1/%2").arg(p.batch).arg(theirs[k]);
            if ((out.groupMap.contains(v) && out.groupMap.value(v) != mine[k]) || (back.contains(mine[k]) && back.value(mine[k]) != v))
                agree = false;
            out.groupMap.insert(v, mine[k]);
            back.insert(mine[k], v);
        }
        if (!agree) break;
    }
    if (!agree) {
        ordered.clear();
        out.groupMap.clear();
    }
    // 4. Change.
    QSet<QString> keptIds;
    for (const auto& p : ordered) {
        out.kept.insert(p.key, p.there->part->guid);
        keptIds.insert(p.there->part->guid);
    }
    for (const auto& h : here)
        if (!keptIds.contains(h.part->guid)) out.removed << h.part->guid;
    const QSet<QString> gone(out.removed.begin(), out.removed.end());
    for (const auto& s : sheets) {
        QStringList ids;
        for (const auto& p : s.parts)
            if (!gone.contains(p.guid)) ids << p.guid;
        out.sheets.insert(s.id, ids);
    }
    QMap<QString, QStringList> bySheet;
    QStringList sheetOrder;
    for (int b = 0; b < static_cast<int>(next.size()); ++b) {
        for (int i = 0; i < static_cast<int>(next[b].bricks.size()); ++i) {
            const QString key = versionKey(b, i);
            const QString id = out.kept.value(key);
            if (id.isEmpty()) out.added << key;
            const QString sheet = next[b].targetLayerGuid;
            if (!sheetOrder.contains(sheet)) sheetOrder << sheet;
            bySheet[sheet] << (id.isEmpty() ? QStringLiteral("new:") + key : id);
        }
    }
    for (const QString& sheet : sheetOrder) {
        if (!out.sheets.contains(sheet)) continue;
        QStringList& list = out.sheets[sheet];
        const QStringList want = bySheet.value(sheet);
        for (int i = 0; i < want.size(); ++i) {
            if (!want[i].startsWith(QLatin1String("new:"))) continue;
            int at = -1;
            for (int k = i - 1; k >= 0 && at < 0; --k)
                if (const int j = static_cast<int>(list.indexOf(want[k])); j >= 0) at = j + 1;
            if (at < 0)
                for (int k = i + 1; k < want.size() && at < 0; ++k) {
                    if (want[k].startsWith(QLatin1String("new:"))) continue;
                    if (const int j = static_cast<int>(list.indexOf(want[k])); j >= 0) at = j;
                }
            if (at < 0) at = static_cast<int>(list.size());
            list.insert(at, want[i]);
        }
    }
    for (const QString& key : out.added) {
        const int b = key.section(QLatin1Char('.'), 0, 0).toInt();
        const int i = key.section(QLatin1Char('.'), 1, 1).toInt();
        QStringList chain;
        for (const QString& g : chainIn(next[b].groups, next[b].bricks[i].myGroupId)) {
            const QString v = QStringLiteral("%1/%2").arg(b).arg(g);
            chain << (out.groupMap.contains(v) ? out.groupMap.value(v) : QStringLiteral("new:") + v);
        }
        out.newGroups.insert(key, chain);
    }
    return out;
}

std::vector<KeepSheet> keepSheets(const core::Map& map) {
    std::vector<KeepSheet> out;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& layer = static_cast<const core::LayerBrick&>(*L);
        out.push_back({ L->guid, layer.bricks, layer.groups });
    }
    return out;
}

QStringList applyKeep(core::Map& map, const KeepPlan& plan, const LayerBatches& next) {
    const QSet<QString> gone(plan.removed.begin(), plan.removed.end());
    // The new parts, under fresh ids, in their sets (new copies made once each).
    QHash<QString, core::Brick> fresh;
    QHash<QString, QString> made;
    QStringList members;
    for (int b = 0; b < static_cast<int>(next.size()); ++b) {
        for (int i = 0; i < static_cast<int>(next[b].bricks.size()); ++i) {
            const QString key = versionKey(b, i);
            if (plan.kept.contains(key)) {
                members << plan.kept.value(key);
                continue;
            }
            core::Brick brick = next[b].bricks[i];
            brick.guid = core::newBbmId();
            members << brick.guid;
            fresh.insert(QStringLiteral("new:") + key, brick);
        }
    }
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        auto& layer = static_cast<core::LayerBrick&>(*L);
        // The parts that go, and the sets only they were in.
        QSet<QString> freed;
        for (const auto& p : layer.bricks)
            if (gone.contains(p.guid))
                for (const QString& g : core::groupChain(layer, p.myGroupId)) freed.insert(g);
        layer.bricks.erase(std::remove_if(layer.bricks.begin(), layer.bricks.end(), [&](const core::Brick& p) { return gone.contains(p.guid); }),
                           layer.bricks.end());
        if (!freed.isEmpty()) {
            const QSet<QString> empty = core::emptyGroups(layer);
            layer.groups.erase(std::remove_if(layer.groups.begin(), layer.groups.end(),
                                              [&](const core::Group& g) { return freed.contains(g.guid) && empty.contains(g.guid); }),
                               layer.groups.end());
        }
        const QStringList order = plan.sheets.value(L->guid);
        if (std::none_of(order.begin(), order.end(), [&](const QString& t) { return fresh.contains(t); })) continue;
        // Sets for the new parts on this sheet.
        const auto setFor = [&](const QString& key) {
            QString parent;
            const QStringList chain = plan.newGroups.value(key);
            for (int k = static_cast<int>(chain.size()) - 1; k >= 0; --k) {
                const QString& token = chain[k];
                if (!token.startsWith(QLatin1String("new:"))) {
                    parent = token;
                    continue;
                }
                QString id = made.value(token);
                if (id.isEmpty()) {
                    id = core::newBbmId();
                    made.insert(token, id);
                    const int b = token.mid(4).section(QLatin1Char('/'), 0, 0).toInt();
                    const QString gid = token.mid(4).section(QLatin1Char('/'), 1);
                    core::Group g;
                    for (const auto& vg : next[b].groups)
                        if (vg.guid == gid) g = vg;
                    g.guid = id;
                    g.myGroupId = parent;
                    layer.groups.push_back(g);
                }
                parent = id;
            }
            return parent;
        };
        std::vector<core::Brick> rebuilt;
        QHash<QString, const core::Brick*> byId;
        for (const auto& p : layer.bricks) byId.insert(p.guid, &p);
        for (const QString& t : order) {
            if (fresh.contains(t)) {
                core::Brick brick = fresh.value(t);
                brick.myGroupId = setFor(t.mid(4));
                rebuilt.push_back(std::move(brick));
            } else if (const core::Brick* p = byId.value(t)) {
                rebuilt.push_back(*p);
            }
        }
        layer.bricks = std::move(rebuilt);
    }
    return members;
}

}  // namespace bld::edit
