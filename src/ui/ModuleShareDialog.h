#pragma once

// Sharing one of your modules, read-only, with a club you're in
// (docs/shared-modules.md; the web's modules/ModuleClubShares.tsx): the
// clubs it's shared with, each with Stop (what that means is said first),
// a club to add, and where the module is used, as counts and club names
// (never the names of other people's layouts). Sharing with one person is
// done on the website; the dialog says so, with a button that opens it.

#include "../sync/LibraryApi.h"

#include <QDialog>

#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace bld::ui {

class ServerLibrary;
struct ConfirmOptions;

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

private:
    void show(const sync::ModuleShares& shares);
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
};

}  // namespace bld::ui
