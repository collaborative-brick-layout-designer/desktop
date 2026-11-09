// Nothing a person made is lost to a file the app can't fully read, to a
// character XML can't hold, or to a crash (2026-11-09 review, area 5):
//   - a cut-off .bbm is refused, and the good copy left alone;
//   - a sidecar that can't be read is never deleted: the first save keeps
//     it as a .bak and says so;
//   - what opening a file couldn't read stays in a card, not a status line;
//   - a pasted control character doesn't stop saving;
//   - each window's layout autosaves to its own file, every leftover is
//     offered at start, and a recovered layout is unsaved;
//   - Save asks about the format for each file it opens.

#include "DialogAnswers.h"

#include "ui/Autosave.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/NoticeArea.h"
#include "ui/RecoverDialog.h"
#include "ui/UpdateCheck.h"

#include "core/LayerText.h"
#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>

using namespace bld;

namespace {

const QString kCorpus = QStringLiteral(BLD_BBM_CORPUS_DIR);

QByteArray bytesOf(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QString cardText(ui::MainWindow& w, const QString& id) {
    QFrame* card = w.notices()->card(id);
    if (!card) return {};
    QStringList text;
    for (auto* label : card->findChildren<QLabel*>()) text << label->text();
    return text.join(QLatin1Char('\n'));
}

// Any change: the layout is then unsaved, so it autosaves.
void makeAnEdit(ui::MainWindow& w) {
    w.findChild<ui::MapView*>()->undoStack()->push(new QUndoCommand(QStringLiteral("an edit")));
}

bool waitForAutosave(ui::MainWindow& w) {
    return QTest::qWaitFor([&] { return QFile::exists(w.autosavePath()); }, 9000);
}

class FileSafety : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QDir(ui::AutosaveSlot::folder()).removeRecursively();
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/autosave.bld-layout"));
        QSettings().remove(QStringLiteral("autosave"));
        bbm_ = dir_.filePath(QStringLiteral("corner.bbm"));
        ASSERT_TRUE(QFile::copy(kCorpus + QStringLiteral("/tight-corner.bbm"), bbm_));
        QFile(bbm_).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        window_ = std::make_unique<ui::MainWindow>(parts_);
    }
    void TearDown() override {
        window_.reset();
        QDir(ui::AutosaveSlot::folder()).removeRecursively();
        ui::RecoverDialog::confirmDiscard = saved_;
        QStandardPaths::setTestModeEnabled(false);
    }

    QTemporaryDir dir_;
    QString bbm_;
    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MainWindow> window_;
    decltype(ui::RecoverDialog::confirmDiscard) saved_ = ui::RecoverDialog::confirmDiscard;
};

} // namespace

TEST_F(FileSafety, ACutOffBbmIsRefusedAndTheFileLeftAlone) {
    ASSERT_TRUE(window_->openFile(bbm_));
    const QString title = window_->windowTitle();
    const QByteArray whole = bytesOf(bbm_);
    const QString cut = dir_.filePath(QStringLiteral("cut.bbm"));
    writeBytes(cut, whole.left(whole.size() / 2));
    QString said;
    Answers refuse{ { [&](QWidget* w) {
        if (auto* box = qobject_cast<QMessageBox*>(w)) said = box->text();
        click(QStringLiteral("OK"))(w);
    } } };
    bool opened = true;
    refuse.during([&] { opened = window_->openFile(cut); });
    EXPECT_FALSE(opened);
    EXPECT_TRUE(said.contains(QStringLiteral("cut.bbm couldn't be opened"))) << said.toStdString();
    EXPECT_TRUE(said.contains(QStringLiteral("cut off"))) << said.toStdString();
    // The layout that was open stays, and nothing was written.
    EXPECT_EQ(window_->windowTitle(), title);
    EXPECT_EQ(bytesOf(cut), whole.left(whole.size() / 2));
}

// R5.UnreadableSidecarSurvivesSave: Save used to delete it.
TEST_F(FileSafety, AnUnreadableSidecarIsKeptWhenSaving) {
    const QString sidecar = saveload::sidecarPathFor(bbm_);
    const QByteArray garbage = "{ \"anchoredLabels\": [ { \"id\": \"l1\", \"te";
    writeBytes(sidecar, garbage);
    ASSERT_TRUE(window_->openFile(bbm_));
    ASSERT_TRUE(window_->notices()->isShown(QStringLiteral("open-warnings")));
    const QString card = cardText(*window_, QStringLiteral("open-warnings"));
    EXPECT_TRUE(card.contains(QStringLiteral("corner.bbm.bld"))) << card.toStdString();
    EXPECT_TRUE(card.contains(QStringLiteral("couldn't be read"))) << card.toStdString();
    EXPECT_TRUE(card.contains(QStringLiteral(".bak"))) << card.toStdString();

    makeAnEdit(*window_);
    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    bool saved = false;
    keep.during([&] { saved = invoke(*window_, "onSave"); });
    ASSERT_TRUE(saved);
    EXPECT_EQ(bytesOf(sidecar + QStringLiteral(".bak")), garbage);
    EXPECT_TRUE(window_->notices()->isShown(QStringLiteral("kept-copies")));
    EXPECT_TRUE(
        cardText(*window_, QStringLiteral("kept-copies")).contains(QStringLiteral("corner.bbm.bld.bak")));
    // Copied once: the next save doesn't make another.
    Answers none;
    none.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_FALSE(QFile::exists(sidecar + QStringLiteral(".2.bak")));
}

// R5.SidecarMismatchIsShown: "Opened …" used to wipe the warning at once.
TEST_F(FileSafety, AMapChangedElsewhereIsSaidInACard) {
    auto map = saveload::readBbm(bbm_).map;
    ASSERT_TRUE(map);
    core::AnchoredLabel label;
    label.id = QStringLiteral("l1");
    label.text = QStringLiteral("Station");
    map->sidecar.anchoredLabels.push_back(label);
    ASSERT_TRUE(saveload::writeSidecar(saveload::sidecarPathFor(bbm_), bytesOf(bbm_), map->sidecar));
    writeBytes(bbm_, bytesOf(bbm_) + "\r\n"); // as another program would save it
    ASSERT_TRUE(window_->openFile(bbm_));
    EXPECT_TRUE(window_->statusBar()->currentMessage().startsWith(QStringLiteral("Opened corner.bbm")));
    ASSERT_TRUE(window_->notices()->isShown(QStringLiteral("open-warnings")));
    EXPECT_TRUE(cardText(*window_, QStringLiteral("open-warnings"))
                    .contains(QStringLiteral("changed in another program")));
    // The labels are still there.
    EXPECT_EQ(window_->findChild<ui::MapView*>()->currentMap()->sidecar.anchoredLabels.size(), 1u);
}

// A sheet of a kind this version can't read: opened without it, said, and
// the file copied before a save writes over it.
TEST_F(FileSafety, ASheetLeftOutIsKeptInACopy) {
    QByteArray bytes = bytesOf(bbm_);
    const qsizetype at = bytes.indexOf("</Layers>");
    ASSERT_GT(at, 0);
    bytes.insert(at, "<Layer type=\"hologram\" id=\"99\"><Name>Future</Name></Layer>\r\n");
    writeBytes(bbm_, bytes);
    ASSERT_TRUE(window_->openFile(bbm_));
    const QString card = cardText(*window_, QStringLiteral("open-warnings"));
    EXPECT_TRUE(card.contains(QStringLiteral("left out"))) << card.toStdString();
    EXPECT_TRUE(card.contains(QStringLiteral(".bak"))) << card.toStdString();
    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    bool saved = false;
    keep.during([&] { saved = invoke(*window_, "onSave"); });
    ASSERT_TRUE(saved);
    EXPECT_EQ(bytesOf(bbm_ + QStringLiteral(".bak")), bytes);
}

TEST_F(FileSafety, AControlCharacterDoesNotStopSaving) {
    ASSERT_TRUE(window_->openFile(bbm_));
    core::Map* map = window_->findChild<ui::MapView*>()->currentMap();
    map->author = QStringLiteral("Ann\vBob\x01");
    makeAnEdit(*window_);
    // The autosave goes through too (it used to fail without a word).
    ASSERT_TRUE(waitForAutosave(*window_));
    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    bool saved = false;
    keep.during([&] { saved = invoke(*window_, "onSave"); });
    ASSERT_TRUE(saved);
    EXPECT_EQ(keep.unexpected, 0);
    const auto back = saveload::readBbm(bbm_);
    ASSERT_TRUE(back.ok()) << back.error.toStdString();
    EXPECT_EQ(back.map->author, QStringLiteral("Ann\nBob"));
}

// Two copies of the app used to share one autosave: one's Save deleted the
// other's crash recovery.
TEST_F(FileSafety, EachWindowKeepsItsOwnAutosave) {
    const QString other = dir_.filePath(QStringLiteral("other.bbm"));
    ASSERT_TRUE(QFile::copy(bbm_, other));
    QFile(other).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    auto second = std::make_unique<ui::MainWindow>(parts_);
    ASSERT_TRUE(window_->openFile(bbm_));
    ASSERT_TRUE(second->openFile(other));
    makeAnEdit(*window_);
    makeAnEdit(*second);
    ASSERT_TRUE(waitForAutosave(*window_));
    ASSERT_TRUE(waitForAutosave(*second));
    EXPECT_NE(window_->autosavePath(), second->autosavePath());
    // Both are in use: neither is a leftover.
    EXPECT_TRUE(ui::AutosaveSlot::leftovers().isEmpty());

    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    keep.during([&] { invoke(*window_, "onSave"); });
    EXPECT_FALSE(QFile::exists(window_->autosavePath()));
    const QString secondsAutosave = second->autosavePath();
    EXPECT_TRUE(QFile::exists(secondsAutosave));

    // The second copy stops without saving (a crash): its work is left over.
    second.reset();
    const auto leftovers = ui::AutosaveSlot::leftovers();
    ASSERT_EQ(leftovers.size(), 1);
    EXPECT_EQ(leftovers.front().layoutPath, secondsAutosave);
    EXPECT_EQ(leftovers.front().sourceFile, other);
    EXPECT_EQ(leftovers.front().displayName(), QStringLiteral("other.bbm"));
}

// R5.AutosaveRestoreIsDirty, through the start-up list.
TEST_F(FileSafety, ARecoveredLayoutIsUnsaved) {
    ASSERT_TRUE(window_->openFile(bbm_));
    makeAnEdit(*window_);
    ASSERT_TRUE(waitForAutosave(*window_));
    const QString autosave = window_->autosavePath();
    window_.reset(); // a crash

    window_ = std::make_unique<ui::MainWindow>(parts_);
    QStringList listed;
    Answers recover{ { clickIn(QStringLiteral("Recover"), [&](QWidget* w) {
        EXPECT_EQ(w->objectName(), QStringLiteral("recoverDialog"));
        if (auto* list = w->findChild<QListWidget*>(QStringLiteral("recoverList")))
            for (int i = 0; i < list->count(); ++i) listed << list->item(i)->text();
    }) } };
    bool recovered = false;
    recover.during([&] { recovered = window_->offerRecovery(); });
    ASSERT_TRUE(recovered);
    ASSERT_EQ(listed.size(), 1);
    EXPECT_TRUE(listed.front().startsWith(QStringLiteral("corner.bbm — changes from Today")))
        << listed.front().toStdString();
    auto* view = window_->findChild<ui::MapView*>();
    EXPECT_FALSE(view->undoStack()->isClean());
    EXPECT_TRUE(window_->windowTitle().startsWith(QStringLiteral("corner.bbm *")))
        << window_->windowTitle().toStdString();
    EXPECT_TRUE(window_->statusBar()->currentMessage().contains(
        QStringLiteral("Recovered the unsaved changes to corner.bbm")));
    // It goes on autosaving to the same file, which is this window's now.
    EXPECT_EQ(window_->autosavePath(), autosave);
    EXPECT_TRUE(ui::AutosaveSlot::leftovers().isEmpty());

    // Closing asks; Discard lets the recovered changes go for good.
    Answers discard{ { click(QStringLiteral("Discard")) } };
    discard.during([&] { window_->close(); });
    EXPECT_EQ(discard.unexpected, 0);
    EXPECT_FALSE(QFile::exists(autosave));
    window_.reset();
    EXPECT_TRUE(ui::AutosaveSlot::leftovers().isEmpty());
}

TEST_F(FileSafety, EveryLeftoverIsOfferedAndDiscardTakesOnlyThatOne) {
    const QString other = dir_.filePath(QStringLiteral("other.bbm"));
    ASSERT_TRUE(QFile::copy(bbm_, other));
    for (const QString& file : { bbm_, other }) {
        ui::MainWindow w(parts_);
        ASSERT_TRUE(w.openFile(file));
        makeAnEdit(w);
        ASSERT_TRUE(waitForAutosave(w));
    } // both "crash"
    ASSERT_EQ(ui::AutosaveSlot::leftovers().size(), 2);
    ui::RecoverDialog::confirmDiscard = [](QWidget*, const QString&) { return true; };
    int rows = 0, after = 0;
    Answers answers{ { [&](QWidget* w) {
        auto* list = w->findChild<QListWidget*>(QStringLiteral("recoverList"));
        ASSERT_NE(list, nullptr);
        rows = list->count();
        list->setCurrentRow(0);
        clickIn(QStringLiteral("Discard"))(w);
        after = list->count();
        clickIn(QStringLiteral("Not now"))(w);
    } } };
    bool recovered = true;
    answers.during([&] { recovered = window_->offerRecovery(); });
    EXPECT_FALSE(recovered);
    EXPECT_EQ(rows, 2);
    EXPECT_EQ(after, 1);
    EXPECT_EQ(ui::AutosaveSlot::leftovers().size(), 1);
    // The other waits, and the card says where.
    ASSERT_TRUE(window_->notices()->isShown(QStringLiteral("recover-more")));
    EXPECT_TRUE(cardText(*window_, QStringLiteral("recover-more"))
                    .contains(QStringLiteral("File › Recover Unsaved Work")));
}

// The single autosave older versions kept is a leftover too.
TEST_F(FileSafety, TheOldAutosaveIsStillOffered) {
    auto map = saveload::readBbm(bbm_).map;
    ASSERT_TRUE(map);
    const QString legacy = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/autosave.bld-layout");
    QDir().mkpath(QFileInfo(legacy).absolutePath());
    QString error;
    ASSERT_TRUE(import::writeLayoutFile(*map, legacy, &error)) << error.toStdString();
    QSettings().setValue(QStringLiteral("autosave/sourceFile"), dir_.filePath(QStringLiteral("gone.bbm")));
    const auto leftovers = ui::AutosaveSlot::leftovers();
    ASSERT_EQ(leftovers.size(), 1);
    EXPECT_TRUE(leftovers.front().legacy());
    Answers recover{ { clickIn(QStringLiteral("Recover")) } };
    bool recovered = false;
    recover.during([&] { recovered = window_->offerRecovery(); });
    ASSERT_TRUE(recovered);
    EXPECT_TRUE(window_->windowTitle().startsWith(QStringLiteral("gone.bbm *")));
    // Moved into a file of this window's own.
    EXPECT_FALSE(QFile::exists(legacy));
    EXPECT_TRUE(QFile::exists(window_->autosavePath()));
}

TEST_F(FileSafety, TheFileMenuListsUnsavedWork) {
    QAction* recover = window_->findChild<QAction*>(QStringLiteral("file.recover"));
    ASSERT_NE(recover, nullptr);
    EXPECT_FALSE(recover->toolTip().isEmpty());
    QString intro;
    Answers answers{ { clickIn(QStringLiteral("Close"), [&](QWidget* w) {
        for (auto* l : w->findChildren<QLabel*>())
            if (l->wordWrap()) intro = l->text();
    }) } };
    answers.during([&] { recover->trigger(); });
    EXPECT_TRUE(intro.contains(QStringLiteral("no unsaved work"))) << intro.toStdString();
}

// R5.SaveFormatChoiceDoesNotCarryOver: "Keep .bbm" once let Save write the
// next file, a .tdl, in its lossy format without asking.
TEST_F(FileSafety, SaveAsksAboutTheFormatForEachFile) {
    ASSERT_TRUE(window_->openFile(bbm_));
    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    keep.during([&] { invoke(*window_, "onSave"); });
    const QString tdl = dir_.filePath(QStringLiteral("corner.tdl"));
    ASSERT_TRUE(
        QFile::copy(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/tight-corner.tdl"), tdl));
    QFile(tdl).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const QByteArray before = bytesOf(tdl);
    ASSERT_TRUE(window_->openFile(tdl));
    makeAnEdit(*window_);
    QString asked;
    Answers cancel{ { [&](QWidget* w) {
        if (auto* box = qobject_cast<QMessageBox*>(w)) asked = box->text();
        click(QStringLiteral("Cancel"))(w);
    } } };
    bool saved = true;
    cancel.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_FALSE(saved);
    EXPECT_TRUE(asked.contains(QStringLiteral("corner.tdl is a .tdl file"))) << asked.toStdString();
    // The real menu item, by its name.
    EXPECT_TRUE(asked.contains(QStringLiteral("File › Export as BlueBrick Map (.bbm)…")))
        << asked.toStdString();
    EXPECT_EQ(bytesOf(tdl), before);
}
