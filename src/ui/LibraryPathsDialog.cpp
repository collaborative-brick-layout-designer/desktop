#include "LibraryPathsDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::ui {

LibraryPathsDialog::LibraryPathsDialog(QStringList initial, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Parts Folders"));
    resize(600, 360);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Folders of parts to show in the Parts panel, beside the parts that come with the app, such as "
           "a TrixBrix or BrickTracks pack or parts a friend sent. When two folders have a part with the "
           "same number, the one higher in the list is used."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* row = new QHBoxLayout();

    list_ = new QListWidget(this);
    list_->setAccessibleName(tr("Parts folders"));
    for (const QString& p : initial) list_->addItem(p);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    // A long path scrolls, rather than widening the window.
    list_->setTextElideMode(Qt::ElideMiddle);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setMinimumWidth(300);
    list_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    row->addWidget(list_, 1);

    auto* side = new QVBoxLayout();
    auto* addBtn    = new QPushButton(tr("Add…"),    this);
    auto* removeBtn = new QPushButton(tr("Remove"),    this);
    auto* upBtn     = new QPushButton(tr("Move Up"),   this);
    auto* downBtn   = new QPushButton(tr("Move Down"), this);
    side->addWidget(addBtn);
    side->addWidget(removeBtn);
    side->addWidget(upBtn);
    side->addWidget(downBtn);
    side->addStretch();
    row->addLayout(side);

    layout->addLayout(row, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(bb);

    connect(addBtn,    &QPushButton::clicked, this, &LibraryPathsDialog::onAdd);
    connect(removeBtn, &QPushButton::clicked, this, &LibraryPathsDialog::onRemove);
    connect(upBtn,     &QPushButton::clicked, this, &LibraryPathsDialog::onMoveUp);
    connect(downBtn,   &QPushButton::clicked, this, &LibraryPathsDialog::onMoveDown);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QStringList LibraryPathsDialog::paths() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) out << list_->item(i)->text();
    return out;
}

void LibraryPathsDialog::onAdd() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose a parts folder"),
        {}, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    // Avoid duplicates.
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->text() == dir) return;
    }
    list_->addItem(dir);
}

void LibraryPathsDialog::onRemove() {
    qDeleteAll(list_->selectedItems());
}

void LibraryPathsDialog::onMoveUp() {
    const int row = list_->currentRow();
    if (row <= 0) return;
    auto* item = list_->takeItem(row);
    list_->insertItem(row - 1, item);
    list_->setCurrentRow(row - 1);
}

void LibraryPathsDialog::onMoveDown() {
    const int row = list_->currentRow();
    if (row < 0 || row >= list_->count() - 1) return;
    auto* item = list_->takeItem(row);
    list_->insertItem(row + 1, item);
    list_->setCurrentRow(row + 1);
}

}
