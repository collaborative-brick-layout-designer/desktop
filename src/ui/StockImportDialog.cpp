#include "StockImportDialog.h"

#include "TrainDialogs.h"
#include "help/HelpButton.h"

#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bld::ui {

namespace {

// Scene units: studs, so a marker's x is its place along the car.
constexpr double kStep = 0.25;
constexpr double kPad = 3.0;

double snapped(double v) {
    return std::round(v / kStep) * kStep;
}

// A marker dragged along the car only: it stays on its line, snaps to a
// quarter stud and tells the dialog where it went.
class Marker : public QGraphicsRectItem {
public:
    Marker(const QRectF& r, double limit, std::function<void(double)> moved)
        : QGraphicsRectItem(r), limit_(limit), moved_(std::move(moved)) {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges | ItemIsFocusable);
        setCursor(Qt::SizeHorCursor);
        setAcceptHoverEvents(true);
    }
    // Moved by the dialog (a typed number): no callback.
    void place(double x) {
        quiet_ = true;
        setPos(x, 0);
        quiet_ = false;
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if (change == ItemPositionChange)
            return QPointF(std::clamp(snapped(value.toPointF().x()), -limit_, limit_), 0.0);
        if (change == ItemPositionHasChanged && !quiet_ && moved_) moved_(pos().x());
        return QGraphicsRectItem::itemChange(change, value);
    }

private:
    double limit_;
    std::function<void(double)> moved_;
    bool quiet_ = false;
};

// Keeps the whole car in view as the dialog is resized.
class FitView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;

protected:
    void resizeEvent(QResizeEvent* e) override {
        QGraphicsView::resizeEvent(e);
        if (scene()) fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    }
};

QDoubleSpinBox* studBox(double min, double max, QWidget* parent) {
    auto* b = new QDoubleSpinBox(parent);
    b->setRange(min, max);
    b->setSingleStep(kStep);
    b->setDecimals(2);
    b->setSuffix(StockImportDialog::tr(" studs"));
    return b;
}

} // namespace

QString stockGuessNote(const track::StockGuess& g) {
    const QString trucks =
        g.trucksFrom == QLatin1String("bogies")
            ? StockImportDialog::tr("We found the trucks from the model’s bogies.")
        : g.trucksFrom == QLatin1String("wheels")
            ? StockImportDialog::tr("We found the trucks from the model’s wheels.")
            : StockImportDialog::tr(
                  "We couldn’t find wheels, so the trucks are a quarter of the way in from each end.");
    const QString couplers =
        g.couplersFrom == QLatin1String("found")
            ? StockImportDialog::tr("The couplers are its magnets.")
            : StockImportDialog::tr("No magnets were found, so the couplers are just past each end.");
    return trucks + QLatin1Char(' ') + couplers + QLatin1Char(' ')
           + StockImportDialog::tr("Drag a marker to fix it.");
}

track::StockGuess stockGuessOf(const PreparedPart& part) {
    if (part.stockGuess) return *part.stockGuess;
    return track::guessStock({}, part.contentStuds, QPointF(part.widthStuds / 2.0, part.heightStuds / 2.0));
}

core::RollingStock stockFromModel(const QString& id, const QString& partKey, const StockSetup& setup,
                                  const QString& number, const QString& kind, const QString& file,
                                  const QString& format, const QString& color, const QString& date) {
    core::RollingStock s = core::simpleStock(id, setup.length);
    s.number = number;
    s.kind = kind;
    s.trackKind = kind == QLatin1String("monorail") ? QStringLiteral("monorail") : QStringLiteral("rail");
    s.powered = kind == QLatin1String("engine") || kind == QLatin1String("unit");
    s.width = setup.width;
    s.bodyLength = setup.bodyLength;
    s.trucks = { { std::max(setup.front, setup.rear), 30.0 }, { std::min(setup.front, setup.rear), 30.0 } };
    s.lookMode = QStringLiteral("model");
    s.partKey = partKey;
    if (!color.isEmpty()) s.color = color;
    s.extras.insert(QStringLiteral("model"), QJsonObject{ { QStringLiteral("partKey"), partKey },
                                                          { QStringLiteral("file"), file },
                                                          { QStringLiteral("format"), format },
                                                          { QStringLiteral("importedAt"), date } });
    return s;
}

StockImportDialog::StockImportDialog(PreparedPart part, const track::StockGuess& guess, bool canRoster,
                                     QWidget* parent)
    : QDialog(parent), part_(std::move(part)) {
    setObjectName(QStringLiteral("stockImportDialog"));
    setWindowTitle(tr("Import a model as an engine or car"));
    rotatePart(part_, guess.quarterTurns);
    setup_ = { guess.length, guess.width, guess.bodyLength, guess.front, guess.rear };

    auto* v = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("This is your model from above, its front to the right. The orange markers are its trucks "
           "(where the wheels turn), the blue ones its couplers. Check them, then add it."),
        this);
    intro->setWordWrap(true);
    v->addWidget(intro);

    auto* row = new QHBoxLayout;
    scene_ = new QGraphicsScene(this);
    view_ = new FitView(scene_, this);
    view_->setObjectName(QStringLiteral("stockDiagram"));
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setMinimumSize(420, 220);
    view_->setAccessibleName(tr("The model from above, front to the right, with its trucks and couplers"));
    row->addWidget(view_, 1);

    auto* form = new QFormLayout;
    name_ = new QLineEdit(suggestedImportName(part_), this);
    name_->setObjectName(QStringLiteral("stockImportName"));
    form->addRow(tr("Part name:"), name_);
    number_ = new QLineEdit(this);
    number_->setObjectName(QStringLiteral("stockImportNumber"));
    number_->setPlaceholderText(tr("Optional, shown on the map"));
    form->addRow(tr("Road number:"), number_);
    kind_ = new QComboBox(this);
    kind_->setObjectName(QStringLiteral("stockImportKind"));
    for (const QString& k : { QStringLiteral("engine"), QStringLiteral("car"), QStringLiteral("cab"),
                              QStringLiteral("unit"), QStringLiteral("monorail") })
        kind_->addItem(stockKindLabel(k), k);
    kind_->setCurrentIndex(
        kind_->findData(suggestedImportName(part_).contains(QLatin1String("engine"), Qt::CaseInsensitive)
                            ? QStringLiteral("engine")
                            : QStringLiteral("car")));
    form->addRow(tr("It’s a:"), kind_);
    auto* turn = new QPushButton(tr("Turn around ⇄"), this);
    turn->setObjectName(QStringLiteral("stockImportTurn"));
    turn->setToolTip(tr("The other end is the front"));
    connect(turn, &QPushButton::clicked, this, &StockImportDialog::turnAround);
    form->addRow(QString(), turn);
    frontBox_ = studBox(-200, 200, this);
    frontBox_->setObjectName(QStringLiteral("stockImportFront"));
    form->addRow(tr("Front truck:"), help::withHelp(frontBox_, QStringLiteral("trains.trucks"), this));
    rearBox_ = studBox(-200, 200, this);
    rearBox_->setObjectName(QStringLiteral("stockImportRear"));
    form->addRow(tr("Rear truck:"), rearBox_);
    lengthBox_ = studBox(2, 400, this);
    lengthBox_->setObjectName(QStringLiteral("stockImportLength"));
    form->addRow(tr("Coupler to coupler:"), lengthBox_);
    roster_ = new QCheckBox(tr("Also put it on my roster, for other layouts"), this);
    roster_->setObjectName(QStringLiteral("stockImportRoster"));
    roster_->setEnabled(canRoster);
    if (!canRoster) roster_->setToolTip(tr("Sign in to a server (File › Servers…) to keep a roster."));
    form->addRow(QString(), help::withHelp(roster_, QStringLiteral("trains.roster"), this));
    row->addLayout(form);
    v->addLayout(row, 1);

    note_ = new QLabel(stockGuessNote(guess), this);
    note_->setObjectName(QStringLiteral("stockImportNote"));
    note_->setWordWrap(true);
    v->addWidget(help::withHelp(note_, QStringLiteral("trains.importModel"), this));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    add_ = buttons->addButton(tr("Add to rolling stock"), QDialogButtonBox::AcceptRole);
    add_->setObjectName(QStringLiteral("stockImportAdd"));
    add_->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this,
            [this](const QString& t) { add_->setEnabled(!t.trimmed().isEmpty()); });
    add_->setEnabled(!partName().isEmpty());
    v->addWidget(buttons);

    connect(frontBox_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        if (!syncing_) setFront(x);
    });
    connect(rearBox_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        if (!syncing_) setRear(x);
    });
    connect(lengthBox_, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        if (!syncing_) setLength(x);
    });
    redraw();
}

QString StockImportDialog::partName() const {
    return name_->text().trimmed();
}
QString StockImportDialog::number() const {
    return number_->text().trimmed();
}
QString StockImportDialog::kind() const {
    return kind_->currentData().toString();
}
bool StockImportDialog::toRoster() const {
    return roster_->isEnabled() && roster_->isChecked();
}

double StockImportDialog::frontMarker() const {
    return frontItem_->pos().x();
}
double StockImportDialog::rearMarker() const {
    return rearItem_->pos().x();
}
double StockImportDialog::couplerMarker() const {
    return couplerFront_->pos().x();
}

void StockImportDialog::setFront(double at) {
    setup_.front = at;
    syncFields();
}

void StockImportDialog::setRear(double at) {
    setup_.rear = at;
    syncFields();
}

void StockImportDialog::setLength(double length) {
    setup_.length = std::max(2.0, length);
    setup_.bodyLength = std::max(1.0, std::round((setup_.length - 2.0) * 100.0) / 100.0);
    syncFields();
}

void StockImportDialog::turnAround() {
    rotatePart(part_, 2);
    const double front = setup_.front;
    setup_.front = -setup_.rear;
    setup_.rear = -front;
    redraw();
}

void StockImportDialog::syncFields() {
    syncing_ = true;
    frontBox_->setValue(setup_.front);
    rearBox_->setValue(setup_.rear);
    lengthBox_->setValue(setup_.length);
    static_cast<Marker*>(frontItem_)->place(setup_.front);
    static_cast<Marker*>(rearItem_)->place(setup_.rear);
    static_cast<Marker*>(couplerFront_)->place(setup_.length / 2.0);
    static_cast<Marker*>(couplerRear_)->place(-setup_.length / 2.0);
    body_->setRect(-setup_.bodyLength / 2.0, -setup_.width / 2.0, setup_.bodyLength, setup_.width);
    syncing_ = false;
}

void StockImportDialog::redraw() {
    scene_->clear();
    const double W = part_.widthStuds;
    const double H = part_.heightStuds;
    const double limit = W / 2.0 + kPad - 0.5;
    scene_->setSceneRect(-W / 2.0 - kPad, -H / 2.0 - kPad, W + 2 * kPad, H + 2 * kPad);
    const QColor ink = palette().color(QPalette::WindowText);
    // The track under it, for scale.
    scene_->addLine(-W / 2.0 - kPad, 0, W / 2.0 + kPad, 0, QPen(QColor(0x9C, 0xA3, 0xAF), 0.2, Qt::DashLine));
    picture_ = scene_->addPixmap(QPixmap::fromImage(part_.sprite));
    picture_->setTransformationMode(Qt::SmoothTransformation);
    const double scale = part_.sprite.width() > 0 ? W / part_.sprite.width() : 1.0;
    picture_->setScale(scale);
    picture_->setPos(-W / 2.0, -H / 2.0);
    body_ = scene_->addRect(QRectF(), QPen(QColor(0x25, 0x63, 0xEB), 0.2, Qt::DashLine));
    const auto truck = [&](const QString& label, const QString& name, std::function<void(double)> moved) {
        auto* m = new Marker(QRectF(-0.35, -H / 2.0 - 1.0, 0.7, H + 2.0), limit, std::move(moved));
        m->setBrush(QColor(0x1F, 0x29, 0x37));
        m->setPen(Qt::NoPen);
        m->setData(0, name);
        auto* knob = new QGraphicsEllipseItem(-1.0, -H / 2.0 - 2.6, 2.0, 2.0, m);
        knob->setBrush(QColor(0xF5, 0x9E, 0x0B));
        knob->setPen(QPen(QColor(0x1F, 0x29, 0x37), 0.25));
        auto* t = new QGraphicsSimpleTextItem(label, knob);
        QFont f;
        f.setBold(true);
        f.setPixelSize(12);
        t->setFont(f);
        t->setScale(1.1 / 12.0);
        t->setPos(-t->boundingRect().width() * t->scale() / 2.0,
                  -H / 2.0 - 2.6 + (2.0 - t->boundingRect().height() * t->scale()) / 2.0);
        scene_->addItem(m);
        return m;
    };
    rearItem_ = truck(tr("R", "rear truck"), QStringLiteral("rear"), [this](double x) { setRear(x); });
    frontItem_ = truck(tr("F", "front truck"), QStringLiteral("front"), [this](double x) { setFront(x); });
    const auto coupler = [&](bool front) {
        auto* m = new Marker(QRectF(-0.9, -0.9, 1.8, 1.8), limit,
                             [this](double x) { setLength(std::abs(x) * 2.0); });
        m->setBrush(QColor(0x25, 0x63, 0xEB));
        m->setPen(QPen(Qt::white, 0.25));
        m->setData(0, front ? QStringLiteral("couplerFront") : QStringLiteral("couplerRear"));
        scene_->addItem(m);
        return m;
    };
    couplerFront_ = coupler(true);
    couplerRear_ = coupler(false);
    auto* frontText = scene_->addSimpleText(tr("Front →"));
    QFont f;
    f.setPixelSize(12);
    frontText->setFont(f);
    frontText->setBrush(ink);
    frontText->setScale(1.2 / 12.0);
    frontText->setPos(W / 2.0 + kPad - frontText->boundingRect().width() * frontText->scale() - 0.3,
                      H / 2.0 + kPad - 1.8);
    syncFields();
    view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

} // namespace bld::ui
