#include "TrainDialogs.h"

#include "../edit/venue/VenueUnits.h"
#include "../parts/PartsLibrary.h"
#include "../track/Trains.h"
#include "help/HelpButton.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bld::ui {

namespace {

const QStringList kKinds = { QStringLiteral("engine"), QStringLiteral("car"), QStringLiteral("cab"), QStringLiteral("unit"),
                             QStringLiteral("monorail") };

QDoubleSpinBox* studs(double min, double max, double value, QWidget* parent) {
    auto* b = new QDoubleSpinBox(parent);
    b->setRange(min, max);
    b->setSingleStep(0.5);
    b->setDecimals(2);
    b->setSuffix(QCoreApplication::translate("bld::ui::StockDialog", " studs"));
    b->setValue(value);
    return b;
}

void paintSwatch(QPushButton* b, const QString& color) {
    b->setStyleSheet(QStringLiteral("background: %1; min-width: 48px; border: 1px solid #888; border-radius: 4px;").arg(color));
}

QString partText(const parts::PartMetadata& m) {
    QString d;
    for (const auto& x : m.descriptions)
        if (x.language == QLatin1String("en") || d.isEmpty()) d = x.text;
    return m.partNumber + QLatin1Char(' ') + d;
}

}  // namespace

QString stockKindLabel(const QString& kind) {
    if (kind == QLatin1String("engine")) return StockDialog::tr("Engine");
    if (kind == QLatin1String("cab")) return StockDialog::tr("Cab car");
    if (kind == QLatin1String("unit")) return StockDialog::tr("Powered car (multiple unit)");
    if (kind == QLatin1String("monorail")) return StockDialog::tr("Monorail car");
    return StockDialog::tr("Car");
}

QString stockLabel(const core::RollingStock& s) {
    const QString l = QStringList{ s.number, s.name }.join(QLatin1Char(' ')).trimmed();
    return l.isEmpty() ? stockKindLabel(s.kind) : l;
}

core::RollingStock withLength(core::RollingStock s, double length) {
    const double k = s.length > 0 ? length / s.length : 1.0;
    s.length = length;
    s.bodyLength = std::max(1.0, length - 2.0);
    for (auto& t : s.trucks) t.at = std::round(t.at * k * 100.0) / 100.0;
    return s;
}

// ---- StockDialog ------------------------------------------------------------

StockDialog::StockDialog(const core::RollingStock& initial, bool isNew, parts::PartsLibrary& parts, QWidget* parent)
    : QDialog(parent), base_(initial), parts_(parts) {
    setObjectName(QStringLiteral("StockDialog"));
    setWindowTitle(isNew ? tr("New car") : tr("Change car"));
    auto* v = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("A quick car needs only a number and a length. More details can wait."), this);
    intro->setWordWrap(true);
    v->addWidget(help::withHelp(intro, QStringLiteral("trains.newStock"), this));
    auto* form = new QFormLayout;
    number_ = new QLineEdit(initial.number, this);
    number_->setObjectName(QStringLiteral("stockNumber"));
    number_->setPlaceholderText(QStringLiteral("4021"));
    form->addRow(tr("Number:"), number_);
    name_ = new QLineEdit(initial.name, this);
    name_->setObjectName(QStringLiteral("stockName"));
    name_->setPlaceholderText(tr("Boxcar"));
    form->addRow(tr("Name:"), name_);
    kind_ = new QComboBox(this);
    for (const QString& k : kKinds) kind_->addItem(stockKindLabel(k), k);
    kind_->setCurrentIndex(std::max(0, static_cast<int>(kKinds.indexOf(initial.kind))));
    form->addRow(tr("Kind:"), kind_);
    color_ = new QPushButton(this);
    color_->setAccessibleName(tr("Color"));
    colorName_ = initial.color;
    paintSwatch(color_, colorName_);
    connect(color_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor(colorName_), this, tr("Car color"));
        if (!c.isValid()) return;
        colorName_ = c.name().toUpper();
        paintSwatch(color_, colorName_);
    });
    form->addRow(tr("Color:"), color_);
    auto* lengthRow = new QHBoxLayout;
    length_ = studs(4, 400, initial.length, this);
    length_->setObjectName(QStringLiteral("stockLength"));
    length_->setToolTip(tr("Coupler to coupler"));
    lengthRow->addWidget(length_);
    lengthHint_ = new QLabel(this);
    lengthRow->addWidget(lengthHint_);
    form->addRow(tr("Length:"), lengthRow);
    v->addLayout(form);

    more_ = new QPushButton(tr("More details"), this);
    more_->setObjectName(QStringLiteral("stockMore"));
    more_->setCheckable(true);
    more_->setFlat(true);
    v->addWidget(more_, 0, Qt::AlignLeft);
    moreBox_ = new QWidget(this);
    moreBox_->setObjectName(QStringLiteral("stockMoreFields"));
    auto* mf = new QFormLayout(moreBox_);
    mf->setContentsMargins(0, 0, 0, 0);
    width_ = studs(1, 20, initial.width, moreBox_);
    mf->addRow(tr("Width:"), width_);
    body_ = studs(1, 400, initial.bodyLength, moreBox_);
    mf->addRow(tr("Body length:"), body_);
    std::vector<double> ats;
    ats.reserve(initial.trucks.size());
    for (const auto& t : initial.trucks) ats.push_back(t.at);
    std::sort(ats.begin(), ats.end(), std::greater<>());
    frontTruck_ = studs(0, 200, ats.empty() ? 8 : ats.front(), moreBox_);
    frontTruck_->setObjectName(QStringLiteral("stockFrontTruck"));
    rearTruck_ = studs(0, 200, ats.empty() ? 8 : -ats.back(), moreBox_);
    rearTruck_->setObjectName(QStringLiteral("stockRearTruck"));
    auto* trucksLabel = new QLabel(tr("Front truck, ahead of the middle:"), moreBox_);
    mf->addRow(help::withHelp(trucksLabel, QStringLiteral("trains.trucks"), moreBox_), frontTruck_);
    mf->addRow(tr("Rear truck, behind the middle:"), rearTruck_);
    const auto couplerBox = [this](const core::Coupler& c) {
        auto* b = new QComboBox(moreBox_);
        b->addItem(tr("Magnet"), QStringLiteral("magnet"));
        b->addItem(tr("Fixed bar"), QStringLiteral("fixed"));
        b->addItem(tr("None"), QStringLiteral("none"));
        b->setCurrentIndex(std::max(0, b->findData(c.type)));
        return b;
    };
    frontCoupler_ = couplerBox(initial.front);
    rearCoupler_ = couplerBox(initial.rear);
    mf->addRow(tr("Front coupler:"), frontCoupler_);
    mf->addRow(tr("Rear coupler:"), rearCoupler_);
    powered_ = new QCheckBox(tr("Has a motor"), moreBox_);
    powered_->setChecked(initial.powered);
    mf->addRow(QString(), powered_);
    runsOn_ = new QComboBox(moreBox_);
    runsOn_->addItem(tr("Train track"), QStringLiteral("rail"));
    runsOn_->addItem(tr("Monorail"), QStringLiteral("monorail"));
    runsOn_->setCurrentIndex(initial.trackKind == QLatin1String("monorail") ? 1 : 0);
    mf->addRow(tr("Runs on:"), runsOn_);
    lookSimple_ = new QRadioButton(tr("A box in its color"), moreBox_);
    lookPart_ = new QRadioButton(tr("A part’s picture"), moreBox_);
    (initial.lookMode == QLatin1String("simple") ? lookSimple_ : lookPart_)->setChecked(true);
    auto* looks = new QVBoxLayout;
    looks->addWidget(lookSimple_);
    looks->addWidget(lookPart_);
    mf->addRow(tr("Seen from above as:"), looks);
    partBox_ = new QWidget(moreBox_);
    auto* pv = new QVBoxLayout(partBox_);
    pv->setContentsMargins(0, 0, 0, 0);
    partSearch_ = new QLineEdit(partBox_);
    partSearch_->setPlaceholderText(tr("Search train parts"));
    pv->addWidget(partSearch_);
    partList_ = new QListWidget(partBox_);
    partList_->setMaximumHeight(140);
    pv->addWidget(partList_);
    auto* partNote = new QLabel(tr("Picking a part sets the length from its couplers."), partBox_);
    partNote->setWordWrap(true);
    pv->addWidget(partNote);
    mf->addRow(partBox_);
    partKey_ = initial.partKey;
    notes_ = new QPlainTextEdit(initial.notes, moreBox_);
    notes_->setMaximumHeight(60);
    mf->addRow(tr("Notes:"), notes_);
    moreBox_->hide();
    partBox_->setVisible(lookPart_->isChecked());
    v->addWidget(moreBox_);

    problem_ = new QLabel(tr("The front truck must be ahead of the rear one, and both within the car’s length."), this);
    problem_->setWordWrap(true);
    problem_->setStyleSheet(QStringLiteral("color: #B91C1C;"));
    v->addWidget(problem_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    save_ = buttons->addButton(isNew ? tr("Add car") : tr("Save"), QDialogButtonBox::AcceptRole);
    save_->setObjectName(QStringLiteral("stockSave"));
    save_->setDefault(true);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (valid()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(more_, &QPushButton::toggled, this, [this](bool on) {
        moreBox_->setVisible(on);
        more_->setText(on ? tr("Fewer details") : tr("More details"));
        if (on && partList_->count() == 0) fillParts(QString());
        adjustSize();
    });
    connect(length_, &QDoubleSpinBox::valueChanged, this, &StockDialog::lengthChanged);
    connect(frontTruck_, &QDoubleSpinBox::valueChanged, this, &StockDialog::check);
    connect(rearTruck_, &QDoubleSpinBox::valueChanged, this, &StockDialog::check);
    connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
        const QString k = kind_->currentData().toString();
        if (k == QLatin1String("engine") || k == QLatin1String("unit")) powered_->setChecked(true);
        if (k == QLatin1String("monorail")) runsOn_->setCurrentIndex(1);
    });
    connect(lookPart_, &QRadioButton::toggled, partBox_, &QWidget::setVisible);
    connect(partSearch_, &QLineEdit::textChanged, this, &StockDialog::fillParts);
    connect(partList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        partKey_ = it->data(Qt::UserRole).toString();
        if (const auto meta = parts_.metadata(partKey_)) {
            std::vector<std::pair<QString, QPointF>> conns;
            for (const auto& c : meta->connections) conns.push_back({ c.type, c.position });
            if (const auto len = track::partStockLength(conns)) length_->setValue(std::round(*len * 100.0) / 100.0);
        }
    });
    lengthChanged(length_->value());
}

void StockDialog::fillParts(const QString& query) {
    partList_->clear();
    const QString q = query.trimmed().toLower();
    int n = 0;
    for (const QString& key : parts_.keys()) {
        const auto meta = parts_.metadata(key);
        if (!meta.has_value()) continue;
        const parts::PartMetadata& m = meta.value();
        std::vector<std::pair<QString, QPointF>> conns;
        conns.reserve(static_cast<size_t>(m.connections.size()));
        for (const auto& c : m.connections) conns.push_back({ c.type, c.position });
        if (!track::partStockLength(conns)) continue;
        const QString text = partText(m);
        if (!q.isEmpty() && !text.toLower().contains(q)) continue;
        auto* it = new QListWidgetItem(text, partList_);
        it->setData(Qt::UserRole, key);
        if (key == partKey_) it->setSelected(true);
        if (++n >= 60) break;
    }
    if (n == 0) new QListWidgetItem(tr("No train parts match. Parts with a coupler at each end show here."), partList_);
}

void StockDialog::lengthChanged(double v) {
    // Simple mode: the trucks keep their share in from each end, the body two studs short.
    const double old = base_.length;
    if (old > 0 && std::abs(v - old) > 1e-9) {
        const double k = v / old;
        frontTruck_->blockSignals(true);
        rearTruck_->blockSignals(true);
        frontTruck_->setValue(frontTruck_->value() * k);
        rearTruck_->setValue(rearTruck_->value() * k);
        frontTruck_->blockSignals(false);
        rearTruck_->blockSignals(false);
        body_->setValue(std::max(1.0, v - 2.0));
        base_.length = v;
    }
    const auto unit = edit::venue::accountLengthUnit();
    lengthHint_->setText(unit == edit::venue::LengthUnit::Studs ? QString() : edit::venue::formatLength(v, unit));
    check();
}

bool StockDialog::valid() const {
    const double half = length_->value() / 2.0;
    return frontTruck_->value() + rearTruck_->value() > 0.01 && frontTruck_->value() <= half && rearTruck_->value() <= half;
}

void StockDialog::check() {
    problem_->setVisible(!valid());
    save_->setEnabled(valid());
}

core::RollingStock StockDialog::stock() const {
    core::RollingStock s = base_;
    s.number = number_->text().trimmed();
    s.name = name_->text().trimmed();
    s.kind = kind_->currentData().toString();
    s.color = colorName_;
    s.length = length_->value();
    s.width = width_->value();
    s.bodyLength = body_->value();
    double frontSwivel = 30.0;
    double rearSwivel = 30.0;
    if (s.trucks.size() >= 2) {
        frontSwivel = s.trucks.front().swivelMax;
        rearSwivel = s.trucks.back().swivelMax;
    }
    s.trucks = { { frontTruck_->value(), frontSwivel }, { -rearTruck_->value(), rearSwivel } };
    s.front.type = frontCoupler_->currentData().toString();
    s.rear.type = rearCoupler_->currentData().toString();
    s.powered = powered_->isChecked();
    s.trackKind = runsOn_->currentData().toString();
    s.lookMode = lookPart_->isChecked() ? QStringLiteral("part") : QStringLiteral("simple");
    s.partKey = partKey_;
    s.notes = notes_->toPlainText();
    return s;
}

// ---- TrainDialog ------------------------------------------------------------

TrainDialog::TrainDialog(const core::Train& initial, const std::vector<core::RollingStock>& stock, QWidget* parent)
    : QDialog(parent), t_(initial), stock_(stock) {
    setObjectName(QStringLiteral("TrainDialog"));
    setWindowTitle(initial.units.empty() ? tr("New train") : tr("Change train"));
    auto* v = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Cars in order, front first. A train moves as one."), this);
    intro->setWordWrap(true);
    v->addWidget(help::withHelp(intro, QStringLiteral("trains.newTrain"), this));
    auto* form = new QFormLayout;
    name_ = new QLineEdit(initial.name, this);
    name_->setObjectName(QStringLiteral("trainName"));
    form->addRow(tr("Name:"), name_);
    v->addLayout(form);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("trainUnits"));
    list_->setAccessibleName(tr("Cars in the train, front first"));
    v->addWidget(list_, 1);
    auto* rowButtons = new QHBoxLayout;
    auto* up = new QPushButton(tr("Forward"), this);
    auto* down = new QPushButton(tr("Back"), this);
    auto* turn = new QPushButton(tr("Turn car around"), this);
    auto* out = new QPushButton(tr("Take out"), this);
    for (auto* b : { up, down, turn, out }) rowButtons->addWidget(b);
    rowButtons->addStretch(1);
    v->addLayout(rowButtons);
    connect(up, &QPushButton::clicked, this, [this] { moveUnit(list_->currentRow(), -1); });
    connect(down, &QPushButton::clicked, this, [this] { moveUnit(list_->currentRow(), 1); });
    connect(turn, &QPushButton::clicked, this, [this] { turnUnit(list_->currentRow()); });
    connect(out, &QPushButton::clicked, this, [this] { removeUnit(list_->currentRow()); });
    auto* addRow = new QHBoxLayout;
    add_ = new QComboBox(this);
    add_->setObjectName(QStringLiteral("trainAddPick"));
    add_->setAccessibleName(tr("Car to add"));
    for (const auto& s : stock_) add_->addItem(stockLabel(s), s.id);
    addRow->addWidget(add_, 1);
    auto* addButton = new QPushButton(tr("Add to the end"), this);
    addButton->setObjectName(QStringLiteral("trainAddUnit"));
    addButton->setEnabled(!stock_.empty());
    addRow->addWidget(addButton);
    v->addLayout(addRow);
    if (stock_.empty()) {
        auto* none = new QLabel(tr("This layout has no cars yet. Make one in Rolling stock first."), this);
        none->setWordWrap(true);
        v->addWidget(none);
    }
    connect(addButton, &QPushButton::clicked, this, [this] { addUnit(add_->currentData().toString()); });
    length_ = new QLabel(this);
    v->addWidget(length_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    save_ = buttons->addButton(tr("Save train"), QDialogButtonBox::AcceptRole);
    save_->setObjectName(QStringLiteral("trainSave"));
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!t_.units.empty()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rebuild();
}

void TrainDialog::addUnit(const QString& stockId) {
    if (stockId.isEmpty()) return;
    t_.units.push_back({ stockId, false });
    rebuild();
    list_->setCurrentRow(list_->count() - 1);
}

void TrainDialog::moveUnit(int i, int by) {
    const int j = i + by;
    if (i < 0 || j < 0 || i >= static_cast<int>(t_.units.size()) || j >= static_cast<int>(t_.units.size())) return;
    std::swap(t_.units[static_cast<size_t>(i)], t_.units[static_cast<size_t>(j)]);
    rebuild();
    list_->setCurrentRow(j);
}

void TrainDialog::turnUnit(int i) {
    if (i < 0 || i >= static_cast<int>(t_.units.size())) return;
    t_.units[static_cast<size_t>(i)].reversed = !t_.units[static_cast<size_t>(i)].reversed;
    rebuild();
    list_->setCurrentRow(i);
}

void TrainDialog::removeUnit(int i) {
    if (i < 0 || i >= static_cast<int>(t_.units.size())) return;
    t_.units.erase(t_.units.begin() + i);
    rebuild();
}

void TrainDialog::rebuild() {
    list_->clear();
    track::StockById byId;
    for (const auto& s : stock_) byId.insert(s.id, s);
    int n = 0;
    for (const auto& u : t_.units) {
        const auto it = byId.constFind(u.stock);
        QString text = QStringLiteral("%1. ").arg(++n) + (it == byId.constEnd() ? tr("A car that was deleted") : stockLabel(*it));
        if (u.reversed) text += QStringLiteral(" · ") + tr("turned around");
        list_->addItem(text);
    }
    length_->setText(tr("Length %1").arg(edit::venue::formatLength(track::trainLength(t_, byId), edit::venue::accountLengthUnit())));
    save_->setEnabled(!t_.units.empty());
}

core::Train TrainDialog::train() const {
    core::Train t = t_;
    t.name = name_->text().trimmed().isEmpty() ? tr("Train") : name_->text().trimmed();
    return t;
}

// ---- RosterDialog -----------------------------------------------------------

RosterDialog::RosterDialog(const QList<sync::RosterStock>& entries, const std::vector<core::RollingStock>& stock, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("RosterDialog"));
    setWindowTitle(tr("From a roster"));
    auto* v = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Your engines and cars, and your clubs’. Add one to this layout; it keeps its own copy."), this);
    intro->setWordWrap(true);
    v->addWidget(help::withHelp(intro, QStringLiteral("trains.roster"), this));
    empty_ = new QLabel(tr("Your roster is empty. Save a car from a layout to your roster, or a club’s, to use it in other layouts."), this);
    empty_->setWordWrap(true);
    v->addWidget(empty_);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    body_ = new QWidget(scroll);
    new QVBoxLayout(body_);
    scroll->setWidget(body_);
    v->addWidget(scroll, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);
    resize(420, 360);
    setEntries(entries, stock);
}

void RosterDialog::setEntries(const QList<sync::RosterStock>& entries, const std::vector<core::RollingStock>& stock) {
    auto* lay = static_cast<QVBoxLayout*>(body_->layout());
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (QWidget* w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    empty_->setVisible(entries.isEmpty());
    for (const auto& e : entries) {
        auto* row = new QWidget(body_);
        row->setObjectName(QStringLiteral("roster_") + e.id);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 2, 0, 2);
        const QString name = QStringList{ e.number, e.name }.join(QLatin1Char(' ')).trimmed();
        auto* label = new QLabel((name.isEmpty() ? tr("Unnamed") : name) + QStringLiteral(" · ")
                                     + (e.owner.isClub() ? e.owner.name : tr("Yours")),
                                 row);
        h->addWidget(label, 1);
        const auto mine = std::find_if(stock.begin(), stock.end(), [&](const core::RollingStock& s) { return s.libraryId == e.id; });
        if (mine != stock.end() && mine->libraryVersion >= e.version) {
            h->addWidget(new QLabel(tr("In this layout"), row));
        } else {
            auto* b = new QPushButton(mine != stock.end() ? tr("Update") : tr("Add"), row);
            b->setObjectName(QStringLiteral("rosterAdd"));
            connect(b, &QPushButton::clicked, this, [this, e] { emit addRequested(e); });
            h->addWidget(b);
        }
        if (e.canManage) {
            auto* off = new QPushButton(tr("Take off the roster"), row);
            off->setObjectName(QStringLiteral("rosterRemove"));
            connect(off, &QPushButton::clicked, this, [this, e] { emit removeRequested(e); });
            h->addWidget(off);
        }
        lay->addWidget(row);
    }
    lay->addStretch(1);
}

}  // namespace bld::ui
