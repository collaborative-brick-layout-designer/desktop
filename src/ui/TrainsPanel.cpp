#include "TrainsPanel.h"

#include "FitChecksList.h"
#include "ServerLibrary.h"
#include "TrainDialogs.h"

#include "../core/Ids.h"
#include "../core/Map.h"
#include "../edit/venue/VenueUnits.h"
#include "../saveload/SidecarIO.h"
#include "../track/FitChecks.h"
#include "../track/Trains.h"
#include "help/HelpButton.h"
#include "tours/Tours.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {

const char* const kColors[] = { "#B40000", "#0055BF", "#237841", "#F2CD37", "#6C6E68", "#FE8A18", "#05131D", "#A0A5A9" };

template <typename T, typename F>
QString nextColor(const std::vector<T>& taken, F colorOf) {
    QStringList used;
    for (const auto& x : taken) used << colorOf(x);
    for (const char* c : kColors)
        if (!used.contains(QString::fromLatin1(c))) return QString::fromLatin1(c);
    return QString::fromLatin1(kColors[taken.size() % 8]);
}

QVBoxLayout* scrolled(QWidget* page, QVBoxLayout* outer) {
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    auto* list = new QVBoxLayout(inner);
    list->setContentsMargins(0, 0, 0, 0);
    list->addStretch(1);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
    return list;
}

void clearRows(QVBoxLayout* list) {
    while (list->count() > 1) {
        QLayoutItem* it = list->takeAt(0);
        // Later (a row's own button may be what changed the list), and out of
        // the panel now, so nothing finds the old row meanwhile.
        if (QWidget* w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
}

QLabel* swatch(const QString& color, QWidget* parent) {
    auto* dot = new QLabel(parent);
    dot->setFixedSize(12, 12);
    dot->setStyleSheet(QStringLiteral("background: %1; border: 1px solid rgba(0,0,0,0.25); border-radius: 2px;").arg(color));
    return dot;
}

}  // namespace

TrainsPanel::TrainsPanel(parts::PartsLibrary& parts, QWidget* parent) : QDockWidget(tr("Trains"), parent), parts_(parts) {
    setObjectName(QStringLiteral("dock.trains"));
    auto* body = new QWidget(this);
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(8, 8, 8, 8);
    message_ = new QLabel(body);
    message_->setObjectName(QStringLiteral("trainsMessage"));
    message_->setWordWrap(true);
    message_->hide();
    tabs_ = new QTabWidget(body);
    tabs_->setObjectName(QStringLiteral("trainsTabs"));
    tours::tag(tabs_->tabBar(), QStringLiteral("trainsPanel.tabs"));

    auto* trainsPage = new QWidget(tabs_);
    auto* tv = new QVBoxLayout(trainsPage);
    auto* ttop = new QHBoxLayout;
    newTrain_ = new QPushButton(tr("New train"), trainsPage);
    newTrain_->setObjectName(QStringLiteral("trainNew"));
    connect(newTrain_, &QPushButton::clicked, this, &TrainsPanel::newTrain);
    ttop->addWidget(help::withHelp(newTrain_, QStringLiteral("trains.newTrain"), trainsPage));
    ttop->addStretch(1);
    tv->addLayout(ttop);
    trainsEmpty_ = new QLabel(trainsPage);
    trainsEmpty_->setObjectName(QStringLiteral("trainsEmpty"));
    trainsEmpty_->setWordWrap(true);
    tv->addWidget(trainsEmpty_);
    checks_ = new FitChecksList(tr("Checks"), trainsPage);
    connect(checks_, &FitChecksList::showRequested, this, &TrainsPanel::showSpotRequested);
    checks_->hide();
    tv->addWidget(checks_);
    trainList_ = scrolled(trainsPage, tv);
    tabs_->addTab(trainsPage, tr("Trains"));

    auto* stockPage = new QWidget(tabs_);
    auto* sv = new QVBoxLayout(stockPage);
    auto* stop = new QHBoxLayout;
    newStock_ = new QPushButton(tr("New car"), stockPage);
    newStock_->setObjectName(QStringLiteral("stockNew"));
    connect(newStock_, &QPushButton::clicked, this, &TrainsPanel::newStock);
    stop->addWidget(help::withHelp(newStock_, QStringLiteral("trains.newStock"), stockPage));
    fromRoster_ = new QPushButton(tr("From a roster…"), stockPage);
    fromRoster_->setObjectName(QStringLiteral("rosterOpen"));
    connect(fromRoster_, &QPushButton::clicked, this, &TrainsPanel::openRoster);
    stop->addWidget(help::withHelp(fromRoster_, QStringLiteral("trains.roster"), stockPage));
    stop->addStretch(1);
    sv->addLayout(stop);
    stockEmpty_ = new QLabel(stockPage);
    stockEmpty_->setObjectName(QStringLiteral("stockEmpty"));
    stockEmpty_->setWordWrap(true);
    sv->addWidget(stockEmpty_);
    stockList_ = scrolled(stockPage, sv);
    makeTrain_ = new QPushButton(stockPage);
    makeTrain_->setObjectName(QStringLiteral("trainFromTicked"));
    connect(makeTrain_, &QPushButton::clicked, this, [this] { makeTrainOfTicked(); });
    sv->addWidget(makeTrain_);
    tabs_->addTab(stockPage, tr("Rolling stock"));

    v->addWidget(tabs_, 1);
    v->addWidget(message_);
    setWidget(body);

    confirm_ = [this](const QString& question) { return QMessageBox::question(this, tr("Trains"), question) == QMessageBox::Yes; };
    editStock_ = [this](const core::RollingStock& s, bool isNew) -> std::optional<core::RollingStock> {
        StockDialog d(s, isNew, parts_, this);
        if (d.exec() != QDialog::Accepted) return std::nullopt;
        return d.stock();
    };
    editTrain_ = [this](const core::Train& t) -> std::optional<core::Train> {
        TrainDialog d(t, map_ ? map_->sidecar.rollingStock : std::vector<core::RollingStock>{}, this);
        if (d.exec() != QDialog::Accepted) return std::nullopt;
        return d.train();
    };
    rebuild();
}

void TrainsPanel::setMap(const core::Map* map) {
    map_ = map;
    // No cars yet: Rolling stock is where to start.
    if (map_ && map_->sidecar.rollingStock.empty()) tabs_->setCurrentIndex(1);
    // Ticks of cars no longer here go.
    ticked_.erase(std::remove_if(ticked_.begin(), ticked_.end(), [this](const QString& id) { return !stockById(id); }), ticked_.end());
    rebuild();
}

void TrainsPanel::setPlacing(const QString& trainId) {
    placing_ = trainId;
    rebuild();
}

void TrainsPanel::setServerLibrary(ServerLibrary* library) {
    if (library_ == library) return;
    library_ = library;
    rebuild();
}

void TrainsPanel::setReadOnly(bool readOnly) {
    if (readOnly_ == readOnly) return;
    readOnly_ = readOnly;
    rebuild();
}

void TrainsPanel::setRoster(const QList<sync::RosterStock>& entries) {
    roster_.clear();
    for (const auto& e : entries) roster_.insert(e.id, e);
    rebuild();
}

QString TrainsPanel::message() const { return message_->isHidden() ? QString() : message_->text(); }

void TrainsPanel::say(const QString& text) {
    message_->setText(text);
    message_->setVisible(!text.isEmpty());
}

const core::RollingStock* TrainsPanel::stockById(const QString& id) const {
    if (!map_) return nullptr;
    for (const auto& s : map_->sidecar.rollingStock)
        if (s.id == id) return &s;
    return nullptr;
}

const core::Train* TrainsPanel::trainById(const QString& id) const {
    if (!map_) return nullptr;
    for (const auto& t : map_->sidecar.trains)
        if (t.id == id) return &t;
    return nullptr;
}

void TrainsPanel::newStock() {
    if (!map_) return;
    core::RollingStock s = core::simpleStock(core::newBbmId(), 32.0);
    s.color = nextColor(map_->sidecar.rollingStock, [](const core::RollingStock& x) { return x.color; });
    const auto made = editStock_(s, true);
    if (!made) return;
    tabs_->setCurrentIndex(1);
    say(tr("Added %1.").arg(stockLabel(*made)));
    emit stockEdited(made->id, made, tr("New car"));
}

void TrainsPanel::changeStock(const QString& id) {
    const core::RollingStock* s = stockById(id);
    if (!s) return;
    const auto changed = editStock_(*s, false);
    if (!changed) return;
    say(tr("Saved %1.").arg(stockLabel(*changed)));
    emit stockEdited(id, changed, tr("Change car"));
}

bool TrainsPanel::removeStock(const QString& id) {
    const core::RollingStock* s = stockById(id);
    if (!s || !map_) return false;
    QStringList in;
    for (const auto& t : map_->sidecar.trains)
        if (std::any_of(t.units.begin(), t.units.end(), [&](const core::TrainUnit& u) { return u.stock == id; })) in << t.name;
    const QString q = in.isEmpty() ? tr("Delete %1?").arg(stockLabel(*s))
                                   : tr("Delete %1 and take it out of %2?").arg(stockLabel(*s), in.join(QStringLiteral(", ")));
    if (!confirm_(q)) return false;
    emit stockRemoved(id, tr("Delete car"));
    return true;
}

void TrainsPanel::setTicked(const QString& stockId, bool ticked) {
    ticked_.removeAll(stockId);
    if (ticked) ticked_ << stockId;
    rebuild();
}

QString TrainsPanel::makeTrainOfTicked() {
    if (!map_ || ticked_.isEmpty()) return {};
    core::Train t;
    t.id = core::newBbmId();
    t.name = tr("Train %1").arg(map_->sidecar.trains.size() + 1);
    t.color = nextColor(map_->sidecar.trains, [](const core::Train& x) { return x.color; });
    for (const QString& id : ticked_)
        if (stockById(id)) t.units.push_back({ id, false });
    if (t.units.empty()) return {};
    ticked_.clear();
    tabs_->setCurrentIndex(0);
    say(tr("Made %1 from %n car(s). Put it on the track to see it.", nullptr, static_cast<int>(t.units.size())).arg(t.name));
    emit trainEdited(t.id, t, tr("New train"));
    return t.id;
}

void TrainsPanel::newTrain() {
    if (!map_) return;
    core::Train t;
    t.id = core::newBbmId();
    t.name = tr("Train %1").arg(map_->sidecar.trains.size() + 1);
    t.color = nextColor(map_->sidecar.trains, [](const core::Train& x) { return x.color; });
    const auto made = editTrain_(t);
    if (!made || made->units.empty()) return;
    say(tr("Saved %1.").arg(made->name));
    emit trainEdited(made->id, made, tr("New train"));
}

void TrainsPanel::changeTrain(const QString& id) {
    const core::Train* t = trainById(id);
    if (!t) return;
    const auto changed = editTrain_(*t);
    if (!changed) return;
    core::Train after = *changed;
    if (after.units.empty()) after.position.reset();
    say(tr("Saved %1.").arg(after.name));
    emit trainEdited(id, after, tr("Change train"));
}

void TrainsPanel::turnAround(const QString& id) {
    const core::Train* t = trainById(id);
    if (!t || !map_) return;
    track::StockById stock;
    for (const auto& s : map_->sidecar.rollingStock) stock.insert(s.id, s);
    const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
    const core::Train after = track::turnedAround(net, *t, stock, map_->sidecar.switchStates);
    say(tr("%1 turned around.").arg(t->name));
    emit trainEdited(id, after, tr("Turn train around"));
}

void TrainsPanel::takeOff(const QString& id) {
    const core::Train* t = trainById(id);
    if (!t || !t->position) return;
    core::Train after = *t;
    after.position.reset();
    emit trainEdited(id, after, tr("Take train off the track"));
}

bool TrainsPanel::removeTrain(const QString& id) {
    const core::Train* t = trainById(id);
    if (!t || !confirm_(tr("Delete %1? Its cars stay.").arg(t->name))) return false;
    emit trainEdited(id, std::nullopt, tr("Delete train"));
    return true;
}

void TrainsPanel::addFromRoster(const sync::RosterStock& entry) {
    if (!map_) return;
    const QString shown = QStringList{ entry.number, entry.name }.join(QLatin1Char(' ')).trimmed();
    for (const auto& s : map_->sidecar.rollingStock)
        if (s.libraryId == entry.id) {
            QJsonObject r = entry.record;
            r.insert(QStringLiteral("id"), s.id);  // trains keep their cars
            if (const auto up = saveload::rollingStockFromJson(r)) {
                say(tr("Updated %1 from the roster.").arg(shown));
                emit stockEdited(s.id, up, tr("Update car from roster"));
            }
            return;
        }
    QJsonObject r = entry.record;
    r.insert(QStringLiteral("id"), core::newBbmId());
    if (const auto s = saveload::rollingStockFromJson(r)) {
        say(tr("Added %1 to this layout.").arg(shown));
        emit stockEdited(s->id, s, tr("Add car from roster"));
    }
}

sync::LibraryApi& TrainsPanel::api() {
    // The panel's own client (the signed-in server and token): it never calls back once the panel is gone.
    api_.setBase(library_->server());
    api_.setToken(library_->token());
    return api_;
}

void TrainsPanel::fetchRoster(const std::function<void()>& then) {
    if (!library_ || library_->token().isEmpty()) return;
    api().rollingStock(
        [this, then](const QList<sync::RosterStock>& entries) {
            setRoster(entries);
            if (then) then();
        },
        [this](const sync::ServerRefusal& r) { say(tr("Your rosters couldn’t be loaded: %1").arg(sync::describe(r))); });
}

void TrainsPanel::openRoster() {
    if (!library_ || library_->token().isEmpty()) {
        say(tr("Sign in to a server first (File › Servers…) to use your roster and your clubs’."));
        return;
    }
    fetchRoster([this] {
        if (!map_) return;
        // Window-modal, and gone once closed; a reply after that finds no dialog.
        auto* d = new RosterDialog(roster_.values(), map_->sidecar.rollingStock, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        connect(d, &RosterDialog::addRequested, this, [this](const sync::RosterStock& e) {
            addFromRoster(e);
            if (auto* open = findChild<RosterDialog*>(QString(), Qt::FindDirectChildrenOnly); open && map_)
                open->setEntries(roster_.values(), map_->sidecar.rollingStock);
        });
        connect(d, &RosterDialog::removeRequested, this, &TrainsPanel::removeFromRoster);
        d->open();
    });
}

void TrainsPanel::removeFromRoster(const sync::RosterStock& e) {
    if (!library_) return;
    const QString shown = QStringList{ e.number, e.name }.join(QLatin1Char(' ')).trimmed();
    if (!confirm_(tr("Take %1 off the roster? Layouts keep their copies.").arg(shown))) return;
    api().deleteStock(e.id, [this, id = e.id] {
        roster_.remove(id);
        auto* open = findChild<RosterDialog*>(QString(), Qt::FindDirectChildrenOnly);
        if (open && map_) open->setEntries(roster_.values(), map_->sidecar.rollingStock);
    }, [this](const sync::ServerRefusal& r) { say(tr("It couldn’t be taken off the roster: %1").arg(sync::describe(r))); });
}

void TrainsPanel::saveToRoster(const QString& stockId, const QString& orgSlug) {
    const core::RollingStock* s = stockById(stockId);
    if (!s || !library_ || library_->token().isEmpty()) return;
    QJsonObject record = saveload::rollingStockToJson(*s);
    for (const char* k : { "id", "libraryId", "libraryVersion" }) record.remove(QLatin1String(k));
    const QString& id = stockId;
    const auto done = [this, id](const sync::RosterStock& e) {
        const core::RollingStock* now = stockById(id);
        if (!now) return;
        core::RollingStock linked = *now;
        linked.libraryId = e.id;
        linked.libraryVersion = e.version;
        roster_.insert(e.id, e);
        say(tr("Saved %1 to the roster.").arg(stockLabel(linked)));
        emit stockEdited(id, linked, tr("Save car to roster"));
    };
    const auto failed = [this](const sync::ServerRefusal& r) { say(tr("It couldn’t be saved to the roster: %1").arg(sync::describe(r))); };
    // "Save changes" goes back to the entry it came from; anything else is a new entry.
    if (orgSlug == QLatin1String("@linked") && !s->libraryId.isEmpty()) api().updateStock(s->libraryId, record, done, failed);
    else api().createStock(record, orgSlug, done, failed);
}

void TrainsPanel::rebuild() {
    clearRows(trainList_);
    clearRows(stockList_);
    const bool editable = map_ && !readOnly_;
    newTrain_->setEnabled(editable);
    newStock_->setEnabled(editable);
    fromRoster_->setEnabled(editable);
    const auto unit = edit::venue::accountLengthUnit();
    const std::vector<core::RollingStock> noStock;
    const std::vector<core::Train> noTrains;
    const auto& stock = map_ ? map_->sidecar.rollingStock : noStock;
    const auto& trains = map_ ? map_->sidecar.trains : noTrains;
    const QMap<QString, int> noStates;
    const auto& switchStates = map_ ? map_->sidecar.switchStates : noStates;
    track::StockById byId;
    for (const auto& s : stock) byId.insert(s.id, s);
    const track::TrackNetwork net = map_ && !trains.empty() ? track::trackNetworkOf(*map_, parts_) : track::TrackNetwork();
    const bool hasTrack = !net.brickIds().isEmpty();
    tabs_->setTabText(0, tr("Trains (%1)").arg(trains.size()));
    tabs_->setTabText(1, tr("Rolling stock (%1)").arg(stock.size()));

    // Rolling stock.
    stockEmpty_->setVisible(stock.empty());
    stockEmpty_->setText(tr("No rolling stock yet. Press New car: a number and a length is all a quick car needs."));
    const bool signedIn = library_ && !library_->token().isEmpty();
    int at = 0;
    for (const auto& s : stock) {
        auto* row = new QFrame;
        row->setObjectName(QStringLiteral("stock_") + s.id);
        row->setFrameShape(QFrame::StyledPanel);
        auto* v = new QVBoxLayout(row);
        v->setContentsMargins(8, 6, 8, 6);
        auto* head = new QHBoxLayout;
        if (editable) {
            auto* tick = new QCheckBox(row);
            tick->setObjectName(QStringLiteral("stockTick"));
            tick->setAccessibleName(tr("Pick %1 for a new train").arg(stockLabel(s)));
            tick->setChecked(ticked_.contains(s.id));
            connect(tick, &QCheckBox::toggled, this, [this, id = s.id](bool on) { setTicked(id, on); });
            head->addWidget(tick);
        }
        head->addWidget(swatch(s.color, row));
        auto* name = new QLabel(stockLabel(s), row);
        name->setStyleSheet(QStringLiteral("font-weight: 600;"));
        head->addWidget(name, 1);
        head->addWidget(new QLabel(edit::venue::formatLength(s.length, unit), row));
        v->addLayout(head);
        QStringList facts{ stockKindLabel(s.kind) };
        QStringList in;
        for (const auto& t : trains)
            if (std::any_of(t.units.begin(), t.units.end(), [&](const core::TrainUnit& u) { return u.stock == s.id; })) in << t.name;
        if (!in.isEmpty()) facts << tr("In %1").arg(in.join(QStringLiteral(", ")));
        const auto entry = roster_.constFind(s.libraryId);
        const bool newer = !s.libraryId.isEmpty() && entry != roster_.constEnd() && s.libraryVersion < entry->version;
        if (!s.libraryId.isEmpty()) facts << (newer ? tr("The roster has a newer version") : tr("From a roster"));
        auto* factLabel = new QLabel(facts.join(QStringLiteral(" · ")), row);
        factLabel->setObjectName(QStringLiteral("stockFacts"));
        factLabel->setWordWrap(true);
        v->addWidget(factLabel);
        if (editable) {
            auto* actions = new QHBoxLayout;
            auto* change = new QPushButton(tr("Change"), row);
            change->setObjectName(QStringLiteral("stockChange"));
            connect(change, &QPushButton::clicked, this, [this, id = s.id] { changeStock(id); });
            actions->addWidget(change);
            if (newer) {
                auto* up = new QPushButton(tr("Update from roster"), row);
                up->setObjectName(QStringLiteral("stockUpdate"));
                connect(up, &QPushButton::clicked, this, [this, e = *entry] { addFromRoster(e); });
                actions->addWidget(up);
            }
            if (signedIn) {
                auto* save = new QPushButton(tr("Save to roster"), row);
                save->setObjectName(QStringLiteral("stockSaveToRoster"));
                auto* menu = new QMenu(save);
                if (!s.libraryId.isEmpty() && entry != roster_.constEnd() && entry->canManage)
                    menu->addAction(entry->owner.isClub() ? tr("Save changes to %1’s roster").arg(entry->owner.name) : tr("Save changes to your roster"),
                                    this, [this, id = s.id] { saveToRoster(id, QStringLiteral("@linked")); });
                menu->addAction(tr("Your roster"), this, [this, id = s.id] { saveToRoster(id, QString()); });
                for (const auto& org : sync::clubsThatTake(library_->orgs(), sync::AddKind::Module))
                    menu->addAction(tr("%1’s roster").arg(org.name), this, [this, id = s.id, slug = org.slug] { saveToRoster(id, slug); });
                save->setMenu(menu);
                actions->addWidget(save);
            }
            auto* del = new QPushButton(tr("Delete"), row);
            del->setObjectName(QStringLiteral("stockDelete"));
            connect(del, &QPushButton::clicked, this, [this, id = s.id] { removeStock(id); });
            actions->addWidget(del);
            actions->addStretch(1);
            v->addLayout(actions);
        }
        stockList_->insertWidget(at++, row);
    }
    makeTrain_->setVisible(editable && !ticked_.isEmpty());
    makeTrain_->setText(ticked_.size() == 1 ? tr("Make a train of this car") : tr("Make a train of these %1 cars").arg(ticked_.size()));

    // The fit checks for the trains on the track.
    const bool anyOnTrack = std::any_of(trains.begin(), trains.end(), [](const core::Train& t) { return t.position.has_value(); });
    if (anyOnTrack && map_) {
        track::FitInput in;
        in.trains = trains;
        in.stock = byId;
        in.states = switchStates;
        // The trains' own (the track's and the routes' are in the Routes panel).
        std::vector<track::FitCheck> checks;
        for (auto& c : track::fitChecks(net, in))
            if (!c.train.isEmpty()) checks.push_back(std::move(c));
        checks_->setChecks(checks, *map_, tr("Every train on the track fits: nothing too long, too tight or too close."));
    } else {
        checks_->hide();
    }

    // Trains.
    trainsEmpty_->setVisible(trains.empty());
    trainsEmpty_->setText(stock.empty() ? tr("No trains yet. Make a car or two in Rolling stock first, then put them together here.")
                                        : tr("No trains yet. Press New train, or tick cars in Rolling stock and make a train of them."));
    at = 0;
    for (const auto& t : trains) {
        const auto placed = t.position ? track::placeTrain(net, t, byId, switchStates) : std::nullopt;
        auto* row = new QFrame;
        row->setObjectName(QStringLiteral("train_") + t.id);
        row->setFrameShape(QFrame::StyledPanel);
        auto* v = new QVBoxLayout(row);
        v->setContentsMargins(8, 6, 8, 6);
        auto* head = new QHBoxLayout;
        head->addWidget(swatch(t.color, row));
        auto* name = new QLabel(t.name.isEmpty() ? tr("Untitled train") : t.name, row);
        name->setObjectName(QStringLiteral("trainName"));
        name->setStyleSheet(QStringLiteral("font-weight: 600;"));
        head->addWidget(name, 1);
        head->addWidget(new QLabel(edit::venue::formatLength(track::trainLength(t, byId), unit), row));
        v->addLayout(head);
        auto* state = new QLabel(row);
        state->setObjectName(QStringLiteral("trainState"));
        QString st;
        if (!t.position) st = tr("Not on the map");
        else if (!placed) st = tr("Its track was moved or removed");
        else if (placed->offTrack) st = tr("Runs off the end of the track");
        else st = tr("On the track");
        state->setText(st + QStringLiteral(" · ") + tr("%n car(s)", nullptr, static_cast<int>(t.units.size())));
        if (t.position && (!placed || placed->offTrack)) state->setStyleSheet(QStringLiteral("color: #B91C1C; font-weight: 600;"));
        else if (t.position) state->setStyleSheet(QStringLiteral("color: #15803D; font-weight: 600;"));
        v->addWidget(state);
        if (editable) {
            auto* actions = new QHBoxLayout;
            const bool isPlacing = placing_ == t.id;
            auto* place = new QPushButton(isPlacing ? tr("Click the track…") : t.position ? tr("Move") : tr("Put on the track"), row);
            place->setObjectName(QStringLiteral("trainPlace"));
            place->setCheckable(true);
            place->setChecked(isPlacing);
            place->setEnabled(hasTrack && !t.units.empty());
            if (!hasTrack) place->setToolTip(tr("There’s no track on this layout yet."));
            connect(place, &QPushButton::clicked, this, [this, id = t.id, isPlacing] {
                if (isPlacing) emit placeCancelled();
                else emit placeRequested(id);
            });
            actions->addWidget(help::withHelp(place, QStringLiteral("trains.putOnTrack"), row));
            if (t.position) {
                auto* turn = new QPushButton(tr("Turn around"), row);
                turn->setObjectName(QStringLiteral("trainTurn"));
                connect(turn, &QPushButton::clicked, this, [this, id = t.id] { turnAround(id); });
                actions->addWidget(help::withHelp(turn, QStringLiteral("trains.turnAround"), row));
                auto* off = new QPushButton(tr("Take off the track"), row);
                off->setObjectName(QStringLiteral("trainTakeOff"));
                connect(off, &QPushButton::clicked, this, [this, id = t.id] { takeOff(id); });
                actions->addWidget(off);
            }
            actions->addStretch(1);
            v->addLayout(actions);
            auto* more = new QHBoxLayout;
            auto* change = new QPushButton(tr("Change"), row);
            change->setObjectName(QStringLiteral("trainChange"));
            connect(change, &QPushButton::clicked, this, [this, id = t.id] { changeTrain(id); });
            more->addWidget(change);
            auto* del = new QPushButton(tr("Delete"), row);
            del->setObjectName(QStringLiteral("trainDelete"));
            connect(del, &QPushButton::clicked, this, [this, id = t.id] { removeTrain(id); });
            more->addWidget(del);
            more->addStretch(1);
            v->addLayout(more);
        }
        trainList_->insertWidget(at++, row);
    }
}

}  // namespace bld::ui
