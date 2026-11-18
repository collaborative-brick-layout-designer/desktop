#pragma once

// A shared (read-only) placed module, as the layout's sidecar keeps it
// (docs/shared-modules.md; the web's packages/modules shared.ts and
// placement.ts, apps/web editor/sharedLock.ts). The module's value has
// `shared` next to its link (libraryModuleId, libraryVersion):
//
//   shared: {
//     by:        the owner's public name when it was placed (for files with no server)
//     server:    where the module lives (the desktop can have several servers)
//     placement: how the version's own coordinates map onto the layout
//     review:    set by an update: which version it came from, so the apps
//                can say what changed and offer "Go back to version N".
//                Cleared when someone presses OK.
//   }
//
// Its parts are its owner's: it can be moved, turned, pinned, recolored
// (outline and name), put on another sheet, duplicated and removed, always
// as a whole; nothing changes its parts, which parts it holds, or its
// name. BlueBrick never sees any of it (the sidecar only).

#include "Module.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace bld::core {

// How a version sits in a layout (`shared.placement`): its own coordinates
// turned by `degrees` about its origin, then moved to (x, y).
struct SharedPlacement {
    double degrees = 0.0;
    double x = 0.0;
    double y = 0.0;
    // The version's sheets (by name) to the layout's sheets (by id).
    std::optional<QMap<QString, QString>> sheets;
    bool operator==(const SharedPlacement&) const = default;
};

// Reads `shared.placement`; nullopt when it isn't one.
std::optional<SharedPlacement> readPlacement(const QJsonValue& v);
QJsonObject placementJson(const SharedPlacement& p);

double norm360(double degrees);
// Where the placement puts a point of the version.
QPointF placePoint(const SharedPlacement& p, QPointF at);
// The placement after the placed module is moved by `delta`.
SharedPlacement movedPlacement(SharedPlacement p, QPointF delta);
// The placement after the placed module is turned by `degrees` about `about`.
SharedPlacement turnedPlacement(SharedPlacement p, double degrees, QPointF about);
// A connection snap's turn of a dropped module: by `degrees` about `pivot`, onto `to`.
struct DropTurn {
    double degrees = 0.0;
    QPointF pivot;
    QPointF to;
};
// The placement of a module dropped onto the map: moved by `delta`, then
// (when a connection snap turned it) turned.
SharedPlacement droppedPlacement(QPointF delta, const std::optional<DropTurn>& turn = std::nullopt);

// What an update left for the apps to show.
struct SharedReview {
    int from = 0;  // the version it had before; 0 when that wasn't known
    int to = 0;    // the version it has now
    qint64 at = 0; // when, in ms
};

// A shared placed module's link, read from its sidecar value.
struct SharedPlaced {
    QString placedId;  // the module's id in the layout
    QString name;
    QString moduleId;  // the library module it follows
    int version = 0;   // the version it has; 0 when not known
    QString by;        // empty when not known
    QString server;
    std::optional<SharedPlacement> placement;
    std::optional<SharedReview> review;
    QSet<QString> members;
    // The ids of parts its updates took out (`shared.retired`, the newest
    // kRetiredMax): an undo that brings one back takes it away again
    // (edit/SharedSettle.h settleRetired).
    QStringList retired;
};

constexpr int kRetiredMax = 1000;

// `retired` with `ids` added (each once), the newest kRetiredMax kept (the web's moreRetired).
QStringList moreRetired(QStringList retired, const QStringList& ids);

// Whether the module's value marks it shared (the doc is then shape 4):
// `shared` is there, and it follows a library module.
bool hasSharedMark(const Module& m);
// The module's shared link, or nullopt when it isn't a shared (read-only)
// placed module.
std::optional<SharedPlaced> sharedPlaced(const Module& m);
inline bool isSharedModule(const Module& m) { return sharedPlaced(m).has_value(); }
std::vector<SharedPlaced> sharedModulesIn(const std::vector<Module>& modules);
bool holdsSharedMark(const std::vector<Module>& modules);

// The module with its placement, or its review, changed (the rest of `shared` kept).
Module withPlacement(Module m, const SharedPlacement& p);
Module withReview(Module m, const std::optional<SharedReview>& review);
// The module as an ordinary module of the layout ("Make it mine"): unlocked, and no longer linked.
Module detached(Module m);
// A shared module's copy (Duplicate, paste): another shared placement, under
// the owner's name, with the same link and lock, moved along by `moved`.
Module sharedCopyOf(const Module& m, QSet<QString> members, QString newId, QPointF moved);
// copyOfModule, or sharedCopyOf for a shared module.
Module copyOfPlaced(const Module& m, QSet<QString> members, QString newId, QPointF moved);

// What was asked of a shared module (for the reason given).
enum class LockedAction { Parts, Edit, Rename, Members, Dissolve, Bend };

// Why it can't be done, in plain words (the web's lockMessage).
QString lockMessage(const QString& name, const QString& by, LockedAction what);
inline QString lockMessage(const SharedPlaced& s, LockedAction what) { return lockMessage(s.name, s.by, what); }

// The shared module that a change to the parts `ids` would break: one with
// some of its parts among them. With `whole`, a module whose every part is
// among them is left out (moving it to another sheet, removing it).
std::optional<SharedPlaced> sharedLockOn(const std::vector<Module>& modules, const QSet<QString>& ids,
                                         bool whole = false);

// Whether changing a shared module's value from `was` to `next` keeps the
// lock: the same parts, still shared, following the same module, under the
// same name. nullopt: it does (or `was` isn't shared).
std::optional<LockedAction> keepsLock(const Module& was, const Module& next);

}  // namespace bld::core
