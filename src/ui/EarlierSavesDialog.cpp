#include "EarlierSavesDialog.h"

#include "ConfirmDialog.h"
#include "RecoverDialog.h"
#include "help/HelpButton.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace bld::ui {

std::function<bool(QWidget*, const QString&, const QString&)> EarlierSavesDialog::confirmPutBack =
    [](QWidget* parent, const QString& fileName, const QString& when) {
        ConfirmOptions o;
        o.title = EarlierSavesDialog::tr("Put back the save from %1?").arg(when);
        o.removes = EarlierSavesDialog::tr("%1 becomes as it was saved then.").arg(fileName);
        o.keeps = EarlierSavesDialog::tr("The file as it is now is kept first, as an earlier save, so you can put it back too.");
        o.undo = EarlierSavesDialog::tr("You can undo this from File › Earlier Saves….");
        o.confirmLabel = EarlierSavesDialog::tr("Put Back");
        o.danger = false;
        return ConfirmDialog::ask(parent, o);
    };

std::function<void(const QString&)> EarlierSavesDialog::openFolder = [](const QString& folder) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
};

EarlierSavesDialog::EarlierSavesDialog(const QString& currentFile, QWidget* parent) : QDialog(parent) {
    setWindowTitle(EarlierSavesDialog::tr("Earlier saves"));
    setObjectName(QStringLiteral("earlierSavesDialog"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(EarlierSavesDialog::tr("Earlier saves"), this);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    title->setFont(tf);
    head->addWidget(title, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("file.earlierSaves"), this));
    col->addLayout(head);

    auto* fileRow = new QHBoxLayout();
    auto* fileLabel = new QLabel(EarlierSavesDialog::tr("Layout file:"), this);
    fileBox_ = new QComboBox(this);
    fileBox_->setObjectName(QStringLiteral("earlierSavesFile"));
    fileBox_->setAccessibleName(EarlierSavesDialog::tr("Layout file"));
    fileLabel->setBuddy(fileBox_);
    fileRow->addWidget(fileLabel);
    fileRow->addWidget(fileBox_, 1);
    col->addLayout(fileRow);

    intro_ = new QLabel(this);
    intro_->setWordWrap(true);
    col->addWidget(intro_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("earlierSavesList"));
    list_->setAccessibleName(EarlierSavesDialog::tr("Earlier saves"));
    list_->setMinimumWidth(460);
    col->addWidget(list_, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* close = buttons->addButton(EarlierSavesDialog::tr("Close"), QDialogButtonBox::RejectRole);
    folder_ = buttons->addButton(EarlierSavesDialog::tr("Show Folder"), QDialogButtonBox::ActionRole);
    folder_->setObjectName(QStringLiteral("earlierSavesFolder"));
    folder_->setToolTip(EarlierSavesDialog::tr("Open the folder the earlier saves of this file are kept in"));
    putBack_ = buttons->addButton(EarlierSavesDialog::tr("Put Back…"), QDialogButtonBox::ActionRole);
    putBack_->setObjectName(QStringLiteral("earlierSavesPutBack"));
    putBack_->setToolTip(EarlierSavesDialog::tr("Make the file as it was in the picked save (the file as it is now is kept first)"));
    open_ = buttons->addButton(EarlierSavesDialog::tr("Open as a Copy"), QDialogButtonBox::AcceptRole);
    open_->setObjectName(QStringLiteral("earlierSavesOpen"));
    open_->setToolTip(EarlierSavesDialog::tr("Open the picked save as a new layout, leaving the file as it is"));
    open_->setDefault(true);
    col->addWidget(buttons);

    connect(fileBox_, &QComboBox::currentIndexChanged, this, [this] { fillCopies(); });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(list_, &QListWidget::itemDoubleClicked, open_, &QPushButton::click);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(folder_, &QPushButton::clicked, this, [this] {
        if (!file().isEmpty()) openFolder(FileBackups::folderFor(file()));
    });
    connect(open_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || row >= copies_.size()) return;
        chosen_ = copies_.at(row);
        action_ = Action::OpenCopy;
        accept();
    });
    connect(putBack_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || row >= copies_.size()) return;
        if (!confirmPutBack(this, QFileInfo(file()).fileName(), RecoverDialog::whenText(copies_.at(row).at))) return;
        chosen_ = copies_.at(row);
        action_ = Action::PutBack;
        accept();
    });
    fillFiles(currentFile);
}

QString EarlierSavesDialog::file() const {
    return fileBox_->currentData().toString();
}

void EarlierSavesDialog::fillFiles(const QString& currentFile) {
    files_ = FileBackups::files();
    QSignalBlocker block(fileBox_);
    fileBox_->clear();
    int pick = 0;
    if (!currentFile.isEmpty()) {
        fileBox_->addItem(QFileInfo(currentFile).fileName(), currentFile);
        fileBox_->setItemData(0, QDir::toNativeSeparators(currentFile), Qt::ToolTipRole);
    }
    for (const BackedUpFile& f : std::as_const(files_)) {
        if (!currentFile.isEmpty() && QFileInfo(f.file) == QFileInfo(currentFile)) continue;
        fileBox_->addItem(QFileInfo(f.file).fileName(), f.file);
        fileBox_->setItemData(fileBox_->count() - 1, QDir::toNativeSeparators(f.file), Qt::ToolTipRole);
    }
    fileBox_->setCurrentIndex(pick);
    fileBox_->setEnabled(fileBox_->count() > 1);
    fillCopies();
}

void EarlierSavesDialog::fillCopies() {
    list_->clear();
    copies_ = file().isEmpty() ? QList<SavedCopy>() : FileBackups::copiesOf(file());
    for (const SavedCopy& c : std::as_const(copies_)) {
        auto* item = new QListWidgetItem(EarlierSavesDialog::tr("Saved before %1 — %2").arg(RecoverDialog::whenText(c.at), QLocale().formattedDataSize(c.bytes)), list_);
        item->setToolTip(QDir::toNativeSeparators(c.path));
    }
    const int keep = FileBackups::keepCount();
    if (fileBox_->count() == 0) {
        intro_->setText(keep > 0 ? EarlierSavesDialog::tr("There are no earlier saves yet. Each time you save a layout over its file, the file as "
                                      "it was is kept here: the last %n of each file (Settings › Saving).", "", keep)
                                 : EarlierSavesDialog::tr("Earlier saves are turned off. Turn them on in Settings › Saving."));
    } else if (copies_.isEmpty()) {
        intro_->setText(keep > 0 ? EarlierSavesDialog::tr("This file has no earlier saves yet. The next time you save over it, the file as it was "
                                      "is kept here.")
                                 : EarlierSavesDialog::tr("Earlier saves are turned off. Turn them on in Settings › Saving."));
    } else {
        intro_->setText(EarlierSavesDialog::tr("Each time this file was saved, the file as it was just before was kept. Open one as a copy "
                           "to look at it, or put it back in place of the file."));
        list_->setCurrentRow(0);
    }
    list_->setVisible(!copies_.isEmpty());
    updateButtons();
}

void EarlierSavesDialog::updateButtons() {
    const bool picked = list_->currentRow() >= 0 && list_->currentRow() < copies_.size();
    open_->setEnabled(picked);
    // Only a file that is still there (or can be made again) can be put back.
    putBack_->setEnabled(picked && !file().isEmpty());
    folder_->setEnabled(!copies_.isEmpty());
}

} // namespace bld::ui
