#include "OpsCommands.h"

#include "../core/Ids.h"

#include <QSet>

#include <algorithm>

namespace bld::edit {

namespace {

QJsonArray& listOf(core::Map& map, bool job) { return job ? map.sidecar.opsJobs : map.sidecar.waybills; }
const QJsonArray& listOf(const core::Map& map, bool job) { return job ? map.sidecar.opsJobs : map.sidecar.waybills; }

qsizetype indexOf(const QJsonArray& list, const QString& id) {
    for (qsizetype i = 0; i < list.size(); ++i)
        if (list.at(i).toObject().value(QLatin1String("id")).toString() == id) return i;
    return -1;
}

std::optional<QJsonObject> record(const core::Map& map, bool job, const QString& id) {
    const QJsonArray& list = listOf(map, job);
    const qsizetype i = indexOf(list, id);
    return i < 0 ? std::nullopt : std::optional(list.at(i).toObject());
}

// In id order, as the layout keeps them.
void insert(QJsonArray& list, const QJsonObject& r) {
    const QString id = r.value(QLatin1String("id")).toString();
    qsizetype i = 0;
    while (i < list.size() && list.at(i).toObject().value(QLatin1String("id")).toString() < id) ++i;
    list.insert(i, r);
}

const core::RollingStock* carOf(const core::Map& map, const QString& id) {
    for (const auto& s : map.sidecar.rollingStock)
        if (s.id == id) return &s;
    return nullptr;
}

std::optional<track::Waybill> waybillOf(const core::Map& map, const QString& id) {
    const auto r = record(map, false, id);
    return r ? std::optional(track::asWaybill(*r)) : std::nullopt;
}

QJsonObject with(QJsonObject r, const QJsonObject& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) r.insert(it.key(), it.value());
    return r;
}

void put(OpsChange& c, const core::Map& map, bool job, const QString& id, const QJsonObject& fields) {
    for (auto& had : c.records)
        if (had.job == job && had.id == id && had.after) {
            *had.after = with(*had.after, fields);
            return;
        }
    if (const auto r = record(map, job, id)) c.records.push_back({ job, id, with(*r, fields) });
}

// The car's fields in a change so far.
CarOps& carIn(OpsChange& c, const core::Map& map, const QString& id) {
    if (!c.cars.contains(id)) {
        const core::RollingStock* s = carOf(map, id);
        c.cars.insert(id, s ? carOps(*s) : CarOps());
    }
    return c.cars[id];
}

QJsonObject legFields(const std::vector<track::WaybillLeg>& legs) {
    QJsonObject out;
    for (int k = 1; k <= track::kWaybillSlots; ++k) {
        const QString n = QString::number(k);
        const track::WaybillLeg l = static_cast<size_t>(k) <= legs.size() ? legs[static_cast<size_t>(k - 1)] : track::WaybillLeg();
        out.insert(QStringLiteral("to") + n, l.to);
        out.insert(QStringLiteral("spot") + n, l.spot);
        out.insert(QStringLiteral("load") + n, l.load);
        out.insert(QStringLiteral("note") + n, l.note);
    }
    return out;
}

// The numbers a list needs so that the one at `i` moves by `d`: index -> number.
QMap<int, double> moved(const std::vector<double>& at, int i, int d) {
    const int n = static_cast<int>(at.size());
    const int j = i + d;
    if (i < 0 || j < 0 || j >= n) return {};
    const auto num = [&](int k) { return k >= 0 && k < n ? std::optional(at[static_cast<size_t>(k)]) : std::nullopt; };
    const auto before = d < 0 ? num(j - 1) : num(j);
    const auto after = d < 0 ? num(j) : num(j + 1);
    const auto between = before && after && !(*before < *after) ? std::nullopt : track::orderBetween(before, after);
    if (between) return { { i, *between } };
    // No number left between: number them all.
    QList<int> order;
    for (int k = 0; k < n; ++k) order << k;
    order.move(i, j);
    QMap<int, double> out;
    for (int k = 0; k < n; ++k) out.insert(order.at(k), k + 1.0);
    return out;
}

const track::Job* find(const std::vector<track::Job>& jobs, const QString& id) {
    const auto it = std::find_if(jobs.begin(), jobs.end(), [&](const track::Job& j) { return j.id == id; });
    return it == jobs.end() ? nullptr : &*it;
}

QJsonObject stopJson(const track::JobStop& s, double at) { return { { track::stopField(s.id), QJsonObject{ { QStringLiteral("place"), s.place }, { QStringLiteral("at"), at } } } }; }

}  // namespace

CarOps carOps(const core::RollingStock& s) { return { s.opsWaybill, s.opsHome, s.opsAt, s.opsHold, s.opsLoad }; }

std::vector<track::Job> jobsOf(const core::Map& map) {
    std::vector<track::Job> out;
    for (const auto& r : map.sidecar.opsJobs) out.push_back(track::asJob(r.toObject()));
    return track::jobsInOrder(std::move(out));
}

OpsCommand::OpsCommand(core::Map& map, const OpsChange& change, const QString& text, QUndoCommand* parent) : QUndoCommand(text, parent), map_(map) {
    for (const auto& r : change.records) {
        auto before = record(map, r.job, r.id);
        if (before == r.after) continue;
        records_.push_back({ r.job, r.id, std::move(before), r.after });
    }
    for (auto it = change.cars.constBegin(); it != change.cars.constEnd(); ++it) {
        const core::RollingStock* s = carOf(map, it.key());
        if (s && !(carOps(*s) == it.value())) cars_.push_back({ it.key(), carOps(*s), it.value() });
    }
    if (records_.empty() && cars_.empty()) setObsolete(true);
}

void OpsCommand::apply(bool forward, bool force) {
    for (const auto& step : records_) {
        const auto& from = forward ? step.before : step.after;
        const auto& to = forward ? step.after : step.before;
        QJsonArray& list = listOf(map_, step.job);
        const qsizetype i = indexOf(list, step.id);
        if (!to) {
            if (i >= 0) list.removeAt(i);
            continue;
        }
        if (!from) {
            if (i < 0) insert(list, *to);
            continue;
        }
        // (Deleted by someone else since: it stays gone.)
        if (i < 0) continue;
        QJsonObject r = list.at(i).toObject();
        QStringList keys = from->keys() + to->keys();
        keys.removeDuplicates();
        for (const QString& k : std::as_const(keys)) {
            if (from->value(k) == to->value(k) || (!force && r.value(k) != from->value(k))) continue;
            if (to->contains(k)) r.insert(k, to->value(k));
            else r.remove(k);
        }
        list.replace(i, r);
    }
    for (const auto& c : cars_) {
        const CarOps& from = forward ? c.before : c.after;
        const CarOps& to = forward ? c.after : c.before;
        for (auto& s : map_.sidecar.rollingStock) {
            if (s.id != c.id) continue;
            const auto field = [&](auto core::RollingStock::*f, const auto& a, const auto& b) {
                if (a != b && (force || s.*f == a)) s.*f = b;
            };
            field(&core::RollingStock::opsWaybill, from.waybill, to.waybill);
            field(&core::RollingStock::opsHome, from.home, to.home);
            field(&core::RollingStock::opsAt, from.at, to.at);
            field(&core::RollingStock::opsHold, from.hold, to.hold);
            field(&core::RollingStock::opsLoad, from.load, to.load);
        }
    }
}

void OpsCommand::redo() {
    apply(true, !done_);
    done_ = true;
}

void OpsCommand::undo() { apply(false, false); }

OpsChange newWaybill(const core::Map& map, const QString& id, const QString& car, const std::vector<track::WaybillLeg>& legs, const QString& from) {
    OpsChange c;
    if (map.sidecar.waybills.size() >= track::kMaxWaybills) return c;
    QJsonObject r = legFields(legs);
    r.insert(QStringLiteral("id"), id);
    r.insert(QStringLiteral("car"), car);
    r.insert(QStringLiteral("from"), from);
    r.insert(QStringLiteral("leg"), 1);
    c.records.push_back({ false, id, r });
    if (!car.isEmpty() && carOf(map, car)) {
        CarOps& ops = carIn(c, map, car);
        ops.waybill = id;
        if (!legs.empty() && !legs.front().load.isEmpty()) ops.load = legs.front().load;
    }
    return c;
}

OpsChange changedWaybill(const core::Map& map, const QString& id, const QJsonObject& fields) {
    OpsChange c;
    put(c, map, false, id, fields);
    return c;
}

OpsChange givenWaybill(const core::Map& map, const QString& id, const QString& car) {
    OpsChange c;
    const auto w = waybillOf(map, id);
    if (!w) return c;
    const core::RollingStock* was = carOf(map, w->car);
    if (was && w->car != car && was->opsWaybill == id) carIn(c, map, w->car).waybill = QString();
    if (const core::RollingStock* to = carOf(map, car)) {
        const QString held = to->opsWaybill.value_or(QString());
        if (const auto other = held == id ? std::nullopt : waybillOf(map, held); other && other->car == car)
            put(c, map, false, held, { { QStringLiteral("car"), QString() } });
        CarOps& ops = carIn(c, map, car);
        ops.waybill = id;
        if (const auto leg = track::currentLeg(*w); leg && !leg->load.isEmpty()) ops.load = leg->load;
    }
    put(c, map, false, id, { { QStringLiteral("car"), car } });
    return c;
}

OpsChange removedWaybill(const core::Map& map, const QString& id) {
    OpsChange c;
    const auto w = waybillOf(map, id);
    if (!w) return c;
    const core::RollingStock* s = carOf(map, w->car);
    if (s && s->opsWaybill == id) carIn(c, map, w->car).waybill = QString();
    c.records.push_back({ false, id, std::nullopt });
    return c;
}

QString pocketOf(const core::Map& map, const QString& car) {
    const core::RollingStock* s = carOf(map, car);
    if (!s || !s->opsWaybill) return {};
    const auto w = waybillOf(map, *s->opsWaybill);
    return w && w->car == car ? w->id : QString();
}

OpsChange turnedCar(const core::Map& map, const QString& car, const QString& offTo) {
    OpsChange c;
    const auto w = waybillOf(map, pocketOf(map, car));
    const core::RollingStock* s = carOf(map, car);
    if (!w || !s || track::currentSlot(*w) == 0) return c;
    const auto [leg, load] = track::turnedWaybill(*w, s->opsLoad);
    put(c, map, false, w->id, { { QStringLiteral("leg"), leg } });
    CarOps& ops = carIn(c, map, car);
    ops.load = load;
    if (!offTo.isEmpty()) ops.at = offTo;
    return c;
}

OpsChange restartedWaybill(const core::Map& map, const QString& id) {
    OpsChange c;
    auto w = waybillOf(map, id);
    if (!w) return c;
    put(c, map, false, id, { { QStringLiteral("leg"), 1 } });
    w->leg = 1;
    const auto leg = track::currentLeg(*w);
    if (leg && !leg->load.isEmpty() && pocketOf(map, w->car) == id) carIn(c, map, w->car).load = leg->load;
    return c;
}

OpsChange changedCar(const core::Map& map, const QString& car, const CarOps& after) {
    OpsChange c;
    if (carOf(map, car)) c.cars.insert(car, after);
    return c;
}

OpsChange madeWaybills(const core::Map& map, const track::MadeWaybills& made) {
    OpsChange c;
    qsizetype room = track::kMaxWaybills - map.sidecar.waybills.size();
    for (const auto& m : made.made) {
        if (room <= 0) break;
        const core::RollingStock* s = carOf(map, m.car);
        if (!s) continue;
        // (Given one since the preview.) A finished one is out of the way.
        const auto had = waybillOf(map, pocketOf(map, m.car));
        if (had && track::currentSlot(*had) > 0) continue;
        if (had) c.records.push_back({ false, had->id, std::nullopt });
        const OpsChange one = newWaybill(map, core::newBbmId(), m.car, m.legs, m.from);
        c.records.insert(c.records.end(), one.records.begin(), one.records.end());
        for (auto it = one.cars.constBegin(); it != one.cars.constEnd(); ++it) c.cars.insert(it.key(), it.value());
        --room;
    }
    return c;
}

OpsChange newJob(const core::Map& map, const QString& id, const QString& named) {
    OpsChange c;
    const auto jobs = jobsOf(map);
    if (jobs.size() >= track::kMaxJobs) return c;
    QSet<QString> names;
    for (const auto& j : jobs) names.insert(j.name);
    size_t n = jobs.size() + 1;
    while (names.contains(named.arg(n))) ++n;
    c.records.push_back({ true, id,
                          QJsonObject{ { QStringLiteral("id"), id },
                                       { QStringLiteral("name"), named.arg(n) },
                                       { QStringLiteral("kind"), QStringLiteral("local") },
                                       { QStringLiteral("train"), QString() },
                                       { QStringLiteral("order"), (jobs.empty() ? 0.0 : jobs.back().order) + 1.0 } } });
    return c;
}

OpsChange changedJob(const core::Map& map, const QString& id, const QJsonObject& fields) {
    OpsChange c;
    put(c, map, true, id, fields);
    return c;
}

OpsChange removedJob(const QString& id) {
    OpsChange c;
    c.records.push_back({ true, id, std::nullopt });
    return c;
}

OpsChange movedJob(const core::Map& map, const QString& id, int d) {
    OpsChange c;
    const auto jobs = jobsOf(map);
    std::vector<double> at;
    int i = -1;
    for (const auto& j : jobs) {
        if (j.id == id) i = static_cast<int>(at.size());
        at.push_back(j.order);
    }
    const auto to = moved(at, i, d);
    for (auto it = to.constBegin(); it != to.constEnd(); ++it) put(c, map, true, jobs[static_cast<size_t>(it.key())].id, { { QStringLiteral("order"), it.value() } });
    return c;
}

OpsChange addedStop(const core::Map& map, const QString& job, const QString& place) {
    OpsChange c;
    const auto jobs = jobsOf(map);
    const track::Job* j = find(jobs, job);
    if (!j || j->stops.size() >= track::kMaxStops) return c;
    put(c, map, true, job, stopJson({ core::newBbmId(), place, 0.0 }, (j->stops.empty() ? 0.0 : j->stops.back().at) + 1.0));
    return c;
}

OpsChange movedStop(const core::Map& map, const QString& job, const QString& stop, int d) {
    OpsChange c;
    const auto jobs = jobsOf(map);
    const track::Job* j = find(jobs, job);
    if (!j) return c;
    std::vector<double> at;
    int i = -1;
    for (const auto& s : j->stops) {
        if (s.id == stop) i = static_cast<int>(at.size());
        at.push_back(s.at);
    }
    const auto to = moved(at, i, d);
    for (auto it = to.constBegin(); it != to.constEnd(); ++it) put(c, map, true, job, stopJson(j->stops[static_cast<size_t>(it.key())], it.value()));
    return c;
}

OpsChange removedStop(const core::Map& map, const QString& job, const QString& stop) {
    OpsChange c;
    if (auto r = record(map, true, job)) {
        r->remove(track::stopField(stop));
        c.records.push_back({ true, job, *r });
    }
    return c;
}

PlaceUses usesOf(const core::Map& map, const QStringList& places) {
    PlaceUses out;
    const QSet<QString> gone(places.begin(), places.end());
    for (const auto& r : map.sidecar.waybills) {
        const auto w = track::asWaybill(r.toObject());
        if ((!w.from.isEmpty() && gone.contains(w.from)) || std::any_of(w.legs.begin(), w.legs.end(), [&](const auto& l) { return l && gone.contains(l->to); })) ++out.waybills;
    }
    for (const auto& j : jobsOf(map))
        if (std::any_of(j.stops.begin(), j.stops.end(), [&](const track::JobStop& s) { return gone.contains(s.place); })) ++out.jobs;
    for (const auto& s : map.sidecar.rollingStock)
        if (s.opsHome && !s.opsHome->isEmpty() && gone.contains(*s.opsHome)) ++out.homes;
    return out;
}

}  // namespace bld::edit
