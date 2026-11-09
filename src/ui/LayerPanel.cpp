#include "LayerPanel.h"
#include "SheetNames.h"

#include "../core/Layer.h"
#include "../core/Map.h"
#include "../rendering/SceneBuilder.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace bld::ui {

namespace {

// The file format's name for the kind, shown in the tooltip next to the
// friendly one so the technical detail stays findable.
const char* layerKindName(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return "grid";
        case core::LayerKind::Brick:        return "brick";
        case core::LayerKind::Text:         return "text";
        case core::LayerKind::Area:         return "area";
        case core::LayerKind::Ruler:        return "ruler";
        case core::LayerKind::AnchoredText: return "anchored";
    }
    return "?";
}

// Compact per-kind symbol so the user can tell sheet types apart at a
// glance, drawn as the row's icon. Unicode symbols keep this free of icon
// resources.
QString layerKindGlyph(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return QStringLiteral("◫");
        case core::LayerKind::Brick:        return QStringLiteral("▦");
        case core::LayerKind::Text:         return QStringLiteral("A");
        case core::LayerKind::Area:         return QStringLiteral("▣");
        case core::LayerKind::Ruler:        return QStringLiteral("⟷");
        case core::LayerKind::AnchoredText: return QStringLiteral("↳");
    }
    return QStringLiteral("?");
}

constexpr int kKindRole = Qt::UserRole + 1;

// The active sheet's row: filled with the accent color, a thick accent edge
// on the left, its name bold and an "ACTIVE" badge on the right, so it never
// reads as an ordinary pick. A flashed row gets an accent outline.
class SheetRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const bool active = index.data(LayerPanel::kActiveRole).toBool();
        const bool flash = index.data(LayerPanel::kFlashRole).toBool();
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QColor accent = opt.palette.color(QPalette::Highlight);
        if (active) {
            // Our own fill instead of the style's selection look.
            opt.state &= ~QStyle::State_Selected;
            QColor fill = accent;
            fill.setAlpha(90);
            p->fillRect(opt.rect, fill);
            p->fillRect(QRect(opt.rect.left(), opt.rect.top(), 4, opt.rect.height()), accent);
            opt.font.setBold(true);
            opt.rect.adjust(6, 0, -badgeWidth(opt) - 8, 0);
        }
        QStyledItemDelegate::paint(p, opt, index);
        if (active) {
            const QString text = LayerPanel::tr("ACTIVE");
            QFont f = option.font;
            f.setBold(true);
            f.setPixelSize(10);
            const int w = QFontMetrics(f).horizontalAdvance(text) + 12;
            const int h = 16;
            const QRect pill(option.rect.right() - w - 6, option.rect.center().y() - h / 2 + 1, w, h);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            p->drawRoundedRect(pill, h / 2.0, h / 2.0);
            p->setFont(f);
            p->setPen(option.palette.color(QPalette::HighlightedText));
            p->drawText(pill, Qt::AlignCenter, text);
            p->restore();
        }
        if (flash) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(QPen(accent, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(option.rect).adjusted(1, 1, -1, -1), 4, 4);
            p->restore();
        }
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(std::max(s.height(), 22));
        return s;
    }

private:
    static int badgeWidth(const QStyleOptionViewItem& opt) {
        QFont f = opt.font;
        f.setBold(true);
        f.setPixelSize(10);
        return QFontMetrics(f).horizontalAdvance(LayerPanel::tr("ACTIVE")) + 12;
    }
};

}  // namespace

// The kind's symbol in the list's text color, so it suits light and dark.
QIcon LayerPanel::kindIcon(core::LayerKind k, const QWidget* list) {
    const qreal dpr = list->devicePixelRatioF();
    const int side = 16;
    QPixmap pm(QSize(side, side) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f = list->font();
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(list->palette().color(QPalette::Text));
    p.drawText(QRect(0, 0, side, side), Qt::AlignCenter, layerKindGlyph(k));
    p.end();
    return QIcon(pm);
}

QString LayerPanel::friendlyKindName(core::LayerKind k) {
    switch (k) {
        case core::LayerKind::Grid:         return tr("Grid sheet");
        case core::LayerKind::Brick:        return tr("Parts sheet");
        case core::LayerKind::Text:         return tr("Text sheet");
        case core::LayerKind::Area:         return tr("Area sheet");
        case core::LayerKind::Ruler:        return tr("Ruler sheet");
        case core::LayerKind::AnchoredText: return tr("Label sheet");
    }
    return tr("Sheet");
}

LayerPanel::LayerPanel(QWidget* parent) : QDockWidget(tr("Sheets"), parent) {
    auto* host = new QWidget(this);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(2);

    // Two rows, with words on the buttons ("Add sheet", not "+"), as on the web.
    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    auto* showAll = new QPushButton(tr("Show all"), host);
    auto* hideOthers = new QPushButton(tr("Solo"), host);
    hideOthers->setToolTip(tr("Show only the selected sheet"));
    auto* addBtn = new QPushButton(tr("Add sheet"), host);
    addBtn->setObjectName(QStringLiteral("addSheet"));
    addBtn->setToolTip(tr("Add a new sheet"));
    auto* delBtn = new QPushButton(tr("Remove sheet"), host);
    delBtn->setObjectName(QStringLiteral("removeSheet"));
    delBtn->setToolTip(tr("Remove the selected sheet"));
    auto* upBtn  = new QPushButton(tr("▲"), host);
    upBtn->setToolTip(tr("Move selected sheet up"));
    auto* dnBtn  = new QPushButton(tr("▼"), host);
    dnBtn->setToolTip(tr("Move selected sheet down"));
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch();
    row->addWidget(upBtn);
    row->addWidget(dnBtn);
    col->addLayout(row);
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(2);
    row2->addWidget(showAll);
    row2->addWidget(hideOthers);
    row2->addStretch();
    col->addLayout(row2);

    connect(addBtn, &QPushButton::clicked, this, [this]{
        QMenu m(this);
        auto add = [&](const QString& label, core::LayerKind k){
            auto* a = m.addAction(label);
            connect(a, &QAction::triggered, [this, k]{ emit addLayerRequested(k); });
        };
        add(tr("Grid sheet"),  core::LayerKind::Grid);
        add(tr("Parts sheet"), core::LayerKind::Brick);
        add(tr("Text sheet"),  core::LayerKind::Text);
        add(tr("Area sheet"),  core::LayerKind::Area);
        add(tr("Ruler sheet"), core::LayerKind::Ruler);
        m.exec(QCursor::pos());
    });
    connect(delBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit deleteLayerRequested(indexOfRow(row));
    });
    connect(upBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit moveLayerRequested(indexOfRow(row), +1);
    });
    connect(dnBtn, &QPushButton::clicked, this, [this]{
        const int row = list_->currentRow();
        if (row >= 0) emit moveLayerRequested(indexOfRow(row), -1);
    });

    list_ = new QListWidget(host);
    list_->setObjectName(QStringLiteral("sheetList"));  // (touch mode's taller rows)
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->installEventFilter(this);
    list_->setItemDelegate(new SheetRowDelegate(list_));
    col->addWidget(list_);

    setWidget(host);

    connect(showAll, &QPushButton::clicked, this, [this]{
        for (int i = 0; i < list_->count(); ++i) list_->item(i)->setCheckState(Qt::Checked);
    });
    connect(hideOthers, &QPushButton::clicked, this, [this]{
        auto* sel = list_->currentItem();
        if (!sel) return;
        for (int i = 0; i < list_->count(); ++i) {
            list_->item(i)->setCheckState(list_->item(i) == sel ? Qt::Checked : Qt::Unchecked);
        }
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!builder_) return;
        const int idx = indexOfRow(list_->row(item));
        const bool visible = item->checkState() == Qt::Checked;
        // Only a show / hide counts: a new icon or name changes the item too.
        if (map_ && idx >= 0 && idx < static_cast<int>(map_->layers().size()) && map_->layers()[idx]->visible == visible)
            return;
        builder_->setLayerVisible(idx, visible);
        // Also persist on the underlying Layer so a later scene rebuild
        // (triggered by e.g. a move / undo / paste) honours the user's
        // visibility choice. Without this, any rebuildScene call would
        // read the Layer's default visible=true and make hidden layers
        // reappear despite the checkbox still showing unchecked.
        if (map_ && idx >= 0 && idx < static_cast<int>(map_->layers().size())) {
            map_->layers()[idx]->visible = visible;
            // Module frames fit the parts that show, dashed more sparsely when some are hidden.
            builder_->refreshModuleLabels(*map_);
        }
        emit layerVisibilityChanged(idx, visible);
    });
    // Clicking (or arrow-keying) onto a row sets it as the active layer —
    // vanilla BlueBrick uses Map.selectedLayerIndex for new-item placements.
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row){
        if (row >= 0 && map_) {
            const int index = indexOfRow(row);
            // A sheet the user picks is the one saved from now on.
            if (index != map_->selectedLayerIndex) map_->fileSelection.reset();
            map_->selectedLayerIndex = index;
            emit activeLayerChanged(index);
            // Re-render the panel so the active row renders bold.
            setMap(map_, builder_);
        }
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item){
        if (item) emit layerOptionsRequested(indexOfRow(list_->row(item)));
    });

    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        QMenu menu(this);

        if (item) {
            const int row = list_->row(item);
            const int index = indexOfRow(row);
            const bool visible = (item->checkState() == Qt::Checked);
            auto* setActive = menu.addAction(tr("Make Active Sheet"));
            connect(setActive, &QAction::triggered, [this, row]{
                list_->setCurrentRow(row);
            });
            menu.addSeparator();
            auto* toggle = menu.addAction(visible ? tr("Hide") : tr("Show"));
            connect(toggle, &QAction::triggered, [item, visible]{
                item->setCheckState(visible ? Qt::Unchecked : Qt::Checked);
            });
            auto* allOn = menu.addAction(tr("Show all sheets"));
            connect(allOn, &QAction::triggered, [this]{
                for (int i = 0; i < list_->count(); ++i) list_->item(i)->setCheckState(Qt::Checked);
            });
            auto* allOff = menu.addAction(tr("Hide all other sheets"));
            connect(allOff, &QAction::triggered, [this, item]{
                for (int i = 0; i < list_->count(); ++i) {
                    list_->item(i)->setCheckState(list_->item(i) == item ? Qt::Checked : Qt::Unchecked);
                }
            });
            menu.addSeparator();
            auto* opts = menu.addAction(tr("Sheet options…"));
            connect(opts, &QAction::triggered, [this, index]{ emit layerOptionsRequested(index); });
            auto* ren = menu.addAction(tr("Rename…"));
            connect(ren, &QAction::triggered, [this, index, item]{
                bool ok = false;
                // The sheet's own name (the row may show "(untitled)").
                const QString current = item->data(Qt::UserRole).toString();
                const QString def = displaySheetName(current);
                const QString name = QInputDialog::getText(
                    this, tr("Rename sheet"), tr("Sheet name:"),
                    QLineEdit::Normal, def, &ok);
                if (!ok) return;
                if (const QString next = renamedSheet(current, name); !next.isEmpty()) emit renameLayerRequested(index, next);
            });
            auto* up = menu.addAction(tr("Move Up"));
            connect(up, &QAction::triggered, [this, index]{ emit moveLayerRequested(index, +1); });
            auto* dn = menu.addAction(tr("Move Down"));
            connect(dn, &QAction::triggered, [this, index]{ emit moveLayerRequested(index, -1); });
            menu.addSeparator();
            auto* del = menu.addAction(tr("Remove sheet"));
            connect(del, &QAction::triggered, [this, index]{ emit deleteLayerRequested(index); });
            menu.addSeparator();
        }

        // "Add Layer" submenu — available whether or not a row is under the cursor.
        auto* addMenu = menu.addMenu(tr("Add Sheet"));
        auto addKind = [&](const QString& label, core::LayerKind k){
            auto* a = addMenu->addAction(label);
            connect(a, &QAction::triggered, [this, k]{ emit addLayerRequested(k); });
        };
        addKind(tr("Grid sheet"),  core::LayerKind::Grid);
        addKind(tr("Parts sheet"), core::LayerKind::Brick);
        addKind(tr("Text sheet"),  core::LayerKind::Text);
        addKind(tr("Area sheet"),  core::LayerKind::Area);
        addKind(tr("Ruler sheet"), core::LayerKind::Ruler);

        menu.exec(list_->mapToGlobal(pos));
    });
}

bool LayerPanel::eventFilter(QObject* watched, QEvent* e) {
    // Redraw the kind icons in the list's new text color (light / dark).
    // Quietly: this runs inside Qt's walk over every widget on a theme
    // change, and an itemChanged here once rebuilt the "Adding to" menu,
    // deleting widgets that walk was still to visit (a crash).
    if (watched == list_ && e->type() == QEvent::PaletteChange) {
        const QSignalBlocker quiet(list_);
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* item = list_->item(i);
            item->setIcon(kindIcon(static_cast<core::LayerKind>(item->data(kKindRole).toInt()), list_));
        }
    }
    return QDockWidget::eventFilter(watched, e);
}

int LayerPanel::currentRow() const { return indexOfRow(list_->currentRow()); }

// The list shows the top sheet first, like the web and drawing apps: row 0
// is the last layer in the map (drawn on top). Signals and the map always
// use the layer's index; only the list's rows run the other way.
int LayerPanel::indexOfRow(int row) const { return row < 0 ? -1 : list_->count() - 1 - row; }
int LayerPanel::rowOfIndex(int index) const { return index < 0 || index >= list_->count() ? -1 : list_->count() - 1 - index; }

void LayerPanel::setMap(core::Map* map, rendering::SceneBuilder* builder) {
    builder_ = builder;
    map_ = map;
    list_->blockSignals(true);
    list_->clear();
    if (map) {
        const auto& layers = map->layers();
        for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
            const auto& layer = layers[i];
            const bool isActive = (i == map->selectedLayerIndex);
            // The row shows just the sheet's name, like the web's Sheets
            // panel; the kind is the icon, and the kind, number and
            // transparency are in the tooltip.
            auto* item = new QListWidgetItem(shownSheetName(*layer));
            item->setData(Qt::UserRole, layer->name);
            item->setData(kKindRole, static_cast<int>(layer->kind()));
            item->setIcon(kindIcon(layer->kind(), list_));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(layer->visible ? Qt::Checked : Qt::Unchecked);
            QStringList tip;
            tip << tr("%1 · #%2 · %3")
                       .arg(friendlyKindName(layer->kind()))
                       .arg(i)
                       .arg(QLatin1String(layerKindName(layer->kind())));
            if (layer->transparency < 100) tip << tr("Transparency: %1%").arg(layer->transparency);
            item->setData(kActiveRole, isActive);
            item->setData(kFlashRole, i == flashRow_);
            if (isActive) {
                QFont f = item->font(); f.setBold(true); item->setFont(f);
                tip << (layer->kind() == core::LayerKind::Brick
                            ? tr("Active sheet: new parts are placed here.")
                            : tr("Active sheet."));
            } else {
                tip << tr("Click to make this the active sheet.");
            }
            tip << tr("Double-click for sheet options.");
            item->setToolTip(tip.join(QLatin1Char('\n')));
            item->setData(Qt::AccessibleDescriptionRole, friendlyKindName(layer->kind()));
            list_->addItem(item);
        }
        if (const int row = rowOfIndex(map->selectedLayerIndex); row >= 0) list_->setCurrentRow(row);
    }
    list_->blockSignals(false);
    emit refreshed();
}

void LayerPanel::setActiveRow(int index) {
    if (const int row = rowOfIndex(index); row >= 0) list_->setCurrentRow(row);
}

void LayerPanel::setSheetVisible(int index, bool visible) {
    if (const int row = rowOfIndex(index); row >= 0)
        list_->item(row)->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
}

void LayerPanel::flashRow(int index) {
    const auto mark = [this](int sheet, bool on) {
        if (const int row = rowOfIndex(sheet); row >= 0) list_->item(row)->setData(kFlashRole, on);
    };
    list_->blockSignals(true);
    mark(flashRow_, false);
    flashRow_ = index;
    mark(index, true);
    list_->blockSignals(false);
    if (const int row = rowOfIndex(index); row >= 0) list_->scrollToItem(list_->item(row));
    const int seq = ++flashSeq_;
    QTimer::singleShot(4000, this, [this, seq, mark] {
        if (seq != flashSeq_) return;
        list_->blockSignals(true);
        mark(flashRow_, false);
        list_->blockSignals(false);
        flashRow_ = -1;
    });
}

}
