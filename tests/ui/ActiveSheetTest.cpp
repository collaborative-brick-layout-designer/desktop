// Which sheet new parts go on is plain everywhere: the "Adding to" button
// on the toolbar, the Sheets panel's Active row and the Parts panel's
// title agree and follow every change; a part dragged over the map says
// where it will land; placing on a hidden sheet, or with a sheet active
// that can't hold parts, says what happened. "Fade other sheets while
// editing" fades the rest on screen only, never in a picture.

#include "core/Layer.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "ui/ActiveSheet.h"
#include "ui/ActiveSheetButton.h"
#include "ui/LayerPanel.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/NoticeArea.h"
#include "ui/PartsBrowser.h"
#include "ui/UpdateCheck.h"
#include "ui/SavedViews.h"
#include "rendering/SceneBuilder.h"
#include "ui/theme/PanelHeader.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QCheckBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QApplication>
#include <QDragEnterEvent>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>

#include <memory>

using namespace bld;

namespace {

class ActiveSheetTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QFile f(dir_.filePath(QStringLiteral("PL.7.xml")));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("<part></part>");
        f.close();
        QImage sprite(32, 32, QImage::Format_ARGB32);
        sprite.fill(Qt::darkGray);
        sprite.save(dir_.filePath(QStringLiteral("PL.7.png")));
        lib_.addSearchPath(dir_.path());
        lib_.scan();
        window_ = std::make_unique<ui::MainWindow>(lib_);
        window_->resize(1200, 800);
        window_->ensureDocument();
        window_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window_.get()));
        view_ = window_->findChild<ui::MapView*>();
        panel_ = window_->findChild<ui::LayerPanel*>();
        button_ = window_->findChild<ui::ActiveSheetButton*>(QStringLiteral("activeSheet"));
        ASSERT_TRUE(view_ && panel_ && button_);
        header_ = qobject_cast<ui::theme::PanelHeader*>(window_->findChild<ui::PartsBrowser*>()->titleBarWidget());
        ASSERT_NE(header_, nullptr);
        QApplication::processEvents();
    }
    void TearDown() override {
        view_->undoStack()->setClean();
        window_.reset();
        QStandardPaths::setTestModeEnabled(false);
    }

    core::Map& map() { return *view_->currentMap(); }
    QListWidget* list() { return panel_->findChild<QListWidget*>(QStringLiteral("sheetList")); }
    // The list's row for the map's layer `index` (the top sheet is listed first).
    int rowOf(int index) { return list()->count() - 1 - index; }
    int sheet(const QString& name) {
        for (int i = 0; i < static_cast<int>(map().layers().size()); ++i)
            if (map().layers()[i]->name == name) return i;
        return -1;
    }
    int partsOn(int i) {
        return static_cast<int>(static_cast<const core::LayerBrick&>(*map().layers()[i]).bricks.size());
    }
    void add(core::LayerKind kind, const QString& name) {
        emit panel_->addLayerRequested(kind);
        emit panel_->renameLayerRequested(static_cast<int>(map().layers().size()) - 1, name);
        QApplication::processEvents();
    }
    // Picks `name` from the button's menu, as a click does.
    void pick(const QString& name) {
        for (QAction* a : button_->menu()->actions())
            if (a->text() == name) { a->trigger(); QApplication::processEvents(); return; }
        ADD_FAILURE() << "no menu entry " << name.toStdString();
    }
    // The button, the Sheets panel and the Parts title all name `name`.
    void allSay(const QString& name, const QString& partsGoOn) {
        EXPECT_EQ(button_->text(), QStringLiteral("Adding to: %1").arg(name));
        int activeRows = 0;
        for (int i = 0; i < list()->count(); ++i) {
            if (!list()->item(i)->data(ui::LayerPanel::kActiveRole).toBool()) continue;
            ++activeRows;
            EXPECT_EQ(list()->item(i)->text(), name);
        }
        EXPECT_EQ(activeRows, 1);
        int checked = 0;
        for (QAction* a : button_->menu()->actions())
            if (a->isChecked()) { ++checked; EXPECT_EQ(a->text(), name); }
        EXPECT_EQ(checked, 1);
        EXPECT_EQ(header_->title()->text(), QStringLiteral("Parts → %1").arg(partsGoOn));
    }
    QPushButton* noticeButton(const QString& id, const QString& text) {
        QFrame* card = window_->notices()->card(id);
        if (!card) return nullptr;
        for (auto* b : card->findChildren<QPushButton*>())
            if (b->text() == text) return b;
        return nullptr;
    }

    QTemporaryDir dir_;
    parts::PartsLibrary lib_;
    std::unique_ptr<ui::MainWindow> window_;
    ui::MapView* view_ = nullptr;
    ui::LayerPanel* panel_ = nullptr;
    ui::ActiveSheetButton* button_ = nullptr;
    ui::theme::PanelHeader* header_ = nullptr;
};

}  // namespace

TEST(ActiveSheet, PartsGoOnTheActiveSheetOrTheFirstPartsSheet) {
    core::Map m;
    m.layers().push_back(std::make_unique<core::LayerGrid>());
    m.layers().push_back(std::make_unique<core::LayerBrick>());
    m.layers().push_back(std::make_unique<core::LayerText>());
    m.selectedLayerIndex = 1;
    EXPECT_EQ(ui::placementLayer(m), 1);
    m.selectedLayerIndex = 2;  // a text sheet can't hold parts
    EXPECT_EQ(ui::placementLayer(m), 1);
    m.layers().erase(m.layers().begin() + 1);
    m.selectedLayerIndex = 0;
    EXPECT_EQ(ui::placementLayer(m), -1);
}

TEST_F(ActiveSheetTest, TheButtonThePanelAndThePartsTitleAgreeAndFollowEveryChange) {
    add(core::LayerKind::Brick, QStringLiteral("Tables"));
    // The blank layout's "Bricks" is active.
    allSay(QStringLiteral("Bricks"), QStringLiteral("Bricks"));

    // Switch with the button: everything follows, and a part lands there.
    pick(QStringLiteral("Tables"));
    EXPECT_EQ(map().selectedLayerIndex, sheet(QStringLiteral("Tables")));
    allSay(QStringLiteral("Tables"), QStringLiteral("Tables"));
    view_->addPartAtScenePos(QStringLiteral("PL.7"), QPointF(64, 64));
    EXPECT_EQ(partsOn(sheet(QStringLiteral("Tables"))), 1);
    EXPECT_EQ(partsOn(sheet(QStringLiteral("Bricks"))), 0);

    // A click in the Sheets panel: the button follows.
    list()->setCurrentRow(rowOf(sheet(QStringLiteral("Bricks"))));
    QApplication::processEvents();
    allSay(QStringLiteral("Bricks"), QStringLiteral("Bricks"));

    // A rename, and its undo.
    emit panel_->renameLayerRequested(sheet(QStringLiteral("Bricks")), QStringLiteral("Track"));
    QApplication::processEvents();
    allSay(QStringLiteral("Track"), QStringLiteral("Track"));
    view_->undoStack()->undo();
    QApplication::processEvents();
    allSay(QStringLiteral("Bricks"), QStringLiteral("Bricks"));
}

TEST_F(ActiveSheetTest, ADraggedPartSaysWhereItWillLand) {
    QMimeData mime;
    mime.setData(QByteArray(ui::PartsBrowser::kPartMimeType), QByteArray("PL.7"));
    const QPoint at(200, 150);
    // Newer Qt wants a QPointF here; older Qt only has the QPoint form.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QT_WARNING_POP
    QApplication::sendEvent(view_->viewport(), &enter);
    auto* tag = view_->viewport()->findChild<QLabel*>(QStringLiteral("dropSheetTag"));
    ASSERT_NE(tag, nullptr);
    EXPECT_TRUE(tag->isVisible());
    EXPECT_EQ(tag->text(), QStringLiteral("→ Bricks"));
    QDragLeaveEvent leave;
    QApplication::sendEvent(view_->viewport(), &leave);
    EXPECT_FALSE(tag->isVisible());
}

TEST_F(ActiveSheetTest, PlacingOnAHiddenSheetSaysSoAndOffersToShowIt) {
    const int bricks = sheet(QStringLiteral("Bricks"));
    panel_->setSheetVisible(bricks, false);
    QApplication::processEvents();
    EXPECT_EQ(button_->text(), QStringLiteral("Adding to: Bricks (hidden)"));
    view_->addPartAtScenePos(QStringLiteral("PL.7"), QPointF(64, 64));
    EXPECT_EQ(partsOn(bricks), 1);
    ASSERT_TRUE(window_->notices()->isShown(QStringLiteral("sheet.hidden")));
    EXPECT_EQ(panel_->flashingRow(), bricks);
    QPushButton* show = noticeButton(QStringLiteral("sheet.hidden"), QStringLiteral("Show sheet"));
    ASSERT_NE(show, nullptr);
    show->click();
    QApplication::processEvents();
    EXPECT_TRUE(map().layers()[bricks]->visible);
    EXPECT_EQ(list()->item(rowOf(bricks))->checkState(), Qt::Checked);
    EXPECT_EQ(button_->text(), QStringLiteral("Adding to: Bricks"));
    EXPECT_FALSE(window_->notices()->isShown(QStringLiteral("sheet.hidden")));
}

TEST_F(ActiveSheetTest, APartWithATextSheetActiveGoesOnAPartsSheetAndSaysWhich) {
    add(core::LayerKind::Text, QStringLiteral("Notes"));
    pick(QStringLiteral("Notes"));
    // The Parts title names where parts will really go.
    allSay(QStringLiteral("Notes"), QStringLiteral("Bricks"));
    EXPECT_TRUE(button_->toolTip().contains(QStringLiteral("so new parts go on “Bricks”"))) << button_->toolTip().toStdString();
    view_->addPartAtScenePos(QStringLiteral("PL.7"), QPointF(64, 64));
    const int bricks = sheet(QStringLiteral("Bricks"));
    EXPECT_EQ(partsOn(bricks), 1);
    ASSERT_TRUE(window_->notices()->isShown(QStringLiteral("sheet.redirected")));
    EXPECT_EQ(panel_->flashingRow(), bricks);
    // The notice can make that sheet the active one.
    QPushButton* make = noticeButton(QStringLiteral("sheet.redirected"), QStringLiteral("Make “Bricks” active"));
    ASSERT_NE(make, nullptr);
    make->click();
    QApplication::processEvents();
    allSay(QStringLiteral("Bricks"), QStringLiteral("Bricks"));
}

// "Fade other sheets while editing": off by default; on, every sheet but
// the active one draws faded and the fade follows a switch; a picture made
// from the map is pixel for pixel the unfaded one; it is kept on this
// computer (QSettings), not in the layout.
TEST_F(ActiveSheetTest, FadingTheOtherSheetsShowsOnScreenOnlyNeverInAPicture) {
    const QString key = QStringLiteral("view/fadeOtherSheets");
    QSettings().remove(key);
    add(core::LayerKind::Brick, QStringLiteral("Tables"));
    pick(QStringLiteral("Tables"));
    view_->addPartAtScenePos(QStringLiteral("PL.7"), QPointF(64, 64));
    pick(QStringLiteral("Bricks"));
    view_->addPartAtScenePos(QStringLiteral("PL.7"), QPointF(256, 64));
    const int bricks = sheet(QStringLiteral("Bricks"));
    const int tables = sheet(QStringLiteral("Tables"));
    const auto opacity = [this](int i) {
        const auto items = view_->builder()->layerItems(i);
        return items.isEmpty() ? -1.0 : items.front()->opacity();
    };
    const auto picture = [this] {
        // Nothing picked: the pick's outline isn't what's compared.
        view_->deselectAll();
        QApplication::processEvents();
        const QRectF bounds = view_->scene()->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        return ui::views::renderSceneImage(*view_->scene(), bounds, QSize(240, 120), {});
    };
    const auto box = [this] { return button_->menu()->findChild<QCheckBox*>(QStringLiteral("fadeOtherSheets")); };

    // Off by default: nothing fades. The picture to compare with.
    ASSERT_NE(box(), nullptr);
    EXPECT_FALSE(box()->isChecked());
    EXPECT_FALSE(view_->fadeOtherSheets());
    EXPECT_EQ(opacity(bricks), 1.0);
    EXPECT_EQ(opacity(tables), 1.0);
    const QImage plain = picture();
    // BLD_SHOTS_DIR=<dir> also saves screenshots (for a PR).
    const QString shots = qEnvironmentVariable("BLD_SHOTS_DIR");
    if (!shots.isEmpty()) window_->grab().save(shots + QStringLiteral("/desktop-fade-off.png"));
    const int undoSteps = view_->undoStack()->count();

    // On: the sheet not being added to fades, and it is saved on this computer.
    box()->setChecked(true);
    QApplication::processEvents();
    EXPECT_TRUE(view_->fadeOtherSheets());
    EXPECT_TRUE(QSettings().value(key).toBool());
    EXPECT_EQ(opacity(bricks), 1.0);
    EXPECT_DOUBLE_EQ(opacity(tables), ui::MapView::kFadedSheetOpacity);

    if (!shots.isEmpty()) {
        window_->grab().save(shots + QStringLiteral("/desktop-fade-on.png"));
        button_->menu()->popup(button_->mapToGlobal(QPoint(0, button_->height())));
        QApplication::processEvents();
        button_->menu()->grab().save(shots + QStringLiteral("/desktop-fade-menu.png"));
        button_->menu()->hide();
    }

    // Switch sheets: the fade follows; the box stays ticked in the new menu.
    pick(QStringLiteral("Tables"));
    EXPECT_DOUBLE_EQ(opacity(bricks), ui::MapView::kFadedSheetOpacity);
    EXPECT_EQ(opacity(tables), 1.0);
    EXPECT_TRUE(box()->isChecked());
    // A new part on the faded sheet's neighbour: drawn at full strength on the active one.
    view_->addPartAtScenePos(QStringLiteral("PL.7"), QPointF(64, 256));
    QApplication::processEvents();
    for (QGraphicsItem* it : view_->builder()->layerItems(sheet(QStringLiteral("Tables")))) EXPECT_EQ(it->opacity(), 1.0);
    for (QGraphicsItem* it : view_->builder()->layerItems(sheet(QStringLiteral("Bricks"))))
        EXPECT_DOUBLE_EQ(it->opacity(), ui::MapView::kFadedSheetOpacity);
    view_->undoStack()->undo();
    QApplication::processEvents();

    // The screen's own drawing shows the fade (so the check below means something)...
    EXPECT_NE(picture(), plain);
    // ...but a picture, as Export picture, PDF and Print make it, is the same as with the fade off.
    QImage faded;
    {
        const ui::MapView::SheetFadeOff off(view_);
        faded = picture();
    }
    EXPECT_EQ(faded, plain);
    // ...and the screen is faded again after it.
    EXPECT_DOUBLE_EQ(opacity(bricks), ui::MapView::kFadedSheetOpacity);

    // Not in the layout: turning it on and off made no edit.
    EXPECT_EQ(view_->undoStack()->count(), undoSteps + 1);  // the part placed and undone above
    EXPECT_EQ(view_->undoStack()->index(), undoSteps);

    // Off: everything is back to full strength.
    box()->setChecked(false);
    QApplication::processEvents();
    EXPECT_FALSE(QSettings().value(key).toBool());
    EXPECT_EQ(opacity(bricks), 1.0);
    EXPECT_EQ(opacity(tables), 1.0);
    EXPECT_EQ(picture(), plain);
    QSettings().remove(key);
}

// The "Adding to" menu and the Sheets panel both list the top sheet first,
// as the web does.
TEST_F(ActiveSheetTest, TheMenuAndThePanelListTheTopSheetFirst) {
    QStringList menuNames;
    for (QAction* a : button_->menu()->actions())
        if (a->isCheckable()) menuNames << a->text();
    QStringList panelNames;
    for (int i = 0; i < list()->count(); ++i) panelNames << list()->item(i)->data(Qt::UserRole).toString();
    QStringList mapTopFirst;
    for (int i = static_cast<int>(map().layers().size()) - 1; i >= 0; --i) mapTopFirst << map().layers()[i]->name;
    EXPECT_EQ(panelNames, mapTopFirst);
    ASSERT_EQ(menuNames.size(), mapTopFirst.size());
    for (int i = 0; i < menuNames.size(); ++i) EXPECT_TRUE(menuNames[i].startsWith(mapTopFirst[i])) << i;
}
