#pragma once

// Whether this desktop and a collaborative server work together, from the
// server's /api/version: the oldest desktop it accepts and the one it
// recommends, the shared-document versions it reads and writes, and the
// features it has. The values and the answers follow compat/compat.json,
// which the server's tests read too (CompatTest checks them).

#include <QString>
#include <QStringList>

#include <optional>

namespace bld::sync {

// The shared-document schema this build writes, and the oldest it reads
// (the web's DOC_SCHEMA_VERSION / DOC_MIN_READABLE).
// 2: doc shape 2 (DocShape.h). 3: a module's part changes are keys of
// their own (ModuleMembers.h). 4: a module may be shared, read-only
// (core/SharedModule.h). 5: a shared module's update keeps its unchanged
// parts, and undo is finished for the whole module (edit/SharedSettle.h).
// Docs are still made in kDocBaseSchema.
constexpr int kDocSchemaVersion = 5;
// The shape that has shared (read-only) modules: a doc moves to it with
// its first one.
constexpr int kDocShapeShared = 4;
// The feature a server lists when its web app knows a shared module is
// locked and the server applies its updates (compat/compat.json): only
// there does the desktop offer "Shared with me" and make a shape 4 layout.
constexpr char kSharedModulesFeature[] = "sharedModules";
// The shape whose shared-module updates keep the unchanged parts: a doc
// moves to it with its first such update, marked in meta (kKeptIdsMark).
constexpr int kDocShapeKeepIds = 5;
constexpr char kKeptIdsMark[] = "keptIds";
// The feature a server lists when its updates keep unchanged parts (and its
// web app finishes an undo for the whole module): only there does the
// desktop keep them in its own update of a live layout made offline.
constexpr char kSharedKeepIdsFeature[] = "sharedKeepIds";
constexpr int kDocMinReadable = 1;

// SyncSession::ended's code when the shared layout is in a form this
// version can't read (not a WebSocket close code from the server).
constexpr int kUnreadableDocCode = 4415;
// ...and when an update to it can't be read, even after a full resync.
constexpr int kUnreadableUpdateCode = 4416;

enum class Standing { Ok, UpdateSuggested, UpdateRequired };

// Where `app` stands against a server's minimum and recommended desktop
// versions. A version that can't be read (a developer's build) is never
// refused; an empty minimum or recommendation asks nothing.
Standing desktopStanding(const QString& app, const QString& minimum, const QString& recommended);

// Whether a reader writing `schemaVersion` and reading back to
// `minReadable` can sync a document stamped `docSchema` (unstamped: yes).
bool canReadDoc(std::optional<int> docSchema, int schemaVersion = kDocSchemaVersion,
                int minReadable = kDocMinReadable);

// The server features this desktop uses, in the order it names them.
QStringList desktopFeatures();
// What a server that lists no features (from before the list) is taken to have.
QStringList assumedWhenUnlisted();
// The features this desktop uses that a server lacks; nullopt = it lists none.
QStringList missingFeatures(const std::optional<QStringList>& serverFeatures);
// "The venue library": a feature in words for a person.
QString featureLabel(const QString& id);

}  // namespace bld::sync
