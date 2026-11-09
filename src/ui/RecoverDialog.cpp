#include "RecoverDialog.h"

#include "ConfirmDialog.h"
#include "help/HelpButton.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::ui {

std::function<bool(QWidget*, const QString&)> RecoverDialog::confirmDiscard = [](QWidget* parent,
                                                                                   const QString& name) {
    DeleteWording w;
    w.verb = RecoverDialog::tr("Discard");
    w.title = RecoverDialog::tr("Discard the unsaved changes to “%1”?").arg(name);
    w.removes = RecoverDialog::tr("The changes that were never saved are deleted.");
    w.keeps = RecoverDialog::tr("The file itself stays as it was last saved.");
    return ConfirmDialog::confirmDelete(parent, name, w);
};

QString RecoverDialog::whenText(const QDateTime& at) {
    const QDate day = at.date(), today = QDate::currentDate();
    const QString time = QLocale().toString(at.time(), QLocale::ShortFormat);
    if (day == today) return tr("Today %1").arg(time);
    if (day == today.addDays(-1)) return tr("Yesterday %1").arg(time);
    return QLocale().toString(at, QLocale::ShortFormat);
}

RecoverDialog::RecoverDialog(QList<AutosaveLeftover> leftovers, QWidget* parent)
    : QDialog(parent), leftovers_(std::move(leftovers)) {
    setWindowTitle(tr("Recover unsaved work"));
    setObjectName(QStringLiteral("recoverDialog"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("Recover unsaved work"), this);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    title->setFont(tf);
    head->addWidget(title, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("file.recover"), this));
    col->addLayout(head);

    intro_ = new QLabel(this);
    intro_->setWordWrap(true);
    col->addWidget(intro_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("recoverList"));
    list_->setAccessibleName(tr("Unsaved work"));
    list_->setMinimumWidth(420);
    col->addWidget(list_, 1);

    auto* buttons = new QDialogButtonBox(this);
    later_ = buttons->addButton(tr("Not now"), QDialogButtonBox::RejectRole);
    later_->setToolTip(tr("Keep them for later: File › Recover Unsaved Work…"));
    discard_ = buttons->addButton(tr("Discard"), QDialogButtonBox::DestructiveRole);
    discard_->setToolTip(tr("Delete the picked changes for good"));
    recover_ = buttons->addButton(tr("Recover"), QDialogButtonBox::AcceptRole);
    recover_->setToolTip(tr("Open the picked layout with its changes, ready to save"));
    recover_->setDefault(true);
    col->addWidget(buttons);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(list_, &QListWidget::itemDoubleClicked, recover_, &QPushButton::click);
    connect(recover_, &QPushButton::clicked, this, [this] {
        const int row = current();
        if (row < 0) return;
        chosen_ = leftovers_.takeAt(row);
        accept();
    });
    connect(discard_, &QPushButton::clicked, this, [this] {
        const int row = current();
        if (row < 0) return;
        const AutosaveLeftover l = leftovers_.at(row);
        if (!confirmDiscard(this, l.displayName())) return;
        AutosaveSlot::discard(l);
        leftovers_.removeAt(row);
        fill();
    });
    connect(later_, &QPushButton::clicked, this, &QDialog::reject);
    fill();
}

int RecoverDialog::current() const {
    const int row = list_->currentRow();
    return row >= 0 && row < leftovers_.size() ? row : -1;
}

void RecoverDialog::fill() {
    list_->clear();
    for (const auto& l : leftovers_) {
        auto* item = new QListWidgetItem(tr("%1 — changes from %2").arg(l.displayName(), whenText(l.savedAt)), list_);
        item->setToolTip(l.sourceFile.isEmpty() ? tr("A new layout that was never saved")
                                                : QDir::toNativeSeparators(l.sourceFile));
    }
    if (leftovers_.isEmpty()) {
        intro_->setText(tr("There's no unsaved work to recover: everything was saved."));
    } else {
        intro_->setText(tr("The app closed before these changes were saved. Recover one to keep working on it, "
                           "or discard what you don't need."));
        list_->setCurrentRow(0);
    }
    list_->setVisible(!leftovers_.isEmpty());
    later_->setText(leftovers_.isEmpty() ? tr("Close") : tr("Not now"));
    updateButtons();
}

void RecoverDialog::updateButtons() {
    const bool picked = current() >= 0;
    recover_->setVisible(!leftovers_.isEmpty());
    discard_->setVisible(!leftovers_.isEmpty());
    recover_->setEnabled(picked);
    discard_->setEnabled(picked);
}

}  // namespace bld::ui
