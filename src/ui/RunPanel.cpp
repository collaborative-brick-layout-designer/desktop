#include "RunPanel.h"

#include "RunSpeeds.h"
#include "../core/Map.h"
#include "help/HelpButton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {

QString trainName(const core::Map& map, const QString& id) {
    for (const auto& t : map.sidecar.trains)
        if (t.id == id) return t.name.isEmpty() ? RunPanel::tr("Train") : t.name;
    return RunPanel::tr("A train");
}

QString routeName(const core::Map& map, const QHash<QString, std::optional<RunPick>>& picks, const QString& trainId) {
    const auto p = picks.value(trainId);
    if (!p || p->route.isEmpty()) return {};
    for (const auto& r : map.sidecar.routes)
        if (r.id == p->route) return r.name;
    return {};
}

void clearLayout(QVBoxLayout* box) {
    while (QLayoutItem* it = box->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
}

}  // namespace

QString RunPanel::noteText(const track::RunNote& n, const core::Map& map, const QHash<QString, std::optional<RunPick>>& picks) {
    const QString t = trainName(map, n.train);
    const QString route = routeName(map, picks, n.train);
    switch (n.kind) {
    case track::RunNoteKind::Collision:
        return tr("%1 and %2 would collide here, so both stopped.").arg(t, n.other.isEmpty() ? tr("another train") : trainName(map, n.other));
    case track::RunNoteKind::PushedThrough:
        return tr("%1 pushed through a switch that was set the other way. It flipped for this run.").arg(t);
    case track::RunNoteKind::RouteSet:
        return tr("A switch was set the other way for %1; it flipped for this run.").arg(route.isEmpty() ? tr("its route") : route);
    case track::RunNoteKind::Arrived:
        return tr("%1 reached the end of %2.").arg(t, route.isEmpty() ? tr("its route") : route);
    case track::RunNoteKind::EndOfTrack:
        return tr("%1 reached the end of the track.").arg(t);
    }
    return {};
}

RunPanel::RunPanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("RunPanel"));
    setAccessibleName(tr("Run trains"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setMinimumWidth(300);
    setMaximumWidth(340);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 6, 8, 8);
    v->setSpacing(6);

    auto* top = new QHBoxLayout;
    play_ = new QPushButton(tr("▶ Play"), this);
    play_->setObjectName(QStringLiteral("RunPlay"));
    play_->setDefault(true);
    play_->setMinimumWidth(84);
    play_->setToolTip(tr("Start every ticked train"));
    connect(play_, &QPushButton::clicked, this, [this] { emit playToggled(!playing_); });
    top->addWidget(play_);
    restart_ = new QPushButton(tr("↺ Restart"), this);
    restart_->setObjectName(QStringLiteral("RunRestart"));
    restart_->setToolTip(tr("Every train back to where it started"));
    connect(restart_, &QPushButton::clicked, this, &RunPanel::restartRequested);
    top->addWidget(restart_);
    mult_ = new QComboBox(this);
    mult_->setObjectName(QStringLiteral("RunMultiplier"));
    mult_->setAccessibleName(tr("How fast to watch"));
    mult_->setToolTip(tr("How fast to watch: times real speed"));
    const QStringList names{ QStringLiteral("¼×"), QStringLiteral("½×"), QStringLiteral("1×"), QStringLiteral("2×"), QStringLiteral("4×") };
    for (size_t i = 0; i < track::speedMultipliers().size(); ++i) mult_->addItem(names[static_cast<qsizetype>(i)], track::speedMultipliers()[i]);
    connect(mult_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!filling_ && i >= 0) emit multiplierChanged(mult_->itemData(i).toDouble());
    });
    top->addWidget(mult_);
    top->addWidget(new help::HelpButton(QStringLiteral("run.trains"), this));
    top->addStretch(1);
    fold_ = new QToolButton(this);
    fold_->setObjectName(QStringLiteral("RunFold"));
    fold_->setText(QStringLiteral("▾"));
    fold_->setAutoRaise(true);
    fold_->setToolTip(tr("Show less"));
    connect(fold_, &QToolButton::clicked, this, [this] {
        body_->setVisible(!body_->isVisible());
        fold_->setText(body_->isVisible() ? QStringLiteral("▾") : QStringLiteral("▸"));
        fold_->setToolTip(body_->isVisible() ? tr("Show less") : tr("Show the trains"));
        adjustSize();
    });
    top->addWidget(fold_);
    v->addLayout(top);

    body_ = new QWidget(this);
    auto* bv = new QVBoxLayout(body_);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(6);
    empty_ = new QLabel(tr("There are no trains on this layout yet."), body_);
    empty_->setWordWrap(true);
    bv->addWidget(empty_);
    openTrains_ = new QPushButton(tr("Open Trains"), body_);
    openTrains_->setObjectName(QStringLiteral("RunOpenTrains"));
    connect(openTrains_, &QPushButton::clicked, this, &RunPanel::openTrainsRequested);
    bv->addWidget(openTrains_, 0, Qt::AlignLeft);
    trainsBox_ = new QVBoxLayout;
    trainsBox_->setSpacing(4);
    bv->addLayout(trainsBox_);
    finished_ = new QLabel(tr("Every train has stopped. Restart puts them back where they started."), body_);
    finished_->setObjectName(QStringLiteral("RunFinished"));
    finished_->setWordWrap(true);
    bv->addWidget(finished_);
    notesBox_ = new QVBoxLayout;
    notesBox_->setSpacing(3);
    bv->addLayout(notesBox_);

    auto* bottom = new QHBoxLayout;
    auto* scaleLabel = new QLabel(tr("Scale 1:"), body_);
    bottom->addWidget(scaleLabel);
    scale_ = new QSpinBox(body_);
    scale_->setObjectName(QStringLiteral("RunScale"));
    scale_->setRange(track::kMinScale, track::kMaxScale);
    scale_->setAccessibleName(tr("Scale"));
    scale_->setKeyboardTracking(false);
    connect(scale_, &QSpinBox::valueChanged, this, [this](int v) {
        if (!filling_) emit scaleChanged(v);
    });
    bottom->addWidget(scale_);
    bottom->addWidget(new help::HelpButton(QStringLiteral("trains.scale"), body_));
    bottom->addSpacing(8);
    real_ = new QCheckBox(tr("Real speed"), body_);
    real_->setObjectName(QStringLiteral("RunRealSpeed"));
    connect(real_, &QCheckBox::toggled, this, [this](bool on) {
        if (!filling_) emit realSpeedChanged(on);
    });
    bottom->addWidget(real_);
    bottom->addStretch(1);
    bv->addLayout(bottom);
    v->addWidget(body_);
}

void RunPanel::show(const core::Map& map, const RunView& view) {
    filling_ = true;
    playing_ = view.playing;
    play_->setText(view.playing ? tr("❚❚ Pause") : tr("▶ Play"));
    play_->setToolTip(view.playing ? tr("Pause every train") : tr("Start every ticked train"));
    const bool anyOn = std::any_of(view.picks.begin(), view.picks.end(), [](const std::optional<RunPick>& p) { return p && p->on; });
    play_->setEnabled(anyOn || view.playing);
    restart_->setEnabled(view.hasRun);
    for (int i = 0; i < mult_->count(); ++i)
        if (qFuzzyCompare(mult_->itemData(i).toDouble(), view.multiplier)) mult_->setCurrentIndex(i);
    scale_->setValue(view.scale);
    scale_->setEnabled(!view.readOnly);
    real_->setChecked(view.realSpeed);

    const auto& trains = map.sidecar.trains;
    empty_->setVisible(trains.empty());
    openTrains_->setVisible(trains.empty());
    clearLayout(trainsBox_);
    rows_.clear();
    for (const auto& t : trains) {
        const std::optional<RunPick> p = view.picks.value(t.id);
        auto* row = new QFrame(body_);
        row->setObjectName(QStringLiteral("RunTrain_%1").arg(t.id));
        row->setFrameShape(QFrame::StyledPanel);
        auto* rv = new QVBoxLayout(row);
        rv->setContentsMargins(6, 4, 6, 4);
        rv->setSpacing(3);
        auto* head = new QHBoxLayout;
        auto* on = new QCheckBox(t.name.isEmpty() ? tr("Train") : t.name, row);
        on->setObjectName(QStringLiteral("RunOn"));
        on->setAccessibleName(tr("Run %1").arg(t.name));
        on->setChecked(p && p->on);
        on->setEnabled(p.has_value());
        on->setStyleSheet(QStringLiteral("QCheckBox { border-left: 6px solid %1; padding-left: 4px; }").arg(QColor(t.color).name()));
        connect(on, &QCheckBox::toggled, this, [this, id = t.id, p](bool checked) {
            if (filling_ || !p) return;
            RunPick next = *p;
            next.on = checked;
            emit pickChanged(id, next);
        });
        head->addWidget(on, 1);
        const double kmh = t.speed > 0 ? t.speed : track::kDefaultSpeed;
        auto* speed = new QLabel(view.realSpeed ? RunSpeeds::realSpeed(kmh, view.scale) : RunSpeeds::scaleSpeed(kmh), row);
        speed->setObjectName(QStringLiteral("RunSpeed"));
        head->addWidget(speed);
        rv->addLayout(head);
        if (!p) {
            auto* why = new QLabel(tr("Put it on the track, or draw a route for it, to run it."), row);
            why->setWordWrap(true);
            rv->addWidget(why);
        } else {
            auto* where = new QComboBox(row);
            where->setObjectName(QStringLiteral("RunRoute"));
            where->setAccessibleName(tr("Where %1 runs").arg(t.name));
            if (view.onTrack.contains(t.id)) where->addItem(tr("From where it stands, following the switches"), QString());
            for (const auto& r : map.sidecar.routes)
                if (!view.brokenRoutes.contains(r.id)) where->addItem(tr("Route: %1").arg(r.name), r.id);
            where->setCurrentIndex(std::max(0, where->findData(p.value().route)));
            connect(where, &QComboBox::currentIndexChanged, this, [this, id = t.id, where](int i) {
                if (filling_ || i < 0) return;
                emit pickChanged(id, RunPick{ true, where->itemData(i).toString() });
            });
            rv->addWidget(where);
        }
        if (view.states.contains(t.id)) {
            const track::RunState st = view.states.value(t.id);
            const QString text = st == track::RunState::Running      ? tr("Running")
                                 : st == track::RunState::Arrived    ? tr("At the end of its route")
                                 : st == track::RunState::EndOfTrack ? tr("At the end of the track")
                                                                     : tr("Stopped: another train is in the way");
            auto* state = new QLabel(text, row);
            state->setObjectName(QStringLiteral("RunState"));
            state->setProperty("state", track::runStateName(st));
            if (st == track::RunState::Blocked) state->setStyleSheet(QStringLiteral("color: #B91C1C; font-weight: 600;"));
            rv->addWidget(state);
        }
        trainsBox_->addWidget(row);
        rows_.insert(t.id, row);
    }
    const bool finished = view.hasRun && !view.playing && !view.states.isEmpty() &&
                          std::none_of(view.states.begin(), view.states.end(), [](track::RunState s) { return s == track::RunState::Running; });
    finished_->setVisible(finished);

    clearLayout(notesBox_);
    noteRows_.clear();
    for (auto it = view.notes.rbegin(); it != view.notes.rend(); ++it) {
        const track::RunNote& n = it->note;
        auto* row = new QFrame(body_);
        row->setObjectName(QStringLiteral("RunNote"));
        row->setProperty("kind", track::runNoteKindName(n.kind));
        const bool bad = n.kind == track::RunNoteKind::Collision;
        const bool warn = n.kind == track::RunNoteKind::PushedThrough || n.kind == track::RunNoteKind::RouteSet;
        row->setStyleSheet(bad ? QStringLiteral("QFrame#RunNote { background: #FEE2E2; color: #7F1D1D; border-radius: 4px; }")
                           : warn ? QStringLiteral("QFrame#RunNote { background: #FEF3C7; color: #78350F; border-radius: 4px; }")
                                  : QString());
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(6, 3, 4, 3);
        auto* text = new QLabel(noteText(n, map, view.picks), row);
        text->setWordWrap(true);
        h->addWidget(text, 1);
        if (n.kind == track::RunNoteKind::PushedThrough) h->addWidget(new help::HelpButton(QStringLiteral("run.switchPushed"), row));
        auto* show = new QPushButton(tr("Show me"), row);
        show->setObjectName(QStringLiteral("RunShowMe"));
        connect(show, &QPushButton::clicked, this, [this, at = n.at] { emit showSpotRequested(at); });
        h->addWidget(show);
        notesBox_->addWidget(row);
        noteRows_ << row;
    }
    filling_ = false;
    adjustSize();
}

}  // namespace bld::ui
