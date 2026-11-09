// Parts folders, Download Center packs and imported parts (2026-11-09
// review, area 5):
//   - removing a folder in Parts Folders… takes effect at once;
//   - the Download Center checks every parts folder (Included, Update
//     x → y), installs into your own parts folder, streams with a size cap,
//     can be cancelled, installs a whole package or nothing, and stays open;
//   - a hi-res import's connection points sit where its low-res twin's do;
//   - the import preview's warnings are grouped and say what to do;
//   - Studio's own LDraw library and ldraw.xml are found.

#include "../sync/FakeHttp.h"
#include "DialogAnswers.h"

#include "ui/DownloadCenterDialog.h"
#include "ui/ImportPipeline.h"
#include "ui/LibraryPathsDialog.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/PreferencesDialog.h"
#include "ui/UpdateCheck.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "import/zip/ZipWriter.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace bld;

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray partXml(const QString& author) {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n\t<Author>" + author.toUtf8()
           + "</Author>\n</part>\n";
}

QByteArray png(QSize size = QSize(16, 8)) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer b(&bytes);
    b.open(QIODevice::WriteOnly);
    image.save(&b, "PNG");
    return bytes;
}

void writePart(const QString& dir, const QString& stem, const QString& author) {
    writeFile(dir + QLatin1Char('/') + stem + QStringLiteral(".xml"), partXml(author));
    writeFile(dir + QLatin1Char('/') + stem + QStringLiteral(".png"), png());
}

// A Download Center package: <name>/config/About.txt and one part.
QByteArray packageZip(const QString& name, const QString& version, const QString& part) {
    import::ZipWriter zip;
    zip.add(name + QStringLiteral("/config/About.txt"), "version=" + version.toUtf8() + "\n");
    zip.add(name + QLatin1Char('/') + part + QStringLiteral(".xml"),
            partXml(name + QLatin1Char(' ') + version));
    zip.add(name + QLatin1Char('/') + part + QStringLiteral(".png"), png());
    return zip.finish();
}

QByteArray listing(const QStringList& zips) {
    QByteArray html = "<html><body>";
    for (const QString& z : zips) html += "<a href=\"" + z.toUtf8() + "\">" + z.toUtf8() + "</a>\n";
    return html + "</body></html>";
}

QListWidgetItem* row(QListWidget* list, const QString& startsWith) {
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text().startsWith(startsWith)) return list->item(i);
    return nullptr;
}

QPushButton* button(QWidget& w, const QString& text) {
    for (auto* b : w.findChildren<QPushButton*>())
        if (QString(b->text())
                .replace(QStringLiteral("&&"), QStringLiteral("\x01"))
                .remove(QLatin1Char('&'))
                .replace(QLatin1Char('\x01'), QLatin1Char('&'))
            == text)
            return b;
    return nullptr;
}

// The Download Center pointed at a fake package site, every parts folder
// in `folders`, installing into install_.
class DownloadCenter : public ::testing::Test {
protected:
    void SetUp() override {
        install_ = root_.filePath(QStringLiteral("parts"));
        bundled_ = root_.filePath(QStringLiteral("bundled"));
        // The app came with Town 1.2 and Castle 1.1.
        writeFile(bundled_ + QStringLiteral("/Town/config/About.txt"), "version=1.2\n");
        writePart(bundled_ + QStringLiteral("/Town"), QStringLiteral("HOUSE.1"), QStringLiteral("Town 1.2"));
        writeFile(bundled_ + QStringLiteral("/Castle/config/About.txt"), "version=1.1\n");
        http_.replyRaw("/packs/", 200,
                       listing({ QStringLiteral("Town.1.3.zip"), QStringLiteral("Castle.1.1.zip"),
                                 QStringLiteral("Space.1.0.zip"), QStringLiteral("Broken.1.0.zip") }),
                       "text/html");
        http_.replyRaw("/packs/Town.1.3.zip", 200,
                       packageZip(QStringLiteral("Town"), QStringLiteral("1.3"), QStringLiteral("HOUSE.1")),
                       "application/zip");
        http_.replyRaw("/packs/Space.1.0.zip", 200,
                       packageZip(QStringLiteral("Space"), QStringLiteral("1.0"), QStringLiteral("ROCKET.1")),
                       "application/zip");
        // A package with an entry that would land outside the parts folder.
        import::ZipWriter bad;
        bad.add(QStringLiteral("Broken/config/About.txt"), "version=1.0\n");
        bad.add(QStringLiteral("Broken/BAD.1.xml"), partXml(QStringLiteral("bad")));
        bad.add(QStringLiteral("../outside.txt"), "x");
        http_.replyRaw("/packs/Broken.1.0.zip", 200, bad.finish(), "application/zip");
        dialog_ = std::make_unique<ui::DownloadCenterDialog>(install_, QStringList{ bundled_ });
        dialog_->findChild<QCheckBox*>()->setChecked(false); // not the real site
        const auto boxes = dialog_->findChildren<QCheckBox*>();
        for (auto* b : boxes) b->setChecked(b->text().startsWith(QStringLiteral("Unofficial")));
        dialog_->findChild<QLineEdit*>()->setText(http_.base().toString() + QStringLiteral("/packs/"));
        dialog_->show();
        list_ = dialog_->findChild<QListWidget*>();
    }
    void search() { button(*dialog_, QStringLiteral("Search"))->click(); }
    void tick(const QString& name) {
        auto* r = row(list_, name);
        ASSERT_NE(r, nullptr) << name.toStdString();
        r->setCheckState(Qt::Checked);
    }
    QString status() const {
        QStringList text;
        for (auto* l : dialog_->findChildren<QLabel*>()) text << l->text();
        return text.join(QLatin1Char('\n'));
    }
    bool leftovers() const {
        return !QDir(install_)
                    .entryList({ QStringLiteral(".download-*") },
                               QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                    .isEmpty();
    }

    QTemporaryDir root_;
    QString install_, bundled_;
    synctest::FakeHttp http_;
    std::unique_ptr<ui::DownloadCenterDialog> dialog_;
    QListWidget* list_ = nullptr;
};

} // namespace

// R5.BundledOfferedAsNew: only the install folder was checked, so packs the
// app came with were offered again, and a newer one wasn't called an update.
TEST_F(DownloadCenter, SaysWhatYouAlreadyHaveAndWhatIsAnUpdate) {
    search();
    ASSERT_EQ(list_->count(), 4);
    auto* castle = row(list_, QStringLiteral("Castle"));
    ASSERT_NE(castle, nullptr);
    EXPECT_EQ(castle->text(), QStringLiteral("Castle — Included"));
    EXPECT_FALSE(castle->flags() & Qt::ItemIsUserCheckable);
    auto* town = row(list_, QStringLiteral("Town"));
    ASSERT_NE(town, nullptr);
    EXPECT_EQ(town->text(), QStringLiteral("Town — Update 1.2 → 1.3"));
    EXPECT_TRUE(town->flags() & Qt::ItemIsUserCheckable);
    EXPECT_EQ(row(list_, QStringLiteral("Space"))->text(), QStringLiteral("Space (version 1.0)"));
    EXPECT_TRUE(status().contains(QStringLiteral("2 new, 1 update(s), 1 already included")))
        << status().toStdString();
}

// R5.DownloadCenterCloses: it closed right after installing.
TEST_F(DownloadCenter, InstallsAndStaysOpen) {
    search();
    tick(QStringLiteral("Town"));
    tick(QStringLiteral("Space"));
    button(*dialog_, QStringLiteral("Download & Install"))->click();
    EXPECT_TRUE(dialog_->isVisible());
    EXPECT_EQ(dialog_->installedCount(), 2);
    EXPECT_TRUE(QFile::exists(install_ + QStringLiteral("/Town/HOUSE.1.xml")));
    EXPECT_TRUE(QFile::exists(install_ + QStringLiteral("/Space/ROCKET.1.xml")));
    EXPECT_EQ(row(list_, QStringLiteral("Space"))->text(), QStringLiteral("Space (version 1.0) — Installed"));
    EXPECT_FALSE(leftovers());
    // The newer Town now wins over the bundled one.
    parts::PartsLibrary lib;
    lib.setSearchPaths({ bundled_, install_ });
    lib.scan();
    EXPECT_EQ(lib.metadata(QStringLiteral("HOUSE.1"))->author, QStringLiteral("Town 1.3"));
}

// R5.HalfUnpacked: a bad entry left the entries before it in the library.
TEST_F(DownloadCenter, ABadPackageInstallsNothing) {
    search();
    tick(QStringLiteral("Broken"));
    QString said;
    Answers answers{ { [&](QWidget* w) {
        said = qobject_cast<QMessageBox*>(w)->text();
        click(QStringLiteral("OK"))(w);
    } } };
    answers.during([&] { button(*dialog_, QStringLiteral("Download & Install"))->click(); });
    EXPECT_TRUE(said.contains(QStringLiteral("nothing was installed"))) << said.toStdString();
    EXPECT_FALSE(QFileInfo::exists(install_ + QStringLiteral("/Broken")));
    EXPECT_FALSE(QFileInfo::exists(root_.filePath(QStringLiteral("outside.txt"))));
    EXPECT_FALSE(leftovers());
    EXPECT_EQ(dialog_->installedCount(), 0);
}

// R5.ReadWholeFirst: the whole package was read before its size was checked.
TEST_F(DownloadCenter, StopsAPackageThatIsTooBig) {
    dialog_->setMaxPackageBytes(64);
    search();
    tick(QStringLiteral("Space"));
    QString said;
    Answers answers{ { [&](QWidget* w) {
        said = qobject_cast<QMessageBox*>(w)->text();
        click(QStringLiteral("OK"))(w);
    } } };
    answers.during([&] { button(*dialog_, QStringLiteral("Download & Install"))->click(); });
    EXPECT_TRUE(said.contains(QStringLiteral("too big"))) << said.toStdString();
    EXPECT_FALSE(QFileInfo::exists(install_ + QStringLiteral("/Space")));
}

// R5.NoCancel: a download couldn't be stopped.
TEST_F(DownloadCenter, CancelStopsTheDownload) {
    search();
    tick(QStringLiteral("Space"));
    http_.hold.insert("/packs/Space.1.0.zip");
    QTimer::singleShot(300, dialog_.get(), [this] {
        auto* cancel = button(*dialog_, QStringLiteral("Cancel Download"));
        ASSERT_NE(cancel, nullptr);
        EXPECT_TRUE(cancel->isVisible());
        cancel->click();
    });
    button(*dialog_, QStringLiteral("Download & Install"))->click();
    EXPECT_EQ(dialog_->installedCount(), 0);
    EXPECT_FALSE(QFileInfo::exists(install_ + QStringLiteral("/Space")));
    EXPECT_TRUE(status().contains(QStringLiteral("cancelled"))) << status().toStdString();
    EXPECT_FALSE(button(*dialog_, QStringLiteral("Cancel Download"))->isVisible());
    EXPECT_TRUE(dialog_->isVisible());
}

TEST_F(DownloadCenter, AStalledDownloadStops) {
    dialog_->setStallTimeoutMs(500);
    search();
    tick(QStringLiteral("Space"));
    http_.hold.insert("/packs/Space.1.0.zip");
    QString said;
    Answers answers{ { [&](QWidget* w) {
        said = qobject_cast<QMessageBox*>(w)->text();
        click(QStringLiteral("OK"))(w);
    } } };
    answers.during([&] { button(*dialog_, QStringLiteral("Download & Install"))->click(); });
    EXPECT_TRUE(said.contains(QStringLiteral("stopped"))) << said.toStdString();
    EXPECT_EQ(dialog_->installedCount(), 0);
}

namespace {

class PartsFolders : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        folder_ = dir_.filePath(QStringLiteral("friend"));
        writePart(folder_, QStringLiteral("FRIENDPART.1"), QStringLiteral("friend"));
        setUserPaths({ folder_ });
        window_ = std::make_unique<ui::MainWindow>(parts_);
    }
    void TearDown() override {
        window_.reset();
        setUserPaths({});
        QStandardPaths::setTestModeEnabled(false);
    }
    static void setUserPaths(const QStringList& paths) {
        QSettings s;
        s.beginGroup(QLatin1String(ui::LibraryPathsDialog::kSettingsGroup));
        s.setValue(QLatin1String(ui::LibraryPathsDialog::kSettingsKey), paths);
        s.endGroup();
    }
    QTemporaryDir dir_;
    QString folder_;
    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MainWindow> window_;
};

} // namespace

// R5.RemovedFolderStillScanned: its parts stayed until a restart.
TEST_F(PartsFolders, RemovingAFolderTakesItsPartsAwayAtOnce) {
    ASSERT_TRUE(parts_.metadata(QStringLiteral("FRIENDPART.1")));
    Answers answers{ { [&](QWidget* w) {
        auto* dialog = qobject_cast<ui::LibraryPathsDialog*>(w);
        ASSERT_NE(dialog, nullptr);
        auto* list = dialog->findChild<QListWidget*>();
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text().contains(QStringLiteral("friend"))) list->setCurrentRow(i);
        clickIn(QStringLiteral("Remove"))(w);
        clickIn(QStringLiteral("OK"))(w);
    } } };
    answers.during([&] { window_->findChild<QAction*>(QStringLiteral("act.parts.folders"))->trigger(); });
    EXPECT_EQ(answers.unexpected, 0);
    EXPECT_FALSE(parts_.metadata(QStringLiteral("FRIENDPART.1")));
    EXPECT_FALSE(parts_.searchPaths().contains(folder_));
}

// R5.WrongInstallFolder: packs went into the first parts folder listed,
// often a server's, which labelled every part as that server's.
TEST_F(PartsFolders, GetMorePartsInstallsIntoYourOwnPartsFolder) {
    const QString server = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/server-parts/club.example");
    QDir().mkpath(server);
    setUserPaths({ server, folder_ });
    QString root;
    Answers answers{ { [&](QWidget* w) {
        auto* dialog = qobject_cast<ui::DownloadCenterDialog*>(w);
        ASSERT_NE(dialog, nullptr);
        root = dialog->libraryRoot();
        dialog->reject();
    } } };
    answers.during([&] { window_->findChild<QAction*>(QStringLiteral("act.parts.getMore"))->trigger(); });
    EXPECT_EQ(QDir::cleanPath(root),
              QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                              + QStringLiteral("/parts")));
}

// R5.HiResDots: a 32 px/stud import drew its connection points at a quarter
// of the distance (and size) from its centre.
TEST(HiResParts, ConnectionPointsSitWhereTheLowResTwinsDo) {
    const QString vendored = QStringLiteral(BLD_PARTS_LIBRARY_ROOT) + QStringLiteral("/Track");
    if (!QFile::exists(vendored + QStringLiteral("/9VQuarterStraight.8.xml")))
        GTEST_SKIP() << "no parts library";
    QTemporaryDir dir;
    QFile xml(vendored + QStringLiteral("/9VQuarterStraight.8.xml"));
    ASSERT_TRUE(xml.open(QIODevice::ReadOnly));
    QByteArray hiXml = xml.readAll();
    hiXml.replace("<part>", "<part>\n\t<PixelsPerStud>32</PixelsPerStud>");
    writeFile(dir.filePath(QStringLiteral("HIRES.8.xml")), hiXml);
    const QImage low(vendored + QStringLiteral("/9VQuarterStraight.8.gif"));
    ASSERT_FALSE(low.isNull());
    ASSERT_TRUE(low.scaled(low.size() * 4).save(dir.filePath(QStringLiteral("HIRES.8.png"))));
    parts::PartsLibrary lib;
    lib.setSearchPaths({ vendored, dir.path() });
    lib.scan();
    ASSERT_EQ(lib.metadata(QStringLiteral("HIRES.8"))->pxPerStud, 32);
    ASSERT_EQ(lib.metadata(QStringLiteral("HIRES.8"))->connections.size(),
              lib.metadata(QStringLiteral("9VQUARTERSTRAIGHT.8"))->connections.size());

    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    double x = 0;
    for (const char* part : { "9VQUARTERSTRAIGHT.8", "HIRES.8" }) {
        core::Brick b;
        // Far apart: ends that meet are linked, and only free ends show dots.
        b.displayArea = QRectF(x, 0, 8, 8);
        x += 100;
        b.guid = QString::fromLatin1(part);
        b.partNumber = QString::fromLatin1(part);
        layer->bricks.push_back(b);
    }
    map->layers().push_back(std::move(layer));
    ui::MapView view(lib);
    view.resize(800, 600);
    view.show();
    (void)QTest::qWaitForWindowExposed(&view);
    view.loadMap(std::move(map));
    QApplication::processEvents();
    ASSERT_GE(view.scene()->items().size(), 2);
    // Each part's dots, from its centre, in the scene.
    QHash<QString, QList<QPointF>> dots;
    QHash<QString, double> radius;
    for (QGraphicsItem* item : view.scene()->items()) {
        if (!ui::detail::isBrickItem(item)) continue;
        const QString guid = item->data(ui::detail::kBrickDataGuid).toString();
        for (QGraphicsItem* child : item->childItems()) {
            if (child->data(ui::detail::kBrickDataKind).toString() != QLatin1String("connDot")) continue;
            auto* dot = qgraphicsitem_cast<QGraphicsEllipseItem*>(child);
            ASSERT_NE(dot, nullptr);
            const QRectF r = dot->mapRectToScene(dot->rect()); // without the (cosmetic) pen
            dots[guid] << r.center() - item->scenePos();
            radius[guid] = r.width();
        }
    }
    ASSERT_EQ(dots.value(QStringLiteral("HIRES.8")).size(), 2)
        << QStringList(dots.keys()).join(QLatin1Char(',')).toStdString();
    ASSERT_EQ(dots.value(QStringLiteral("9VQUARTERSTRAIGHT.8")).size(), 2);
    for (int i = 0; i < 2; ++i) {
        const QPointF a = dots[QStringLiteral("9VQUARTERSTRAIGHT.8")][i],
                      b = dots[QStringLiteral("HIRES.8")][i];
        EXPECT_NEAR(a.x(), b.x(), 1.0) << i;
        EXPECT_NEAR(a.y(), b.y(), 1.0) << i;
    }
    EXPECT_NEAR(radius[QStringLiteral("HIRES.8")], radius[QStringLiteral("9VQUARTERSTRAIGHT.8")], 1.0);
}

// R5.HiResHull: the hull divided a 32 px/stud sprite by 8.
TEST(HiResParts, AHullIsInStuds) {
    QTemporaryDir dir;
    const auto part = [&](const QString& stem, int pxPerStud, QSize size) {
        QByteArray xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n";
        if (pxPerStud > 0) xml += "\t<PixelsPerStud>" + QByteArray::number(pxPerStud) + "</PixelsPerStud>\n";
        writeFile(dir.filePath(stem + QStringLiteral(".xml")), xml + "</part>\n");
        writeFile(dir.filePath(stem + QStringLiteral(".png")), png(size));
    };
    part(QStringLiteral("HIRES.1"), 32, QSize(128, 64)); // 4 x 2 studs
    part(QStringLiteral("LORES.1"), 0, QSize(32, 16));   // 4 x 2 studs
    parts::PartsLibrary lib;
    lib.addSearchPath(dir.path());
    lib.scan();
    for (const char* key : { "HIRES.1", "LORES.1" }) {
        const QRectF box = lib.hullPolygonStuds(QString::fromLatin1(key)).boundingRect();
        EXPECT_NEAR(box.width(), 4.0, 0.3) << key;
        EXPECT_NEAR(box.height(), 2.0, 0.3) << key;
    }
}

// R5.RawImportWarnings: "Unresolved part 98560.dat" once per brick, in English.
TEST(ImportWarnings, MissingPartsAreOneLineThatSaysWhatToDo) {
    QStringList raw;
    for (int i = 0; i < 12; ++i) raw << QStringLiteral("Unresolved part 98560.dat");
    raw << QStringLiteral("Unresolved subfile 3001.dat") << QStringLiteral("Could not open /tmp/x.ldr")
        << QStringLiteral("Could not open /tmp/x.ldr");
    const QStringList out = ui::friendlyImportWarnings(raw, false);
    ASSERT_EQ(out.size(), 3) << out.join(QLatin1Char('|')).toStdString();
    EXPECT_EQ(out[0],
              QStringLiteral("2 part(s) aren’t in your LDraw library, so they’re left out of the picture: "
                             "98560.dat (12 times), 3001.dat."));
    EXPECT_TRUE(out[1].contains(QStringLiteral("Preferences › Import")));
    EXPECT_EQ(out[2], QStringLiteral("Couldn’t open /tmp/x.ldr. (2 times)"));
    // From LDD: the ldraw.xml setting.
    EXPECT_TRUE(ui::friendlyImportWarnings({ QStringLiteral("Unresolved part 1.dat") }, true)[1].contains(
        QStringLiteral("ldraw.xml")));
    EXPECT_TRUE(ui::friendlyImportWarnings({}, false).isEmpty());
}

TEST(ImportWarnings, AFewNamesThenHowManyMore) {
    QStringList raw;
    for (int i = 0; i < 11; ++i) raw << QStringLiteral("Unresolved part p%1.dat").arg(i);
    const QString line = ui::friendlyImportWarnings(raw, false).front();
    EXPECT_TRUE(line.startsWith(QStringLiteral("11 part(s)"))) << line.toStdString();
    EXPECT_TRUE(line.contains(QStringLiteral("p7.dat and 3 more"))) << line.toStdString();
}

// Idea 4: Studio 2.0's own LDraw library (<Studio>/ldraw) and LDD → LDraw
// mapping (<Studio>/data/ldraw.xml).
TEST(StudioInstall, FindsStudiosLibraryAndMapping) {
    QTemporaryDir dir;
    const QString studio = dir.filePath(QStringLiteral("Studio 2.0"));
    writeFile(studio + QStringLiteral("/ldraw/LDConfig.ldr"), "0 LDraw.org Configuration File\n");
    writeFile(studio + QStringLiteral("/data/ldraw.xml"), "<LDD2LDraw/>\n");
    const auto found = ui::findStudioInstall({ dir.filePath(QStringLiteral("nothing here")), studio });
    EXPECT_EQ(found.ldrawLibrary, QDir::cleanPath(studio + QStringLiteral("/ldraw")));
    EXPECT_EQ(found.ldrawXml, QDir::cleanPath(studio + QStringLiteral("/data/ldraw.xml")));
    const auto none = ui::findStudioInstall({ dir.filePath(QStringLiteral("nothing here")) });
    EXPECT_TRUE(none.ldrawLibrary.isEmpty());
    EXPECT_TRUE(none.ldrawXml.isEmpty());
}

// Settings in the UI: using Studio's library when found can be turned off.
TEST(StudioInstall, UsingStudiosLibraryIsAPreference) {
    QSettings().remove(QStringLiteral("import/useStudioLibrary"));
    ui::PreferencesDialog dialog;
    QCheckBox* use = nullptr;
    for (auto* c : dialog.findChildren<QCheckBox*>())
        if (c->text().startsWith(QStringLiteral("Use Studio's LDraw library"))) use = c;
    ASSERT_NE(use, nullptr);
    EXPECT_TRUE(use->isChecked());
    use->setChecked(false);
    dialog.accept();
    EXPECT_FALSE(QSettings().value(QStringLiteral("import/useStudioLibrary"), true).toBool());
    QSettings().remove(QStringLiteral("import/useStudioLibrary"));
}
