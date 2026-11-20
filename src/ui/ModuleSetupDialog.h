#pragma once

// A module's table and power (core/ModuleSetup.h): what it stands on and
// how tall, and what it needs from the wall, for the layout's coordinator.
// Store and show only. Each change is applied at once: on a placed module
// (one undo step, and for everyone else on a live layout), or on a Module
// library module on the server. The web's ModuleSetupDialog.tsx is the same.

#include "ModuleSetupText.h"

#include "../core/ModuleSetup.h"

#include <QDialog>

#include <functional>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace bld::ui {

class ModuleSetupDialog : public QDialog {
    Q_OBJECT
public:
    // `current` reads the module's setup as it is now (nullopt once the
    // module is gone); `apply` writes a changed one, with the undo text.
    using Current = std::function<std::optional<core::ModuleSetup>()>;
    using Apply = std::function<void(const core::ModuleSetup& changed, const QString& undoText)>;
    ModuleSetupDialog(const QString& name, ModuleSetupText::Unit unit, Current current, Apply apply,
                      QWidget* parent = nullptr);

    // Reads the module again (after any change); closes when it's gone.
    void refresh();
    // A line under the title (where the change is saved).
    void setNote(const QString& note);
    // A problem saving (empty: none).
    void setError(const QString& error);
    // What a change does (for a dialog made before it knows).
    void setApply(Apply apply) { apply_ = std::move(apply); }

    // For tests: the controls.
    QComboBox* typeBox() const { return type_; }
    QLineEdit* heightEdit() const { return height_; }
    QRadioButton* railButton() const { return toRail_; }
    QComboBox* powerBox() const { return power_; }
    QSpinBox* outletsBox() const { return outlets_; }
    QSpinBox* wattsBox() const { return watts_; }
    QComboBox* sideBox() const { return side_; }
    QPushButton* clearButton() const { return clear_; }
    QLabel* heightProblem() const { return heightProblem_; }
    // Reads the height box (what leaving it or Enter does).
    void commitHeight();

private:
    void write(const core::ModuleSetup& s, const QString& undoText);
    Current current_;
    Apply apply_;
    ModuleSetupText::Unit unit_;
    QLabel* note_ = nullptr;
    QLabel* error_ = nullptr;
    QComboBox* type_ = nullptr;
    QLabel* typeHint_ = nullptr;
    QLineEdit* height_ = nullptr;
    QLabel* heightProblem_ = nullptr;
    QWidget* measured_ = nullptr;
    QRadioButton* toTable_ = nullptr;
    QRadioButton* toRail_ = nullptr;
    QComboBox* power_ = nullptr;
    QWidget* powerDetails_ = nullptr;
    QSpinBox* outlets_ = nullptr;
    QSpinBox* watts_ = nullptr;
    QComboBox* side_ = nullptr;
    QPushButton* clear_ = nullptr;
    bool refreshing_ = false;
};

}  // namespace bld::ui
