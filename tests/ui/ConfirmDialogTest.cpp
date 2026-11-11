// The one confirmation dialog (ui/ConfirmDialog.h): what it says, Cancel
// and Esc, focus on Cancel, typing the name, and the local Module
// library's Delete going through it.

#include "ui/ConfirmDialog.h"
#include "ui/ModuleLibraryPanel.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace bld::ui;

// A name that looks like markup is shown as typed, never rendered.
TEST(ConfirmDialog, ShowsNamesAsPlainText) {
    ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("<b>Yard</b><img src=\"x.png\">"), ConfirmDialog::moduleWording()));
    auto* title = d.findChild<QLabel*>(QStringLiteral("confirmTitle"));
    EXPECT_EQ(title->textFormat(), Qt::PlainText);
    EXPECT_TRUE(title->text().contains(QStringLiteral("<b>Yard</b>")));
}

TEST(ConfirmDialog, SaysWhatGoesWhatStaysAndWhetherItComesBack) {
    DeleteWording w = ConfirmDialog::moduleWording();
    ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("Bench"), w));
    EXPECT_EQ(d.findChild<QLabel*>(QStringLiteral("confirmTitle"))->text(), QStringLiteral("Delete “Bench”?"));
    EXPECT_TRUE(d.bodyText().contains(QStringLiteral("Layouts that already use it don’t change.")));
    EXPECT_TRUE(d.bodyText().contains(QStringLiteral("This can’t be undone.")));
    EXPECT_EQ(d.confirmButton()->text(), QStringLiteral("Delete"));
    EXPECT_TRUE(d.confirmButton()->property("danger").toBool());
    EXPECT_EQ(d.nameEdit(), nullptr);
    EXPECT_FALSE(d.confirmButton()->isDefault());
    EXPECT_FALSE(d.confirmButton()->autoDefault());
    d.show();
    d.activateWindow();
    EXPECT_TRUE(QTest::qWaitForWindowActive(&d));
    EXPECT_EQ(QApplication::focusWidget(), d.cancelButton());
}

TEST(ConfirmDialog, CancelAndEscSayNoDeleteSaysYes) {
    {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("x")));
        d.show();
        d.cancelButton()->click();
        EXPECT_EQ(d.result(), QDialog::Rejected);
    }
    {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("x")));
        d.show();
        QTest::keyClick(&d, Qt::Key_Escape);
        EXPECT_EQ(d.result(), QDialog::Rejected);
        EXPECT_FALSE(d.isVisible());
    }
    {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("x")));
        d.show();
        d.confirmButton()->click();
        EXPECT_EQ(d.result(), QDialog::Accepted);
    }
}

TEST(ConfirmDialog, ABigDeletionNeedsTheNameTyped) {
    ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("Main Yard"), ConfirmDialog::layoutWording()));
    d.show();
    ASSERT_NE(d.nameEdit(), nullptr);
    EXPECT_FALSE(d.confirmButton()->isEnabled());
    d.nameEdit()->setText(QStringLiteral("Main"));
    EXPECT_FALSE(d.confirmButton()->isEnabled());
    emit d.nameEdit()->returnPressed();
    EXPECT_TRUE(d.isVisible());
    d.nameEdit()->setText(QStringLiteral("  main yard "));
    EXPECT_TRUE(d.confirmButton()->isEnabled());
    emit d.nameEdit()->returnPressed();
    EXPECT_EQ(d.result(), QDialog::Accepted);
}

TEST(ConfirmDialog, Words) {
    DeleteWording w;
    w.verb = QStringLiteral("Remove");
    const auto o = ConfirmDialog::deleteOptions(QStringLiteral("Ann"), w);
    EXPECT_EQ(o.title, QStringLiteral("Remove “Ann”?"));
    EXPECT_EQ(o.confirmLabel, QStringLiteral("Remove"));
    EXPECT_EQ(o.removes, QStringLiteral("“Ann” is deleted."));
    w.title = QStringLiteral("Cancel the invite?");
    EXPECT_EQ(ConfirmDialog::deleteOptions(QStringLiteral("x"), w).title, QStringLiteral("Cancel the invite?"));
    EXPECT_TRUE(ConfirmDialog::typedMatches(QStringLiteral(" ABC "), QStringLiteral("abc")));
    EXPECT_FALSE(ConfirmDialog::typedMatches(QStringLiteral("ab"), QStringLiteral("abc")));
    // The same words as the website's.
    EXPECT_EQ(ConfirmDialog::venueWording().keeps, QStringLiteral("Layouts made from it keep their own copy of the venue."));
    EXPECT_EQ(ConfirmDialog::customPartWording().keeps, QStringLiteral("Layouts that use it show a placeholder in its place."));
    EXPECT_EQ(ConfirmDialog::setWording().removes,
              QStringLiteral("The set is deleted from your custom parts, so it can’t be placed again."));
    EXPECT_EQ(ConfirmDialog::setWording().keeps, QStringLiteral("Layouts that use it keep the parts it put down."));
    EXPECT_TRUE(ConfirmDialog::layoutWording().typeName);
}

// Deleted from a server that keeps it in Recently deleted: the box says
// where it waits and for how long, not "This can’t be undone."; a file on
// this computer (or an older server) still says that.
TEST(ConfirmDialog, AServerDeleteSaysItWaitsInRecentlyDeleted) {
    const QString waits =
        QStringLiteral("It waits 30 days in Recently deleted on the website’s Home, where you can restore it.");
    for (const DeleteWording& w : { ConfirmDialog::layoutWording(true), ConfirmDialog::moduleWording(true),
                                    ConfirmDialog::venueWording(true) }) {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("Yard"), w));
        EXPECT_TRUE(d.bodyText().contains(waits)) << d.bodyText().toStdString();
        EXPECT_FALSE(d.bodyText().contains(QStringLiteral("can’t be undone"))) << d.bodyText().toStdString();
    }
    for (const DeleteWording& w : { ConfirmDialog::layoutWording(), ConfirmDialog::moduleWording(),
                                    ConfirmDialog::venueWording(), ConfirmDialog::customPartWording() }) {
        ConfirmDialog d(ConfirmDialog::deleteOptions(QStringLiteral("Yard"), w));
        EXPECT_FALSE(d.bodyText().contains(QStringLiteral("Recently deleted")));
        EXPECT_TRUE(d.bodyText().contains(QStringLiteral("This can’t be undone.")));
    }
}

TEST(ConfirmDialog, AskWaitsForTheAnswer) {
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<ConfirmDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->confirmButton()->click();
    });
    EXPECT_TRUE(ConfirmDialog::confirmDelete(nullptr, QStringLiteral("Bench")));
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<ConfirmDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->cancelButton()->click();
    });
    EXPECT_FALSE(ConfirmDialog::confirmDelete(nullptr, QStringLiteral("Bench")));
}

TEST(ModuleLibraryPanel, DeletesAModuleFileOnlyAfterAYes) {
    QTemporaryDir dir;
    const QString bbm = dir.filePath(QStringLiteral("Bench.bbm"));
    const QString sidecar = bld::saveload::sidecarPathFor(bbm);
    for (const QString& p : { bbm, sidecar }) {
        QFile f(p);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    const QString before = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
    ModuleLibraryPanel panel;
    panel.setLibraryPath(dir.path());
    ASSERT_EQ(panel.list()->count(), 1);
    QStringList asked;
    bool answer = false;
    panel.setConfirm([&](const QString& name) {
        asked << name;
        return answer;
    });
    EXPECT_FALSE(panel.deleteModule(bbm));
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Bench") });
    EXPECT_TRUE(QFile::exists(bbm));
    answer = true;
    EXPECT_TRUE(panel.deleteModule(bbm));
    EXPECT_FALSE(QFile::exists(bbm));
    EXPECT_FALSE(QFile::exists(sidecar));
    EXPECT_EQ(panel.list()->item(0)->flags(), Qt::NoItemFlags);  // "No modules here yet."
    QSettings().setValue(QStringLiteral("modules/libraryPath"), before);
}

TEST(ModuleLibraryPanel, AModuleFileWithNoPartsIsLabeledAndWontImport) {
    QTemporaryDir dir;
    auto write = [&](const QString& name, const QByteArray& body) {
        QFile f(dir.filePath(name));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(body);
    };
    write(QStringLiteral("Blank.bbm"), "<?xml version=\"1.0\"?>\n<Map>\n  <Version>9</Version>\n  <Layer type=\"brick\"><Bricks></Bricks></Layer>\n</Map>\n");
    write(QStringLiteral("Yard.bbm"), "<?xml version=\"1.0\"?>\n<Map>\n  <Layer type=\"brick\"><Brick id=\"1\"><PartNumber>3001</PartNumber></Brick></Layer>\n</Map>\n");
    ModuleLibraryPanel panel;
    QStringList imported;
    QObject::connect(&panel, &ModuleLibraryPanel::moduleImportRequested, [&](const QString& p) { imported << p; });
    panel.setLibraryPath(dir.path());
    ASSERT_EQ(panel.list()->count(), 2);
    QListWidgetItem* blank = panel.list()->item(0);
    QListWidgetItem* yard = panel.list()->item(1);
    EXPECT_EQ(blank->text(), QStringLiteral("Blank  \u00b7  No parts yet"));
    EXPECT_FALSE(blank->flags() & Qt::ItemIsDragEnabled);
    EXPECT_EQ(yard->text(), QStringLiteral("Yard"));
    EXPECT_TRUE(yard->flags() & Qt::ItemIsDragEnabled);
    emit panel.list()->itemActivated(blank);
    EXPECT_TRUE(imported.isEmpty());
    emit panel.list()->itemActivated(yard);
    EXPECT_EQ(imported.size(), 1);
}

// BLD_MODULE_SHOTS=<dir>: pictures of the Module library list with a blank module, light and dark (not compared).
TEST(ModuleLibraryPanel, PictureForReview) {
    const QString out = qEnvironmentVariable("BLD_MODULE_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_MODULE_SHOTS not set";
    QTemporaryDir dir;
    auto write = [&](const QString& name, const QByteArray& body) {
        QFile f(dir.filePath(name));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(body);
    };
    write(QStringLiteral("Blank module.bbm"), "<Map>\n<Layer type=\"brick\"><Bricks></Bricks></Layer>\n</Map>\n");
    write(QStringLiteral("Has parts.bbm"), "<Map>\n<Layer type=\"brick\"><Bricks><Brick id=\"1\"></Brick></Bricks></Layer>\n</Map>\n");
    const QPalette before = QApplication::palette();
    for (const bool dark : { false, true }) {
        QPalette p = before;
        if (dark) {
            p.setColor(QPalette::Window, QColor(32, 34, 38));
            p.setColor(QPalette::Base, QColor(40, 42, 46));
            p.setColor(QPalette::WindowText, QColor(230, 230, 230));
            p.setColor(QPalette::Text, QColor(230, 230, 230));
            p.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 130, 130));
        } else {
            p.setColor(QPalette::Window, QColor(246, 246, 246));
            p.setColor(QPalette::Base, Qt::white);
            p.setColor(QPalette::WindowText, Qt::black);
            p.setColor(QPalette::Text, Qt::black);
            p.setColor(QPalette::Disabled, QPalette::Text, QColor(150, 150, 150));
        }
        QApplication::setPalette(p);
        ModuleLibraryPanel panel;
        panel.setLibraryPath(dir.path());
        panel.resize(320, 220);
        panel.show();
        QApplication::processEvents();
        panel.grab().save(out + (dark ? QStringLiteral("/module-panel-dark.png") : QStringLiteral("/module-panel-light.png")));
    }
    QApplication::setPalette(before);
}
