#include "ModuleSetupDialog.h"

#include "help/HelpButton.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace bld::ui {

ModuleSetupDialog::ModuleSetupDialog(const QString& name, ModuleSetupText::Unit unit, Current current, Apply apply,
                                     QWidget* parent)
    : QDialog(parent), current_(std::move(current)), apply_(std::move(apply)), unit_(unit) {
    setWindowTitle(tr("Table and power"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("Table and power · %1").arg(name.isEmpty() ? tr("(module)") : name), this);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    title->setWordWrap(true);
    head->addWidget(title, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("dialog.moduleSetup"), this));
    col->addLayout(head);
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    note_->hide();
    col->addWidget(note_);

    // Table: its type, its height, and what the height is measured to.
    auto* tableBox = new QGroupBox(tr("Table"), this);
    auto* tableForm = new QFormLayout(tableBox);
    type_ = new QComboBox(tableBox);
    type_->addItem(tr("Not set"), QString());
    for (const QString& t : core::tableTypes()) type_->addItem(ModuleSetupText::typeLabel(t), t);
    type_->setAccessibleName(tr("Table type"));
    tableForm->addRow(tr("Type"), type_);
    typeHint_ = new QLabel(tableBox);
    typeHint_->setWordWrap(true);
    tableForm->addRow(QString(), typeHint_);
    height_ = new QLineEdit(tableBox);
    height_->setAccessibleName(tr("Height"));
    tableForm->addRow(tr("Height"), height_);
    heightProblem_ = new QLabel(tableBox);
    heightProblem_->setWordWrap(true);
    heightProblem_->setStyleSheet(QStringLiteral("color: palette(link-visited);"));
    heightProblem_->hide();
    tableForm->addRow(QString(), heightProblem_);
    measured_ = new QWidget(tableBox);
    auto* measuredRow = new QHBoxLayout(measured_);
    measuredRow->setContentsMargins(0, 0, 0, 0);
    toTable_ = new QRadioButton(tr("To the table top"), measured_);
    toRail_ = new QRadioButton(tr("To the top of the rails"), measured_);
    auto* measuredGroup = new QButtonGroup(measured_);
    measuredGroup->addButton(toTable_);
    measuredGroup->addButton(toRail_);
    measuredRow->addWidget(toTable_);
    measuredRow->addWidget(toRail_);
    measuredRow->addStretch();
    tableForm->addRow(tr("Measured"), measured_);
    col->addWidget(tableBox);

    // Power: whether it needs any, and then how much and where the plug is.
    auto* powerBox = new QGroupBox(tr("Power"), this);
    auto* powerCol = new QVBoxLayout(powerBox);
    power_ = new QComboBox(powerBox);
    power_->setAccessibleName(tr("Power"));
    power_->addItem(tr("Not set"), QString());
    power_->addItem(tr("Doesn’t need power"), QStringLiteral("no"));
    power_->addItem(tr("Needs power"), QStringLiteral("yes"));
    powerCol->addWidget(power_);
    powerDetails_ = new QWidget(powerBox);
    auto* detailsForm = new QFormLayout(powerDetails_);
    detailsForm->setContentsMargins(0, 0, 0, 0);
    outlets_ = new QSpinBox(powerDetails_);
    outlets_->setRange(0, core::kMaxOutlets);
    detailsForm->addRow(tr("Outlets"), outlets_);
    watts_ = new QSpinBox(powerDetails_);
    watts_->setRange(0, static_cast<int>(core::kMaxWatts));
    watts_->setSingleStep(10);
    watts_->setSuffix(QStringLiteral(" W"));
    watts_->setSpecialValueText(tr("Not known"));
    detailsForm->addRow(tr("About how many watts"), watts_);
    side_ = new QComboBox(powerDetails_);
    side_->addItem(tr("Not set"), QString());
    for (const QString& s : core::plugSides()) side_->addItem(ModuleSetupText::sideLabel(s), s);
    detailsForm->addRow(tr("Plug side"), side_);
    auto* sideHint = new QLabel(tr("Front is the side people watch from."), powerDetails_);
    sideHint->setWordWrap(true);
    detailsForm->addRow(QString(), sideHint);
    powerCol->addWidget(powerDetails_);
    col->addWidget(powerBox);

    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->hide();
    col->addWidget(error_);

    auto* buttons = new QDialogButtonBox(this);
    clear_ = buttons->addButton(tr("Clear all"), QDialogButtonBox::ResetRole);
    auto* done = buttons->addButton(tr("Done"), QDialogButtonBox::AcceptRole);
    done->setDefault(false);
    done->setAutoDefault(false);
    clear_->setAutoDefault(false);
    col->addWidget(buttons);

    auto setup = [this] { return current_().value_or(core::ModuleSetup{}); };
    connect(type_, &QComboBox::currentIndexChanged, this, [this, setup](int) {
        if (refreshing_) return;
        core::ModuleSetup s = setup();
        const QString type = type_->currentData().toString();
        if (type.isEmpty()) s.table.reset();
        else {
            core::ModuleTable t = s.table.value_or(core::ModuleTable{});
            t.type = type;
            s.table = t;
        }
        write(s, tr("Module table"));
    });
    connect(height_, &QLineEdit::editingFinished, this, &ModuleSetupDialog::commitHeight);
    connect(toRail_, &QRadioButton::toggled, this, [this, setup](bool rail) {
        if (refreshing_) return;
        core::ModuleSetup s = setup();
        if (!s.table || s.table->toRail == rail) return;
        s.table->toRail = rail;
        write(s, tr("Module table"));
    });
    connect(power_, &QComboBox::currentIndexChanged, this, [this, setup](int) {
        if (refreshing_) return;
        core::ModuleSetup s = setup();
        const QString v = power_->currentData().toString();
        if (v.isEmpty()) s.power.reset();
        else if (v == QLatin1String("no")) s.power = core::ModulePower{};
        else {
            core::ModulePower p = s.power.value_or(core::ModulePower{});
            p.needed = true;
            if (!p.outlets) p.outlets = 1;
            s.power = p;
        }
        write(s, tr("Module power"));
    });
    auto powerChange = [this, setup] {
        if (refreshing_) return;
        core::ModuleSetup s = setup();
        if (!s.power || !s.power->needed) return;
        s.power->outlets = outlets_->value();
        s.power->watts = watts_->value() > 0 ? std::optional<int>(watts_->value()) : std::nullopt;
        s.power->plugSide = side_->currentData().toString();
        write(s, tr("Module power"));
    };
    connect(outlets_, &QSpinBox::valueChanged, this, powerChange);
    connect(watts_, &QSpinBox::valueChanged, this, powerChange);
    connect(side_, &QComboBox::currentIndexChanged, this, powerChange);
    connect(clear_, &QPushButton::clicked, this, [this] { write(core::ModuleSetup{}, tr("Clear module table and power")); });
    connect(done, &QPushButton::clicked, this, [this] {
        commitHeight();
        accept();
    });
    refresh();
}

void ModuleSetupDialog::write(const core::ModuleSetup& s, const QString& undoText) {
    if (const auto was = current_(); was && *was == s) return;
    if (apply_) apply_(s, undoText);
    refresh();
}

void ModuleSetupDialog::commitHeight() {
    if (refreshing_) return;
    const auto now = current_();
    if (!now || !now->table) return;
    core::ModuleSetup s = *now;
    const QString text = height_->text().trimmed();
    if (text.isEmpty()) {
        heightProblem_->hide();
        if (!s.table->heightMm) return;
        s.table->heightMm.reset();
        s.table->toRail = false;
        write(s, tr("Module table"));
        return;
    }
    const auto mm = ModuleSetupText::parseHeightMm(text, unit_);
    if (!mm) {
        heightProblem_->setText(tr("That isn’t a height we can read. %1.").arg(ModuleSetupText::unitHint(unit_)));
        heightProblem_->show();
        return;
    }
    heightProblem_->hide();
    s.table->heightMm = mm;
    write(s, tr("Module table"));
    // The same height typed another way: show it the usual way.
    refresh();
}

void ModuleSetupDialog::setNote(const QString& note) {
    note_->setText(note);
    note_->setVisible(!note.isEmpty());
}

void ModuleSetupDialog::setError(const QString& error) {
    error_->setText(error);
    error_->setVisible(!error.isEmpty());
}

void ModuleSetupDialog::refresh() {
    const auto now = current_();
    if (!now) {
        // Deleted here or by someone else: nothing left to edit.
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }
    refreshing_ = true;
    const core::ModuleSetup& s = *now;
    type_->setCurrentIndex(std::max(0, type_->findData(s.table ? s.table->type : QString())));
    typeHint_->setText(s.table ? ModuleSetupText::typeHint(s.table->type) : QString());
    typeHint_->setVisible(s.table.has_value());
    height_->setEnabled(s.table.has_value());
    height_->setPlaceholderText(s.table ? ModuleSetupText::unitHint(unit_) : tr("Pick a table type first"));
    if (!height_->hasFocus() || !s.table)
        height_->setText(s.table && s.table->heightMm ? ModuleSetupText::formatHeight(*s.table->heightMm, unit_) : QString());
    measured_->setEnabled(s.table && s.table->heightMm);
    (s.table && s.table->toRail ? toRail_ : toTable_)->setChecked(true);
    power_->setCurrentIndex(!s.power ? 0 : s.power->needed ? 2 : 1);
    powerDetails_->setVisible(s.power && s.power->needed);
    if (s.power && s.power->needed) {
        outlets_->setValue(core::outletsOf(*s.power));
        watts_->setValue(s.power->watts.value_or(0));
        side_->setCurrentIndex(std::max(0, side_->findData(s.power->plugSide)));
    }
    clear_->setEnabled(!s.empty());
    refreshing_ = false;
}

}  // namespace bld::ui
