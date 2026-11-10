#pragma once

// Edit > Settings...: every setting in one window, in sections down the
// side. Light or dark, the color, bigger text, snapping and the help
// buttons as the web's Settings page has them (synced through the
// PrefsStore: the theme follows, PrefsSync pushes it), and this computer's
// own: editing defaults, the map's labels, the language, starting up,
// parts and folders, and bringing in models (SettingsSections.h). (The
// synced settings also carry expertMode, kept so it round-trips with the
// server; the desktop shows every feature, so it has no switch for it.)
// Every change is saved at once; there is no OK.

#include "SettingsSections.h"

#include <QDialog>

#include <functional>

class QLabel;

namespace bld::ui {

namespace theme { class PrefsStore; }

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // `syncedHost`: the host of the server these settings sync with right
    // now, or empty when not connected. `changed` hears what a change of
    // this computer's settings was for, so the window follows at once.
    // `startTour` runs a tour (by id) once this dialog has closed; without
    // it there are no tour buttons. `openPartsFolders` opens Parts
    // Folders… (no button without it).
    SettingsDialog(theme::PrefsStore& store, const QString& syncedHost,
                   const settings::Changed& changed = {}, QWidget* parent = nullptr,
                   const std::function<void(const QString&)>& startTour = {},
                   const std::function<void()>& openPartsFolders = {});

    // The note under the options: synced with the host, or kept here.
    QLabel* syncNote() const { return syncNote_; }

    // The Servers section's "Manage servers…" opens this (MainWindow's
    // Servers dialog); the section then says what changed. Without it the
    // section only lists them.
    void setManageServers(const std::function<void()>& manage);

private:
    void load();
    void loadServers();
    theme::PrefsStore& store_;
    QLabel* syncNote_ = nullptr;
    QLabel* toursNote_ = nullptr;
    QLabel* serversNote_ = nullptr;
    class QPushButton* manageServers_ = nullptr;
    std::function<void()> manage_;
    class QPushButton* showToursAgain_ = nullptr;
    bool loading_ = false;
};

}  // namespace bld::ui
