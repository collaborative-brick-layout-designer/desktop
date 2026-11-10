// File › Save / Save As / Export BlueBrick Map with the native layout file.

#include "ui/LibraryPathsDialog.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/PartDifferencesDialog.h"
#include "ui/PartsBrowser.h"
#include "ui/MapViewInternal.h"
#include "ui/SheetNames.h"
#include "ui/UpdateCheck.h"

#include "core/AnchoredLabel.h"
#include "core/DrawnLimits.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "rendering/SceneBuilder.h"
#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include "DialogAnswers.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QRegularExpression>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>

#include <functional>

using namespace bld;

namespace {

const QString kCorpus = QStringLiteral(BLD_BBM_CORPUS_DIR);

QAction* action(QWidget& w, const QString& text) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == text) return a;
    return nullptr;
}

class MainWindowFile : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        bbm_ = dir_.filePath(QStringLiteral("corner.bbm"));
        ASSERT_TRUE(QFile::copy(kCorpus + QStringLiteral("/tight-corner.bbm"), bbm_));
        window_ = std::make_unique<ui::MainWindow>(parts_);
    }
    void TearDown() override {
        window_.reset();
        QStandardPaths::setTestModeEnabled(false);
    }

    QTemporaryDir dir_;
    QString bbm_;
    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MainWindow> window_;
};

}  // namespace

TEST_F(MainWindowFile, AboutLinksToTheSourceOfBothApps) {
    // The About box blocks on most platforms but is a non-blocking sheet on
    // macOS: look for it among the window's children either way.
    QString text;
    const auto readAbout = [&] {
        for (auto* box : window_->findChildren<QMessageBox*>()) {
            if (!box->isVisible()) continue;
            text = box->text();
            box->accept();
        }
    };
    QTimer::singleShot(0, readAbout);
    QMetaObject::invokeMethod(window_.get(), "onAbout", Qt::DirectConnection);
    if (text.isEmpty()) {
        EXPECT_TRUE(QTest::qWaitFor([&] { readAbout(); return !text.isEmpty(); }, 2000));
    }
    EXPECT_TRUE(text.contains(QStringLiteral("https://github.com/collaborative-brick-layout-designer/desktop\"")));
    EXPECT_TRUE(text.contains(QStringLiteral("https://github.com/collaborative-brick-layout-designer/web\"")));
}

// A file opens on the same sheet as on the web: the one saved as selected
// when it takes parts, else the topmost parts sheet. Fordyce was saved with
// "rulers" selected, so both apps open it on "Tracks".
TEST_F(MainWindowFile, OpensOnTheSavedSheetOrTheTopmostPartsSheet) {
    const QString fordyce = kCorpus + QStringLiteral("/fordyce-2026.bbm");
    ASSERT_TRUE(window_->openFile(fordyce));
    const core::Map* map = window_->findChild<ui::MapView*>()->currentMap();
    ASSERT_GE(map->selectedLayerIndex, 0);
    EXPECT_EQ(map->layers()[map->selectedLayerIndex]->name, QStringLiteral("Tracks"));
    // A saved sheet that takes parts is kept.
    QFile f(fordyce);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray bytes = f.readAll();
    bytes.replace("<SelectedLayerIndex>4</SelectedLayerIndex>", "<SelectedLayerIndex>1</SelectedLayerIndex>");
    const QString copy = dir_.filePath(QStringLiteral("fordyce-tables.bbm"));
    QFile out(copy);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(bytes);
    out.close();
    ASSERT_TRUE(window_->openFile(copy));
    map = window_->findChild<ui::MapView*>()->currentMap();
    EXPECT_EQ(map->layers()[map->selectedLayerIndex]->name, QStringLiteral("Tables"));
}

// Two files are the same, else the first line that differs (not a byte dump).
static void expectSameLines(const QByteArray& got, const QByteArray& want) {
    if (got == want) return;
    const QList<QByteArray> a = got.split('\n'), b = want.split('\n');
    for (qsizetype i = 0; i < std::max(a.size(), b.size()); ++i) {
        const QByteArray x = i < a.size() ? a[i] : QByteArray("<end>"), y = i < b.size() ? b[i] : QByteArray("<end>");
        if (x != y) {
            ADD_FAILURE() << "line " << i + 1 << ": saved \"" << x.trimmed().toStdString() << "\", expected \""
                          << y.trimmed().toStdString() << "\" (" << a.size() << " vs " << b.size() << " lines)";
            return;
        }
    }
    ADD_FAILURE() << "the files differ only in line endings";
}

// Opening a file never changes it: Fordyce opens adding to "Tracks", but
// saving it untouched writes back its own saved sheet ("rulers") and its
// links, byte for byte. A sheet the user picks is saved.
TEST_F(MainWindowFile, OpeningAndSavingAnUntouchedFileChangesNothing) {
    QFile f(kCorpus + QStringLiteral("/fordyce-2026.bbm"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray original = f.readAll();
    ASSERT_TRUE(original.contains("<SelectedLayerIndex>4</SelectedLayerIndex>"));
    const QString copy = dir_.filePath(QStringLiteral("fordyce.bbm"));
    QFile out(copy);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(original);
    out.close();
    const auto readBack = [&] {
        QFile in(copy);
        return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    };
    // With the parts library, which knows the straights but not the
    // BrickTracks curves they join: those links are kept too.
    parts_.addSearchPath(QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
    parts_.scan();
    ASSERT_TRUE(parts_.metadata(QStringLiteral("2865.8")));
    ASSERT_TRUE(window_->openFile(copy));
    auto* view = window_->findChild<ui::MapView*>();
    const core::Map* map = view->currentMap();
    EXPECT_EQ(map->layers()[map->selectedLayerIndex]->name, QStringLiteral("Tracks"));
    // An empty edit, so the file counts as changed; the map is as opened.
    view->undoStack()->push(new QUndoCommand(QStringLiteral("nothing")));
    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    bool saved = false;
    keep.during([&] { saved = invoke(*window_, "onSave"); });
    ASSERT_TRUE(saved);
    expectSameLines(readBack(), original);

    // Pick "Tables" in the Sheets panel: that is saved.
    auto* list = window_->findChild<QListWidget*>(QStringLiteral("sheetList"));
    ASSERT_NE(list, nullptr);
    int tablesRow = -1;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == QStringLiteral("Tables")) tablesRow = i;
    ASSERT_GE(tablesRow, 0);
    list->setCurrentRow(tablesRow);
    int tables = -1;
    for (int i = 0; i < static_cast<int>(map->layers().size()); ++i)
        if (map->layers()[i]->name == QStringLiteral("Tables")) tables = i;
    ASSERT_EQ(map->selectedLayerIndex, tables);
    view->undoStack()->push(new QUndoCommand(QStringLiteral("nothing either")));
    Answers none;
    none.during([&] { saved = invoke(*window_, "onSave"); });
    ASSERT_TRUE(saved);
    QByteArray picked = original;
    picked.replace("<SelectedLayerIndex>4</SelectedLayerIndex>",
                   QStringLiteral("<SelectedLayerIndex>%1</SelectedLayerIndex>").arg(tables).toUtf8());
    expectSameLines(readBack(), picked);
}

// Every menu item that opens a window ("…") says in its tooltip what it
// does, not just its own name again ("Save As": "Save As").
TEST_F(MainWindowFile, MenuItemsThatOpenAWindowSayWhatTheyDo) {
    QStringList bare;
    std::function<void(QMenu*)> walk = [&](QMenu* menu) {
        for (QAction* a : menu->actions()) {
            if (a->menu()) { walk(a->menu()); continue; }
            QString label = a->text();
            label.remove(QRegularExpression(QStringLiteral("&(?!&)")));
            label.replace(QStringLiteral("&&"), QStringLiteral("&"));
            if (!label.endsWith(QChar(0x2026))) continue;
            label.chop(1);
            QString tip = a->toolTip();
            tip.remove(QChar(0x2026));
            if (tip.trimmed().isEmpty() || tip.trimmed() == label.trimmed()) bare << a->text();
        }
    };
    for (QAction* top : window_->menuBar()->actions())
        if (top->menu()) walk(top->menu());
    EXPECT_TRUE(bare.isEmpty()) << bare.join(QStringLiteral(", ")).toStdString();
}

// The Budget menu, all greyed out until there is a budget, starts by saying
// what a budget is.
TEST_F(MainWindowFile, BudgetMenuSaysWhatABudgetIs) {
    QAction* what = nullptr;
    for (QAction* a : window_->findChildren<QAction*>())
        if (a->objectName() == QStringLiteral("act.budget.what")) what = a;
    ASSERT_NE(what, nullptr);
    EXPECT_TRUE(what->isEnabled());
    EXPECT_FALSE(what->toolTip().isEmpty());
    // Triggering it opens a box that explains; close it straight away.
    QTimer::singleShot(0, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            EXPECT_TRUE(box->text().contains(QStringLiteral("how many of each part you own")));
            box->accept();
        }
    });
    what->trigger();
}

// The status bar names the opened file by its name, not its whole path
// (which was cut off and showed internal folders); the path is the tooltip.
TEST_F(MainWindowFile, StatusBarNamesTheFileWithThePathAsTooltip) {
    ASSERT_TRUE(window_->openFile(bbm_));
    QStatusBar* bar = window_->statusBar();
    const QString name = QFileInfo(bbm_).fileName();
    EXPECT_TRUE(bar->currentMessage().startsWith(QStringLiteral("Opened %1").arg(name))) << bar->currentMessage().toStdString();
    EXPECT_FALSE(bar->currentMessage().contains(QFileInfo(bbm_).absolutePath()));
    EXPECT_EQ(bar->toolTip(), QDir::toNativeSeparators(bbm_));
    bar->showMessage(QStringLiteral("Something else"));
    EXPECT_TRUE(bar->toolTip().isEmpty());
}

// BlueBrick's untouched "Layer 1" shows as "Sheet 1", but the map keeps
// "Layer 1": the .bbm written from the opened map names it "Layer 1", and a
// read-then-write of the file is byte for byte the file.
TEST_F(MainWindowFile, BlueBricksLayerOneShowsAsSheetOneAndTheBbmKeepsIt) {
    QFile original(bbm_);
    ASSERT_TRUE(original.open(QIODevice::ReadOnly));
    const QByteArray bytes = original.readAll();
    ASSERT_TRUE(bytes.contains("<Name>Layer 1</Name>"));
    ASSERT_TRUE(window_->openFile(bbm_));
    auto* list = window_->findChild<QListWidget*>(QStringLiteral("sheetList"));
    ASSERT_NE(list, nullptr);
    QStringList shown;
    for (int i = 0; i < list->count(); ++i) shown << list->item(i)->text();
    EXPECT_TRUE(shown.contains(QStringLiteral("Sheet 1"))) << shown.join(QStringLiteral(", ")).toStdString();
    EXPECT_FALSE(shown.contains(QStringLiteral("Layer 1")));
    const core::Map* map = window_->findChild<ui::MapView*>()->currentMap();
    const QString again = dir_.filePath(QStringLiteral("again.bbm"));
    ASSERT_TRUE(saveload::writeBbm(*map, again).ok);
    QFile b(again);
    ASSERT_TRUE(b.open(QIODevice::ReadOnly));
    const QByteArray written = b.readAll();
    EXPECT_TRUE(written.contains("<Name>Layer 1</Name>"));
    EXPECT_FALSE(written.contains("Sheet 1"));
    // The reader and writer alone (what export runs on) give the file back byte for byte.
    auto read = saveload::readBbm(bbm_);
    ASSERT_TRUE(read.ok());
    QBuffer buf;
    ASSERT_TRUE(buf.open(QIODevice::WriteOnly));
    ASSERT_TRUE(saveload::writeBbm(*read.map, buf).ok);
    EXPECT_TRUE(buf.data() == bytes) << "a read-then-write of the .bbm differs from the file";
}

TEST(SheetNames, OnlyTheUntouchedDefaultIsShownAsSheet) {
    EXPECT_EQ(ui::displaySheetName(QStringLiteral("Layer 1")), QStringLiteral("Sheet 1"));
    EXPECT_EQ(ui::displaySheetName(QStringLiteral("Layer 12")), QStringLiteral("Sheet 12"));
    EXPECT_EQ(ui::displaySheetName(QStringLiteral("Layer 1 old")), QStringLiteral("Layer 1 old"));
    EXPECT_EQ(ui::displaySheetName(QStringLiteral("layer 1")), QStringLiteral("layer 1"));
    EXPECT_EQ(ui::displaySheetName(QStringLiteral("Tracks")), QStringLiteral("Tracks"));
    // A rename box left at the shown default saves nothing.
    EXPECT_TRUE(ui::renamedSheet(QStringLiteral("Layer 1"), QStringLiteral("Sheet 1")).isEmpty());
    EXPECT_TRUE(ui::renamedSheet(QStringLiteral("Layer 1"), QStringLiteral(" Layer 1 ")).isEmpty());
    EXPECT_TRUE(ui::renamedSheet(QStringLiteral("Layer 1"), QString()).isEmpty());
    EXPECT_EQ(ui::renamedSheet(QStringLiteral("Layer 1"), QStringLiteral("Baseplates")), QStringLiteral("Baseplates"));
}

// An edit made right after another is autosaved within seconds too, not
// left for the minute tick.
TEST_F(MainWindowFile, AutosaveTakesInEveryEditOfABurst) {
    ASSERT_TRUE(window_->openFile(bbm_));
    QUndoStack* undo = window_->findChild<ui::MapView*>()->undoStack();
    const QString autosave = window_->autosavePath();
    QFile::remove(autosave);
    undo->push(new QUndoCommand(QStringLiteral("first")));
    // Whatever the first edit wrote is gone; the second must still reach disk.
    QTest::qWait(300);
    QFile::remove(autosave);
    undo->push(new QUndoCommand(QStringLiteral("second")));
    EXPECT_TRUE(QTest::qWaitFor([&] { return QFile::exists(autosave); }, 8000));
    QFile::remove(autosave);
}

TEST_F(MainWindowFile, SavingABbmOffersTheOneFileFormat) {
    ASSERT_TRUE(window_->openFile(bbm_));
    const QString native = dir_.filePath(QStringLiteral("corner.bld-layout"));
    Answers answers{ { click(QStringLiteral("Save as .bld-layout")) } };
    bool saved = false;
    answers.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_EQ(answers.unexpected, 0);
    ASSERT_TRUE(QFile::exists(native));
    EXPECT_FALSE(QFile::exists(saveload::sidecarPathFor(native)));
    EXPECT_TRUE(import::readLayoutFile(native, dir_.path()).ok());
    EXPECT_TRUE(window_->windowTitle().startsWith(QStringLiteral("corner.bld-layout")))
        << window_->windowTitle().toStdString();

    // Now it is one: Save asks nothing.
    Answers none;
    none.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_EQ(none.unexpected, 0);
}

TEST_F(MainWindowFile, KeepingTheBbmAsksOnlyOnce) {
    ASSERT_TRUE(window_->openFile(bbm_));
    QFile::remove(bbm_);
    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    bool saved = false;
    keep.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_TRUE(QFile::exists(bbm_));
    EXPECT_FALSE(QFile::exists(dir_.filePath(QStringLiteral("corner.bld-layout"))));

    Answers again;
    again.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_EQ(again.unexpected, 0);
}

TEST_F(MainWindowFile, CancelSavesNothing) {
    ASSERT_TRUE(window_->openFile(bbm_));
    QFile::remove(bbm_);
    Answers cancel{ { click(QStringLiteral("Cancel")) } };
    bool saved = true;
    cancel.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_FALSE(saved);
    EXPECT_FALSE(QFile::exists(bbm_));
    EXPECT_FALSE(QFile::exists(dir_.filePath(QStringLiteral("corner.bld-layout"))));
}

TEST_F(MainWindowFile, ExportingABbmLeavesOutWhatBlueBrickCannotHold) {
    // A layout with a venue in it.
    auto map = saveload::readBbm(bbm_).map;
    ASSERT_TRUE(map);
    map->sidecar.venue = saveload::readVenueFile(QStringLiteral(BLD_SOURCE_DIR "/fixtures/venues/grand-lobby.bld-venue"));
    ASSERT_TRUE(map->sidecar.venue);
    const QString native = dir_.filePath(QStringLiteral("lobby.bld-layout"));
    QString error;
    ASSERT_TRUE(import::writeLayoutFile(*map, native, &error)) << error.toStdString();
    ASSERT_TRUE(window_->openFile(native));

    const QString exported = dir_.filePath(QStringLiteral("for-bluebrick.bbm"));
    QAction* exportAct = action(*window_, QStringLiteral("Export as &BlueBrick Map (.bbm)…"));
    ASSERT_NE(exportAct, nullptr);
    Answers answers{ { chooseFile(exported), click(QStringLiteral("Yes")) } };
    answers.during([&] { exportAct->trigger(); });
    EXPECT_EQ(answers.unexpected, 0);
    ASSERT_TRUE(QFile::exists(exported));
    EXPECT_FALSE(QFile::exists(saveload::sidecarPathFor(exported)));
    auto back = saveload::readBbm(exported);
    ASSERT_TRUE(back.ok());
    EXPECT_FALSE(back.map->sidecar.venue.has_value());
    // The layout is still the .bld-layout, venue and all.
    EXPECT_TRUE(window_->windowTitle().startsWith(QStringLiteral("lobby.bld-layout")))
        << window_->windowTitle().toStdString();
}

// A layout file holding a label with a huge font (any writer may put any
// number there) opens, and the label draws at the largest size there is,
// not one without end.
TEST_F(MainWindowFile, ALabelWithAHugeFontOpensAndDrawsAtTheLimit) {
    auto map = saveload::readBbm(bbm_).map;
    ASSERT_TRUE(map);
    core::AnchoredLabel huge;
    huge.id = QStringLiteral("huge");
    huge.text = QStringLiteral("Big sign");
    huge.font.sizePt = 1e30f;
    huge.offset = QPointF(10, 10);
    map->sidecar.anchoredLabels.push_back(huge);
    const QString file = dir_.filePath(QStringLiteral("huge-label.bld-layout"));
    QString error;
    ASSERT_TRUE(import::writeLayoutFile(*map, file, &error)) << error.toStdString();
    ASSERT_TRUE(window_->openFile(file));

    auto* view = window_->findChild<ui::MapView*>();
    ASSERT_NE(view, nullptr);
    ASSERT_NE(view->currentMap(), nullptr);
    ASSERT_EQ(view->currentMap()->sidecar.anchoredLabels.size(), 1u);
    EXPECT_EQ(view->currentMap()->sidecar.anchoredLabels[0].font.sizePt, core::limits::kFontSize.max);
    const QGraphicsSimpleTextItem* drawn = nullptr;
    for (QGraphicsItem* it : view->scene()->items())
        if (ui::detail::isLabelItem(it)
            && it->data(ui::detail::kBrickDataGuid).toString() == QStringLiteral("huge"))
            drawn = dynamic_cast<const QGraphicsSimpleTextItem*>(it);
    ASSERT_NE(drawn, nullptr) << "the label is on the map";
    EXPECT_EQ(drawn->font().pointSize(), static_cast<int>(core::limits::kFontSize.max));
    EXPECT_EQ(drawn->text(), QStringLiteral("Big sign"));
}

namespace {

QByteArray partXml() {
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n\t<Author>Me</Author>\n"
                             "\t<Description>\n\t\t<en>My part</en>\n\t</Description>\n</part>\n");
}

QByteArray partGif() {
    QImage image(16, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QBuffer b;
    b.open(QIODevice::WriteOnly);
    image.save(&b, "PNG");
    return b.data();
}

// A layout of one MYPART.1 brick.
std::unique_ptr<core::Map> oneBrickOf(const QString& part) {
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    core::Brick b;
    b.partNumber = part;
    layer->bricks.push_back(b);
    map->layers().push_back(std::move(layer));
    return map;
}

QString layoutPartsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/layout-parts");
}

}  // namespace

TEST_F(MainWindowFile, SavingALayoutCarriesTheUsersOwnParts) {
    // A part of the user's own, outside the bundled library.
    const QString mine = dir_.filePath(QStringLiteral("mine"));
    QDir().mkpath(mine);
    QFile xml(mine + QStringLiteral("/MYPART.1.xml")), png(mine + QStringLiteral("/MYPART.1.png"));
    ASSERT_TRUE(xml.open(QIODevice::WriteOnly) && png.open(QIODevice::WriteOnly));
    xml.write(partXml());
    png.write(partGif());
    xml.close();
    png.close();
    parts_.addSearchPath(mine);
    parts_.scan();
    ASSERT_TRUE(parts_.metadata(QStringLiteral("MYPART.1")));

    QString error;
    const QString source = dir_.filePath(QStringLiteral("source.bld-layout"));
    ASSERT_TRUE(import::writeLayoutFile(*oneBrickOf(QStringLiteral("MYPART.1")), source, &error));
    ASSERT_TRUE(window_->openFile(source));
    const QString saved = dir_.filePath(QStringLiteral("saved.bld-layout"));
    QAction* saveAs = action(*window_, QStringLiteral("Save &As…"));
    ASSERT_NE(saveAs, nullptr);
    Answers answers{ { chooseFile(saved) } };
    answers.during([&] { saveAs->trigger(); });
    const auto read = import::readLayoutFile(saved, dir_.path());
    ASSERT_TRUE(read.ok());
    EXPECT_EQ(read.partFiles.keys(), (QStringList{ QStringLiteral("MYPART.1.png"), QStringLiteral("MYPART.1.xml") }));
    EXPECT_EQ(read.partFiles.value(QStringLiteral("MYPART.1.xml")), partXml());
}

TEST_F(MainWindowFile, OpeningALayoutAddsThePartsItCarries) {
    QDir(layoutPartsDir()).removeRecursively();
    ASSERT_FALSE(parts_.metadata(QStringLiteral("THEIRPART.1")));
    QString error;
    const QString file = dir_.filePath(QStringLiteral("theirs.bld-layout"));
    ASSERT_TRUE(import::writeLayoutFile(*oneBrickOf(QStringLiteral("THEIRPART.1")), file, &error, nullptr,
                                        { { QStringLiteral("THEIRPART.1.xml"), partXml() },
                                          { QStringLiteral("THEIRPART.1.png"), partGif() } }));
    ASSERT_TRUE(window_->openFile(file));
    const auto meta = parts_.metadata(QStringLiteral("THEIRPART.1"));
    ASSERT_TRUE(meta);
    EXPECT_TRUE(meta->xmlFilePath.startsWith(layoutPartsDir())) << meta->xmlFilePath.toStdString();
    EXPECT_FALSE(meta->gifFilePath.isEmpty());
    // The folder stays in the library after a restart.
    QSettings s;
    s.beginGroup(ui::LibraryPathsDialog::kSettingsGroup);
    QStringList paths = s.value(ui::LibraryPathsDialog::kSettingsKey).toStringList();
    EXPECT_TRUE(paths.contains(layoutPartsDir()));
    paths.removeAll(layoutPartsDir());
    s.setValue(ui::LibraryPathsDialog::kSettingsKey, paths);
    s.endGroup();
    QDir(layoutPartsDir()).removeRecursively();
}

namespace {

// Answers the part differences dialog: `choice` for every row, then Apply.
std::function<void(QWidget*)> chooseForEachPart(const QString& choice) {
    return [choice](QWidget* w) {
        auto* dialog = qobject_cast<ui::PartDifferencesDialog*>(w);
        ASSERT_NE(dialog, nullptr);
        for (auto* combo : dialog->findChildren<QComboBox*>()) combo->setCurrentText(choice);
        for (auto* b : dialog->findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Apply")) b->click();
    };
}

QByteArray theirXml() {
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n\t<Author>Someone else</Author>\n"
                             "\t<Description>\n\t\t<en>Their part</en>\n\t</Description>\n</part>\n");
}

QByteArray readBytes(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

// The user's own MYPART.1, and a layout carrying a different MYPART.1.
class MainWindowPartDifferences : public MainWindowFile {
protected:
    void SetUp() override {
        MainWindowFile::SetUp();
        QDir(layoutPartsDir()).removeRecursively();
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/replaced-parts"))
            .removeRecursively();
        mine_ = dir_.filePath(QStringLiteral("mine"));
        QDir().mkpath(mine_);
        QFile xml(mine_ + QStringLiteral("/MYPART.1.xml"));
        ASSERT_TRUE(xml.open(QIODevice::WriteOnly));
        xml.write(partXml());
        xml.close();
        parts_.addSearchPath(mine_);
        parts_.scan();
        ASSERT_TRUE(parts_.metadata(QStringLiteral("MYPART.1")));
        QString error;
        file_ = dir_.filePath(QStringLiteral("theirs.bld-layout"));
        ASSERT_TRUE(import::writeLayoutFile(*oneBrickOf(QStringLiteral("MYPART.1")), file_, &error, nullptr,
                                            { { QStringLiteral("MYPART.1.xml"), theirXml() },
                                              { QStringLiteral("MYPART.1.png"), partGif() } }));
    }
    void TearDown() override {
        QSettings s;
        s.beginGroup(ui::LibraryPathsDialog::kSettingsGroup);
        QStringList paths = s.value(ui::LibraryPathsDialog::kSettingsKey).toStringList();
        paths.removeAll(layoutPartsDir());
        s.setValue(ui::LibraryPathsDialog::kSettingsKey, paths);
        s.endGroup();
        QDir(layoutPartsDir()).removeRecursively();
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/replaced-parts"))
            .removeRecursively();
        MainWindowFile::TearDown();
    }
    QString brickPart() {
        const auto* map = window_->findChild<ui::MapView*>()->currentMap();
        return static_cast<const core::LayerBrick&>(*map->layers().front()).bricks.front().partNumber;
    }

    QString mine_, file_;
};

TEST_F(MainWindowPartDifferences, UsingTheLayoutsPartReplacesMine) {
    Answers answers{ { chooseForEachPart(QStringLiteral("Use the layout's")) } };
    bool opened = false;
    answers.during([&] { opened = window_->openFile(file_); });
    ASSERT_TRUE(opened);
    EXPECT_EQ(answers.unexpected, 0);
    const auto meta = parts_.metadata(QStringLiteral("MYPART.1"));
    ASSERT_TRUE(meta);
    EXPECT_EQ(meta->author, QStringLiteral("Someone else"));
    EXPECT_EQ(readBytes(mine_ + QStringLiteral("/MYPART.1.xml")), theirXml());
    EXPECT_FALSE(meta->gifFilePath.isEmpty());
    // The old one is in replaced-parts/<time>/.
    QDir backups(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/replaced-parts"));
    const QStringList stamps = backups.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    ASSERT_EQ(stamps.size(), 1);
    EXPECT_EQ(readBytes(backups.filePath(stamps.front() + QStringLiteral("/MYPART.1.xml"))), partXml());
    EXPECT_EQ(brickPart(), QStringLiteral("MYPART.1"));
}

TEST_F(MainWindowPartDifferences, KeepingBothSwitchesTheLayoutToTheNewNumber) {
    Answers answers{ { chooseForEachPart(QStringLiteral("Keep both")) } };
    answers.during([&] { ASSERT_TRUE(window_->openFile(file_)); });
    EXPECT_EQ(answers.unexpected, 0);
    EXPECT_EQ(parts_.metadata(QStringLiteral("MYPART.1"))->author, QStringLiteral("Me"));
    const auto added = parts_.metadata(QStringLiteral("MYPART-2.1"));
    ASSERT_TRUE(added);
    EXPECT_EQ(added->author, QStringLiteral("Someone else"));
    EXPECT_TRUE(added->xmlFilePath.startsWith(layoutPartsDir()));
    EXPECT_EQ(brickPart(), QStringLiteral("MYPART-2.1"));
}

// R5.KeepBothEveryOpen: each open made another -N copy, and the layout,
// now using the new number, opened looking saved.
TEST_F(MainWindowPartDifferences, KeepingBothIsAnUndoStepAndReusesTheCopy) {
    for (int open = 0; open < 2; ++open) {
        Answers answers{ { chooseForEachPart(QStringLiteral("Keep both")) } };
        answers.during([&] { ASSERT_TRUE(window_->openFile(file_)); });
        EXPECT_EQ(answers.unexpected, 0);
        EXPECT_EQ(brickPart(), QStringLiteral("MYPART-2.1"));
        // Not saved: the layout now differs from the file.
        auto* stack = window_->findChild<ui::MapView*>()->undoStack();
        EXPECT_FALSE(stack->isClean());
        EXPECT_TRUE(window_->windowTitle().contains(QLatin1Char('*')))
            << window_->windowTitle().toStdString();
        // The second open finds the copy the first one kept.
        EXPECT_FALSE(parts_.metadata(QStringLiteral("MYPART-3.1")));
        EXPECT_EQ(QDir(layoutPartsDir()).entryList({ QStringLiteral("MYPART-*.xml") }, QDir::Files).size(),
                  1);
        // Undo puts the layout's own number back.
        stack->undo();
        EXPECT_EQ(brickPart(), QStringLiteral("MYPART.1"));
        EXPECT_TRUE(stack->isClean());
        // Open again without being asked to save first.
        stack->setClean();
    }
}

TEST_F(MainWindowPartDifferences, KeepAllMineChangesNothing) {
    Answers answers{ { [](QWidget* w) {
        auto* dialog = qobject_cast<ui::PartDifferencesDialog*>(w);
        ASSERT_NE(dialog, nullptr);
        dialog->reject();
    } } };
    answers.during([&] { ASSERT_TRUE(window_->openFile(file_)); });
    EXPECT_EQ(parts_.metadata(QStringLiteral("MYPART.1"))->author, QStringLiteral("Me"));
    EXPECT_EQ(readBytes(mine_ + QStringLiteral("/MYPART.1.xml")), partXml());
    EXPECT_FALSE(parts_.metadata(QStringLiteral("MYPART-2.1")));
    EXPECT_EQ(brickPart(), QStringLiteral("MYPART.1"));
}

// The friendlier shell: plain panel names with the same header, task tabs
// that bring up their panels, and the status bar's piece count and sheet.
TEST_F(MainWindowFile, ShellHasTaskTabsSheetsAndThePieceCount) {
    ASSERT_TRUE(window_->openFile(bbm_));
    auto* sheets = window_->findChild<QDockWidget*>(QStringLiteral("dock.layers"));
    ASSERT_NE(sheets, nullptr);
    EXPECT_EQ(sheets->windowTitle(), QStringLiteral("Sheets"));
    EXPECT_NE(sheets->titleBarWidget(), nullptr);
    EXPECT_EQ(window_->findChild<QDockWidget*>(QStringLiteral("dock.venueLibrary"))->windowTitle(),
              QStringLiteral("Venue library"));

    auto* room = window_->findChild<QDockWidget*>(QStringLiteral("dock.venueLibrary"));
    room->hide();
    auto* roomTab = window_->findChild<QToolButton*>(QStringLiteral("task.room"));
    ASSERT_NE(roomTab, nullptr);
    EXPECT_EQ(roomTab->text(), QStringLiteral("Venue"));
    roomTab->click();
    EXPECT_FALSE(room->isHidden());
    EXPECT_TRUE(roomTab->isChecked());
    for (const char* id : { "task.build", "task.notes", "task.parts" })
        EXPECT_NE(window_->findChild<QToolButton*>(QString::fromLatin1(id)), nullptr) << id;

    QCoreApplication::processEvents();
    auto* pieces = window_->findChild<QLabel*>(QStringLiteral("PiecesLabel"));
    ASSERT_NE(pieces, nullptr);
    EXPECT_TRUE(pieces->text().endsWith(QStringLiteral("pieces"))) << pieces->text().toStdString();
    EXPECT_NE(window_->findChild<QLabel*>(QStringLiteral("ZoomLabel")), nullptr);
    // Everything the old toolbar did is still in the menus.
    EXPECT_NE(action(*window_, QStringLiteral("&Settings…")), nullptr);
    // One Settings window: Preferences joined it.
    EXPECT_EQ(action(*window_, QStringLiteral("Prefere&nces…")), nullptr);
    EXPECT_NE(action(*window_, QStringLiteral("Cu&t")), nullptr);
    EXPECT_NE(action(*window_, QStringLiteral("Send to &Back")), nullptr);
}

// The status bar's piece count is every part on every sheet (as the web
// counts), and it follows an open, a delete, an undo and a remote edit. It
// used to stay at "0 pieces" after opening a file.
TEST_F(MainWindowFile, ThePieceCountFollowsOpenEditUndoAndRemoteEdits) {
    auto* pieces = window_->findChild<QLabel*>(QStringLiteral("PiecesLabel"));
    auto* sheet = window_->findChild<QLabel*>(QStringLiteral("SheetLabel"));
    ASSERT_NE(pieces, nullptr);
    ASSERT_NE(sheet, nullptr);
    auto* view = window_->findChild<ui::MapView*>();
    ASSERT_NE(view, nullptr);
    const auto bricks = [&] {
        int n = 0;
        for (const auto& L : view->currentMap()->layers())
            if (L && L->kind() == core::LayerKind::Brick)
                n += static_cast<int>(static_cast<const core::LayerBrick&>(*L).bricks.size());
        return n;
    };

    ASSERT_TRUE(window_->openFile(bbm_));
    QCoreApplication::processEvents();
    EXPECT_EQ(pieces->text(), QStringLiteral("304 pieces"));
    EXPECT_TRUE(sheet->text().startsWith(QStringLiteral("Sheet: "))) << sheet->text().toStdString();

    // Delete what the current sheet holds, then undo it.
    view->selectAll();
    view->deleteSelected();
    QCoreApplication::processEvents();
    const int left = bricks();
    ASSERT_LT(left, 304);
    EXPECT_EQ(pieces->text(), QStringLiteral("%1 pieces").arg(left));
    view->undoStack()->undo();
    QCoreApplication::processEvents();
    EXPECT_EQ(pieces->text(), QStringLiteral("304 pieces"));

    // A remote edit takes one part away.
    auto remote = saveload::readBbm(bbm_).map;
    ASSERT_TRUE(remote);
    for (auto& L : remote->layers())
        if (L && L->kind() == core::LayerKind::Brick) {
            auto& list = static_cast<core::LayerBrick&>(*L).bricks;
            if (list.empty()) continue;
            list.pop_back();
            break;
        }
    view->updateMap(std::move(remote));
    QCoreApplication::processEvents();
    EXPECT_EQ(pieces->text(), QStringLiteral("303 pieces"));
}

// Opening a layout and Fit show the grid's column letters and row numbers
// whole: column A and row 1 sit in the grid's first row and column, above
// and left of the parts, and used to be cut off at the top of the map.
TEST_F(MainWindowFile, OpeningAndFitKeepTheGridLettersOnScreen) {
    window_->resize(1440, 900);
    window_->show();
    QCoreApplication::processEvents();
    ASSERT_TRUE(window_->openFile(bbm_));
    QCoreApplication::processEvents();
    auto* view = window_->findChild<ui::MapView*>();
    ASSERT_NE(view, nullptr);
    const core::LayerGrid* grid = nullptr;
    for (const auto& L : view->currentMap()->layers())
        if (L && L->kind() == core::LayerKind::Grid) grid = static_cast<const core::LayerGrid*>(L.get());
    ASSERT_NE(grid, nullptr);
    ASSERT_TRUE(grid->displayCellIndex);
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    const double cell = grid->gridSizeInStud * px;
    const double glyph = grid->cellIndexFont.sizePt * 4.0 / 3.0 * px;  // as drawCellIndices sizes it
    const QPoint c = grid->cellIndexCorner;
    // "A" and "1": centred in their cells, a glyph high.
    const auto glyphAt = [&](int col, int row) {
        const QPointF centre((col + 0.5) * cell, (row + 0.5) * cell);
        return QRectF(centre - QPointF(glyph / 2, glyph / 2), QSizeF(glyph, glyph));
    };
    const auto shown = [&] { return view->mapToScene(view->viewport()->rect()).boundingRect(); };
    const auto expectLabelsShown = [&] {
        EXPECT_TRUE(shown().contains(glyphAt(c.x() + 1, c.y()))) << "column A";
        EXPECT_TRUE(shown().contains(glyphAt(c.x(), c.y() + 1))) << "row 1";
    };
    expectLabelsShown();

    view->scale(3, 3);
    view->centerOn(view->scene()->itemsBoundingRect().center());
    QCoreApplication::processEvents();
    EXPECT_FALSE(shown().contains(glyphAt(c.x() + 1, c.y())));
    QMetaObject::invokeMethod(window_.get(), "onFitToView", Qt::DirectConnection);
    QCoreApplication::processEvents();
    expectLabelsShown();
}

// BlueBrick's Use as Model: right-click a part on the map and choose Use as
// Model; the parts list shows and picks that part.
TEST(MainWindowUseAsModel, RightClickAPartToPickItInThePartsList) {
    const QString root = QStringLiteral(BLD_PARTS_LIBRARY_ROOT);
    if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    lib.addSearchPath(root);
    lib.scan();
    QTemporaryDir dir;
    const QString bbm = dir.filePath(QStringLiteral("corner.bbm"));
    ASSERT_TRUE(QFile::copy(kCorpus + QStringLiteral("/tight-corner.bbm"), bbm));
    ui::MainWindow window(lib);
    window.resize(1400, 900);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.openFile(bbm));
    auto* view = window.findChild<ui::MapView*>();
    auto* browser = window.findChild<ui::PartsBrowser*>();
    ASSERT_TRUE(view && browser);
    browser->showPart(QStringLiteral("nothing-matches-this"));

    // A part on screen, under the pointer.
    QGraphicsItem* part = nullptr;
    QPoint at;
    for (QGraphicsItem* it : view->scene()->items()) {
        if (!ui::detail::isBrickItem(it)) continue;
        const QPoint p = view->mapFromScene(it->sceneBoundingRect().center());
        if (view->viewport()->rect().contains(p) && view->itemAt(p) == it) {
            part = it;
            at = p;
            break;
        }
    }
    ASSERT_TRUE(part) << "no part on screen";
    const QString partNumber = part->data(12).toString();  // SceneBuilder::kBrickPartRole

    bool chosen = false;
    QTimer closer;
    closer.setInterval(20);
    QObject::connect(&closer, &QTimer::timeout, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            closer.stop();
            for (QAction* a : menu->actions())
                if (a->text() == QStringLiteral("Use as Model")) {
                    a->trigger();
                    chosen = true;
                }
            menu->close();
        }
    });
    closer.start();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, at);
    QContextMenuEvent ce(QContextMenuEvent::Mouse, at, view->viewport()->mapToGlobal(at));
    QApplication::sendEvent(view->viewport(), &ce);
    ASSERT_TRUE(chosen);
    ASSERT_TRUE(browser->grid()->currentItem());
    EXPECT_EQ(browser->grid()->currentItem()->data(Qt::UserRole + 1).toString().toLower(), partNumber.toLower());
    EXPECT_FALSE(browser->grid()->currentItem()->isHidden());
    EXPECT_TRUE(browser->isVisible());
    QStandardPaths::setTestModeEnabled(false);
}

TEST(PartsBrowserShowPart, PicksThePartItself) {
    const QString root = QStringLiteral(BLD_PARTS_LIBRARY_ROOT);
    if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    parts::PartsLibrary lib;
    lib.addSearchPath(root);
    lib.scan();
    ui::PartsBrowser browser(lib);
    browser.rebuild();
    browser.showPart(QStringLiteral("2865.8"));
    ASSERT_TRUE(browser.grid()->currentItem());
    EXPECT_EQ(browser.grid()->currentItem()->data(Qt::UserRole + 1).toString(), QStringLiteral("2865.8"));
}
