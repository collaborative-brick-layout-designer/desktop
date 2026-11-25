#include "PlacesPanel.h"

#include "PlacesText.h"

#include "../core/Ids.h"
#include "../core/Map.h"
#include "../core/Places.h"
#include "../core/SharedModule.h"
#include "../rendering/SceneBuilder.h"
#include "help/HelpButton.h"
#include "tours/Tours.h"

#include <QComboBox>
#include <QCompleter>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {

namespace cp = core::places;

QString str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }
QJsonArray spotsOf(const QJsonObject& place) { return place.value(QLatin1String("spots")).toArray(); }

QLabel* note(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    return l;
}

// A notes box that says when it is left.
class NotesEdit : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    std::function<void(const QString&)> left;

protected:
    void focusOutEvent(QFocusEvent* e) override {
        QPlainTextEdit::focusOutEvent(e);
        if (left) left(toPlainText());
    }
};

}  // namespace

PlacesPanel::PlacesPanel(parts::PartsLibrary& parts, QWidget* parent) : QDockWidget(tr("Places"), parent), parts_(parts) {
    setObjectName(QStringLiteral("dock.places"));
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    setWidget(scroll_);
    confirm_ = [this](const QString& name) {
        return QMessageBox::question(this, tr("Delete place"), tr("Delete the place “%1”? The track stays. You can undo this.").arg(name)) == QMessageBox::Yes;
    };
    rebuild();
}

void PlacesPanel::setMap(const core::Map* map) {
    map_ = map;
    later();
}

void PlacesPanel::setReadOnly(bool readOnly) {
    if (readOnly_ == readOnly) return;
    readOnly_ = readOnly;
    later();
}

void PlacesPanel::setPicking(bool picking) {
    picking_ = picking;
    later();
}

void PlacesPanel::showModule(const QString& moduleId) {
    module_ = moduleId;
    openId_.clear();
    later();
}

void PlacesPanel::openPlace(const QString& owner, const QString& id, bool fresh) {
    openOwner_ = owner;
    openId_ = id;
    fresh_ = fresh;
    later();
}

// Not while a field's own signal is being handled: it would be deleted under it.
void PlacesPanel::later() {
    if (waiting_) return;
    waiting_ = true;
    QTimer::singleShot(0, this, [this] {
        if (waiting_) rebuild();
    });
}

void PlacesPanel::drawNow() { rebuild(); }

std::vector<PlacesPanel::Group> PlacesPanel::groups() const {
    std::vector<Group> out;
    if (!map_) return out;
    out.push_back({ QString(), QString(), false, map_->sidecar.layoutPlaces });
    for (const auto& m : map_->sidecar.modules)
        out.push_back({ m.id, m.name.isEmpty() ? tr("Module") : m.name, core::isSharedModule(m), m.places });
    return out;
}

std::optional<std::pair<PlacesPanel::Group, QJsonObject>> PlacesPanel::open() const {
    if (openId_.isEmpty()) return std::nullopt;
    for (const auto& g : groups()) {
        if (g.owner != openOwner_) continue;
        const int i = cp::indexOf(g.places, openId_);
        if (i >= 0) return std::pair(g, g.places.at(i).toObject());
    }
    return std::nullopt;
}

track::SpotNow PlacesPanel::now(const QJsonObject& spot) const { return track::spotNow(map_ ? &net_ : nullptr, runs_, spot); }

QStringList PlacesPanel::carTypes() const {
    QStringList out = cp::carTypes();
    if (map_)
        for (const auto& s : map_->sidecar.rollingStock)
            if (!s.type.trimmed().isEmpty() && std::none_of(out.begin(), out.end(), [&](const QString& t) { return cp::sameCarType(t, s.type); }))
                out << s.type.trimmed();
    return out;
}

void PlacesPanel::change(const std::function<void(QJsonObject&)>& f, const QString& what) {
    const auto here = open();
    if (!here || readOnly_ || here->first.locked) return;
    QJsonObject after = here->second;
    f(after);
    const auto kept = cp::readPlace(after);
    if (!kept || *kept == here->second) return;
    emit placeEdited(openOwner_, openId_, kept, what);
}

void PlacesPanel::changeSpot(const QString& spotId, const std::function<void(QJsonObject&)>& f, const QString& what) {
    change(
        [&](QJsonObject& place) {
            QJsonArray spots = spotsOf(place);
            for (qsizetype i = 0; i < spots.size(); ++i) {
                QJsonObject s = spots.at(i).toObject();
                if (str(s, "id") != spotId) continue;
                f(s);
                spots.replace(i, s);
            }
            place.insert(QStringLiteral("spots"), spots);
        },
        what);
}

void PlacesPanel::newPlace() { emit pickRequested(module_.isEmpty() ? std::nullopt : std::optional(module_), QString(), QString()); }

void PlacesPanel::newPlaceOffLayout() {
    if (!map_ || readOnly_) return;
    QStringList names;
    for (const auto& g : groups())
        for (const auto& p : g.places) names << str(p.toObject(), "name");
    int n = 1;
    while (names.contains(tr("Place %1").arg(n))) ++n;
    const QJsonObject place{ { QStringLiteral("id"), core::newBbmId() },     { QStringLiteral("name"), tr("Place %1").arg(n) },
                             { QStringLiteral("kind"), QStringLiteral("staging") }, { QStringLiteral("spots"), QJsonArray() },
                             { QStringLiteral("accepts"), QJsonArray() },    { QStringLiteral("ships"), QJsonArray() },
                             { QStringLiteral("tags"), QJsonArray() },       { QStringLiteral("notes"), QString() } };
    openPlace(module_, str(place, "id"), true);
    emit placeEdited(module_, str(place, "id"), place, tr("New place"));
}

void PlacesPanel::pickTrack(const QString& spotId) {
    if (open()) emit pickRequested(openOwner_, openId_, spotId);
}

void PlacesPanel::removeOpen() {
    const auto here = open();
    if (!here || readOnly_ || here->first.locked || !confirm_(str(here->second, "name"))) return;
    const QString owner = openOwner_;
    const QString id = openId_;
    openId_.clear();
    emit placeEdited(owner, id, std::nullopt, tr("Delete place"));
}

void PlacesPanel::rebuild() {
    waiting_ = false;
    net_ = map_ ? track::trackNetworkOf(*map_, parts_) : track::TrackNetwork();
    runs_ = track::trackRuns(net_);
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
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(8, 8, 8, 8);
    const auto here = open();
    if (!here) openId_.clear();
    if (here) buildEditor(v, here->first, here->second);
    else buildList(v);
    v->addStretch(1);
    if (QWidget* old = scroll_->takeWidget()) {
        old->blockSignals(true);
        const auto fields = old->findChildren<QWidget*>();
        for (QWidget* w : fields) w->blockSignals(true);
        for (QWidget* w : fields)
            if (auto* n = dynamic_cast<NotesEdit*>(w)) n->left = nullptr;
        old->deleteLater();
    }
    scroll_->setWidget(body);
    scroll_->verticalScrollBar()->setValue(at);
    if (here && fresh_ && !readOnly_) {
        fresh_ = false;
        if (auto* name = body->findChild<QLineEdit*>(QStringLiteral("placeName"))) {
            name->setFocus();
            name->selectAll();
        }
    } else if (!focused.isEmpty()) {
        if (auto* w = body->findChild<QWidget*>(focused)) {
            w->setFocus();
            if (auto* line = qobject_cast<QLineEdit*>(w); line && typedIn) {
                line->setText(typed);
                line->setModified(true);
                line->setCursorPosition(cursor);
            }
        }
    }
}

void PlacesPanel::buildList(QVBoxLayout* v) {
    const auto all = groups();
    const bool hasTrack = !net_.brickIds().isEmpty();
    const Group* asked = nullptr;
    for (const auto& g : all)
        if (!module_.isEmpty() && g.owner == module_) asked = &g;
    // The module asked for is gone (deleted, or undone): every place again.
    if (!asked) module_.clear();
    QWidget* body = v->parentWidget();
    if (asked) {
        auto* row = new QHBoxLayout;
        row->addWidget(note(tr("Places on %1").arg(asked->name), body), 1);
        auto* every = new QPushButton(tr("Show all"), body);
        every->setObjectName(QStringLiteral("placesShowAll"));
        connect(every, &QPushButton::clicked, this, [this] { showModule(QString()); });
        row->addWidget(every);
        v->addLayout(row);
    }
    const QJsonArray own = asked ? asked->places : (all.empty() ? QJsonArray() : all.front().places);
    const bool room = own.size() < cp::kMaxPlaces;
    if (!readOnly_ && map_ && !(asked && asked->locked)) {
        auto* row = new QHBoxLayout;
        auto* add = new QPushButton(tr("New place"), body);
        add->setObjectName(QStringLiteral("placesNew"));
        tours::tag(add, QStringLiteral("places.new"));
        add->setEnabled(hasTrack && room && !picking_);
        connect(add, &QPushButton::clicked, this, &PlacesPanel::newPlace);
        row->addWidget(help::withHelp(add, QStringLiteral("ops.places"), body));
        auto* off = new QPushButton(tr("New place off the layout"), body);
        off->setObjectName(QStringLiteral("placesNewOff"));
        off->setEnabled(room && !picking_);
        connect(off, &QPushButton::clicked, this, &PlacesPanel::newPlaceOffLayout);
        row->addWidget(off);
        row->addStretch(1);
        v->addLayout(row);
    }
    if (asked && asked->locked) v->addWidget(note(tr("This module is shared with you, so its places are its owner’s."), body));
    qsizetype total = 0;
    for (const auto& g : all) total += g.places.size();
    QString empty;
    if (total == 0)
        empty = readOnly_ ? tr("No places on this layout yet.")
                : hasTrack
                    ? tr("No places yet. A place is where cars go: an industry, a yard, staging or an interchange. Press New place, then click the track where the cars stand.")
                    : tr("There’s no track on this layout yet. Add some from the Parts panel’s Track group, then name the places along it.");
    else if (asked && asked->places.isEmpty())
        empty = readOnly_ || asked->locked ? tr("No places on this module yet.")
                                           : tr("No places on this module yet. Press New place, then click the module’s track where the cars stand.");
    if (!empty.isEmpty()) {
        auto* e = note(empty, body);
        e->setObjectName(QStringLiteral("placesEmpty"));
        v->addWidget(e);
        return;
    }
    for (const auto& g : all) {
        if (g.places.isEmpty() || (asked && g.owner != asked->owner)) continue;
        auto* head = note(g.owner.isEmpty() ? tr("This layout") : g.locked ? tr("%1 (shared with you)").arg(g.name) : g.name, body);
        head->setStyleSheet(QStringLiteral("font-weight: 600;"));
        v->addWidget(head);
        for (const auto& pv : g.places) {
            const QJsonObject p = pv.toObject();
            auto* row = new QFrame(body);
            row->setObjectName(QStringLiteral("place_") + str(p, "id"));
            row->setFrameShape(QFrame::StyledPanel);
            auto* rv = new QVBoxLayout(row);
            rv->setContentsMargins(8, 6, 8, 6);
            auto* top = new QHBoxLayout;
            auto* dot = new QLabel(row);
            dot->setFixedSize(12, 12);
            dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 6px;").arg(rendering::placeColor(str(p, "kind")).name()));
            top->addWidget(dot);
            auto* name = new QPushButton(str(p, "name").isEmpty() ? tr("(no name)") : str(p, "name"), row);
            name->setObjectName(QStringLiteral("placeOpen"));
            name->setFlat(true);
            name->setStyleSheet(QStringLiteral("text-align: left; font-weight: 600;"));
            connect(name, &QPushButton::clicked, this, [this, owner = g.owner, id = str(p, "id")] { openPlace(owner, id, false); });
            top->addWidget(name, 1);
            int cars = 0;
            int lost = 0;
            bool known = true;
            std::optional<QPointF> first;
            const QJsonArray spots = spotsOf(p);
            for (const auto& sv : spots) {
                const track::SpotNow s = now(sv.toObject());
                if (s.at && !first) first = s.at->middle;
                if (!s.at && sv.toObject().contains(QLatin1String("track"))) ++lost;
                if (s.capacity) cars += *s.capacity;
                else known = false;
            }
            if (first) {
                auto* show = new QPushButton(tr("Show me"), row);
                show->setObjectName(QStringLiteral("placeShow"));
                connect(show, &QPushButton::clicked, this, [this, at = *first] { emit showSpotRequested(at); });
                top->addWidget(show);
            }
            rv->addLayout(top);
            QStringList line{ PlacesText::kind(str(p, "kind")) };
            if (!spots.isEmpty()) line << (spots.size() == 1 ? tr("1 spot") : tr("%1 spots").arg(spots.size()));
            if (!spots.isEmpty() && known) line << PlacesText::capacityOf(cars);
            if (lost > 0) line << (lost == 1 ? tr("1 spot has lost its track") : tr("%1 spots have lost their track").arg(lost));
            auto* info = note(line.join(QStringLiteral(" · ")), row);
            info->setObjectName(QStringLiteral("placeInfo"));
            rv->addWidget(info);
            v->addWidget(row);
        }
    }
}

void PlacesPanel::buildEditor(QVBoxLayout* v, const Group& group, const QJsonObject& place) {
    QWidget* body = v->parentWidget();
    const bool locked = readOnly_ || group.locked;
    auto* top = new QHBoxLayout;
    auto* back = new QPushButton(tr("← All places"), body);
    back->setObjectName(QStringLiteral("placeBack"));
    connect(back, &QPushButton::clicked, this, [this] {
        openId_.clear();
        later();
    });
    top->addWidget(back);
    top->addWidget(note(group.owner.isEmpty() ? tr("On this layout") : tr("On %1").arg(group.name), body), 1);
    v->addLayout(top);
    if (group.locked && !readOnly_) v->addWidget(note(tr("This module is shared with you, so its places are its owner’s."), body));

    const auto field = [&](QVBoxLayout* into, const QString& label, QWidget* w) {
        auto* l = new QLabel(label, body);
        l->setBuddy(w);
        l->setStyleSheet(QStringLiteral("font-weight: 600;"));
        into->addWidget(l);
        into->addWidget(w);
    };
    const auto line = [&](const QString& object, const QString& text, int max, const std::function<void(const QString&)>& save) {
        auto* e = new QLineEdit(text, body);
        e->setObjectName(object);
        e->setMaxLength(max);
        e->setReadOnly(locked);
        connect(e, &QLineEdit::editingFinished, this, [e, text, save] {
            const QString t = e->text().trimmed();
            if (t != text) save(t);
        });
        return e;
    };

    field(v, tr("Name"), line(QStringLiteral("placeName"), str(place, "name"), cp::kMaxName, [this](const QString& t) {
              if (!t.isEmpty()) change([&](QJsonObject& p) { p.insert(QStringLiteral("name"), t); }, tr("Rename place"));
              else later();
          }));

    auto* kind = new QComboBox(body);
    kind->setObjectName(QStringLiteral("placeKind"));
    QStringList kinds = cp::kinds();
    if (!kinds.contains(str(place, "kind"))) kinds << str(place, "kind");
    for (const QString& k : std::as_const(kinds)) kind->addItem(PlacesText::kind(k), k);
    kind->setCurrentIndex(static_cast<int>(kinds.indexOf(str(place, "kind"))));
    kind->setEnabled(!locked);
    connect(kind, &QComboBox::activated, this, [this, kind] {
        const QString k = kind->currentData().toString();
        change([&](QJsonObject& p) { p.insert(QStringLiteral("kind"), k); }, tr("Change a place"));
    });
    field(v, tr("Kind"), kind);
    if (const QString hint = PlacesText::kindHint(str(place, "kind")); !hint.isEmpty()) v->addWidget(note(hint, body));

    auto* spotsHead = new QLabel(tr("Spots"), body);
    spotsHead->setStyleSheet(QStringLiteral("font-weight: 600;"));
    v->addWidget(help::withHelp(spotsHead, QStringLiteral("ops.spot"), body));
    const QJsonArray spots = spotsOf(place);
    if (spots.isEmpty()) v->addWidget(note(tr("No spots: cars that go here leave the layout."), body));
    for (const auto& sv : spots) {
        const QJsonObject s = sv.toObject();
        const QString sid = str(s, "id");
        auto* box = new QFrame(body);
        box->setObjectName(QStringLiteral("spot_") + sid);
        box->setFrameShape(QFrame::StyledPanel);
        auto* bv = new QVBoxLayout(box);
        bv->setContentsMargins(8, 6, 8, 6);
        auto* name = line(QStringLiteral("spotName_") + sid, str(s, "name"), cp::kMaxName, [this, sid](const QString& t) {
            changeSpot(sid, [&](QJsonObject& x) { x.insert(QStringLiteral("name"), t); }, tr("Rename spot"));
        });
        field(bv, tr("Spot"), name);
        auto* cars = new QSpinBox(box);
        cars->setObjectName(QStringLiteral("spotCars_") + sid);
        cars->setRange(0, cp::kMaxCapacity);
        cars->setSpecialValueText(tr("From the track"));
        cars->setKeyboardTracking(false);
        cars->setValue(s.value(QLatin1String("capacity")).toInt(0));
        cars->setEnabled(!locked);
        connect(cars, &QSpinBox::valueChanged, this, [this, sid](int n) {
            changeSpot(
                sid,
                [&](QJsonObject& x) {
                    if (n > 0) x.insert(QStringLiteral("capacity"), n);
                    else x.remove(QStringLiteral("capacity"));
                },
                tr("Change a spot"));
        });
        field(bv, tr("Cars it holds"), cars);
        if (str(place, "kind") == QLatin1String("yard")) {
            auto* role = new QComboBox(box);
            role->setObjectName(QStringLiteral("spotRole_") + sid);
            role->addItem(tr("Anything"), QString());
            QStringList roles = cp::yardRoles();
            if (const QString r = str(s, "role"); !r.isEmpty() && !roles.contains(r)) roles << r;
            for (const QString& r : std::as_const(roles)) role->addItem(PlacesText::role(r), r);
            role->setCurrentIndex(std::max(0, role->findData(str(s, "role"))));
            role->setEnabled(!locked);
            connect(role, &QComboBox::activated, this, [this, role, sid] {
                const QString r = role->currentData().toString();
                changeSpot(
                    sid,
                    [&](QJsonObject& x) {
                        if (r.isEmpty()) x.remove(QStringLiteral("role"));
                        else x.insert(QStringLiteral("role"), r);
                    },
                    tr("Change a spot"));
            });
            field(bv, tr("Used for"), role);
        }
        const track::SpotNow where = now(s);
        QString info;
        if (!s.contains(QLatin1String("track"))) info = tr("No track picked yet.");
        else if (!where.at) info = tr("Its track was moved or removed. Pick it again.");
        else if (where.capacity)
            info = s.contains(QLatin1String("capacity")) ? PlacesText::capacityOf(*where.capacity)
                                                         : tr("%1 (from the track’s length)").arg(PlacesText::capacityOf(*where.capacity));
        else info = tr("This is a switch: say how many cars it holds, or pick track beside it.");
        auto* says = note(info, box);
        says->setObjectName(QStringLiteral("spotInfo_") + sid);
        if (!where.at) says->setStyleSheet(QStringLiteral("color: #B45309;"));
        bv->addWidget(says);
        auto* acts = new QHBoxLayout;
        if (where.at) {
            auto* show = new QPushButton(tr("Show me"), box);
            connect(show, &QPushButton::clicked, this, [this, at = where.at->middle] { emit showSpotRequested(at); });
            acts->addWidget(show);
        }
        if (!locked) {
            auto* pick = new QPushButton(tr("Pick its track"), box);
            pick->setObjectName(QStringLiteral("spotPick_") + sid);
            pick->setEnabled(!picking_);
            connect(pick, &QPushButton::clicked, this, [this, sid] { pickTrack(sid); });
            acts->addWidget(pick);
            auto* del = new QPushButton(tr("Remove"), box);
            del->setObjectName(QStringLiteral("spotRemove_") + sid);
            connect(del, &QPushButton::clicked, this, [this, sid] {
                change(
                    [&](QJsonObject& p) {
                        QJsonArray left;
                        const QJsonArray had = spotsOf(p);
                        for (const auto& x : had)
                            if (str(x.toObject(), "id") != sid) left.append(x);
                        p.insert(QStringLiteral("spots"), left);
                    },
                    tr("Remove a spot"));
            });
            acts->addWidget(del);
        }
        acts->addStretch(1);
        bv->addLayout(acts);
        v->addWidget(box);
    }
    if (!locked && spots.size() < cp::kMaxSpots) {
        auto* add = new QPushButton(tr("Add a spot"), body);
        add->setObjectName(QStringLiteral("spotAdd"));
        add->setEnabled(!picking_);
        connect(add, &QPushButton::clicked, this, &PlacesPanel::addSpot);
        v->addWidget(add, 0, Qt::AlignLeft);
    }

    const auto words = [&](const QString& label, const QString& object, const char* key, bool suggest) {
        QStringList had;
        const QJsonArray list = place.value(QLatin1String(key)).toArray();
        for (const auto& w : list) had << w.toString();
        auto* e = new QLineEdit(had.join(QStringLiteral(", ")), body);
        e->setObjectName(object);
        e->setReadOnly(locked);
        e->setPlaceholderText(tr("Separate with commas"));
        if (suggest) {
            auto* c = new QCompleter(carTypes(), e);
            c->setCaseSensitivity(Qt::CaseInsensitive);
            e->setCompleter(c);
        }
        const QString k = QLatin1String(key);
        connect(e, &QLineEdit::editingFinished, this, [this, e, k, had] {
            QStringList next;
            const QStringList typed = e->text().split(QLatin1Char(','));
            for (const QString& t : typed) {
                const QString w = t.trimmed().left(cp::kMaxWord);
                if (!w.isEmpty() && next.size() < cp::kMaxWords && std::none_of(next.begin(), next.end(), [&](const QString& x) { return cp::sameCarType(x, w); }))
                    next << w;
            }
            if (next != had) change([&](QJsonObject& p) { p.insert(k, QJsonArray::fromStringList(next)); }, tr("Change a place"));
        });
        field(v, label, e);
    };
    words(tr("Takes in"), QStringLiteral("placeAccepts"), "accepts", true);
    words(tr("Sends out"), QStringLiteral("placeShips"), "ships", true);
    words(tr("Tags"), QStringLiteral("placeTags"), "tags", false);
    auto* notes = new NotesEdit(str(place, "notes"), body);
    notes->setObjectName(QStringLiteral("placeNotes"));
    notes->setReadOnly(locked);
    notes->setMaximumHeight(90);
    notes->setTabChangesFocus(true);
    notes->left = [this, was = str(place, "notes")](const QString& t) {
        if (t != was) change([&](QJsonObject& p) { p.insert(QStringLiteral("notes"), t); }, tr("Change a place"));
    };
    field(v, tr("Notes"), notes);
    if (!locked) {
        auto* del = new QPushButton(tr("Delete this place"), body);
        del->setObjectName(QStringLiteral("placeDelete"));
        connect(del, &QPushButton::clicked, this, &PlacesPanel::removeOpen);
        v->addWidget(del, 0, Qt::AlignLeft);
    }
}

}  // namespace bld::ui
