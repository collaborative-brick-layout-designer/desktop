// Earlier saves (FileBackups.h): saving over a layout file keeps the file
// as it was, the newest N per file (Settings › Saving; none at 0), never
// the same twice, a .bbm with its labels, modules and venue; File ›
// Earlier Saves… opens one as a new, unsaved layout or puts it back, after
// keeping the file as it is.

#include "ui/EarlierSavesDialog.h"
#include "ui/FileBackups.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/SettingsDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"

#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "saveload/SidecarIO.h"

#include "DialogAnswers.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>

using namespace bld;

namespace {

const QString kCorpus = QStringLiteral(BLD_BBM_CORPUS_DIR);

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void write(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

class FileBackupsTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QDir(ui::FileBackups::folder()).removeRecursively();
        QSettings().remove(QStringLiteral("files/keepSavedCopies"));
        file_ = dir_.filePath(QStringLiteral("yard.bld-layout"));
    }
    void TearDown() override {
        QDir(ui::FileBackups::folder()).removeRecursively();
        QSettings().remove(QStringLiteral("files/keepSavedCopies"));
        QStandardPaths::setTestModeEnabled(false);
    }
    QTemporaryDir dir_;
    QString file_;
};

}  // namespace

TEST_F(FileBackupsTest, KeepsTheFileAsItWasNeverTwiceAndOnlyTheNewestN) {
    EXPECT_EQ(ui::FileBackups::keepCount(), 10);
    // Nothing there yet: nothing to keep.
    EXPECT_TRUE(ui::FileBackups::keepBeforeSaving(file_));
    EXPECT_TRUE(ui::FileBackups::copiesOf(file_).isEmpty());

    const QDateTime t0 = QDateTime::currentDateTime();
    write(file_, "one");
    ASSERT_TRUE(ui::FileBackups::keepBeforeSaving(file_, nullptr, t0));
    // The same again: not kept twice.
    ASSERT_TRUE(ui::FileBackups::keepBeforeSaving(file_, nullptr, t0.addSecs(1)));
    ASSERT_EQ(ui::FileBackups::copiesOf(file_).size(), 1);
    EXPECT_EQ(readAll(ui::FileBackups::copiesOf(file_).first().path), "one");

    ui::FileBackups::setKeepCount(3);
    for (int i = 0; i < 5; ++i) {
        write(file_, QByteArray("v") + QByteArray::number(i));
        ASSERT_TRUE(ui::FileBackups::keepBeforeSaving(file_, nullptr, t0.addSecs(10 + i)));
    }
    const auto copies = ui::FileBackups::copiesOf(file_);
    ASSERT_EQ(copies.size(), 3);
    EXPECT_EQ(readAll(copies.at(0).path), "v4");
    EXPECT_EQ(readAll(copies.at(2).path), "v2");
    // Listed by the file they are of, even when it isn't open.
    const auto files = ui::FileBackups::files();
    ASSERT_EQ(files.size(), 1);
    EXPECT_EQ(QFileInfo(files.first().file), QFileInfo(file_));
    EXPECT_EQ(files.first().copies, 3);

    // None: nothing more is kept.
    ui::FileBackups::setKeepCount(0);
    write(file_, "later");
    ASSERT_TRUE(ui::FileBackups::keepBeforeSaving(file_, nullptr, t0.addSecs(60)));
    EXPECT_EQ(ui::FileBackups::copiesOf(file_).size(), 3);
}

TEST_F(FileBackupsTest, ABbmKeepsItsLabelsModulesAndVenueAndPutBackRemovesOnesItDidntHave) {
    const QString bbm = dir_.filePath(QStringLiteral("corner.bbm"));
    const QString side = saveload::sidecarPathFor(bbm);
    const QDateTime earlier = QDateTime::currentDateTime().addSecs(-3600);
    write(bbm, "map 1");
    write(side, "sidecar 1");
    ASSERT_TRUE(ui::FileBackups::keepBeforeSaving(bbm, nullptr, earlier));
    write(bbm, "map 2");
    QFile::remove(side);
    ASSERT_TRUE(ui::FileBackups::keepBeforeSaving(bbm, nullptr, earlier.addSecs(5)));
    auto copies = ui::FileBackups::copiesOf(bbm);
    ASSERT_EQ(copies.size(), 2);
    EXPECT_TRUE(copies.at(0).sidecar.isEmpty());
    EXPECT_EQ(readAll(copies.at(1).sidecar), "sidecar 1");

    // Put the first back: map and sidecar as they were; the file as it was is kept first.
    write(bbm, "map 3");
    QString error;
    ASSERT_TRUE(ui::FileBackups::restore(copies.at(1), bbm, &error)) << error.toStdString();
    EXPECT_EQ(readAll(bbm), "map 1");
    EXPECT_EQ(readAll(side), "sidecar 1");
    copies = ui::FileBackups::copiesOf(bbm);
    ASSERT_EQ(copies.size(), 3);
    EXPECT_EQ(readAll(copies.at(0).path), "map 3");
    // Putting back one without a sidecar removes the one beside the file.
    ASSERT_TRUE(ui::FileBackups::restore(copies.at(1), bbm, &error));
    EXPECT_EQ(readAll(bbm), "map 2");
    EXPECT_FALSE(QFileInfo::exists(side));
}

TEST_F(FileBackupsTest, PutBackKeepsTheFileEvenWithKeepingOff) {
    write(file_, "old");
    ASSERT_TRUE(ui::FileBackups::keepBeforeSaving(file_, nullptr, QDateTime::currentDateTime()));
    ui::FileBackups::setKeepCount(0);
    write(file_, "new");
    const auto copy = ui::FileBackups::copiesOf(file_).first();
    ASSERT_TRUE(ui::FileBackups::restore(copy, file_));
    EXPECT_EQ(readAll(file_), "old");
    const auto copies = ui::FileBackups::copiesOf(file_);
    ASSERT_EQ(copies.size(), 1);
    EXPECT_EQ(readAll(copies.first().path), "new");
}

// writeMapTo is the window's own (what Save and Save As call).
struct TestWindow : ui::MainWindow {
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::writeMapTo;
};

class EarlierSavesWindow : public FileBackupsTest {
protected:
    void SetUp() override {
        FileBackupsTest::SetUp();
        window_ = std::make_unique<TestWindow>(parts_);
        ASSERT_TRUE(window_->openFile(kCorpus + QStringLiteral("/tight-corner.bbm")));
        ui::EarlierSavesDialog::confirmPutBack = [](QWidget*, const QString&, const QString&) { return true; };
    }
    void TearDown() override {
        window_.reset();
        FileBackupsTest::TearDown();
    }
    parts::PartsLibrary parts_;
    std::unique_ptr<TestWindow> window_;
};

TEST_F(EarlierSavesWindow, SavingOverALayoutFileKeepsTheOneBeforeAndFileMenuOpensOrPutsItBack) {
    auto* view = window_->findChild<ui::MapView*>();
    ASSERT_TRUE(window_->writeMapTo(file_));
    const QByteArray first = readAll(file_);
    // No earlier save for a new file.
    EXPECT_TRUE(ui::FileBackups::copiesOf(file_).isEmpty());
    view->undoStack()->push(new QUndoCommand(QStringLiteral("a change")));
    view->currentMap()->author = QStringLiteral("Changed");
    ASSERT_TRUE(window_->writeMapTo(file_));
    ASSERT_EQ(ui::FileBackups::copiesOf(file_).size(), 1);
    EXPECT_EQ(readAll(ui::FileBackups::copiesOf(file_).first().path), first);

    // The File menu has it.
    QAction* menuItem = nullptr;
    for (QAction* a : window_->findChildren<QAction*>())
        if (a->objectName() == QLatin1String("file.earlierSaves")) menuItem = a;
    ASSERT_NE(menuItem, nullptr);

    // The dialog lists the open file's earlier saves; Open as a Copy.
    {
        ui::EarlierSavesDialog dialog(file_);
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("earlierSavesList"));
        ASSERT_EQ(list->count(), 1);
        EXPECT_TRUE(list->item(0)->text().startsWith(QStringLiteral("Saved before")));
        dialog.findChild<QPushButton*>(QStringLiteral("earlierSavesOpen"))->click();
        EXPECT_EQ(dialog.action(), ui::EarlierSavesDialog::Action::OpenCopy);
        const auto chosen = dialog.chosen();
        ASSERT_TRUE(chosen.has_value());
        ASSERT_TRUE(window_->openSavedCopy(*chosen, dialog.file()));
    }
    // A new, unsaved layout as the file was; the file itself unchanged.
    EXPECT_NE(view->currentMap()->author, QStringLiteral("Changed"));
    EXPECT_FALSE(view->undoStack()->isClean());
    EXPECT_FALSE(window_->windowTitle().contains(QStringLiteral("yard")));
    EXPECT_TRUE(readAll(file_) != first);
    EXPECT_FALSE(QSettings().value(QStringLiteral("recent/list")).toStringList().contains(ui::FileBackups::copiesOf(file_).first().path));

    // Put Back from the File menu, with the file open.
    view->undoStack()->setClean();
    ASSERT_TRUE(window_->openFile(file_));
    Answers putBack{ { clickIn(QStringLiteral("Put Back…")) } };
    putBack.during([&] { window_->onEarlierSaves(); });
    EXPECT_EQ(readAll(file_), first);
    EXPECT_NE(view->currentMap()->author, QStringLiteral("Changed"));
    // The file as it was before putting back is an earlier save now.
    ASSERT_EQ(ui::FileBackups::copiesOf(file_).size(), 2);
}

TEST_F(EarlierSavesWindow, WithNothingKeptTheDialogSaysWhereToTurnItOn) {
    ui::FileBackups::setKeepCount(0);
    ui::EarlierSavesDialog dialog{ QString() };
    const auto labels = dialog.findChildren<QLabel*>();
    bool said = false;
    for (QLabel* l : labels) said = said || l->text().contains(QStringLiteral("Settings › Saving"));
    EXPECT_TRUE(said);
    EXPECT_FALSE(dialog.findChild<QPushButton*>(QStringLiteral("earlierSavesOpen"))->isEnabled());
}

TEST_F(EarlierSavesWindow, SettingsSetsHowManyAreKept) {
    ui::theme::PrefsStore store(QStringLiteral("fileBackupsTestPrefs"));
    ui::SettingsDialog settings(store, QString());
    auto* spin = settings.findChild<QSpinBox*>(QStringLiteral("files/keepSavedCopies"));
    ASSERT_NE(spin, nullptr);
    EXPECT_EQ(spin->value(), 10);
    spin->setValue(4);
    EXPECT_EQ(ui::FileBackups::keepCount(), 4);
    spin->setValue(0);
    EXPECT_EQ(spin->text(), QStringLiteral("None"));
    EXPECT_EQ(ui::FileBackups::keepCount(), 0);
}
