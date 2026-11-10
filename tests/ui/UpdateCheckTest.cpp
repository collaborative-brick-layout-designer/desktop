// Version comparison for the update check.

#include "ui/UpdateCheck.h"
#include "core/Version.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <gtest/gtest.h>

using bld::ui::isNewerVersion;

TEST(UpdateCheck, ComparesReleaseVersions) {
    EXPECT_TRUE(isNewerVersion(QStringLiteral("v1.3.0"), QStringLiteral("1.2.0")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.10.0"), QStringLiteral("1.9.9")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("2.0"), QStringLiteral("1.99.99")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("v1.2.0"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.2"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.1.9"), QStringLiteral("1.2.0")));
}

TEST(UpdateCheck, PreReleasesAndJunk) {
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.3.0"), QStringLiteral("1.3.0-beta.2")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.3.0-beta.1"), QStringLiteral("1.3.0")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.4.0-rc.1"), QStringLiteral("1.3.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("nightly"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.3.0"), QStringLiteral("")));
    EXPECT_FALSE(isNewerVersion(QString(), QStringLiteral("1.2.0")));
}

// A nightly is stamped with the release it follows plus its run number:
// newer than that release, so it isn't told to "update" to it, and older
// than the next one, which it is offered.
TEST(UpdateCheck, NightlyVersions) {
    EXPECT_FALSE(isNewerVersion(QStringLiteral("v2.0.12"), QStringLiteral("2.0.12.345")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("2.0.11"), QStringLiteral("2.0.12.345")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("v2.0.13"), QStringLiteral("2.0.12.345")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("2.0.12.346"), QStringLiteral("2.0.12.345")));
}

// About says which version this is, and that a nightly is one.
TEST(UpdateCheck, AboutVersionLine) {
    using bld::ui::versionLine;
    EXPECT_EQ(versionLine(QStringLiteral("2.0.12"), QString()), QStringLiteral("Version 2.0.12"));
    EXPECT_EQ(versionLine(QStringLiteral("2.0.12.345"), QStringLiteral("nightly")),
              QStringLiteral("Version 2.0.12.345, a nightly build"));
    EXPECT_EQ(versionLine(QString(), QString()), QStringLiteral("Version dev"));
}

// About's "Third-party licenses" opens the installed notices, or the copy on
// GitHub when the app runs from its build folder: never a dead link.
TEST(UpdateCheck, ThirdPartyNoticesLink) {
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/THIRD-PARTY-NOTICES.txt");
    ASSERT_FALSE(QFileInfo::exists(beside));
    EXPECT_EQ(bld::core::thirdPartyNoticesUrl(),
              QStringLiteral("https://github.com/collaborative-brick-layout-designer/desktop/blob/main/THIRD-PARTY-NOTICES.txt"));
    {
        QFile f(beside);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("notices\n");
    }
    EXPECT_EQ(bld::core::thirdPartyNoticesUrl(), QUrl::fromLocalFile(QFileInfo(beside).absoluteFilePath()).toString());
    QFile::remove(beside);
    // The file itself is in the repo, names what is linked, and is current
    // (CI's --check of scripts/gen-third-party-notices.py).
    QFile notices(QStringLiteral(BLD_SOURCE_DIR "/THIRD-PARTY-NOTICES.txt"));
    ASSERT_TRUE(notices.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(notices.readAll());
    for (const char* name : { "QtKeychain", "yffi 0.28.0", "yrs 0.28.0", "Figtree", "Liberation Sans" })
        EXPECT_TRUE(text.contains(QLatin1String(name))) << name;
}

// ---------------------------------------------------------------------------
// The update notice: a friendly card over the map, not a box.

#include "ui/MainWindow.h"
#include "ui/NoticeArea.h"
#include "parts/PartsLibrary.h"

#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

using namespace bld;

namespace {

ui::ReleaseInfo release(const char* version, bool important = false) {
    ui::ReleaseInfo r;
    r.version = QString::fromLatin1(version);
    r.url = QStringLiteral("https://example.org/release");
    r.notes = QStringLiteral("## What's new\n\n- Faster loading");
    r.important = important;
    return r;
}

QStringList buttonsOf(QWidget* card) {
    QStringList out;
    for (auto* b : card->findChildren<QPushButton*>()) out << b->text();
    return out;
}

QString titleOf(QWidget* card) { return card->findChild<QLabel*>(QStringLiteral("NoticeTitle"))->text(); }

bool waitFor(const std::function<bool()>& done) {
    for (int i = 0; i < 200 && !done(); ++i) QTest::qWait(10);
    return done();
}

}  // namespace

TEST(UpdateCheck, ReadsAReleaseAndItsImportantMarker) {
    const ui::ReleaseInfo r = ui::readRelease(
        QJsonObject{ { QStringLiteral("tag_name"), QStringLiteral("v1.3.1") },
                     { QStringLiteral("body"), QStringLiteral("Fixes a sign-in problem. [Security]\n") } });
    EXPECT_EQ(r.version, QStringLiteral("1.3.1"));
    EXPECT_TRUE(r.important);
    EXPECT_EQ(r.notes, QStringLiteral("Fixes a sign-in problem. [Security]"));
    EXPECT_EQ(r.url, QStringLiteral("https://github.com/collaborative-brick-layout-designer/desktop/releases/latest"));
    // Only a web page is kept as the release's link.
    EXPECT_EQ(ui::readRelease(QJsonObject{ { QStringLiteral("html_url"), QStringLiteral("file:///tmp/x") } }).url,
              QStringLiteral("https://github.com/collaborative-brick-layout-designer/desktop/releases/latest"));
    EXPECT_EQ(ui::readRelease(QJsonObject{ { QStringLiteral("html_url"), QStringLiteral("https://github.com/a/b/releases/tag/v2") } }).url,
              QStringLiteral("https://github.com/a/b/releases/tag/v2"));
    EXPECT_TRUE(ui::isImportantRelease(QStringLiteral("[important] please update")));
    EXPECT_FALSE(ui::isImportantRelease(QStringLiteral("An important new tool")));
}

TEST(UpdateCheck, OffersNewerReleasesUnlessSkipped) {
    QStandardPaths::setTestModeEnabled(true);
    QSettings().remove(QStringLiteral("updates/skippedVersion"));
    QWidget parent;
    ui::UpdateCheck check(&parent);
    QSignalSpy offered(&check, &ui::UpdateCheck::updateAvailable);
    check.offer(release("1.3.0"), QStringLiteral("1.3.0"));
    check.offer(release("1.2.0"), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 0);
    check.offer(release("1.4.0"), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 1);
    ui::UpdateCheck::skipVersion(QStringLiteral("1.4.0"));
    check.offer(release("1.4.0"), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 1);
    // A newer one than the skipped version, or an important fix, still comes.
    check.offer(release("1.4.1"), QStringLiteral("1.3.0"));
    check.offer(release("1.4.0", true), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 3);
    QSettings().remove(QStringLiteral("updates/skippedVersion"));
}

TEST(UpdateCheck, ShowsANoticeWithDownloadSkipAndLater) {
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    QSettings().remove(QStringLiteral("updates/skippedVersion"));
    parts::PartsLibrary lib;
    ui::MainWindow window(lib);
    window.resize(1200, 800);
    window.show();
    auto* check = window.findChild<ui::UpdateCheck*>();
    ASSERT_NE(check, nullptr);
    check->offer(release("9.0.0"), QStringLiteral("1.3.0"));
    QWidget* card = window.notices()->card(QStringLiteral("update"));
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->isVisible());
    EXPECT_EQ(titleOf(card), QStringLiteral("Version 9.0.0 is ready"));
    EXPECT_EQ(buttonsOf(card), (QStringList{ QStringLiteral("What's new"), QStringLiteral("Download"),
                                             QStringLiteral("Skip This Version"), QStringLiteral("Later") }));
    // Not a modal box: nothing waits on it.
    EXPECT_EQ(QApplication::activeModalWidget(), nullptr);
    // What's new shows the notes under the card.
    auto* notes = card->findChild<QScrollArea*>(QStringLiteral("NoticeDetails"));
    ASSERT_NE(notes, nullptr);
    EXPECT_FALSE(notes->isVisible());
    card->findChild<QPushButton*>(QStringLiteral("NoticeWhatsNew"))->click();
    EXPECT_TRUE(notes->isVisible());
    // Skip this version: remembered, and the card goes.
    for (auto* b : card->findChildren<QPushButton*>())
        if (b->text() == QStringLiteral("Skip This Version")) b->click();
    ASSERT_TRUE(waitFor([&] { return !window.notices()->isShown(QStringLiteral("update")); }));
    EXPECT_EQ(ui::UpdateCheck::skippedVersion(), QStringLiteral("9.0.0"));
    QSettings().remove(QStringLiteral("updates/skippedVersion"));

    // An important update says so and can't be skipped.
    check->offer(release("9.0.1", true), QStringLiteral("1.3.0"));
    card = window.notices()->card(QStringLiteral("update"));
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(titleOf(card), QStringLiteral("Important: Version 9.0.1 is ready"));
    EXPECT_TRUE(card->property("important").toBool());
    EXPECT_FALSE(buttonsOf(card).contains(QStringLiteral("Skip This Version")));
    // The ✕ hides it.
    card->findChild<QToolButton*>(QStringLiteral("NoticeClose"))->click();
    ASSERT_TRUE(waitFor([&] { return !window.notices()->isShown(QStringLiteral("update")); }));
}
