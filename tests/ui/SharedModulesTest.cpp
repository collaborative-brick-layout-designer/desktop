// Shared (read-only) modules on the map (docs/shared-modules.md):
//  - the placement math against the vectors both apps share
//    (fixtures/contracts/shared-module-placement.json, written by the
//    web's packages/modules placement.test.ts);
//  - the lock under every edit in the map view: a change to a shared
//    module's parts, name or members is taken back with a plain reason;
//    Edit module and bending its track say why instead; moving and
//    turning it keep its placement up;
//  - the badge and the dashed frame, the notices over the map, the words,
//    and what an update now overlaps.

#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/ModuleEdit.h"
#include "core/SharedModule.h"
#include "edit/EditCommands.h"
#include "edit/ModuleCommands.h"
#include "edit/SharedMarks.h"
#include "edit/SharedModulePlacement.h"
#include "parts/BrickPlacement.h"
#include "parts/PartsLibrary.h"
#include "rendering/ModuleLabels.h"
#include "import/LayoutFile.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"
#include "ui/FindDialog.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/SharedModuleWords.h"
#include "ui/SharedModulesBar.h"

#include "ui/SharedUpdatesDialog.h"
#include "ui/help/HelpButton.h"
#include <QRadioButton>
#include <gtest/gtest.h>

#include <QApplication>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>

#include <cmath>

using namespace bld;

namespace {

const QString kLib = QStringLiteral("5f0c1a2b-3c4d-4e5f-8a9b-0c1d2e3f4a5b");

QJsonObject vectors() {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/shared-module-placement.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// The vectors' parts as a parts library: each one's picture at its size,
// its pixels per stud, and its <hull> when it has one.
class VectorParts {
public:
    VectorParts() {
        const QJsonObject parts = vectors().value("parts").toObject();
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

core::Brick brickOf(const QJsonObject& o) {
    core::Brick b;
    b.partNumber = o.value("partNumber").toString();
    const QJsonObject a = o.value("displayArea").toObject();
    b.displayArea = QRectF(a.value("x").toDouble(), a.value("y").toDouble(), a.value("width").toDouble(), a.value("height").toDouble());
    b.orientation = static_cast<float>(o.value("orientation").toDouble());
    return b;
}

std::vector<core::Brick> bricksOf(const QJsonValue& v) {
    std::vector<core::Brick> out;
    for (const auto& b : v.toArray()) out.push_back(brickOf(b.toObject()));
    return out;
}

edit::LayerBatches batch(const QJsonValue& v) {
    edit::ImportBbmAsModuleCommand::LayerBatch b;
    b.layerName = QStringLiteral("Track");
    b.bricks = bricksOf(v);
    return { b };
}

core::SharedPlacement placementOf(const QJsonValue& v) { return core::readPlacement(v).value_or(core::SharedPlacement{}); }

void expectPlacement(const core::SharedPlacement& got, const QJsonValue& want, double tol, const std::string& what) {
    const core::SharedPlacement w = placementOf(want);
    const double d = std::abs(got.degrees - w.degrees);
    EXPECT_LE(std::min(d, 360.0 - d), tol) << what << " degrees " << got.degrees << " vs " << w.degrees;
    EXPECT_NEAR(got.x, w.x, tol) << what;
    EXPECT_NEAR(got.y, w.y, tol) << what;
}

}  // namespace

// ---- The vectors both apps share ------------------------------------------

TEST(SharedModulePlacementContract, PlacesAVersionAsTheWebDoes) {
    const QJsonObject v = vectors();
    ASSERT_FALSE(v.isEmpty());
    VectorParts parts;
    const double tol = v.value("tolerance").toDouble();
    ASSERT_GT(v.value("place").toArray().size(), 3);
    for (const auto& c : v.value("place").toArray()) {
        const QJsonObject o = c.toObject();
        const std::string name = o.value("name").toString().toStdString();
        const auto placed = edit::placeVersion(batch(o.value("bricks")), placementOf(o.value("placement")), parts.lib);
        const auto want = bricksOf(o.value("expect"));
        ASSERT_EQ(placed[0].bricks.size(), want.size()) << name;
        for (std::size_t i = 0; i < want.size(); ++i) {
            const core::Brick& g = placed[0].bricks[i];
            const std::string at = name + " part " + std::to_string(i) + " " + want[i].partNumber.toStdString();
            EXPECT_NEAR(g.displayArea.x(), want[i].displayArea.x(), tol) << at;
            EXPECT_NEAR(g.displayArea.y(), want[i].displayArea.y(), tol) << at;
            EXPECT_NEAR(g.displayArea.width(), want[i].displayArea.width(), tol) << at;
            EXPECT_NEAR(g.displayArea.height(), want[i].displayArea.height(), tol) << at;
            const double d = std::abs(core::norm360(g.orientation) - core::norm360(want[i].orientation));
            EXPECT_LE(std::min(d, 360.0 - d), 1e-3) << at;
        }
    }
}

TEST(SharedModulePlacementContract, KeepsThePlacementThroughAMoveATurnAndADrop) {
    const QJsonObject v = vectors();
    const double tol = v.value("tolerance").toDouble();
    int n = 0;
    for (const auto& c : v.value("move").toArray()) {
        const QJsonObject o = c.toObject();
        expectPlacement(core::movedPlacement(placementOf(o.value("placement")), QPointF(o.value("dx").toDouble(), o.value("dy").toDouble())),
                        o.value("expect"), tol, "move " + std::to_string(n++));
    }
    n = 0;
    for (const auto& c : v.value("turn").toArray()) {
        const QJsonObject o = c.toObject();
        const QJsonObject about = o.value("about").toObject();
        expectPlacement(core::turnedPlacement(placementOf(o.value("placement")), o.value("degrees").toDouble(),
                                              QPointF(about.value("x").toDouble(), about.value("y").toDouble())),
                        o.value("expect"), tol, "turn " + std::to_string(n++));
    }
    n = 0;
    for (const auto& c : v.value("drop").toArray()) {
        const QJsonObject d = c.toObject().value("drop").toObject();
        std::optional<core::DropTurn> turn;
        if (const QJsonValue t = d.value("turn"); t.isObject()) {
            const QJsonObject to = t.toObject();
            turn = core::DropTurn{ to.value("degrees").toDouble(), QPointF(to.value("pivotX").toDouble(), to.value("pivotY").toDouble()),
                                   QPointF(to.value("toX").toDouble(), to.value("toY").toDouble()) };
        }
        expectPlacement(core::droppedPlacement(QPointF(d.value("dx").toDouble(), d.value("dy").toDouble()), turn),
                        c.toObject().value("expect"), tol, "drop " + std::to_string(n++));
    }
    EXPECT_GT(n, 1);
}

TEST(SharedModulePlacementContract, FindsThePlacementFromThePartsAndTellsAChangedModule) {
    const QJsonObject v = vectors();
    VectorParts parts;
    const double tol = v.value("tolerance").toDouble();
    for (const auto& c : v.value("align").toArray()) {
        const QJsonObject o = c.toObject();
        const std::string name = o.value("name").toString().toStdString();
        const edit::Placement got = edit::alignToPlaced(batch(o.value("version")), bricksOf(o.value("placed")), parts.lib);
        expectPlacement({ got.degrees, got.to.x(), got.to.y(), std::nullopt }, o.value("expect"), tol, "align " + name);
        EXPECT_EQ(got.matched, o.value("expect").toObject().value("matched").toInt()) << name;
    }
    for (const auto& c : v.value("matches").toArray()) {
        const QJsonObject o = c.toObject();
        EXPECT_EQ(edit::matchesVersion(batch(o.value("version")), bricksOf(o.value("placed")), parts.lib,
                                       core::readPlacement(o.value("placement"))),
                  o.value("expect").toBool())
            << o.value("name").toString().toStdString();
    }
    for (const auto& c : v.value("resolve").toArray()) {
        const QJsonObject o = c.toObject();
        expectPlacement(edit::resolvePlacement(batch(o.value("version")), bricksOf(o.value("placed")),
                                               core::readPlacement(o.value("kept")), parts.lib),
                        o.value("expect"), tol, "resolve " + o.value("name").toString().toStdString());
    }
}

// ---- The words --------------------------------------------------------------

TEST(SharedModuleWords, SayAPlacedCopysStateAsTheWebDoes) {
    core::SharedPlaced p;
    p.placedId = QStringLiteral("m1");
    p.name = QStringLiteral("Harbor crane");
    p.version = 7;
    p.by = QStringLiteral("Alice");
    EXPECT_EQ(ui::sharedwords::sharedNote(p, nullptr), QStringLiteral("Shared by Alice · v7"));
    sync::LayoutSharedModule row;
    row.placedId = QStringLiteral("m1");
    row.state = QStringLiteral("shared");
    row.ownerName = QStringLiteral("Alice B");
    row.latest = 8;
    row.latestNote = QStringLiteral("longer pier");
    EXPECT_EQ(ui::sharedwords::newerVersion(p, &row), 8);
    EXPECT_EQ(ui::sharedwords::sharedNote(p, &row), QStringLiteral("Shared by Alice B · v7 · v8 is ready"));
    EXPECT_EQ(ui::sharedwords::badge(p, 8), QStringLiteral("🔗 Alice ● v8 is ready"));
    EXPECT_EQ(ui::sharedwords::badge(p, 0), QStringLiteral("🔗 Alice"));
    const auto updates = ui::sharedwords::findUpdates({ p }, { row });
    ASSERT_EQ(updates.size(), 1);
    EXPECT_EQ(ui::sharedwords::updatesText(updates),
              QStringLiteral("“Harbor crane” has a new version from Alice B (v8): “longer pier”."));
    row.latest = 7;
    EXPECT_EQ(ui::sharedwords::newerVersion(p, &row), 0);
    EXPECT_TRUE(ui::sharedwords::findUpdates({ p }, { row }).isEmpty());
    row.state = QStringLiteral("unshared");
    row.latest = 9;
    EXPECT_EQ(ui::sharedwords::newerVersion(p, &row), 0) << "a copy that no longer follows gets no updates";
    EXPECT_EQ(ui::sharedwords::sharedNote(p, &row), QStringLiteral("No longer shared · v7: no more updates"));
    row.state = QStringLiteral("gone");
    EXPECT_EQ(ui::sharedwords::sharedNote(p, &row), QStringLiteral("Deleted by its owner · v7: no more updates"));

    ui::sharedwords::Extra extra;
    extra.changedOutside = true;
    extra.marks.overlaps = 2;
    EXPECT_EQ(ui::sharedwords::sharedNote(p, nullptr, extra),
              QStringLiteral("Shared by Alice · v7 · Changed outside the app · Overlaps 2 part(s)"));
    extra = {};
    extra.cantCheck = true;
    EXPECT_EQ(ui::sharedwords::sharedNote(p, nullptr, extra), QStringLiteral("Shared by Alice · v7 · Can't check for updates (offline)"));

    p.review = core::SharedReview{ 7, 8, 1 };
    EXPECT_EQ(ui::sharedwords::reviewText(p, {}), QStringLiteral("“Harbor crane” is now version 8."));
    EXPECT_TRUE(ui::sharedwords::reviewText(p, { 2, 1 }).startsWith(QStringLiteral("“Harbor crane” is now version 8. It overlaps 2 part(s).")));
}

TEST(SharedModuleWords, SayWhomToAskWhenAModuleCantGoInALayout) {
    sync::SharedPlaceCheck c;
    c.title = QStringLiteral("Harbor crane");
    c.state = QStringLiteral("unshared");
    c.moduleOwner = { QStringLiteral("user"), QStringLiteral("Alice"), false };
    c.layoutOwner = { QStringLiteral("org"), QStringLiteral("Riverside LUG"), false };
    auto r = ui::sharedwords::refusalText(c);
    EXPECT_EQ(r.title, QStringLiteral("“Harbor crane” can't go in this layout yet"));
    EXPECT_EQ(r.body, QStringLiteral("Alice shared “Harbor crane” with you, not with Riverside LUG. Ask Alice to share it with the club."));
    EXPECT_TRUE(r.ask.startsWith(QStringLiteral("Hi Alice, could you share your module “Harbor crane” with Riverside LUG?")));
    c.layoutOwner = { QStringLiteral("user"), QString(), true };
    r = ui::sharedwords::refusalText(c);
    EXPECT_EQ(r.body, QStringLiteral("Alice shared “Harbor crane” with a club you're in, for the club's layouts, not with you. Ask Alice to share it with you too."));

    sync::ServerRefusal no;
    no.code = QStringLiteral("only_the_layouts_owner");
    EXPECT_TRUE(ui::sharedwords::stepRefusal(no, QStringLiteral("x")).startsWith(QStringLiteral("Only the layout's owner")));
    no.code = QStringLiteral("something_else");
    EXPECT_EQ(ui::sharedwords::stepRefusal(no, QStringLiteral("x")), QStringLiteral("x"));
}

// ---- The notices over the map -------------------------------------------------

TEST(SharedModulesBarTest, ShowsUpdateNotNowAndThenGoBackAndOk) {
    ui::SharedModulesBar bar;
    QSignalSpy update(&bar, &ui::SharedModulesBar::updateRequested);
    QSignalSpy goBack(&bar, &ui::SharedModulesBar::goBackRequested);
    QSignalSpy ok(&bar, &ui::SharedModulesBar::okRequested);
    EXPECT_FALSE(bar.updatesShown());

    // Someone who can't change the layout sees the news, not the button.
    bar.setUpdates(QStringLiteral("“Harbor crane” has a new version from Alice (v8)."), {}, 1, QStringLiteral("m1:8"));
    EXPECT_TRUE(bar.updatesShown());
    EXPECT_TRUE(bar.updateButton()->isHidden());
    EXPECT_FALSE(bar.findChild<QLabel*>(QStringLiteral("sharedUpdatesCant"))->isHidden());
    bar.setCanUpdate(true);
    EXPECT_FALSE(bar.updateButton()->isHidden());
    EXPECT_EQ(bar.updateButton()->text(), QStringLiteral("Update"));
    bar.updateButton()->click();
    EXPECT_EQ(update.count(), 1);
    // Busy: no second press.
    bar.setBusy(true);
    EXPECT_FALSE(bar.updateButton()->isEnabled());
    bar.setBusy(false);

    // Not now hides it for these versions; a newer one shows it again.
    bar.notNowButton()->click();
    EXPECT_FALSE(bar.updatesShown());
    bar.setUpdates(QStringLiteral("x"), {}, 1, QStringLiteral("m1:8"));
    EXPECT_FALSE(bar.updatesShown());
    bar.setUpdates(QStringLiteral("3 shared modules have new versions."), QStringLiteral("“A” v2, “B” v3, “C” v9"), 3, QStringLiteral("m1:9,m2:3"));
    EXPECT_TRUE(bar.updatesShown());
    EXPECT_EQ(bar.updateButton()->text(), QStringLiteral("Update all 3"));

    // After an update: what it is now, Go back and OK.
    bar.setReview(QStringLiteral("“Harbor crane” is now version 8. It overlaps 2 part(s)."), 7);
    EXPECT_TRUE(bar.reviewShown());
    EXPECT_EQ(bar.goBackButton()->text(), QStringLiteral("Go back to version 7"));
    EXPECT_FALSE(bar.goBackButton()->isHidden());
    bar.goBackButton()->click();
    bar.okButton()->click();
    EXPECT_EQ(goBack.count(), 1);
    EXPECT_EQ(ok.count(), 1);
    bar.setReview(QStringLiteral("2 shared modules were updated."), 0);
    EXPECT_TRUE(bar.goBackButton()->isHidden());
    bar.setReview({}, 0);
    EXPECT_FALSE(bar.reviewShown());
}

// ---- The lock in the map view -----------------------------------------------

namespace {

class SharedLockView : public ::testing::Test {
protected:
    void SetUp() override {
        const QString partsRoot = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        parts_.addSearchPath(partsRoot);
        parts_.scan();
        auto map = std::make_unique<core::Map>();
        auto sheet = std::make_unique<core::LayerBrick>();
        sheet->guid = QStringLiteral("L1");
        sheet->name = QStringLiteral("Track");
        const auto add = [&](const QString& guid, const QString& part, double x, double y) {
            core::Brick b;
            b.guid = guid;
            b.partNumber = part;
            b.displayArea = QRectF(x, y, 16, 4);
            parts::placement::placeByImageCentre(b, QPointF(x + 8, y + 2), parts_);
            sheet->bricks.push_back(b);
        };
        // Two straight rails end to end (the shared module), and a loose rail well away.
        add(QStringLiteral("p1"), QStringLiteral("2865.8"), 0, 0);
        add(QStringLiteral("p2"), QStringLiteral("2865.8"), 16, 0);
        add(QStringLiteral("loose"), QStringLiteral("2865.8"), 0, 80);
        map->layers().push_back(std::move(sheet));
        core::Module m;
        m.id = QStringLiteral("m1");
        m.name = QStringLiteral("Harbor crane");
        m.memberIds = { QStringLiteral("p1"), QStringLiteral("p2") };
        m.libraryModuleId = kLib;
        m.libraryVersion = 7;
        m.shared = QJsonObject{ { "by", "Alice" },
                                { "placement", core::placementJson({ 0, 8, 2, QMap<QString, QString>{ { "Track", "L1" } } }) } };
        map->sidecar.modules.push_back(m);
        view_ = std::make_unique<ui::MapView>(parts_);
        view_->resize(800, 600);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(map));
        told_ = std::make_unique<QSignalSpy>(view_.get(), &ui::MapView::sharedLockTold);
    }
    core::Map& map() { return *view_->currentMap(); }
    core::Module& mod() { return map().sidecar.modules.front(); }
    core::LayerBrick& sheet() { return static_cast<core::LayerBrick&>(*map().layers().front()); }
    QString lastTold() const { return told_->isEmpty() ? QString() : told_->last().first().toString(); }
    QGraphicsItem* moduleItem(const QString& role) {
        for (QGraphicsItem* it : view_->scene()->items())
            if (it->data(rendering::kModuleAnnotationRole).toString() == role) return it;
        return nullptr;
    }

    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MapView> view_;
    std::unique_ptr<QSignalSpy> told_;
};

}  // namespace

TEST_F(SharedLockView, AChangeToItsPartsIsTakenBackAndSaysWhy) {
    QUndoStack& stack = *view_->undoStack();
    // Renaming it.
    stack.push(new edit::RenameModuleCommand(map(), "m1", "My crane"));
    EXPECT_EQ(mod().name, QStringLiteral("Harbor crane"));
    ASSERT_EQ(told_->count(), 1);
    EXPECT_EQ(lastTold(), QStringLiteral("“Harbor crane” is shared by Alice, so its name is Alice's. You can move it, turn it and change its outline color."));
    EXPECT_FALSE(stack.canUndo()) << "nothing was done, so there is nothing to undo";

    // Deleting one of its parts.
    edit::DeleteBricksCommand::Entry e;
    e.layerIndex = 0;
    e.indexInLayer = 0;
    e.brick = sheet().bricks[0];
    stack.push(new edit::DeleteBricksCommand(map(), { e }));
    EXPECT_EQ(sheet().bricks.size(), 3u);
    EXPECT_EQ(mod().memberIds.size(), 2);
    EXPECT_EQ(told_->count(), 2);
    EXPECT_TRUE(lastTold().contains(QStringLiteral("its parts can't be changed here")));
    // Redo tries the refused step again: refused again.
    stack.redo();
    EXPECT_EQ(sheet().bricks.size(), 3u);
    EXPECT_EQ(told_->count(), 3);

    // Ungrouping it.
    stack.push(new edit::FlattenModuleCommand(map(), "m1"));
    ASSERT_EQ(map().sidecar.modules.size(), 1u);
    EXPECT_TRUE(lastTold().contains(QStringLiteral("it stays one module")));

    // Edit module: it says why instead of opening.
    view_->setEditingModule(QStringLiteral("m1"));
    EXPECT_TRUE(view_->editingModule().isEmpty());
    EXPECT_TRUE(lastTold().contains(QStringLiteral("its parts can't be changed here")));

    // The loose part is as free as ever.
    edit::DeleteBricksCommand::Entry loose;
    loose.layerIndex = 0;
    loose.indexInLayer = 2;
    loose.brick = sheet().bricks[2];
    const int said = static_cast<int>(told_->count());
    stack.push(new edit::DeleteBricksCommand(map(), { loose }));
    EXPECT_EQ(sheet().bricks.size(), 2u);
    EXPECT_EQ(told_->count(), said);
}

TEST_F(SharedLockView, MovedAndTurnedAsAWholeItsPlacementFollows) {
    QUndoStack& stack = *view_->undoStack();
    stack.push(new edit::MoveModuleCommand(map(), "m1", QPointF(32, 16)));
    EXPECT_TRUE(told_->isEmpty());
    auto at = core::sharedPlaced(mod())->placement;
    ASSERT_TRUE(at);
    EXPECT_NEAR(at->x, 40, 1e-6);
    EXPECT_NEAR(at->y, 18, 1e-6);
    stack.push(new edit::RotateModuleCommand(map(), parts_, "m1", 90));
    at = core::sharedPlaced(mod())->placement;
    EXPECT_NEAR(at->degrees, 90, 1e-4);
    // The version (its first rail's middle at the origin, the second 16 to
    // the right) placed by it is the module where it sits now.
    core::Brick a = sheet().bricks[0], b = sheet().bricks[1];
    a.orientation = b.orientation = 0;
    parts::placement::placeByImageCentre(a, QPointF(0, 0), parts_);
    parts::placement::placeByImageCentre(b, QPointF(16, 0), parts_);
    edit::ImportBbmAsModuleCommand::LayerBatch v;
    v.layerName = QStringLiteral("Track");
    v.bricks = { a, b };
    EXPECT_TRUE(edit::matchesVersion({ v }, edit::placedParts(map(), mod().memberIds), parts_, at));
    EXPECT_EQ(edit::partsInPlace({ v }, edit::placedParts(map(), mod().memberIds), *at, parts_), 2);
    // Undo: back, placement and all.
    stack.undo();
    stack.undo();
    at = core::sharedPlaced(mod())->placement;
    EXPECT_NEAR(at->x, 8, 1e-6);
    EXPECT_NEAR(at->degrees, 0, 1e-6);
    // Removing it whole is allowed.
    stack.push(edit::deleteModuleWithPartsCommand(map(), "m1"));
    EXPECT_TRUE(map().sidecar.modules.empty());
    EXPECT_TRUE(told_->isEmpty());
}

TEST_F(SharedLockView, TheMapShowsALinkBadgeAndItsOwnDashes) {
    QGraphicsItem* badge = moduleItem(QStringLiteral("badge"));
    ASSERT_NE(badge, nullptr);
    EXPECT_EQ(badge->data(rendering::kModuleBadgeTextRole).toString(), QStringLiteral("🔗 Alice"));
    EXPECT_TRUE(badge->flags() & QGraphicsItem::ItemIgnoresTransformations) << "the same size at every zoom";
    auto* frame = dynamic_cast<QGraphicsRectItem*>(moduleItem(QStringLiteral("frame")));
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->pen().dashPattern().size(), 4) << "long dash, dot: the shared frame";

    view_->setSharedBadges({ { QStringLiteral("m1"), QStringLiteral("🔗 Alice ● v8 is ready") } });
    badge = moduleItem(QStringLiteral("badge"));
    ASSERT_NE(badge, nullptr);
    EXPECT_EQ(badge->data(rendering::kModuleBadgeTextRole).toString(), QStringLiteral("🔗 Alice ● v8 is ready"));

    // "Make it mine": an ordinary module again, no badge, the usual dashes.
    view_->undoStack()->push(new edit::DetachSharedModuleCommand(map(), "m1"));
    EXPECT_EQ(moduleItem(QStringLiteral("badge")), nullptr);
    frame = dynamic_cast<QGraphicsRectItem*>(moduleItem(QStringLiteral("frame")));
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->pen().dashPattern().size(), 2);
    view_->undoStack()->undo();
    EXPECT_NE(moduleItem(QStringLiteral("badge")), nullptr) << "undo puts the lock back";
}

TEST_F(SharedLockView, AnUpdateSaysWhatItNowOverlaps) {
    EXPECT_FALSE(edit::sharedMarks(map(), mod().memberIds, parts_).any());
    // The loose rail right under the module's first one.
    sheet().bricks[2].displayArea.moveTopLeft(sheet().bricks[0].displayArea.topLeft() + QPointF(2, 1));
    const edit::SharedMarks marks = edit::sharedMarks(map(), mod().memberIds, parts_);
    EXPECT_EQ(marks.overlaps, 1);
    // Touching end to end is not lying over it.
    sheet().bricks[2].displayArea.moveTopLeft(sheet().bricks[1].displayArea.topRight());
    EXPECT_EQ(edit::sharedMarks(map(), mod().memberIds, parts_).overlaps, 0);
}

// Find & Replace leaves a shared module's parts as they are, and says how many it skipped.
TEST_F(SharedLockView, FindAndReplaceSkipsItsPartsAndSaysHowMany) {
    // Any other part the library has.
    QString other;
    for (const QString& key : parts_.keys())
        if (key != QStringLiteral("2865.8") && key.startsWith(QStringLiteral("2867."))) other = key;
    if (other.isEmpty()) GTEST_SKIP() << "the BlueBrickParts library isn't checked out";
    ui::FindDialog d(*view_);
    d.show();
    const auto lines = d.findChildren<QLineEdit*>();
    ASSERT_GE(lines.size(), 2);
    lines[0]->setText(QStringLiteral("2865.8"));
    lines[1]->setText(other);
    QPushButton* all = nullptr;
    for (auto* b : d.findChildren<QPushButton*>())
        if (b->text().remove(QLatin1Char('&')) == QStringLiteral("Replace All")) all = b;
    ASSERT_NE(all, nullptr);
    all->click();
    int changed = 0, kept = 0;
    for (const auto& b : sheet().bricks) (b.partNumber.compare(other, Qt::CaseInsensitive) == 0 ? changed : kept) += 1;
    EXPECT_EQ(changed, 1) << "the loose rail";
    EXPECT_EQ(kept, 2) << "the shared module's rails are its owner's";
    const QString said = d.findChild<QLabel*>(QStringLiteral("findStatus"))->text();
    EXPECT_TRUE(said.startsWith(QStringLiteral("Replaced 1 part."))) << said.toStdString();
    EXPECT_TRUE(said.contains(QStringLiteral("Skipped 2 part(s) of shared modules"))) << said.toStdString();
    EXPECT_TRUE(told_->isEmpty()) << "skipped, not refused";
}

// Files: a shared module is ordinary parts to BlueBrick (the .bbm is byte
// for byte what it would be without the lock), and the lock and its link
// live in the sidecar, which a .bbm's sidecar file and a .bld-layout both
// keep whole.
TEST_F(SharedLockView, ABbmAndALayoutFileKeepTheLockAndBlueBrickSeesOrdinaryParts) {
    // (As the app makes them: parts with BlueBrick's own kind of id.)
    core::Map layout;
    {
        auto track = std::make_unique<core::LayerBrick>();
        track->guid = core::newBbmId();
        track->name = QStringLiteral("Track");
        track->bricks = sheet().bricks;
        for (auto& b : track->bricks) b.guid = core::newBbmId();
        core::Module m = mod();
        m.memberIds = { track->bricks[0].guid, track->bricks[1].guid };
        m.shared->insert(QStringLiteral("placement"),
                         core::placementJson({ 0, 8, 2, QMap<QString, QString>{ { QStringLiteral("Track"), track->guid } } }));
        layout.sidecar.modules.push_back(m);
        layout.layers().push_back(std::move(track));
    }
    const core::Module shared = layout.sidecar.modules.front();
    const auto bbmOf = [&] {
        QBuffer out;
        out.open(QIODevice::WriteOnly);
        EXPECT_TRUE(saveload::writeBbm(layout, out).ok);
        return out.data();
    };
    const QByteArray locked = bbmOf();
    layout.sidecar.modules.front() = core::detached(shared);
    const QByteArray plain = bbmOf();
    layout.sidecar.modules.front() = shared;
    EXPECT_EQ(locked, plain) << "BlueBrick's file doesn't know the lock";
    EXPECT_FALSE(locked.contains("shared"));
    EXPECT_FALSE(locked.contains("Alice"));

    QTemporaryDir dir;
    // .bbm with its sidecar beside it.
    const QString bbm = dir.filePath(QStringLiteral("yard.bbm"));
    ASSERT_TRUE(saveload::writeBbm(layout, bbm).ok);
    ASSERT_TRUE(saveload::writeSidecar(saveload::sidecarPathFor(bbm), locked, layout.sidecar));
    auto read = saveload::readBbm(bbm);
    ASSERT_TRUE(read.ok());
    saveload::readSidecar(saveload::sidecarPathFor(bbm), locked, read.map->sidecar);
    ASSERT_EQ(read.map->sidecar.modules.size(), 1u);
    EXPECT_EQ(read.map->sidecar.modules.front().shared, shared.shared);
    EXPECT_EQ(read.map->sidecar.modules.front().memberIds, shared.memberIds);
    EXPECT_EQ(edit::placedParts(*read.map, shared.memberIds).size(), 2u) << "real parts, in the file";
    // Saved again: the same bytes.
    QBuffer again;
    again.open(QIODevice::WriteOnly);
    ASSERT_TRUE(saveload::writeBbm(*read.map, again).ok);
    EXPECT_TRUE(again.data() == locked) << "a .bbm read and written again is the same bytes";

    // .bld-layout: one file, the same.
    const QString native = dir.filePath(QStringLiteral("yard.bld-layout"));
    QString error;
    ASSERT_TRUE(import::writeLayoutFile(layout, native, &error)) << error.toStdString();
    const auto back = import::readLayoutFile(native, dir.path());
    ASSERT_TRUE(back.map) << back.error.toStdString();
    ASSERT_EQ(back.map->sidecar.modules.size(), 1u);
    const core::Module& kept = back.map->sidecar.modules.front();
    EXPECT_EQ(kept.shared, shared.shared);
    EXPECT_EQ(kept.memberIds, shared.memberIds);
    EXPECT_EQ(kept.libraryModuleId, shared.libraryModuleId);
    EXPECT_EQ(kept.libraryVersion, shared.libraryVersion);
    EXPECT_TRUE(core::isSharedModule(kept));
    EXPECT_EQ(edit::placedParts(*back.map, kept.memberIds).size(), 2u);
    // The lock still holds once it is open again.
    edit::SharedLockState lock;
    lock.rebase(*back.map, parts_);
    back.map->sidecar.modules.front().name = QStringLiteral("Mine");
    EXPECT_TRUE(lock.check(*back.map));
}

// Modules › Updates from Shared Modules…: the two choices, shown as the
// server has them; picking the other asks; someone who can't change the
// layout sees the choice, not the buttons.
TEST(SharedUpdatesDialog, ShowsTheServersChoiceAndAsksForTheOther) {
    ui::SharedUpdatesDialog dlg;
    EXPECT_FALSE(dlg.askButton()->isEnabled()) << "nothing to pick before the server has said";
    dlg.setCanChange(true);
    dlg.setCurrent(QStringLiteral("ask"));
    EXPECT_TRUE(dlg.askButton()->isChecked());
    EXPECT_FALSE(dlg.autoButton()->isChecked());
    EXPECT_TRUE(dlg.readOnlyNote()->isHidden());
    QStringList chosen;
    QObject::connect(&dlg, &ui::SharedUpdatesDialog::chosen, [&](const QString& v) { chosen << v; });
    dlg.autoButton()->click();
    EXPECT_EQ(chosen, QStringList{ QStringLiteral("auto") });
    // The server's answer shows; showing it isn't another choice.
    dlg.setCurrent(QStringLiteral("auto"));
    EXPECT_TRUE(dlg.autoButton()->isChecked());
    EXPECT_EQ(chosen.size(), 1);
    // Busy: no second ask.
    dlg.setBusy(true);
    dlg.askButton()->click();
    EXPECT_EQ(chosen.size(), 1);
    dlg.setBusy(false);
    // Read only.
    dlg.setCanChange(false);
    EXPECT_FALSE(dlg.askButton()->isEnabled());
    EXPECT_FALSE(dlg.readOnlyNote()->isHidden());
    EXPECT_EQ(dlg.readOnlyNote()->text(), QStringLiteral("Someone who can change this layout can change this."));
    auto* help = dlg.findChild<ui::help::HelpButton*>();
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->key(), QStringLiteral("module.sharedUpdatesSetting"));
}

TEST(SharedModuleWords, UpdatedWhileYouWereAway) {
    EXPECT_EQ(ui::sharedwords::awayText(QStringLiteral("Harbor crane"), QStringLiteral("Alice"), 7, 9),
              QStringLiteral("“Harbor crane” went from version 7 to 9: this layout takes Alice's updates automatically."));
    EXPECT_EQ(ui::sharedwords::awayText(QStringLiteral("Harbor crane"), {}, 0, 9),
              QStringLiteral("“Harbor crane” is now version 9: this layout takes its owner's updates automatically."));
    EXPECT_EQ(ui::sharedwords::awayManyText(2),
              QStringLiteral("2 shared modules were updated: this layout takes their owners' updates automatically."));
    EXPECT_EQ(ui::sharedwords::awayLine(QStringLiteral("Tunnel"), 2, 3), QStringLiteral("“Tunnel” v2 → v3"));
}
