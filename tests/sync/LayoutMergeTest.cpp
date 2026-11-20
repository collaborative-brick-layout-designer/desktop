// Three-way compare and merge for reconnecting after offline edits.

#include "sync/LayoutMerge.h"

#include "core/LayerBrick.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "core/SharedModule.h"
#include "sync/SyncDoc.h"
#include "sync/WebModel.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

using namespace bld;
using namespace bld::sync::merge;

namespace {

const QString kDir = QStringLiteral(BLD_SOURCE_DIR "/fixtures/sync/compare/");

// A shared document as the desktop holds it, read as a desktop map.
std::unique_ptr<core::Map> load(const QString& name) {
    QFile f(kDir + name + QStringLiteral(".bin"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    sync::SyncDoc doc;
    QString error;
    EXPECT_TRUE(doc.applyUpdate(f.readAll(), &error)) << error.toStdString();
    auto map = sync::mapFromDocJson(doc.toJson(), &error);
    EXPECT_TRUE(map) << error.toStdString();
    return map;
}

struct Case {
    std::unique_ptr<core::Map> base = load(QStringLiteral("base")), mine = load(QStringLiteral("mine")),
                               server = load(QStringLiteral("server"));
    Snapshot b = snapshotOf(*base), m = snapshotOf(*mine), s = snapshotOf(*server);
    QList<ItemChange> changes = compareLayouts(b, m, s);
};

const char* name(Status s) {
    switch (s) {
    case Status::Mine: return "mine";
    case Status::Server: return "server";
    case Status::Same: return "same";
    case Status::Conflict: return "conflict";
    }
    return "?";
}

const char* name(Side s) {
    switch (s) {
    case Side::Added: return "added";
    case Side::Edited: return "edited";
    case Side::Deleted: return "deleted";
    case Side::Unchanged: return "unchanged";
    }
    return "?";
}

const core::Brick* brick(const core::Map& map, const QString& id) {
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (b.guid == id) return &b;
    return nullptr;
}

int brickCount(const core::Map& map) {
    int n = 0;
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick) n += static_cast<int>(static_cast<const core::LayerBrick&>(*l).bricks.size());
    return n;
}

// "id=name" for each saved view, in list order.
QStringList viewNames(const core::Map& map) {
    QStringList out;
    for (const auto& v : map.sidecar.views) out << v.id + QLatin1Char('=') + v.name;
    return out;
}

const core::SavedView* view(const core::Map& map, const QString& id) {
    for (const auto& v : map.sidecar.views)
        if (v.id == id) return &v;
    return nullptr;
}

QStringList labelTexts(const core::Map& map) {
    QStringList out;
    for (const auto& l : map.sidecar.anchoredLabels) out << l.text;
    out.sort();
    return out;
}

}  // namespace

// fixtures/sync/compare/expected.json is what the web server's compare
// reports for the same three files (scripts/sync-fixtures/make-compare-fixture.mjs).
TEST(LayoutMerge, ReportsWhatTheServersCompareReports) {
    Case c;
    QFile f(kDir + QStringLiteral("expected.json"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QStringList expected, got;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("changes")).toArray()) {
        const QJsonObject o = v.toObject();
        expected << QStringLiteral("%1 %2 %3 %4 %5")
                        .arg(o.value(QLatin1String("key")).toString(), o.value(QLatin1String("kind")).toString(),
                             o.value(QLatin1String("status")).toString(), o.value(QLatin1String("mine")).toString(),
                             o.value(QLatin1String("server")).toString());
    }
    for (const auto& ch : c.changes)
        got << QStringLiteral("%1 %2 %3 %4 %5")
                   .arg(ch.key, ch.kind, QString::fromLatin1(name(ch.status)), QString::fromLatin1(name(ch.mine)),
                        QString::fromLatin1(name(ch.server)));
    EXPECT_EQ(got.join(QLatin1Char('\n')).toStdString(), expected.join(QLatin1Char('\n')).toStdString());
}

TEST(LayoutMerge, ByDefaultKeepsMyChangesAndTheServersOnClashes) {
    Case c;
    QString error;
    const auto merged = mergeLayouts(c.m, c.s, c.changes, {}, &error);
    ASSERT_TRUE(merged) << error.toStdString();

    // Only mine: in.
    EXPECT_EQ(brick(*merged, QStringLiteral("222"))->orientation, brick(*c.mine, QStringLiteral("222"))->orientation);
    EXPECT_EQ(brick(*merged, QStringLiteral("223")), nullptr);
    ASSERT_NE(brick(*merged, QStringLiteral("mine-new-brick")), nullptr);
    // Only the server's: kept.
    EXPECT_EQ(brick(*merged, QStringLiteral("3226")), nullptr);
    EXPECT_TRUE(merged->sidecar.venue.has_value());
    // A clash: the server's.
    EXPECT_EQ(brick(*merged, QStringLiteral("224"))->displayArea, brick(*c.server, QStringLiteral("224"))->displayArea);
    EXPECT_EQ(merged->author, c.server->author);
    EXPECT_EQ(merged->event, c.server->event);
    EXPECT_EQ(labelTexts(*merged), (QStringList{ QStringLiteral("Added offline"), QStringLiteral("Server says this") }));
    // Mine's text edit.
    bool edited = false;
    for (const auto& l : merged->layers())
        if (l->kind() == core::LayerKind::Text)
            for (const auto& t : static_cast<const core::LayerText&>(*l).textCells) edited |= t.text == QStringLiteral("Edited offline");
    EXPECT_TRUE(edited);
    EXPECT_EQ(brickCount(*merged), brickCount(*c.base) - 2 + 1);
    // Saved views: the server's name on the clash and its delete, my
    // changed area (with the field this build doesn't know) and my new view.
    EXPECT_EQ(viewNames(*merged), (QStringList{ QStringLiteral("V-both=Server calls it this"),
                                                QStringLiteral("V-keep=Mine changes this"),
                                                QStringLiteral("V-mine=Added offline") }));
    const core::SavedView* keep = view(*merged, QStringLiteral("V-keep"));
    ASSERT_NE(keep, nullptr);
    EXPECT_FALSE(keep->fit);
    EXPECT_EQ(keep->rect, QRectF(10, 20, 30, 40));
    EXPECT_EQ(keep->sheets, QStringList{ QStringLiteral("211") });
    EXPECT_EQ(keep->extras.value(QLatin1String("note")).toString(), QStringLiteral("kept"));

    // Merged, it's the server's layout plus my changes: comparing it with
    // the server as "mine" shows only those.
    const auto after = compareLayouts(c.s, snapshotOf(*merged), c.s);
    QStringList keys;
    for (const auto& ch : after) keys << ch.key;
    EXPECT_EQ(keys.join(QLatin1Char(' ')).toStdString(), (QStringList{ QStringLiteral("brick:211:222"), QStringLiteral("brick:211:223"),
                                  QStringLiteral("brick:211:mine-new-brick"), QStringLiteral("label:L-mine"),
                                  QStringLiteral("text:3144:T0"), QStringLiteral("view:V-keep"),
                                  QStringLiteral("view:V-mine") })
                                    .join(QLatin1Char(' '))
                                    .toStdString());
}

TEST(LayoutMerge, TakesTheChoicesMadeForClashes) {
    Case c;
    const QHash<QString, Choice> choices{
        { QStringLiteral("brick:211:224"), Choice::Both },
        { QStringLiteral("label:L-both"), Choice::Mine },
        { QStringLiteral("map"), Choice::Mine },
        { QStringLiteral("venue"), Choice::Mine },  // mine has none: the server's goes
        { QStringLiteral("brick:211:222"), Choice::Server },  // my own change dropped
        { QStringLiteral("view:V-both"), Choice::Mine },
        { QStringLiteral("view:V-keep"), Choice::Server },
    };
    const auto merged = mergeLayouts(c.m, c.s, c.changes, choices);
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->author, c.mine->author);
    EXPECT_EQ(merged->event, c.mine->event);
    EXPECT_FALSE(merged->sidecar.venue.has_value());
    EXPECT_EQ(labelTexts(*merged), (QStringList{ QStringLiteral("Added offline"), QStringLiteral("Mine says this") }));
    EXPECT_EQ(brick(*merged, QStringLiteral("222"))->orientation, brick(*c.server, QStringLiteral("222"))->orientation);
    EXPECT_EQ(viewNames(*merged), (QStringList{ QStringLiteral("V-both=Mine calls it this"),
                                                QStringLiteral("V-keep=Mine changes this"),
                                                QStringLiteral("V-mine=Added offline") }));
    EXPECT_TRUE(view(*merged, QStringLiteral("V-keep"))->fit);  // the server's, unchanged
    // Both: the server's stays, mine comes in again as a copy.
    EXPECT_EQ(brick(*merged, QStringLiteral("224"))->displayArea, brick(*c.server, QStringLiteral("224"))->displayArea);
    const core::Brick* copy = nullptr;
    for (const auto& l : merged->layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (!brick(*c.mine, b.guid) && !brick(*c.server, b.guid)) copy = &b;
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->displayArea, brick(*c.mine, QStringLiteral("224"))->displayArea);
    for (const auto& link : copy->connections) EXPECT_TRUE(link.linkedToId.isEmpty());
}

TEST(LayoutMerge, MyBrickBringsBackTheLayerTheServerDeleted) {
    Case c;
    // The server deletes brick layer 211; mine only added a brick to it.
    Snapshot server = c.b;
    QJsonObject data = server.doc.value(QLatin1String("layerData")).toObject();
    data.remove(QStringLiteral("211"));
    server.doc.insert(QStringLiteral("layerData"), data);
    QJsonArray order = server.doc.value(QLatin1String("layers")).toArray();
    const int at = static_cast<int>(order.toVariantList().indexOf(QStringLiteral("211")));
    ASSERT_GE(at, 0);
    order.removeAt(at);
    server.doc.insert(QStringLiteral("layers"), order);
    Snapshot mine = c.b;
    QJsonObject mineData = mine.doc.value(QLatin1String("layerData")).toObject();
    QJsonObject layer = mineData.value(QStringLiteral("211")).toObject();
    QJsonArray bricks = layer.value(QLatin1String("bricks")).toArray();
    QJsonObject added = bricks[0].toObject();
    added.insert(QStringLiteral("id"), QStringLiteral("offline-brick"));
    bricks.append(added);
    layer.insert(QStringLiteral("bricks"), bricks);
    mineData.insert(QStringLiteral("211"), layer);
    mine.doc.insert(QStringLiteral("layerData"), mineData);

    const auto changes = compareLayouts(c.b, mine, server);
    const auto merged = mergeLayouts(mine, server, changes, {});
    ASSERT_TRUE(merged);
    const auto layers = snapshotOf(*merged).doc.value(QLatin1String("layers")).toArray();
    EXPECT_EQ(layers.toVariantList().indexOf(QStringLiteral("211")), at);  // back where it was
    ASSERT_NE(brick(*merged, QStringLiteral("offline-brick")), nullptr);
    EXPECT_EQ(brick(*merged, bricks[0].toObject().value(QLatin1String("id")).toString()), nullptr);

    // With the layers before it gone from the server too, it comes back first.
    Snapshot fewer = server;
    QJsonArray rest = fewer.doc.value(QLatin1String("layers")).toArray();
    for (int i = 0; i < at; ++i) rest.removeAt(0);
    fewer.doc.insert(QStringLiteral("layers"), rest);
    const auto first = mergeLayouts(mine, fewer, compareLayouts(c.b, mine, fewer), {});
    ASSERT_TRUE(first);
    EXPECT_EQ(snapshotOf(*first).doc.value(QLatin1String("layers")).toArray().first().toString(), QStringLiteral("211"));

    // Taking the server's side of the layer too drops it with my brick.
    QHash<QString, Choice> server211;
    for (const auto& ch : changes)
        if (ch.layerId == QLatin1String("211")) server211.insert(ch.key, Choice::Server);
    const auto dropped = mergeLayouts(mine, server, changes, server211);
    ASSERT_TRUE(dropped);
    EXPECT_EQ(brick(*dropped, QStringLiteral("offline-brick")), nullptr);
}

TEST(LayoutMerge, SaysWhatEachChangeIs) {
    Case c;
    QStringList lines;
    for (const auto& ch : c.changes) lines << describe(ch);
    // Bricks of one part tell apart by where they are and their layer.
    EXPECT_TRUE(lines.contains(
        QStringLiteral("Brick TABLE96X190 at (296, 96) on \"Tables\": you changed it, the server changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Brick TABLE96X190 at (288, 192) on \"Tables\": you changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Brick TABLE96X190 at (96, 192) on \"Tables\": you deleted it")));
    EXPECT_TRUE(
        lines.contains(QStringLiteral("Brick TABLE96X190 at (96, 112) on \"Tables\": you and the server both changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Brick 3811.1 at (176, 208) on \"Buildings\": the server deleted it")));
    EXPECT_TRUE(lines.contains(
        QStringLiteral("Text \"Edited offline\" at (332.6, 212.1) on \"Building labels\": you changed it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Label \"Added offline\": you added it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("Venue: the server added it")));
    EXPECT_TRUE(lines.contains(QStringLiteral("View \"Added offline\": you added it")));
    for (const auto& ch : c.changes) {
        const bool copyable = ch.kind == QLatin1String("brick") || ch.kind == QLatin1String("text")
                           || ch.kind == QLatin1String("label") || ch.kind == QLatin1String("module")
                           || ch.kind == QLatin1String("view");
        EXPECT_EQ(ch.allowsBoth(), copyable && !ch.mineValue.isUndefined() && !ch.serverValue.isUndefined())
            << ch.key.toStdString();
    }
}

// Where the item is comes from mine, else the server's, else base; a group
// says its part; no layer name, no "on".
TEST(LayoutMerge, SaysWhereFromWhicheverSideHasIt) {
    const auto brickAt = [](double x, double y) {
        return QJsonObject{ { QStringLiteral("partNumber"), QStringLiteral("3001") },
                            { QStringLiteral("displayArea"),
                              QJsonObject{ { QStringLiteral("x"), x }, { QStringLiteral("y"), y } } } };
    };
    ItemChange c;
    c.kind = QStringLiteral("brick");
    c.layerName = QStringLiteral("Track");
    c.base = brickAt(1, 2);
    c.serverValue = brickAt(-3.04, 4.25);
    c.mine = Side::Deleted;
    EXPECT_EQ(describe(c).toStdString(), std::string("Brick 3001 at (-3, 4.3) on \"Track\": you deleted it"));
    c.mineValue = brickAt(7, 8);
    c.mine = Side::Edited;
    EXPECT_EQ(describe(c).toStdString(), std::string("Brick 3001 at (7, 8) on \"Track\": you changed it"));
    c.mineValue = QJsonValue(QJsonValue::Undefined);
    c.serverValue = QJsonValue(QJsonValue::Undefined);
    c.mine = Side::Deleted;
    c.layerName.clear();
    EXPECT_EQ(describe(c).toStdString(), std::string("Brick 3001 at (1, 2): you deleted it"));

    ItemChange g;
    g.kind = QStringLiteral("group");
    g.layerName = QStringLiteral("Track");
    g.mineValue = QJsonObject{ { QStringLiteral("partNumber"), QStringLiteral("SET-1") } };
    g.mine = Side::Added;
    EXPECT_EQ(describe(g).toStdString(), std::string("Group SET-1 on \"Track\": you added it"));
    g.mineValue = QJsonObject{};
    EXPECT_EQ(describe(g).toStdString(), std::string("Group on \"Track\": you added it"));
}

// The budget is compared one part at a time, and a placed set by its group:
// neither is in the server's compare, and both were dropped by a merge.
TEST(LayoutMerge, BudgetLimitsAndPlacedSetsAreComparedAndMerged) {
    Case c;
    using Budget = QMap<QString, int>;
    c.base->sidecar.budgetLimits = Budget{ { QStringLiteral("A"), 1 }, { QStringLiteral("B"), 2 }, { QStringLiteral("C"), 3 },
                                           { QStringLiteral("D"), 4 } };
    // Mine: A raised, B taken out, E new. The server: C lowered, D raised (as mine does, differently).
    c.mine->sidecar.budgetLimits = Budget{ { QStringLiteral("A"), 10 }, { QStringLiteral("C"), 3 }, { QStringLiteral("D"), 40 },
                                           { QStringLiteral("E"), 5 } };
    c.server->sidecar.budgetLimits = Budget{ { QStringLiteral("A"), 1 }, { QStringLiteral("B"), 2 }, { QStringLiteral("C"), 0 },
                                             { QStringLiteral("D"), 44 } };
    core::PlacedSet set;
    set.groupId = QStringLiteral("G-offline");
    set.version = QStringLiteral("v1");
    c.mine->sidecar.sets.push_back(set);
    const Snapshot b = snapshotOf(*c.base), m = snapshotOf(*c.mine), s = snapshotOf(*c.server);
    const auto changes = compareLayouts(b, m, s);

    QStringList lines;
    for (const auto& ch : changes)
        if (ch.kind == QLatin1String("budget") || ch.kind == QLatin1String("set")) lines << describe(ch);
    EXPECT_EQ(lines.join(QLatin1Char('\n')).toStdString(),
              "Budget limit for A: you changed it\n"
              "Budget limit for B: you deleted it\n"
              "Budget limit for C: the server changed it\n"
              "Budget limit for D (yours: 40, the server's: 44): you changed it, the server changed it\n"
              "Budget limit for E: you added it\n"
              "Custom set: you added it");
    for (const auto& ch : changes) {
        if (ch.kind == QLatin1String("budget") || ch.kind == QLatin1String("set")) {
            EXPECT_FALSE(ch.allowsBoth());
        }
    }

    // By default: mine where nothing clashes, the server's on the clash.
    auto merged = mergeLayouts(m, s, changes, {});
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->sidecar.budgetLimits, (Budget{ { QStringLiteral("A"), 10 }, { QStringLiteral("C"), 0 },
                                                     { QStringLiteral("D"), 44 }, { QStringLiteral("E"), 5 } }));
    ASSERT_EQ(merged->sidecar.sets.size(), 1u);
    EXPECT_EQ(merged->sidecar.sets[0].groupId, QStringLiteral("G-offline"));
    // "Keep mine" on the clash.
    merged = mergeLayouts(m, s, changes, { { QStringLiteral("budget:D"), Choice::Mine } });
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->sidecar.budgetLimits->value(QStringLiteral("D")), 40);

    // The whole budget taken away offline: gone after the merge, not left empty.
    c.mine->sidecar.budgetLimits.reset();
    c.server->sidecar.budgetLimits = c.base->sidecar.budgetLimits;
    const Snapshot none = snapshotOf(*c.mine), same = snapshotOf(*c.server);
    merged = mergeLayouts(none, same, compareLayouts(b, none, same), {});
    ASSERT_TRUE(merged);
    EXPECT_FALSE(merged->sidecar.budgetLimits.has_value());
}

// ── A module both sides changed (offline, then back) ──

namespace {

Snapshot withModule(const QString& moduleName, const QSet<QString>& members, const QString& outline = {}) {
    core::Map map;
    core::Module m;
    m.id = QStringLiteral("M");
    m.name = moduleName;
    m.memberIds = members;
    m.outlineColor = outline;
    map.sidecar.modules.push_back(m);
    return snapshotOf(map);
}

const core::Module& onlyModule(const core::Map& map) {
    EXPECT_EQ(map.sidecar.modules.size(), 1U);
    return map.sidecar.modules.front();
}

}  // namespace

// The parts I added or took out offline go on top of the server's list, and
// a field only one side changed is kept: nothing to ask about.
TEST(LayoutMerge, AModuleBothSidesChangedMergesItsPartsWithoutAsking) {
    const Snapshot base = withModule(QStringLiteral("Yard"), { QStringLiteral("a"), QStringLiteral("b") });
    const Snapshot mine =
        withModule(QStringLiteral("Yard"), { QStringLiteral("b"), QStringLiteral("c") }, QStringLiteral("#ff8800"));
    const Snapshot server = withModule(QStringLiteral("Harbour"), { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("d") });
    const QList<ItemChange> changes = compareLayouts(base, mine, server);
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0].key, QStringLiteral("module:M"));
    EXPECT_EQ(changes[0].status, Status::Mine) << name(changes[0].status);
    EXPECT_EQ(changes[0].defaultChoice(), Choice::Mine);
    QString error;
    const auto merged = mergeLayouts(mine, server, changes, {}, &error);
    ASSERT_TRUE(merged) << error.toStdString();
    const core::Module& m = onlyModule(*merged);
    // Mine: a out, c in. The server's: d in, the new name.
    EXPECT_EQ(m.memberIds, (QSet<QString>{ QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d") }));
    EXPECT_EQ(m.name, QStringLiteral("Harbour"));
    EXPECT_EQ(m.outlineColor, QStringLiteral("#ff8800"));
}

TEST(LayoutMerge, AModuleWhereBothMadeTheSamePartChangeHasNothingLeftToPutIn) {
    const Snapshot base = withModule(QStringLiteral("Yard"), { QStringLiteral("a"), QStringLiteral("b") });
    const Snapshot mine = withModule(QStringLiteral("Yard"), { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") });
    const Snapshot server =
        withModule(QStringLiteral("Harbour"), { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") });
    const QList<ItemChange> changes = compareLayouts(base, mine, server);
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0].status, Status::Same) << name(changes[0].status);
    const auto merged = mergeLayouts(mine, server, changes, {});
    ASSERT_TRUE(merged);
    EXPECT_EQ(onlyModule(*merged).name, QStringLiteral("Harbour"));
    EXPECT_EQ(onlyModule(*merged).memberIds, (QSet<QString>{ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") }));
}

// Both renamed it, differently: that is asked about. Whichever name is
// kept, keeping mine keeps the parts of both.
TEST(LayoutMerge, AModuleBothRenamedIsAClashAndKeepingMineStillMergesTheParts) {
    const Snapshot base = withModule(QStringLiteral("Yard"), { QStringLiteral("a"), QStringLiteral("b") });
    const Snapshot mine = withModule(QStringLiteral("North yard"), { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") });
    const Snapshot server = withModule(QStringLiteral("South yard"), { QStringLiteral("b"), QStringLiteral("d") });
    const QList<ItemChange> changes = compareLayouts(base, mine, server);
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0].status, Status::Conflict) << name(changes[0].status);
    // Unasked: the server's.
    const auto byDefault = mergeLayouts(mine, server, changes, {});
    ASSERT_TRUE(byDefault);
    EXPECT_EQ(onlyModule(*byDefault).name, QStringLiteral("South yard"));
    EXPECT_EQ(onlyModule(*byDefault).memberIds, (QSet<QString>{ QStringLiteral("b"), QStringLiteral("d") }));
    // Keep mine: my name, and the parts as both changed them.
    const auto kept = mergeLayouts(mine, server, changes, { { QStringLiteral("module:M"), Choice::Mine } });
    ASSERT_TRUE(kept);
    EXPECT_EQ(onlyModule(*kept).name, QStringLiteral("North yard"));
    EXPECT_EQ(onlyModule(*kept).memberIds, (QSet<QString>{ QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d") }));
}

// Only one side changed it: as before.
TEST(LayoutMerge, AModuleOnlyOneSideChangedIsThatSides) {
    const Snapshot base = withModule(QStringLiteral("Yard"), { QStringLiteral("a"), QStringLiteral("b") });
    const Snapshot mine = withModule(QStringLiteral("Yard"), { QStringLiteral("a") });
    const QList<ItemChange> changes = compareLayouts(base, mine, base);
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0].status, Status::Mine);
    const auto merged = mergeLayouts(mine, base, changes, {});
    ASSERT_TRUE(merged);
    EXPECT_EQ(onlyModule(*merged).memberIds, QSet<QString>{ QStringLiteral("a") });
}

// ---- Shared (read-only) modules (docs/shared-modules.md) --------------------

namespace {

const QString kSharedLib = QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b");

// A layout with a loose part and the shared module "M" at `version`, its
// parts (id, x) on one sheet, sitting at `at` (the version's origin).
Snapshot withShared(int version, const QList<std::pair<QString, double>>& parts, QPointF at, double degrees = 0,
                    double looseX = 500) {
    core::Map map;
    auto sheet = std::make_unique<core::LayerBrick>();
    sheet->guid = QStringLiteral("L1");
    sheet->name = QStringLiteral("Track");
    const auto add = [&](const QString& id, double x, double y) {
        core::Brick b;
        b.guid = id;
        b.partNumber = QStringLiteral("3001.1");
        b.displayArea = QRectF(x - 2, y - 1, 4, 2);
        b.orientation = static_cast<float>(degrees);
        sheet->bricks.push_back(b);
    };
    core::Module m;
    m.id = QStringLiteral("M");
    m.name = QStringLiteral("Harbor crane");
    m.libraryModuleId = kSharedLib;
    m.libraryVersion = version;
    const core::SharedPlacement placement{ degrees, at.x(), at.y(), QMap<QString, QString>{ { QStringLiteral("Track"), QStringLiteral("L1") } } };
    for (const auto& [id, x] : parts) {
        // (x along the version's own x axis, from its origin)
        const QPointF p = core::placePoint(placement, QPointF(x, 0));
        add(id, p.x(), p.y());
        m.memberIds.insert(id);
    }
    add(QStringLiteral("loose"), looseX, 300);
    m.shared = QJsonObject{ { QStringLiteral("by"), QStringLiteral("Alice") }, { QStringLiteral("placement"), core::placementJson(placement) } };
    map.sidecar.modules.push_back(m);
    map.layers().push_back(std::move(sheet));
    return snapshotOf(map);
}

}  // namespace

// Offline I moved a shared module; meanwhile the server put its owner's
// next version in. Back online it is the server's: its parts are the new
// version's, nothing clashes, and my move is put back on top through its
// placement, so the new parts sit where I moved the module to.
TEST(LayoutMerge, ASharedModuleTheServerUpdatedIsTheServersWithMyOfflineMoveOnTop) {
    const Snapshot base = withShared(7, { { "a", 0 }, { "b", 10 } }, QPointF(100, 50));
    // Mine: moved by (40, -8), and the loose part too.
    const Snapshot mine = withShared(7, { { "a", 0 }, { "b", 10 } }, QPointF(140, 42), 0, 520);
    // The server: version 8, new part ids, one part more, where it sat.
    const Snapshot server = withShared(8, { { "n1", 0 }, { "n2", 10 }, { "n3", 20 } }, QPointF(100, 50));
    const QList<ItemChange> changes = compareLayouts(base, mine, server);
    for (const auto& c : changes) EXPECT_NE(c.status, Status::Conflict) << c.key.toStdString() << ": nothing to ask about";

    const auto merged = mergeLayouts(mine, server, changes, {});
    ASSERT_TRUE(merged);
    const core::Module& m = onlyModule(*merged);
    EXPECT_EQ(m.libraryVersion, 8);
    EXPECT_EQ(m.memberIds, (QSet<QString>{ QStringLiteral("n1"), QStringLiteral("n2"), QStringLiteral("n3") }));
    EXPECT_FALSE(brick(*merged, QStringLiteral("a"))) << "the old version's parts are gone, moved or not";
    EXPECT_FALSE(brick(*merged, QStringLiteral("b")));
    EXPECT_EQ(brickCount(*merged), 4);
    // The new parts, where I moved the module.
    ASSERT_TRUE(brick(*merged, QStringLiteral("n1")));
    EXPECT_DOUBLE_EQ(brick(*merged, QStringLiteral("n1"))->displayArea.center().x(), 140);
    EXPECT_DOUBLE_EQ(brick(*merged, QStringLiteral("n1"))->displayArea.center().y(), 42);
    EXPECT_DOUBLE_EQ(brick(*merged, QStringLiteral("n3"))->displayArea.center().x(), 160);
    const auto at = core::sharedPlaced(m)->placement;
    ASSERT_TRUE(at);
    EXPECT_DOUBLE_EQ(at->x, 140);
    EXPECT_DOUBLE_EQ(at->y, 42);
    // My other offline edit is kept as ever.
    EXPECT_DOUBLE_EQ(brick(*merged, QStringLiteral("loose"))->displayArea.center().x(), 520);
}

// A quarter turn offline goes on top of the server's version too.
TEST(LayoutMerge, ASharedModuleITurnedOfflineIsTurnedAgainOverTheServersVersion) {
    const Snapshot base = withShared(7, { { "a", 0 }, { "b", 10 } }, QPointF(100, 50));
    const Snapshot mine = withShared(7, { { "a", 0 }, { "b", 10 } }, QPointF(100, 50), 90);
    const Snapshot server = withShared(8, { { "n1", 0 }, { "n2", 12 } }, QPointF(100, 50));
    const QList<ItemChange> changes = compareLayouts(base, mine, server);
    const auto merged = mergeLayouts(mine, server, changes, {});
    ASSERT_TRUE(merged);
    const core::Brick* n2 = brick(*merged, QStringLiteral("n2"));
    ASSERT_TRUE(n2);
    EXPECT_NEAR(n2->displayArea.center().x(), 100, 1e-9);
    EXPECT_NEAR(n2->displayArea.center().y(), 62, 1e-9);
    EXPECT_NEAR(n2->orientation, 90, 1e-6);
    EXPECT_NEAR(n2->displayArea.width(), 2, 1e-9) << "its box turned with it";
    EXPECT_NEAR(core::sharedPlaced(onlyModule(*merged))->placement->degrees, 90, 1e-9);
}

// The server updated it and I didn't touch it: simply the server's. And a
// shared module only I moved (no update meanwhile) is my move, as for any module.
TEST(LayoutMerge, ASharedModuleOnlyOneSideTouchedIsThatSides) {
    const Snapshot base = withShared(7, { { "a", 0 }, { "b", 10 } }, QPointF(100, 50));
    const Snapshot server = withShared(8, { { "n1", 0 } }, QPointF(100, 50));
    auto changes = compareLayouts(base, base, server);
    for (const auto& c : changes) EXPECT_EQ(c.status, Status::Server) << c.key.toStdString();
    auto merged = mergeLayouts(base, server, changes, {});
    ASSERT_TRUE(merged);
    EXPECT_EQ(onlyModule(*merged).memberIds, QSet<QString>{ QStringLiteral("n1") });

    const Snapshot mine = withShared(7, { { "a", 0 }, { "b", 10 } }, QPointF(132, 50));
    changes = compareLayouts(base, mine, base);
    for (const auto& c : changes) EXPECT_EQ(c.status, Status::Mine) << c.key.toStdString();
    merged = mergeLayouts(mine, base, changes, {});
    ASSERT_TRUE(merged);
    EXPECT_DOUBLE_EQ(brick(*merged, QStringLiteral("a"))->displayArea.center().x(), 132);
    EXPECT_DOUBLE_EQ(core::sharedPlaced(onlyModule(*merged))->placement->x, 132);
}

// Switch settings merge one switch at a time (trains design 2.2): flips of
// different switches both stay; the same switch flipped both ways waits
// for the review, the server's by default.
TEST(LayoutMerge, SwitchSettingsAreComparedAndMergedOneSwitchAtATime) {
    Case c;
    using States = QMap<QString, int>;
    c.base->sidecar.switchStates = States{ { QStringLiteral("A/0"), 1 }, { QStringLiteral("B/0"), 2 } };
    c.mine->sidecar.switchStates = States{ { QStringLiteral("A/0"), 2 }, { QStringLiteral("B/0"), 2 }, { QStringLiteral("C/0"), 1 } };
    c.server->sidecar.switchStates = States{ { QStringLiteral("B/0"), 2 }, { QStringLiteral("C/0"), 2 } };
    const Snapshot b = snapshotOf(*c.base), m = snapshotOf(*c.mine), s = snapshotOf(*c.server);
    const auto changes = compareLayouts(b, m, s);
    QStringList lines;
    for (const auto& ch : changes)
        if (ch.kind == QLatin1String("switch")) {
            lines << ch.key + QLatin1Char(' ') + describe(ch);
            EXPECT_FALSE(ch.allowsBoth());
        }
    EXPECT_EQ(lines.join(QLatin1Char('\n')).toStdString(),
              "switch:A/0 A switch's setting: you changed it, the server deleted it\n"
              "switch:C/0 A switch's setting: you added it, the server added it");
    auto merged = mergeLayouts(m, s, changes, {});
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->sidecar.switchStates, (States{ { QStringLiteral("B/0"), 2 }, { QStringLiteral("C/0"), 2 } }));
    merged = mergeLayouts(m, s, changes, { { QStringLiteral("switch:A/0"), Choice::Mine }, { QStringLiteral("switch:C/0"), Choice::Mine } });
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->sidecar.switchStates,
              (States{ { QStringLiteral("A/0"), 2 }, { QStringLiteral("B/0"), 2 }, { QStringLiteral("C/0"), 1 } }));
}

