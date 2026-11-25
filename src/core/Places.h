#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

// Places (Live Ops design 2.1): named spots on the track where cars go: an
// industry, a yard, staging or an interchange. A place has spots, each a
// piece of track with room for some cars.
//
//   { id, name, kind: "industry" | "yard" | "staging" | "interchange",
//     spots: [{ id, name, track?: { brick, from, to }, capacity?, role? }],
//     accepts: [car types], ships: [car types], tags: [], notes: "",
//     source? }
//
// Kept as JSON, as the sidecar has them, so fields a newer build added stay.
// A module's places are in its sidecar value (core::Module::places) and go
// wherever it goes; places on loose track are the layout's own
// (Sidecar::layoutPlaces; live, the web's meta.places). A Module library
// module keeps its places in its own contents, as that layout's own.
// `source` is a placed module's place's id in the Module library. Never in a
// .bbm. The web's packages/bbm/src/places.ts is the twin; both tests use
// fixtures/contracts/places.json.
namespace bld::core::places {

// What the apps and the server hold a place to (lengths in UTF-16 units).
inline constexpr int kMaxPlaces = 200;
inline constexpr int kMaxSpots = 50;
inline constexpr int kMaxId = 64;
inline constexpr int kMaxBrick = 128;
inline constexpr int kMaxConn = 255;
inline constexpr int kMaxName = 80;
inline constexpr int kMaxKind = 40;
inline constexpr int kMaxWord = 40;
inline constexpr int kMaxWords = 20;
inline constexpr int kMaxNotes = 2000;
inline constexpr int kMaxCapacity = 999;
inline constexpr int kMaxSize = 65536;
// The car length capacities are worked out with when a spot doesn't say, studs.
inline constexpr double kTypicalCar = 32.0;

QStringList kinds();     // industry, yard, staging, interchange
QStringList yardRoles(); // arrival, departure, class, rip, engine
QStringList carTypes();  // offered as you type

// Gives a part's new id; nullopt: it isn't there (its spot stays, without its track).
using Rename = std::function<std::optional<QString>(const QString&)>;
using NewId = std::function<QString()>;

// How big a value is: the same count in both apps, whatever order its keys are in.
int placeSize(const QJsonValue& v);
// Whether `v` is a place (or a module's list of them) as the apps write one, within the limits.
bool validPlace(const QJsonValue& v);
bool validPlaces(const QJsonValue& v);
// A place as stored, put within the limits; nullopt when it isn't one (no id).
std::optional<QJsonObject> readPlace(const QJsonValue& v);
// The readable ones, each id once, within the limits.
QJsonArray readPlaces(const QJsonValue& v);

QJsonArray renamed(const QJsonArray& places, const Rename& rename);
// For another placed copy of a module: new ids, remembering which place each is.
QJsonArray copied(const QJsonArray& places, const Rename& rename, const NewId& newId);
// A library version's places in place of a placed module's: each keeps the id it has here.
QJsonArray updated(const QJsonArray& current, const QJsonArray& next, const NewId& newId);
// As the Module library keeps them: under its ids, spots only on the module's own parts.
QJsonArray forLibrary(const QJsonArray& places, const QSet<QString>& members);
// Making a module: the layout's places that are all on its parts become its own.
struct Adopted {
    QJsonArray module, layout;
};
Adopted adopted(const QJsonArray& layoutPlaces, const QSet<QString>& members);
// A module taken apart (its parts stay): its places become the layout's.
QJsonArray released(const QJsonArray& layoutPlaces, const QJsonArray& places, const NewId& newId);

// The parts a place's spots are on.
QStringList bricksOf(const QJsonObject& place);
// How many of each kind, known kinds first.
QList<std::pair<QString, int>> counts(const QStringList& kinds);
QList<std::pair<QString, int>> countsOf(const QJsonArray& places);
// Ignoring case and spaces at either end.
bool sameCarType(const QString& a, const QString& b);
// What the spot says, else what fits in `usable` studs of track; nullopt when that isn't known.
std::optional<int> spotCapacity(const QJsonObject& spot, std::optional<double> usable);

// The place with this id: its index in the list (-1: none).
int indexOf(const QJsonArray& places, const QString& id);
// The list with `place` in it (replacing the one with its id, or at the end), or without one.
QJsonArray withPlace(QJsonArray places, const QJsonObject& place);
QJsonArray without(QJsonArray places, const QString& id);

}  // namespace bld::core::places
