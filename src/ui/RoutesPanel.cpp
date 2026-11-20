#include "RoutesPanel.h"

#include "../core/Map.h"
#include "../edit/venue/VenueUnits.h"
#include "../track/Routes.h"
#include "help/HelpButton.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace bld::ui {

RoutesPanel::RoutesPanel(parts::PartsLibrary& parts, QWidget* parent) : QDockWidget(tr("Routes"), parent), parts_(parts) {
    setObjectName(QStringLiteral("dock.routes"));
    auto* body = new QWidget(this);
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(8, 8, 8, 8);
    auto* top = new QHBoxLayout;
    newButton_ = new QPushButton(tr("New route"), body);
    newButton_->setObjectName(QStringLiteral("routesNew"));
    connect(newButton_, &QPushButton::clicked, this, &RoutesPanel::newRouteRequested);
    top->addWidget(help::withHelp(newButton_, QStringLiteral("routes.new"), body));
    top->addStretch(1);
    v->addLayout(top);
    message_ = new QLabel(body);
    message_->setObjectName(QStringLiteral("routesMessage"));
    message_->setWordWrap(true);
    message_->hide();
    v->addWidget(message_);
    empty_ = new QLabel(body);
    empty_->setObjectName(QStringLiteral("routesEmpty"));
    empty_->setWordWrap(true);
    v->addWidget(empty_);
    auto* scroll = new QScrollArea(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    list_ = new QVBoxLayout(inner);
    list_->setContentsMargins(0, 0, 0, 0);
    list_->addStretch(1);
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);
    setWidget(body);
    confirm_ = [this](const QString& name) {
        return QMessageBox::question(this, tr("Delete route"), tr("Delete the route “%1”?").arg(name)) == QMessageBox::Yes;
    };
    askName_ = [this](const QString& name) -> std::optional<QString> {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename route"), tr("Name:"), QLineEdit::Normal, name, &ok);
        if (!ok || n.trimmed().isEmpty()) return std::nullopt;
        return n.trimmed();
    };
    rebuild();
}

void RoutesPanel::setMap(const core::Map* map) {
    map_ = map;
    rebuild();
}

void RoutesPanel::setHidden(const QSet<QString>& ids) {
    hidden_ = ids;
    rebuild();
}

void RoutesPanel::setDrawing(bool on) {
    drawing_ = on;
    rebuild();
}

QString RoutesPanel::message() const { return message_->isHidden() ? QString() : message_->text(); }

const core::Route* RoutesPanel::find(const QString& id) const {
    if (!map_) return nullptr;
    for (const auto& r : map_->sidecar.routes)
        if (r.id == id) return &r;
    return nullptr;
}

void RoutesPanel::setShown(const QString& id, bool shown) {
    if (shown) {
        hidden_.remove(id);
    } else {
        hidden_.insert(id);
    }
    emit hiddenChanged(hidden_);
    rebuild();
}

void RoutesPanel::setSwitches(const QString& id) {
    const core::Route* r = find(id);
    if (!r || !map_) return;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const track::RouteStatus st = track::routeStatus(net, *r, map_->sidecar.switchStates);
    if (st.broken || st.switches.isEmpty()) return;
    QMap<QString, std::optional<int>> changes;
    for (auto it = st.switches.constBegin(); it != st.switches.constEnd(); ++it) {
        const track::SwitchPoint* sw = net.switchAt(it.key());
        changes.insert(it.key(), sw && sw->defaultExit == it.value() ? std::nullopt : std::optional<int>(it.value()));
    }
    message_->setText(tr("Switches set for “%1”.").arg(r->name));
    message_->show();
    emit switchesRequested(changes, tr("Set switches for a route"));
}

bool RoutesPanel::findNewWay(const QString& id) {
    const core::Route* r = find(id);
    if (!r || !map_) return false;
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    track::FindOptions opts;
    for (const auto& v : r->via) opts.via.push_back(v.at);
    const auto found = track::findRoute(net, r->from, r->to, opts);
    if (const auto* p = std::get_if<track::RouteProblem>(&found)) {
        message_->setText(track::problemText(*p) + QLatin1Char(' ') + tr("Draw the route again from its new start or end."));
        message_->show();
        return false;
    }
    core::Route after = *r;
    after.steps = std::get<track::RouteFound>(found).steps;
    message_->setText(tr("Found a new way for “%1”.").arg(r->name));
    message_->show();
    emit routeEdited(id, after, tr("Find a new way"));
    return true;
}

void RoutesPanel::rename(const QString& id, const QString& name) {
    const core::Route* r = find(id);
    if (!r || name.trimmed().isEmpty()) return;
    core::Route after = *r;
    after.name = name.trimmed();
    emit routeEdited(id, after, tr("Rename route"));
}

bool RoutesPanel::remove(const QString& id) {
    const core::Route* r = find(id);
    if (!r || !confirm_(r->name)) return false;
    emit routeEdited(id, std::nullopt, tr("Delete route"));
    return true;
}

void RoutesPanel::rebuild() {
    while (list_->count() > 1) {
        QLayoutItem* it = list_->takeAt(0);
        // Later (a row's own button may be what changed the routes), and out
        // of the panel now, so nothing finds the old row meanwhile.
        if (QWidget* w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    const track::TrackNetwork net = map_ ? track::trackNetworkOf(*map_, parts_) : track::TrackNetwork();
    const bool hasTrack = !net.brickIds().isEmpty();
    newButton_->setEnabled(hasTrack && !drawing_);
    const bool none = !map_ || map_->sidecar.routes.empty();
    empty_->setVisible(!hasTrack || none);
    empty_->setText(!hasTrack ? tr("There’s no track on this layout yet. Add some from the Parts panel’s Track group, then draw a route along it.")
                              : tr("No routes yet. Press New route, then click two places on the track: where it starts and where it ends."));
    if (!map_) return;
    const auto unit = edit::venue::accountLengthUnit();
    int at = 0;
    for (const auto& r : map_->sidecar.routes) {
        const track::RouteStatus st = track::routeStatus(net, r, map_->sidecar.switchStates);
        auto* row = new QFrame;
        row->setObjectName(QStringLiteral("route_") + r.id);
        row->setFrameShape(QFrame::StyledPanel);
        auto* v = new QVBoxLayout(row);
        v->setContentsMargins(8, 6, 8, 6);
        auto* head = new QHBoxLayout;
        auto* dot = new QLabel(row);
        dot->setFixedSize(12, 12);
        dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 6px;").arg(r.color));
        head->addWidget(dot);
        auto* name = new QLabel(r.name.isEmpty() ? tr("Untitled route") : r.name, row);
        name->setObjectName(QStringLiteral("routeName"));
        name->setStyleSheet(QStringLiteral("font-weight: 600;"));
        head->addWidget(name, 1);
        const bool shown = !hidden_.contains(r.id);
        auto* eye = new QToolButton(row);
        eye->setObjectName(QStringLiteral("routeEye"));
        eye->setCheckable(true);
        eye->setChecked(shown);
        eye->setText(shown ? tr("Shown") : tr("Hidden"));
        eye->setToolTip(shown ? tr("Shown on your map (only yours)") : tr("Hidden on your map (only yours)"));
        eye->setAccessibleName(shown ? tr("Hide %1 on your map").arg(r.name) : tr("Show %1 on your map").arg(r.name));
        connect(eye, &QToolButton::clicked, this, [this, id = r.id, shown] { setShown(id, !shown); });
        head->addWidget(eye);
        v->addLayout(head);
        auto* status = new QLabel(row);
        status->setObjectName(QStringLiteral("routeStatus"));
        status->setWordWrap(true);
        if (st.broken) {
            status->setText(tr("Broken: the track under it changed."));
            status->setStyleSheet(QStringLiteral("color: #B91C1C; font-weight: 600;"));
        } else {
            QString t = st.setAgainst.isEmpty() ? tr("OK") : tr("%n switch(es) set the other way", nullptr, static_cast<int>(st.setAgainst.size()));
            t += QStringLiteral(" · ") + edit::venue::formatLength(st.length, unit);
            if (!st.turnsAround.isEmpty()) t += QStringLiteral(" · ") + tr("Turns the train around");
            status->setText(t);
            if (!st.setAgainst.isEmpty()) status->setStyleSheet(QStringLiteral("color: #B45309; font-weight: 600;"));
        }
        v->addWidget(st.broken ? help::withHelp(status, QStringLiteral("routes.broken"), row) : status);
        auto* actions = new QHBoxLayout;
        if (!st.broken && !st.switches.isEmpty()) {
            auto* b = new QPushButton(tr("Set switches"), row);
            b->setObjectName(QStringLiteral("routeSetSwitches"));
            connect(b, &QPushButton::clicked, this, [this, id = r.id] { setSwitches(id); });
            actions->addWidget(help::withHelp(b, QStringLiteral("routes.setSwitches"), row));
        }
        if (st.broken) {
            auto* b = new QPushButton(tr("Find a new way"), row);
            b->setObjectName(QStringLiteral("routeFindNewWay"));
            connect(b, &QPushButton::clicked, this, [this, id = r.id] { findNewWay(id); });
            actions->addWidget(b);
        }
        auto* ren = new QPushButton(tr("Rename"), row);
        ren->setObjectName(QStringLiteral("routeRename"));
        connect(ren, &QPushButton::clicked, this, [this, id = r.id, n = r.name] {
            if (const auto nn = askName_(n)) rename(id, *nn);
        });
        actions->addWidget(ren);
        auto* del = new QPushButton(tr("Delete"), row);
        del->setObjectName(QStringLiteral("routeDelete"));
        connect(del, &QPushButton::clicked, this, [this, id = r.id] { remove(id); });
        actions->addWidget(del);
        actions->addStretch(1);
        v->addLayout(actions);
        list_->insertWidget(at++, row);
    }
}

}  // namespace bld::ui
