// The README screenshots, taken on request (skipped otherwise):
//   BLD_README_SHOTS=<out dir>   where the PNGs go
//   BLD_README_LIBS=<folder>     the part libraries, as the Download Center installs them
//                                (beside the bundled BlueBrickParts library)
//   BLD_README_LAYOUT=<.bbm>     the layout shown in the editor
//   BLD_README_VENUE=<.bld-venue> the venue shown in the Venue Designer
// Each in the dark and the light theme, set through the app's own theme
// setting. Fails when a part on the map is in no library.

#include "ui/LibraryPathsDialog.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"
#include "ui/venue/VenueDesignerDialog.h"
#include "ui/venue/VenueDesignerView.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QStatusBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using namespace bld;

TEST(ReadmeShots, EditorAndVenueInBothThemes) {
    const QString out = qEnvironmentVariable("BLD_README_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_README_SHOTS not set";
    const QString libs = qEnvironmentVariable("BLD_README_LIBS");
    const QString layout = qEnvironmentVariable("BLD_README_LAYOUT");
    const QString venueFile = qEnvironmentVariable("BLD_README_VENUE");
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    {
        QSettings s;
        s.beginGroup(QLatin1String(ui::LibraryPathsDialog::kSettingsGroup));
        s.setValue(QLatin1String(ui::LibraryPathsDialog::kSettingsKey), QStringList{ QStringLiteral(BLD_PARTS_LIBRARY_ROOT), libs });
    }
    namespace th = ui::theme;
    th::ThemeManager themer(th::PrefsStore::instance());
    for (const auto choice : { th::ThemeChoice::Dark, th::ThemeChoice::Light }) {
        const QString name = choice == th::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        th::AppPrefs p = th::PrefsStore::instance().prefs();
        p.theme = choice;
        th::PrefsStore::instance().update(p);
        themer.apply();

        parts::PartsLibrary parts;
        ui::MainWindow window(parts);
        window.resize(1440, 900);
        window.show();
        ASSERT_TRUE(window.openFile(layout));
        auto* view = window.findChild<ui::MapView*>();
        ASSERT_TRUE(view && view->currentMap());
        int missing = 0;
        for (const auto& L : view->currentMap()->layers())
            if (L && L->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                    if (!parts.metadata(b.partNumber) || parts.pixmap(b.partNumber).isNull()) {
                        ++missing;
                        ADD_FAILURE() << "missing part " << b.partNumber.toStdString();
                    }
        EXPECT_EQ(missing, 0);
        QTest::qWait(500);
        QMetaObject::invokeMethod(&window, "onFitToView", Qt::DirectConnection);
        QTest::qWait(500);
        window.statusBar()->clearMessage();  // "Opened <the file's path>"
        window.grab().save(out + QStringLiteral("/desktop-editor-%1.png").arg(name));

        const auto venue = saveload::readVenueFile(venueFile);
        ASSERT_TRUE(venue);
        core::Venue v = *venue;
        v.name = QStringLiteral("Grand Lobby");
        ui::VenueDesignerDialog dlg(v, QObject::tr("Venue in your library"), QObject::tr("Save"),
                                    [](const core::Venue&) { return QString(); });
        dlg.resize(1440, 900);
        dlg.show();
        // As the web's shot: measurements hidden, the plan itself shown.
        for (auto* box : dlg.findChildren<QAbstractButton*>())
            if (box->isCheckable() && box->text().remove(QLatin1Char('&')) == QLatin1String("Measurements") && box->isChecked())
                box->click();
        QTest::qWait(300);
        dlg.findChild<ui::VenueDesignerView*>()->fit();
        QTest::qWait(300);
        dlg.grab().save(out + QStringLiteral("/desktop-venue-%1.png").arg(name));
    }
}
