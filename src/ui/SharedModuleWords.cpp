#include "SharedModuleWords.h"

#include <QCoreApplication>

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call W::tr().
namespace {
struct W { Q_DECLARE_TR_FUNCTIONS(SharedModules) };
}  // namespace

namespace bld::ui::sharedwords {

int newerVersion(const core::SharedPlaced& placed, const sync::LayoutSharedModule* row) {
    if (!row || !row->following() || row->latest <= 0) return 0;
    return placed.version <= 0 || row->latest > placed.version ? row->latest : 0;
}

QString sharedNote(const core::SharedPlaced& placed, const sync::LayoutSharedModule* row, const Extra& extra) {
    const QString by = row && !row->ownerName.isEmpty() ? row->ownerName : placed.by;
    const QString v = placed.version > 0 ? W::tr(" · v%1").arg(placed.version) : QString();
    if (row && row->state == QLatin1String("unshared")) return W::tr("No longer shared%1: no more updates").arg(v);
    if (row && row->state == QLatin1String("gone")) return W::tr("Deleted by its owner%1: no more updates").arg(v);
    QString note = by.isEmpty() ? W::tr("Shared%1").arg(v) : W::tr("Shared by %1%2").arg(by, v);
    if (const int newer = newerVersion(placed, row)) note = W::tr("%1 · v%2 is ready").arg(note).arg(newer);
    if (extra.changedOutside) note += QStringLiteral(" · ") + W::tr("Changed outside the app");
    if (extra.marks.overlaps > 0) note += QStringLiteral(" · ") + W::tr("Overlaps %n part(s)", nullptr, extra.marks.overlaps);
    if (extra.cantCheck) note += QStringLiteral(" · ") + W::tr("Can't check for updates (offline)");
    return note;
}

QString badge(const core::SharedPlaced& placed, int newer) {
    const QString who = placed.by.isEmpty() ? W::tr("Shared") : placed.by;
    return newer > 0 ? W::tr("🔗 %1 ● v%2 is ready").arg(who).arg(newer) : QStringLiteral("🔗 %1").arg(who);
}

QList<Update> findUpdates(const std::vector<core::SharedPlaced>& placed, const QList<sync::LayoutSharedModule>& rows) {
    QList<Update> out;
    for (const auto& p : placed)
        for (const auto& row : rows) {
            if (row.placedId != p.placedId) continue;
            if (const int latest = newerVersion(p, &row)) out.append({ p, row, latest });
            break;
        }
    return out;
}

QString updatesKey(const QList<Update>& updates) {
    QStringList keys;
    for (const auto& u : updates) keys << QStringLiteral("%1:%2").arg(u.placed.placedId).arg(u.latest);
    return keys.join(QLatin1Char(','));
}

QString updatesText(const QList<Update>& updates) {
    if (updates.isEmpty()) return {};
    if (updates.size() > 1) return W::tr("%n shared module(s) have new versions.", nullptr, static_cast<int>(updates.size()));
    const Update& u = updates.first();
    const QString name = !u.placed.name.isEmpty() ? u.placed.name : !u.row.title.isEmpty() ? u.row.title : W::tr("A shared module");
    const QString what = u.row.ownerName.isEmpty()
                             ? W::tr("“%1” has a new version (v%2)").arg(name).arg(u.latest)
                             : W::tr("“%1” has a new version from %2 (v%3)").arg(name, u.row.ownerName).arg(u.latest);
    return u.row.latestNote.isEmpty() ? what + QLatin1Char('.') : W::tr("%1: “%2”.").arg(what, u.row.latestNote);
}

QString updatesList(const QList<Update>& updates) {
    QStringList out;
    for (const auto& u : updates)
        out << W::tr("“%1” v%2")
                   .arg(!u.placed.name.isEmpty() ? u.placed.name : !u.row.title.isEmpty() ? u.row.title : W::tr("Module"))
                   .arg(u.latest);
    return out.join(QStringLiteral(", "));
}

QString marksText(const edit::SharedMarks& marks) {
    QStringList out;
    if (marks.overlaps > 0) out << W::tr("It overlaps %n part(s).", nullptr, marks.overlaps);
    if (marks.looseEnds > 0) out << W::tr("%n track end(s) no longer meet.", nullptr, marks.looseEnds);
    return out.join(QLatin1Char(' '));
}

QString reviewText(const core::SharedPlaced& placed, const edit::SharedMarks& marks) {
    const int to = placed.review ? placed.review->to : placed.version;
    const QString name = placed.name.isEmpty() ? W::tr("The shared module") : W::tr("“%1”").arg(placed.name);
    const QString now = to > 0 ? W::tr("%1 is now version %2.").arg(name).arg(to) : W::tr("%1 was updated.").arg(name);
    const QString more = marksText(marks);
    return more.isEmpty() ? now : now + QLatin1Char(' ') + more;
}

QString awayText(const QString& name, const QString& by, int from, int to) {
    const QString n = name.isEmpty() ? W::tr("A shared module") : W::tr("“%1”").arg(name);
    if (from > 0 && from != to)
        return by.isEmpty() ? W::tr("%1 went from version %2 to %3: this layout takes its owner's updates automatically.").arg(n).arg(from).arg(to)
                            : W::tr("%1 went from version %2 to %3: this layout takes %4's updates automatically.").arg(n).arg(from).arg(to).arg(by);
    return by.isEmpty() ? W::tr("%1 is now version %2: this layout takes its owner's updates automatically.").arg(n).arg(to)
                        : W::tr("%1 is now version %2: this layout takes %3's updates automatically.").arg(n).arg(to).arg(by);
}

QString awayManyText(int count) {
    return W::tr("%1 shared modules were updated: this layout takes their owners' updates automatically.").arg(count);
}

QString awayLine(const QString& name, int from, int to) {
    const QString n = W::tr("“%1”").arg(name.isEmpty() ? W::tr("Module") : name);
    return from > 0 && from != to ? QStringLiteral("%1 v%2 → v%3").arg(n).arg(from).arg(to) : QStringLiteral("%1 v%2").arg(n).arg(to);
}

Refusal refusalText(const sync::SharedPlaceCheck& c) {
    const QString owner = c.moduleOwner.name.isEmpty() ? W::tr("its owner") : c.moduleOwner.name;
    const bool club = c.layoutOwner.kind == QLatin1String("org");
    Refusal r;
    r.title = W::tr("“%1” can't go in this layout yet").arg(c.title);
    if (club) {
        const QString where = c.layoutOwner.name.isEmpty() ? W::tr("this club") : c.layoutOwner.name;
        r.body = W::tr("%1 shared “%2” with you, not with %3. Ask %1 to share it with the club.").arg(owner, c.title, where);
        r.ask = W::tr("Hi %1, could you share your module “%2” with %3? I'd like to place it in one of the club's layouts.")
                    .arg(owner, c.title, where);
    } else if (c.layoutOwner.mine) {
        r.body = W::tr("%1 shared “%2” with a club you're in, for the club's layouts, not with you. Ask %1 to share it "
                       "with you too.")
                     .arg(owner, c.title);
        r.ask = W::tr("Hi %1, could you share your module “%2” with me as well? I'd like to place it in one of my own "
                      "layouts.")
                    .arg(owner, c.title);
    } else {
        const QString where = c.layoutOwner.name.isEmpty() ? W::tr("this layout's owner") : c.layoutOwner.name;
        r.body = W::tr("%1 shared “%2” with you, not with %3. Ask %1 to share it with them.").arg(owner, c.title, where);
        r.ask = W::tr("Hi %1, could you share your module “%2” with %3? I'd like to place it in one of their layouts.")
                    .arg(owner, c.title, where);
    }
    return r;
}

QString stepRefusal(const sync::ServerRefusal& r, const QString& fallback) {
    const QString& c = r.code;
    if (c == QLatin1String("no_longer_shared"))
        return W::tr("This module is no longer shared with this layout, so it stays at the version it has. You can "
                     "remove it, or its owner can share it again.");
    if (c == QLatin1String("module_gone")) return W::tr("Its owner deleted this module, so it stays at the version it has.");
    if (c == QLatin1String("module_not_found"))
        return W::tr("You can't see this module in the Module library, so you can't update it. Ask whoever added it.");
    if (c == QLatin1String("not_placed")) return W::tr("That module is no longer in this layout.");
    if (c == QLatin1String("not_linked")) return W::tr("This module doesn't follow a Module library module any more.");
    if (c == QLatin1String("version_not_found")) return W::tr("That version isn't kept any more (the newest 20 are).");
    if (c == QLatin1String("no_sheet"))
        return W::tr("This layout has no parts sheet to put the module on. Add a parts sheet first.");
    if (c == QLatin1String("only_the_layouts_owner"))
        return W::tr("Only the layout's owner can make a shared module theirs. In a club's layout, ask a club admin or "
                     "manager.");
    return fallback;
}

}  // namespace bld::ui::sharedwords
