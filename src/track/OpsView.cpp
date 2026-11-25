#include "OpsView.h"

#include "../core/Places.h"

#include <algorithm>
#include <limits>

namespace bld::track {

namespace {
QStringList wordsOf(const QJsonObject& o, const char* key) {
    QStringList out;
    for (const auto& w : o.value(QLatin1String(key)).toArray()) out << w.toString();
    return out;
}
}  // namespace

const NamedPlace* OpsView::place(const QString& id) const {
    const auto it = std::find_if(places.begin(), places.end(), [&](const NamedPlace& p) { return p.id == id; });
    return it == places.end() ? nullptr : &*it;
}

const Job* OpsView::job(const QString& id) const {
    const auto it = std::find_if(jobs.begin(), jobs.end(), [&](const Job& j) { return j.id == id; });
    return it == jobs.end() ? nullptr : &*it;
}

OpsView opsView(const core::Map& map, const TrackNetwork& net, const std::vector<TrackRun>& runs, const QString& moduleName, const QString& noName) {
    OpsView out;
    const auto& sc = map.sidecar;
    for (const auto& r : sc.waybills) {
        const Waybill w = asWaybill(r.toObject());
        out.waybills.insert(w.id, w);
    }
    std::vector<Job> jobs;
    for (const auto& r : sc.opsJobs) jobs.push_back(asJob(r.toObject()));
    out.jobs = jobsInOrder(std::move(jobs));
    out.stock = stockById(sc);
    out.trains = standingTrains(sc);

    // The places: the layout's first, then each module's, by the module's id (two of one id: the first).
    struct Group {
        QString owner;
        QString name;
        QJsonArray places;
    };
    std::vector<Group> groups{ { QString(), QString(), sc.layoutPlaces } };
    for (const auto& m : sc.modules) groups.push_back({ m.id, m.name.isEmpty() ? moduleName : m.name, m.places });
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.owner < b.owner; });
    QMap<QString, int> times;
    for (const auto& g : groups)
        for (const auto& p : g.places) ++times[p.toObject().value(QLatin1String("name")).toString()];
    const QMap<QString, int> pieces = trackPieces(net);
    std::vector<SpotLine> lines;
    QSet<QString> seen;
    for (const auto& g : groups)
        for (const auto& pv : g.places) {
            const QJsonObject p = pv.toObject();
            const QString id = p.value(QLatin1String("id")).toString();
            if (seen.contains(id)) continue;
            seen.insert(id);
            const QString raw = p.value(QLatin1String("name")).toString();
            const QString name = raw.isEmpty() ? noName : raw;
            NamedPlace named{ id, times.value(raw) > 1 && !g.owner.isEmpty() ? QStringLiteral("%1 (%2)").arg(name, g.name) : name, p, std::nullopt };
            OpsPlace ops{ id, p.value(QLatin1String("kind")).toString(), {} };
            int total = 0;
            bool known = true;
            const QJsonArray spots = p.value(QLatin1String("spots")).toArray();
            for (const auto& sv : spots) {
                const QJsonObject s = sv.toObject();
                const SpotNow now = spotNow(&net, runs, s);
                OpsSpot spot;
                spot.id = s.value(QLatin1String("id")).toString();
                if (s.value(QLatin1String("capacity")).isDouble()) spot.capacity = s.value(QLatin1String("capacity")).toInt();
                if (now.at) spot.usable = now.at->usable;
                spot.role = s.value(QLatin1String("role")).toString();
                const QString brick = s.value(QLatin1String("track")).toObject().value(QLatin1String("brick")).toString();
                if (pieces.contains(brick)) spot.track = pieces.value(brick);
                if (now.at) lines.push_back({ id, spot.id, now.at->line });
                known = known && now.capacity.has_value();
                total += now.capacity.value_or(0);
                ops.spots.push_back(spot);
            }
            if (known && !spots.isEmpty()) named.capacity = total;
            out.places.push_back(std::move(named));
            out.opsPlaces.push_back(std::move(ops));
        }

    struct Stands {
        const core::Train* train;
        int unit;
    };
    QMap<QString, Stands> standing;
    for (const auto& t : out.trains) {
        if (!t.position) continue;
        for (int i = 0; i < static_cast<int>(t.units.size()); ++i) {
            const QString& car = t.units[static_cast<size_t>(i)].stock;
            if (!standing.contains(car)) standing.insert(car, { &t, i });
            out.onTrack.insert(car);
        }
    }
    for (const auto& t : out.trains) {
        if (!t.position) continue;
        const auto placed = placeTrain(net, t, out.stock, sc.switchStates);
        if (!placed) continue;
        for (const auto& u : placed->units) {
            if (!standing.contains(u.stock) || standing.value(u.stock).train->id != t.id) continue;
            out.points.insert(u.stock, (u.frontTruck + u.rearTruck) / 2.0);
            if (const auto spot = spotUnder({ u.frontTruck, u.rearTruck }, lines)) out.at.insert(u.stock, *spot);
        }
    }
    std::vector<OpsCar> cars;
    for (const auto& s : std::as_const(out.stock)) {
        const auto w = s.opsWaybill ? out.waybills.constFind(*s.opsWaybill) : out.waybills.constEnd();
        const bool held = w != out.waybills.constEnd() && w->car == s.id;
        if (held) out.pockets.insert(s.id, w->id);
        const bool stands = standing.contains(s.id);
        if (!stands && s.opsAt && seen.contains(*s.opsAt)) out.at.insert(s.id, { *s.opsAt, QString() });
        const auto to = destinationOf(held ? &*w : nullptr, s.opsHome.value_or(QString()));
        if (to) out.to.insert(s.id, *to);
        if (isEngine(s)) continue;
        OpsCar car;
        car.id = s.id;
        car.length = s.length;
        if (out.at.contains(s.id)) car.at = out.at.value(s.id);
        car.onTrack = stands;
        if (stands) {
            const Stands at = standing.value(s.id);
            car.train = at.train->id;
            car.unit = at.unit;
            int nearest = std::numeric_limits<int>::max();
            for (int i = 0; i < static_cast<int>(at.train->units.size()); ++i) {
                const auto e = out.stock.constFind(at.train->units[static_cast<size_t>(i)].stock);
                if (e != out.stock.constEnd() && isEngine(*e)) nearest = std::min(nearest, std::abs(i - at.unit));
            }
            if (nearest != std::numeric_limits<int>::max()) car.fromEngine = nearest;
        }
        car.to = to;
        car.hold = s.opsHold.value_or(false);
        cars.push_back(std::move(car));
    }
    std::vector<OpsJob> routed;
    for (const auto& j : out.jobs) {
        OpsJob o{ j.id, j.train, {} };
        for (const auto& s : j.stops) o.stops << s.place;
        routed.push_back(std::move(o));
    }
    out.routing = routeCars(out.opsPlaces, cars, routed);
    std::stable_sort(out.places.begin(), out.places.end(), [](const NamedPlace& a, const NamedPlace& b) {
        const int c = QString::localeAwareCompare(a.name, b.name);
        return c != 0 ? c < 0 : a.id < b.id;
    });
    return out;
}

MadeWaybills previewWaybills(const OpsView& view, quint32 seed) {
    QMap<QString, int> heading;
    for (const auto& d : view.to)
        if (!d.home) ++heading[d.place];
    std::vector<MakeCar> cars;
    for (const auto& s : view.stock) {
        if (isEngine(s) || s.opsHold.value_or(false)) continue;
        const auto w = view.waybills.constFind(view.pockets.value(s.id));
        if (w != view.waybills.constEnd() && currentSlot(*w) > 0) continue;
        cars.push_back({ s.id, s.type, view.at.value(s.id).place });
    }
    std::vector<MakePlace> places;
    places.reserve(view.places.size());
    for (const auto& p : view.places) places.push_back({ p.id, wordsOf(p.place, "ships"), wordsOf(p.place, "accepts"), p.capacity });
    return makeWaybills(cars, places, heading, seed);
}

}  // namespace bld::track
