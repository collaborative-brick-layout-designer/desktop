// Waybills, jobs and routing against the shared contract (fixtures/
// contracts/ops-routing.json): the web's packages/parts-catalog/src/track/
// ops.test.ts checks the same, so a layout routes the same in both apps.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/SidecarIO.h"
#include "track/OpsView.h"

#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>

using namespace bld;
using namespace bld::track;

namespace {

QJsonObject contract() {
    static const QJsonObject d = [] {
        QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/ops-routing.json"));
        EXPECT_TRUE(f.open(QIODevice::ReadOnly));
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return d;
}

QString str(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toString(); }
std::optional<int> whole(const QJsonValue& v) { return v.isDouble() ? std::optional(v.toInt()) : std::nullopt; }
std::optional<double> number(const QJsonValue& v) { return v.isDouble() ? std::optional(v.toDouble()) : std::nullopt; }
QJsonValue orNull(const std::optional<int>& v) { return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null); }
QStringList words(const QJsonValue& v) {
    QStringList out;
    for (const auto& x : v.toArray()) out << x.toString();
    return out;
}
std::string text(const QJsonValue& v) { return QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact).toStdString(); }

std::vector<OpsPlace> placesOf(const QJsonValue& v) {
    std::vector<OpsPlace> out;
    for (const auto& pv : v.toArray()) {
        OpsPlace p{ str(pv.toObject(), "id"), str(pv.toObject(), "kind"), {} };
        for (const auto& sv : pv.toObject().value(QLatin1String("spots")).toArray()) {
            const QJsonObject s = sv.toObject();
            p.spots.push_back({ str(s, "id"), whole(s.value(QLatin1String("capacity"))), number(s.value(QLatin1String("usable"))), str(s, "role"), whole(s.value(QLatin1String("track"))) });
        }
        out.push_back(p);
    }
    return out;
}

QJsonObject legJson(const WaybillLeg& l) {
    return { { QStringLiteral("to"), l.to }, { QStringLiteral("spot"), l.spot }, { QStringLiteral("load"), l.load }, { QStringLiteral("note"), l.note } };
}

// A routing as the contract writes it.
QJsonObject shown(const OpsRouting& r) {
    QJsonArray cars, cant, lists;
    for (auto it = r.cars.constBegin(); it != r.cars.constEnd(); ++it)
        cars.append(QJsonObject{ { QStringLiteral("car"), it.key() }, { QStringLiteral("job"), it->job }, { QStringLiteral("pickUp"), it->pickUp }, { QStringLiteral("setOut"), it->setOut },
                                 { QStringLiteral("spot"), it->spot }, { QStringLiteral("jobs"), it->jobs } });
    for (const auto& c : r.cantRoute) {
        QJsonObject o{ { QStringLiteral("car"), c.car }, { QStringLiteral("why"), c.why } };
        if (c.why == QLatin1String("placeGone")) {
            o.insert(QStringLiteral("place"), c.place);
            o.insert(QStringLiteral("by"), c.byHome ? QStringLiteral("home") : QStringLiteral("waybill"));
        } else if (c.why == QLatin1String("noJob") || c.why == QLatin1String("noTrack")) {
            o.insert(QStringLiteral("from"), c.from);
            o.insert(QStringLiteral("place"), c.place);
        } else if (c.why == QLatin1String("full")) {
            o.insert(QStringLiteral("place"), c.place);
            o.insert(QStringLiteral("spot"), c.spot);
            o.insert(QStringLiteral("count"), c.count);
            o.insert(QStringLiteral("capacity"), orNull(c.capacity));
        }
        cant.append(o);
    }
    for (const auto& [job, list] : r.lists) {
        QJsonArray stops;
        for (const auto& s : list) {
            QJsonArray setOut, pickUp;
            for (const auto& m : s.setOut) setOut.append(QJsonObject{ { QStringLiteral("car"), m.car }, { QStringLiteral("spot"), m.spot } });
            for (const auto& m : s.pickUp) pickUp.append(QJsonObject{ { QStringLiteral("car"), m.car }, { QStringLiteral("to"), m.to } });
            stops.append(QJsonObject{ { QStringLiteral("place"), s.place }, { QStringLiteral("setOut"), setOut }, { QStringLiteral("pickUp"), pickUp } });
        }
        lists.append(QJsonObject{ { QStringLiteral("job"), job }, { QStringLiteral("stops"), stops } });
    }
    return { { QStringLiteral("cars"), cars }, { QStringLiteral("arrived"), QJsonArray::fromStringList(r.arrived) }, { QStringLiteral("cantRoute"), cant }, { QStringLiteral("lists"), lists } };
}

}  // namespace

TEST(OpsContract, Limits) {
    const QJsonObject got{ { QStringLiteral("waybills"), kMaxWaybills }, { QStringLiteral("jobs"), kMaxJobs },   { QStringLiteral("stops"), kMaxStops },
                           { QStringLiteral("name"), kMaxJobName },      { QStringLiteral("note"), kMaxLegNote }, { QStringLiteral("slots"), kWaybillSlots },
                           { QStringLiteral("steps"), kRouteSteps },     { QStringLiteral("kinds"), QJsonArray::fromStringList(jobKinds()) } };
    EXPECT_EQ(text(got), text(contract().value(QLatin1String("limits"))));
}

TEST(OpsContract, Routing) {
    const QJsonArray cases = contract().value(QLatin1String("routing")).toArray();
    EXPECT_GT(cases.size(), 15);
    for (const auto& cv : cases) {
        const QJsonObject c = cv.toObject();
        std::vector<OpsCar> cars;
        for (const auto& v : c.value(QLatin1String("cars")).toArray()) {
            const QJsonObject o = v.toObject();
            OpsCar car;
            car.id = str(o, "id");
            car.length = o.value(QLatin1String("length")).toDouble();
            if (o.value(QLatin1String("at")).isObject()) car.at = SpotRef{ str(o.value(QLatin1String("at")).toObject(), "place"), str(o.value(QLatin1String("at")).toObject(), "spot") };
            car.onTrack = o.value(QLatin1String("onTrack")).toBool();
            car.train = str(o, "train");
            car.unit = o.value(QLatin1String("unit")).toInt();
            car.fromEngine = o.value(QLatin1String("fromEngine")).toInt();
            if (const QJsonObject to = o.value(QLatin1String("to")).toObject(); !to.isEmpty()) car.to = Destination{ str(to, "place"), str(to, "spot"), str(to, "why") == QLatin1String("home") };
            car.hold = o.value(QLatin1String("hold")).toBool();
            cars.push_back(car);
        }
        std::vector<OpsJob> jobs;
        for (const auto& v : c.value(QLatin1String("jobs")).toArray()) jobs.push_back({ str(v.toObject(), "id"), str(v.toObject(), "train"), words(v.toObject().value(QLatin1String("stops"))) });
        const auto got = routeCars(placesOf(c.value(QLatin1String("places"))), cars, jobs, c.value(QLatin1String("maxSteps")).toInt(kRouteSteps));
        EXPECT_EQ(text(shown(got)), text(c.value(QLatin1String("expect")))) << str(c, "name").toStdString();
    }
}

TEST(OpsContract, MakeWaybills) {
    for (const auto& cv : contract().value(QLatin1String("make")).toArray()) {
        const QJsonObject c = cv.toObject();
        std::vector<MakeCar> cars;
        for (const auto& v : c.value(QLatin1String("cars")).toArray()) cars.push_back({ str(v.toObject(), "id"), str(v.toObject(), "type"), str(v.toObject(), "at") });
        std::vector<MakePlace> places;
        for (const auto& v : c.value(QLatin1String("places")).toArray()) {
            const QJsonObject o = v.toObject();
            places.push_back({ str(o, "id"), words(o.value(QLatin1String("ships"))), words(o.value(QLatin1String("accepts"))), whole(o.value(QLatin1String("capacity"))) });
        }
        QMap<QString, int> heading;
        const QJsonObject h = c.value(QLatin1String("heading")).toObject();
        for (auto it = h.begin(); it != h.end(); ++it) heading.insert(it.key(), it.value().toInt());
        const auto got = makeWaybills(cars, places, heading, static_cast<quint32>(c.value(QLatin1String("seed")).toDouble()));
        QJsonArray made, skipped;
        for (const auto& m : got.made) {
            QJsonArray legs;
            for (const auto& l : m.legs) legs.append(legJson(l));
            made.append(QJsonObject{ { QStringLiteral("car"), m.car }, { QStringLiteral("from"), m.from }, { QStringLiteral("legs"), legs } });
        }
        for (const auto& [car, why] : got.skipped) skipped.append(QJsonObject{ { QStringLiteral("car"), car }, { QStringLiteral("why"), why } });
        EXPECT_EQ(text(QJsonObject{ { QStringLiteral("made"), made }, { QStringLiteral("skipped"), skipped } }), text(c.value(QLatin1String("expect")))) << str(c, "name").toStdString();
    }
}

TEST(OpsContract, WaybillsAndJobsAsRead) {
    for (const auto& cv : contract().value(QLatin1String("waybills")).toArray()) {
        const QJsonObject c = cv.toObject();
        const Waybill w = asWaybill(c.value(QLatin1String("record")).toObject());
        QJsonArray legs;
        for (const auto& l : w.legs) legs.append(l ? QJsonValue(legJson(*l)) : QJsonValue(QJsonValue::Null));
        const auto [leg, load] = turnedWaybill(w, str(c, "load"));
        const QJsonObject got{ { QStringLiteral("waybill"), QJsonObject{ { QStringLiteral("id"), w.id }, { QStringLiteral("car"), w.car }, { QStringLiteral("from"), w.from }, { QStringLiteral("leg"), w.leg }, { QStringLiteral("legs"), legs } } },
                               { QStringLiteral("slot"), currentSlot(w) },
                               { QStringLiteral("turned"), QJsonObject{ { QStringLiteral("leg"), leg }, { QStringLiteral("load"), load } } } };
        EXPECT_EQ(text(got), text(c.value(QLatin1String("expect")))) << str(c, "name").toStdString();
    }
    for (const auto& cv : contract().value(QLatin1String("jobs")).toArray()) {
        const QJsonObject c = cv.toObject();
        const Job j = asJob(c.value(QLatin1String("record")).toObject());
        QJsonArray stops;
        for (const auto& s : j.stops) stops.append(QJsonObject{ { QStringLiteral("id"), s.id }, { QStringLiteral("place"), s.place }, { QStringLiteral("at"), s.at } });
        const QJsonObject got{ { QStringLiteral("id"), j.id }, { QStringLiteral("name"), j.name }, { QStringLiteral("kind"), j.kind }, { QStringLiteral("train"), j.train }, { QStringLiteral("order"), j.order }, { QStringLiteral("stops"), stops } };
        EXPECT_EQ(text(got), text(c.value(QLatin1String("expect")))) << str(c, "name").toStdString();
    }
}

TEST(OpsContract, SpotsBrokenStopsAndOrder) {
    const QJsonObject su = contract().value(QLatin1String("spotUnder")).toObject();
    const auto points = [](const QJsonValue& v) {
        std::vector<QPointF> out;
        for (const auto& p : v.toArray()) out.emplace_back(p.toArray()[0].toDouble(), p.toArray()[1].toDouble());
        return out;
    };
    std::vector<SpotLine> lines;
    for (const auto& v : su.value(QLatin1String("spots")).toArray()) lines.push_back({ str(v.toObject(), "place"), str(v.toObject(), "spot"), points(v.toObject().value(QLatin1String("line"))) });
    for (const auto& cv : su.value(QLatin1String("cases")).toArray()) {
        const auto got = spotUnder(points(cv.toObject().value(QLatin1String("trucks"))), lines);
        EXPECT_EQ(text(got ? QJsonValue(QJsonObject{ { QStringLiteral("place"), got->place }, { QStringLiteral("spot"), got->spot } }) : QJsonValue(QJsonValue::Null)),
                  text(cv.toObject().value(QLatin1String("expect"))));
    }
    const QJsonObject b = contract().value(QLatin1String("broken")).toObject();
    const auto places = placesOf(b.value(QLatin1String("places")));
    for (const auto& cv : b.value(QLatin1String("cases")).toArray()) {
        QJsonArray got;
        for (const int i : brokenStops(words(cv.toObject().value(QLatin1String("stops"))), places)) got.append(i);
        EXPECT_EQ(text(got), text(cv.toObject().value(QLatin1String("expect"))));
    }
    for (const auto& cv : contract().value(QLatin1String("between")).toArray()) {
        const QJsonObject c = cv.toObject();
        const auto got = orderBetween(number(c.value(QLatin1String("a"))), number(c.value(QLatin1String("b"))));
        EXPECT_EQ(text(got ? QJsonValue(*got) : QJsonValue(QJsonValue::Null)), text(c.value(QLatin1String("expect"))));
    }
}

// Big layouts route without freezing, or say it is too much; every car is accounted for.
TEST(Ops, ABigLayout) {
    std::vector<OpsPlace> places;
    std::vector<OpsCar> cars;
    std::vector<OpsJob> jobs;
    for (int i = 0; i < 1000; ++i) places.push_back({ QStringLiteral("p%1").arg(i), i % 10 ? QStringLiteral("industry") : QStringLiteral("yard"), { { QStringLiteral("s"), 5, std::nullopt, QString(), 1 } } });
    for (int i = 0; i < 2000; ++i) {
        OpsCar c;
        c.id = QStringLiteral("c%1").arg(i);
        c.at = SpotRef{ QStringLiteral("p%1").arg(i % 1000), QStringLiteral("s") };
        c.onTrack = true;
        c.to = Destination{ QStringLiteral("p%1").arg((i * 7 + 3) % 1000), QString(), false };
        cars.push_back(c);
    }
    for (int j = 0; j < 100; ++j) {
        OpsJob job{ QStringLiteral("j%1").arg(j), QString(), {} };
        for (int k = 0; k < 40; ++k) job.stops << QStringLiteral("p%1").arg((j * 37 + k * 11) % 1000);
        jobs.push_back(job);
    }
    QElapsedTimer t;
    t.start();
    const auto r = routeCars(places, cars, jobs);
    EXPECT_LT(t.elapsed(), 5000);
    EXPECT_EQ(r.cars.size() + r.arrived.size() + static_cast<qsizetype>(r.cantRoute.size()), 2000);
    EXPECT_TRUE(std::any_of(r.cantRoute.begin(), r.cantRoute.end(), [](const CantRoute& c) { return c.why == QLatin1String("tooMuch"); }));
}

// What the sidecar keeps: records and a car's fields as they were written, unknown ones too.
TEST(Ops, TheFileKeepsWaybillsAndJobsAsTheyAre) {
    const QJsonObject root{
        { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("waybills"), QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("w") }, { QStringLiteral("car"), QStringLiteral("a") }, { QStringLiteral("leg"), 2 }, { QStringLiteral("later"), QJsonArray{ 1 } } }, QJsonObject{ { QStringLiteral("car"), QStringLiteral("x") } }, 7 } },
        { QStringLiteral("opsJobs"), QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("j") }, { QStringLiteral("stop_a"), QJsonObject{ { QStringLiteral("place"), QStringLiteral("p") }, { QStringLiteral("at"), 1.5 } } } } } },
        { QStringLiteral("rollingStock"), QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("a") }, { QStringLiteral("length"), 20 }, { QStringLiteral("opsWaybill"), QStringLiteral("w") }, { QStringLiteral("opsHome"), QString() }, { QStringLiteral("opsHold"), false } } } },
    };
    core::Sidecar sc;
    saveload::sidecarFromJson(root, sc);
    ASSERT_EQ(sc.waybills.size(), 1);
    EXPECT_EQ(sc.waybills.at(0), root.value(QLatin1String("waybills")).toArray().at(0));
    EXPECT_EQ(sc.opsJobs, root.value(QLatin1String("opsJobs")).toArray());
    EXPECT_FALSE(sc.isEmpty());
    const QJsonObject car = saveload::rollingStockToJson(sc.rollingStock.at(0));
    EXPECT_EQ(str(car, "opsWaybill"), QStringLiteral("w"));
    EXPECT_TRUE(car.value(QLatin1String("opsHome")).isString());
    EXPECT_TRUE(car.value(QLatin1String("opsHold")).isBool());
    EXPECT_FALSE(car.contains(QLatin1String("opsAt")));
    const QJsonObject back = saveload::sidecarToJson(sc);
    EXPECT_EQ(back.value(QLatin1String("waybills")).toArray().size(), 1);
    EXPECT_EQ(back.value(QLatin1String("opsJobs")), root.value(QLatin1String("opsJobs")));
}
