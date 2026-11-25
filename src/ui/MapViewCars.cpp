// Dragging cars along the track (Live Ops design 2.3; the web's
// render/CarDragLayer.tsx and CarBar.tsx): a car that stands on the track
// is dragged along it, with the cars on the side it goes to (an engine
// takes its whole train); where they would stop shows as it is dragged,
// and what they meet they couple to. A click on a car opens its menu: its
// load, and off the track. Each is one undo step.

#include "MapView.h"

#include "../core/Ids.h"
#include "../edit/CutCommands.h"
#include "../track/Cuts.h"
#include "TrainDialogs.h"

#include <QApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QUndoStack>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace bld::ui {

namespace {

// What a drag did (or why not), for the status line.
QString moveText(const track::CutMove& m, const core::Sidecar& sidecar) {
    const auto name = [&](const QString& id) {
        for (const auto& t : namedTrains(sidecar))
            if (t.id == id) return t.name.isEmpty() ? MapView::tr("the train") : t.name;
        return MapView::tr("the train");
    };
    if (!m.couples.isEmpty()) return MapView::tr("Coupled to %1.").arg(name(m.couples));
    if (m.why == QLatin1String("trains")) return MapView::tr("Stopped against %1: two trains don’t couple.").arg(name(m.blockedBy));
    if (m.why == QLatin1String("coupler")) return MapView::tr("Stopped against %1: there’s no coupler at that end.").arg(name(m.blockedBy));
    if (m.why == QLatin1String("full")) return MapView::tr("Stopped against %1: a train can have up to %2 cars.").arg(name(m.blockedBy)).arg(track::kMaxUnits);
    if (!m.blockedBy.isEmpty()) return MapView::tr("Stopped against %1.").arg(name(m.blockedBy));
    return {};
}

}  // namespace

// One drag: the car, where it was grabbed, and the layout it began on.
struct MapView::CarDrag {
    QString train;
    QString stock;
    int unit = 0;
    QPointF mid;   // the car's middle, studs
    QPointF from;  // where it was grabbed, studs
    QPoint press;  // on screen
    bool moved = false;
    std::vector<core::Train> trains;
    track::TrackNetwork net;
    std::optional<track::CutDrag> drag;
    std::optional<track::CutMove> latest;
};

std::optional<CarAt> MapView::carAt(QPointF studs) const {
    if (!map_ || map_->sidecar.trains.empty()) return std::nullopt;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const auto stock = track::stockById(map_->sidecar);
    std::optional<CarAt> found;
    for (const auto& t : track::standingTrains(map_->sidecar)) {
        const auto placed = track::placeTrain(net, t, stock, map_->sidecar.switchStates);
        if (!placed) continue;
        for (size_t i = 0; i < placed->units.size(); ++i) {
            const auto& u = placed->units[i];
            const core::RollingStock s = stock.value(u.stock);
            const double a = qDegreesToRadians(u.angle);
            const QPointF d = studs - u.centre;
            const double along = d.x() * std::cos(a) + d.y() * std::sin(a);
            const double across = -d.x() * std::sin(a) + d.y() * std::cos(a);
            const double len = std::max(s.bodyLength > 0 ? s.bodyLength : s.length - 2.0, 4.0);
            if (std::abs(along) <= len / 2 && std::abs(across) <= std::max(s.width, 4.0) / 2) found = CarAt{ t.id, u.stock, static_cast<int>(i), u.centre };
        }
    }
    return found;
}

bool MapView::carsMove() const { return map_ && !clicksTaken() && !runReadOnly_ && tool_ == Tool::Select; }

bool MapView::startCarDrag(QPointF studs, QPoint press) {
    if (!carsMove()) return false;
    const auto car = carAt(studs);
    if (!car) return false;
    carDrag_ = std::make_shared<CarDrag>();
    carDrag_->train = car->train;
    carDrag_->stock = car->stock;
    carDrag_->unit = car->unit;
    carDrag_->mid = car->middle;
    carDrag_->from = studs;
    carDrag_->press = press;
    return true;
}

void MapView::moveCarDrag(QPointF studs, QPoint at) {
    if (!carDrag_ || !map_) return;
    CarDrag& d = *carDrag_;
    if (!d.moved) {
        if ((at - d.press).manhattanLength() < QApplication::startDragDistance()) return;
        d.moved = true;
        d.trains = track::settleTrains(map_->sidecar.trains, track::stockById(map_->sidecar));
        d.net = track::trackNetworkOf(*map_, parts_);
        d.drag = track::startDrag(d.net, d.trains, track::stockById(map_->sidecar), map_->sidecar.switchStates, d.train, d.unit, core::newBbmId());
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
    // Someone else moved cars meanwhile: what the drag would write is out of date.
    if (d.trains != track::settleTrains(map_->sidecar.trains, track::stockById(map_->sidecar))) {
        cancelCarDrag();
        showStatus(tr("Someone else moved cars. Drag again."), 4000);
        return;
    }
    d.latest = d.drag ? d.drag->to(d.mid + studs - d.from) : std::nullopt;
    if (!builder_) return;
    std::vector<std::pair<core::Train, track::PlacedTrain>> ghost;
    if (d.latest)
        for (const auto& t : d.latest->change.trains) {
            if (std::none_of(t.units.begin(), t.units.end(), [&](const core::TrainUnit& u) { return d.latest->moved.contains(u.stock); })) continue;
            if (const auto p = track::placeTrain(d.net, t, track::stockById(map_->sidecar), map_->sidecar.switchStates)) ghost.emplace_back(t, *p);
        }
    builder_->showCarGhost(*map_, ghost);
}

void MapView::cancelCarDrag() {
    if (!carDrag_) return;
    carDrag_.reset();
    viewport()->unsetCursor();
    if (builder_) builder_->clearCarGhost();
}

void MapView::endCarDrag(QPoint globalPos) {
    if (!carDrag_) return;
    const auto d = carDrag_;
    if (d->moved && map_ && d->trains != track::settleTrains(map_->sidecar.trains, track::stockById(map_->sidecar))) {
        cancelCarDrag();
        showStatus(tr("Someone else moved cars. Drag again."), 4000);
        return;
    }
    cancelCarDrag();
    if (!map_) return;
    if (!d->moved) {
        showCarMenu(d->stock, globalPos);
        return;
    }
    if (!d->latest) return;
    const QString text = moveText(*d->latest, map_->sidecar);
    if (!d->latest->change.isEmpty())
        undoStack_->push(new edit::CutCommand(*map_, d->latest->change, d->latest->couples.isEmpty() ? tr("Move cars") : tr("Couple cars")));
    if (!text.isEmpty()) showStatus(text, 4000);
}

void MapView::setCarLoad(const QString& stockId, const QString& load) {
    if (!map_ || runReadOnly_) return;
    undoStack_->push(new edit::SetLoadCommand(*map_, stockId, load, tr("Change load")));
}

void MapView::takeCarOff(const QString& stockId) {
    if (!map_ || runReadOnly_) return;
    const auto stock = track::stockById(map_->sidecar);
    const auto trains = track::settleTrains(map_->sidecar.trains, stock);
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    undoStack_->push(new edit::CutCommand(*map_, track::takeOut(net, trains, stock, map_->sidecar.switchStates, stockId, core::newBbmId()),
                                          tr("Take car off the track")));
}

QMenu* MapView::makeCarMenu(const QString& stockId) {
    const auto stock = track::stockById(map_->sidecar);
    if (!stock.contains(stockId)) return nullptr;
    const core::RollingStock s = stock[stockId];
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("CarMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto trains = track::standingTrains(map_->sidecar);
    const core::Train* at = track::standingIn(stockId, trains);
    const int others = at ? static_cast<int>(at->units.size()) - 1 : 0;
    menu->addSection(stockLabel(s) + QStringLiteral(" · ") + (others == 0 ? tr("On its own") : tr("Coupled to %n other(s)", nullptr, others)));
    auto* load = menu->addMenu(tr("Load"));
    for (const auto& [key, label] : { std::pair{ QString(), tr("Doesn’t apply") }, std::pair{ QStringLiteral("empty"), tr("Empty") }, std::pair{ QStringLiteral("loaded"), tr("Loaded") } }) {
        auto* a = load->addAction(label, this, [this, stockId, key] { setCarLoad(stockId, key); });
        a->setObjectName(QStringLiteral("CarLoad.") + key);
        a->setCheckable(true);
        a->setChecked(s.opsLoad == key);
    }
    menu->addAction(tr("Take off the track"), this, [this, stockId] { takeCarOff(stockId); })->setObjectName(QStringLiteral("CarTakeOff"));
    menu->addSeparator();
    menu->addAction(tr("Open Trains"), this, &MapView::openTrainsRequested);
    return menu;
}

void MapView::showCarMenu(const QString& stockId, QPoint globalPos) {
    if (!map_) return;
    if (QMenu* menu = makeCarMenu(stockId)) menu->popup(globalPos);
}

}  // namespace bld::ui
