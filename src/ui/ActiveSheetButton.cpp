#include "ActiveSheetButton.h"
#include "SheetNames.h"

#include "ActiveSheet.h"
#include "LayerPanel.h"
#include "help/HelpButton.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QSettings>
#include <QWidgetAction>

namespace bld::ui {

namespace {
const QString kFadeKey = QStringLiteral("view/fadeOtherSheets");
}  // namespace

bool ActiveSheetButton::fadeOtherSheetsSetting() { return QSettings().value(kFadeKey, false).toBool(); }

ActiveSheetButton::ActiveSheetButton(QWidget* parent) : QToolButton(parent) {
    setObjectName(QStringLiteral("activeSheet"));
    setPopupMode(QToolButton::InstantPopup);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setMenu(new QMenu(this));
    // A clear frame in the accent color, so it reads as the place new parts go;
    // tall enough for a finger in touch mode.
    setStyleSheet(QStringLiteral(
        "QToolButton#activeSheet { border: 2px solid palette(highlight); border-radius: 8px;"
        " background: palette(window); color: palette(window-text);"
        " padding: 4px 20px 4px 8px; font-weight: 600; min-height: 22px; }"
        "QToolButton#activeSheet::menu-indicator { subcontrol-position: right center; right: 6px; }"));
    refresh(nullptr);
}

void ActiveSheetButton::refresh(const core::Map* map) {
    QMenu* m = menu();
    m->clear();
    const QString none = tr("no sheet");
    if (!map) {
        setText(tr("Adding to: %1").arg(none));
        setIcon(QIcon());
        iconKind_ = -1;
        setToolTip(tr("Open a layout to pick a sheet."));
        setEnabled(false);
        emit changed();
        return;
    }
    setEnabled(true);
    const auto& layers = map->layers();
    const int n = static_cast<int>(layers.size());
    const int active = map->selectedLayerIndex;
    const core::Layer* a = active >= 0 && active < n ? layers[active].get() : nullptr;
    const auto nameOf = [](const core::Layer& l) { return shownSheetName(l); };
    if (!a) {
        setText(tr("Adding to: %1").arg(none));
        setIcon(QIcon());
        iconKind_ = -1;
        setToolTip(tr("No sheet is picked. Click to pick one."));
    } else {
        setText(a->visible ? tr("Adding to: %1").arg(nameOf(*a)) : tr("Adding to: %1 (hidden)").arg(nameOf(*a)));
        setIcon(LayerPanel::kindIcon(a->kind(), this));
        iconKind_ = static_cast<int>(a->kind());
        const int target = placementLayer(*map);
        if (target != active)
            setToolTip(target >= 0
                           ? tr("“%1” is a %2, so new parts go on “%3”. Click to pick another sheet.")
                                 .arg(nameOf(*a), LayerPanel::friendlyKindName(a->kind()).toLower(), nameOf(*layers[target]))
                           : tr("“%1” is a %2, and there is no parts sheet yet. Click to pick another sheet.")
                                 .arg(nameOf(*a), LayerPanel::friendlyKindName(a->kind()).toLower()));
        else
            setToolTip(tr("New parts go on “%1”. Click to pick another sheet.").arg(nameOf(*a)));
    }
    // The menu lists the sheets in the Sheets panel's order: the top sheet first.
    auto* group = new QActionGroup(m);
    for (int i = n - 1; i >= 0; --i) {
        const core::Layer* l = layers[i].get();
        if (!l) continue;
        QString label = nameOf(*l);
        if (!l->visible) label += tr(" (hidden)");
        QAction* act = m->addAction(LayerPanel::kindIcon(l->kind(), this), label);
        act->setCheckable(true);
        act->setChecked(i == active);
        act->setToolTip(LayerPanel::friendlyKindName(l->kind()));
        act->setData(i);
        act->setProperty("kind", static_cast<int>(l->kind()));
        group->addAction(act);
        connect(act, &QAction::triggered, this, [this, i] { emit sheetPicked(i); });
    }
    // Under the sheets: the fade, with its help.
    m->addSeparator();
    auto* row = new QWidget(m);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 2, 8, 2);
    rowLayout->setSpacing(4);
    auto* fade = new QCheckBox(tr("Fade other sheets while editing"), row);
    fade->setObjectName(QStringLiteral("fadeOtherSheets"));
    fade->setToolTip(tr("Draw every sheet but this one faded, so it stands out. Only on your screen."));
    fade->setChecked(fadeOtherSheetsSetting());
    rowLayout->addWidget(fade, 1);
    rowLayout->addWidget(new help::HelpButton(QStringLiteral("sheets.fadeOthers"), row, fade));
    auto* fadeAct = new QWidgetAction(m);
    fadeAct->setDefaultWidget(row);
    m->addAction(fadeAct);
    connect(fade, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(kFadeKey, on);
        emit fadeOtherSheetsToggled(on);
    });
    emit changed();
}

void ActiveSheetButton::changeEvent(QEvent* e) {
    QToolButton::changeEvent(e);
    if (e->type() != QEvent::PaletteChange && e->type() != QEvent::StyleChange) return;
    if (iconKind_ >= 0) setIcon(LayerPanel::kindIcon(static_cast<core::LayerKind>(iconKind_), this));
    for (QAction* a : menu()->actions())
        if (const QVariant k = a->property("kind"); k.isValid())
            a->setIcon(LayerPanel::kindIcon(static_cast<core::LayerKind>(k.toInt()), this));
}

}  // namespace bld::ui
