#include "Ops.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::track {

namespace {

constexpr int kMaxHops = 4;
constexpr int kMaxRounds = 8;
constexpr double kOnSpot = 1.5;
constexpr int kMakePlaces = 2000;

QString text(const QJsonObject& r, const QString& key) { return r.value(key).toString(); }

bool wholeIn(const QJsonValue& v, int lo, int hi) {
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    return d >= lo && d <= hi && d == std::floor(d);
}

QString flipped(const QString& load) {
    if (load == QLatin1String("loaded")) return QStringLiteral("empty");
    if (load == QLatin1String("empty")) return QStringLiteral("loaded");
    return load;
}

template <typename T>
std::vector<T> firstById(const std::vector<T>& list) {
    std::vector<T> out;
    QSet<QString> seen;
    for (const auto& x : list) {
        if (seen.contains(x.id)) continue;
        seen.insert(x.id);
        out.push_back(x);
    }
    return out;
}

bool nearLine(QPointF p, const std::vector<QPointF>& line) {
    if (line.size() == 1) return std::hypot(line[0].x() - p.x(), line[0].y() - p.y()) <= kOnSpot;
    for (size_t k = 1; k < line.size(); ++k) {
        const QPointF a = line[k - 1];
        const QPointF b = line[k];
        if (p.x() < std::min(a.x(), b.x()) - kOnSpot || p.x() > std::max(a.x(), b.x()) + kOnSpot || p.y() < std::min(a.y(), b.y()) - kOnSpot
            || p.y() > std::max(a.y(), b.y()) + kOnSpot)
            continue;
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double l2 = dx * dx + dy * dy;
        const double t = l2 > 0 ? std::clamp(((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / l2, 0.0, 1.0) : 0.0;
        if (std::hypot(a.x() + dx * t - p.x(), a.y() + dy * t - p.y()) <= kOnSpot) return true;
    }
    return false;
}

// Whether a train can get from one place to another (a place off the track is always reachable).
bool placesJoined(const OpsPlace& a, const OpsPlace& b) {
    if (a.id == b.id) return true;
    QSet<int> ta;
    for (const auto& s : a.spots)
        if (s.track) ta.insert(*s.track);
    if (ta.isEmpty()) return true;
    bool any = false;
    for (const auto& s : b.spots)
        if (s.track) {
            any = true;
            if (ta.contains(*s.track)) return true;
        }
    return !any;
}

QString typeKey(const QString& t) { return t.trimmed().toLower(); }

// Whole numbers below `n`, the same from one seed in both apps.
class Seeded {
public:
    explicit Seeded(quint32 seed) : a_(seed) {}
    int below(size_t n) {
        a_ += 0x6d2b79f5U;
        quint32 t = (a_ ^ (a_ >> 15)) * (a_ | 1U);
        t = t ^ (t + (t ^ (t >> 7)) * (t | 61U));
        return static_cast<int>(std::floor(static_cast<double>(t ^ (t >> 14)) / 4294967296.0 * static_cast<double>(n)));
    }

private:
    quint32 a_;
};

}  // namespace

QStringList jobKinds() {
    return { QStringLiteral("local"), QStringLiteral("yard"), QStringLiteral("through"), QStringLiteral("turn") };
}

Waybill asWaybill(const QJsonObject& r) {
    Waybill w;
    for (int k = 1; k <= kWaybillSlots; ++k) {
        const QString n = QString::number(k);
        const QString to = text(r, QStringLiteral("to") + n);
        if (to.isEmpty()) continue;
        const QString load = text(r, QStringLiteral("load") + n);
        w.legs[static_cast<size_t>(k - 1)] = WaybillLeg{ to, text(r, QStringLiteral("spot") + n),
                                                         load == QLatin1String("loaded") || load == QLatin1String("empty") ? load : QString(),
                                                         text(r, QStringLiteral("note") + n) };
    }
    w.id = text(r, QStringLiteral("id"));
    w.car = text(r, QStringLiteral("car"));
    w.from = text(r, QStringLiteral("from"));
    const QJsonValue leg = r.value(QLatin1String("leg"));
    w.leg = wholeIn(leg, 0, kWaybillSlots) ? leg.toInt() : 1;
    return w;
}

int currentSlot(const Waybill& w) {
    if (w.leg == 0) return 0;
    for (int k = w.leg; k <= kWaybillSlots; ++k)
        if (w.legs[static_cast<size_t>(k - 1)]) return k;
    return 0;
}

std::optional<WaybillLeg> currentLeg(const Waybill& w) {
    const int k = currentSlot(w);
    return k ? w.legs[static_cast<size_t>(k - 1)] : std::nullopt;
}

std::pair<int, QString> turnedWaybill(const Waybill& w, const QString& load) {
    const int k = currentSlot(w);
    if (k == 0) return { 0, load };
    Waybill after = w;
    after.leg = k + 1 > kWaybillSlots ? 0 : k + 1;
    const int next = currentSlot(after);
    if (next) {
        const QString l = w.legs[static_cast<size_t>(next - 1)].value_or(WaybillLeg()).load;
        return { next, l.isEmpty() ? flipped(load) : l };
    }
    const QString l = w.legs[static_cast<size_t>(k - 1)].value_or(WaybillLeg()).load;
    return { 0, flipped(l.isEmpty() ? load : l) };
}

QString stopField(const QString& stopId) { return QStringLiteral("stop_") + stopId; }

Job asJob(const QJsonObject& r) {
    Job j;
    for (auto it = r.begin(); it != r.end(); ++it) {
        if (!it.key().startsWith(QLatin1String("stop_")) || !it.value().isObject()) continue;
        const QJsonObject s = it.value().toObject();
        const QString place = s.value(QLatin1String("place")).toString();
        if (place.isEmpty()) continue;
        j.stops.push_back({ it.key().mid(5), place, s.value(QLatin1String("at")).toDouble() });
    }
    std::sort(j.stops.begin(), j.stops.end(), [](const JobStop& a, const JobStop& b) { return a.at != b.at ? a.at < b.at : a.id < b.id; });
    constexpr size_t kKept = static_cast<size_t>(2) * kMaxStops;
    if (j.stops.size() > kKept) j.stops.resize(kKept);
    j.id = text(r, QStringLiteral("id"));
    j.name = text(r, QStringLiteral("name"));
    j.kind = text(r, QStringLiteral("kind"));
    if (j.kind.isEmpty()) j.kind = QStringLiteral("local");
    j.train = text(r, QStringLiteral("train"));
    j.order = r.value(QLatin1String("order")).toDouble();
    return j;
}

std::vector<Job> jobsInOrder(std::vector<Job> jobs) {
    std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
    return jobs;
}

std::optional<double> orderBetween(std::optional<double> a, std::optional<double> b) {
    if (!a && !b) return 1.0;
    if (!a) return *b - 1.0;
    if (!b) return *a + 1.0;
    const double m = (*a + *b) / 2.0;
    return m > *a && m < *b ? std::optional(m) : std::nullopt;
}

std::optional<Destination> destinationOf(const Waybill* waybill, const QString& home) {
    if (waybill)
        if (const auto leg = currentLeg(*waybill)) return Destination{ leg->to, leg->spot, false };
    if (home.isEmpty()) return std::nullopt;
    return Destination{ home, QString(), true };
}

std::optional<SpotRef> spotUnder(const std::vector<QPointF>& trucks, const std::vector<SpotLine>& spots) {
    const SpotLine* best = nullptr;
    int most = 0;
    for (const auto& s : spots) {
        const int n = static_cast<int>(std::count_if(trucks.begin(), trucks.end(), [&](QPointF t) { return nearLine(t, s.line); }));
        if (n > most || (n > 0 && n == most && best && std::pair(s.place, s.spot) < std::pair(best->place, best->spot))) {
            most = n;
            best = &s;
        }
    }
    return best ? std::optional(SpotRef{ best->place, best->spot }) : std::nullopt;
}

QMap<QString, int> trackPieces(const TrackNetwork& net) {
    QMap<QString, int> out;
    int n = 0;
    for (const QString& start : net.brickIds()) {
        if (out.contains(start)) continue;
        ++n;
        out.insert(start, n);
        QStringList queue{ start };
        while (!queue.isEmpty()) {
            const QString brick = queue.takeLast();
            const PartTrack* t = net.trackOf(brick);
            const int kinds = t ? static_cast<int>(t->kinds.size()) : 0;
            for (int c = 0; c < kinds; ++c) {
                const auto p = net.partner({ brick, c });
                if (p && !out.contains(p->brick)) {
                    out.insert(p->brick, n);
                    queue << p->brick;
                }
            }
        }
    }
    return out;
}

const std::vector<ListStop>* OpsRouting::listOf(const QString& job) const {
    for (const auto& [id, l] : lists)
        if (id == job) return &l;
    return nullptr;
}

QList<int> brokenStops(const QStringList& stops, const std::vector<OpsPlace>& places) {
    QList<int> out;
    const OpsPlace* last = nullptr;
    for (int i = 0; i < stops.size(); ++i) {
        const auto p = std::find_if(places.begin(), places.end(), [&](const OpsPlace& x) { return x.id == stops.at(i); });
        if (p == places.end()) continue;
        if (last && !placesJoined(*last, *p)) out << i;
        last = &*p;
    }
    return out;
}

OpsRouting routeCars(const std::vector<OpsPlace>& placesIn, const std::vector<OpsCar>& carsIn, const std::vector<OpsJob>& jobsIn, int maxSteps) {
    const std::vector<OpsPlace> placeList = firstById(placesIn);
    QHash<QString, const OpsPlace*> places;
    for (const auto& p : placeList) places.insert(p.id, &p);
    std::vector<OpsCar> cars = firstById(carsIn);
    std::sort(cars.begin(), cars.end(), [](const OpsCar& a, const OpsCar& b) { return a.id < b.id; });
    const std::vector<OpsJob> listed = firstById(jobsIn);

    struct Stop {
        int index;
        QString place;
        int piece;
    };
    struct JobWay {
        QString id;
        QString train;
        std::vector<Stop> stops;
    };
    struct Way {
        int jobs = 0;
        int job = 0;
        int pickUp = -1;
        int setOut = 0;
    };
    std::vector<JobWay> jobs;
    jobs.reserve(listed.size());
    for (const auto& j : listed) {
        JobWay w{ j.id, j.train, {} };
        int piece = 0;
        for (int i = 0; i < j.stops.size(); ++i) {
            const OpsPlace* p = places.value(j.stops.at(i));
            if (!p) continue;
            if (!w.stops.empty() && !placesJoined(*places.value(w.stops.back().place), *p)) ++piece;
            w.stops.push_back({ i, p->id, piece });
        }
        jobs.push_back(std::move(w));
    }
    int steps = maxSteps;
    const auto isYard = [&](const QString& place) { return places.value(place)->kind == QLatin1String("yard"); };

    // The way from every place to one, by whole jobs: the first leg of each. None: too much work.
    using Ways = QHash<QString, Way>;
    QHash<QString, std::optional<Ways>> ways;
    const auto waysTo = [&](const QString& dest, bool loose) -> const std::optional<Ways>& {
        const QString key = (loose ? QLatin1Char('1') : QLatin1Char('0')) + dest;
        const auto had = ways.constFind(key);
        if (had != ways.constEnd()) return *had;
        Ways out;
        for (int r = 1; r <= kMaxHops; ++r) {
            bool grew = false;
            for (int ji = 0; ji < static_cast<int>(jobs.size()); ++ji) {
                const auto& stops = jobs[static_cast<size_t>(ji)].stops;
                steps -= static_cast<int>(stops.size());
                if (steps < 0) return *ways.insert(key, std::nullopt);
                // From the last stop back: the next stop on that the car can be set out at.
                int target = -1;
                for (int k = static_cast<int>(stops.size()) - 1; k >= 0; --k) {
                    const Stop& s = stops[static_cast<size_t>(k)];
                    if (target >= 0 && !loose && stops[static_cast<size_t>(target)].piece != s.piece) target = -1;
                    const auto it = out.constFind(s.place);
                    // (As it was before this stop: an insert below doesn't change it.)
                    const bool had = it != out.constEnd();
                    const int hadJobs = had ? it->jobs : 0;
                    const int hadJob = had ? it->job : -1;
                    if (target >= 0 && s.place != dest && (!had || (hadJobs == r && hadJob == ji))) {
                        out.insert(s.place, { r, ji, k, target });
                        grew = true;
                    }
                    if (r == 1 ? s.place == dest : isYard(s.place) && had && hadJobs == r - 1) target = k;
                }
            }
            if (!grew) break;
        }
        return *ways.insert(key, std::move(out));
    };

    OpsRouting out;
    struct Going {
        const OpsCar* car;
        Way way;
        bool aboard;
        bool last;
    };
    std::vector<Going> going;
    QSet<QString> ridden;
    QMap<QString, CantRoute> cant;
    const auto cannot = [&](const OpsCar& c, const char* why) -> CantRoute& {
        CantRoute r;
        r.car = c.id;
        r.why = QLatin1String(why);
        return *cant.insert(c.id, r);
    };

    for (const auto& c : cars) {
        std::vector<int> aboard;
        if (!c.train.isEmpty())
            for (int i = 0; i < static_cast<int>(jobs.size()); ++i)
                if (jobs[static_cast<size_t>(i)].train == c.train) aboard.push_back(i);
        if (!aboard.empty()) ridden.insert(c.id);
        if (!c.to) continue;
        const OpsPlace* dest = places.value(c.to->place);
        if (!dest) {
            auto& r = cannot(c, "placeGone");
            r.place = c.to->place;
            r.byHome = c.to->home;
            continue;
        }
        const bool wantSpot = !c.to->spot.isEmpty() && std::any_of(dest->spots.begin(), dest->spots.end(), [&](const OpsSpot& s) { return s.id == c.to->spot; });
        const SpotRef* here = c.at && places.contains(c.at->place) ? &*c.at : nullptr;
        if (aboard.empty() && here && here->place == dest->id && (!wantSpot || here->spot == c.to->spot)) {
            out.arrived << c.id;
            continue;
        }
        if (c.hold) {
            cannot(c, "held");
            continue;
        }
        if (!aboard.empty()) {
            // On a job's train: set out where it goes, else at the yard nearest to it by jobs.
            std::optional<Way> found;
            bool last = false;
            bool spent = false;
            for (const int ji : aboard) {
                const auto& stops = jobs[static_cast<size_t>(ji)].stops;
                const int n = static_cast<int>(stops.size());
                const auto find = [&](const QString& place, int from) {
                    for (int k = std::max(0, from); k < n; ++k)
                        if (stops[static_cast<size_t>(k)].place == place) return k;
                    return -1;
                };
                const int hereAt = here ? find(here->place, 0) : -1;
                const int after = find(dest->id, hereAt);
                const int direct = after >= 0 ? after : find(dest->id, 0);
                if (direct >= 0) {
                    found = Way{ 1, ji, -1, direct };
                    last = true;
                    break;
                }
                const auto& on = waysTo(dest->id, false);
                if (!on) {
                    spent = true;
                    break;
                }
                for (int k = 0; k < n; ++k) {
                    const QString& at = stops[static_cast<size_t>(k)].place;
                    const auto w = isYard(at) ? on->constFind(at) : on->constEnd();
                    if (w != on->constEnd() && (!found || w->jobs + 1 < found->jobs)) found = Way{ w->jobs + 1, ji, -1, k };
                }
                if (found) break;
            }
            if (found) going.push_back({ &c, *found, true, last });
            else if (spent) cannot(c, "tooMuch");
            else cannot(c, "noJob").place = dest->id;
            continue;
        }
        if (!here) {
            cannot(c, c.onTrack ? "nowhere" : "offTrack");
            continue;
        }
        if (here->place == dest->id) {
            // At the place, another spot: the first job that stops there.
            bool done = false;
            for (int ji = 0; ji < static_cast<int>(jobs.size()) && !done; ++ji) {
                const auto& stops = jobs[static_cast<size_t>(ji)].stops;
                const auto k = std::find_if(stops.begin(), stops.end(), [&](const Stop& s) { return s.place == dest->id; });
                if (k == stops.end()) continue;
                const int at = static_cast<int>(k - stops.begin());
                going.push_back({ &c, { 1, ji, at, at }, false, true });
                done = true;
            }
            if (!done) {
                auto& r = cannot(c, "noJob");
                r.from = dest->id;
                r.place = dest->id;
            }
            continue;
        }
        const auto& on = waysTo(dest->id, false);
        if (!on) {
            cannot(c, "tooMuch");
            continue;
        }
        const auto way = on->constFind(here->place);
        if (way != on->constEnd()) {
            going.push_back({ &c, *way, false, way->jobs == 1 });
            continue;
        }
        const auto& loose = waysTo(dest->id, true);
        auto& r = cannot(c, loose && loose->contains(here->place) ? "noTrack" : "noJob");
        r.from = here->place;
        r.place = dest->id;
    }

    // Room: those that leave make room; one that finds none stays, and takes up its own again.
    QMap<QString, CantRoute> stuck;
    QHash<QString, QString> spotsFor;
    struct Use {
        int count = 0;
        double length = 0.0;
    };
    for (int round = 0; round < kMaxRounds; ++round) {
        QSet<QString> leaving;
        for (const auto& g : going)
            if (!stuck.contains(g.car->id)) leaving.insert(g.car->id);
        QHash<std::pair<QString, QString>, Use> used;
        const auto take = [&](const QString& place, const QString& spot, double length) {
            Use& u = used[{ place, spot }];
            ++u.count;
            u.length += length;
        };
        for (const auto& c : cars)
            if (c.at && !leaving.contains(c.id) && !ridden.contains(c.id)) take(c.at->place, c.at->spot, c.length);
        const auto fits = [&](const QString& place, const OpsSpot& s, double length) {
            const Use u = used.value({ place, s.id });
            if (s.capacity) return u.count < *s.capacity;
            return !s.usable || u.length + length <= *s.usable + 1e-6;
        };
        spotsFor.clear();
        bool more = false;
        for (const auto& g : going) {
            if (stuck.contains(g.car->id)) continue;
            const OpsPlace& place = *places.value(jobs[static_cast<size_t>(g.way.job)].stops[static_cast<size_t>(g.way.setOut)].place);
            if (place.spots.empty()) {
                spotsFor.insert(g.car->id, QString());
                continue;
            }
            std::vector<const OpsSpot*> tries;
            bool named = false;
            if (g.last && !g.car->to->spot.isEmpty())
                for (const auto& s : place.spots)
                    if (s.id == g.car->to->spot) tries.push_back(&s);
            named = !tries.empty();
            if (!named) {
                for (const auto& s : place.spots)
                    if (s.role != QLatin1String("rip") && s.role != QLatin1String("engine")) tries.push_back(&s);
                if (tries.empty())
                    for (const auto& s : place.spots) tries.push_back(&s);
            }
            const auto room = std::find_if(tries.begin(), tries.end(), [&](const OpsSpot* s) { return fits(place.id, *s, g.car->length); });
            if (room != tries.end()) {
                take(place.id, (*room)->id, g.car->length);
                spotsFor.insert(g.car->id, (*room)->id);
            } else if (!g.last) {
                // A yard on the way: the yardmaster finds it a track.
                spotsFor.insert(g.car->id, QString());
            } else {
                CantRoute r;
                r.car = g.car->id;
                r.why = QStringLiteral("full");
                r.place = place.id;
                if (named) r.spot = tries.front()->id;
                int total = 0;
                bool known = true;
                for (const auto* s : tries) {
                    r.count += used.value({ place.id, s->id }).count;
                    known = known && s->capacity.has_value();
                    total += s->capacity.value_or(0);
                }
                if (known) r.capacity = total;
                stuck.insert(g.car->id, r);
                more = true;
            }
        }
        if (!more) break;
    }
    for (auto it = stuck.constBegin(); it != stuck.constEnd(); ++it) cant.insert(it.key(), it.value());

    // The lists.
    for (const auto& j : listed) {
        std::vector<ListStop> l;
        for (const QString& place : j.stops) l.push_back({ place, {}, {} });
        out.lists.emplace_back(j.id, std::move(l));
    }
    std::vector<Going> placed;
    std::copy_if(going.begin(), going.end(), std::back_inserter(placed), [&](const Going& g) { return !stuck.contains(g.car->id); });
    const auto spotIndex = [&](const OpsCar& c) {
        const OpsPlace* p = c.at ? places.value(c.at->place) : nullptr;
        if (!p) return 0;
        for (size_t i = 0; i < p->spots.size(); ++i)
            if (p->spots[i].id == c.at->spot) return static_cast<int>(i);
        return 0;
    };
    std::stable_sort(placed.begin(), placed.end(), [&](const Going& a, const Going& b) {
        if (a.way.job != b.way.job) return a.way.job < b.way.job;
        if (a.aboard != b.aboard) return a.aboard;
        if (a.aboard) {
            if (a.car->fromEngine != b.car->fromEngine) return a.car->fromEngine < b.car->fromEngine;
        } else {
            if (a.way.pickUp != b.way.pickUp) return a.way.pickUp < b.way.pickUp;
            const int sa = spotIndex(*a.car);
            const int sb = spotIndex(*b.car);
            if (sa != sb) return sa < sb;
            if (a.car->train != b.car->train) return a.car->train < b.car->train;
        }
        if (a.car->unit != b.car->unit) return a.car->unit < b.car->unit;
        return a.car->id < b.car->id;
    });
    for (const auto& g : placed) {
        const JobWay& job = jobs[static_cast<size_t>(g.way.job)];
        auto& list = out.lists[static_cast<size_t>(g.way.job)].second;
        const int setOut = job.stops[static_cast<size_t>(g.way.setOut)].index;
        const int pickUp = g.aboard ? -1 : job.stops[static_cast<size_t>(g.way.pickUp)].index;
        const QString spot = spotsFor.value(g.car->id);
        out.cars.insert(g.car->id, { job.id, pickUp, setOut, spot, g.way.jobs });
        list[static_cast<size_t>(setOut)].setOut.push_back({ g.car->id, spot });
        if (pickUp >= 0) list[static_cast<size_t>(pickUp)].pickUp.push_back({ g.car->id, g.car->to->place });
    }
    for (const auto& r : std::as_const(cant)) out.cantRoute.push_back(r);
    return out;
}

MadeWaybills makeWaybills(const std::vector<MakeCar>& carsIn, const std::vector<MakePlace>& placesIn, const QMap<QString, int>& heading, quint32 seed) {
    Seeded rnd(seed);
    std::vector<MakePlace> places = firstById(placesIn);
    std::sort(places.begin(), places.end(), [](const MakePlace& a, const MakePlace& b) { return a.id < b.id; });
    if (places.size() > kMakePlaces) places.resize(kMakePlaces);
    using List = std::vector<const MakePlace*>;
    QHash<QString, List> ships;
    QHash<QString, List> accepts;
    for (const auto& p : places)
        for (const bool out : { true, false }) {
            QSet<QString> seen;
            for (const QString& word : out ? p.ships : p.accepts) {
                const QString t = typeKey(word);
                if (t.isEmpty() || seen.contains(t)) continue;
                seen.insert(t);
                (out ? ships : accepts)[t].push_back(&p);
            }
        }
    std::vector<MakeCar> cars = firstById(carsIn);
    std::sort(cars.begin(), cars.end(), [](const MakeCar& a, const MakeCar& b) { return a.id < b.id; });
    for (size_t i = cars.size(); i-- > 1;) std::swap(cars[i], cars[static_cast<size_t>(rnd.below(i + 1))]);
    QHash<QString, int> load;
    const auto going = [&](const MakePlace& p) { return load.value(p.id, heading.value(p.id, 0)); };
    const auto room = [&](const MakePlace& p) { return !p.capacity || going(p) < *p.capacity; };
    const auto pick = [&](const List& list, const auto& ok) -> const MakePlace* {
        int least = std::numeric_limits<int>::max();
        List from;
        for (const auto* p : list) {
            if (!ok(*p)) continue;
            const int n = going(*p);
            if (n < least) {
                least = n;
                from = { p };
            } else if (n == least) from.push_back(p);
        }
        return from.empty() ? nullptr : from[static_cast<size_t>(rnd.below(from.size()))];
    };
    MadeWaybills out;
    for (const auto& c : cars) {
        const QString t = typeKey(c.type);
        if (t.isEmpty()) {
            out.skipped.emplace_back(c.id, QStringLiteral("noType"));
            continue;
        }
        const List s = ships.value(t);
        const List a = accepts.value(t);
        if (std::none_of(s.begin(), s.end(), [&](const MakePlace* p) { return std::any_of(a.begin(), a.end(), [&](const MakePlace* q) { return q->id != p->id; }); })) {
            out.skipped.emplace_back(c.id, QStringLiteral("noPlaces"));
            continue;
        }
        // Loaded where it stands when that sends out its type, else somewhere that does and has room.
        const auto isOnly = [&](const MakePlace& p) { return a.size() == 1 && a.front()->id == p.id; };
        const auto ownAt = std::find_if(s.begin(), s.end(), [&](const MakePlace* p) { return p->id == c.at && !isOnly(*p); });
        const bool own = ownAt != s.end();
        const MakePlace* from = own ? *ownAt : pick(s, [&](const MakePlace& p) { return room(p) && !isOnly(p); });
        const MakePlace* to = from ? pick(a, [&](const MakePlace& p) { return p.id != from->id && room(p); }) : nullptr;
        if (!from || !to) {
            out.skipped.emplace_back(c.id, QStringLiteral("full"));
            continue;
        }
        if (!own) load.insert(from->id, going(*from) + 1);
        load.insert(to->id, going(*to) + 1);
        MadeWaybill m{ c.id, from->id, {} };
        if (!own) m.legs.push_back({ from->id, QString(), QStringLiteral("empty"), QString() });
        m.legs.push_back({ to->id, QString(), QStringLiteral("loaded"), QString() });
        out.made.push_back(std::move(m));
    }
    return out;
}

}  // namespace bld::track
