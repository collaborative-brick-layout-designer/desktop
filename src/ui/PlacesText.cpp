#include "PlacesText.h"

#include "../core/Places.h"

#include <QStringList>

namespace bld::ui {

QString PlacesText::kind(const QString& k) {
    if (k == QLatin1String("industry")) return tr("Industry");
    if (k == QLatin1String("yard")) return tr("Yard");
    if (k == QLatin1String("staging")) return tr("Staging");
    if (k == QLatin1String("interchange")) return tr("Interchange");
    return k;
}

QString PlacesText::kindHint(const QString& k) {
    if (k == QLatin1String("industry")) return tr("Somewhere cars are loaded or unloaded.");
    if (k == QLatin1String("yard")) return tr("Where cars are sorted into trains.");
    if (k == QLatin1String("staging")) return tr("Off the layout: where trains come from and go to.");
    if (k == QLatin1String("interchange")) return tr("Where cars change to another railroad, the rest of the world.");
    return {};
}

QString PlacesText::role(const QString& r) {
    if (r == QLatin1String("arrival")) return tr("Arrival");
    if (r == QLatin1String("departure")) return tr("Departure");
    if (r == QLatin1String("class")) return tr("Sorting");
    if (r == QLatin1String("rip")) return tr("Repair");
    if (r == QLatin1String("engine")) return tr("Engines");
    return r;
}

QString PlacesText::counts(const QList<std::pair<QString, int>>& counts) {
    QStringList out;
    for (const auto& [k, n] : counts) {
        if (k == QLatin1String("industry")) out << (n == 1 ? tr("1 industry") : tr("%1 industries").arg(n));
        else if (k == QLatin1String("yard")) out << (n == 1 ? tr("1 yard") : tr("%1 yards").arg(n));
        else if (k == QLatin1String("staging")) out << tr("%1 staging").arg(n);
        else if (k == QLatin1String("interchange")) out << (n == 1 ? tr("1 interchange") : tr("%1 interchanges").arg(n));
        else out << QStringLiteral("%1 %2").arg(n).arg(k);
    }
    return out.join(QStringLiteral(", "));
}

QString PlacesText::places(const QJsonArray& places) { return counts(core::places::countsOf(places)); }

QString PlacesText::capacityOf(int cars) {
    if (cars == 0) return tr("Too short for a car");
    return cars == 1 ? tr("1 car") : tr("%1 cars").arg(cars);
}

QString PlacesText::capacity(std::optional<int> cars) { return cars ? capacityOf(*cars) : QString(); }

}  // namespace bld::ui
