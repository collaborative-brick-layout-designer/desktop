#pragma once

// Waybills, jobs and the routing of cars (Live Ops design 2.4, 3): where
// each car goes next, which job takes it there, the spot it is set out at,
// and each job's switch list; or, plainly, why it can't be routed. Worked
// out from the records, never stored.
//
// A waybill has four slots, as the four sides of a printed one, each a
// place to go to; its fields are flat (`to1` ... `note4`), and a job's stops
// are a field each (`stop_<id>`), so two people who change different things
// at once lose nothing. A waybill is in a car's pocket when both say so (the
// car's opsWaybill, the waybill's `car`); else it waits for a car.
//
// The web's packages/parts-catalog/src/track/ops.ts is the twin; both check
// fixtures/contracts/ops-routing.json.

#include "TrackNetwork.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace bld::track {

// What the apps hold a layout to (the server takes twice the counts).
inline constexpr int kMaxWaybills = 2000;
inline constexpr int kMaxJobs = 100;
inline constexpr int kMaxStops = 40;
inline constexpr int kMaxJobName = 80;
inline constexpr int kMaxLegNote = 200;
// How much work the routing does before it gives up (and says so): a step is one stop looked at.
inline constexpr int kRouteSteps = 600000;
inline constexpr int kWaybillSlots = 4;

QStringList jobKinds();  // local, yard, through, turn

struct WaybillLeg {
    QString to;    // the place (its id)
    QString spot;  // empty: any
    QString load;  // loaded, empty, or none
    QString note;
    bool operator==(const WaybillLeg&) const = default;
};

struct Waybill {
    QString id;
    QString car;   // empty: it waits for one
    QString from;  // where its first leg starts
    int leg = 1;   // the slot it is on, 1 to 4; 0: finished
    std::array<std::optional<WaybillLeg>, kWaybillSlots> legs;
};
Waybill asWaybill(const QJsonObject& r);
// The slot a waybill is on (1 to 4), skipping those not used; 0: finished.
int currentSlot(const Waybill& w);
std::optional<WaybillLeg> currentLeg(const Waybill& w);
// A car's waybill turned to its next leg: the slot it is on then, and the car's load.
std::pair<int, QString> turnedWaybill(const Waybill& w, const QString& load);

struct JobStop {
    QString id;  // the field is stop_<id>
    QString place;
    double at = 0.0;
};

struct Job {
    QString id;
    QString name;
    QString kind = QStringLiteral("local");
    QString train;
    double order = 0.0;
    std::vector<JobStop> stops;  // in order
};
Job asJob(const QJsonObject& r);
QString stopField(const QString& stopId);
std::vector<Job> jobsInOrder(std::vector<Job> jobs);
// A number between two others; none: there is none left, number them all again.
std::optional<double> orderBetween(std::optional<double> a, std::optional<double> b);

struct Destination {
    QString place;
    QString spot;  // empty: any
    bool home = false;  // its home, not its waybill
};
// Where a car goes: its waybill's leg, else its home, else nowhere.
std::optional<Destination> destinationOf(const Waybill* waybill, const QString& home);

// ---- where cars are

struct SpotLine {
    QString place;
    QString spot;
    std::vector<QPointF> line;
};
struct SpotRef {
    QString place;
    QString spot;
    bool operator==(const SpotRef&) const = default;
};
// Which spot a car is at: the one with the most of its trucks on it.
std::optional<SpotRef> spotUnder(const std::vector<QPointF>& trucks, const std::vector<SpotLine>& spots);
// Which piece of joined-up track each brick is on.
QMap<QString, int> trackPieces(const TrackNetwork& net);

// ---- routing

struct OpsSpot {
    QString id;
    std::optional<int> capacity;   // cars it says it holds
    std::optional<double> usable;  // the track's usable length, studs
    QString role;
    std::optional<int> track;  // the piece of joined-up track it is on
};
struct OpsPlace {
    QString id;
    QString kind;
    std::vector<OpsSpot> spots;
};
struct OpsCar {
    QString id;
    double length = 32.0;
    std::optional<SpotRef> at;  // spot empty: at a place off the layout
    bool onTrack = false;
    QString train;  // the train or cut it stands in
    int unit = 0;
    int fromEngine = 0;
    std::optional<Destination> to;
    bool hold = false;
};
struct OpsJob {
    QString id;
    QString train;
    QStringList stops;  // place ids
};
struct CantRoute {
    QString car;
    QString why;  // held, offTrack, nowhere, tooMuch, placeGone, noJob, noTrack, full
    QString place;
    QString from;
    QString spot;
    bool byHome = false;
    int count = 0;
    std::optional<int> capacity;
};
struct RoutedCar {
    QString job;
    int pickUp = -1;  // the stop it is picked up at; -1: it is on the job's train
    int setOut = 0;
    QString spot;
    int jobs = 1;  // how many jobs it still takes, this one included
};
struct ListStop {
    struct SetOut {
        QString car, spot;
    };
    struct PickUp {
        QString car, to;
    };
    QString place;
    std::vector<SetOut> setOut;
    std::vector<PickUp> pickUp;
};
struct OpsRouting {
    QMap<QString, RoutedCar> cars;
    QStringList arrived;
    std::vector<CantRoute> cantRoute;
    std::vector<std::pair<QString, std::vector<ListStop>>> lists;  // as the jobs come
    const std::vector<ListStop>* listOf(const QString& job) const;
};
// Every car's next move (the web's routeCars has the rules).
OpsRouting routeCars(const std::vector<OpsPlace>& places, const std::vector<OpsCar>& cars, const std::vector<OpsJob>& jobs,
                     int maxSteps = kRouteSteps);
// The stops a job goes on from that no track joins to the one before (indexes of its stops).
QList<int> brokenStops(const QStringList& stops, const std::vector<OpsPlace>& places);

// ---- making waybills

struct MakeCar {
    QString id, type, at;
};
struct MakePlace {
    QString id;
    QStringList ships, accepts;
    std::optional<int> capacity;
};
struct MadeWaybill {
    QString car, from;
    std::vector<WaybillLeg> legs;
};
struct MadeWaybills {
    std::vector<MadeWaybill> made;
    std::vector<std::pair<QString, QString>> skipped;  // car, why: noType, noPlaces, full
};
// A waybill for each of these cars: empty to a place that sends out its type, loaded to one that takes it in.
MadeWaybills makeWaybills(const std::vector<MakeCar>& cars, const std::vector<MakePlace>& places, const QMap<QString, int>& heading, quint32 seed);

}  // namespace bld::track
