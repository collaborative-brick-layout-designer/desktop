// File › Version History… on the desktop: the live layout's versions as the
// server keeps them (newest first, when, why, who, parts), Keep a Version
// Now with a name, Restore after asking (the server keeps the layout as it
// was first), Copy as a New Layout, the website's page for pictures, and an
// older server without history said in plain words, never an error box.

#include "FakeHttp.h"

#include "LibraryApi.h"
#include "ui/VersionHistoryDialog.h"
#include "ui/tours/Tours.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>

using namespace bld;
using bld::synctest::FakeHttp;

namespace {

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

const QByteArray kVersions = QByteArrayLiteral("/api/layouts/L1/versions");

QJsonObject version(const QString& id, const QString& reason, const QString& label, qint64 at, int parts) {
    return QJsonObject{
        { QStringLiteral("id"), id },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("title"), QStringLiteral("Yard") },
        { QStringLiteral("label"), label.isEmpty() ? QJsonValue() : QJsonValue(label) },
        { QStringLiteral("createdAt"), static_cast<double>(at) },
        { QStringLiteral("createdBy"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("u1") }, { QStringLiteral("name"), QStringLiteral("Ann") } } },
        { QStringLiteral("editors"), QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("u2") }, { QStringLiteral("name"), QStringLiteral("Bob") } } } },
        { QStringLiteral("summary"), QJsonObject{ { QStringLiteral("parts"), parts }, { QStringLiteral("sheets"), 2 } } },
    };
}

QJsonObject list(bool canRestore) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return QJsonObject{
        { QStringLiteral("canRestore"), canRestore },
        { QStringLiteral("versions"), QJsonArray{ version(QStringLiteral("v2"), QStringLiteral("auto"), {}, now - 60000, 12),
                                                  version(QStringLiteral("v1"), QStringLiteral("manual"), QStringLiteral("Start"), now - 3600000, 1) } },
    };
}

class VersionHistory : public ::testing::Test {
protected:
    void SetUp() override {
        api_.setBase(http_.base());
        api_.setToken(QStringLiteral("bld_pat_test"));
        ui::VersionHistoryDialog::confirmRestore = [this](QWidget*, const QString&) { ++asked_; return true; };
        ui::VersionHistoryDialog::askName = [](QWidget*, QString* name) { *name = QStringLiteral("Before the show"); return true; };
        ui::VersionHistoryDialog::openLink = [this](const QUrl& u) { opened_ = u; };
    }
    QListWidget* listOf(ui::VersionHistoryDialog& d) { return d.findChild<QListWidget*>(QStringLiteral("versionHistoryList")); }
    QString statusOf(ui::VersionHistoryDialog& d) { return d.findChild<QLabel*>(QStringLiteral("versionHistoryStatus"))->text(); }
    QPushButton* button(ui::VersionHistoryDialog& d, const char* name) { return d.findChild<QPushButton*>(QString::fromLatin1(name)); }
    const synctest::Request* last(const QByteArray& method, const QByteArray& path) {
        for (auto it = http_.requests.rbegin(); it != http_.requests.rend(); ++it)
            if (it->method == method && it->path == path) return &*it;
        return nullptr;
    }

    FakeHttp http_;
    sync::LibraryApi api_;
    int asked_ = 0;
    QUrl opened_;
};

} // namespace

TEST_F(VersionHistory, ListsTheVersionsNewestFirstWithWhenWhyWhoAndParts) {
    http_.reply(kVersions, 200, list(true));
    ui::VersionHistoryDialog d(api_, QStringLiteral("L1"), QStringLiteral("Yard"));
    ASSERT_TRUE(waitFor([&] { return listOf(d)->count() == 2; }));
    const QString first = listOf(d)->item(0)->text();
    EXPECT_TRUE(first.contains(QStringLiteral("While editing"))) << first.toStdString();
    EXPECT_TRUE(first.contains(QStringLiteral("Bob")));
    EXPECT_TRUE(first.contains(QStringLiteral("12 part")));
    EXPECT_TRUE(listOf(d)->item(1)->text().contains(QStringLiteral("“Start”")));
    EXPECT_TRUE(listOf(d)->item(1)->text().contains(QStringLiteral("\n1 part")));
    EXPECT_FALSE(button(d, "versionHistoryRestore")->isHidden());
    // Pictures: the website's page for the layout's history.
    button(d, "versionHistoryWeb")->click();
    EXPECT_EQ(opened_.path(), QStringLiteral("/editor/L1/history"));
}

TEST_F(VersionHistory, KeepRestoreAndCopyAskTheServer) {
    http_.reply(kVersions, 200, list(true));
    http_.replyTo("POST", kVersions, 201, QJsonObject{ { QStringLiteral("id"), QStringLiteral("v3") } });
    http_.reply("/api/layouts/L1/versions/v1/restore", 200, QJsonObject{ { QStringLiteral("ok"), true }, { QStringLiteral("safety"), QStringLiteral("v4") } });
    http_.reply("/api/layouts/L1/versions/v1/copy", 201, QJsonObject{ { QStringLiteral("id"), QStringLiteral("L9") }, { QStringLiteral("title"), QStringLiteral("Yard (earlier version)") } });
    ui::VersionHistoryDialog d(api_, QStringLiteral("L1"), QStringLiteral("Yard"));
    ASSERT_TRUE(waitFor([&] { return listOf(d)->count() == 2; }));

    button(d, "versionHistoryKeep")->click();
    ASSERT_TRUE(waitFor([&] { return last("POST", kVersions) != nullptr; }));
    const QJsonObject body = QJsonDocument::fromJson(last("POST", kVersions)->body).object();
    EXPECT_EQ(body.value(QLatin1String("reason")).toString(), QStringLiteral("manual"));
    EXPECT_EQ(body.value(QLatin1String("label")).toString(), QStringLiteral("Before the show"));
    ASSERT_TRUE(waitFor([&] { return statusOf(d).contains(QStringLiteral("Kept a version")); }));

    // The list is fetched again after keeping one: then Restore is ready.
    ASSERT_TRUE(waitFor([&] { return button(d, "versionHistoryRestore")->isEnabled(); }));
    listOf(d)->setCurrentRow(1);
    button(d, "versionHistoryRestore")->click();
    EXPECT_EQ(asked_, 1);
    ASSERT_TRUE(waitFor([&] { return statusOf(d).contains(QStringLiteral("back to how it was")); }));
    EXPECT_TRUE(statusOf(d).contains(QStringLiteral("Before a restore")));

    QString copiedId;
    QObject::connect(&d, &ui::VersionHistoryDialog::copied, [&](const QString& id, const QString&) { copiedId = id; });
    ASSERT_TRUE(waitFor([&] { return button(d, "versionHistoryCopy")->isEnabled(); }));
    listOf(d)->setCurrentRow(1);
    button(d, "versionHistoryCopy")->click();
    ASSERT_TRUE(waitFor([&] { return !copiedId.isEmpty(); }));
    EXPECT_EQ(copiedId, QStringLiteral("L9"));
    EXPECT_TRUE(statusOf(d).contains(QStringLiteral("Yard (earlier version)")));
    // Each with the desktop's sign-in.
    EXPECT_EQ(last("POST", "/api/layouts/L1/versions/v1/copy")->authorization, QByteArray("Bearer bld_pat_test"));
}

TEST_F(VersionHistory, AViewerCanCopyButNotRestoreOrKeep) {
    http_.reply(kVersions, 200, list(false));
    ui::VersionHistoryDialog d(api_, QStringLiteral("L1"), QStringLiteral("Yard"));
    ASSERT_TRUE(waitFor([&] { return listOf(d)->count() == 2; }));
    EXPECT_TRUE(button(d, "versionHistoryRestore")->isHidden());
    EXPECT_TRUE(button(d, "versionHistoryKeep")->isHidden());
    EXPECT_TRUE(button(d, "versionHistoryCopy")->isEnabled());
    EXPECT_TRUE(statusOf(d).contains(QStringLiteral("Only the layout's editors")));
}

TEST_F(VersionHistory, AnOlderServerWithoutHistoryIsSaidPlainly) {
    ui::VersionHistoryDialog d(api_, QStringLiteral("L1"), QStringLiteral("Yard"));
    ASSERT_TRUE(waitFor([&] { return statusOf(d).contains(QStringLiteral("doesn't keep earlier versions")); }));
    EXPECT_FALSE(button(d, "versionHistoryCopy")->isEnabled());
}

TEST_F(VersionHistory, TheTourPointsAtTheListAndTheThreeButtonsAndStartsFromShowMeAround) {
    http_.reply(kVersions, 200, list(true));
    ui::VersionHistoryDialog d(api_, QStringLiteral("L1"), QStringLiteral("Yard"));
    d.show();
    ASSERT_TRUE(waitFor([&] { return listOf(d)->count() == 2; }));
    for (const auto& step : ui::tours::findTour(QStringLiteral("history"))->steps)
        EXPECT_NE(ui::tours::findTarget(&d, step.target), nullptr) << step.target.toStdString();
    button(d, "versionHistoryTour")->click();
    auto* overlay = d.findChild<ui::tours::TourOverlay*>();
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->tour().id, QStringLiteral("history"));
    overlay->finish();
}

TEST_F(VersionHistory, SwitchedOffItSaysSoKeepsNothingAndStillCopiesOut) {
    QJsonObject off = list(true);
    off.insert(QStringLiteral("enabled"), false);
    http_.reply(kVersions, 200, off);
    ui::VersionHistoryDialog d(api_, QStringLiteral("L1"), QStringLiteral("Yard"));
    d.show();
    ASSERT_TRUE(waitFor([&] { return listOf(d)->count() == 2; }));
    auto* note = d.findChild<QLabel*>(QStringLiteral("versionHistoryOff"));
    ASSERT_NE(note, nullptr);
    EXPECT_TRUE(note->isVisible());
    EXPECT_TRUE(note->text().contains(QStringLiteral("switched off")));
    EXPECT_FALSE(button(d, "versionHistoryKeep")->isVisible());
    EXPECT_FALSE(button(d, "versionHistoryRestore")->isVisible());
    EXPECT_TRUE(button(d, "versionHistoryCopy")->isEnabled());
}

TEST_F(VersionHistory, TheStatusSaysWhetherTheServerKeepsNewVersions) {
    http_.reply("/api/version-history/status", 200, QJsonObject{ { QStringLiteral("enabled"), false } });
    int answered = -1;
    api_.versionHistoryStatus([&](bool on) { answered = on ? 1 : 0; }, [&](const sync::ServerRefusal&) { answered = 2; });
    ASSERT_TRUE(waitFor([&] { return answered != -1; }));
    EXPECT_EQ(answered, 0);
}

TEST_F(VersionHistory, AnOlderServerHasNoStatusPageAndNothingIsAssumed) {
    http_.reply("/api/version-history/status", 404, QJsonObject{ { QStringLiteral("error"), QStringLiteral("not_found") } });
    int answered = -1;
    api_.versionHistoryStatus([&](bool on) { answered = on ? 1 : 0; }, [&](const sync::ServerRefusal&) { answered = 2; });
    ASSERT_TRUE(waitFor([&] { return answered != -1; }));
    EXPECT_EQ(answered, 2);
}
