#pragma once

// "Recover unsaved work": the layouts a copy of the app was changing when
// it closed before saving (Autosave.h), by file name and time, newest
// first. Recover opens the picked one with its changes still to be saved;
// Discard deletes it for good (after asking); Not now leaves them all for
// File › Recover Unsaved Work…. Shown at start when there are any.

#include "Autosave.h"

#include <QDialog>
#include <QList>

#include <functional>
#include <optional>

class QLabel;
class QListWidget;
class QPushButton;

namespace bld::ui {

class RecoverDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecoverDialog(QList<AutosaveLeftover> leftovers, QWidget* parent = nullptr);

    // After Recover: the one picked.
    std::optional<AutosaveLeftover> chosen() const { return chosen_; }
    // The ones still listed (not recovered, not discarded).
    const QList<AutosaveLeftover>& remaining() const { return leftovers_; }

    // How Discard asks first (tests answer instead).
    static std::function<bool(QWidget*, const QString& name)> confirmDiscard;

    // "Today 14:05", "Yesterday 09:12", or the date.
    static QString whenText(const QDateTime& at);

private:
    void fill();
    void updateButtons();
    int current() const;

    QList<AutosaveLeftover> leftovers_;
    std::optional<AutosaveLeftover> chosen_;
    QLabel* intro_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* recover_ = nullptr;
    QPushButton* discard_ = nullptr;
    QPushButton* later_ = nullptr;
};

} // namespace bld::ui
