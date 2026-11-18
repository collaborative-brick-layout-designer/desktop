#include "SharedUpdatesDialog.h"

#include "help/HelpButton.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace bld::ui {

namespace {

// A choice and the line under it that says what it does.
QRadioButton* choice(QVBoxLayout* col, const QString& label, const QString& text, QWidget* parent) {
    auto* b = new QRadioButton(label, parent);
    QFont f = b->font();
    f.setBold(true);
    b->setFont(f);
    col->addWidget(b);
    auto* words = new QLabel(text, parent);
    words->setWordWrap(true);
    words->setContentsMargins(24, 0, 0, 6);
    words->setForegroundRole(QPalette::PlaceholderText);
    words->setBuddy(b);
    col->addWidget(words);
    b->setAccessibleDescription(text);
    return b;
}

}  // namespace

SharedUpdatesDialog::SharedUpdatesDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("sharedUpdatesDialog"));
    setWindowTitle(tr("Updates from Shared Modules"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    auto* about = new QLabel(tr("When someone who shared a module with this layout saves a new version:"), this);
    about->setWordWrap(true);
    head->addWidget(about, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("module.sharedUpdatesSetting"), this), 0, Qt::AlignTop);
    col->addLayout(head);
    ask_ = choice(col, tr("Ask first"), tr("The layout says when a shared module has a new version. Nothing changes until someone chooses Update."), this);
    ask_->setObjectName(QStringLiteral("sharedUpdatesAsk"));
    auto_ = choice(col, tr("Automatically"),
                   tr("A new version goes in as soon as its owner saves it. You'll see what changed and anything it now overlaps."), this);
    auto_->setObjectName(QStringLiteral("sharedUpdatesAuto"));
    auto* group = new QButtonGroup(this);
    group->addButton(ask_);
    group->addButton(auto_);
    readOnly_ = new QLabel(tr("Someone who can change this layout can change this."), this);
    readOnly_->setObjectName(QStringLiteral("sharedUpdatesReadOnly"));
    readOnly_->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(readOnly_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Done"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addWidget(buttons);
    const auto picked = [this](const QString& value) {
        if (busy_ || !canChange_ || value == current_) return;
        emit chosen(value);
    };
    connect(ask_, &QRadioButton::toggled, this, [picked](bool on) {
        if (on) picked(QStringLiteral("ask"));
    });
    connect(auto_, &QRadioButton::toggled, this, [picked](bool on) {
        if (on) picked(QStringLiteral("auto"));
    });
    refresh();
}

void SharedUpdatesDialog::setCurrent(const QString& updates) {
    current_ = updates;
    refresh();
}

void SharedUpdatesDialog::setCanChange(bool can) {
    canChange_ = can;
    refresh();
}

void SharedUpdatesDialog::setBusy(bool busy) {
    busy_ = busy;
    refresh();
}

void SharedUpdatesDialog::refresh() {
    {
        // Showing what the server says isn't a choice someone made.
        const QSignalBlocker a(ask_), b(auto_);
        ask_->setAutoExclusive(false);
        auto_->setAutoExclusive(false);
        ask_->setChecked(current_ == QLatin1String("ask"));
        auto_->setChecked(current_ == QLatin1String("auto"));
        ask_->setAutoExclusive(true);
        auto_->setAutoExclusive(true);
    }
    const bool open = canChange_ && !busy_ && !current_.isEmpty();
    ask_->setEnabled(open);
    auto_->setEnabled(open);
    readOnly_->setVisible(!current_.isEmpty() && !canChange_);
}

}  // namespace bld::ui
