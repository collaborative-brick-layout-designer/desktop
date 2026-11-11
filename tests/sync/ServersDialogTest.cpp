// File › Servers… against two in-process servers: each row says who is
// signed in, the version and what the server can't do yet; each server is
// only ever sent its own token; sign out, remove, Main and sign in act on
// one server alone.

#include "FakeHttp.h"
#include "ServerList.h"
#include "ServersDialog.h"
#include "TokenStore.h"
#include "ui/ConfirmDialog.h"
#include "ui/help/HelpButton.h"
#include "ui/help/HelpTexts.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QTranslator>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>

#include <functional>
#include <memory>
#include <optional>

using namespace bld::sync;
using bld::synctest::FakeHttp;

namespace {

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

QJsonObject version(const QString& v, const std::optional<QStringList>& features = std::nullopt) {
    QJsonObject o{ { QStringLiteral("version"), v },
                   { QStringLiteral("schemaVersion"), 1 },
                   { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } } };
    if (features) o.insert(QStringLiteral("features"), QJsonArray::fromStringList(*features));
    return o;
}

QJsonObject user(const QString& name) {
    return { { QStringLiteral("user"),
               QJsonObject{ { QStringLiteral("id"), name.toLower() }, { QStringLiteral("displayName"), name } } } };
}

// The same, from a server that says how long the sign-in lasts: 90 days
// unused, and `longest` at the latest.
QJsonObject userWithLifetime(const QString& name, const QDateTime& longest) {
    QJsonObject o = user(name);
    o.insert(QStringLiteral("token"),
             QJsonObject{ { QStringLiteral("idleSeconds"), 90 * 24 * 3600 },
                          { QStringLiteral("longestAt"), static_cast<double>(longest.toMSecsSinceEpoch()) } });
    return o;
}

// The DELETE /api/tokens/current requests a server got (Sign Out).
std::vector<bld::synctest::Request> signOuts(const FakeHttp& http) {
    std::vector<bld::synctest::Request> out;
    for (const auto& r : http.requests)
        if (r.method == "DELETE" && r.path == "/api/tokens/current") out.push_back(r);
    return out;
}

// Every request each server got carried no token or its own.
void expectOnlyOwnToken(const FakeHttp& http, const QByteArray& own) {
    for (const auto& r : http.requests)
        EXPECT_TRUE(r.authorization.isEmpty() || r.authorization == "Bearer " + own)
            << r.path.toStdString() << " got " << r.authorization.toStdString();
}

class ServersDialogTest : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings s;
        s.remove(QLatin1String(ServerList::kLegacyAddressKey));
        ServerList list;
        list.add(club.base(), QStringLiteral("Train club"));
        list.add(pal.base());
        list.save();
        // The club lists every feature; the friend's server is older.
        club.reply("/api/version", 200, version(QStringLiteral("2.4.0"), desktopFeatures()));
        pal.reply("/api/version", 200,
                  version(QStringLiteral("2.0.1"), QStringList{ QStringLiteral("liveSync"), QStringLiteral("signIn"),
                                                                QStringLiteral("publish") }));
        club.reply("/api/tokens/current", 200, user(QStringLiteral("Ann")));
        pal.reply("/api/tokens/current", 200, user(QStringLiteral("Bob")));
        tokens.save(club.base(), QStringLiteral("bld_pat_club"));
        tokens.save(pal.base(), QStringLiteral("bld_pat_pal"));
    }
    void TearDown() override { QSettings().setValue(QLatin1String(ServerList::kKey), QByteArray("[]")); }

    std::unique_ptr<ServersDialog> open() {
        auto d = std::make_unique<ServersDialog>(tokens, [](const QUrl&) {});
        dialog = d.get();
        return d;
    }
    bool settled() {
        return dialog->isSignedIn(club.base()) && dialog->isSignedIn(pal.base())
               && !dialog->statusText(club.base()).contains(QStringLiteral("Checking"))
               && dialog->statusText(pal.base()).contains(QStringLiteral("version"))
               && dialog->statusText(club.base()).contains(QStringLiteral("version"));
    }

    FakeHttp club, pal;
    MemoryTokenStore tokens;
    ServersDialog* dialog = nullptr;
};

}  // namespace

TEST_F(ServersDialogTest, EachRowSaysWhoIsSignedInTheVersionAndWhatIsMissing) {
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    EXPECT_EQ(dialog->statusText(club.base()), QStringLiteral("Signed in as Ann · version 2.4.0"));
    EXPECT_EQ(dialog->statusText(pal.base()), QStringLiteral("Signed in as Bob · version 2.0.1"));
    EXPECT_TRUE(dialog->missingText(club.base()).isEmpty());
    const QString missing = dialog->missingText(pal.base());
    EXPECT_TRUE(missing.contains(featureLabel(QStringLiteral("venues")))) << missing.toStdString();

    // The rows show the same words, and Main on the club only.
    auto* rows = dialog->findChild<QListWidget*>(QStringLiteral("serverRows"));
    ASSERT_EQ(rows->count(), 2);
    QWidget* first = rows->itemWidget(rows->item(0));
    EXPECT_EQ(first->findChild<QLabel*>(QStringLiteral("serverName"))->text(), QStringLiteral("Train club"));
    EXPECT_FALSE(first->findChild<QLabel*>(QStringLiteral("mainBadge"))->isHidden());
    EXPECT_EQ(first->findChild<QLabel*>(QStringLiteral("serverStatus"))->text(), dialog->statusText(club.base()));
    QWidget* second = rows->itemWidget(rows->item(1));
    EXPECT_TRUE(second->findChild<QLabel*>(QStringLiteral("mainBadge"))->isHidden());
    EXPECT_EQ(second->findChild<QLabel*>(QStringLiteral("serverMissing"))->text(), missing);
    EXPECT_EQ(second->findChild<QLabel*>(QStringLiteral("serverMissing"))->toolTip(), missing);
    // A row with the "Can't do yet" line is no taller than one without.
    EXPECT_TRUE(first->findChild<QLabel*>(QStringLiteral("serverMissing"))->isHidden());
    EXPECT_EQ(rows->item(0)->sizeHint().height(), rows->item(1)->sizeHint().height());
    EXPECT_EQ(first->sizeHint().height(), second->sizeHint().height());

    // What they said is remembered for next time.
    const ServerList list = ServerList::load();
    EXPECT_EQ(list.find(pal.base())->version, QStringLiteral("2.0.1"));
    EXPECT_EQ(list.find(club.base())->userName, QStringLiteral("Ann"));
}

TEST_F(ServersDialogTest, EachServerIsOnlySentItsOwnToken) {
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    ASSERT_FALSE(club.requests.empty());
    ASSERT_FALSE(pal.requests.empty());
    expectOnlyOwnToken(club, "bld_pat_club");
    expectOnlyOwnToken(pal, "bld_pat_pal");
    bool clubAsked = false, palAsked = false;
    for (const auto& r : club.requests) clubAsked |= r.authorization == "Bearer bld_pat_club";
    for (const auto& r : pal.requests) palAsked |= r.authorization == "Bearer bld_pat_pal";
    EXPECT_TRUE(clubAsked && palAsked);
}

TEST_F(ServersDialogTest, NotSignedInAndSignedOutByTheServerSaySo) {
    tokens.remove(pal.base());
    club.clear("/api/tokens/current");
    club.reply("/api/tokens/current", 401, { { QStringLiteral("error"), QStringLiteral("unauthorized") } });
    auto d = open();
    ASSERT_TRUE(waitFor([&] {
        return dialog->statusText(pal.base()).startsWith(QStringLiteral("Not signed in"))
               && dialog->statusText(club.base()).startsWith(QStringLiteral("Signed out by the server"));
    }));
    // Nothing was sent to the friend's server with anyone's token.
    for (const auto& r : pal.requests) EXPECT_TRUE(r.authorization.isEmpty()) << r.path.toStdString();
}

TEST_F(ServersDialogTest, SignOutRemoveAndMainTouchOneServerOnly) {
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    dialog->selectServer(pal.base());
    auto* sign = dialog->findChild<QPushButton*>(QStringLiteral("signInOut"));
    EXPECT_EQ(sign->text(), QStringLiteral("Sign Out"));

    dialog->signOut(pal.base());
    EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(pal.base())));
    EXPECT_TRUE(tokens.tokens.contains(TokenStore::keyFor(club.base())));
    EXPECT_TRUE(dialog->statusText(pal.base()).startsWith(QStringLiteral("Not signed in")));
    EXPECT_EQ(sign->text(), QStringLiteral("Sign In"));

    dialog->makeMain(pal.base());
    EXPECT_EQ(ServerList::load().settingsAccount(), pal.base());

    dialog->renameServer(pal.base(), QStringLiteral("Bob's server"));
    EXPECT_EQ(ServerList::load().find(pal.base())->label(), QStringLiteral("Bob's server"));

    // Removing the club signs out of it and nothing else; the friend's stays Main.
    tokens.save(pal.base(), QStringLiteral("bld_pat_pal"));
    dialog->removeServer(club.base());
    EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(club.base())));
    EXPECT_TRUE(tokens.tokens.contains(TokenStore::keyFor(pal.base())));
    const ServerList list = ServerList::load();
    ASSERT_EQ(list.servers().size(), 1);
    EXPECT_EQ(list.settingsAccount(), pal.base());
}

// Sign Out ends the sign-in on the server too (it used to stay valid there
// until it ran out): the server is asked with its own token, and only it.
TEST_F(ServersDialogTest, SignOutEndsTheSignInOnTheServerToo) {
    pal.replyTo("DELETE", "/api/tokens/current", 200, { { QStringLiteral("ok"), true } });
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    EXPECT_TRUE(dialog->noticeText().isEmpty());
    dialog->selectServer(pal.base());
    auto* sign = dialog->findChild<QPushButton*>(QStringLiteral("signInOut"));
    ASSERT_EQ(sign->text(), QStringLiteral("Sign Out"));
    sign->click();
    ASSERT_TRUE(waitFor([&] { return !dialog->noticeText().isEmpty(); }));

    const auto asked = signOuts(pal);
    ASSERT_EQ(asked.size(), 1U);
    EXPECT_EQ(asked[0].authorization, QByteArray("Bearer bld_pat_pal"));
    EXPECT_TRUE(signOuts(club).empty());
    EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(pal.base())));
    EXPECT_TRUE(tokens.tokens.contains(TokenStore::keyFor(club.base())));
    EXPECT_EQ(dialog->noticeText(), QStringLiteral("Signed out of %1.").arg(ServerList::load().find(pal.base())->label()));
    auto* notice = dialog->findChild<QLabel*>(QStringLiteral("serversNotice"));
    ASSERT_NE(notice, nullptr);
    EXPECT_FALSE(notice->isHidden());
    EXPECT_EQ(notice->text(), dialog->noticeText());
    EXPECT_TRUE(dialog->statusText(pal.base()).startsWith(QStringLiteral("Not signed in")));
    EXPECT_EQ(sign->text(), QStringLiteral("Sign In"));
    expectOnlyOwnToken(club, "bld_pat_club");
    expectOnlyOwnToken(pal, "bld_pat_pal");
}

// The server can't be reached: signed out here all the same, and the window
// says until when that sign-in still works and where to end it.
TEST_F(ServersDialogTest, SignOutWithoutTheServerSaysUntilWhenTheSignInStaysValid) {
    const QDateTime longest = QDateTime::currentDateTime().addDays(30);
    auto away = std::make_unique<FakeHttp>();
    const QUrl url = away->base();
    away->reply("/api/version", 200, version(QStringLiteral("2.4.0"), desktopFeatures()));
    away->reply("/api/tokens/current", 200, userWithLifetime(QStringLiteral("Cy"), longest));
    {
        ServerList list = ServerList::load();
        list.add(url, QStringLiteral("Show hall"));
        list.save();
    }
    tokens.save(url, QStringLiteral("bld_pat_away"));
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled() && dialog->statusText(url).startsWith(QStringLiteral("Signed in as Cy")); }));
    EXPECT_EQ(ServerList::load().find(url)->signIn.longestAt.toMSecsSinceEpoch(), longest.toMSecsSinceEpoch());

    away.reset();  // the server is gone
    dialog->selectServer(url);
    dialog->findChild<QPushButton*>(QStringLiteral("signInOut"))->click();
    // Signed out here at once, whatever the server does.
    ASSERT_TRUE(waitFor([&] { return !tokens.tokens.contains(TokenStore::keyFor(url)); }));
    ASSERT_TRUE(waitFor([&] { return !dialog->noticeText().isEmpty(); }, 15000));
    EXPECT_EQ(dialog->noticeText(),
              QStringLiteral("Signed out here. The server couldn’t be reached, so that sign-in stays valid until %1; "
                             "you can end it from Profile › Browsers and devices on the website.")
                  .arg(QLocale().toString(longest.date(), QLocale::LongFormat)));
    EXPECT_TRUE(dialog->statusText(url).startsWith(QStringLiteral("Not signed in")));
    EXPECT_TRUE(tokens.tokens.contains(TokenStore::keyFor(club.base())));
}

// A server from before the route refuses (403 token_not_allowed) and never
// said how long its sign-ins last. It answered, so it isn't called
// "couldn't be reached": it didn't end the sign-in.
TEST_F(ServersDialogTest, SignOutOfAnOlderServerSaysTheSignInStaysValid) {
    pal.replyTo("DELETE", "/api/tokens/current", 403, { { QStringLiteral("error"), QStringLiteral("token_not_allowed") } });
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    dialog->signOut(pal.base());
    EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(pal.base())));
    ASSERT_TRUE(waitFor([&] { return !dialog->noticeText().isEmpty(); }));
    EXPECT_EQ(dialog->noticeText(),
              QStringLiteral("Signed out here. The server didn’t end that sign-in, so it stays valid until it "
                             "runs out; you can end it from Profile › Browsers and devices on the website."));
    ASSERT_EQ(signOuts(pal).size(), 1U);
}

// An older server that turns away an account on hold (403), one waiting to
// be deleted (403) or this version of the app (426): the same plain words,
// with the date the sign-in runs out when the server said one.
TEST_F(ServersDialogTest, ARefusedSignOutSaysTheServerDidntEndItNotThatItWasUnreachable) {
    const QDateTime longest = QDateTime::currentDateTime().addDays(30);
    pal.clear("/api/tokens/current");
    pal.reply("/api/tokens/current", 200, userWithLifetime(QStringLiteral("Pat"), longest));
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled() && dialog->statusText(pal.base()).startsWith(QStringLiteral("Signed in as Pat")); }));
    const QString expected =
        QStringLiteral("Signed out here. The server didn’t end that sign-in, so it stays valid until %1; you can end "
                       "it from Profile › Browsers and devices on the website.")
            .arg(QLocale().toString(longest.date(), QLocale::LongFormat));
    for (const auto& [status, error] : { std::pair{ 403, "account_restricted" }, std::pair{ 403, "account_pending_deletion" },
                                         std::pair{ 426, "update_required" } }) {
        tokens.save(pal.base(), QStringLiteral("bld_pat_pal"));
        pal.clear("DELETE /api/tokens/current");
        pal.replyTo("DELETE", "/api/tokens/current", status, { { QStringLiteral("error"), QLatin1String(error) } });
        dialog->signOut(pal.base());
        EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(pal.base())));
        ASSERT_TRUE(waitFor([&] { return !dialog->noticeText().isEmpty(); }));
        EXPECT_EQ(dialog->noticeText(), expected) << error;
        EXPECT_FALSE(dialog->noticeText().contains(QStringLiteral("reached")));
    }
}

// Remove server ends its sign-in on the server too; it only speaks up when
// the server couldn't end it.
TEST_F(ServersDialogTest, RemovingAServerEndsItsSignInOnTheServerToo) {
    const QDateTime longest = QDateTime::currentDateTime().addDays(400);
    club.clear("/api/tokens/current");
    club.reply("/api/tokens/current", 200, userWithLifetime(QStringLiteral("Ann"), longest));
    club.replyTo("DELETE", "/api/tokens/current", 200, { { QStringLiteral("ok"), true } });
    pal.replyTo("DELETE", "/api/tokens/current", 500, { { QStringLiteral("error"), QStringLiteral("internal") } });
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    ASSERT_TRUE(waitFor([&] { return ServerList::load().find(club.base())->signIn.idleSeconds > 0; }));

    dialog->removeServer(club.base());
    ASSERT_TRUE(waitFor([&] { return signOuts(club).size() == 1; }));
    EXPECT_EQ(signOuts(club)[0].authorization, QByteArray("Bearer bld_pat_club"));
    EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(club.base())));
    EXPECT_EQ(ServerList::load().servers().size(), 1);
    // Ended there: nothing to add.
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 200) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    EXPECT_TRUE(dialog->noticeText().isEmpty()) << dialog->noticeText().toStdString();

    // The friend's server fails to end it: removed and signed out here, and the window says so.
    dialog->removeServer(pal.base());
    EXPECT_TRUE(tokens.tokens.isEmpty());
    EXPECT_TRUE(ServerList::load().isEmpty());
    ASSERT_TRUE(waitFor([&] { return !dialog->noticeText().isEmpty(); }));
    // (It answered, with an error: not "couldn't be reached".)
    EXPECT_TRUE(dialog->noticeText().startsWith(QStringLiteral("Signed out here. The server didn’t end that sign-in")))
        << dialog->noticeText().toStdString();
    EXPECT_EQ(signOuts(pal).size(), 1U);
    EXPECT_EQ(signOuts(pal)[0].authorization, QByteArray("Bearer bld_pat_pal"));
}

namespace {
void writeBytes(const QString& path, int bytes) {
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(bytes, 'x'));
}

// Answers the Remove question when it opens: ticks or unticks its "Also
// remove…" box (when it is offered) and presses Remove. Says what it saw.
struct RemoveQuestion {
    bool seen = false, offered = false, wasTicked = false;
    QString offer, tip, body, warning;
    QTimer poll;
    explicit RemoveQuestion(std::optional<bool> tick = std::nullopt) {
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, [this, tick] {
            auto* ask = qobject_cast<bld::ui::ConfirmDialog*>(QApplication::activeModalWidget());
            if (!ask || seen) return;
            seen = true;
            offered = !ask->optionBox()->isHidden();
            wasTicked = ask->optionChosen();
            offer = ask->optionBox()->text();
            tip = ask->optionBox()->toolTip();
            body = ask->bodyText();
            warning = ask->warningText();
            if (offered && tick) ask->optionBox()->setChecked(*tick);
            QMetaObject::invokeMethod(ask->confirmButton(), &QPushButton::click, Qt::QueuedConnection);
        });
        poll.start();
    }
};
}  // namespace

// Removing a server offers to delete what this computer downloaded from it,
// saying how much room that frees: its parts and its live layouts' copies go,
// the app is told at once (the Parts panel drops the parts), and the other
// server's downloads stay.
TEST_F(ServersDialogTest, RemovingAServerOffersToDeleteItsDownloadedParts) {
    const QStringList palData = ServerList::dataFolders(pal.base()), clubData = ServerList::dataFolders(club.base());
    for (const QString& f : palData + clubData) QDir(f).removeRecursively();
    writeBytes(palData[0] + QStringLiteral("/custom/P.xml"), 3 * 1024 * 1024);
    writeBytes(palData[1] + QStringLiteral("/L1/doc.bin"), 512 * 1024);
    writeBytes(clubData[0] + QStringLiteral("/custom/C.xml"), 2048);
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    const QString label = ServerList::load().find(pal.base())->label();
    EXPECT_EQ(dialog->removeDataOffer(pal.base()),
              QStringLiteral("Also remove the parts and layout copies downloaded from %1? (%2)")
                  .arg(label, QLocale().formattedDataSize(3 * 1024 * 1024 + 512 * 1024, 1, QLocale::DataSizeTraditionalFormat)));
    EXPECT_TRUE(dialog->removeDataOffer(pal.base()).contains(QStringLiteral("3.5 MB"))) << dialog->removeDataOffer(pal.base()).toStdString();

    QList<QPair<QString, bool>> told;  // (signal, the folder was still there)
    QObject::connect(dialog, &ServersDialog::removingServerData, [&](const QUrl& u) {
        EXPECT_EQ(u, pal.base());
        told.append({ QStringLiteral("removing"), QDir(palData[0]).exists() });
    });
    QObject::connect(dialog, &ServersDialog::serverDataRemoved, [&](const QUrl& u) {
        EXPECT_EQ(u, pal.base());
        told.append({ QStringLiteral("removed"), QDir(palData[0]).exists() });
    });

    dialog->selectServer(pal.base());
    RemoveQuestion question;
    dialog->findChild<QPushButton*>(QStringLiteral("removeServer"))->click();
    ASSERT_TRUE(waitFor([&] { return ServerList::load().servers().size() == 1; }));
    EXPECT_TRUE(question.seen);
    EXPECT_TRUE(question.offered);
    EXPECT_TRUE(question.wasTicked);
    EXPECT_EQ(question.offer, QStringLiteral("Also remove the parts and layout copies downloaded from %1? (3.5 MB)").arg(label));
    EXPECT_TRUE(question.tip.contains(QStringLiteral("live layouts"))) << question.tip.toStdString();
    EXPECT_FALSE(QDir(palData[0]).exists());
    EXPECT_FALSE(QDir(palData[1]).exists());
    EXPECT_TRUE(QFileInfo::exists(clubData[0] + QStringLiteral("/custom/C.xml")));
    EXPECT_EQ(told, (QList<QPair<QString, bool>>{ { QStringLiteral("removing"), true }, { QStringLiteral("removed"), false } }));
    for (const QString& f : clubData) QDir(f).removeRecursively();
}

// Changes made offline that never reached the server go with the layout
// copies. The box says so in plain sight (how many, and to which layout),
// and doesn't tick "Also remove…" for you; left unticked, they stay.
TEST_F(ServersDialogTest, RemovingAServerNamesTheOfflineChangesThatNeverReachedIt) {
    const QStringList palData = ServerList::dataFolders(pal.base());
    for (const QString& f : palData) QDir(f).removeRecursively();
    const QString layoutId = QStringLiteral("0b9c2f6e-1111-4222-8333-444455556666");
    {
        ServerList list = ServerList::load();
        list.addRecent(pal.base(), { layoutId, QStringLiteral("Club yard"), false, {} });
        list.save();
    }
    writeBytes(palData[0] + QStringLiteral("/custom/P.xml"), 4096);
    const QString offline = palData[1] + QLatin1Char('/') + ServerList::layoutFolder(layoutId) + QStringLiteral("/offline.json");
    ASSERT_TRUE(QDir().mkpath(QFileInfo(offline).absolutePath()));
    {
        QFile f(offline);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(R"({"base":{},"mine":{},"count":2})");
    }
    // A copy with nothing waiting isn't counted.
    writeBytes(palData[1] + QStringLiteral("/other/layout.ydoc"), 128);
    ASSERT_EQ(ServerList::unsentChanges(pal.base()).size(), 1);
    EXPECT_EQ(ServerList::unsentChanges(pal.base()).first().changes, 2);
    EXPECT_TRUE(ServerList::unsentChanges(club.base()).isEmpty());

    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    // (This suite runs without the English plurals catalog, so "%n change(s)" shows as written.)
    const QString expected =
        QStringLiteral("2 change(s) you made offline to “Club yard” never reached the server. Removing the layout copies "
                       "deletes them for good. To send them first, cancel and open the layout from this server.");
    EXPECT_EQ(dialog->unsentWarning(pal.base()), expected);
    EXPECT_TRUE(dialog->unsentWarning(club.base()).isEmpty());

    dialog->selectServer(pal.base());
    RemoveQuestion question;  // presses Remove, leaving the box as it was offered
    dialog->findChild<QPushButton*>(QStringLiteral("removeServer"))->click();
    ASSERT_TRUE(waitFor([&] { return ServerList::load().servers().size() == 1; }));
    EXPECT_TRUE(question.seen);
    EXPECT_TRUE(question.offered);
    EXPECT_FALSE(question.wasTicked);
    EXPECT_EQ(question.warning, expected);
    // Not ticked: the changes are still here.
    EXPECT_TRUE(QFileInfo::exists(offline));
    for (const QString& f : palData) QDir(f).removeRecursively();
}

// One change, and a layout the list no longer knows by name.
TEST_F(ServersDialogTest, TheOfflineChangesWarningCountsInPlainWords) {
    const QStringList palData = ServerList::dataFolders(pal.base());
    for (const QString& f : palData) QDir(f).removeRecursively();
    const auto waiting = [&](const QString& folder, const char* json) {
        const QString path = palData[1] + QLatin1Char('/') + folder + QStringLiteral("/offline.json");
        ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(json);
    };
    waiting(QStringLiteral("a"), R"({"base":{},"mine":{}})");
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    EXPECT_TRUE(dialog->unsentWarning(pal.base()).startsWith(QStringLiteral("1 change(s) you made offline to a layout never reached the server. ")))
        << dialog->unsentWarning(pal.base()).toStdString();
    waiting(QStringLiteral("b"), R"({"base":{},"mine":{},"count":4})");
    EXPECT_TRUE(dialog->unsentWarning(pal.base()).startsWith(QStringLiteral("5 change(s) you made offline to 2 layouts never reached the server. ")))
        << dialog->unsentWarning(pal.base()).toStdString();
    // Nothing waiting, no warning in the box, and the box is ticked as before.
    for (const QString& f : palData) QDir(f).removeRecursively();
    EXPECT_TRUE(dialog->unsentWarning(pal.base()).isEmpty());
}

// Unticked, the downloads stay; and a server this computer keeps nothing from isn't asked about them.
TEST_F(ServersDialogTest, RemovingAServerKeepsItsDownloadsWhenUntickedAndDoesntOfferWhenThereAreNone) {
    const QStringList palData = ServerList::dataFolders(pal.base()), clubData = ServerList::dataFolders(club.base());
    for (const QString& f : palData + clubData) QDir(f).removeRecursively();
    writeBytes(palData[0] + QStringLiteral("/custom/P.xml"), 4096);
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    EXPECT_TRUE(dialog->removeDataOffer(club.base()).isEmpty());
    int told = 0;
    QObject::connect(dialog, &ServersDialog::removingServerData, [&] { ++told; });
    QObject::connect(dialog, &ServersDialog::serverDataRemoved, [&] { ++told; });

    {
        dialog->selectServer(pal.base());
        RemoveQuestion question(false);
        dialog->findChild<QPushButton*>(QStringLiteral("removeServer"))->click();
        ASSERT_TRUE(waitFor([&] { return ServerList::load().servers().size() == 1; }));
        EXPECT_TRUE(question.offered);
        EXPECT_TRUE(QFileInfo::exists(palData[0] + QStringLiteral("/custom/P.xml")));
    }
    {
        dialog->selectServer(club.base());
        RemoveQuestion question;
        dialog->findChild<QPushButton*>(QStringLiteral("removeServer"))->click();
        ASSERT_TRUE(waitFor([&] { return ServerList::load().isEmpty(); }));
        EXPECT_TRUE(question.seen);
        EXPECT_FALSE(question.offered);
    }
    EXPECT_EQ(told, 0);
    for (const QString& f : palData) QDir(f).removeRecursively();
}

TEST_F(ServersDialogTest, AddsAServerByItsAddressAndSignsInToIt) {
    auto d = open();
    QString error;
    EXPECT_FALSE(dialog->addServer(QStringLiteral("http://layouts.example.org"), {}, &error));
    EXPECT_FALSE(error.isEmpty());

    FakeHttp third;
    third.reply("/api/version", 200, version(QStringLiteral("2.4.0"), desktopFeatures()));
    third.reply("/api/tokens/current", 200, user(QStringLiteral("Cy")));
    ASSERT_TRUE(dialog->addServer(third.base().toString(), QStringLiteral("  Show hall ")));
    EXPECT_EQ(dialog->selectedServer(), third.base());
    ASSERT_TRUE(waitFor([&] { return dialog->statusText(third.base()).startsWith(QStringLiteral("Not signed in")); }));
    EXPECT_EQ(ServerList::load().find(third.base())->label(), QStringLiteral("Show hall"));

    // Signing in: the new token is kept for this server alone.
    QList<QUrl> asked;
    dialog->setSignInHandler([&](const QUrl& url) {
        asked << url;
        tokens.save(url, QStringLiteral("bld_pat_third"));
        return QStringLiteral("bld_pat_third");
    });
    dialog->signIn(third.base());
    ASSERT_TRUE(waitFor([&] { return dialog->isSignedIn(third.base()); }));
    EXPECT_EQ(asked, QList<QUrl>{ third.base() });
    // The name and the version arrive in separate replies.
    EXPECT_TRUE(waitFor([&] {
        return dialog->statusText(third.base()) == QStringLiteral("Signed in as Cy · version 2.4.0");
    })) << dialog->statusText(third.base()).toStdString();
    expectOnlyOwnToken(third, "bld_pat_third");
    expectOnlyOwnToken(club, "bld_pat_club");
    expectOnlyOwnToken(pal, "bld_pat_pal");
}

TEST_F(ServersDialogTest, HasHelpForTheListAndMain) {
    auto d = open();
    QStringList keys;
    for (auto* b : dialog->findChildren<bld::ui::help::HelpButton*>()) {
        keys << b->key();
        EXPECT_TRUE(bld::ui::help::helpEntry(b->key())) << b->key().toStdString();
    }
    EXPECT_TRUE(keys.contains(QStringLiteral("servers.list")));
    EXPECT_TRUE(keys.contains(QStringLiteral("servers.main")));
}

// BLD_REVIEW_SHOTS=<dir>: pictures for a PR, light and dark (skipped
// otherwise): the window after Sign Out of a server that can't be reached,
// and the Remove question for a server with downloaded parts. Uses only the
// buttons, so the same test takes the "before" pictures on an older build.
TEST_F(ServersDialogTest, PicturesOfSigningOutAndRemoving) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    bld::ui::theme::PrefsStore store(QStringLiteral("servers-shots"));
    bld::ui::theme::ThemeManager themer(store);
    const auto shoot = [&](QWidget* w, const QString& name) {
        for (const auto mode : { bld::ui::theme::ThemeChoice::Light, bld::ui::theme::ThemeChoice::Dark }) {
            bld::ui::theme::AppPrefs p = store.prefs();
            p.theme = mode;
            store.update(p);
            waitFor([] { return false; }, 600);
            ASSERT_TRUE(w->grab().save(out + QLatin1Char('/') + name
                                       + (mode == bld::ui::theme::ThemeChoice::Dark ? QStringLiteral("-dark.png")
                                                                                    : QStringLiteral("-light.png"))));
        }
    };
    // A third server that answers, then is gone; its sign-in lasts 30 more days at the longest.
    auto away = std::make_unique<FakeHttp>();
    const QUrl url = away->base();
    QJsonObject who{ { QStringLiteral("user"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("cy") },
                                                             { QStringLiteral("displayName"), QStringLiteral("Cy") } } },
                     { QStringLiteral("token"),
                       QJsonObject{ { QStringLiteral("idleSeconds"), 90 * 24 * 3600 },
                                    { QStringLiteral("longestAt"),
                                      static_cast<double>(QDateTime::currentDateTime().addDays(30).toMSecsSinceEpoch()) } } } };
    away->reply("/api/version", 200, version(QStringLiteral("2.4.0"), desktopFeatures()));
    away->reply("/api/tokens/current", 200, who);
    {
        ServerList list = ServerList::load();
        list.add(url, QStringLiteral("Show hall"));
        list.rename(pal.base(), QStringLiteral("Sam’s server"));
        list.save();
    }
    tokens.save(url, QStringLiteral("bld_pat_away"));
    // What this computer downloaded from Sam's server.
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString folder = pal.base().host() + QLatin1Char('_') + QString::number(pal.base().port());
    const QString partsDir = appData + QStringLiteral("/server-parts/") + folder;
    QDir(partsDir).removeRecursively();
    ASSERT_TRUE(QDir().mkpath(partsDir + QStringLiteral("/custom")));
    {
        QFile f(partsDir + QStringLiteral("/custom/P.xml"));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(12 * 1024 * 1024 + 300 * 1024, 'x'));
    }

    auto d = open();
    dialog->show();
    ASSERT_TRUE(waitFor([&] { return settled() && dialog->statusText(url).startsWith(QStringLiteral("Signed in as Cy")); }));
    away.reset();
    dialog->selectServer(url);
    dialog->findChild<QPushButton*>(QStringLiteral("signInOut"))->click();
    ASSERT_TRUE(waitFor([&] { return !tokens.tokens.contains(TokenStore::keyFor(url)); }));
    waitFor([] { return false; }, 2500);
    shoot(dialog, QStringLiteral("servers-signed-out"));

    dialog->selectServer(pal.base());
    // The question is modal: it is pictured and answered from inside its own loop.
    bool pictured = false;
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal || modal == dialog) return;
        poll.stop();
        const QPointer<QDialog> question(modal);
        waitFor([] { return false; }, 300);
        shoot(question, QStringLiteral("servers-remove-question"));
        pictured = true;
        if (question) question->reject();
    });
    poll.start();
    // (The button asks a moment later, from the event loop.)
    dialog->findChild<QPushButton*>(QStringLiteral("removeServer"))->click();
    EXPECT_TRUE(waitFor([&] { return pictured; }, 10000));
    poll.stop();
    QDir(partsDir).removeRecursively();
}

// (An older build, for the "before" pictures, has no argument here.)
#define BLD_SHOTS_KEEPS_DELETED true

// BLD_REVIEW_SHOTS=<dir>: the Remove server box with offline changes
// waiting, a sign-out the server refused, and the box for deleting a layout
// from a server that keeps deleted things, light and dark, for a PR
// (skipped otherwise; an older build takes the "before" pictures the same way).
TEST_F(ServersDialogTest, PicturesOfRemoveSignOutAndDelete) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    // English's plurals, as the app loads them ("2 changes", not "2 change(s)").
    QTranslator english;
    if (english.load(QStringLiteral("bld_en"), QCoreApplication::applicationDirPath() + QStringLiteral("/../..")))
        QCoreApplication::installTranslator(&english);
    bld::ui::theme::PrefsStore store(QStringLiteral("review-area9-shots"));
    bld::ui::theme::ThemeManager themer(store);
    const QStringList palData = ServerList::dataFolders(pal.base());
    for (const QString& f : palData) QDir(f).removeRecursively();
    const QString layoutId = QStringLiteral("0b9c2f6e-1111-4222-8333-444455556666");
    {
        ServerList list = ServerList::load();
        list.rename(pal.base(), QStringLiteral("Friend's server"));
        list.addRecent(pal.base(), { layoutId, QStringLiteral("Club yard"), false, {} });
        list.save();
    }
    writeBytes(palData[0] + QStringLiteral("/custom/P.xml"), 3 * 1024 * 1024);
    {
        const QString offline = palData[1] + QLatin1Char('/') + layoutId + QStringLiteral("/offline.json");
        ASSERT_TRUE(QDir().mkpath(QFileInfo(offline).absolutePath()));
        QFile f(offline);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(R"({"base":{},"mine":{},"count":2})");
    }
    pal.clear("/api/tokens/current");
    pal.reply("/api/tokens/current", 200, userWithLifetime(QStringLiteral("Bob"), QDateTime::currentDateTime().addDays(30)));
    pal.replyTo("DELETE", "/api/tokens/current", 403, { { QStringLiteral("error"), QStringLiteral("account_restricted") } });
    const auto settle = [] { waitFor([] { return false; }, 300); };
    for (const auto mode : { bld::ui::theme::ThemeChoice::Light, bld::ui::theme::ThemeChoice::Dark }) {
        bld::ui::theme::AppPrefs p = store.prefs();
        p.theme = mode;
        store.update(p);
        const QString scheme = mode == bld::ui::theme::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        tokens.save(pal.base(), QStringLiteral("bld_pat_pal"));
        auto d = open();
        d->resize(760, 440);
        d->show();
        ASSERT_TRUE(waitFor([&] { return settled(); }));

        // Remove server, with two offline changes that never reached it.
        dialog->selectServer(pal.base());
        // (The question is modal, and asked a moment later from the event
        // loop: it is pictured and answered from inside its own loop.)
        bool shot = false;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, [&] {
            auto* ask = qobject_cast<bld::ui::ConfirmDialog*>(QApplication::activeModalWidget());
            if (!ask) return;
            poll.stop();
            const QPointer<bld::ui::ConfirmDialog> question(ask);
            settle();
            if (question) {
                EXPECT_TRUE(question->grab().save(out + QStringLiteral("/remove-server-%1.png").arg(scheme)));
            }
            shot = true;
            if (question) question->reject();
        });
        poll.start();
        dialog->findChild<QPushButton*>(QStringLiteral("removeServer"))->click();
        EXPECT_TRUE(waitFor([&] { return shot; }, 10000));
        poll.stop();

        // Sign Out, refused by the server (an account on hold on an older server).
        dialog->signOut(pal.base());
        ASSERT_TRUE(waitFor([&] { return !dialog->noticeText().isEmpty(); }));
        settle();
        EXPECT_TRUE(d->grab().save(out + QStringLiteral("/sign-out-refused-%1.png").arg(scheme)));

        // Deleting a layout from a server that keeps deleted things for 30 days.
        bld::ui::ConfirmDialog box(
            bld::ui::ConfirmDialog::deleteOptions(QStringLiteral("Main yard"), bld::ui::ConfirmDialog::layoutWording(BLD_SHOTS_KEEPS_DELETED)));
        box.show();
        settle();
        EXPECT_TRUE(box.grab().save(out + QStringLiteral("/delete-layout-%1.png").arg(scheme)));
    }
    for (const QString& f : palData) QDir(f).removeRecursively();
}
