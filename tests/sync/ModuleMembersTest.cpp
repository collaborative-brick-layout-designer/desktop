// A module's parts one key per part (sync/ModuleMembers.h, doc shape 3):
// the keys, values and lists match the web's (the shared vectors in
// fixtures/contracts/module-members.json); two people adding and removing
// parts of one module at the same instant both keep their change; a layout
// from before the keys reads the same; and an edit the web made at the same
// time as the desktop's merges with it.

#include "ModuleMembers.h"
#include "Compat.h"
#include "SidecarItems.h"
#include "SyncDoc.h"
#include "WebModel.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Module.h"
#include "core/ModuleEdit.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <functional>

using namespace bld;

namespace {

QJsonObject contract() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/module-members.json"));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
QJsonObject docs() { return contract().value(QLatin1String("docs")).toObject(); }
QByteArray bytes(const QJsonValue& base64) { return QByteArray::fromBase64(base64.toString().toLatin1()); }

sync::MemberKeys keysOf(const QJsonObject& o) {
    sync::MemberKeys out;
    for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), it.value());
    return out;
}
QStringList stringsOf(const QJsonValue& list) {
    QStringList out;
    for (const auto& v : list.toArray()) out << v.toString();
    return out;
}

std::unique_ptr<core::Map> mapOf(const sync::SyncDoc& doc) {
    QString err;
    auto map = sync::mapFromDocJson(doc.toJson(), &err);
    EXPECT_TRUE(map) << err.toStdString();
    return map;
}
int schemaOf(const sync::SyncDoc& d) {
    return d.toJson().value(QLatin1String("meta")).toObject().value(QLatin1String("schemaVersion")).toInt();
}
QJsonObject itemsOf(const sync::SyncDoc& d) { return d.toJson().value(QLatin1String("sidecarItems")).toObject(); }
// The doc's `p:` keys.
QJsonObject partKeys(const sync::SyncDoc& d) {
    QJsonObject out;
    const QJsonObject items = itemsOf(d);
    for (auto it = items.begin(); it != items.end(); ++it)
        if (it.key().startsWith(QLatin1String("p:"))) out.insert(it.key(), it.value());
    return out;
}
// meta.cache's list for the module: what apps that read only it, and the files, see.
QStringList cachedMembers(const sync::SyncDoc& d, const QString& id) {
    const QJsonArray mods = d.toJson()
                                .value(QLatin1String("meta"))
                                .toObject()
                                .value(QLatin1String("cache"))
                                .toObject()
                                .value(QLatin1String("modules"))
                                .toArray();
    for (const auto& m : mods)
        if (m.toObject().value(QLatin1String("id")).toString() == id) {
            QStringList out = stringsOf(m.toObject().value(QLatin1String("members")));
            out.sort();
            return out;
        }
    return {};
}

core::LayerBrick& bricksOf(core::Map& m) {
    for (auto& l : m.layers())
        if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l);
    throw std::runtime_error("no bricks");
}
core::Module* moduleOf(core::Map& m, const QString& id) {
    for (auto& mod : m.sidecar.modules)
        if (mod.id == id) return &mod;
    return nullptr;
}
QStringList sorted(QStringList l) {
    l.sort();
    return l;
}
QStringList membersOf(const sync::SyncDoc& d, const QString& id) {
    auto map = mapOf(d);
    const core::Module* m = map ? core::findModule(map->sidecar.modules, id) : nullptr;
    return m ? sorted(QStringList(m->memberIds.cbegin(), m->memberIds.cend())) : QStringList{ QStringLiteral("(no module)") };
}

// Every module's name and sorted parts, as the contract's `expect`.
QJsonObject shown(const sync::SyncDoc& d) {
    QJsonArray mods;
    auto map = mapOf(d);
    for (const auto& m : map->sidecar.modules)
        mods.append(QJsonObject{ { QStringLiteral("name"), m.name },
                                 { QStringLiteral("members"),
                                   QJsonArray::fromStringList(sorted(QStringList(m.memberIds.cbegin(), m.memberIds.cend()))) } });
    return { { QStringLiteral("modules"), mods } };
}

// One person's desktop on the contract's layout: the doc and edits made as
// the app makes them (read the map, change it, write it).
struct Person {
    sync::SyncDoc doc;
    QString module;
    QStringList p;

    explicit Person(quint64 clientId) : doc(clientId) {
        const QJsonObject c = docs();
        module = c.value(QLatin1String("moduleId")).toString();
        p = stringsOf(c.value(QLatin1String("parts")));
        EXPECT_TRUE(doc.applyUpdate(bytes(c.value(QLatin1String("base")))));
    }
    // Returns what the edit wrote, for the other person.
    QByteArray edit(const std::function<void(core::Map&)>& change) {
        auto map = mapOf(doc);
        change(*map);
        return doc.writeMap(*map);
    }
    QByteArray add(const QString& part) {
        return edit([&](core::Map& m) { moduleOf(m, module)->memberIds.insert(part); });
    }
    QByteArray takeOut(const QString& part) {
        return edit([&](core::Map& m) { moduleOf(m, module)->memberIds.remove(part); });
    }
    QByteArray rename(const QString& name) {
        return edit([&](core::Map& m) { moduleOf(m, module)->name = name; });
    }
    QByteArray deletePart(const QString& part) {
        return edit([&](core::Map& m) {
            auto& bricks = bricksOf(m).bricks;
            bricks.erase(std::remove_if(bricks.begin(), bricks.end(), [&](const core::Brick& b) { return b.guid == part; }),
                         bricks.end());
            // As DeleteBricksCommand does: it leaves its modules.
            for (auto& mod : m.sidecar.modules) mod.memberIds.remove(part);
        });
    }
    QByteArray ungroup() {
        return edit([&](core::Map& m) {
            auto& mods = m.sidecar.modules;
            mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const core::Module& x) { return x.id == module; }),
                       mods.end());
        });
    }
    QStringList members() const { return membersOf(doc, module); }
};

// Each gets what the other has.
void swap(Person& a, Person& b) {
    const QByteArray toB = a.doc.diffSince(b.doc.stateVector());
    const QByteArray toA = b.doc.diffSince(a.doc.stateVector());
    ASSERT_TRUE(b.doc.applyUpdate(toB));
    ASSERT_TRUE(a.doc.applyUpdate(toA));
}

// The desktop's half of each contract case: what `desktop` says.
QByteArray desktopEdit(Person& me, const QString& what) {
    if (what == QLatin1String("add part 3 to the module")) return me.add(me.p[3]);
    if (what == QLatin1String("take part 0 out of the module")) return me.takeOut(me.p[0]);
    if (what == QLatin1String("rename the module Harbour")) return me.rename(QStringLiteral("Harbour"));
    if (what == QLatin1String("delete part 1 from the map")) return me.deletePart(me.p[1]);
    ADD_FAILURE() << "unknown desktop edit: " << what.toStdString();
    return {};
}

}  // namespace

// ── The shared vectors ──

TEST(ModuleMembers, TheShapeIsTheContracts) {
    EXPECT_EQ(sync::kDocShapeMemberKeys, contract().value(QLatin1String("shape")).toInt());
    EXPECT_EQ(sync::kDocSchemaVersion, sync::kDocShapeMemberKeys);
}

TEST(ModuleMembers, KeysMatchTheWebs) {
    const QJsonArray keys = contract().value(QLatin1String("keys")).toArray();
    ASSERT_GE(keys.size(), 5);
    for (const auto& v : keys) {
        const QJsonObject k = v.toObject();
        const QString moduleId = k.value(QLatin1String("moduleId")).toString(), partId = k.value(QLatin1String("partId")).toString(),
                      key = k.value(QLatin1String("key")).toString();
        EXPECT_EQ(sync::memberKey(moduleId, partId), key);
        const auto at = sync::parseMemberKey(key);
        ASSERT_TRUE(at) << key.toStdString();
        EXPECT_EQ(at->moduleId, moduleId);
        EXPECT_EQ(at->partId, partId);
    }
    const QJsonArray bad = contract().value(QLatin1String("badKeys")).toArray();
    ASSERT_GE(bad.size(), 5);
    for (const auto& v : bad) EXPECT_FALSE(sync::parseMemberKey(v.toString())) << v.toString().toStdString();
    EXPECT_FALSE(sync::parseMemberKey(QStringLiteral("m:m1")));
}

TEST(ModuleMembers, ValuesMatchTheWebs) {
    const QJsonArray values = contract().value(QLatin1String("values")).toArray();
    ASSERT_GE(values.size(), 8);
    for (const auto& v : values) {
        const QJsonObject o = v.toObject();
        EXPECT_EQ(sync::validMemberValue(o.value(QLatin1String("value"))), o.value(QLatin1String("valid")).toBool())
            << QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
    }
    EXPECT_FALSE(sync::validMemberValue(QJsonValue(QJsonValue::Undefined)));
}

TEST(ModuleMembers, ReadingMatchesTheWebs) {
    const QJsonArray cases = contract().value(QLatin1String("read")).toArray();
    ASSERT_GE(cases.size(), 8);
    for (const auto& v : cases) {
        const QJsonObject c = v.toObject();
        EXPECT_EQ(sync::moduleMembers(c.value(QLatin1String("members")), keysOf(c.value(QLatin1String("keys")).toObject())),
                  stringsOf(c.value(QLatin1String("expect"))))
            << c.value(QLatin1String("what")).toString().toStdString();
    }
}

TEST(ModuleMembers, WritingMatchesTheWebs) {
    const QJsonArray cases = contract().value(QLatin1String("write")).toArray();
    ASSERT_GE(cases.size(), 8);
    for (const auto& v : cases) {
        const QJsonObject c = v.toObject();
        SCOPED_TRACE(c.value(QLatin1String("what")).toString().toStdString());
        sync::MemberKeys keys = keysOf(c.value(QLatin1String("keys")).toObject());
        const QStringList want = stringsOf(c.value(QLatin1String("want")));
        const sync::MemberWrites w = sync::memberWrites(c.value(QLatin1String("members")), keys, want);
        QJsonArray set;
        for (const auto& [id, value] : w.set) set.append(QJsonArray{ id, value });
        EXPECT_EQ(set, c.value(QLatin1String("set")).toArray());
        EXPECT_EQ(w.remove, stringsOf(c.value(QLatin1String("delete"))));
        // And after those writes the module reads as wanted.
        for (const QString& id : w.remove) keys.remove(id);
        for (const auto& [id, value] : w.set) keys.insert(id, value);
        EXPECT_EQ(sorted(sync::moduleMembers(c.value(QLatin1String("members")), keys)), sorted(want));
    }
}

// ── A layout from before the keys ──

TEST(ModuleMembers, ALayoutFromBeforeTheKeysReadsAsItAlwaysDidAndOpeningItWritesNothing) {
    Person me(7);
    EXPECT_EQ(me.members(), sorted({ me.p[0], me.p[1] }));
    EXPECT_EQ(schemaOf(me.doc), 1);
    EXPECT_TRUE(partKeys(me.doc).isEmpty());
    // Opened and written back unchanged: nothing to send, nothing converted.
    auto map = mapOf(me.doc);
    EXPECT_TRUE(me.doc.writeMap(*map).isEmpty());
    EXPECT_TRUE(partKeys(me.doc).isEmpty());
    EXPECT_EQ(schemaOf(me.doc), 1);
    EXPECT_TRUE(sync::canReadDoc(schemaOf(me.doc)));
}

TEST(ModuleMembers, ARenameOrAnotherEditLeavesTheShapeAlone) {
    Person me(7);
    EXPECT_FALSE(me.rename(QStringLiteral("Harbour")).isEmpty());
    EXPECT_TRUE(partKeys(me.doc).isEmpty());
    EXPECT_EQ(schemaOf(me.doc), 1);
    // A build from before the keys still reads it.
    EXPECT_TRUE(sync::canReadDoc(schemaOf(me.doc), 2, 1));
}

TEST(ModuleMembers, APartChangeWritesKeysLeavesTheStartingListAndStampsShape3) {
    Person me(7);
    me.add(me.p[2]);
    me.takeOut(me.p[0]);
    const QJsonObject keys = partKeys(me.doc);
    EXPECT_EQ(keys.size(), 2);
    EXPECT_EQ(keys.value(sync::memberKey(me.module, me.p[2])), QJsonValue(0.0));
    EXPECT_EQ(keys.value(sync::memberKey(me.module, me.p[0])), QJsonValue(false));
    EXPECT_EQ(sorted(stringsOf(itemsOf(me.doc).value(QStringLiteral("m:") + me.module).toObject().value(QLatin1String("members")))),
              sorted({ me.p[0], me.p[1] }));
    EXPECT_EQ(me.members(), sorted({ me.p[1], me.p[2] }));
    EXPECT_EQ(cachedMembers(me.doc, me.module), sorted({ me.p[1], me.p[2] }));
    EXPECT_EQ(schemaOf(me.doc), 3);
    // A build from before the keys refuses it; this one reads it.
    EXPECT_FALSE(sync::canReadDoc(schemaOf(me.doc), 2, 1));
    EXPECT_TRUE(sync::canReadDoc(schemaOf(me.doc)));
    // The hash goes with meta.cache, so the items are read.
    const QJsonObject json = me.doc.toJson();
    EXPECT_EQ(itemsOf(me.doc).value(QLatin1String("__blob")).toString(),
              sync::sidecarBlobHash(json.value(QLatin1String("meta")).toObject().value(QLatin1String("cache")).toObject()));
    // Put back as they were: no keys left.
    me.takeOut(me.p[2]);
    me.add(me.p[0]);
    EXPECT_TRUE(partKeys(me.doc).isEmpty());
    EXPECT_EQ(me.members(), sorted({ me.p[0], me.p[1] }));
}

TEST(ModuleMembers, AWriteThatChangesNothingSendsNothing) {
    Person me(7);
    me.add(me.p[2]);
    auto map = mapOf(me.doc);
    EXPECT_TRUE(me.doc.writeMap(*map).isEmpty());
    EXPECT_TRUE(me.rename(QStringLiteral("Yard")).isEmpty());
}

TEST(ModuleMembers, DeletingAModuleTakesItsKeys) {
    Person me(7);
    me.add(me.p[2]);
    me.takeOut(me.p[0]);
    me.ungroup();
    EXPECT_TRUE(partKeys(me.doc).isEmpty());
    EXPECT_FALSE(itemsOf(me.doc).contains(QStringLiteral("m:") + me.module));
}

TEST(ModuleMembers, TheSidecarAFileSavesListsEveryPartAsBefore) {
    Person me(7);
    const QJsonObject before = saveload::sidecarToJson(mapOf(me.doc)->sidecar);
    me.add(me.p[2]);
    me.takeOut(me.p[0]);
    const QJsonObject after = saveload::sidecarToJson(mapOf(me.doc)->sidecar);
    const QJsonObject was = before.value(QLatin1String("modules")).toArray()[0].toObject();
    const QJsonObject is = after.value(QLatin1String("modules")).toArray()[0].toObject();
    EXPECT_EQ(sorted(stringsOf(is.value(QLatin1String("members")))), sorted({ me.p[1], me.p[2] }));
    // The same fields, and nothing of the shared layout's bookkeeping.
    EXPECT_EQ(is.keys(), was.keys());
    const QByteArray text = QJsonDocument(after).toJson(QJsonDocument::Compact);
    EXPECT_FALSE(text.contains("_order"));
    EXPECT_FALSE(text.contains("__blob"));
    EXPECT_FALSE(text.contains("\"p:"));
    // Everything else in the file is as it was.
    QJsonObject rest = after, restBefore = before;
    rest.remove(QStringLiteral("modules"));
    restBefore.remove(QStringLiteral("modules"));
    EXPECT_EQ(rest, restBefore);
}

// ── A server whose web app doesn't read the keys ──

TEST(ModuleMembers, TheFeatureIsOneTheContractListsForServers) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/compat/compat.json"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QJsonObject c = QJsonDocument::fromJson(f.readAll()).object();
    EXPECT_TRUE(stringsOf(c.value(QLatin1String("serverFeatures"))).contains(QLatin1String(sync::kMemberKeysFeature)));
    // Not one the desktop tells people is missing: it simply writes the list whole.
    EXPECT_FALSE(stringsOf(c.value(QLatin1String("desktopUses"))).contains(QLatin1String(sync::kMemberKeysFeature)));
}

// There the desktop writes a module's list whole, as before: the layout
// stays in the shape that web app reads.
TEST(ModuleMembers, OnAServerWithoutTheFeatureTheListIsWrittenWholeAndTheShapeStays) {
    Person me(7);
    me.doc.setMemberKeys(false);
    me.add(me.p[2]);
    me.takeOut(me.p[0]);
    EXPECT_TRUE(partKeys(me.doc).isEmpty());
    EXPECT_EQ(schemaOf(me.doc), 1);
    EXPECT_TRUE(sync::canReadDoc(schemaOf(me.doc), 2, 1));
    EXPECT_EQ(sorted(stringsOf(itemsOf(me.doc).value(QStringLiteral("m:") + me.module).toObject().value(QLatin1String("members")))),
              sorted({ me.p[1], me.p[2] }));
    EXPECT_EQ(me.members(), sorted({ me.p[1], me.p[2] }));
    EXPECT_EQ(cachedMembers(me.doc, me.module), sorted({ me.p[1], me.p[2] }));
    // Undo and redo as ever.
    EXPECT_FALSE(me.doc.undo().isEmpty());
    EXPECT_EQ(me.members(), sorted({ me.p[0], me.p[1], me.p[2] }));
    EXPECT_FALSE(me.doc.redo().isEmpty());
    EXPECT_EQ(me.members(), sorted({ me.p[1], me.p[2] }));
    // A build that knows the keys reads it, and its first part change
    // starts from that list.
    Person other(8);
    ASSERT_TRUE(other.doc.applyUpdate(me.doc.diffSince(other.doc.stateVector())));
    other.add(other.p[4]);
    EXPECT_EQ(other.members(), sorted({ me.p[1], me.p[2], me.p[4] }));
    EXPECT_EQ(schemaOf(other.doc), 3);
}

// A layout that already has part keys (someone who reads them wrote them):
// the keys are used, whatever the server says.
TEST(ModuleMembers, ALayoutThatAlreadyHasKeysKeepsUsingThem) {
    Person me(7);
    me.add(me.p[2]);
    ASSERT_EQ(partKeys(me.doc).size(), 1);
    me.doc.setMemberKeys(false);
    me.add(me.p[3]);
    me.takeOut(me.p[0]);
    EXPECT_EQ(partKeys(me.doc).size(), 3);
    EXPECT_EQ(me.members(), sorted({ me.p[1], me.p[2], me.p[3] }));
    EXPECT_EQ(schemaOf(me.doc), 3);
}

// ── Two desktops, the same instant ──

class TwoPeople : public ::testing::TestWithParam<bool> {
protected:
    // (Which of two values written at the same instant stays goes by the
    // writers' client ids: every case runs with them both ways round.)
    Person a{ GetParam() ? 1001U : 2002U };
    Person b{ GetParam() ? 2002U : 1001U };
    void expectSame(const QStringList& members) {
        for (Person* x : { &a, &b }) {
            EXPECT_EQ(x->members(), members);
            EXPECT_EQ(cachedMembers(x->doc, x->module), members) << "meta.cache (what the files and older readers see)";
        }
        EXPECT_EQ(a.doc.toJson(), b.doc.toJson());
    }
};
INSTANTIATE_TEST_SUITE_P(BothWaysRound, TwoPeople, ::testing::Bool());

TEST_P(TwoPeople, BothAddAPart) {
    a.add(a.p[2]);
    b.add(b.p[3]);
    swap(a, b);
    // meta.cache follows with the next write from either.
    a.rename(QStringLiteral("Yard east"));
    swap(a, b);
    expectSame(sorted({ a.p[0], a.p[1], a.p[2], a.p[3] }));
}

TEST_P(TwoPeople, OneAddsOneTakesOut) {
    a.add(a.p[2]);
    b.takeOut(b.p[0]);
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[1], a.p[2] }));
    EXPECT_EQ(b.members(), sorted({ a.p[1], a.p[2] }));
}

TEST_P(TwoPeople, BothTakeOutDifferentParts) {
    a.add(a.p[2]);
    swap(a, b);
    a.takeOut(a.p[0]);
    b.takeOut(b.p[2]);
    swap(a, b);
    EXPECT_EQ(a.members(), QStringList{ a.p[1] });
    EXPECT_EQ(b.members(), QStringList{ a.p[1] });
}

TEST_P(TwoPeople, OneRenamesOneAddsAPart) {
    a.rename(QStringLiteral("Harbour"));
    b.add(b.p[3]);
    swap(a, b);
    for (Person* x : { &a, &b }) {
        EXPECT_EQ(x->members(), sorted({ a.p[0], a.p[1], a.p[3] }));
        EXPECT_EQ(core::findModule(mapOf(x->doc)->sidecar.modules, x->module)->name, QStringLiteral("Harbour"));
    }
}

TEST_P(TwoPeople, SeveralRoundsEachBeforeHearingTheOther) {
    a.add(a.p[2]);
    b.add(b.p[3]);
    swap(a, b);
    a.takeOut(a.p[3]);
    b.add(b.p[4]);
    swap(a, b);
    a.takeOut(a.p[0]);
    b.takeOut(b.p[2]);
    b.add(b.p[3]);
    swap(a, b);
    a.rename(QStringLiteral("Yard east"));
    swap(a, b);
    expectSame(sorted({ a.p[1], a.p[3], a.p[4] }));
}

TEST_P(TwoPeople, OneDeletesAPartFromTheMapOneAddsAnother) {
    a.deletePart(a.p[1]);
    b.add(b.p[4]);
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[4] }));
    EXPECT_EQ(b.members(), sorted({ a.p[0], a.p[4] }));
}

TEST_P(TwoPeople, OneUngroupsWhileTheOtherAddsAPartItStaysUngrouped) {
    a.ungroup();
    b.add(b.p[3]);
    swap(a, b);
    EXPECT_TRUE(mapOf(a.doc)->sidecar.modules.empty());
    EXPECT_TRUE(mapOf(b.doc)->sidecar.modules.empty());
}

TEST_P(TwoPeople, UndoTakesBackMyPartAndKeepsTheOthersRedoPutsItBack) {
    a.add(a.p[2]);
    b.add(b.p[3]);
    swap(a, b);
    ASSERT_TRUE(a.doc.canUndo());
    EXPECT_FALSE(a.doc.undo().isEmpty());
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[1], a.p[3] }));
    EXPECT_EQ(b.members(), sorted({ a.p[0], a.p[1], a.p[3] }));
    // The stamp is no undo step: older builds still refuse the layout.
    EXPECT_EQ(schemaOf(a.doc), 3);
    ASSERT_TRUE(a.doc.canRedo());
    EXPECT_FALSE(a.doc.redo().isEmpty());
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[1], a.p[2], a.p[3] }));
    EXPECT_EQ(b.members(), sorted({ a.p[0], a.p[1], a.p[2], a.p[3] }));
}

TEST_P(TwoPeople, UndoOfAPartTakenOutAsTheOtherTakesAnotherOut) {
    a.takeOut(a.p[0]);
    b.add(b.p[4]);
    b.takeOut(b.p[1]);
    swap(a, b);
    EXPECT_EQ(a.members(), QStringList{ a.p[4] });
    EXPECT_FALSE(a.doc.undo().isEmpty());
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[4] }));
    EXPECT_EQ(b.members(), sorted({ a.p[0], a.p[4] }));
    EXPECT_FALSE(a.doc.redo().isEmpty());
    swap(a, b);
    EXPECT_EQ(b.members(), QStringList{ a.p[4] });
}

TEST_P(TwoPeople, UndoOfAnUngroupBringsBackThePartsBothAdded) {
    a.add(a.p[2]);
    b.add(b.p[3]);
    swap(a, b);
    a.ungroup();
    swap(a, b);
    EXPECT_TRUE(mapOf(b.doc)->sidecar.modules.empty());
    EXPECT_FALSE(a.doc.undo().isEmpty());
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[1], a.p[2], a.p[3] }));
    EXPECT_EQ(b.members(), sorted({ a.p[0], a.p[1], a.p[2], a.p[3] }));
}

// One was away (its edits kept in its own doc, as the cache file keeps
// them) while the other went on: back together, everything is there.
TEST_P(TwoPeople, WhatOneDidWhileAwayMergesWithWhatTheOtherDidMeanwhile) {
    b.add(b.p[2]);
    b.takeOut(b.p[0]);
    b.rename(QStringLiteral("Away name"));
    a.add(a.p[3]);
    a.takeOut(a.p[1]);
    a.add(a.p[4]);
    // b's app was closed and opened again while away.
    sync::SyncDoc back(GetParam() ? 3003U : 903U);
    ASSERT_TRUE(back.applyUpdate(b.doc.encodeState()));
    ASSERT_TRUE(back.applyUpdate(a.doc.diffSince(back.stateVector())));
    ASSERT_TRUE(a.doc.applyUpdate(back.diffSince(a.doc.stateVector())));
    EXPECT_EQ(membersOf(back, a.module), sorted({ a.p[2], a.p[3], a.p[4] }));
    EXPECT_EQ(a.members(), sorted({ a.p[2], a.p[3], a.p[4] }));
    EXPECT_EQ(core::findModule(mapOf(a.doc)->sidecar.modules, a.module)->name, QStringLiteral("Away name"));
}

// Undo straight after someone else wrote the same sidecar value at the same
// instant: yrs takes ours back and, with theirs in between, puts nothing in
// its place. SyncDoc writes it again (restoreSidecar), so the sidecar and
// the module are never left missing.
TEST_P(TwoPeople, UndoStraightAfterBothChangedTheModuleLeavesTheSidecarWhole) {
    a.add(a.p[2]);
    b.add(b.p[3]);
    swap(a, b);
    EXPECT_FALSE(a.doc.undo().isEmpty());
    const QJsonObject meta = a.doc.toJson().value(QLatin1String("meta")).toObject();
    ASSERT_TRUE(meta.value(QLatin1String("cache")).isObject()) << "meta.cache is gone";
    EXPECT_EQ(itemsOf(a.doc).value(QLatin1String("__blob")).toString(),
              sync::sidecarBlobHash(meta.value(QLatin1String("cache")).toObject()));
    swap(a, b);
    expectSame(sorted({ a.p[0], a.p[1], a.p[3] }));
    // The restore is no undo step of its own: redo is still there.
    ASSERT_TRUE(a.doc.canRedo());
    EXPECT_FALSE(a.doc.redo().isEmpty());
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[1], a.p[2], a.p[3] }));
    EXPECT_EQ(b.members(), sorted({ a.p[0], a.p[1], a.p[2], a.p[3] }));
}

TEST_P(TwoPeople, UndoStraightAfterBothRenamedTheModuleLeavesTheModule) {
    a.rename(QStringLiteral("North"));
    b.rename(QStringLiteral("South"));
    swap(a, b);
    // (Nothing to send when the other person's value was the one kept.)
    a.doc.undo();
    swap(a, b);
    for (Person* x : { &a, &b }) {
        auto map = mapOf(x->doc);
        ASSERT_EQ(map->sidecar.modules.size(), 1U);
        EXPECT_EQ(x->members(), sorted({ a.p[0], a.p[1] }));
    }
    EXPECT_EQ(mapOf(a.doc)->sidecar.modules.front().name, mapOf(b.doc)->sidecar.modules.front().name);
}

// Undo still takes back what the step made: a new module goes, with its keys.
TEST_P(TwoPeople, UndoOfANewModuleAndOfItsPartsTakesThemBack) {
    const QString made = QStringLiteral("made-here");
    a.edit([&](core::Map& m) {
        core::Module mod;
        mod.id = made;
        mod.name = QStringLiteral("Town");
        mod.memberIds = { a.p[4] };
        m.sidecar.modules.push_back(mod);
    });
    a.add(a.p[2]);
    swap(a, b);
    EXPECT_EQ(membersOf(b.doc, made), QStringList{ a.p[4] });
    EXPECT_FALSE(a.doc.undo().isEmpty());
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[1] }));
    EXPECT_TRUE(partKeys(a.doc).isEmpty());
    EXPECT_FALSE(a.doc.undo().isEmpty());
    EXPECT_FALSE(itemsOf(a.doc).contains(QStringLiteral("m:") + made));
    swap(a, b);
    EXPECT_EQ(mapOf(b.doc)->sidecar.modules.size(), 1U);
    EXPECT_FALSE(a.doc.redo().isEmpty());
    EXPECT_FALSE(a.doc.redo().isEmpty());
    swap(a, b);
    EXPECT_EQ(membersOf(b.doc, made), QStringList{ a.p[4] });
    EXPECT_EQ(b.members(), sorted({ a.p[0], a.p[1], a.p[2] }));
}

// KNOWN LIMIT (docs/module-members.md), written down so a fix has a test to
// tighten: both make the very same change to the very same part at the same
// instant, and one undoes. The part follows the undo when the undoing
// desktop's key was the one kept, though the other person made the same
// change; yrs doesn't tell the desktop that someone else's key sat under
// its own. Everyone still ends up showing the same module.
TEST_P(TwoPeople, BothAddTheSamePartAndOneUndoesEndsTheSameEverywhere) {
    a.add(a.p[2]);
    b.add(b.p[2]);
    swap(a, b);
    EXPECT_EQ(a.members(), sorted({ a.p[0], a.p[1], a.p[2] }));
    // (Nothing to send when the other person's value was the one kept.)
    a.doc.undo();
    swap(a, b);
    EXPECT_EQ(a.members(), b.members());
    // The other person's part (today only when their key was the one kept), or not.
    EXPECT_TRUE(a.members() == sorted({ a.p[0], a.p[1], a.p[2] }) || a.members() == sorted({ a.p[0], a.p[1] }));
    ASSERT_EQ(mapOf(a.doc)->sidecar.modules.size(), 1U);
}

// ── The web and the desktop (contract) ──

TEST(ModuleMembers, AnEditTheWebMadeAtTheSameTimeAsTheDesktopsMerges) {
    const QJsonArray cases = docs().value(QLatin1String("concurrent")).toArray();
    ASSERT_EQ(cases.size(), 8);
    const QByteArray base = bytes(docs().value(QLatin1String("base")));
    for (const auto& v : cases) {
        const QJsonObject c = v.toObject();
        const QString name = c.value(QLatin1String("name")).toString();
        SCOPED_TRACE(name.toStdString());
        const QByteArray web = bytes(c.value(QLatin1String("web")));
        Person me(3);
        const QByteArray ours = desktopEdit(me, c.value(QLatin1String("desktop")).toString());
        ASSERT_FALSE(ours.isEmpty());
        // Arriving in either order.
        for (const bool webFirst : { true, false }) {
            sync::SyncDoc d;
            ASSERT_TRUE(d.applyUpdate(base));
            ASSERT_TRUE(d.applyUpdate(webFirst ? web : ours));
            ASSERT_TRUE(d.applyUpdate(webFirst ? ours : web));
            EXPECT_EQ(shown(d), c.value(QLatin1String("expect")).toObject()) << (webFirst ? "web first" : "desktop first");
        }
        // And on the desktop that made the edit.
        ASSERT_TRUE(me.doc.applyUpdate(web));
        EXPECT_EQ(shown(me.doc), c.value(QLatin1String("expect")).toObject());
    }
}

TEST(ModuleMembers, ALayoutWithKeysTheWebWroteReadsTheSameAndTheNextEditWritesTheKeysTheWebWould) {
    const QJsonObject c = docs();
    sync::SyncDoc d(3);
    ASSERT_TRUE(d.applyUpdate(bytes(c.value(QLatin1String("keyed")))));
    EXPECT_EQ(shown(d), c.value(QLatin1String("keyedExpect")).toObject());
    EXPECT_EQ(schemaOf(d), 3);
    EXPECT_EQ(partKeys(d), c.value(QLatin1String("keyedKeys")).toObject());
    // Reading it and writing it back sends nothing (the web's order of the
    // parts stands; the desktop doesn't write its own).
    auto map = mapOf(d);
    EXPECT_TRUE(d.writeMap(*map).isEmpty());
    const QString id = c.value(QLatin1String("moduleId")).toString();
    const QStringList p = stringsOf(c.value(QLatin1String("parts")));
    moduleOf(*map, id)->memberIds.insert(p[3]);
    moduleOf(*map, id)->memberIds.remove(p[1]);
    EXPECT_FALSE(d.writeMap(*map).isEmpty());
    EXPECT_EQ(partKeys(d), c.value(QLatin1String("keyedThenKeys")).toObject());
    EXPECT_EQ(membersOf(d, id), sorted({ p[2], p[3] }));
}

// A writer that only knows meta.cache replaced it: its lists stand, and the
// keys from before go with the next write.
TEST(ModuleMembers, AListReplacedWholeInMetaCacheStandsAndTheOldKeysGo) {
    Person me(7);
    me.add(me.p[2]);
    me.takeOut(me.p[0]);
    QJsonObject cache = me.doc.toJson().value(QLatin1String("meta")).toObject().value(QLatin1String("cache")).toObject();
    QJsonArray mods = cache.value(QLatin1String("modules")).toArray();
    QJsonObject mod = mods[0].toObject();
    mod.insert(QStringLiteral("members"), QJsonArray{ me.p[0], me.p[4] });
    mods[0] = mod;
    cache.insert(QStringLiteral("modules"), mods);
    me.doc.setMeta(QStringLiteral("cache"), cache);
    EXPECT_EQ(me.members(), sorted({ me.p[0], me.p[4] }));
    me.add(me.p[3]);
    EXPECT_EQ(me.members(), sorted({ me.p[0], me.p[3], me.p[4] }));
    EXPECT_TRUE(partKeys(me.doc).isEmpty());
    me.add(me.p[1]);
    EXPECT_EQ(partKeys(me.doc).size(), 1);
    EXPECT_EQ(me.members(), sorted({ me.p[0], me.p[1], me.p[3], me.p[4] }));
}
