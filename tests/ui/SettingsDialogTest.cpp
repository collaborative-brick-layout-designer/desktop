// Edit > Settings...: shows the current settings, every change goes
// straight to the store, the note names the connected server's host, and
// this computer's settings (once Preferences) are in it too, each saved at
// once. No Expert mode switch.

#include "ui/SettingsDialog.h"
#include "ui/TranslationCoverage.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/LegoColors.h"
#include "ServerList.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QSpinBox>
#include <QTimer>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTest>
#include <QToolButton>

using namespace bld::ui;
using namespace bld::ui::theme;

namespace {

class SettingsDialogTest : public ::testing::Test {
protected:
    void SetUp() override { QSettings().remove(kGroup); }
    void TearDown() override { QSettings().remove(kGroup); }
    static constexpr const char* kGroup = "test-settings-dialog";
};

}  // namespace

TEST_F(SettingsDialogTest, ShowsTheSettingsAndChangesThemAtOnce) {
    PrefsStore store(QString::fromLatin1(kGroup));
    AppPrefs p;
    p.accent = QStringLiteral("ocean");
    p.helpIcons = false;
    store.update(p);

    SettingsDialog dialog(store, QString());
    EXPECT_TRUE(dialog.findChild<QToolButton*>(QStringLiteral("theme_system"))->isChecked());
    EXPECT_TRUE(dialog.findChild<QToolButton*>(QStringLiteral("accent_ocean"))->isChecked());
    EXPECT_FALSE(dialog.findChild<QToolButton*>(QStringLiteral("accent_brick"))->isChecked());
    EXPECT_FALSE(dialog.findChild<QCheckBox*>(QStringLiteral("helpIcons"))->isChecked());
    EXPECT_EQ(dialog.findChild<QAbstractButton*>(QStringLiteral("expertMode")), nullptr);

    int changes = 0;
    QObject::connect(&store, &PrefsStore::changed, [&changes](bool local) { changes += local ? 1 : 100; });
    dialog.findChild<QToolButton*>(QStringLiteral("theme_dark"))->click();
    EXPECT_EQ(store.prefs().theme, ThemeChoice::Dark);
    dialog.findChild<QToolButton*>(QStringLiteral("accent_sunny"))->click();
    EXPECT_EQ(store.prefs().accent, QStringLiteral("sunny"));
    dialog.findChild<QCheckBox*>(QStringLiteral("largeText"))->click();
    EXPECT_TRUE(store.prefs().largeText);
    dialog.findChild<QCheckBox*>(QStringLiteral("helpIcons"))->click();
    EXPECT_TRUE(store.prefs().helpIcons);
    EXPECT_EQ(changes, 4);
    EXPECT_EQ(store.prefs().accent, QStringLiteral("sunny"));  // later changes kept the earlier ones

    // A change from elsewhere (a server pull) shows in the open dialog.
    AppPrefs theirs = store.prefs();
    theirs.accent = QStringLiteral("plum");
    store.adopt(theirs);
    EXPECT_TRUE(dialog.findChild<QToolButton*>(QStringLiteral("accent_plum"))->isChecked());
}

// Any LEGO color: a searchable list with LEGO and BrickLink names; picking
// one unticks the presets, and a preset clears it again. How much color is
// the original look until changed.
TEST_F(SettingsDialogTest, AnyLegoColorIsFoundByEitherNameOrNumberAndApplies) {
    PrefsStore store(QString::fromLatin1(kGroup));
    SettingsDialog dialog(store, QString());
    auto* tile = dialog.findChild<QToolButton*>(QStringLiteral("legoColorTile"));
    auto* picker = dialog.findChild<QFrame*>(QStringLiteral("LegoPicker"));
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("legoColorSearch"));
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("legoColorList"));
    ASSERT_TRUE(tile && picker && search && list);
    EXPECT_EQ(tile->text(), QStringLiteral("Any LEGO color"));
    EXPECT_TRUE(picker->isHidden());
    EXPECT_EQ(list->count(), 122);
    EXPECT_TRUE(dialog.findChild<QRadioButton*>(QStringLiteral("colorStyle_original"))->isChecked());
    tile->click();
    EXPECT_FALSE(picker->isHidden());

    const auto shown = [list] {
        QStringList out;
        for (int i = 0; i < list->count(); ++i)
            if (!list->item(i)->isHidden()) out << list->item(i)->data(Qt::AccessibleTextRole).toString();
        return out;
    };
    search->setText(QStringLiteral("dark blue"));  // BrickLink's name for Earth Blue
    EXPECT_TRUE(shown().contains(QStringLiteral("Earth Blue (LEGO 140), BrickLink: Dark Blue (63)")));
    search->setText(QStringLiteral("24"));  // LEGO's number
    EXPECT_EQ(shown(), QStringList{ QStringLiteral("Bright Yellow (LEGO 24), BrickLink: Yellow (3)") });
    search->setText(QStringLiteral("trans clear"));
    auto* none = dialog.findChild<QLabel*>(QStringLiteral("legoColorNone"));
    auto* showAll = dialog.findChild<QPushButton*>(QStringLiteral("legoColorShowAll"));
    EXPECT_FALSE(none->isHidden());
    EXPECT_TRUE(none->text().startsWith(QStringLiteral("No LEGO color matches")));
    showAll->click();
    EXPECT_TRUE(search->text().isEmpty());
    EXPECT_EQ(shown().size(), 122);
    EXPECT_TRUE(none->isHidden());

    search->setText(QStringLiteral("earth blue"));
    QListWidgetItem* earthBlue = nullptr;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toInt() == 140) earthBlue = list->item(i);
    ASSERT_NE(earthBlue, nullptr);
    emit list->itemClicked(earthBlue);
    EXPECT_EQ(store.prefs().legoColor, 140);
    EXPECT_EQ(tile->text(), QStringLiteral("Earth Blue"));
    EXPECT_TRUE(tile->isChecked());
    EXPECT_FALSE(dialog.findChild<QToolButton*>(QStringLiteral("accent_brick"))->isChecked());

    dialog.findChild<QRadioButton*>(QStringLiteral("colorStyle_full"))->click();
    EXPECT_EQ(store.prefs().colorStyle, QStringLiteral("full"));

    dialog.findChild<QToolButton*>(QStringLiteral("accent_ocean"))->click();
    EXPECT_EQ(store.prefs().accent, QStringLiteral("ocean"));
    EXPECT_EQ(store.prefs().legoColor, 0);
    EXPECT_EQ(tile->text(), QStringLiteral("Any LEGO color"));
    EXPECT_FALSE(tile->isChecked());
    EXPECT_EQ(store.prefs().colorStyle, QStringLiteral("full"));  // kept
}

TEST_F(SettingsDialogTest, SnapStrengthIsGentleUntilChanged) {
    PrefsStore store(QString::fromLatin1(kGroup));
    SettingsDialog dialog(store, QString());
    auto* off = dialog.findChild<QRadioButton*>(QStringLiteral("snap_off"));
    auto* gentle = dialog.findChild<QRadioButton*>(QStringLiteral("snap_gentle"));
    auto* strong = dialog.findChild<QRadioButton*>(QStringLiteral("snap_strong"));
    ASSERT_TRUE(off && gentle && strong);
    EXPECT_TRUE(gentle->isChecked());
    strong->click();
    EXPECT_EQ(store.prefs().connectionSnap, QStringLiteral("strong"));
    off->click();
    EXPECT_EQ(store.prefs().connectionSnap, QStringLiteral("off"));
    EXPECT_EQ(PrefsStore(QString::fromLatin1(kGroup)).prefs().connectionSnap, QStringLiteral("off"));  // kept

    AppPrefs theirs = store.prefs();
    theirs.connectionSnap = QStringLiteral("gentle");
    store.adopt(theirs);
    EXPECT_TRUE(gentle->isChecked());
}

TEST_F(SettingsDialogTest, TheNoteNamesTheConnectedServer) {
    PrefsStore store(QString::fromLatin1(kGroup));
    SettingsDialog connected(store, QStringLiteral("layouts.club.example:8443"));
    EXPECT_TRUE(connected.syncNote()->text().contains(QStringLiteral("Synced with layouts.club.example:8443")))
        << connected.syncNote()->text().toStdString();

    SettingsDialog offline(store, QString());
    EXPECT_FALSE(offline.syncNote()->text().contains(QStringLiteral("Synced with")));
    EXPECT_TRUE(offline.syncNote()->text().contains(QStringLiteral("this computer")));
}

// Preferences joined Settings: one window, sections down the side, every
// control saved the moment it changes, and the window told what changed.
TEST_F(SettingsDialogTest, OneWindowWithSectionsForEverySetting) {
    PrefsStore store(QString::fromLatin1(kGroup));
    int folders = 0;
    SettingsDialog dialog(store, QString(), {}, nullptr, {}, [&folders] { ++folders; });
    EXPECT_EQ(dialog.findChild<QPushButton*>(QStringLiteral("moreOptions")), nullptr);
    auto* nav = dialog.findChild<QListWidget*>(QStringLiteral("settingsSections"));
    ASSERT_TRUE(nav);
    QStringList names;
    for (int i = 0; i < nav->count(); ++i) names << nav->item(i)->text();
    EXPECT_EQ(names, (QStringList{ QStringLiteral("Light or dark"), QStringLiteral("Color"), QStringLiteral("Editing"),
                                   QStringLiteral("Map labels"), QStringLiteral("Language"), QStringLiteral("Starting up"),
                                   QStringLiteral("Parts and folders"), QStringLiteral("Bringing in models"),
                                   QStringLiteral("Help"), QStringLiteral("Servers") }));
    dialog.findChild<QPushButton*>(QStringLiteral("partsFolders"))->click();
    EXPECT_EQ(folders, 1);
    // The settings that did nothing are gone (Bug 3): undo depth, splash
    // screen, show grid (each sheet has its own), selection tint and the
    // export watermark (Export as Image has its own).
    QStringList all;
    for (auto* w : dialog.findChildren<QAbstractButton*>()) all << w->text();
    for (auto* w : dialog.findChildren<QLabel*>()) all << w->text();
    for (const char* gone : { "Undo stack", "splash", "Show grid", "Selection tint", "exported pictures", "More options" })
        for (const QString& t : all) EXPECT_FALSE(t.contains(QLatin1String(gone), Qt::CaseInsensitive)) << t.toStdString();
}

namespace {
struct Saved {
    QStringList keys;
    void forget() {
        for (const QString& k : keys) QSettings().remove(k);
    }
    ~Saved() { forget(); }
};
}  // namespace

TEST_F(SettingsDialogTest, EveryComputerSettingSavesAtOnceAndSaysWhatChanged) {
    Saved saved{ { QStringLiteral("editing/snapStepStuds"), QStringLiteral("editing/rotationStepDegrees"),
                   QStringLiteral("general/wheelZoomFactor"), QStringLiteral("appearance/alwaysShowConnections"),
                   QStringLiteral("view/moduleFrameThickness"), QStringLiteral("view/moduleLabelPercent"),
                   QStringLiteral("venue/labelPx"), QStringLiteral("general/reopenLastFile"),
                   QStringLiteral("updates/checkAtStartup"), QStringLiteral("parts/showAllServers"),
                   QStringLiteral("general/language") } };
    saved.forget();
    PrefsStore store(QString::fromLatin1(kGroup));
    QList<settings::Change> heard;
    SettingsDialog dialog(store, QString(), [&heard](settings::Change c) { heard << c; });
    const auto combo = [&](const char* key) { return dialog.findChild<QComboBox*>(QString::fromLatin1(key)); };
    const auto box = [&](const char* key) { return dialog.findChild<QCheckBox*>(QString::fromLatin1(key)); };

    combo("editing/snapStepStuds")->setCurrentIndex(combo("editing/snapStepStuds")->findData(8.0));
    EXPECT_EQ(QSettings().value(QStringLiteral("editing/snapStepStuds")).toDouble(), 8.0);
    combo("editing/rotationStepDegrees")->setCurrentIndex(combo("editing/rotationStepDegrees")->findData(45.0));
    EXPECT_EQ(QSettings().value(QStringLiteral("editing/rotationStepDegrees")).toDouble(), 45.0);
    EXPECT_EQ(combo("general/wheelZoomFactor")->currentData().toDouble(), settings::kZoomNormal);
    combo("general/wheelZoomFactor")->setCurrentIndex(combo("general/wheelZoomFactor")->findData(settings::kZoomFast));
    EXPECT_EQ(QSettings().value(QStringLiteral("general/wheelZoomFactor")).toDouble(), settings::kZoomFast);
    box("appearance/alwaysShowConnections")->click();
    EXPECT_TRUE(QSettings().value(QStringLiteral("appearance/alwaysShowConnections")).toBool());
    dialog.findChild<QDoubleSpinBox*>(QStringLiteral("view/moduleFrameThickness"))->setValue(8);
    EXPECT_EQ(QSettings().value(QStringLiteral("view/moduleFrameThickness")).toDouble(), 8.0);
    dialog.findChild<QDoubleSpinBox*>(QStringLiteral("view/moduleLabelPercent"))->setValue(50);
    EXPECT_EQ(QSettings().value(QStringLiteral("view/moduleLabelPercent")).toDouble(), 50.0);
    dialog.findChild<QSpinBox*>(QStringLiteral("venue/labelPx"))->setValue(40);
    EXPECT_EQ(QSettings().value(QStringLiteral("venue/labelPx")).toInt(), 40);
    EXPECT_TRUE(box("general/reopenLastFile")->isChecked());
    box("general/reopenLastFile")->click();
    EXPECT_FALSE(QSettings().value(QStringLiteral("general/reopenLastFile")).toBool());
    box("updates/checkAtStartup")->click();
    EXPECT_FALSE(QSettings().value(QStringLiteral("updates/checkAtStartup")).toBool());
    // Other servers' parts are listed (grayed) while live, until this is unticked.
    EXPECT_TRUE(box("parts/showAllServers")->isChecked());
    box("parts/showAllServers")->click();
    EXPECT_FALSE(QSettings().value(QStringLiteral("parts/showAllServers"), true).toBool());

    using C = settings::Change;
    EXPECT_EQ(heard, (QList<C>{ C::SnapStep, C::TurnStep, C::ZoomSpeed, C::MapLook, C::MapLook, C::MapLook, C::MapLook,
                                C::PartsList }));
}

// Bug 6: unset, the picker showed English and OK wrote "en", so a German
// computer got English from then on. Unset is "Match my computer" now.
TEST_F(SettingsDialogTest, TheLanguageMatchesTheComputerUntilOneIsPicked) {
    Saved saved{ { QStringLiteral("general/language") } };
    saved.forget();
    PrefsStore store(QString::fromLatin1(kGroup));
    {
        SettingsDialog dialog(store, QString());
        auto* lang = dialog.findChild<QComboBox*>(QStringLiteral("general/language"));
        ASSERT_TRUE(lang);
        EXPECT_EQ(lang->currentText(), QStringLiteral("Match my computer"));
        EXPECT_TRUE(lang->currentData().toString().isEmpty());
        lang->setCurrentIndex(lang->findData(QStringLiteral("de")));
        EXPECT_EQ(QSettings().value(QStringLiteral("general/language")).toString(), QStringLiteral("de"));
        lang->setCurrentIndex(0);
        EXPECT_EQ(QSettings().value(QStringLiteral("general/language")).toString(), QString());
    }
    QSettings().setValue(QStringLiteral("general/language"), QStringLiteral("ja"));
    SettingsDialog again(store, QString());
    // A language that isn't finished says how far it got.
    const QString ja = again.findChild<QComboBox*>(QStringLiteral("general/language"))->currentText();
    EXPECT_TRUE(ja.startsWith(QStringLiteral("日本語"))) << ja.toStdString();
    const int done = translationPercent(QStringLiteral("ja"));
    EXPECT_EQ(ja, done < 100 ? QStringLiteral("日本語 (%1% translated)").arg(done) : QStringLiteral("日本語"));
}

// Bug 1 (the color kept a reference to a gone local) stays fixed here too.
TEST_F(SettingsDialogTest, ThePaintColorStartsAtTheSavedOneAndSavesThePickedOne) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    Saved saved{ { QStringLiteral("editing/paintColor") } };
    QSettings().setValue(QStringLiteral("editing/paintColor"), QStringLiteral("#ff008000"));
    PrefsStore store(QString::fromLatin1(kGroup));
    QList<settings::Change> heard;
    SettingsDialog dialog(store, QString(), [&heard](settings::Change c) { heard << c; });
    auto* color = dialog.findChild<QPushButton*>(QStringLiteral("paintColorButton"));
    ASSERT_TRUE(color);
    EXPECT_EQ(color->text(), QStringLiteral("#ff008000"));
    QString startedAt;
    QTimer::singleShot(0, [&] {
        auto* cd = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
        ASSERT_TRUE(cd);
        startedAt = cd->currentColor().name(QColor::HexArgb);
        cd->setCurrentColor(QColor(255, 0, 0));
        cd->accept();
    });
    color->click();
    EXPECT_EQ(startedAt, QStringLiteral("#ff008000"));
    EXPECT_EQ(color->text(), QStringLiteral("#ffff0000"));
    EXPECT_EQ(QSettings().value(QStringLiteral("editing/paintColor")).toString(), QStringLiteral("#ffff0000"));
    EXPECT_EQ(heard, QList<settings::Change>{ settings::Change::PaintColor });
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
}

TEST_F(SettingsDialogTest, ServersSaysWhatIsSetUpAndOpensServers) {
    const QVariant before = QSettings().value(QLatin1String(bld::sync::ServerList::kKey));
    QSettings().setValue(QLatin1String(bld::sync::ServerList::kKey), QByteArray("[]"));
    PrefsStore store(QString::fromLatin1(kGroup));
    {
        SettingsDialog dlg(store, QString());
        auto* note = dlg.findChild<QLabel*>(QStringLiteral("serversNote"));
        auto* manage = dlg.findChild<QPushButton*>(QStringLiteral("manageServers"));
        ASSERT_NE(note, nullptr);
        ASSERT_NE(manage, nullptr);
        EXPECT_TRUE(note->text().startsWith(QStringLiteral("No servers yet")));
        EXPECT_TRUE(manage->isHidden());  // nothing to open it with

        // Opened from the app: the button opens Servers; a server added there shows at once.
        int opened = 0;
        dlg.setManageServers([&opened] {
            ++opened;
            bld::sync::ServerList list;
            list.add(QUrl(QStringLiteral("https://layouts.example.org")), QStringLiteral("Club server"));
            list.add(QUrl(QStringLiteral("https://other.example.org")), QString());
            list.save();
        });
        EXPECT_FALSE(manage->isHidden());
        EXPECT_EQ(manage->text(), QStringLiteral("Add a server…"));
        manage->click();
        ASSERT_TRUE(QTest::qWaitFor([&] { return opened == 1; }, 2000));
        EXPECT_EQ(note->text(), QStringLiteral("Main: Club server (layouts.example.org) and 1 more"));
        EXPECT_EQ(manage->text(), QStringLiteral("Manage servers…"));
    }
    QSettings().setValue(QLatin1String(bld::sync::ServerList::kKey), before);
}
