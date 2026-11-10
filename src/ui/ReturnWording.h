#pragma once

// "Take back to mine" / "Give back to ‹author›" and saving into a club:
// the questions asked first, in the website's words (owners/OwnerControls.tsx).

#include "../sync/Credit.h"
#include "ConfirmDialog.h"

#include <QCoreApplication>

namespace bld::ui {

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call ReturnWordingText::tr().
struct ReturnWordingText { Q_DECLARE_TR_FUNCTIONS(bld::ui::ReturnWording) };


// `word`: layout / module / venue / part. `give`: the club gives it back to its author.
inline ConfirmOptions returnOptions(const QString& word, const QString& title, const sync::Credit& c,
                                    bool give) {
    const QString who = give ? (c.authorName.isEmpty() ? ReturnWordingText::tr("its author") : c.authorName) : ReturnWordingText::tr("you");
    const QString club = c.club.isEmpty() ? ReturnWordingText::tr("The club") : c.club;
    ConfirmOptions o;
    o.title = give ? ReturnWordingText::tr("Give “%1” back to %2?").arg(title, who) : ReturnWordingText::tr("Take “%1” back?").arg(title);
    o.removes = give ? ReturnWordingText::tr("The %1 goes back to %2, who made it.").arg(word, who)
                     : ReturnWordingText::tr("The %1 becomes yours again, and only yours.").arg(word);
    o.keeps = ReturnWordingText::tr("%1 keeps its own copy, credited to %2, so nothing the club built with it changes. "
                 "Members with the Delete club items permission are told.")
                  .arg(club, who);
    o.undo = give ? ReturnWordingText::tr("%1 can move it back to the club.").arg(who)
                  : ReturnWordingText::tr("To share it again, move it back to the club.");
    o.confirmLabel = give ? ReturnWordingText::tr("Give back") : ReturnWordingText::tr("Take back");
    o.danger = false;
    return o;
}

// Before a new thing is saved straight into a club (the Save to choosers).
inline ConfirmOptions saveToClubOptions(const QString& club) {
    const auto w = sync::moveToClubWording(club, true);
    ConfirmOptions o;
    o.title = w.title;
    o.removes = w.removes;
    o.keeps = w.keeps;
    o.confirmLabel = QCoreApplication::translate("bld::ui::ReturnWording", "Save");
    o.danger = false;
    return o;
}

} // namespace bld::ui
