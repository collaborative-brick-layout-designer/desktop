#pragma once

// Shared (read-only) modules in words (docs/shared-modules.md; the web's
// editor/sharedModules.ts, with the same wording): the state of a placed
// copy, which copies have a newer version waiting, the notices, why a
// shared module can't go into a layout, and what the server meant when it
// refused a step.

#include "../core/SharedModule.h"
#include "../edit/SharedMarks.h"
#include "../sync/LibraryApi.h"

#include <QList>
#include <QString>

namespace bld::ui::sharedwords {

// The newer version waiting for a placed copy; 0 for none. `row`: what the
// server says about it (null: not asked yet, or not known).
int newerVersion(const core::SharedPlaced& placed, const sync::LayoutSharedModule* row);

// What else is known about a placed copy, past what the server says.
struct Extra {
    bool changedOutside = false;  // its parts aren't its version's (a file changed in another app)
    bool cantCheck = false;       // the module's server can't be reached, or you aren't signed in to it
    edit::SharedMarks marks;      // after an update: what it now overlaps
};

// The line the Modules panel shows under a shared module: "Shared by Alice
// · v7", "… · v8 is ready", "No longer shared · v7: no more updates".
QString sharedNote(const core::SharedPlaced& placed, const sync::LayoutSharedModule* row, const Extra& extra = {});

// The badge over a shared module on the map: "🔗 Alice", and "● v8 is
// ready" when a version waits.
QString badge(const core::SharedPlaced& placed, int newer);

// A placed copy with a newer version waiting.
struct Update {
    core::SharedPlaced placed;
    sync::LayoutSharedModule row;
    int latest = 0;
};
QList<Update> findUpdates(const std::vector<core::SharedPlaced>& placed, const QList<sync::LayoutSharedModule>& rows);
// What tells one set of waiting versions from another ("Not now" is for these).
QString updatesKey(const QList<Update>& updates);
// The notice for new versions: one module by name, with its note; several by count.
QString updatesText(const QList<Update>& updates);
// "“Harbor crane” v8, “Tunnel” v3": the modules behind "3 shared modules have new versions."
QString updatesList(const QList<Update>& updates);
// The notice after an update: what it is now, and what it now overlaps.
QString reviewText(const core::SharedPlaced& placed, const edit::SharedMarks& marks);
// "It overlaps 2 parts. 1 track end no longer meets."; empty when nothing does.
QString marksText(const edit::SharedMarks& marks);

// "Updated while you were away" (the web's awayText): one module went
// from version `from` to `to` (`from` 0: not known), its owner `by`
// (empty: "its owner"); or `count` modules (more than one).
QString awayText(const QString& name, const QString& by, int from, int to);
QString awayManyText(int count);
// "“Harbor crane” v7 → v9", for the list under several.
QString awayLine(const QString& name, int from, int to);

// Why a shared module can't go into this layout, and what to do about it.
struct Refusal {
    QString title;
    QString body;
    QString ask;  // a short message to send its owner
};
Refusal refusalText(const sync::SharedPlaceCheck& check);

// What the server meant when it refused Update, Go back, Put back or Make
// it mine, in plain words (the web's api.ts ERROR_MESSAGES); `fallback`
// for anything else.
QString stepRefusal(const sync::ServerRefusal& r, const QString& fallback);

}  // namespace bld::ui::sharedwords
