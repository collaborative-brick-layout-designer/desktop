// Updates that keep the ids of unchanged parts, and finishing an undo for
// a shared module (docs/shared-modules.md, phase 3):
//  - the rules against the vectors both apps share
//    (fixtures/contracts/shared-module-keep.json and shared-module-settle.json,
//    written by the web's packages/modules keepIds.test.ts);
//  - the desktop's own update of a file (ReplaceModulePartsCommand keeping
//    parts): only what changed is touched, undo and redo, and a .bbm and a
//    .bld-layout written and read again keep the ids and the lock;
//  - finishing an undo on the maps (edit/SharedSettle.h finishShared).

#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/SharedModule.h"
#include "edit/ModuleCommands.h"
#include "edit/SharedModuleKeep.h"
#include "edit/SharedSettle.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUndoStack>

#include <cmath>

using namespace bld;

namespace {

QJsonObject fileOf(const char* name) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// The vectors' parts as a parts library (as SharedModulesTest's VectorParts).
class Parts {
public:
    explicit Parts(const QJsonObject& parts) {
        for (auto it = parts.begin(); it != parts.end(); ++it) {
            const QJsonObject p = it.value().toObject();
            const QJsonObject size = p.value("spriteSize").toObject();
            QImage img(size.value("w").toInt(), size.value("h").toInt(), QImage::Format_ARGB32);
            img.fill(Qt::red);
            img.save(dir_.filePath(it.key() + QStringLiteral(".png")));
            QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<part>\n  <Author>t</Author>\n"
                                         "  <Description><en>%1</en></Description>\n  <PixelsPerStud>%2</PixelsPerStud>\n")
                              .arg(it.key())
                              .arg(p.value("pxPerStud").toInt(8));
            if (const QJsonArray hull = p.value("hullPts").toArray(); !hull.isEmpty()) {
                xml += QStringLiteral("  <hull>\n");
                for (const auto& v : hull)
                    xml += QStringLiteral("    <point><x>%1</x><y>%2</y></point>\n")
                               .arg(v.toObject().value("x").toDouble())
                               .arg(v.toObject().value("y").toDouble());
                xml += QStringLiteral("  </hull>\n");
            }
            xml += QStringLiteral("</part>\n");
            QFile f(dir_.filePath(it.key() + QStringLiteral(".xml")));
            if (f.open(QIODevice::WriteOnly)) f.write(xml.toUtf8());
            f.close();
            lib.scanFile(f.fileName());
        }
    }
    parts::PartsLibrary lib;

private:
    QTemporaryDir dir_;
};

QRectF areaOf(const QJsonValue& v) {
    const QJsonObject a = v.toObject();
    return { a.value("x").toDouble(), a.value("y").toDouble(), a.value("width").toDouble(), a.value("height").toDouble() };
}

core::Brick brickOf(const QJsonObject& o) {
    core::Brick b;
    b.guid = o.value("id").toString();
    b.partNumber = o.value("partNumber").toString();
    b.displayArea = areaOf(o.value("displayArea"));
    b.orientation = static_cast<float>(o.value("orientation").toDouble());
    b.altitude = static_cast<float>(o.value("altitude").toDouble());
    b.myGroupId = o.value("myGroup").toString();
    return b;
}

std::vector<core::Group> groupsOf(const QJsonValue& v) {
    std::vector<core::Group> out;
    for (const auto& g : v.toArray()) {
        core::Group x;
        x.guid = g.toObject().value("id").toString();
        x.partNumber = g.toObject().value("partNumber").toString();
        x.myGroupId = g.toObject().value("myGroup").toString();
        out.push_back(x);
    }
    return out;
}

QJsonObject planJson(const edit::KeepPlan& p) {
    QJsonObject kept, sheets, groups;
    for (auto it = p.kept.begin(); it != p.kept.end(); ++it) kept.insert(it.key(), it.value());
    for (auto it = p.sheets.begin(); it != p.sheets.end(); ++it) sheets.insert(it.key(), QJsonArray::fromStringList(it.value()));
    for (auto it = p.newGroups.begin(); it != p.newGroups.end(); ++it) groups.insert(it.key(), QJsonArray::fromStringList(it.value()));
    return { { "kept", kept }, { "removed", QJsonArray::fromStringList(p.removed) }, { "added", QJsonArray::fromStringList(p.added) },
             { "sheets", sheets }, { "newGroups", groups } };
}

edit::SettleSide sideOf(const QJsonValue& v) {
    edit::SettleSide s;
    s.record = v.toObject().value("record").toBool();
    for (const auto& p : v.toObject().value("parts").toArray()) {
        const QJsonObject o = p.toObject();
        s.parts.push_back({ o.value("id").toString(), o.value("sheet").toString(), o.value("partNumber").toString(),
                            areaOf(o.value("displayArea")), o.value("orientation").toDouble() });
    }
    return s;
}

}  // namespace

TEST(SharedKeepVectors, TheKeepRuleAgreesWithTheWeb) {
    const QJsonObject f = fileOf("shared-module-keep.json");
    ASSERT_FALSE(f.value("keep").toArray().isEmpty());
    Parts parts(f.value("parts").toObject());
    for (const auto& v : f.value("keep").toArray()) {
        const QJsonObject c = v.toObject();
        std::vector<edit::KeepSheet> sheets;
        for (const auto& s : c.value("sheets").toArray()) {
            edit::KeepSheet k;
            k.id = s.toObject().value("id").toString();
            for (const auto& b : s.toObject().value("parts").toArray()) k.parts.push_back(brickOf(b.toObject()));
            k.groups = groupsOf(s.toObject().value("groups"));
            sheets.push_back(std::move(k));
        }
        QSet<QString> members;
        for (const auto& m : c.value("members").toArray()) members.insert(m.toString());
        edit::LayerBatches next;
        for (const auto& b : c.value("next").toArray()) {
            edit::ImportBbmAsModuleCommand::LayerBatch batch;
            batch.targetLayerGuid = b.toObject().value("sheetId").toString();
            for (const auto& x : b.toObject().value("bricks").toArray()) batch.bricks.push_back(brickOf(x.toObject()));
            batch.groups = groupsOf(b.toObject().value("groups"));
            next.push_back(std::move(batch));
        }
        const QJsonObject got = planJson(edit::planKeep(sheets, members, next, parts.lib));
        EXPECT_EQ(QJsonDocument(got).toJson(QJsonDocument::Compact).toStdString(),
                  QJsonDocument(c.value("expect").toObject()).toJson(QJsonDocument::Compact).toStdString())
            << c.value("name").toString().toStdString();
    }
}

TEST(SharedKeepVectors, TheSettleRuleAgreesWithTheWeb) {
    const QJsonObject f = fileOf("shared-module-settle.json");
    ASSERT_FALSE(f.value("settle").toArray().isEmpty());
    Parts parts(f.value("parts").toObject());
    for (const auto& v : f.value("settle").toArray()) {
        const QJsonObject c = v.toObject();
        const std::string name = c.value("name").toString().toStdString();
        const edit::Settled got = edit::settleShared(sideOf(c.value("before")), sideOf(c.value("after")), parts.lib);
        const QJsonObject want = c.value("expect").toObject();
        QStringList remove;
        for (const auto& r : want.value("remove").toArray()) remove << r.toString();
        EXPECT_EQ(got.remove, remove) << name;
        EXPECT_EQ(got.removeModule, want.value("removeModule").toBool()) << name;
        const QJsonArray move = want.value("move").toArray();
        ASSERT_EQ(got.move.size(), static_cast<std::size_t>(move.size())) << name;
        for (int i = 0; i < move.size(); ++i) {
            const QJsonObject m = move[i].toObject();
            const edit::SettlePart& g = got.move[static_cast<std::size_t>(i)];
            EXPECT_EQ(g.id, m.value("id").toString()) << name;
            EXPECT_EQ(g.sheet, m.value("sheet").toString()) << name;
            const double d = std::abs(core::norm360(g.orientation) - core::norm360(m.value("orientation").toDouble()));
            EXPECT_LE(std::min(d, 360.0 - d), 1e-3) << name;
            const QRectF a = areaOf(m.value("displayArea"));
            EXPECT_NEAR(g.displayArea.x(), a.x(), 1e-3) << name;
            EXPECT_NEAR(g.displayArea.y(), a.y(), 1e-3) << name;
            EXPECT_NEAR(g.displayArea.width(), a.width(), 1e-3) << name;
            EXPECT_NEAR(g.displayArea.height(), a.height(), 1e-3) << name;
        }
    }
}

TEST(SharedKeepVectors, TheRetiredRuleAgreesWithTheWeb) {
    const QJsonObject f = fileOf("shared-module-settle.json");
    const QJsonArray cases = f.value("retired").toArray();
    ASSERT_FALSE(cases.isEmpty());
    const auto list = [](const QJsonValue& v) {
        QStringList out;
        for (const auto& x : v.toArray()) out << x.toString();
        return out;
    };
    for (const auto& v : cases) {
        const QJsonObject c = v.toObject();
        const QStringList retired = list(c.value("retired")), before = list(c.value("before"));
        EXPECT_EQ(edit::settleRetired(QSet<QString>(retired.begin(), retired.end()), QSet<QString>(before.begin(), before.end()),
                                      list(c.value("after"))),
                  list(c.value("expect")))
            << c.value("name").toString().toStdString();
    }
}

namespace {

// A layout with a loose part under and over a shared module of `n` parts in a row.
struct Yard {
    core::Map map;
    QStringList members;
    QString sheet;
};

Yard yard(int n) {
    Yard y;
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = y.sheet = core::newBbmId();
    layer->name = QStringLiteral("Track");
    const auto add = [&](const QString& pn, double x) {
        core::Brick b;
        b.guid = core::newBbmId();
        b.partNumber = pn;
        b.displayArea = QRectF(x, 0, 2, 2);
        layer->bricks.push_back(b);
        return b.guid;
    };
    add(QStringLiteral("loose.1"), -50);
    for (int i = 0; i < n; ++i) y.members << add(QStringLiteral("p%1.1").arg(i % 7), i * 2);
    add(QStringLiteral("loose.2"), 50);
    core::Module m;
    m.id = QStringLiteral("crane");
    m.name = QStringLiteral("Harbor crane");
    m.memberIds = QSet<QString>(y.members.begin(), y.members.end());
    m.libraryModuleId = QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b");
    m.libraryVersion = 1;
    m.shared = QJsonObject{ { QStringLiteral("by"), QStringLiteral("Alice") } };
    y.map.sidecar.modules.push_back(m);
    y.map.layers().push_back(std::move(layer));
    return y;
}

edit::LayerBatches versionOf(int n, const QString& sheet, const std::function<void(std::vector<core::Brick>&)>& change = {}) {
    edit::ImportBbmAsModuleCommand::LayerBatch b;
    b.layerName = QStringLiteral("Track");
    b.targetLayerGuid = sheet;
    for (int i = 0; i < n; ++i) {
        core::Brick x;
        x.partNumber = QStringLiteral("p%1.1").arg(i % 7);
        x.displayArea = QRectF(i * 2, 0, 2, 2);
        b.bricks.push_back(x);
    }
    if (change) change(b.bricks);
    return { b };
}

// A copy of a yard (one parts sheet and the sidecar).
core::Map copyOf(const core::Map& m) {
    core::Map out;
    const auto& from = static_cast<const core::LayerBrick&>(*m.layers().front());
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = from.guid;
    layer->name = from.name;
    layer->bricks = from.bricks;
    layer->groups = from.groups;
    out.layers().push_back(std::move(layer));
    out.sidecar = m.sidecar;
    return out;
}

QStringList orderOf(const core::Map& m) {
    QStringList out;
    for (const auto& b : static_cast<const core::LayerBrick&>(*m.layers().front()).bricks) out << b.guid;
    return out;
}

}  // namespace

TEST(SharedKeepFile, AnUpdateTouchesOnlyWhatChangedAndUndoRedoAreExact) {
    Yard y = yard(5);
    parts::PartsLibrary none;
    const QStringList before = orderOf(y.map);
    const auto next = versionOf(5, y.sheet, [](std::vector<core::Brick>& b) {
        b[2].displayArea.translate(0, 6);
        b[0].partNumber = QStringLiteral("p0.4");
    });
    QUndoStack stack;
    auto* cmd = new edit::ReplaceModulePartsCommand(y.map, QStringLiteral("crane"), next, y.map.sidecar.modules.front().libraryModuleId, 2);
    cmd->keepUnchanged(&none);
    stack.push(cmd);
    EXPECT_EQ(cmd->keptCount(), 3);
    const core::Module& m = y.map.sidecar.modules.front();
    EXPECT_EQ(m.libraryVersion, 2);
    EXPECT_EQ(m.memberIds.size(), 5);
    for (const int i : { 1, 3, 4 }) EXPECT_TRUE(m.memberIds.contains(y.members[i])) << "part " << i << " kept its id";
    EXPECT_FALSE(m.memberIds.contains(y.members[0]));
    EXPECT_FALSE(m.memberIds.contains(y.members[2]));
    const QStringList after = orderOf(y.map);
    // Loose parts where they were; the module's parts in the version's order between them.
    EXPECT_EQ(after.front(), before.front());
    EXPECT_EQ(after.back(), before.back());
    EXPECT_EQ(after[2], y.members[1]);
    EXPECT_EQ(after[4], y.members[3]);
    const QStringList afterRedo = after;
    stack.undo();
    EXPECT_EQ(orderOf(y.map), before);
    EXPECT_EQ(y.map.sidecar.modules.front().libraryVersion, 1);
    stack.redo();
    EXPECT_EQ(orderOf(y.map), afterRedo) << "redo puts the same parts back, ids and all";
}

TEST(SharedKeepFile, AnUpdateRemembersThePartsItTookOut) {
    Yard y = yard(4);
    parts::PartsLibrary none;
    QUndoStack stack;
    auto* cmd = new edit::ReplaceModulePartsCommand(y.map, QStringLiteral("crane"),
                                                    versionOf(4, y.sheet, [](std::vector<core::Brick>& b) { b[1].displayArea.translate(3, 0); }),
                                                    y.map.sidecar.modules.front().libraryModuleId, 2);
    cmd->setSharedAfter(*y.map.sidecar.modules.front().shared);
    cmd->keepUnchanged(&none);
    stack.push(cmd);
    EXPECT_EQ(core::sharedPlaced(y.map.sidecar.modules.front())->retired, QStringList{ y.members[1] });
    EXPECT_EQ(core::moreRetired({ QStringLiteral("x"), QStringLiteral("y") }, { QStringLiteral("x"), QStringLiteral("z") }),
              (QStringList{ QStringLiteral("y"), QStringLiteral("x"), QStringLiteral("z") }));
}

TEST(SharedKeepFile, ABbmAndALayoutFileKeepTheKeptIdsAndTheLock) {
    Yard y = yard(4);
    parts::PartsLibrary none;
    QUndoStack stack;
    auto* cmd = new edit::ReplaceModulePartsCommand(y.map, QStringLiteral("crane"),
                                                    versionOf(4, y.sheet, [](std::vector<core::Brick>& b) { b[3].displayArea.translate(5, 0); }),
                                                    y.map.sidecar.modules.front().libraryModuleId, 2);
    cmd->setSharedAfter(*y.map.sidecar.modules.front().shared);
    cmd->keepUnchanged(&none);
    stack.push(cmd);
    const core::Module mod = y.map.sidecar.modules.front();
    QTemporaryDir dir;
    const QString bbm = dir.filePath(QStringLiteral("yard.bbm"));
    ASSERT_TRUE(saveload::writeBbm(y.map, bbm).ok);
    QFile f(bbm);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    ASSERT_TRUE(saveload::writeSidecar(saveload::sidecarPathFor(bbm), bytes, y.map.sidecar));
    auto read = saveload::readBbm(bbm);
    ASSERT_TRUE(read.ok());
    saveload::readSidecar(saveload::sidecarPathFor(bbm), bytes, read.map->sidecar);
    ASSERT_EQ(read.map->sidecar.modules.size(), 1u);
    EXPECT_EQ(read.map->sidecar.modules.front().memberIds, mod.memberIds);
    EXPECT_TRUE(core::isSharedModule(read.map->sidecar.modules.front()));
    EXPECT_EQ(orderOf(*read.map), orderOf(y.map));
    for (int i = 0; i < 3; ++i) EXPECT_TRUE(read.map->sidecar.modules.front().memberIds.contains(y.members[i]));

    const QString native = dir.filePath(QStringLiteral("yard.bld-layout"));
    QString error;
    ASSERT_TRUE(import::writeLayoutFile(y.map, native, &error)) << error.toStdString();
    const auto back = import::readLayoutFile(native, dir.path());
    ASSERT_TRUE(back.map) << back.error.toStdString();
    EXPECT_EQ(back.map->sidecar.modules.front().memberIds, mod.memberIds);
    EXPECT_EQ(orderOf(*back.map), orderOf(y.map));
}

TEST(SharedKeepFile, FinishingAnUndoMovesOrRemovesTheWholeModule) {
    parts::PartsLibrary none;
    // An undo that moved two of three parts back by (-16, -8).
    Yard y = yard(3);
    core::Map before = copyOf(y.map);
    core::Map after = copyOf(y.map);
    auto& bricks = static_cast<core::LayerBrick&>(*after.layers().front()).bricks;
    for (auto& b : bricks)
        if (b.guid == y.members[0] || b.guid == y.members[1]) b.displayArea.translate(-16, -8);
    core::SettledBack removed;
    EXPECT_TRUE(edit::finishShared(before, after, none, &removed));
    for (const auto& b : bricks) {
        if (b.guid == y.members[2]) {
            EXPECT_EQ(b.displayArea.topLeft(), QPointF(4 - 16, -8));
        }
    }
    EXPECT_TRUE(removed.empty());
    // An undo that took two of its three parts away: the rest goes, and the record.
    core::Map gone = copyOf(y.map);
    auto& left = static_cast<core::LayerBrick&>(*gone.layers().front()).bricks;
    std::erase_if(left, [&](const core::Brick& b) { return b.guid == y.members[0] || b.guid == y.members[1]; });
    EXPECT_TRUE(edit::finishShared(before, gone, none, &removed));
    EXPECT_TRUE(gone.sidecar.modules.empty());
    EXPECT_EQ(left.size(), 2u) << "only the loose parts";
    EXPECT_EQ(removed.bricks.size(), 1u);
    EXPECT_EQ(removed.modules.size(), 1u);
    // Put back (a redo): whole again.
    core::putBack(gone, removed);
    EXPECT_EQ(gone.sidecar.modules.size(), 1u);
    EXPECT_EQ(left.size(), 3u);
}
