#pragma once

// Sharing one of your modules, read-only, with a club you're in
// (docs/shared-modules.md; the web's modules/ModuleClubShares.tsx): the
// clubs it's shared with, each with Stop (what that means is said first),
// a club to add, and where the module is used, as counts and club names
// (never the names of other people's layouts). Sharing with one person is
// done on the website; the dialog says so, with a button that opens it.
//
// A club's module (its Admins and Managers; section 11): Partner clubs get
// it at once; Another club… (a search of listed clubs, or a pasted link)
// gets an offer its Admins or Managers must accept. Offers show waiting
// (Take back) or declined (Offer again a day later, Remove).

#include "../sync/LibraryApi.h"

#include <QDialog>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace bld::ui {

class ServerLibrary;
struct ConfirmOptions;

// One line for an offer of a club's module: "Riverside LUG · waiting for them
// to accept", "… · declined". A club that isn't listed is named by its web
// address (`address`) until it accepts.
QString offerText(const sync::ClubOffer& offer, const QString& address, qint64 now);

// What stopping a share with `who` leaves behind (the web's stopShareText).
ConfirmOptions stopShareOptions(const QString& title, const QString& who, int layouts, int version);

class ModuleShareDialog : public QDialog {
    Q_OBJECT
public:
    ModuleShareDialog(ServerLibrary& library, const sync::ServerModule& module, QWidget* parent = nullptr);

    // Asks the server again.
    void reload();
    // Confirms stopping a share; tests answer instead.
    std::function<bool(const ConfirmOptions&)> confirmStop;

    // For tests.
    QComboBox* clubPick() const { return pick_; }
    QPushButton* shareButton() const { return share_; }
    QLabel* usedIn() const { return usedIn_; }
    QLabel* note() const { return note_; }
    // The Stop button of the club with this id, or null.
    QPushButton* stopButton(const QString& orgId) const;
    const sync::ModuleShares& shares() const { return shares_; }
    bool loaded() const { return loaded_; }
    // A club's module (tests): the partner club pick and its Share; Another
    // club…; a search result's Share or Offer; the pasted link's Offer; an
    // offer's Take back / Remove and Offer again; what the last step said.
    QComboBox* partnerPick() const { return partnerPick_; }
    QPushButton* partnerShareButton() const { return partnerShare_; }
    QLineEdit* otherClub() const { return otherClub_; }
    QPushButton* targetButton(const QString& slug) const;
    QPushButton* offerLinkButton() const { return offerLink_; }
    QPushButton* withdrawButton(const QString& offerId) const;
    QPushButton* offerAgainButton(const QString& offerId) const;
    QLabel* said() const { return said_; }
    // Searches now (tests; people wait for the pause after typing).
    void searchNow();

private:
    void show(const sync::ModuleShares& shares);
    void showClubToClub(const sync::ModuleShares& shares);
    void showTargets(const QList<sync::ClubTarget>& targets);
    void offerTo(const QString& slugOrLink);
    void withdraw(const sync::ClubOffer& offer);
    void shareWith(const QString& slug);
    void stop(const sync::ClubRef& club);
    void fail(const sync::ServerRefusal& r);

    ServerLibrary& library_;
    QString id_;
    QString title_;
    sync::ModuleShares shares_;
    bool loaded_ = false;
    QLabel* note_ = nullptr;
    QWidget* addRow_ = nullptr;
    QComboBox* pick_ = nullptr;
    QPushButton* share_ = nullptr;
    QLabel* error_ = nullptr;
    QWidget* clubsHost_ = nullptr;
    QVBoxLayout* clubs_ = nullptr;
    QLabel* usedIn_ = nullptr;
    // A club's module.
    QWidget* otherHost_ = nullptr;
    QLabel* partnersNote_ = nullptr;
    QComboBox* partnerPick_ = nullptr;
    QPushButton* partnerShare_ = nullptr;
    QLineEdit* otherClub_ = nullptr;
    QTimer* search_ = nullptr;
    QWidget* targetsHost_ = nullptr;
    QVBoxLayout* targets_ = nullptr;
    QPushButton* offerLink_ = nullptr;
    QLabel* said_ = nullptr;
    QWidget* offersHost_ = nullptr;
    QVBoxLayout* offers_ = nullptr;
};

}  // namespace bld::ui
