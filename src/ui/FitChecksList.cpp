#include "FitChecksList.h"

#include "TrainDialogs.h"
#include "help/HelpButton.h"

#include "../core/Map.h"
#include "../edit/venue/VenueUnits.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace bld::ui {

namespace {

QString deg(double v) { return QStringLiteral("%1°").arg(std::lround(v)); }

}  // namespace

QString fitCheckText(const track::FitCheck& c, const core::Map& map) {
    const auto& sc = map.sidecar;
    const auto trains = namedTrains(sc);
    QString train = FitChecksList::tr("A train");
    for (const auto& t : trains)
        if (t.id == c.train && !t.name.isEmpty()) train = t.name;
    const core::RollingStock* car = nullptr;
    for (const auto& s : sc.rollingStock)
        if (s.id == c.stock) car = &s;
    const QString carName = car ? stockLabel(*car) : FitChecksList::tr("A car");
    QString route = FitChecksList::tr("A route");
    for (const auto& r : sc.routes)
        if (r.id == c.route && !r.name.isEmpty()) route = r.name;
    const auto unit = edit::venue::accountLengthUnit();
    const auto len = [&](const char* k) { return edit::venue::formatLength(c.values.value(QLatin1String(k)), unit); };
    const auto v = [&](const char* k) { return c.values.value(QLatin1String(k)); };
    if (c.kind == QLatin1String("siding"))
        return FitChecksList::tr("%1 (%2) is longer than the track it stands in (%3 between its switches and ends).").arg(train, len("length"), len("usable"));
    if (c.kind == QLatin1String("truck"))
        return FitChecksList::tr("%1 in %2: the curve is too tight for its trucks (they turn %3; at most %4).").arg(carName, train, deg(v("angle")), deg(v("max")));
    if (c.kind == QLatin1String("coupler"))
        return v("gap") > 1.0
                   ? FitChecksList::tr("%1 in %2 can't reach the car ahead: its couplers are %3 apart on this curve.").arg(carName, train, len("gap"))
                   : FitChecksList::tr("%1 in %2: its coupler turns %3 on this curve (at most %4).").arg(carName, train, deg(v("angle")), deg(v("max")));
    if (c.kind == QLatin1String("uncoupled")) return FitChecksList::tr("%1 in %2 has no coupler to the car ahead.").arg(carName, train);
    if (c.kind == QLatin1String("clearance")) return FitChecksList::tr("%1 in %2 is too close to the other track here.").arg(carName, train);
    if (c.kind == QLatin1String("overlap")) {
        QString other = FitChecksList::tr("another train");
        for (const auto& t : trains)
            if (t.id == c.other && !t.name.isEmpty()) other = t.name;
        return FitChecksList::tr("%1 and %2 are on top of each other.").arg(train, other);
    }
    if (c.kind == QLatin1String("trackKind"))
        return FitChecksList::tr("%1 in %2 doesn't run on this kind of track (it runs on %3).")
            .arg(carName, train, car && car->trackKind == QLatin1String("monorail") ? FitChecksList::tr("monorail") : FitChecksList::tr("train track"));
    if (c.kind == QLatin1String("offEnd")) return FitChecksList::tr("%1 runs off the end of the track.").arg(train);
    if (c.kind == QLatin1String("lost")) return FitChecksList::tr("%1's track was moved or removed: put it on the track again.").arg(train);
    if (c.kind == QLatin1String("reverse")) return FitChecksList::tr("%1 turns the train around (a reversing loop or wye).").arg(route);
    if (c.kind == QLatin1String("power"))
        return FitChecksList::tr("%1 turns the train around on powered track: it needs a polarity switch there.").arg(route);
    if (c.kind == QLatin1String("gap"))
        return FitChecksList::tr("The track doesn’t quite join here (the ends are %1 apart). Trains and routes run across it; nudge one piece to close the gap.")
            .arg(len("gap"));
    return {};
}

FitChecksList::FitChecksList(const QString& title, QWidget* parent) : QFrame(parent), titleText_(title) {
    setObjectName(QStringLiteral("fitChecks"));
    setFrameShape(QFrame::StyledPanel);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 6, 8, 6);
    title_ = new QLabel(title, this);
    title_->setObjectName(QStringLiteral("fitChecksTitle"));
    title_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    v->addWidget(help::withHelp(title_, QStringLiteral("trains.checks"), this));
    clear_ = new QLabel(this);
    clear_->setObjectName(QStringLiteral("fitChecksClear"));
    clear_->setWordWrap(true);
    v->addWidget(clear_);
    rows_ = new QVBoxLayout;
    rows_->setContentsMargins(0, 0, 0, 0);
    v->addLayout(rows_);
}

void FitChecksList::setChecks(const std::vector<track::FitCheck>& checks, const core::Map& map, const QString& allClear) {
    while (QLayoutItem* it = rows_->takeAt(0)) {
        if (QWidget* w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    // Problems first, then warnings, each in the order found.
    std::vector<const track::FitCheck*> sorted;
    for (const auto& c : checks)
        if (c.error) sorted.push_back(&c);
    for (const auto& c : checks)
        if (!c.error) sorted.push_back(&c);
    count_ = static_cast<int>(sorted.size());
    title_->setText(count_ ? QStringLiteral("%1 (%2)").arg(titleText_).arg(count_) : titleText_);
    clear_->setText(allClear);
    clear_->setVisible(count_ == 0);
    setVisible(count_ > 0 || !allClear.isEmpty());
    for (const auto* c : sorted) {
        auto* row = new QWidget(this);
        row->setObjectName(QStringLiteral("fitCheck_") + c->kind);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* dot = new QLabel(row);
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(c->error ? QStringLiteral("#DC2626") : QStringLiteral("#F59E0B")));
        dot->setAccessibleName(c->error ? tr("Problem") : tr("Warning"));
        h->addWidget(dot, 0, Qt::AlignTop);
        auto* text = new QLabel(fitCheckText(*c, map), row);
        text->setObjectName(QStringLiteral("fitCheckText"));
        text->setWordWrap(true);
        h->addWidget(text, 1);
        auto* show = new QPushButton(tr("Show me"), row);
        show->setObjectName(QStringLiteral("fitCheckShow"));
        show->setFlat(true);
        connect(show, &QPushButton::clicked, this, [this, at = c->at] { emit showRequested(at); });
        h->addWidget(show, 0, Qt::AlignTop);
        rows_->addWidget(row);
    }
}

QStringList FitChecksList::lines() const {
    QStringList out;
    for (auto* l : findChildren<QLabel*>(QStringLiteral("fitCheckText")))
        if (!l->isHidden() && l->parentWidget() && l->parentWidget()->parent()) out << l->text();
    return out;
}

}  // namespace bld::ui
