// Running trains in Run mode (trains design 5): the Run panel in the map's
// corner, Play and Pause, Restart, the speed choice, which trains run and on
// which route, and each frame's cars. Running is this person's own: nothing
// here writes to the layout, so it makes no undo steps and nobody else's
// map moves. A switch a train pushes through flips for this run only; a
// click on a switch is a real, shared flip (undoable), as in Run mode
// before. The layout's scale (1:38 unless changed) is shared: it is a
// setting of the layout. The web's editor/trainRun.ts does the same.

#include "TrainDialogs.h"
#include "../track/Cuts.h"
#include "MapView.h"

#include "RunPanel.h"
#include "../edit/TrainCommands.h"
#include "../track/Routes.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

namespace bld::ui {

namespace {

constexpr int kFrameMs = 16;  // about 60 a second

track::StockById stockOf(const core::Map& map) {
    track::StockById out;
    for (const auto& s : map.sidecar.rollingStock) out.insert(s.id, s);
    return out;
}

}  // namespace

int MapView::trainScale() const {
    if (!map_) return track::kDefaultScale;
    const std::optional<int>& s = map_->sidecar.trainScale;
    return track::asScale(s.has_value() ? std::optional<double>(s.value()) : std::nullopt);
}

QHash<QString, std::optional<RunPick>> MapView::runPicksNow(QSet<QString>* onTrack, QSet<QString>* broken) const {
    QHash<QString, std::optional<RunPick>> out;
    if (!map_) return out;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const auto stock = stockOf(*map_);
    QSet<QString> standing;
    for (const auto& t : runnableTrains(map_->sidecar))
        if (t.position && track::placeTrain(net, t, stock, map_->sidecar.switchStates)) standing.insert(t.id);
    QSet<QString> bad;
    for (const auto& r : map_->sidecar.routes)
        if (track::routeStatus(net, r, map_->sidecar.switchStates).broken) bad.insert(r.id);
    const auto usable = [&](const QString& route) {
        return std::any_of(map_->sidecar.routes.begin(), map_->sidecar.routes.end(), [&](const core::Route& r) { return r.id == route && !bad.contains(r.id); });
    };
    for (const auto& t : runnableTrains(map_->sidecar)) {
        if (t.units.empty()) {
            out.insert(t.id, std::nullopt);
            continue;
        }
        const bool on = standing.contains(t.id);
        const auto chosen = runPicks_.constFind(t.id);
        if (chosen != runPicks_.constEnd() && (chosen->route.isEmpty() ? on : usable(chosen->route))) {
            out.insert(t.id, *chosen);
            continue;
        }
        // Nobody picked: its own route (one made for it), else from where it stands.
        std::optional<RunPick> p;
        for (const auto& r : map_->sidecar.routes)
            if (r.train == t.id && !bad.contains(r.id)) {
                p = RunPick{ true, r.id };
                break;
            }
        if (!p && on) p = RunPick{ true, QString() };
        out.insert(t.id, p);
    }
    if (onTrack) *onTrack = standing;
    if (broken) *broken = bad;
    return out;
}

void MapView::refreshRunPanel() {
    if (!runMode_ || !map_) {
        if (runPanel_) runPanel_->hide();
        return;
    }
    if (!runPanel_) {
        runPanel_ = new RunPanel(viewport());
        connect(runPanel_, &RunPanel::playToggled, this, &MapView::playRun);
        connect(runPanel_, &RunPanel::restartRequested, this, &MapView::restartRun);
        connect(runPanel_, &RunPanel::multiplierChanged, this, &MapView::setRunMultiplier);
        connect(runPanel_, &RunPanel::realSpeedChanged, this, &MapView::setRunRealSpeed);
        connect(runPanel_, &RunPanel::pickChanged, this, &MapView::setRunPick);
        connect(runPanel_, &RunPanel::scaleChanged, this, &MapView::setTrainScale);
        connect(runPanel_, &RunPanel::showSpotRequested, this, &MapView::showSpot);
        connect(runPanel_, &RunPanel::openTrainsRequested, this, &MapView::openTrainsRequested);
        QSettings s;
        runMultiplier_ = s.value(QStringLiteral("run/multiplier"), 1.0).toDouble();
        if (std::find(track::speedMultipliers().begin(), track::speedMultipliers().end(), runMultiplier_) == track::speedMultipliers().end())
            runMultiplier_ = 1.0;
        runRealSpeed_ = s.value(QStringLiteral("run/realSpeed"), false).toBool();
    }
    RunView v;
    v.playing = runPlaying_;
    v.hasRun = run_ != nullptr;
    v.multiplier = runMultiplier_;
    v.realSpeed = runRealSpeed_;
    v.scale = trainScale();
    v.readOnly = runReadOnly_;
    v.picks = runPicksNow(&v.onTrack, &v.brokenRoutes);
    if (run_)
        for (const QString& id : run_->trainIds())
            if (const auto st = run_->stateOf(id)) v.states.insert(id, st.value());
    v.notes = runNotes_;
    runPanel_->show(*map_, v);
    runPanel_->setVisible(true);
    placeRunPanel();
}

void MapView::placeRunPanel() {
    if (!runPanel_ || !runPanel_->isVisible()) return;
    const QRect vp = viewport()->rect();
    runPanel_->adjustSize();
    const int h = std::min(runPanel_->sizeHint().height(), vp.height() * 6 / 10);
    runPanel_->resize(runPanel_->width(), h);
    // Over the scale bar's corner, a little up so the bar still shows.
    runPanel_->move(10, std::max(10, vp.bottom() - h - 44));
    runPanel_->raise();
}

void MapView::endRun() {
    if (runTimer_) runTimer_->stop();
    run_.reset();
    runPlaying_ = false;
    runNotes_.clear();
    if (builder_ && map_) {
        builder_->clearRun();
        builder_->setRunningTrains({}, *map_);
        builder_->setSwitchStatesShown(std::nullopt);
        builder_->refreshSwitches(*map_);
    }
}

void MapView::playRun(bool play) {
    if (!runMode_ || !map_) return;
    if (!play) {
        runPlaying_ = false;
        if (runTimer_) runTimer_->stop();
        refreshRunPanel();
        return;
    }
    if (!run_) {
        const auto picks = runPicksNow();
        std::vector<track::RunPlan> plans;
        QSet<QString> ids;
        for (const auto& t : runnableTrains(map_->sidecar)) {
            const auto p = picks.value(t.id);
            if (!p.has_value() || !p.value().on) continue;
            const QString routeId = p.value().route;
            track::RunPlan plan;
            plan.train = t;
            if (!routeId.isEmpty())
                for (const auto& r : map_->sidecar.routes)
                    if (r.id == routeId) plan.route = r;
            plans.push_back(plan);
            ids.insert(t.id);
        }
        if (plans.empty()) return;
        const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
        const auto stock = stockOf(*map_);
        track::TrainRun::Parked parked;
        for (const auto& t : track::standingTrains(map_->sidecar))
            if (!ids.contains(t.id) && t.position) parked.push_back({ t.id, track::placeTrain(net, t, stock, map_->sidecar.switchStates) });
        run_ = std::make_unique<track::TrainRun>(net, stock, plans, map_->sidecar.switchStates, trainScale(), parked);
        for (const auto& n : run_->startNotes()) runNotes_.push_back({ ++runNoteId_, n });
        if (builder_) builder_->setRunningTrains(QSet<QString>(ids.begin(), ids.end()), *map_);
        showRunFrame();
    } else {
        run_->resume();
        // Every train is at its end: Play runs them again from the start.
        if (!run_->moving()) {
            endRun();
            return playRun(true);
        }
    }
    runPlaying_ = run_->moving();
    if (runPlaying_) {
        if (!runTimer_) {
            runTimer_ = new QTimer(this);
            runTimer_->setTimerType(Qt::PreciseTimer);
            runTimer_->setInterval(kFrameMs);
            connect(runTimer_, &QTimer::timeout, this, [this] {
                const double dt = std::min(0.1, runClock_.restart() / 1000.0);
                stepRun(dt * runMultiplier_);
            });
        }
        runClock_.start();
        runTimer_->start();
    }
    refreshRunPanel();
}

void MapView::stepRun(double seconds) {
    if (!run_ || !map_) return;
    const auto notes = run_->step(seconds);
    if (!run_->moving()) {
        runPlaying_ = false;
        if (runTimer_) runTimer_->stop();
    }
    if (!notes.empty() || !runPlaying_) {
        for (const auto& n : notes) {
            // A collision already shown between the same two trains at the same spot (Play again) isn't said twice.
            const bool same = n.kind == track::RunNoteKind::Collision &&
                              std::any_of(runNotes_.begin(), runNotes_.end(), [&](const ShownNote& o) {
                                  return o.note.kind == track::RunNoteKind::Collision &&
                                         ((o.note.train == n.train && o.note.other == n.other) || (o.note.train == n.other && o.note.other == n.train)) &&
                                         std::hypot(o.note.at.x() - n.at.x(), o.note.at.y() - n.at.y()) < 2.0;
                              });
            if (!same) runNotes_.push_back({ ++runNoteId_, n });
        }
        if (runNotes_.size() > 20) runNotes_.erase(runNotes_.begin(), runNotes_.end() - 20);
        const bool flipped = std::any_of(notes.begin(), notes.end(), [](const track::RunNote& n) {
            return n.kind == track::RunNoteKind::PushedThrough || n.kind == track::RunNoteKind::RouteSet;
        });
        if (flipped && builder_) {
            builder_->setSwitchStatesShown(run_->states());
            builder_->refreshSwitches(*map_);
        }
        refreshRunPanel();
    }
    // (After the notes: a collision's ring goes up with it.)
    showRunFrame();
}

void MapView::showRunFrame() {
    if (!run_ || !builder_ || !map_) return;
    std::vector<std::pair<QString, std::optional<track::PlacedTrain>>> placed;
    for (const QString& id : run_->trainIds()) placed.push_back({ id, run_->placedOf(id) });
    // A collision is marked while the train it stopped is still standing there.
    std::vector<QPointF> marks;
    for (const auto& n : runNotes_)
        if (n.note.kind == track::RunNoteKind::Collision && run_->stateOf(n.note.train) == track::RunState::Blocked) marks.push_back(n.note.at);
    builder_->showRunFrame(*map_, placed, marks);
    viewport()->update();
}

void MapView::restartRun() {
    endRun();
    refreshRunPanel();
}

void MapView::setRunPick(const QString& trainId, const RunPick& pick) {
    runPicks_.insert(trainId, pick);
    // Which trains run changed: they start again from the beginning.
    endRun();
    refreshRunPanel();
}

void MapView::setRunMultiplier(double m) {
    runMultiplier_ = m;
    QSettings().setValue(QStringLiteral("run/multiplier"), m);
}

void MapView::setRunRealSpeed(bool on) {
    runRealSpeed_ = on;
    QSettings().setValue(QStringLiteral("run/realSpeed"), on);
    refreshRunPanel();
}

void MapView::setTrainScale(int scale) {
    if (!map_ || runReadOnly_ || scale == trainScale()) return;
    undoStack_->push(new edit::SetTrainScaleCommand(*map_, scale, tr("Set the train scale")));
    // Speeds changed: start again from the beginning.
    endRun();
    refreshRunPanel();
}

void MapView::runMapChanged() {
    if (!runMode_ || !map_) return;
    if (run_) {
        run_->setLayoutStates(map_->sidecar.switchStates);
        if (builder_) {
            builder_->setSwitchStatesShown(run_->states());
            builder_->refreshSwitches(*map_);
        }
        showRunFrame();
    }
    if (trainScale() != runScaleSeen_ && run_) endRun();
    runScaleSeen_ = trainScale();
    refreshRunPanel();
}

}  // namespace bld::ui
