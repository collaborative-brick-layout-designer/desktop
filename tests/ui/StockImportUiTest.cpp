// Trains › Import a model… on the desktop (trains design Phase 6), walked as
// a person would: the Rolling stock tab's Import a model… picks an LDraw
// engine (two bogie plates, four wheel holders and a magnet coupler at each
// end, drawn from a tiny LDraw library), the dialog shows it front to the
// right with the trucks on the bogies and the couplers on the magnets, a
// truck is dragged and the engine turned around, and Add to rolling stock
// makes the part and an engine drawn with it, one undo step. The part goes
// with the layout's file and is offered to the server like a placed part.
// The web's journeys/trainsModel.spec.ts walks the same.

#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "sync/PartsUpload.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/StockImportDialog.h"
#include "ui/TrainsPanel.h"
#include "ui/UpdateCheck.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>

#include <memory>

using namespace bld;

namespace {

void writeFile(const QString& path, const QByteArray& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(body);
}

// A flat w x d stud plate at the part's origin (LDraw: 20 LDU a stud, -y up).
QByteArray plate(double w, double d) {
    const double x = w * 10, z = d * 10;
    return QStringLiteral("0 Test plate\n4 16 %1 -8 %2 %3 -8 %2 %3 -8 %4 %1 -8 %4\n")
        .arg(-x)
        .arg(-z)
        .arg(x)
        .arg(z)
        .toUtf8();
}

class Win : public ui::MainWindow {
public:
    using ui::MainWindow::askModelFile;
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::runStockImportDialog;
};

class StockImport : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().setValue(QStringLiteral("modules/libraryPath"), home_.path());
        QSettings().setValue(QStringLiteral("import/useStudioLibrary"), false);
        // A tiny LDraw library: the train parts the guess knows, and a base.
        const QDir lib(ldraw_.path());
        writeFile(lib.filePath(QStringLiteral("LDConfig.ldr")),
                  "0 !COLOUR Black CODE 0 VALUE #1B2A34 EDGE #2B4354\n"
                  "0 !COLOUR Red CODE 4 VALUE #C91A09 EDGE #333333\n"
                  "0 !COLOUR Light_Grey CODE 7 VALUE #9BA19D EDGE #333333\n");
        writeFile(lib.filePath(QStringLiteral("parts/base.dat")), plate(28, 6));
        writeFile(lib.filePath(QStringLiteral("parts/4092a.dat")), plate(2, 4));
        writeFile(lib.filePath(QStringLiteral("parts/2878c01.dat")), plate(1, 6));
        writeFile(lib.filePath(QStringLiteral("parts/2920.dat")), plate(2, 2));
        QSettings().setValue(QStringLiteral("import/ldrawLibraryPath"), ldraw_.path());
        // The engine: bogies 9 studs either side of its middle, wheels around
        // them, magnets 15 studs out, all on a 28 x 6 base.
        model_ = QDir(files_.path()).filePath(QStringLiteral("Santa Fe engine.ldr"));
        writeFile(model_, "0 Santa Fe engine\n"
                          "1 4 0 0 0 1 0 0 0 1 0 0 0 1 base.dat\n"
                          "1 0 180 8 0 1 0 0 0 1 0 0 0 1 4092a.dat\n"
                          "1 0 -180 8 0 1 0 0 0 1 0 0 0 1 4092a.dat\n"
                          "1 0 130 16 0 1 0 0 0 1 0 0 0 1 2878c01.dat\n"
                          "1 0 230 16 0 1 0 0 0 1 0 0 0 1 2878c01.dat\n"
                          "1 0 -130 16 0 1 0 0 0 1 0 0 0 1 2878c01.dat\n"
                          "1 0 -230 16 0 1 0 0 0 1 0 0 0 1 2878c01.dat\n"
                          "1 7 300 0 0 1 0 0 0 1 0 0 0 1 2920.dat\n"
                          "1 7 -300 0 0 1 0 0 0 1 0 0 0 1 2920.dat\n");
        window_ = std::make_unique<Win>(parts_);
        window_->resize(1300, 850);
        window_->ensureDocument();
        window_->show();
        view_ = window_->findChild<ui::MapView*>();
        panel_ = window_->findChild<ui::TrainsPanel*>();
        ASSERT_TRUE(view_ && panel_);
    }
    void TearDown() override {
        view_->undoStack()->setClean();
        window_.reset();
        QSettings().remove(QStringLiteral("modules/libraryPath"));
        QSettings().remove(QStringLiteral("import/ldrawLibraryPath"));
        QSettings().remove(QStringLiteral("import/useStudioLibrary"));
        QStandardPaths::setTestModeEnabled(false);
    }

    QTemporaryDir home_, ldraw_, files_;
    QString model_;
    parts::PartsLibrary parts_;
    std::unique_ptr<Win> window_;
    ui::MapView* view_ = nullptr;
    ui::TrainsPanel* panel_ = nullptr;
};

} // namespace

TEST_F(StockImport, AnEngineIsGuessedConfirmedAndAddedWithItsPicture) {
    panel_->show();
    panel_->tabs()->setCurrentIndex(1);
    auto* button = panel_->importModelButton();
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isEnabled());
    window_->askModelFile = [this](const QString&, const QString&) { return model_; };
    int shown = 0;
    window_->runStockImportDialog = [&](ui::StockImportDialog& d) {
        ++shown;
        d.show();
        EXPECT_TRUE(QTest::qWaitForWindowExposed(&d));
        // Turned to run along x, front to the right: 32 x 6 studs (the magnets stick out a stud).
        EXPECT_EQ(d.part().widthStuds, 32);
        EXPECT_EQ(d.part().heightStuds, 6);
        // The trucks on the bogie plates, the couplers on the magnets, and it says so.
        EXPECT_DOUBLE_EQ(d.frontMarker(), 9.0);
        EXPECT_DOUBLE_EQ(d.rearMarker(), -9.0);
        EXPECT_DOUBLE_EQ(d.couplerMarker(), 15.0);
        EXPECT_TRUE(d.note()->text().contains(QStringLiteral("bogies"))) << d.note()->text().toStdString();
        EXPECT_TRUE(d.note()->text().contains(QStringLiteral("magnets"))) << d.note()->text().toStdString();
        EXPECT_EQ(d.kind(), QStringLiteral("engine")); // its name says so
        // Drag the front truck one stud forward, on the picture.
        QGraphicsItem* front = nullptr;
        for (auto* it : d.view()->scene()->items())
            if (it->data(0).toString() == QLatin1String("front")) front = it;
        EXPECT_NE(front, nullptr);
        if (front) {
            QWidget* vp = d.view()->viewport();
            const QPoint from = d.view()->mapFromScene(QPointF(9.0, 0.0));
            const QPoint to = d.view()->mapFromScene(QPointF(10.0, 0.0));
            QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, from);
            QTest::mouseMove(vp, (from + to) / 2);
            QTest::mouseMove(vp, to);
            QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, to);
        }
        EXPECT_NEAR(d.setup().front, 10.0, 0.25 + 1e-9);
        d.setFront(10.0); // exactly, whatever the screen's rounding
        EXPECT_DOUBLE_EQ(d.findChild<QDoubleSpinBox*>(QStringLiteral("stockImportFront"))->value(), 10.0);
        // The other end is the front: the trucks swap ends.
        d.turnAround();
        EXPECT_DOUBLE_EQ(d.setup().front, 9.0);
        EXPECT_DOUBLE_EQ(d.setup().rear, -10.0);
        EXPECT_DOUBLE_EQ(d.frontMarker(), 9.0);
        EXPECT_EQ(d.part().quarterTurns, 2);
        // Signed out: the roster box says how to sign in instead.
        EXPECT_FALSE(d.findChild<QCheckBox*>(QStringLiteral("stockImportRoster"))->isEnabled());
        d.findChild<QLineEdit*>(QStringLiteral("stockImportNumber"))->setText(QStringLiteral("SF 16"));
        return true;
    };
    QTest::mouseClick(button, Qt::LeftButton);
    ASSERT_EQ(shown, 1);

    const core::Map* map = view_->currentMap();
    ASSERT_EQ(map->sidecar.rollingStock.size(), 1u);
    const core::RollingStock& s = map->sidecar.rollingStock.front();
    EXPECT_EQ(s.number, QStringLiteral("SF 16"));
    EXPECT_EQ(s.kind, QStringLiteral("engine"));
    EXPECT_TRUE(s.powered);
    EXPECT_EQ(s.lookMode, QStringLiteral("model"));
    EXPECT_DOUBLE_EQ(s.length, 30.0);
    EXPECT_DOUBLE_EQ(s.width, 6.0);
    ASSERT_EQ(s.trucks.size(), 2u);
    EXPECT_DOUBLE_EQ(s.trucks[0].at, 9.0);
    EXPECT_DOUBLE_EQ(s.trucks[1].at, -10.0);
    const QJsonObject model = s.extras.value(QLatin1String("model")).toObject();
    EXPECT_EQ(model.value(QLatin1String("file")).toString(), QStringLiteral("Santa Fe engine.ldr"));
    EXPECT_EQ(model.value(QLatin1String("format")).toString(), QStringLiteral("ldraw"));
    EXPECT_EQ(model.value(QLatin1String("partKey")).toString(), s.partKey);
    // The part is in the library, with its picture, in the Rolling stock folder.
    const auto meta = parts_.metadata(s.partKey);
    ASSERT_TRUE(meta.has_value()) << s.partKey.toStdString();
    EXPECT_FALSE(parts_.pixmap(s.partKey).isNull());
    EXPECT_TRUE(meta->xmlFilePath.contains(QStringLiteral("Rolling stock")))
        << meta->xmlFilePath.toStdString();
    // The panel shows it, on the Rolling stock tab.
    EXPECT_EQ(panel_->tabs()->currentIndex(), 1);
    // It travels with the layout's file, and counts as a part the layout uses (offered to a server).
    EXPECT_TRUE(sync::partNumbersIn(*map).contains(s.partKey.toUpper()));
    const auto files = import::layoutPartFiles(*map, parts_, QStringLiteral(BLD_PARTS_LIBRARY_ROOT));
    EXPECT_TRUE(files.contains(QFileInfo(meta->xmlFilePath).fileName()));
    // One undo step takes the engine back off (the part stays in the library).
    view_->undoStack()->undo();
    EXPECT_TRUE(view_->currentMap()->sidecar.rollingStock.empty());
}

TEST_F(StockImport, CancelAddsNothing) {
    window_->askModelFile = [this](const QString&, const QString&) { return model_; };
    window_->runStockImportDialog = [](ui::StockImportDialog&) { return false; };
    QTest::mouseClick(panel_->importModelButton(), Qt::LeftButton);
    EXPECT_TRUE(view_->currentMap()->sidecar.rollingStock.empty());
}

TEST(StockImportDialog, WithoutWheelsTheTrucksAreAQuarterIn) {
    ui::PreparedPart part;
    part.sprite = QImage(20 * 8, 4 * 8, QImage::Format_ARGB32_Premultiplied);
    part.sprite.fill(Qt::darkGreen);
    part.widthStuds = 4; // built along y: turned a quarter
    part.heightStuds = 20;
    part.sprite = part.sprite.transformed(QTransform().rotate(90));
    part.contentStuds = QRectF(0, 0, 4, 20);
    const track::StockGuess g = ui::stockGuessOf(part);
    EXPECT_EQ(g.quarterTurns, 1);
    EXPECT_EQ(g.trucksFrom, QStringLiteral("none"));
    ui::StockImportDialog d(part, g, true);
    EXPECT_EQ(d.part().widthStuds, 20);
    EXPECT_DOUBLE_EQ(d.setup().length, 22.0);
    EXPECT_DOUBLE_EQ(d.frontMarker(), 5.5);
    EXPECT_DOUBLE_EQ(d.rearMarker(), -5.5);
    EXPECT_TRUE(d.note()->text().contains(QStringLiteral("quarter of the way in")));
    EXPECT_TRUE(d.findChild<QCheckBox*>(QStringLiteral("stockImportRoster"))->isEnabled());
    d.findChild<QLineEdit*>(QStringLiteral("stockImportName"))->clear();
    EXPECT_FALSE(d.addButton()->isEnabled()); // a part needs a name
    // A monorail car runs on monorail track and isn't powered.
    const core::RollingStock m =
        ui::stockFromModel(QStringLiteral("m1"), QStringLiteral("MONO.1"), d.setup(), QString(),
                           QStringLiteral("monorail"), QStringLiteral("mono.io"), QStringLiteral("studio"),
                           QStringLiteral("#0055BF"), QStringLiteral("2026-11-24"));
    EXPECT_EQ(m.trackKind, QStringLiteral("monorail"));
    EXPECT_FALSE(m.powered);
    EXPECT_EQ(m.color, QStringLiteral("#0055BF"));
    EXPECT_EQ(m.extras.value(QLatin1String("model")).toObject().value(QLatin1String("importedAt")).toString(),
              QStringLiteral("2026-11-24"));
}
