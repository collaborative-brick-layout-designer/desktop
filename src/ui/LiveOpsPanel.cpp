#include "LiveOpsPanel.h"

#include "OpsText.h"
#include "TrainDialogs.h"

#include "../core/Ids.h"
#include "../core/Map.h"
#include "help/HelpButton.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {

QLabel* note(const QString& text, QWidget* parent, const QString& name = {}) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    if (!name.isEmpty()) l->setObjectName(name);
    return l;
}

QLabel* bold(const QString& text, QWidget* parent) {
    QLabel* l = note(text, parent);
    l->setStyleSheet(QStringLiteral("font-weight: 600;"));
    return l;
}

QPushButton* button(const QString& text, const QString& name, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName(name);
    return b;
}

QFrame* card(const QString& name, QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setObjectName(name);
    f->setFrameShape(QFrame::StyledPanel);
    auto* v = new QVBoxLayout(f);
    v->setContentsMargins(8, 6, 8, 6);
    return f;
}

QVBoxLayout* columnOf(QFrame* f) { return static_cast<QVBoxLayout*>(f->layout()); }

// A list of places to pick from; `value` stays listed when its place is gone.
QComboBox* placeBox(const track::OpsView& view, const QString& value, const QString& none, const QString& name, QWidget* parent, bool offOnly = false) {
    auto* box = new QComboBox(parent);
    box->setObjectName(name);
    box->addItem(none, QString());
    if (!value.isEmpty() && !view.place(value)) box->addItem(OpsText::gonePlace(), value);
    for (const auto& p : view.places)
        if (p.id == value || !offOnly || p.place.value(QLatin1String("spots")).toArray().isEmpty()) box->addItem(p.name, p.id);
    box->setCurrentIndex(std::max(0, box->findData(value)));
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    return box;
}

int moves(const std::vector<track::ListStop>* list) {
    int n = 0;
    if (list)
        for (const auto& s : *list) n += static_cast<int>(s.setOut.size() + s.pickUp.size());
    return n;
}

QString fieldOf(const char* name, int slot) { return QLatin1String(name) + QString::number(slot); }

}  // namespace

LiveOpsPanel::LiveOpsPanel(parts::PartsLibrary& parts, QWidget* parent) : QDockWidget(tr("Live Ops"), parent), parts_(parts) {
    setObjectName(QStringLiteral("dock.liveops"));
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    setWidget(scroll_);
    confirm_ = [this](const QString& question) { return QMessageBox::question(this, tr("Delete"), question) == QMessageBox::Yes; };
    rebuild();
}

void LiveOpsPanel::setMap(const core::Map* map) {
    if (map != map_) {
        waybill_.clear();
        job_.clear();
        making_ = false;
    }
    map_ = map;
    later();
}

void LiveOpsPanel::setReadOnly(bool readOnly) {
    if (readOnly_ == readOnly) return;
    readOnly_ = readOnly;
    later();
}

void LiveOpsPanel::showTab(Tab tab) {
    tab_ = tab;
    waybill_.clear();
    job_.clear();
    making_ = false;
    later();
}

void LiveOpsPanel::openWaybill(const QString& id) {
    tab_ = Tab::Waybills;
    waybill_ = id;
    later();
}

void LiveOpsPanel::openJob(const QString& id) {
    tab_ = Tab::Jobs;
    job_ = id;
    later();
}

void LiveOpsPanel::showEvent(QShowEvent* e) {
    QDockWidget::showEvent(e);
    later();
}

// Not while a field's own signal is being handled: it would be deleted under it.
void LiveOpsPanel::later() {
    if (waiting_) return;
    waiting_ = true;
    QTimer::singleShot(0, this, [this] {
        // (Worked out while it shows.)
        if (waiting_ && isVisible()) rebuild();
        waiting_ = false;
    });
}

void LiveOpsPanel::drawNow() { rebuild(); }

void LiveOpsPanel::change(const edit::OpsChange& c, const QString& what) {
    if (!map_ || readOnly_ || c.isEmpty()) return;
    emit changed(c, what);
}

void LiveOpsPanel::editCar(const QString& car, const std::function<void(edit::CarOps&)>& f, const QString& what) {
    if (!map_) return;
    for (const auto& s : map_->sidecar.rollingStock)
        if (s.id == car) {
            edit::CarOps ops = edit::carOps(s);
            f(ops);
            change(edit::changedCar(*map_, car, ops), what);
            return;
        }
}

void LiveOpsPanel::addWaybill(const QString& car) {
    if (!map_ || readOnly_) return;
    const QString id = core::newBbmId();
    const auto c = edit::newWaybill(*map_, id, car);
    if (c.isEmpty()) return;
    openWaybill(id);
    change(c, tr("New waybill"));
}

void LiveOpsPanel::startMaking() {
    if (readOnly_) return;
    tab_ = Tab::Waybills;
    waybill_.clear();
    making_ = true;
    seed_ = QRandomGenerator::global()->generate();
    later();
}

void LiveOpsPanel::shuffle() {
    seed_ = QRandomGenerator::global()->generate();
    later();
}

void LiveOpsPanel::addMade() {
    if (!map_) return;
    making_ = false;
    change(edit::madeWaybills(*map_, track::previewWaybills(view_, seed_)), tr("Make waybills"));
    later();
}

void LiveOpsPanel::delivered(const QString& car) {
    if (!map_ || readOnly_) return;
    const auto to = view_.to.constFind(car);
    const auto* p = to == view_.to.constEnd() ? nullptr : view_.place(to->place);
    const bool leaves = p && p->place.value(QLatin1String("spots")).toArray().isEmpty();
    const auto c = edit::turnedCar(*map_, car, leaves ? p->id : QString());
    if (c.isEmpty()) return;
    if (leaves) emit carLeaves(car, c, tr("Delivered"));
    else change(c, tr("Delivered"));
}

void LiveOpsPanel::addJob() {
    if (!map_ || readOnly_) return;
    const QString id = core::newBbmId();
    const auto c = edit::newJob(*map_, id, tr("Job %1"));
    if (c.isEmpty()) return;
    openJob(id);
    change(c, tr("New job"));
}

void LiveOpsPanel::removeOpen() {
    if (!map_ || readOnly_) return;
    if (tab_ == Tab::Waybills && !waybill_.isEmpty()) {
        if (!confirm_(tr("Delete this waybill? The car stays where it is. You can undo this."))) return;
        const QString id = std::exchange(waybill_, QString());
        change(edit::removedWaybill(*map_, id), tr("Delete waybill"));
    } else if (tab_ == Tab::Jobs && !job_.isEmpty()) {
        const track::Job* j = view_.job(job_);
        if (!confirm_(tr("Delete the job “%1”? Its cars and their waybills stay; another job takes them, or they’re listed under Can’t route. You can undo this.")
                          .arg(j ? j->name : QString())))
            return;
        const QString id = std::exchange(job_, QString());
        change(edit::removedJob(id), tr("Delete job"));
    }
    later();
}

void LiveOpsPanel::rebuild() {
    waiting_ = false;
    if (map_) {
        const track::TrackNetwork net = track::trackNetworkOf(*map_, parts_);
        view_ = track::opsView(*map_, net, track::trackRuns(net), tr("Module"), tr("(no name)"));
    } else view_ = {};
    // The field being typed in: kept, with what's typed, when it is still there.
    QString focused;
    QString typed;
    bool typedIn = false;
    int cursor = 0;
    if (QWidget* had = scroll_->widget())
        if (auto* f = focusWidget(); f && had->isAncestorOf(f) && f->hasFocus()) {
            focused = f->objectName();
            if (auto* line = qobject_cast<QLineEdit*>(f)) {
                typed = line->text();
                typedIn = line->isModified();
                cursor = line->cursorPosition();
            }
        }
    const int at = scroll_->verticalScrollBar()->value();
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(8, 8, 8, 8);
    build(v);
    v->addStretch(1);
    if (QWidget* old = scroll_->takeWidget()) {
        old->blockSignals(true);
        for (QWidget* w : old->findChildren<QWidget*>()) w->blockSignals(true);
        old->deleteLater();
    }
    scroll_->setWidget(page);
    scroll_->verticalScrollBar()->setValue(at);
    if (auto* line = typedIn ? page->findChild<QLineEdit*>(focused) : nullptr) {
        line->setFocus();
        line->setText(typed);
        line->setModified(true);
        line->setCursorPosition(cursor);
    }
}

void LiveOpsPanel::build(QVBoxLayout* v) {
    QWidget* page = v->parentWidget();
    auto* tabs = new QHBoxLayout;
    for (const auto& [t, text, name] : { std::tuple{ Tab::Cars, tr("Cars"), "opsTabCars" }, std::tuple{ Tab::Waybills, tr("Waybills"), "opsTabWaybills" }, std::tuple{ Tab::Jobs, tr("Jobs"), "opsTabJobs" } }) {
        auto* b = button(text, QLatin1String(name), page);
        b->setCheckable(true);
        b->setChecked(tab_ == t);
        connect(b, &QPushButton::clicked, this, [this, t = t] { showTab(t); });
        tabs->addWidget(b);
    }
    v->addLayout(tabs);
    const bool anyCars = std::any_of(view_.stock.begin(), view_.stock.end(), [](const core::RollingStock& s) { return !track::isEngine(s); });
    if (view_.places.empty()) {
        v->addWidget(note(tr("Cars go to places: industries, yards, staging and interchanges. There are none on this layout yet."), page, QStringLiteral("opsNothing")));
        if (!readOnly_) {
            auto* b = button(tr("Open Places"), QStringLiteral("opsOpenPlaces"), page);
            connect(b, &QPushButton::clicked, this, &LiveOpsPanel::openPlacesRequested);
            v->addWidget(b);
        }
        return;
    }
    if (!anyCars) {
        v->addWidget(note(tr("There are no cars on this layout yet."), page, QStringLiteral("opsNothing")));
        if (!readOnly_) {
            auto* b = button(tr("Open Trains"), QStringLiteral("opsOpenTrains"), page);
            connect(b, &QPushButton::clicked, this, &LiveOpsPanel::openTrainsRequested);
            v->addWidget(b);
        }
        return;
    }
    buildCant(v);
    if (tab_ == Tab::Cars) buildCars(v);
    else if (tab_ == Tab::Waybills) {
        const auto w = view_.waybills.constFind(waybill_);
        if (w != view_.waybills.constEnd()) buildWaybill(v, *w);
        else buildWaybills(v);
    } else if (const track::Job* j = view_.job(job_)) buildJob(v, *j);
    else buildJobs(v);
}

void LiveOpsPanel::buildCant(QVBoxLayout* v) {
    const auto& list = view_.routing.cantRoute;
    if (list.empty()) return;
    auto* box = card(QStringLiteral("opsCant"), v->parentWidget());
    auto* col = columnOf(box);
    auto* title = bold(tr("Can’t route (%1)").arg(list.size()), box);
    title->setObjectName(QStringLiteral("opsCantTitle"));
    col->addWidget(help::withHelp(title, QStringLiteral("ops.cantRoute"), box));
    for (const auto& c : list) {
        auto* row = new QHBoxLayout;
        row->addWidget(note(QStringLiteral("%1: %2").arg(OpsText::carName(view_, c.car), OpsText::cant(view_, c)), box, QStringLiteral("cant_") + c.car), 1);
        if (view_.points.contains(c.car)) {
            auto* b = button(tr("Show me"), QStringLiteral("cantShow"), box);
            connect(b, &QPushButton::clicked, this, [this, at = view_.points.value(c.car)] { emit showSpotRequested(at); });
            row->addWidget(b);
        }
        if (view_.pockets.contains(c.car)) {
            auto* b = button(tr("Waybill"), QStringLiteral("cantWaybill"), box);
            connect(b, &QPushButton::clicked, this, [this, id = view_.pockets.value(c.car)] { openWaybill(id); });
            row->addWidget(b);
        }
        col->addLayout(row);
    }
    v->addWidget(box);
}

void LiveOpsPanel::buildCars(QVBoxLayout* v) {
    QWidget* page = v->parentWidget();
    std::vector<core::RollingStock> cars;
    for (const auto& s : std::as_const(view_.stock))
        if (!track::isEngine(s)) cars.push_back(s);
    QCollator order;
    order.setNumericMode(true);
    std::stable_sort(cars.begin(), cars.end(), [&](const auto& a, const auto& b) { return order.compare(stockLabel(a), stockLabel(b)) < 0; });
    const bool anyOff = std::any_of(view_.places.begin(), view_.places.end(), [](const track::NamedPlace& p) { return p.place.value(QLatin1String("spots")).toArray().isEmpty(); });
    v->addWidget(help::withHelp(bold(tr("Cars"), page), QStringLiteral("ops.hold"), page));
    for (const auto& s : cars) {
        auto* row = card(QStringLiteral("opsCar_") + s.id, page);
        auto* col = columnOf(row);
        auto* head = new QHBoxLayout;
        head->addWidget(bold(stockLabel(s), row), 1);
        if (!s.type.isEmpty()) head->addWidget(note(s.type, row));
        if (view_.points.contains(s.id)) {
            auto* b = button(tr("Show me"), QStringLiteral("opsShow"), row);
            connect(b, &QPushButton::clicked, this, [this, at = view_.points.value(s.id)] { emit showSpotRequested(at); });
            head->addWidget(b);
        }
        col->addLayout(head);
        const bool stands = view_.onTrack.contains(s.id);
        const auto at = view_.at.constFind(s.id);
        col->addWidget(note(at != view_.at.constEnd() ? tr("At %1").arg(OpsText::spotName(view_, at->place, at->spot)) : stands ? tr("On the track, at no place") : tr("Not on the track"), row,
                            QStringLiteral("opsCarAt")));
        col->addWidget(note(OpsText::next(view_, s.id), row, QStringLiteral("opsCarNext")));
        auto* actions = new QHBoxLayout;
        if (!readOnly_ && view_.routing.arrived.contains(s.id) && view_.pockets.contains(s.id)) {
            auto* b = button(tr("Delivered"), QStringLiteral("opsDelivered"), row);
            connect(b, &QPushButton::clicked, this, [this, id = s.id] { delivered(id); });
            actions->addWidget(b);
        }
        if (view_.pockets.contains(s.id)) {
            auto* b = button(tr("Waybill"), QStringLiteral("opsCarWaybill"), row);
            connect(b, &QPushButton::clicked, this, [this, id = view_.pockets.value(s.id)] { openWaybill(id); });
            actions->addWidget(b);
        } else if (!readOnly_) {
            auto* b = button(tr("New waybill"), QStringLiteral("opsCarWaybill"), row);
            connect(b, &QPushButton::clicked, this, [this, id = s.id] { addWaybill(id); });
            actions->addWidget(b);
        }
        auto* hold = new QCheckBox(tr("Set aside"), row);
        hold->setObjectName(QStringLiteral("opsHold"));
        hold->setChecked(s.opsHold.value_or(false));
        hold->setEnabled(!readOnly_);
        connect(hold, &QCheckBox::clicked, this, [this, id = s.id](bool on) { editCar(id, [on](edit::CarOps& o) { o.hold = on; }, tr("Set a car aside")); });
        actions->addWidget(hold);
        actions->addStretch(1);
        col->addLayout(actions);
        auto* grid = new QGridLayout;
        grid->addWidget(note(tr("Home"), row), 0, 0);
        auto* home = placeBox(view_, s.opsHome.value_or(QString()), tr("None"), QStringLiteral("opsHome"), row);
        home->setEnabled(!readOnly_);
        home->setAccessibleName(tr("Home"));
        connect(home, &QComboBox::activated, this, [this, home, id = s.id] { editCar(id, [to = home->currentData().toString()](edit::CarOps& o) { o.home = to; }, tr("Change a car’s home")); });
        grid->addWidget(home, 0, 1);
        if (!stands && (anyOff || !s.opsAt.value_or(QString()).isEmpty())) {
            grid->addWidget(note(tr("Where it is"), row), 1, 0);
            auto* where = placeBox(view_, s.opsAt.value_or(QString()), tr("Nowhere"), QStringLiteral("opsAt"), row, true);
            where->setEnabled(!readOnly_);
            where->setAccessibleName(tr("Where it is"));
            connect(where, &QComboBox::activated, this, [this, where, id = s.id] { editCar(id, [to = where->currentData().toString()](edit::CarOps& o) { o.at = to; }, tr("Change where a car is")); });
            grid->addWidget(where, 1, 1);
        }
        grid->setColumnStretch(1, 1);
        col->addLayout(grid);
        v->addWidget(row);
    }
}

void LiveOpsPanel::buildWaybills(QVBoxLayout* v) {
    QWidget* page = v->parentWidget();
    const bool full = view_.waybills.size() >= track::kMaxWaybills;
    if (!readOnly_) {
        auto* row = new QHBoxLayout;
        auto* make = button(tr("Make waybills…"), QStringLiteral("opsMake"), page);
        make->setEnabled(!full && !making_);
        connect(make, &QPushButton::clicked, this, &LiveOpsPanel::startMaking);
        row->addWidget(make);
        auto* fresh = button(tr("New waybill"), QStringLiteral("opsNewWaybill"), page);
        fresh->setEnabled(!full);
        connect(fresh, &QPushButton::clicked, this, [this] { addWaybill(QString()); });
        row->addWidget(fresh);
        row->addStretch(1);
        v->addLayout(row);
    }
    v->addWidget(help::withHelp(bold(tr("Waybills"), page), QStringLiteral("ops.waybill"), page));
    if (making_ && !readOnly_) buildMaking(v);
    if (view_.waybills.isEmpty() && !making_)
        v->addWidget(note(tr("No waybills yet. A waybill says where a car goes next: make one for every car at once, or write one."), page, QStringLiteral("opsNoWaybills")));
    QMap<QString, QString> holder;
    for (auto it = view_.pockets.constBegin(); it != view_.pockets.constEnd(); ++it) holder.insert(it.value(), it.key());
    std::vector<track::Waybill> list(view_.waybills.begin(), view_.waybills.end());
    QCollator order;
    order.setNumericMode(true);
    std::stable_sort(list.begin(), list.end(), [&](const auto& a, const auto& b) {
        const bool ha = holder.contains(a.id);
        const bool hb = holder.contains(b.id);
        if (ha != hb) return ha;
        return ha && order.compare(OpsText::carName(view_, holder.value(a.id)), OpsText::carName(view_, holder.value(b.id))) < 0;
    });
    for (const auto& w : list) {
        auto* row = card(QStringLiteral("opsWaybill_") + w.id, page);
        const QString car = holder.value(w.id);
        auto* open = button(car.isEmpty() ? tr("Waiting for a car") : OpsText::carName(view_, car), QStringLiteral("opsOpenWaybill"), row);
        connect(open, &QPushButton::clicked, this, [this, id = w.id] { openWaybill(id); });
        columnOf(row)->addWidget(open);
        const auto leg = track::currentLeg(w);
        const bool any = std::any_of(w.legs.begin(), w.legs.end(), [](const auto& l) { return l.has_value(); });
        columnOf(row)->addWidget(note(leg ? (car.isEmpty() ? OpsText::leg(view_, *leg) : OpsText::next(view_, car)) : any ? tr("Finished") : tr("No legs yet"), row, QStringLiteral("opsWaybillState")));
        v->addWidget(row);
    }
}

void LiveOpsPanel::buildMaking(QVBoxLayout* v) {
    auto* box = card(QStringLiteral("opsMaking"), v->parentWidget());
    auto* col = columnOf(box);
    col->addWidget(help::withHelp(bold(tr("Make waybills"), box), QStringLiteral("ops.make"), box));
    const auto made = track::previewWaybills(view_, seed_);
    const auto without = [&](const char* why) {
        return static_cast<int>(std::count_if(made.skipped.begin(), made.skipped.end(), [&](const auto& s) { return s.second == QLatin1String(why); }));
    };
    const auto cars = [](int n) { return n == 1 ? tr("1 car") : tr("%1 cars").arg(n); };
    if (made.made.empty()) {
        const bool held = std::any_of(view_.stock.begin(), view_.stock.end(), [](const core::RollingStock& s) { return s.opsHold.value_or(false); });
        col->addWidget(note(!made.skipped.empty() ? tr("No car can get one:") : held ? tr("Every car that isn’t set aside has a waybill.") : tr("Every car has a waybill."), box, QStringLiteral("opsMadeNone")));
    }
    for (const auto& m : made.made) {
        QStringList legs;
        for (const auto& l : m.legs) legs << OpsText::leg(view_, l);
        col->addWidget(note(QStringLiteral("%1: %2").arg(OpsText::carName(view_, m.car), legs.join(tr(", then "))), box, QStringLiteral("opsMade")));
    }
    if (!made.skipped.empty() && !made.made.empty()) col->addWidget(note(tr("Left without:"), box));
    if (const int n = without("noType")) col->addWidget(note(tr("%1 without a type (set it in Trains).").arg(cars(n)), box));
    if (const int n = without("noPlaces")) col->addWidget(note(tr("%1 no two places send out and take in (set what each place sends out and takes in).").arg(cars(n)), box));
    if (const int n = without("full")) col->addWidget(note(tr("%1 with no room left at the places they’d go to.").arg(cars(n)), box));
    auto* row = new QHBoxLayout;
    auto* add = button(made.made.size() == 1 ? tr("Add 1 waybill") : tr("Add %1 waybills").arg(made.made.size()), QStringLiteral("opsMakeAdd"), box);
    add->setEnabled(!made.made.empty());
    connect(add, &QPushButton::clicked, this, &LiveOpsPanel::addMade);
    row->addWidget(add);
    auto* again = button(tr("Shuffle"), QStringLiteral("opsShuffle"), box);
    again->setEnabled(!made.made.empty());
    connect(again, &QPushButton::clicked, this, &LiveOpsPanel::shuffle);
    row->addWidget(again);
    if (without("noPlaces") > 0) {
        auto* places = button(tr("Open Places"), QStringLiteral("opsOpenPlaces"), box);
        connect(places, &QPushButton::clicked, this, &LiveOpsPanel::openPlacesRequested);
        row->addWidget(places);
    }
    auto* cancel = button(tr("Cancel"), QStringLiteral("opsMakeCancel"), box);
    connect(cancel, &QPushButton::clicked, this, [this] {
        making_ = false;
        later();
    });
    row->addWidget(cancel);
    col->addLayout(row);
    v->addWidget(box);
}

void LiveOpsPanel::buildWaybill(QVBoxLayout* v, const track::Waybill& w) {
    QWidget* page = v->parentWidget();
    const QString id = w.id;
    const QString car = view_.pockets.key(id);
    const int slot = track::currentSlot(w);
    const auto save = [this, id](const QJsonObject& fields) {
        if (map_) change(edit::changedWaybill(*map_, id, fields), tr("Change waybill"));
    };
    auto* back = button(tr("← All waybills"), QStringLiteral("opsBack"), page);
    connect(back, &QPushButton::clicked, this, [this] {
        waybill_.clear();
        later();
    });
    v->addWidget(help::withHelp(back, QStringLiteral("ops.waybill"), page));
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    grid->addWidget(note(tr("Car"), page), 0, 0);
    auto* carBox = new QComboBox(page);
    carBox->setObjectName(QStringLiteral("opsWaybillCar"));
    carBox->setAccessibleName(tr("Car"));
    carBox->addItem(tr("Waiting for a car"), QString());
    for (const auto& s : std::as_const(view_.stock))
        if (!track::isEngine(s)) carBox->addItem(view_.pockets.contains(s.id) && s.id != car ? tr("%1 (has a waybill)").arg(stockLabel(s)) : stockLabel(s), s.id);
    carBox->setCurrentIndex(std::max(0, carBox->findData(car)));
    carBox->setEnabled(!readOnly_);
    connect(carBox, &QComboBox::activated, this, [this, carBox, id] {
        if (map_) change(edit::givenWaybill(*map_, id, carBox->currentData().toString()), tr("Give a waybill to a car"));
    });
    grid->addWidget(carBox, 0, 1);
    grid->addWidget(note(tr("From"), page), 1, 0);
    auto* from = placeBox(view_, w.from, tr("Not said"), QStringLiteral("opsWaybillFrom"), page);
    from->setEnabled(!readOnly_);
    from->setAccessibleName(tr("From"));
    connect(from, &QComboBox::activated, this, [from, save] { save({ { QStringLiteral("from"), from->currentData().toString() } }); });
    grid->addWidget(from, 1, 1);
    v->addLayout(grid);
    auto* group = new QButtonGroup(page);
    for (int k = 1; k <= track::kWaybillSlots; ++k) {
        const auto& l = w.legs[static_cast<size_t>(k - 1)];
        auto* box = card(QStringLiteral("opsLeg%1").arg(k), page);
        auto* col = columnOf(box);
        auto* now = new QRadioButton(slot == k ? tr("Leg %1 · now").arg(k) : tr("Leg %1").arg(k), box);
        now->setObjectName(QStringLiteral("opsLegNow"));
        now->setChecked(slot == k);
        now->setEnabled(!readOnly_ && l.has_value());
        group->addButton(now);
        connect(now, &QRadioButton::clicked, this, [save, k] { save({ { QStringLiteral("leg"), k } }); });
        col->addWidget(now);
        auto* to = placeBox(view_, l ? l->to : QString(), tr("Not used"), QStringLiteral("opsLegTo"), box);
        to->setAccessibleName(tr("Leg %1 goes to").arg(k));
        to->setEnabled(!readOnly_);
        connect(to, &QComboBox::activated, this, [to, save, k] { save({ { fieldOf("to", k), to->currentData().toString() }, { fieldOf("spot", k), QString() } }); });
        col->addWidget(to);
        if (l) {
            auto* g = new QGridLayout;
            g->setColumnStretch(1, 1);
            g->addWidget(note(tr("Spot"), box), 0, 0);
            auto* spot = new QComboBox(box);
            spot->setObjectName(QStringLiteral("opsLegSpot"));
            spot->setAccessibleName(tr("Spot"));
            spot->addItem(tr("Any"), QString());
            if (const auto* p = view_.place(l->to))
                for (const auto& s : p->place.value(QLatin1String("spots")).toArray()) {
                    const QString name = s.toObject().value(QLatin1String("name")).toString();
                    spot->addItem(name.isEmpty() ? tr("A spot") : name, s.toObject().value(QLatin1String("id")).toString());
                }
            if (!l->spot.isEmpty() && spot->findData(l->spot) < 0) spot->addItem(tr("One that was deleted (any)"), l->spot);
            spot->setCurrentIndex(std::max(0, spot->findData(l->spot)));
            spot->setEnabled(!readOnly_);
            connect(spot, &QComboBox::activated, this, [spot, save, k] { save({ { fieldOf("spot", k), spot->currentData().toString() } }); });
            g->addWidget(spot, 0, 1);
            g->addWidget(note(tr("Load"), box), 1, 0);
            auto* load = new QComboBox(box);
            load->setObjectName(QStringLiteral("opsLegLoad"));
            load->setAccessibleName(tr("Load"));
            for (const char* key : { "", "empty", "loaded" }) load->addItem(OpsText::load(QLatin1String(key)), QString::fromLatin1(key));
            load->setCurrentIndex(std::max(0, load->findData(l->load)));
            load->setEnabled(!readOnly_);
            connect(load, &QComboBox::activated, this, [load, save, k] { save({ { fieldOf("load", k), load->currentData().toString() } }); });
            g->addWidget(load, 1, 1);
            g->addWidget(note(tr("Note"), box), 2, 0);
            auto* text = new QLineEdit(l->note, box);
            text->setObjectName(QStringLiteral("opsLegNote%1").arg(k));
            text->setAccessibleName(tr("Note"));
            text->setMaxLength(track::kMaxLegNote);
            text->setReadOnly(readOnly_);
            connect(text, &QLineEdit::editingFinished, this, [text, save, k, was = l->note] {
                const QString t = text->text().trimmed();
                if (t != was) save({ { fieldOf("note", k), t } });
            });
            g->addWidget(text, 2, 1);
            col->addLayout(g);
        }
        v->addWidget(box);
    }
    if (readOnly_) return;
    auto* row = new QHBoxLayout;
    if (!car.isEmpty() && slot > 0) {
        const auto* p = view_.place(view_.to.value(car).place);
        const bool leaves = p && p->place.value(QLatin1String("spots")).toArray().isEmpty() && !view_.routing.arrived.contains(car);
        auto* b = button(leaves ? tr("It has left the layout") : tr("Delivered"), QStringLiteral("opsDelivered"), page);
        b->setToolTip(tr("The waybill turns to its next leg, and the load with it."));
        connect(b, &QPushButton::clicked, this, [this, car] { delivered(car); });
        row->addWidget(b);
    }
    if (slot == 0 && std::any_of(w.legs.begin(), w.legs.end(), [](const auto& l) { return l.has_value(); })) {
        auto* b = button(tr("Start it again"), QStringLiteral("opsRestart"), page);
        connect(b, &QPushButton::clicked, this, [this, id] {
            if (map_) change(edit::restartedWaybill(*map_, id), tr("Start a waybill again"));
        });
        row->addWidget(b);
    }
    auto* del = button(tr("Delete this waybill"), QStringLiteral("opsWaybillDelete"), page);
    connect(del, &QPushButton::clicked, this, &LiveOpsPanel::removeOpen);
    row->addWidget(del);
    v->addLayout(row);
}

void LiveOpsPanel::buildJobs(QVBoxLayout* v) {
    QWidget* page = v->parentWidget();
    if (!readOnly_) {
        auto* fresh = button(tr("New job"), QStringLiteral("opsNewJob"), page);
        fresh->setEnabled(view_.jobs.size() < track::kMaxJobs);
        connect(fresh, &QPushButton::clicked, this, &LiveOpsPanel::addJob);
        v->addWidget(fresh);
    }
    v->addWidget(help::withHelp(bold(tr("Jobs"), page), QStringLiteral("ops.job"), page));
    if (view_.jobs.empty())
        v->addWidget(note(tr("No jobs yet. A job is one crew’s work: the places it stops at, in order. Cars with a waybill are given to the first job that can take them."), page, QStringLiteral("opsNoJobs")));
    for (size_t i = 0; i < view_.jobs.size(); ++i) {
        const auto& j = view_.jobs[i];
        auto* row = card(QStringLiteral("opsJob_") + j.id, page);
        auto* head = new QHBoxLayout;
        auto* open = button(j.name.isEmpty() ? tr("A job") : j.name, QStringLiteral("opsOpenJob"), row);
        connect(open, &QPushButton::clicked, this, [this, id = j.id] { openJob(id); });
        head->addWidget(open, 1);
        if (!readOnly_)
            for (const int d : { -1, 1 }) {
                auto* b = button(d < 0 ? QStringLiteral("↑") : QStringLiteral("↓"), d < 0 ? QStringLiteral("opsUp") : QStringLiteral("opsDown"), row);
                b->setAccessibleName((d < 0 ? tr("Move %1 up") : tr("Move %1 down")).arg(j.name));
                b->setEnabled(d < 0 ? i > 0 : i + 1 < view_.jobs.size());
                connect(b, &QPushButton::clicked, this, [this, id = j.id, d] {
                    if (map_) change(edit::movedJob(*map_, id, d), tr("Move job"));
                });
                head->addWidget(b);
            }
        columnOf(row)->addLayout(head);
        columnOf(row)->addWidget(note(QStringLiteral("%1 · %2 · %3").arg(OpsText::jobKind(j.kind), OpsText::stops(static_cast<int>(j.stops.size())), OpsText::movesText(moves(view_.routing.listOf(j.id)))), row,
                                      QStringLiteral("opsJobMoves")));
        v->addWidget(row);
    }
}

void LiveOpsPanel::buildJob(QVBoxLayout* v, const track::Job& j) {
    QWidget* page = v->parentWidget();
    const QString id = j.id;
    const auto save = [this, id](const QJsonObject& fields) {
        if (map_) change(edit::changedJob(*map_, id, fields), tr("Change job"));
    };
    auto* back = button(tr("← All jobs"), QStringLiteral("opsBack"), page);
    connect(back, &QPushButton::clicked, this, [this] {
        job_.clear();
        later();
    });
    v->addWidget(help::withHelp(back, QStringLiteral("ops.job"), page));
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    grid->addWidget(note(tr("Name"), page), 0, 0);
    auto* name = new QLineEdit(j.name, page);
    name->setObjectName(QStringLiteral("opsJobName"));
    name->setAccessibleName(tr("Name"));
    name->setMaxLength(track::kMaxJobName);
    name->setReadOnly(readOnly_);
    connect(name, &QLineEdit::editingFinished, this, [name, save, was = j.name] {
        const QString t = name->text().trimmed();
        if (!t.isEmpty() && t != was) save({ { QStringLiteral("name"), t } });
    });
    grid->addWidget(name, 0, 1);
    grid->addWidget(note(tr("Kind"), page), 1, 0);
    auto* kind = new QComboBox(page);
    kind->setObjectName(QStringLiteral("opsJobKind"));
    kind->setAccessibleName(tr("Kind"));
    for (const QString& k : track::jobKinds()) kind->addItem(OpsText::jobKind(k), k);
    if (kind->findData(j.kind) < 0) kind->addItem(j.kind, j.kind);
    kind->setCurrentIndex(kind->findData(j.kind));
    kind->setEnabled(!readOnly_);
    connect(kind, &QComboBox::activated, this, [kind, save] { save({ { QStringLiteral("kind"), kind->currentData().toString() } }); });
    grid->addWidget(kind, 1, 1);
    grid->addWidget(note(tr("Train"), page), 2, 0);
    auto* train = new QComboBox(page);
    train->setObjectName(QStringLiteral("opsJobTrain"));
    train->setAccessibleName(tr("Train"));
    train->addItem(tr("None yet"), QString());
    for (const auto& t : view_.trains)
        if (!t.cut) train->addItem(t.name.isEmpty() ? tr("Train") : t.name, t.id);
    if (!j.train.isEmpty() && train->findData(j.train) < 0) train->addItem(tr("A train that was deleted"), j.train);
    train->setCurrentIndex(std::max(0, train->findData(j.train)));
    train->setEnabled(!readOnly_);
    connect(train, &QComboBox::activated, this, [train, save] { save({ { QStringLiteral("train"), train->currentData().toString() } }); });
    grid->addWidget(train, 2, 1);
    v->addLayout(grid);
    if (const QString hint = OpsText::jobKindHint(j.kind); !hint.isEmpty()) v->addWidget(note(hint, page));

    v->addWidget(bold(tr("Stops"), page));
    QStringList places;
    for (const auto& s : j.stops) places << s.place;
    const auto broken = track::brokenStops(places, view_.opsPlaces);
    for (int i = 0; i < static_cast<int>(j.stops.size()); ++i) {
        const auto& s = j.stops[static_cast<size_t>(i)];
        const QString where = OpsText::placeName(view_, s.place);
        auto* row = new QHBoxLayout;
        row->addWidget(note(QStringLiteral("%1. %2").arg(i + 1).arg(where), page, QStringLiteral("opsStop")), 1);
        if (!readOnly_) {
            for (const int d : { -1, 1 }) {
                auto* b = button(d < 0 ? QStringLiteral("↑") : QStringLiteral("↓"), d < 0 ? QStringLiteral("opsUp") : QStringLiteral("opsDown"), page);
                b->setAccessibleName((d < 0 ? tr("Move %1 up") : tr("Move %1 down")).arg(where));
                b->setEnabled(d < 0 ? i > 0 : i + 1 < static_cast<int>(j.stops.size()));
                connect(b, &QPushButton::clicked, this, [this, id, stop = s.id, d] {
                    if (map_) change(edit::movedStop(*map_, id, stop, d), tr("Move stop"));
                });
                row->addWidget(b);
            }
            auto* gone = button(QStringLiteral("×"), QStringLiteral("opsStopRemove"), page);
            gone->setAccessibleName(tr("Remove %1").arg(where));
            connect(gone, &QPushButton::clicked, this, [this, id, stop = s.id] {
                if (map_) change(edit::removedStop(*map_, id, stop), tr("Remove stop"));
            });
            row->addWidget(gone);
        }
        v->addLayout(row);
        if (broken.contains(i)) v->addWidget(note(tr("No track joins the stop before to %1.").arg(where), page, QStringLiteral("opsStopBroken")));
    }
    if (!readOnly_ && j.stops.size() < track::kMaxStops) {
        auto* add = placeBox(view_, QString(), j.stops.empty() ? tr("Add its first stop…") : tr("Add a stop…"), QStringLiteral("opsAddStop"), page);
        add->setAccessibleName(tr("Add a stop"));
        connect(add, &QComboBox::activated, this, [this, add, id] {
            const QString place = add->currentData().toString();
            if (map_ && !place.isEmpty()) change(edit::addedStop(*map_, id, place), tr("Add stop"));
        });
        v->addWidget(add);
    }

    auto* box = card(QStringLiteral("opsSwitchList"), page);
    auto* col = columnOf(box);
    col->addWidget(help::withHelp(bold(tr("Switch list"), box), QStringLiteral("ops.switchList"), box));
    const auto* list = view_.routing.listOf(id);
    if (moves(list) == 0) {
        col->addWidget(note(j.stops.empty() ? tr("Add the places it stops at.") : tr("Nothing to do: no car goes by this job now."), box, QStringLiteral("opsListEmpty")));
    } else {
        const auto line = [&](const QString& car, const QString& text) {
            auto* row = new QHBoxLayout;
            row->setContentsMargins(12, 0, 0, 0);
            row->addWidget(note(text, box, QStringLiteral("opsListLine")), 1);
            if (view_.points.contains(car)) {
                auto* b = button(tr("Show me"), QStringLiteral("opsShow"), box);
                b->setFlat(true);
                connect(b, &QPushButton::clicked, this, [this, at = view_.points.value(car)] { emit showSpotRequested(at); });
                row->addWidget(b);
            }
            col->addLayout(row);
        };
        for (int i = 0; i < static_cast<int>(list->size()); ++i) {
            const auto& s = (*list)[static_cast<size_t>(i)];
            if (s.setOut.empty() && s.pickUp.empty()) continue;
            col->addWidget(bold(QStringLiteral("%1. %2").arg(i + 1).arg(OpsText::placeName(view_, s.place)), box));
            for (const auto& m : s.setOut) {
                QString spot;
                if (const auto* p = view_.place(s.place))
                    for (const auto& x : p->place.value(QLatin1String("spots")).toArray())
                        if (!m.spot.isEmpty() && x.toObject().value(QLatin1String("id")).toString() == m.spot) {
                            spot = x.toObject().value(QLatin1String("name")).toString();
                            if (spot.isEmpty()) spot = tr("a spot");
                        }
                const QString car = OpsText::carName(view_, m.car);
                line(m.car, spot.isEmpty() ? tr("Set out %1").arg(car) : tr("Set out %1 at %2").arg(car, spot));
            }
            for (const auto& m : s.pickUp) line(m.car, tr("Pick up %1 for %2").arg(OpsText::carName(view_, m.car), OpsText::placeName(view_, m.to)));
        }
    }
    v->addWidget(box);
    if (!readOnly_) {
        auto* del = button(tr("Delete this job"), QStringLiteral("opsJobDelete"), page);
        connect(del, &QPushButton::clicked, this, &LiveOpsPanel::removeOpen);
        v->addWidget(del);
    }
}

}  // namespace bld::ui
