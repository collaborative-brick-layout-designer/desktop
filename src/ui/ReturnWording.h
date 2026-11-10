#pragma once

// "Take back to mine" / "Give back to ‹author›" and saving into a club:
// the questions asked first, in the website's words (owners/OwnerControls.tsx).

#include "../sync/Credit.h"
#include "ConfirmDialog.h"

#include <QCoreApplication>

namespace bld::ui {

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call ReturnWordingText::tr().
struct ReturnWordingText { Q_DECLARE_TR_FUNCTIONS(bld::ui::ReturnWording) };


// What goes back: each kind has its own sentences, so no language has to fit a
// translated noun into a sentence written around a blank.
enum class ReturnKind { Layout, Module, Venue, Part };

// `give`: the club gives it back to its author; otherwise you take it back.
inline ConfirmOptions returnOptions(ReturnKind kind, const QString& title, const sync::Credit& c, bool give) {
    const bool named = !c.authorName.isEmpty();
    const QString who = c.authorName;
    const QString club = c.club;
    ConfirmOptions o;
    if (give) {
        o.title = named ? ReturnWordingText::tr("Give “%1” back to %2?").arg(title, who) : ReturnWordingText::tr("Give “%1” back to its author?").arg(title);
        switch (kind) {
        case ReturnKind::Layout:
            o.removes = named ? ReturnWordingText::tr("The layout goes back to %1, who made it.").arg(who)
                              : ReturnWordingText::tr("The layout goes back to the person who made it.");
            break;
        case ReturnKind::Module:
            o.removes = named ? ReturnWordingText::tr("The module goes back to %1, who made it.").arg(who)
                              : ReturnWordingText::tr("The module goes back to the person who made it.");
            break;
        case ReturnKind::Venue:
            o.removes = named ? ReturnWordingText::tr("The venue goes back to %1, who made it.").arg(who)
                              : ReturnWordingText::tr("The venue goes back to the person who made it.");
            break;
        case ReturnKind::Part:
            o.removes = named ? ReturnWordingText::tr("The part goes back to %1, who made it.").arg(who)
                              : ReturnWordingText::tr("The part goes back to the person who made it.");
            break;
        }
        if (!club.isEmpty())
            o.keeps = named ? ReturnWordingText::tr("%1 keeps its own copy, credited to %2, so nothing the club built with it changes. "
                                    "Members with the Delete club items permission are told.").arg(club, who)
                            : ReturnWordingText::tr("%1 keeps its own copy, credited to its author, so nothing the club built with it changes. "
                                    "Members with the Delete club items permission are told.").arg(club);
        else
            o.keeps = named ? ReturnWordingText::tr("The club keeps its own copy, credited to %1, so nothing the club built with it changes. "
                                    "Members with the Delete club items permission are told.").arg(who)
                            : ReturnWordingText::tr("The club keeps its own copy, credited to its author, so nothing the club built with it "
                                    "changes. Members with the Delete club items permission are told.");
        o.undo = named ? ReturnWordingText::tr("%1 can move it back to the club.").arg(who) : ReturnWordingText::tr("Its author can move it back to the club.");
        o.confirmLabel = ReturnWordingText::tr("Give back");
    } else {
        o.title = ReturnWordingText::tr("Take “%1” back?").arg(title);
        switch (kind) {
        case ReturnKind::Layout: o.removes = ReturnWordingText::tr("The layout becomes yours again, and only yours."); break;
        case ReturnKind::Module: o.removes = ReturnWordingText::tr("The module becomes yours again, and only yours."); break;
        case ReturnKind::Venue: o.removes = ReturnWordingText::tr("The venue becomes yours again, and only yours."); break;
        case ReturnKind::Part: o.removes = ReturnWordingText::tr("The part becomes yours again, and only yours."); break;
        }
        o.keeps = !club.isEmpty() ? ReturnWordingText::tr("%1 keeps its own copy, credited to you, so nothing the club built with it changes. "
                                          "Members with the Delete club items permission are told.").arg(club)
                                  : ReturnWordingText::tr("The club keeps its own copy, credited to you, so nothing the club built with it "
                                          "changes. Members with the Delete club items permission are told.");
        o.undo = ReturnWordingText::tr("To share it again, move it back to the club.");
        o.confirmLabel = ReturnWordingText::tr("Take back");
    }
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
