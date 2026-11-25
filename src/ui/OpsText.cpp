#include "OpsText.h"

#include "TrainDialogs.h"

namespace bld::ui {

QString OpsText::gonePlace() { return tr("A place that was deleted"); }

QString OpsText::placeName(const track::OpsView& v, const QString& id) {
    const auto* p = v.place(id);
    return p ? p->name : gonePlace();
}

QString OpsText::spotName(const track::OpsView& v, const QString& place, const QString& spot) {
    const auto* p = v.place(place);
    if (p && !spot.isEmpty())
        for (const auto& s : p->place.value(QLatin1String("spots")).toArray())
            if (s.toObject().value(QLatin1String("id")).toString() == spot) {
                const QString name = s.toObject().value(QLatin1String("name")).toString();
                return QStringLiteral("%1, %2").arg(p->name, name.isEmpty() ? tr("a spot") : name);
            }
    return placeName(v, place);
}

QString OpsText::carName(const track::OpsView& v, const QString& id) {
    const auto s = v.stock.constFind(id);
    return s == v.stock.constEnd() ? tr("A car that was deleted") : stockLabel(*s);
}

QString OpsText::load(const QString& load) {
    if (load == QLatin1String("loaded")) return tr("Loaded");
    if (load == QLatin1String("empty")) return tr("Empty");
    return tr("Doesn’t apply");
}

QString OpsText::leg(const track::OpsView& v, const track::WaybillLeg& leg) {
    const QString where = spotName(v, leg.to, leg.spot);
    if (leg.load == QLatin1String("loaded")) return tr("Loaded to %1").arg(where);
    if (leg.load == QLatin1String("empty")) return tr("Empty to %1").arg(where);
    return tr("To %1").arg(where);
}

QString OpsText::next(const track::OpsView& v, const QString& car) {
    const auto to = v.to.constFind(car);
    if (to == v.to.constEnd()) return tr("Nowhere to go: it stays.");
    QString text;
    if (to->home) text = tr("Home to %1").arg(placeName(v, to->place));
    else if (const auto leg = track::currentLeg(v.waybills.value(v.pockets.value(car)))) text = OpsText::leg(v, *leg);
    const auto r = v.routing.cars.constFind(car);
    if (r == v.routing.cars.constEnd()) return v.routing.arrived.contains(car) ? tr("%1: it’s there").arg(text) : text;
    const track::Job* j = v.job(r->job);
    const QString job = j && !j->name.isEmpty() ? j->name : tr("a job");
    return r->jobs > 1 ? tr("%1, with %2 first").arg(text, job) : tr("%1, with %2").arg(text, job);
}

QString OpsText::cant(const track::OpsView& v, const track::CantRoute& c) {
    if (c.why == QLatin1String("held")) return tr("It’s set aside.");
    if (c.why == QLatin1String("offTrack")) return tr("It isn’t on the track. Put it on the track, or say which place off the layout it’s at.");
    if (c.why == QLatin1String("nowhere")) return tr("It isn’t at a place.");
    if (c.why == QLatin1String("tooMuch")) return tr("There are too many jobs and places to work out its way.");
    if (c.why == QLatin1String("placeGone"))
        return c.byHome ? tr("Its home was deleted. Pick another.") : tr("The place it goes to was deleted. Pick another on its waybill.");
    const QString place = placeName(v, c.place);
    if (c.why == QLatin1String("noTrack")) return tr("No track joins %1 to %2.").arg(placeName(v, c.from), place);
    if (c.why == QLatin1String("noJob")) {
        if (c.from.isEmpty()) return tr("The job whose train it’s on doesn’t go to %1.").arg(place);
        return c.from == c.place ? tr("No job stops at %1.").arg(place) : tr("No job goes from %1 to %2.").arg(placeName(v, c.from), place);
    }
    const QString where = spotName(v, c.place, c.spot);
    return c.capacity ? tr("%1 is full (%2 of %3).").arg(where).arg(c.count).arg(*c.capacity) : tr("%1 is full.").arg(where);
}

QString OpsText::jobKind(const QString& kind) {
    if (kind == QLatin1String("local")) return tr("Local");
    if (kind == QLatin1String("yard")) return tr("Yard job");
    if (kind == QLatin1String("through")) return tr("Through freight");
    if (kind == QLatin1String("turn")) return tr("Turn");
    return kind;
}

QString OpsText::jobKindHint(const QString& kind) {
    if (kind == QLatin1String("local")) return tr("Works the industries along the way.");
    if (kind == QLatin1String("yard")) return tr("Sorts cars in a yard.");
    if (kind == QLatin1String("through")) return tr("Runs from one yard or staging to another.");
    if (kind == QLatin1String("turn")) return tr("Goes out, works, and comes back.");
    return {};
}

QString OpsText::stops(int n) { return n == 1 ? tr("1 stop") : tr("%1 stops").arg(n); }
QString OpsText::movesText(int n) { return n == 0 ? tr("no work") : n == 1 ? tr("1 move") : tr("%1 moves").arg(n); }

QString OpsText::uses(const edit::PlaceUses& u) {
    QStringList parts;
    if (u.waybills) parts << (u.waybills == 1 ? tr("1 waybill") : tr("%1 waybills").arg(u.waybills));
    if (u.jobs) parts << (u.jobs == 1 ? tr("1 job") : tr("%1 jobs").arg(u.jobs));
    if (u.homes) parts << (u.homes == 1 ? tr("1 car’s home") : tr("%1 cars’ homes").arg(u.homes));
    if (parts.isEmpty()) return {};
    return tr("In use by %1: nothing is lost, and those cars are listed under Can’t route in Live Ops until you send them somewhere else.").arg(parts.join(QStringLiteral(", ")));
}

}  // namespace bld::ui
