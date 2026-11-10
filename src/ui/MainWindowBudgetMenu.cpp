// Budget menu: the layout's budget (how many of each part it may use),
// saved with the layout and shared with everyone who edits it. New /
// Import from a .bbb file / Import and Merge / Export to a .bbb file /
// Remove, the three budget options, and the budget editor.

#include "MainWindow.h"

#include "BudgetDialog.h"
#include "BudgetSession.h"
#include "LiveLayout.h"
#include "MapView.h"
#include "NoticeArea.h"
#include "help/HelpTexts.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>

namespace bld::ui {

namespace {

QString budgetFilter() { return QObject::tr("BlueBrick budget (*.bbb)"); }

QString budgetFileStart() {
    const QString dir = BudgetSession::lastFileDir();
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : dir;
}

}  // namespace

void MainWindow::setupBudgetMenu() {
    auto* menu = menuBar()->addMenu(tr("&Budget"));

    // Nothing in the menu works until a budget exists, so it starts by
    // saying what one is.
    auto* whatAct = menu->addAction(tr("What's a &Budget?"));
    whatAct->setObjectName(QStringLiteral("act.budget.what"));
    whatAct->setToolTip(tr("What a budget is and how to start one"));
    connect(whatAct, &QAction::triggered, this, [this] {
        const auto entry = help::helpEntry(QStringLiteral("status.budget"));
        const QString text = entry ? entry->shortText + QStringLiteral("\n\n") + entry->more : QString();
        QMessageBox::information(this, tr("What's a budget?"),
                                 text + QStringLiteral("\n\n") + tr("Choose Budget › New Budget to start one."));
    });
    menu->addSeparator();
    auto* newAct = menu->addAction(tr("&New Budget"));
    newAct->setObjectName(QStringLiteral("act.budget.new"));
    newAct->setToolTip(tr("Start a budget for this layout: how many of each part it may use"));
    connect(newAct, &QAction::triggered, this, [this] {
        budget_->create();
        statusBar()->showMessage(
            tr("New budget started. Budget › Edit Budget sets how many of each part you have."), 6000);
    });
    auto* importAct = menu->addAction(tr("&Import Budget from File…"));
    importAct->setObjectName(QStringLiteral("act.budget.import"));
    importAct->setToolTip(tr("Use the limits in a BlueBrick budget file (.bbb) as this layout's budget"));
    connect(importAct, &QAction::triggered, this, [this]{
        const QString path = QFileDialog::getOpenFileName(this, tr("Import budget"), budgetFileStart(), budgetFilter());
        if (path.isEmpty()) return;
        QString err;
        if (!budget_->importFile(path, &err)) QMessageBox::warning(this, tr("Import budget"), err);
    });
    auto* mergeAct = menu->addAction(tr("Import and &Merge Budget…"));
    mergeAct->setObjectName(QStringLiteral("act.budget.merge"));
    mergeAct->setToolTip(tr("Add the limits of a budget file to this layout's budget"));
    connect(mergeAct, &QAction::triggered, this, [this]{
        const QString path = QFileDialog::getOpenFileName(this, tr("Import and merge budget"), budgetFileStart(),
                                                          budgetFilter());
        if (path.isEmpty()) return;
        QString err;
        if (!budget_->importAndMerge(path, &err)) QMessageBox::warning(this, tr("Import and merge budget"), err);
    });
    auto* exportAct = menu->addAction(tr("Export Budget to &File…"));
    exportAct->setObjectName(QStringLiteral("act.budget.export"));
    exportAct->setToolTip(tr("Save this layout's budget as a BlueBrick budget file (.bbb), for BlueBrick or another layout"));
    connect(exportAct, &QAction::triggered, this, [this]{
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export budget"), QDir(budgetFileStart()).filePath(tr("Budget.bbb")), budgetFilter());
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".bbb");
        QString err;
        if (!budget_->exportFile(path, &err)) QMessageBox::warning(this, tr("Export budget"), err);
        else statusBar()->showMessage(tr("Budget exported to %1").arg(QFileInfo(path).fileName()), 6000);
    });
    auto* removeAct = menu->addAction(tr("&Remove Budget"));
    removeAct->setObjectName(QStringLiteral("act.budget.remove"));
    removeAct->setToolTip(tr("Take the budget off this layout, for everyone who edits it (Undo brings it back)"));
    connect(removeAct, &QAction::triggered, this, [this]{ budget_->remove(); });
    menu->addSeparator();

    auto* onlyAct = menu->addAction(tr("Show Only &Parts in the Budget"));
    onlyAct->setCheckable(true);
    onlyAct->setToolTip(tr("Hide parts without a budget from the Parts panel"));
    connect(onlyAct, &QAction::toggled, this, [this](bool on){ budget_->setShowOnlyBudgetedParts(on); });
    auto* numbersAct = menu->addAction(tr("Show Budget N&umbers"));
    numbersAct->setCheckable(true);
    numbersAct->setToolTip(tr("Show how many of each part are used and allowed in the Parts panel"));
    connect(numbersAct, &QAction::toggled, this, [this](bool on){ budget_->setShowBudgetNumbers(on); });
    auto* limitAct = menu->addAction(tr("Stop at the Budget &Limits"));
    limitAct->setCheckable(true);
    limitAct->setToolTip(tr("Don't let anyone place more of a part than its budget allows"));
    connect(limitAct, &QAction::toggled, this, [this, numbersAct](bool on){
        budget_->setUseBudgetLimitation(on);
        // As BlueBrick: offer to show the numbers, which make the limit visible.
        const QString key = QStringLiteral("general/askShowBudgetNumbers");
        if (!on || numbersAct->isChecked() || !QSettings().value(key, true).toBool()) return;
        QMessageBox box(QMessageBox::Question, tr("Budget limits"),
                        tr("Do you also want to show the budget numbers in the Parts panel?"),
                        QMessageBox::Yes | QMessageBox::No, this);
        auto* dontAsk = new QCheckBox(tr("Don't ask again"), &box);
        box.setCheckBox(dontAsk);
        const bool yes = box.exec() == QMessageBox::Yes;
        QSettings().setValue(key, !dontAsk->isChecked());
        if (yes) numbersAct->setChecked(true);
    });
    menu->addSeparator();
    auto* editAct = menu->addAction(tr("&Edit Budget…"));
    editAct->setObjectName(QStringLiteral("act.budget.edit"));
    editAct->setToolTip(tr("Set how many of each part this layout may use"));
    connect(editAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap() || !budget_->exists()) return;
        auto* dlg = new BudgetDialog(*mapView_->currentMap(), *budget_, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    const auto sync = [=, this]{
        const bool on = budget_->exists();
        const bool can = budget_->canEdit();
        for (QAction* a : { newAct, importAct }) a->setEnabled(can);
        for (QAction* a : { mergeAct, exportAct, removeAct, onlyAct, numbersAct, limitAct, editAct })
            a->setEnabled(on && can);
        for (auto [a, v] : { std::pair{ onlyAct, budget_->showOnlyBudgetedParts() },
                             std::pair{ numbersAct, budget_->showBudgetNumbers() },
                             std::pair{ limitAct, budget_->useBudgetLimitation() } }) {
            const QSignalBlocker block(a);
            a->setChecked(v);
        }
    };
    connect(budget_, &BudgetSession::changed, this, sync);
    sync();

    // The budget file older versions kept for the whole app: offered to the
    // first layout without a budget, at start or when one is opened.
    BudgetSession::oldAppWideFile();  // noted now, before an import or export moves budget/lastFile
    connect(mapView_, &MapView::mapLoaded, this, [this] { QTimer::singleShot(0, this, &MainWindow::offerOldBudget); });
    QTimer::singleShot(0, this, &MainWindow::offerOldBudget);
}

void MainWindow::offerOldBudget() {
    if (!notices_ || !budget_->canEdit() || budget_->exists()) return;
    // A layout others only let you look at can't take a budget.
    if (live_ && live_->active() && live_->readOnly()) return;
    const QString path = BudgetSession::oldAppWideFile();
    if (path.isEmpty()) return;
    // Offered once: asked now, never again (Budget › Import Budget from File stays).
    BudgetSession::forgetOldAppWideFile();
    const QString id = QStringLiteral("budget.old");
    notices_->showNotice(
        id, tr("Bring your budget into this layout?"),
        tr("A budget is now saved with its layout, and shared with everyone who edits that layout. "
           "This layout has no budget yet. Your budget from before is in the file “%1”.\n\n"
           "You won't be asked again. To bring it into this or any other layout later, choose "
           "Budget › Import Budget from File.")
            .arg(QDir::toNativeSeparators(path)),
        { NoticeAction{ tr("Bring it in"),
                        [this, path] {
                            QString err;
                            if (!budget_->importFile(path, &err)) {
                                QMessageBox::warning(this, tr("Import budget"), err);
                                return;
                            }
                            statusBar()->showMessage(
                                tr("Your budget is in this layout now. Budget › Edit Budget shows it."), 6000);
                        },
                        true },
          NoticeAction{ tr("No, thanks"), {} } });
    notices_->addHelp(id, QStringLiteral("dialog.budget"));
}

}  // namespace bld::ui
