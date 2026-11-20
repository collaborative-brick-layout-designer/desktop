// Switches on the map (trains design 6.3): Run mode, where a click on a
// switch flips it and parts stay where they are; View > Switches; and the
// right-click menu's "Set the switch". The web's editor/trackSwitches.ts
// does the same.

#include "MapView.h"
#include "MapViewInternal.h"

#include "../edit/SwitchCommands.h"
#include "help/HelpButton.h"
#include "../track/TrackNetwork.h"

#include <QAction>
#include <QGraphicsScene>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QUndoStack>

#include <cmath>

namespace bld::ui {

namespace {

constexpr double kArrowStuds = 10.0;  // as SceneBuilderSwitches.cpp draws it

// A leg's name in menus: "Straight", "Turn left", "Turn right" ("Turn left (2)").
QString legLabel(const QString& name) {
    const QString side = name.section(QLatin1Char(' '), 0, 0);
    const QString n = name.section(QLatin1Char(' '), 1, 1);
    const QString word = side == QLatin1String("left")    ? MapView::tr("Turn left")
                         : side == QLatin1String("right") ? MapView::tr("Turn right")
                                                          : MapView::tr("Straight");
    return n.isEmpty() ? word : MapView::tr("%1 (%2)").arg(word, n);
}

}  // namespace

void MapView::setRunMode(bool on) {
    if (runMode_ == on) return;
    if (on) cancelTrainPlacing();
    runMode_ = on;
    if (on && scene()) {
        scene()->clearSelection();
        connect(scene(), &QGraphicsScene::selectionChanged, this, &MapView::dropRunModePick, Qt::UniqueConnection);
    }
    setInteractive(!on);
    applySwitchesShown();
    if (on && !runBar_) {
        auto* bar = new QFrame(viewport());
        bar->setObjectName(QStringLiteral("RunModeBar"));
        bar->setFrameShape(QFrame::StyledPanel);
        bar->setAutoFillBackground(true);
        runBar_ = bar;
        auto* row = new QHBoxLayout(runBar_);
        row->setContentsMargins(12, 6, 8, 6);
        auto* text = new QLabel(tr("<b>Run mode:</b> click a switch to flip it. Parts can't be moved."), runBar_);
        text->setObjectName(QStringLiteral("RunModeText"));
        row->addWidget(help::withHelp(text, QStringLiteral("run.mode"), runBar_));
        auto* stop = new QPushButton(tr("Stop"), runBar_);
        stop->setObjectName(QStringLiteral("RunModeStop"));
        stop->setDefault(true);
        connect(stop, &QPushButton::clicked, this, [this] { setRunMode(false); });
        row->addWidget(stop);
    }
    if (runBar_) runBar_->setVisible(on);
    placeRunBar();
    emit runModeChanged(on);
}

QWidget* MapView::runTrainsButton() const { return runButton_; }

void MapView::placeRunBar() {
    const QRect vp = viewport()->rect();
    if (!runButton_) {
        runButton_ = new QToolButton(viewport());
        runButton_->setObjectName(QStringLiteral("RunTrainsButton"));
        runButton_->setText(tr("▶ Run trains"));
        runButton_->setToolTip(tr("Run trains: click a switch to flip it. Parts can't be moved."));
        runButton_->setAutoRaise(false);
        runButton_->setMinimumHeight(36);  // easy to find, and big enough for a finger
        runButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        runButton_->setCursor(Qt::PointingHandCursor);
        connect(runButton_, &QToolButton::clicked, this, [this] { setRunMode(true); });
    }
    // Building: the button; running: the banner, with Stop.
    runButton_->setVisible(!runMode_ && map_);
    runButton_->adjustSize();
    runButton_->move(vp.right() - runButton_->width() - 10, 10);
    runButton_->raise();
    if (runBar_ && runMode_) {
        runBar_->adjustSize();
        runBar_->move(vp.center().x() - runBar_->width() / 2, 10);
        runBar_->raise();
    }
}

void MapView::dropRunModePick() {
    if (runMode_ && scene() && !scene()->selectedItems().isEmpty()) scene()->clearSelection();
}

void MapView::setShowSwitches(bool on) {
    if (showSwitches_ == on) return;
    showSwitches_ = on;
    applySwitchesShown();
}

void MapView::applySwitchesShown() {
    if (!builder_) return;
    builder_->setShowSwitches(showSwitches_ || runMode_);
    if (map_) builder_->refreshSwitches(*map_);
}

std::optional<track::SwitchPoint> MapView::switchNear(QPointF studs, double reach) const {
    if (!map_) return std::nullopt;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const track::SwitchPoint* best = nullptr;
    double bestD = reach;
    for (const track::SwitchPoint& sw : net.switches())
        for (int exit : sw.exits) {
            const auto& pts = net.path(sw.brick, sw.heel, exit);
            if (pts.empty()) continue;
            const double len = std::min(kArrowStuds, pts.back().s * 0.6);
            for (double s : { 0.0, len / 2, len }) {
                const QPointF p = track::pointAt(pts, s).p;
                const double d = std::hypot(p.x() - studs.x(), p.y() - studs.y());
                if (d < bestD) {
                    bestD = d;
                    best = &sw;
                }
            }
        }
    return best ? std::optional<track::SwitchPoint>(*best) : std::nullopt;
}

void MapView::setSwitch(const QString& key, int exit) {
    if (!map_) return;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const track::SwitchPoint* sw = net.switchAt(key);
    if (!sw || !sw->exits.contains(exit)) return;
    QMap<QString, std::optional<int>> change;
    change.insert(key, exit == sw->defaultExit ? std::nullopt : std::optional<int>(exit));
    undoStack_->push(new edit::SetSwitchStatesCommand(*map_, change, tr("Set a switch")));
}

bool MapView::flipSwitchNear(QPointF studs) {
    if (!map_) return false;
    // Near enough: 4 studs, or 28 pixels on screen when zoomed out.
    const double reach = std::max(4.0, 28.0 / (transform().m11() * rendering::SceneBuilder::kPixelsPerStud));
    const auto sw = switchNear(studs, reach);
    if (!sw) {
        showStatus(tr("Click on a switch to flip it."), 3000);
        return false;
    }
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const int exit = track::nextExit(*sw, net.settingOf(*sw, map_->sidecar.switchStates));
    setSwitch(sw->key, exit);
    showStatus(tr("Switch set: %1").arg(legLabel(sw->legs.value(exit, QStringLiteral("straight")))), 3000);
    return true;
}

void MapView::addSwitchMenu(QMenu& menu, QPointF studs) {
    if (!map_) return;
    const double reach = std::max(3.0, 20.0 / (transform().m11() * rendering::SceneBuilder::kPixelsPerStud));
    const auto near = switchNear(studs, reach);
    if (!near) return;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    QVector<track::SwitchPoint> here;
    for (const auto& sw : net.switches())
        if (sw.brick == near->brick) here << sw;
    menu.addSection(tr("Set the switch"));
    for (qsizetype n = 0; n < here.size(); ++n) {
        const track::SwitchPoint& sw = here[n];
        QVector<int> exits = sw.exits;
        std::stable_sort(exits.begin(), exits.end(), [&](int a, int b) { return a == sw.defaultExit && b != sw.defaultExit; });
        const int set = net.settingOf(sw, map_->sidecar.switchStates);
        for (int exit : exits) {
            const QString leg = legLabel(sw.legs.value(exit, QStringLiteral("straight")));
            auto* a = menu.addAction(here.size() > 1 ? tr("From end %1: %2").arg(n + 1).arg(leg) : leg);
            a->setObjectName(QStringLiteral("switch_%1_%2").arg(sw.heel).arg(exit));
            a->setCheckable(true);
            a->setChecked(exit == set);
            connect(a, &QAction::triggered, this, [this, key = sw.key, exit] { setSwitch(key, exit); });
        }
    }
    menu.addSeparator();
}

}  // namespace bld::ui
