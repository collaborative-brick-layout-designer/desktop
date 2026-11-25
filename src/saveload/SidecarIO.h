#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QJsonValue>
#include <QString>

#include "../core/Route.h"
#include "../core/Train.h"

#include <optional>

namespace bld::core { class Map; struct Sidecar; struct SavedView; struct Route; }

namespace bld::saveload {

struct SidecarLoadResult {
    bool ok = false;
    QString error;
    bool hashMismatch = false;   // true if the stored hash differs from the current .bbm
    // Saved by a newer version (schemaVersion above ours): what this build
    // doesn't know is kept (Sidecar::extras) but not shown.
    bool newerVersion = false;
};

// Compute the canonical sidecar path for a .bbm: "foo.bbm" -> "foo.bbm.bld".
QString sidecarPathFor(const QString& bbmPath);

// Read the sidecar at `cldPath` and populate `out`. If `bbmBytes` is non-empty,
// verify that its SHA-256 matches the hash stored in the sidecar; report a
// mismatch in the result without failing the load (caller decides how to react).
SidecarLoadResult readSidecar(const QString& cldPath,
                               const QByteArray& bbmBytes,
                               core::Sidecar& out);

// Write the sidecar JSON. `bbmBytes` is the just-written .bbm content used to
// stamp the hash so we can detect drift on next load.
bool writeSidecar(const QString& cldPath,
                  const QByteArray& bbmBytes,
                  const core::Sidecar& sidecar,
                  QString* error = nullptr);

// The sidecar as JSON and back, without the file or its hash: the same
// shape the web keeps in a live layout's shared document (meta.cache).
QJsonObject sidecarToJson(const core::Sidecar& sidecar);
// The budget as a sidecar's `budgetLimits` (part number -> limit, 0 or
// more), and back; nullopt when the value isn't an object (no budget).
QJsonObject budgetLimitsToJson(const QMap<QString, int>& limits);
std::optional<QMap<QString, int>> budgetLimitsFromJson(const QJsonValue& v);
// switchStates: "<brick>/<heel>" to a connection index (a whole number, 0 or
// more); anything else in the object is left out.
QJsonObject switchStatesToJson(const QMap<QString, int>& states);
QMap<QString, int> switchStatesFromJson(const QJsonValue& v);
// A named route as the sidecar (and the web's meta.routes) keeps it; an
// entry without an id reads as nullopt. Unknown fields are kept.
QJsonObject routeToJson(const core::Route& r);
std::optional<core::Route> routeFromJson(const QJsonValue& v);
// A car or a train as the sidecar (and the web's meta.rollingStock and
// meta.trains) keeps it; one without an id reads as nullopt. A car's missing
// fields take a quick car's defaults. Unknown fields are kept.
QJsonObject rollingStockToJson(const core::RollingStock& r);
std::optional<core::RollingStock> rollingStockFromJson(const QJsonValue& v);
QJsonObject trainToJson(const core::Train& t);
std::optional<core::Train> trainFromJson(const QJsonValue& v);
// A sidecar's trainScale: a whole number from 10 to 220 (1:N); none otherwise.
std::optional<int> trainScaleFromJson(const QJsonValue& v);
void sidecarFromJson(const QJsonObject& root, core::Sidecar& out);

// One saved view as JSON and back (references/LAYOUT-FILE.md "Saved
// views"). Unknown fields ride along in SavedView::extras. A fit view's
// rect is written as null. viewFromJson returns nothing for a value that
// isn't a view at all (no id).
QJsonObject viewToJson(const core::SavedView& view);
std::optional<core::SavedView> viewFromJson(const QJsonValue& value);

// A module kept as a .bbm file (the Module library folder on this computer)
// has its places (core/Places.h) in its .bbm.bld, as the layout's own: read
// them into / write them from the map's sidecar. Nothing else of it is touched.
void readModulePlaces(const QString& bbmPath, core::Map& module);
bool writeModulePlaces(const QString& bbmPath, const core::Map& module, QString* error = nullptr);

// Points the sidecar's references (views' sheets, labels' targets, module
// members) at the ids the map was renumbered to (LoadResult::renamedIds).
void renameSidecarIds(core::Sidecar& sidecar, const QHash<QString, QString>& renamed);

// What the person is told when the labels, modules and venue were saved by
// a newer version (Sidecar::fromNewerVersion).
QString newerSidecarWarning();

// Compute SHA-256 of the bytes (hex-encoded lowercase).
QByteArray sha256Hex(const QByteArray& bytes);

}
