#pragma once

// One confirmation dialog for every deletion (and the other "are you
// sure?" questions), worded like the website's (apps/web/src/ui/
// ConfirmDialog.tsx): "Delete ‹name›?", what goes, what stays, whether it
// can be undone, a red Delete and a Cancel that starts with the focus.
// Esc cancels. For big or permanent deletions the person types the name
// before Delete turns on.

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace bld::ui {

struct ConfirmOptions {
    QString title;         // "Delete “Main yard”?"
    QString removes;       // what will be removed
    QString keeps;         // what stays
    QString undo;          // whether it can be undone
    QString confirmLabel;  // the red button; empty = "Delete"
    QString typeName;      // when set, typed before the button turns on
    bool danger = true;    // red; false = the accent color (not a removal)
};

struct DeleteWording {
    QString removes;       // empty = "“‹name›” is deleted."
    QString keeps;
    QString undo;          // empty = "This can’t be undone."
    bool typeName = false; // type the name first
    QString verb;          // empty = "Delete" ("Remove", "Revoke"…)
    QString title;         // a whole title instead of "‹verb› “‹name›”?"
};

class ConfirmDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfirmDialog(const ConfirmOptions& opts, QWidget* parent = nullptr);

    /** Ask and wait: true only for the confirm button. */
    static bool ask(QWidget* parent, const ConfirmOptions& opts);
    /** The standard deletion question. */
    static bool confirmDelete(QWidget* parent, const QString& name, const DeleteWording& w = {});
    static ConfirmOptions deleteOptions(const QString& name, const DeleteWording& w = {});
    /** Same name, ignoring case and the spaces around it. */
    static bool typedMatches(const QString& typed, const QString& name);

    // The same words as the website's ui/deleteWording.ts. `recentlyDeleted`:
    // deleted from a server that keeps it 30 days in Recently deleted, so the
    // box says where it waits instead of "This can’t be undone." (a file on
    // this computer, or an older server, is deleted for good).
    static DeleteWording layoutWording(bool recentlyDeleted = false);
    static DeleteWording moduleWording(bool recentlyDeleted = false);
    static DeleteWording venueWording(bool recentlyDeleted = false);
    // "It waits 30 days in Recently deleted on the website’s Home, where you can restore it."
    static QString waitsInRecentlyDeleted();
    static DeleteWording customPartWording();
    // A set of parts (Save selection as set): its placed parts stay.
    static DeleteWording setWording();

    static QString undoWithCtrlZ();

    // An "Also …" checkbox under the text ("Also delete it from Club server"),
    // hidden until this shows it, which may be while the dialog is open.
    void showOption(const QString& text, bool checked = true);
    QCheckBox* optionBox() const { return option_; }
    // Shown and ticked.
    bool optionChosen() const;
    // A warning under the checkbox, for what ticking it loses ("2 changes you
    // made offline never reached the server…"). Hidden until this shows it.
    void showWarning(const QString& text);
    QString warningText() const;

    QPushButton* confirmButton() const { return confirm_; }
    QPushButton* cancelButton() const { return cancel_; }
    QLineEdit* nameEdit() const { return typed_; }
    QString bodyText() const;

private:
    void update();

    ConfirmOptions opts_;
    QLabel* body_ = nullptr;
    QLineEdit* typed_ = nullptr;
    QCheckBox* option_ = nullptr;
    QLabel* warning_ = nullptr;
    QPushButton* confirm_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

} // namespace bld::ui
