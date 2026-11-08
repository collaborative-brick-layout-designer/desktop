// The Sheets panel's rows: just the sheet's name, like the web's Sheets
// panel, with the kind as an icon and the kind, number and transparency
// in the tooltip.

#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/Map.h"
#include "ui/LayerPanel.h"

#include <gtest/gtest.h>

#include <QImage>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>

using namespace bld;

namespace {

struct PanelOnMap {
    core::Map map;
    ui::LayerPanel panel;

    PanelOnMap() {
        auto grid = std::make_unique<core::LayerGrid>();
        grid->name = QStringLiteral("Grid");
        map.layers().push_back(std::move(grid));
        auto track = std::make_unique<core::LayerBrick>();
        track->name = QStringLiteral("Track");
        map.layers().push_back(std::move(track));
        auto buildings = std::make_unique<core::LayerBrick>();
        buildings->name = QStringLiteral("Buildings");
        buildings->transparency = 60;
        map.layers().push_back(std::move(buildings));
        auto unnamed = std::make_unique<core::LayerBrick>();
        map.layers().push_back(std::move(unnamed));
        map.selectedLayerIndex = 1;
        panel.setMap(&map, nullptr);
    }

    QListWidget* list() { return panel.findChild<QListWidget*>(); }
    // The row of the map's layer `i` (the list shows the top sheet first).
    QListWidgetItem* row(int i) { return list()->item(list()->count() - 1 - i); }
};

// The lightness of the icon's drawn (opaque) pixels.
int iconLightness(const QListWidgetItem* item) {
    const QImage img = item->icon().pixmap(16, 16).toImage();
    int best = -1;
    int alpha = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.alpha() > alpha) {
                alpha = c.alpha();
                best = c.lightness();
            }
        }
    }
    return best;
}

}  // namespace

TEST(LayerPanel, RowsShowJustTheSheetsName) {
    PanelOnMap t;
    EXPECT_EQ(t.row(0)->text(), QStringLiteral("Grid"));
    EXPECT_EQ(t.row(1)->text(), QStringLiteral("Track"));
    EXPECT_EQ(t.row(2)->text(), QStringLiteral("Buildings"));
    EXPECT_EQ(t.row(3)->text(), QStringLiteral("(untitled)"));
    // The name itself, for Rename's prompt.
    EXPECT_EQ(t.row(2)->data(Qt::UserRole).toString(), QStringLiteral("Buildings"));
    EXPECT_TRUE(t.row(3)->data(Qt::UserRole).toString().isEmpty());
}

TEST(LayerPanel, TheKindIsAnIconAndTheDetailsAreInTheTooltip) {
    PanelOnMap t;
    for (int i = 0; i < 4; ++i) EXPECT_FALSE(t.row(i)->icon().isNull()) << i;
    const QString tip = t.row(2)->toolTip();
    EXPECT_TRUE(tip.contains(QStringLiteral("Parts sheet"))) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("#2"))) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("brick"))) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("60%"))) << tip.toStdString();
    EXPECT_FALSE(tip.contains(QStringLiteral("Active"))) << tip.toStdString();
    EXPECT_TRUE(t.row(0)->toolTip().contains(QStringLiteral("Grid sheet")));
    EXPECT_FALSE(t.row(0)->toolTip().contains(QStringLiteral("Transparency")));
    // The active sheet says so, in bold.
    EXPECT_TRUE(t.row(1)->toolTip().contains(QStringLiteral("Active sheet")));
    EXPECT_TRUE(t.row(1)->font().bold());
    EXPECT_FALSE(t.row(2)->font().bold());
}

TEST(LayerPanel, KindIconsFollowTheTheme) {
    PanelOnMap t;
    QPalette light = t.panel.palette();
    light.setColor(QPalette::Text, Qt::black);
    t.panel.setPalette(light);
    const int dark = iconLightness(t.row(2));
    QPalette night = light;
    night.setColor(QPalette::Text, Qt::white);
    t.panel.setPalette(night);
    const int bright = iconLightness(t.row(2));
    EXPECT_LT(dark, 60);
    EXPECT_GT(bright, 200);
}

// The top sheet is listed first, like the web's Sheets panel and drawing
// apps, and picking, ▲ / ▼ and the signals all use the map's own order.
TEST(LayerPanel, ListsTheTopSheetFirst) {
    PanelOnMap t;
    QListWidget* list = t.list();
    ASSERT_EQ(list->count(), 4);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("(untitled)"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("Buildings"));
    EXPECT_EQ(list->item(2)->text(), QStringLiteral("Track"));
    EXPECT_EQ(list->item(3)->text(), QStringLiteral("Grid"));
    // The active sheet (layer 1, "Track") is the current row, and currentRow() is its layer index.
    EXPECT_EQ(list->currentItem()->text(), QStringLiteral("Track"));
    EXPECT_EQ(t.panel.currentRow(), 1);
    // Picking a row makes its layer active, by the layer's index.
    int active = -1;
    QObject::connect(&t.panel, &ui::LayerPanel::activeLayerChanged, [&](int i) { active = i; });
    list->setCurrentRow(1);
    EXPECT_EQ(active, 2);
    EXPECT_EQ(t.map.selectedLayerIndex, 2);
    // ▲ asks to move that layer up the map's order (+1), which is up the list.
    int moved = -1, by = 0;
    QObject::connect(&t.panel, &ui::LayerPanel::moveLayerRequested, [&](int i, int d) { moved = i; by = d; });
    QPushButton* up = nullptr;
    for (auto* b : t.panel.findChildren<QPushButton*>())
        if (b->text() == QString::fromUtf8("\u25B2")) up = b;
    ASSERT_NE(up, nullptr);
    up->click();
    EXPECT_EQ(moved, 2);
    EXPECT_EQ(by, +1);
}
