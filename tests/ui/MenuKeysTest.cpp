// The keys of the main window: no two menu items share a shortcut (Qt fires
// neither when two do: Ctrl+P was both Print… and Select Path), no two items
// of one menu share an Alt letter, Help > Keyboard Shortcuts lists each item
// once, and Export as Image waits for a file.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"
#include "ui/help/ShortcutsDialog.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QHash>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

using namespace bld;

namespace {

parts::PartsLibrary& library() {
    static parts::PartsLibrary lib;
    return lib;
}

QString plain(QString t) { return t.remove(QLatin1Char('&')); }

// The letter after a single '&' ("&&" is a plain '&').
QChar mnemonic(const QString& text) {
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != QLatin1Char('&')) continue;
        if (text[i + 1] == QLatin1Char('&')) { ++i; continue; }
        return text[i + 1].toLower();
    }
    return {};
}

// Every menu under the bar, filled the way opening it fills it.
QList<QPair<QString, QMenu*>> allMenus(QMenuBar* bar) {
    QList<QPair<QString, QMenu*>> out;
    for (QAction* top : bar->actions())
        if (top->menu()) out << qMakePair(plain(top->text()), top->menu());
    for (int i = 0; i < out.size(); ++i) {
        emit out[i].second->aboutToShow();
        for (QAction* a : out[i].second->actions())
            if (a->menu()) out << qMakePair(out[i].first + QStringLiteral(" > ") + plain(a->text()), a->menu());
    }
    return out;
}

class MenuKeys : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
    }
    void TearDown() override { QStandardPaths::setTestModeEnabled(false); }
};

}  // namespace

TEST_F(MenuKeys, NoTwoItemsShareAShortcut) {
    ui::MainWindow w(library());
    w.show();
    QApplication::processEvents();
    allMenus(w.menuBar());  // fills the menus built on opening
    QHash<QString, QStringList> byKey;
    for (QAction* a : w.findChildren<QAction*>()) {
        if (!a->isVisible() || a->shortcutContext() == Qt::WidgetShortcut) continue;
        // In a menu or on a toolbar (a menu is a window of its own).
        bool shown = false;
        for (QObject* o : a->associatedObjects())
            if (qobject_cast<QWidget*>(o)) shown = true;
        if (!shown) continue;
        for (const QKeySequence& k : a->shortcuts())
            if (!k.isEmpty()) byKey[k.toString()] << plain(a->text());
    }
    EXPECT_GT(byKey.size(), 20);  // the scan found the menus
    QStringList clashes;
    for (auto it = byKey.cbegin(); it != byKey.cend(); ++it)
        if (it.value().size() > 1) clashes << it.key() + QStringLiteral(": ") + it.value().join(QStringLiteral(", "));
    EXPECT_TRUE(clashes.isEmpty()) << clashes.join(QLatin1Char('\n')).toStdString();
    // Ctrl+P prints.
    EXPECT_EQ(byKey.value(QKeySequence(QKeySequence::Print).toString()), QStringList{ QStringLiteral("Print…") });
}

TEST_F(MenuKeys, NoTwoItemsOfAMenuShareAnAltLetter) {
    ui::MainWindow w(library());
    w.show();
    QApplication::processEvents();
    QStringList clashes;
    const auto check = [&clashes](const QString& where, const QList<QAction*>& actions) {
        QHash<QChar, QString> letters;
        for (QAction* a : actions) {
            if (a->isSeparator() || !a->isVisible()) continue;
            const QChar c = mnemonic(a->text());
            if (c.isNull()) continue;
            if (letters.contains(c))
                clashes << QStringLiteral("%1: '%2' %3 / %4").arg(where, c, letters.value(c), plain(a->text()));
            else
                letters.insert(c, plain(a->text()));
        }
    };
    check(QStringLiteral("menu bar"), w.menuBar()->actions());
    for (const auto& [where, menu] : allMenus(w.menuBar())) check(where, menu->actions());
    EXPECT_TRUE(clashes.isEmpty()) << clashes.join(QLatin1Char('\n')).toStdString();
}

TEST_F(MenuKeys, KeyboardShortcutsListsEachItemOnce) {
    ui::MainWindow w(library());
    QStringList seen, twice;
    for (const auto& g : ui::help::collectShortcuts(w.menuBar()))
        for (const auto& s : g.items) {
            const QString key = g.title + QStringLiteral(": ") + s.does;
            (seen.contains(key) ? twice : seen) << key;
        }
    EXPECT_TRUE(seen.contains(QStringLiteral("Edit: Undo")));
    EXPECT_TRUE(twice.isEmpty()) << twice.join(QLatin1Char('\n')).toStdString();
}

// OK used to close the window and save nothing when no file was chosen.
TEST_F(MenuKeys, ExportAsImageWaitsForAFile) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QSettings().remove(QStringLiteral("export/path"));
    ui::MainWindow w(library());
    w.show();
    ASSERT_TRUE(w.openFile(QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm")));
    // Opened from a file, it suggests a picture beside it.
    QAction* exportAct = nullptr;
    for (QAction* a : w.findChildren<QAction*>())
        if (plain(a->text()) == QStringLiteral("Export as Image…")) exportAct = a;
    ASSERT_TRUE(exportAct);
    bool checked = false;
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_TRUE(d);
        auto* path = d->findChild<QLineEdit*>(QStringLiteral("exportImagePath"));
        auto* ok = d->findChild<QDialogButtonBox*>(QStringLiteral("exportImageButtons"))->button(QDialogButtonBox::Ok);
        ASSERT_TRUE(path && ok);
        EXPECT_TRUE(path->text().endsWith(QStringLiteral("/tight-corner.png"))) << path->text().toStdString();
        EXPECT_TRUE(QDir::isAbsolutePath(path->text()));
        EXPECT_TRUE(ok->isEnabled());
        path->clear();
        EXPECT_FALSE(ok->isEnabled());
        EXPECT_FALSE(ok->toolTip().isEmpty());
        QTest::keyClick(d, Qt::Key_Return);  // Enter doesn't slip past it
        EXPECT_TRUE(d->isVisible());
        path->setText(QStringLiteral("x.png"));
        EXPECT_TRUE(ok->isEnabled());
        checked = true;
        d->reject();
    });
    exportAct->trigger();
    EXPECT_TRUE(checked);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
}
