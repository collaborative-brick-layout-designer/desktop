// The words and small behaviors the desktop shares with the website (the
// web's review fixes of 2026-11-09): one click adds the first part to an
// empty map, Find & Replace looks in parts first (by name too) and asks
// before putting a part the library doesn't have on the map, the venue's
// sizes take meters and centimeters, and the Venue library says "layout",
// not "project".

#include "ui/FindDialog.h"
#include "ui/MapView.h"
#include "ui/PartsBrowser.h"
#include "ui/ConfirmDialog.h"
#include "ui/VenueDimensionsDialog.h"
#include "ui/VenueLibraryPanel.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QRegularExpression>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QTimer>

using namespace bld;

namespace {

parts::PartsLibrary& library() {
    static parts::PartsLibrary lib;
    static bool scanned = false;
    if (!scanned) {
        const QString root = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        if (QDir(root).exists()) {
            lib.addSearchPath(root);
            lib.scan();
        }
        scanned = true;
    }
    return lib;
}

std::unique_ptr<core::Map> tightCorner() {
    auto r = saveload::readBbm(QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm"));
    return r.ok() ? std::move(r.map) : std::make_unique<core::Map>();
}

int countPart(const core::Map& map, const QString& part) {
    int n = 0;
    for (const auto& l : map.layers())
        if (l && l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) n += b.partNumber.compare(part, Qt::CaseInsensitive) == 0;
    return n;
}

QPushButton* button(QWidget& w, const QString& text) {
    for (auto* b : w.findChildren<QPushButton*>())
        if (b->text() == text) return b;
    return nullptr;
}

// Answers the next app question: the confirm button when `yes`, else Cancel; keeps its title.
void answerNext(bool yes, QString* title) {
    QTimer::singleShot(100, [yes, title] {
        auto* d = qobject_cast<ui::ConfirmDialog*>(QApplication::activeModalWidget());
        if (!d) return;
        *title = d->windowTitle() + QLatin1Char('|') + d->bodyText();
        (yes ? d->confirmButton() : d->cancelButton())->click();
    });
}

}  // namespace

TEST(PlainWords, FindLooksInPartsFirstByNameAndAsksBeforeAnUnknownPart) {
    if (!library().metadata(QStringLiteral("2865.8"))) GTEST_SKIP() << "the BlueBrickParts library isn't checked out";
    ui::MapView view(library());
    view.loadMap(tightCorner());
    ui::FindDialog d(view);
    d.show();
    auto* scope = d.findChild<QComboBox*>();
    ASSERT_TRUE(scope);
    EXPECT_EQ(scope->currentIndex(), 0);
    EXPECT_EQ(scope->itemText(0), QStringLiteral("Parts (name or number)"));
    const auto edits = d.findChildren<QLineEdit*>();
    ASSERT_GE(edits.size(), 2);
    QLineEdit* findE = edits[0];
    QLineEdit* replaceE = edits[1];
    QLabel* status = d.findChild<QLabel*>(QStringLiteral("findStatus"));
    ASSERT_TRUE(status);

    // "Rail 9V Straight" is 2865.8's name: 88 of them on Tight corner.
    findE->setText(QStringLiteral("9v straight"));
    QTest::qWait(400);
    EXPECT_TRUE(status->text().startsWith(QStringLiteral("88 "))) << status->text().toStdString();

    // A number the library doesn't have: asked first, and Go back changes nothing.
    findE->setText(QStringLiteral("2865.8"));
    replaceE->setText(QStringLiteral("nosuchpart.1"));
    QString asked;
    answerNext(false, &asked);
    button(d, QStringLiteral("Replace All"))->click();
    EXPECT_TRUE(asked.contains(QStringLiteral("“nosuchpart.1” isn’t in the parts library"))) << asked.toStdString();
    EXPECT_EQ(countPart(*view.currentMap(), QStringLiteral("2865.8")), 88);
    EXPECT_EQ(countPart(*view.currentMap(), QStringLiteral("nosuchpart.1")), 0);

    // A part the library has: no question, and the count says "parts".
    replaceE->setText(QStringLiteral("2865.1"));
    asked.clear();
    if (library().metadata(QStringLiteral("2865.1"))) {
        button(d, QStringLiteral("Replace All"))->click();
        EXPECT_TRUE(asked.isEmpty());
        EXPECT_EQ(countPart(*view.currentMap(), QStringLiteral("2865.1")), 88);
        EXPECT_EQ(status->text(), QStringLiteral("Replaced 88 parts."));
    }
}

// Find Next went to every match at once; now it steps through them, one
// picked and in view at a time (Bug 17), by name too.
TEST(PlainWords, FindNextStepsThroughTheMatchesOneByOne) {
    if (!library().metadata(QStringLiteral("2865.8"))) GTEST_SKIP() << "the BlueBrickParts library isn't checked out";
    ui::MapView view(library());
    view.resize(800, 600);
    view.show();
    view.loadMap(tightCorner());
    ui::FindDialog d(view);
    d.show();
    auto* findE = d.findChild<QLineEdit*>(QStringLiteral("findText"));
    auto* next = d.findChild<QPushButton*>(QStringLiteral("findNext"));
    QLabel* status = d.findChild<QLabel*>(QStringLiteral("findStatus"));
    ASSERT_TRUE(findE && next && status);
    findE->setText(QStringLiteral("9v straight"));
    QTest::qWait(400);
    const qsizetype all = view.scene()->selectedItems().size();
    EXPECT_GE(all, 88);
    EXPECT_TRUE(status->text().startsWith(QStringLiteral("88 matches"))) << status->text().toStdString();
    next->click();
    EXPECT_EQ(status->text(), QStringLiteral("Match 1 of 88."));
    const QList<QGraphicsItem*> first = view.scene()->selectedItems();
    ASSERT_FALSE(first.isEmpty());
    EXPECT_LT(first.size(), all);  // one match, not all of them
    EXPECT_TRUE(view.mapToScene(view.viewport()->rect()).boundingRect().intersects(first.first()->sceneBoundingRect()));
    // The next one shows something new (rails in one set are picked together).
    const QRegularExpression at(QStringLiteral("^Match (\\d+) of 88\\.$"));
    next->click();
    const auto second = at.match(status->text());
    ASSERT_TRUE(second.hasMatch()) << status->text().toStdString();
    EXPECT_GT(second.captured(1).toInt(), 1);
    EXPECT_NE(view.scene()->selectedItems(), first);
    // Enter in the box does the same.
    QTest::keyClick(findE, Qt::Key_Return);
    const auto third = at.match(status->text());
    ASSERT_TRUE(third.hasMatch()) << status->text().toStdString();
    EXPECT_NE(third.captured(1), second.captured(1));
    findE->setText(QStringLiteral("no such thing"));
    QTest::qWait(400);
    EXPECT_EQ(status->text(), QStringLiteral("No matches."));
}

TEST(PlainWords, OneClickAddsTheFirstPartToAnEmptyMapAndADoubleClickNoSecond) {
    if (!library().metadata(QStringLiteral("2865.8"))) GTEST_SKIP() << "the BlueBrickParts library isn't checked out";
    ui::PartsBrowser browser(library());
    auto* grid = browser.findChild<QListWidget*>();
    ASSERT_TRUE(grid && grid->count() > 0);
    QListWidgetItem* item = grid->item(0);
    int added = 0;
    QObject::connect(&browser, &ui::PartsBrowser::partActivated, [&](const QString&) { ++added; });
    bool empty = true;
    browser.mapIsEmpty = [&] { return empty; };

    // A click on an empty map adds it; the double-click it starts adds no second one.
    emit grid->itemClicked(item);
    EXPECT_EQ(added, 1);
    empty = false;
    emit grid->itemActivated(item);
    EXPECT_EQ(added, 1);
    // With parts on the map: a click only picks, a double-click adds.
    QTest::qWait(QApplication::doubleClickInterval() + 150);
    emit grid->itemClicked(item);
    EXPECT_EQ(added, 1);
    emit grid->itemActivated(item);
    EXPECT_EQ(added, 2);
}

TEST(PlainWords, TheVenueSizesTakeMetersAndTheRectangleFollowsTheUnit) {
    ui::VenueDimensionsDialog d;
    QComboBox* unit = nullptr;
    for (auto* c : d.findChildren<QComboBox*>())
        if (c->findData(QStringLiteral("ft")) >= 0) unit = c;
    ASSERT_TRUE(unit);
    EXPECT_GE(unit->findData(QStringLiteral("m")), 0);
    EXPECT_GE(unit->findData(QStringLiteral("cm")), 0);
    unit->setCurrentIndex(unit->findData(QStringLiteral("m")));
    QString suffix;
    QTimer::singleShot(100, [&suffix] {
        auto* box = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!box) return;
        if (auto* w = box->findChild<QDoubleSpinBox*>()) suffix = w->suffix();
        box->reject();
    });
    button(d, QStringLiteral("Rectangle preset…"))->click();
    EXPECT_EQ(suffix, QStringLiteral(" m"));
}

TEST(PlainWords, TheVenueLibrarySaysLayoutNotProject) {
    QTemporaryDir dir;
    ui::VenueLibraryPanel panel;
    panel.setLibraryPath(dir.path());
    EXPECT_TRUE(button(panel, QStringLiteral("Use in This Layout")));
    EXPECT_TRUE(button(panel, QStringLiteral("Save This Layout’s Venue…")));
    EXPECT_FALSE(button(panel, QStringLiteral("Load into Project")));
}
