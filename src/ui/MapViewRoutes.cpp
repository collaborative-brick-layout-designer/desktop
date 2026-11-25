// Drawing a named route on the map (trains design 6.2): a click picks the
// place on the track nearest it, its start then its end (Shift-click adds a
// stop); the banner says what to click next, or how long the way found is,
// takes a name and saves the route as one undo step. The train never turns
// around on the spot: the shortest way either way is found, arrows show
// which way it runs, and Go the other way sends it round the other way. A
// start on a train's front is that train's route, the way it faces. The
// web's RouteDraftBanner (editor/EditorPage.tsx) does the same.

#include "TrainDialogs.h"
#include "MapView.h"

#include "../core/Ids.h"
#include "../track/Trains.h"
#include "../edit/SwitchCommands.h"
#include "../edit/venue/VenueUnits.h"
#include "../track/Routes.h"
#include "help/HelpButton.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

namespace bld::ui {

namespace {

const char* const kColors[] = { "#3B82F6", "#A855F7", "#14B8A6", "#F59E0B", "#EC4899", "#22C55E" };

}  // namespace

void MapView::startRouteDraft() {
    if (routeDraft_) return;
    if (runMode_) setRunMode(false);
    cancelTrainPlacing();
    cancelPlacePick();
    if (scene()) scene()->clearSelection();
    routeDraft_ = RouteDraft{};
    setInteractive(false);
    if (!routeBar_) {
        auto* bar = new QFrame(viewport());
        bar->setObjectName(QStringLiteral("RouteDraftBar"));
        bar->setFrameShape(QFrame::StyledPanel);
        bar->setAutoFillBackground(true);
        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(12, 6, 8, 6);
        routeBarText_ = new QLabel(bar);
        routeBarText_->setObjectName(QStringLiteral("RouteDraftText"));
        row->addWidget(help::withHelp(routeBarText_, QStringLiteral("routes.new"), bar));
        routeName_ = new QLineEdit(bar);
        routeName_->setObjectName(QStringLiteral("RouteDraftName"));
        routeName_->setAccessibleName(tr("Route name"));
        routeName_->setMinimumWidth(160);
        row->addWidget(routeName_);
        routeFlip_ = new QPushButton(tr("Go the other way"), bar);
        routeFlip_->setObjectName(QStringLiteral("RouteDraftFlip"));
        row->addWidget(routeFlip_);
        connect(routeFlip_, &QPushButton::clicked, this, &MapView::flipRouteDraft);
        routeSave_ = new QPushButton(tr("Save route"), bar);
        routeSave_->setObjectName(QStringLiteral("RouteDraftSave"));
        routeSave_->setDefault(true);
        row->addWidget(routeSave_);
        auto* cancel = new QPushButton(tr("Cancel"), bar);
        cancel->setObjectName(QStringLiteral("RouteDraftCancel"));
        row->addWidget(cancel);
        connect(routeSave_, &QPushButton::clicked, this, [this] { saveRouteDraft(routeName_->text()); });
        connect(routeName_, &QLineEdit::returnPressed, this, [this] { saveRouteDraft(routeName_->text()); });
        connect(cancel, &QPushButton::clicked, this, &MapView::cancelRouteDraft);
        routeBar_ = bar;
    }
    const int n = map_ ? static_cast<int>(map_->sidecar.routes.size()) : 0;
    routeName_->setText(tr("Route %1").arg(n + 1));
    routeBar_->show();
    refreshRouteDraft();
    emit routeDraftChanged(true);
}

void MapView::cancelRouteDraft() {
    if (!routeDraft_) return;
    routeDraft_.reset();
    setInteractive(!runMode_);
    if (routeBar_) routeBar_->hide();
    if (builder_) {
        builder_->setRouteDraft(std::nullopt);
        if (map_) builder_->refreshRoutes(*map_);
    }
    emit routeDraftChanged(false);
}

void MapView::routeDraftClick(QPointF studs, bool shift) {
    if (!routeDraft_ || !map_) return;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const double reach = std::max(3.0, 24.0 / (transform().m11() * rendering::SceneBuilder::kPixelsPerStud));
    const auto pos = track::nearestPosition(net, studs, reach);
    if (!pos) {
        showStatus(tr("Click on the track."), 3000);
        return;
    }
    RouteDraft& d = *routeDraft_;
    if (!d.from) {
        // A start on a train's front: the route is that train's, and it goes the way the train faces.
        track::StockById stock;
        for (const auto& s : map_->sidecar.rollingStock) stock.insert(s.id, s);
        const core::Train* onTrain = nullptr;
        core::TrainPosition head;
        const auto trains = namedTrains(map_->sidecar);
        for (const auto& t : trains) {
            if (!t.position || !track::placeTrain(net, t, stock, map_->sidecar.switchStates)) continue;
            const core::TrainPosition at = *t.position;
            const auto p = net.pointAtPosition(at);
            if (p && std::hypot(p->p.x() - studs.x(), p->p.y() - studs.y()) <= reach) {
                onTrain = &t;
                head = at;
                break;
            }
        }
        if (onTrain) {
            d.from = static_cast<const core::TrackPosition&>(head);
            d.facing = head.facingTo ? track::Facing::To : track::Facing::From;
            d.train = onTrain->id;
        } else {
            d.from = pos;
            d.facing.reset();
            d.train.clear();
        }
    } else if (shift && d.to) {
        d.via.push_back(*d.to);
        d.to = pos;
    } else if (shift) d.via.push_back(*pos);
    else d.to = pos;
    refreshRouteDraft();
}

void MapView::refreshRouteDraft() {
    if (!routeDraft_ || !routeBar_) return;
    const RouteDraft& d = *routeDraft_;
    std::optional<std::variant<track::RouteFound, track::RouteProblem>> found;
    rendering::SceneBuilder::RouteDraftLook look;
    if (map_) {
        const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
        if (d.from && d.to) {
            track::FindOptions opts;
            opts.via = d.via;
            opts.facing = d.facing;
            found = track::findRoute(net, *d.from, *d.to, opts);
            if (const auto* f = std::get_if<track::RouteFound>(&*found)) {
                core::Route r;
                r.from = *d.from;
                r.to = *d.to;
                r.steps = f->steps;
                look.lines = track::routeLines(net, r);
            }
        }
        for (const auto* p : { d.from ? &*d.from : nullptr, d.to ? &*d.to : nullptr })
            if (p)
                if (const auto at = net.pointAtPosition(*p)) look.ends.push_back(at->p);
        for (const auto& v : d.via)
            if (const auto at = net.pointAtPosition(v)) look.ends.push_back(at->p);
    }
    const auto* ok = found ? std::get_if<track::RouteFound>(&*found) : nullptr;
    QString trainName;
    if (!d.train.isEmpty())
        for (const auto& t : map_ ? namedTrains(map_->sidecar) : std::vector<core::Train>{})
            if (t.id == d.train) trainName = t.name.isEmpty() ? tr("the train") : t.name;
    const auto* problem = found ? std::get_if<track::RouteProblem>(&*found) : nullptr;
    QString text;
    if (!d.from) text = tr("<b>New route:</b> click on the track where it starts (or on a train’s front).");
    else if (!d.to)
        text = trainName.isEmpty()
                   ? tr("<b>New route:</b> click where it ends (Shift-click adds a stop on the way).")
                   : tr("<b>New route:</b> starts at %1, the way it faces. Click where it ends (Shift-click adds a stop on the way).").arg(trainName.toHtmlEscaped());
    else if (ok) {
        text = tr("<b>New route:</b> %1 along the track, the way the arrows point%2.")
                   .arg(edit::venue::formatLength(ok->length, edit::venue::accountLengthUnit()),
                        ok->turnsAround.isEmpty() ? QString() : tr(", and it turns the train around"));
        if (!ok->bothWays.isEmpty())
            text += QLatin1Char(' ') + tr("%n switch(es) on the way must be flipped while the train runs: it goes through them twice, set a different way each time.", nullptr,
                                          static_cast<int>(ok->bothWays.size()));
        text += QLatin1Char(' ') + tr("Shift-click adds a stop.");
    } else if (problem && *problem == track::RouteProblem::TurnAround && d.facing)
        text = tr("Going this way, the train can’t get there without turning around.");
    else
        text = track::problemText(problem ? *problem : track::RouteProblem::NoTrack);
    routeBarText_->setText(text);
    routeName_->setVisible(ok != nullptr);
    routeSave_->setVisible(ok != nullptr);
    // The other way: offered with a way found, or when the way picked can't get there.
    routeFlip_->setVisible(d.from && d.to && (ok || (problem && *problem == track::RouteProblem::TurnAround && d.facing)));
    routeBar_->adjustSize();
    routeBar_->move(viewport()->rect().center().x() - routeBar_->width() / 2, 10);
    routeBar_->raise();
    if (builder_ && map_) {
        builder_->setRouteDraft(look);
        builder_->refreshRoutes(*map_);
    }
}

void MapView::flipRouteDraft() {
    if (!routeDraft_ || !map_ || !routeDraft_->from || !routeDraft_->to) return;
    RouteDraft& d = *routeDraft_;
    // The way it runs now: as picked, or the shortest found.
    std::optional<track::Facing> runs = d.facing;
    if (!runs) {
        const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
        track::FindOptions opts;
        opts.via = d.via;
        const auto found = track::findRoute(net, *d.from, *d.to, opts);
        if (const auto* f = std::get_if<track::RouteFound>(&found)) runs = f->facing;
    }
    if (!runs) return;
    d.facing = *runs == track::Facing::From ? track::Facing::To : track::Facing::From;
    refreshRouteDraft();
}

bool MapView::saveRouteDraft(const QString& name) {
    if (!routeDraft_ || !map_ || !routeDraft_->from || !routeDraft_->to) return false;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    track::FindOptions opts;
    opts.via = routeDraft_->via;
    opts.facing = routeDraft_->facing;
    const auto found = track::findRoute(net, *routeDraft_->from, *routeDraft_->to, opts);
    const auto* f = std::get_if<track::RouteFound>(&found);
    if (!f) return false;
    core::Route r;
    r.id = core::newBbmId();
    const int n = static_cast<int>(map_->sidecar.routes.size());
    r.name = name.trimmed().isEmpty() ? tr("Route %1").arg(n + 1) : name.trimmed();
    r.train = routeDraft_->train;
    r.from = *routeDraft_->from;
    r.to = *routeDraft_->to;
    for (const auto& v : routeDraft_->via) r.via.push_back({ v, false });
    r.steps = f->steps;
    QStringList used;
    for (const auto& x : map_->sidecar.routes) used << x.color;
    r.color = QString::fromLatin1(kColors[n % 6]);
    for (const char* c : kColors)
        if (!used.contains(QString::fromLatin1(c))) {
            r.color = QString::fromLatin1(c);
            break;
        }
    const QString id = r.id;
    const QString shown = r.name;
    cancelRouteDraft();
    undoStack_->push(new edit::SetRouteCommand(*map_, id, r, tr("New route")));
    showStatus(tr("Route “%1” saved.").arg(shown), 3000);
    return true;
}

void MapView::setHiddenRoutes(const QSet<QString>& ids) {
    if (!builder_) return;
    builder_->setHiddenRoutes(ids);
    if (map_) builder_->refreshRoutes(*map_);
}

}  // namespace bld::ui
