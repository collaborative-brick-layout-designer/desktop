// Notifications on the desktop (docs/notifications.md in the web repo): the
// API's JSON, grouping and the delivery choices; the center that follows
// every signed-in server (counts added up, live hints, new ones, an older
// server's 404); and the main window's bell, panel, Notifications window,
// Accept / Decline in the list, the warning card's "I understand", the
// Modules tab's club requests answered in the app, and an older server
// keeping its notices card.

#include "FakeHttp.h"

#include "NotificationCenter.h"
#include "Notifications.h"
#include "ServerList.h"
#include "TokenStore.h"
#include "parts/PartsLibrary.h"
#include "ui/ConfirmDialog.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/NoticeArea.h"
#include "ui/NotificationViews.h"
#include "ui/ServerLibrary.h"
#include "ui/ServerWindow.h"
#include "ui/UpdateCheck.h"
#include "ui/tours/Tours.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimeZone>
#include <QToolButton>

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

const synctest::Request* lastRequest(const FakeHttp& http, const QByteArray& method, const QByteArray& pathStart) {
    for (auto it = http.requests.rbegin(); it != http.requests.rend(); ++it)
        if (it->method == method && it->path.startsWith(pathStart)) return &*it;
    return nullptr;
}

int count(const FakeHttp& http, const QByteArray& method, const QByteArray& pathStart) {
    int n = 0;
    for (const auto& r : http.requests)
        if (r.method == method && r.path.startsWith(pathStart)) ++n;
    return n;
}

parts::PartsLibrary& bundledParts() {
    static parts::PartsLibrary lib;
    static bool scanned = false;
    if (!scanned) {
        lib.addSearchPath(QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts"));
        lib.scan();
        scanned = true;
    }
    return lib;
}

constexpr qint64 kNow = 1795000000000;

QJsonObject item(const QString& id, const QString& type, const QString& title, qint64 at, bool read = false,
                 const QJsonArray& actions = {}, const QJsonObject& extra = {}) {
    QJsonObject o{ { QStringLiteral("id"), id },
                   { QStringLiteral("type"), type },
                   { QStringLiteral("category"), type.section(QLatin1Char('.'), 0, 0) == QLatin1String("club") ? QStringLiteral("clubs")
                                                 : type.startsWith(QLatin1String("moderation")) ? QStringLiteral("moderation")
                                                                                                 : type.section(QLatin1Char('.'), 0, 0) },
                   { QStringLiteral("title"), title },
                   { QStringLiteral("body"), QString() },
                   { QStringLiteral("link"), QJsonValue() },
                   { QStringLiteral("createdAt"), static_cast<double>(at) },
                   { QStringLiteral("readAt"), read ? QJsonValue(static_cast<double>(at + 1)) : QJsonValue() },
                   { QStringLiteral("doneAt"), QJsonValue() },
                   { QStringLiteral("outcome"), QJsonValue() },
                   { QStringLiteral("outcomeBy"), QJsonValue() },
                   { QStringLiteral("club"), QJsonValue() },
                   { QStringLiteral("actor"), QJsonValue() },
                   { QStringLiteral("subject"), QJsonValue() },
                   { QStringLiteral("groupKey"), QJsonValue() },
                   { QStringLiteral("actions"), actions } };
    for (auto it = extra.begin(); it != extra.end(); ++it) o.insert(it.key(), it.value());
    return o;
}

QJsonObject club(const char* slug, const char* name) {
    return { { QStringLiteral("id"), QStringLiteral("org-") + QLatin1String(slug) },
             { QStringLiteral("name"), QLatin1String(name) },
             { QStringLiteral("slug"), QLatin1String(slug) } };
}

QJsonObject page(const QJsonArray& items, int unread, int waiting = 0, int warnings = 0) {
    return { { QStringLiteral("notifications"), items }, { QStringLiteral("nextCursor"), QJsonValue() },
             { QStringLiteral("unread"), unread }, { QStringLiteral("waiting"), waiting }, { QStringLiteral("warnings"), warnings } };
}

QJsonObject counts(int unread, int waiting = 0, int warnings = 0) {
    return { { QStringLiteral("unread"), unread }, { QStringLiteral("waiting"), waiting }, { QStringLiteral("warnings"), warnings } };
}

const QByteArray kUnreadPath = "/api/notifications?unread=1&limit=30";
const QByteArray kWarningsPath = "/api/notifications?type=moderation.warning,moderation.final_warning&limit=50";
const QByteArray kPanelPath = "/api/notifications?limit=25";

QJsonObject types() {
    const auto t = [](const char* type, const char* category, const char* label) {
        return QJsonObject{ { QStringLiteral("type"), QLatin1String(type) }, { QStringLiteral("category"), QLatin1String(category) },
                            { QStringLiteral("label"), QLatin1String(label) }, { QStringLiteral("waits"), false },
                            { QStringLiteral("action"), QString() } };
    };
    return { { QStringLiteral("categories"), QJsonArray{ QStringLiteral("clubs"), QStringLiteral("sharing") } },
             { QStringLiteral("types"), QJsonArray{ t("club.invite", "clubs", "Invites to a club"),
                                                    t("club.join_request", "clubs", "Requests to join a club you run"),
                                                    t("sharing.layout_invite", "sharing", "Invites to a layout") } } };
}

void serveNotifications(FakeHttp& http, int unread, const QJsonArray& items, int waiting = 0) {
    http.reply("/api/notifications/count", 200, counts(unread, waiting));
    http.reply(kUnreadPath, 200, page(items, unread, waiting));
    http.reply(kPanelPath, 200, page(items, unread, waiting));
    http.reply("/api/notifications/types", 200, types());
    http.reply("/api/me/notification-settings", 404, { { QStringLiteral("error"), QStringLiteral("not_found") } });
}

}  // namespace

// ---------- The API ---------------------------------------------------------

TEST(Notifications, ReadsAnItemAsTheContractShowsIt) {
    const QJsonObject o = QJsonDocument::fromJson(R"({
      "id": "6f0c", "type": "club.join_request", "category": "clubs",
      "title": "Ann asked to join ArkLUG", "body": "Their note: “Hi!”", "link": "/orgs/arklug/admin?tab=people",
      "createdAt": 1795000000000, "readAt": null, "doneAt": null, "outcome": null, "outcomeBy": null,
      "club": { "id": "o1", "name": "ArkLUG", "slug": "arklug" }, "actor": { "id": "u1", "name": "Ann" },
      "subject": { "kind": "join_request", "id": "j1" }, "groupKey": "join:o1", "actions": ["accept", "decline"] })")
                              .object();
    const sync::NotificationItem n = sync::NotificationApi::itemFromJson(o);
    EXPECT_EQ(n.id, QStringLiteral("6f0c"));
    EXPECT_EQ(n.title, QStringLiteral("Ann asked to join ArkLUG"));
    EXPECT_EQ(n.createdAt, 1795000000000);
    EXPECT_FALSE(n.read());
    EXPECT_TRUE(n.waits());
    EXPECT_FALSE(n.needsAcknowledge());
    ASSERT_TRUE(n.club);
    EXPECT_EQ(n.club->slug, QStringLiteral("arklug"));
    EXPECT_EQ(n.actor->name, QStringLiteral("Ann"));
    EXPECT_EQ(n.subjectKind, QStringLiteral("join_request"));
    EXPECT_EQ(n.groupKey, QStringLiteral("join:o1"));
    // While it waits there's nothing of theirs to open yet.
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::None);
}

TEST(Notifications, TheListsFiltersGoInItsAddress) {
    sync::NotificationQuery q;
    EXPECT_EQ(q.path(), QStringLiteral("/api/notifications?limit=30"));
    q.unreadOnly = true;
    q.types = { QStringLiteral("club.join_request"), QStringLiteral("club.invite") };
    q.search = QStringLiteral("ark lug&x");
    q.cursor = QStringLiteral("1795.6f0c");
    q.limit = 500;
    EXPECT_EQ(q.path(),
              QStringLiteral("/api/notifications?unread=1&type=club.join_request,club.invite&q=ark%20lug%26x&cursor=1795.6f0c&limit=100"));
}

TEST(Notifications, ItemsWithOneGroupKeyCollapseNewestFirst) {
    QList<sync::NotificationItem> items;
    const auto add = [&](const char* id, const char* key) {
        sync::NotificationItem n;
        n.id = QLatin1String(id);
        n.groupKey = QLatin1String(key);
        items << n;
    };
    add("a", "join:1");
    add("b", "");
    add("c", "join:1");
    add("d", "join:2");
    add("e", "join:1");
    const auto groups = sync::groupNotifications(items);
    ASSERT_EQ(groups.size(), 3);
    EXPECT_EQ(groups[0].head.id, QStringLiteral("a"));
    ASSERT_EQ(groups[0].rest.size(), 2);
    EXPECT_EQ(groups[0].rest[0].id, QStringLiteral("c"));
    EXPECT_EQ(groups[0].rest[1].id, QStringLiteral("e"));
    EXPECT_EQ(groups[1].head.id, QStringLiteral("b"));
    EXPECT_EQ(groups[2].head.id, QStringLiteral("d"));
}

TEST(Notifications, PlacesInTheAppComeFromTheLink) {
    sync::NotificationItem n;
    n.link = QStringLiteral("/editor/L1");
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::Layout);
    EXPECT_EQ(ui::placeFor(n).id, QStringLiteral("L1"));
    n.link = QStringLiteral("/modules/M1");
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::Module);
    n.link = QStringLiteral("/orgs/arklug?tab=partners");
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::Club);
    EXPECT_EQ(ui::placeFor(n).id, QStringLiteral("arklug"));
    n.link = QStringLiteral("/orgs/arklug/admin?tab=people");
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::Club);
    n.link = QStringLiteral("/orgs");
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::Clubs);
    // No place in the app: the website, and the button says so.
    n.link = QStringLiteral("/profile#download");
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::Web);
    EXPECT_EQ(ui::placeFor(n).openLabel(), QStringLiteral("Open on the website"));
    n.link.clear();
    EXPECT_EQ(ui::placeFor(n).kind, ui::NotificationPlace::None);
}

TEST(Notifications, OutcomesInPlainWords) {
    sync::NotificationItem n;
    n.type = QStringLiteral("club.partner_request");
    EXPECT_TRUE(ui::NotificationWords::outcome(n).isEmpty());
    n.outcome = QStringLiteral("accepted");
    n.outcomeBy = sync::NotificationPerson{ QStringLiteral("u"), QStringLiteral("Ann"), true };
    EXPECT_EQ(ui::NotificationWords::outcome(n), QStringLiteral("You accepted"));
    n.outcomeBy->you = false;
    EXPECT_EQ(ui::NotificationWords::outcome(n), QStringLiteral("Ann accepted"));
    n.type = QStringLiteral("club.join_request");
    EXPECT_EQ(ui::NotificationWords::outcome(n), QStringLiteral("Ann let them in"));
    n.outcome = QStringLiteral("withdrawn");
    EXPECT_EQ(ui::NotificationWords::outcome(n), QStringLiteral("Taken back, so nothing to answer"));
    EXPECT_EQ(ui::NotificationWords::badge(0), QString());
    EXPECT_EQ(ui::NotificationWords::badge(7), QStringLiteral("7"));
    EXPECT_EQ(ui::NotificationWords::badge(100), QStringLiteral("99+"));
    EXPECT_EQ(ui::NotificationWords::timeAgo(kNow - 30000, kNow), QStringLiteral("just now"));
    EXPECT_EQ(ui::NotificationWords::timeAgo(kNow - 5 * 60000, kNow), QStringLiteral("5 min ago"));
    EXPECT_EQ(ui::NotificationWords::timeAgo(kNow - 3 * 3600000, kNow), QStringLiteral("3 h ago"));
}

TEST(Notifications, PopUpsFollowTheServersPushChoicesAndQuietHours) {
    const QJsonObject view = QJsonDocument::fromJson(R"({
      "categories": ["clubs"],
      "types": [
        { "type": "club.join_request", "category": "clubs", "label": "Requests", "waits": true, "appLocked": false, "emailApart": null, "app": true, "email": "now", "push": true },
        { "type": "club.role_changed", "category": "clubs", "label": "Roles", "waits": false, "appLocked": false, "emailApart": null, "app": true, "email": "off", "push": false },
        { "type": "moderation.warning", "category": "moderation", "label": "Warnings", "waits": true, "appLocked": true, "emailApart": null, "app": true, "email": "now", "push": true }
      ],
      "quiet": { "on": true, "start": "22:00", "end": "07:00" }, "timeZone": "America/Chicago",
      "dailyAt": "08:00", "weeklyDay": 1, "pauseEmail": false, "pausePush": false,
      "clubs": [ { "id": "o1", "name": "ArkLUG", "slug": "arklug", "mute": true, "email": true, "push": true },
                 { "id": "o2", "name": "Rail", "slug": "rail", "mute": false, "email": true, "push": false } ],
      "emailAvailable": true, "pushAvailable": false })")
                                 .object();
    sync::NotificationSettings s = sync::NotificationApi::settingsFromJson(view);
    EXPECT_TRUE(s.pushFor(QStringLiteral("club.join_request"), {}));
    EXPECT_FALSE(s.pushFor(QStringLiteral("club.role_changed"), {}));
    EXPECT_FALSE(s.pushFor(QStringLiteral("club.join_request"), QStringLiteral("o1")));  // muted
    EXPECT_FALSE(s.pushFor(QStringLiteral("club.join_request"), QStringLiteral("o2")));  // no pop-ups from it
    EXPECT_TRUE(s.pushFor(QStringLiteral("moderation.warning"), QStringLiteral("o1")));  // warnings get through a mute
    EXPECT_FALSE(s.pushFor(QStringLiteral("some.new_type"), {}));
    s.pausePush = true;
    EXPECT_FALSE(s.pushFor(QStringLiteral("club.join_request"), {}));
    // 22:00 to 07:00 in Chicago (UTC-6 in January).
    const QTimeZone utc = QTimeZone::UTC;
    EXPECT_TRUE(s.inQuietHours(QDateTime(QDate(2027, 1, 10), QTime(5, 0), utc)));    // 23:00 there
    EXPECT_TRUE(s.inQuietHours(QDateTime(QDate(2027, 1, 10), QTime(12, 59), utc)));  // 06:59
    EXPECT_FALSE(s.inQuietHours(QDateTime(QDate(2027, 1, 10), QTime(13, 0), utc)));  // 07:00
    EXPECT_FALSE(s.inQuietHours(QDateTime(QDate(2027, 1, 10), QTime(18, 0), utc)));  // noon
    s.quietOn = false;
    EXPECT_FALSE(s.inQuietHours(QDateTime(QDate(2027, 1, 10), QTime(5, 0), utc)));
}

// ---------- The center --------------------------------------------------------

class NotificationCenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().remove(QStringLiteral("sync/serverAddress"));
        sync::ServerList list;
        list.add(a_.base(), QStringLiteral("Club server"));
        list.add(b_.base(), QStringLiteral("Friend's server"));
        list.save();
        tokens_->save(a_.base(), QStringLiteral("bld_pat_a"));
        tokens_->save(b_.base(), QStringLiteral("bld_pat_b"));
        for (FakeHttp* h : { &a_, &b_ }) h->reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
    }
    void TearDown() override {
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QStandardPaths::setTestModeEnabled(false);
    }
    FakeHttp a_, b_;
    std::shared_ptr<sync::MemoryTokenStore> tokens_ = std::make_shared<sync::MemoryTokenStore>();
};

TEST_F(NotificationCenterTest, TheBellAddsUpEveryServerAndAnOlderOneIsLeftAlone) {
    serveNotifications(a_, 3, {});
    serveNotifications(b_, 120, {});
    sync::NotificationCenter center;
    center.setTokenStore(tokens_);
    center.reload();
    ASSERT_TRUE(waitFor([&] { return center.totalUnread() == 123; })) << center.totalUnread();
    EXPECT_EQ(center.supported().size(), 2);
    EXPECT_EQ(lastRequest(a_, "GET", "/api/notifications/count")->authorization, QByteArray("Bearer bld_pat_a"));
    EXPECT_EQ(lastRequest(b_, "GET", "/api/notifications/count")->authorization, QByteArray("Bearer bld_pat_b"));
    ui::NotificationBell bell(center);
    EXPECT_EQ(bell.badgeText(), QStringLiteral("99+"));
    EXPECT_EQ(bell.accessibleName(), QStringLiteral("Notifications, 123 unread"));

    // The friend's server is an older one (404): it has none, quietly.
    b_.clear("/api/notifications/count");
    center.refresh(b_.base());
    ASSERT_TRUE(waitFor([&] { return center.support(b_.base()) == sync::NotificationCenter::Support::No; }));
    EXPECT_EQ(center.totalUnread(), 3);
    EXPECT_EQ(bell.badgeText(), QStringLiteral("3"));
}

TEST_F(NotificationCenterTest, ANewOneArrivesOnceAndTheOnesThereAtStartAreNotNew) {
    serveNotifications(a_, 1, { item(QStringLiteral("old"), QStringLiteral("club.role_changed"), QStringLiteral("Old news"), kNow) });
    sync::NotificationCenter center;
    QList<sync::NotificationItem> arrived;
    QObject::connect(&center, &sync::NotificationCenter::arrived, [&](const QUrl&, const QList<sync::NotificationItem>& l) { arrived << l; });
    center.setTokenStore(tokens_);
    center.reload();
    ASSERT_TRUE(waitFor([&] { return count(a_, "GET", kUnreadPath) >= 1; }));
    waitFor([] { return false; }, 200);
    EXPECT_TRUE(arrived.isEmpty());

    a_.clear("/api/notifications/count");
    a_.clear(kUnreadPath);
    a_.reply("/api/notifications/count", 200, counts(2));
    a_.reply(kUnreadPath, 200,
             page({ item(QStringLiteral("new"), QStringLiteral("club.invite"), QStringLiteral("Rail Club invited you"), kNow + 10),
                    item(QStringLiteral("old"), QStringLiteral("club.role_changed"), QStringLiteral("Old news"), kNow) },
                  2));
    center.refresh();
    ASSERT_TRUE(waitFor([&] { return arrived.size() == 1; }));
    EXPECT_EQ(arrived[0].id, QStringLiteral("new"));
    center.refresh();
    waitFor([&] { return count(a_, "GET", kUnreadPath) >= 3; });
    waitFor([] { return false; }, 200);
    EXPECT_EQ(arrived.size(), 1);  // once each
}

TEST_F(NotificationCenterTest, ALiveHintAsksAgain) {
    serveNotifications(b_, 0, {});
    a_.reply("/api/notifications/count", 200, counts(0));
    a_.reply("/api/notifications/count", 200, counts(4));
    a_.reply(kUnreadPath, 200, page({}, 0));
    a_.clear("/api/events");
    a_.replyRaw("/api/events", 200, "retry: 5000\n\n: connected\n\ndata: {\"kind\":\"notification\",\"action\":\"create\"}\n\n",
                "text/event-stream");
    a_.reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
    sync::NotificationCenter center;
    center.setReconnectDelay(50, 100);
    center.setTokenStore(tokens_);
    center.reload();
    ASSERT_TRUE(waitFor([&] { return center.totalUnread() == 4; })) << center.totalUnread();
    EXPECT_GE(count(a_, "GET", "/api/notifications/count"), 2);
}

// ---------- The main window ---------------------------------------------------

namespace {

class Win : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::setTokenStore;
};

}  // namespace

class NotificationsWindow : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().remove(QStringLiteral("sync/serverAddress"));
        {
            sync::ServerList list;
            list.add(http_.base(), QStringLiteral("Club server"));
            sync::ServerInfo info;
            info.features = QStringList{ QStringLiteral("liveSync"), QStringLiteral("sharedModules") };
            list.remember(http_.base(), info);
            list.touch(http_.base());
            if (second_) list.add(other_.base(), QStringLiteral("Friend's server"));
            list.save();
        }
        tokens_->save(http_.base(), QStringLiteral("bld_pat_win"));
        if (second_) tokens_->save(other_.base(), QStringLiteral("bld_pat_other"));
        http_.reply("/api/orgs", 200,
                    { { QStringLiteral("orgs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") },
                                                                          { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                          { QStringLiteral("myRole"), QStringLiteral("admin") } } } } });
        http_.reply("/api/catalog/settings", 200, { { QStringLiteral("modules"), true }, { QStringLiteral("parts"), true } });
        http_.reply("/api/modules", 200, { { QStringLiteral("modules"), QJsonArray{} } });
        http_.reply("/api/notices", 200, { { QStringLiteral("notices"), QJsonArray{} } });
        for (FakeHttp* h : { &http_, &other_ })
            h->reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
        beforeOpen();
        ui::NotificationRow::confirm = [this](QWidget*, const ui::ConfirmOptions& o) {
            asked_ << o.title;
            return answer_;
        };
        window_ = std::make_unique<Win>(bundledParts());
        window_->setTokenStore(tokens_);
        window_->ensureDocument();
        window_->openWebLink = [this](const QUrl& u) { opened_ << u; };
        library_ = window_->serverLibrary();
        ASSERT_TRUE(waitFor([&] { return library_->state() == ui::ServerLibrary::State::Ready; }));
    }
    void TearDown() override {
        if (auto* view = window_ ? window_->findChild<ui::MapView*>() : nullptr) view->undoStack()->setClean();
        window_.reset();
        ui::NotificationRow::confirm = [](QWidget* p, const ui::ConfirmOptions& o) { return ui::ConfirmDialog::ask(p, o); };
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QStandardPaths::setTestModeEnabled(false);
    }
    virtual void beforeOpen() {}
    ui::NotificationPanel* openPanel() {
        window_->notificationBell()->click();
        waitFor([&] { return window_->notificationPanel() && window_->notificationPanel()->isVisible(); });
        return window_->notificationPanel();
    }
    QPushButton* button(QWidget* in, const char* name) { return in ? in->findChild<QPushButton*>(QLatin1String(name)) : nullptr; }
    QString outcome(QWidget* row) {
        auto* l = row ? row->findChild<QLabel*>(QStringLiteral("notificationOutcome")) : nullptr;
        return l ? l->text() : QString();
    }

    bool second_ = false;
    FakeHttp http_, other_;
    std::shared_ptr<sync::MemoryTokenStore> tokens_ = std::make_shared<sync::MemoryTokenStore>();
    std::unique_ptr<Win> window_;
    ui::ServerLibrary* library_ = nullptr;
    QList<QUrl> opened_;
    QStringList asked_;
    bool answer_ = true;
};

class NotificationsBell : public NotificationsWindow {
protected:
    void beforeOpen() override {
        const QJsonArray items{
            item(QStringLiteral("n1"), QStringLiteral("club.invite"), QStringLiteral("Rail Club invited you"), kNow + 30, false,
                 { QStringLiteral("accept"), QStringLiteral("decline") }, { { QStringLiteral("club"), club("rail", "Rail Club") } }),
            item(QStringLiteral("n2"), QStringLiteral("account.export_ready"), QStringLiteral("Your data download is ready"), kNow + 20,
                 false, {}, { { QStringLiteral("link"), QStringLiteral("/profile#download") } }),
            item(QStringLiteral("n3"), QStringLiteral("club.join_request"), QStringLiteral("Bo asked to join Train Club"), kNow + 10, true,
                 { QStringLiteral("accept"), QStringLiteral("decline") }, { { QStringLiteral("club"), club("train", "Train Club") } }),
        };
        serveNotifications(http_, 2, items, 2);
    }
};

TEST_F(NotificationsBell, TheBellShowsTheUnreadCountAndItsPanelListsTheNewestFirst) {
    ui::NotificationBell* bell = window_->notificationBell();
    ASSERT_NE(bell, nullptr);
    ASSERT_TRUE(waitFor([&] { return bell->badgeText() == QStringLiteral("2"); })) << bell->badgeText().toStdString();
    EXPECT_EQ(bell->text(), QStringLiteral("Notifications"));
    ui::NotificationPanel* panel = openPanel();
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(waitFor([&] { return panel->list()->rows().size() == 3; }));
    const auto rows = panel->list()->rows();
    EXPECT_EQ(rows[0]->item().id, QStringLiteral("n1"));
    EXPECT_EQ(rows[2]->item().id, QStringLiteral("n3"));
    // A request to join says "Let them in"; one with no place in the app says it opens the website.
    EXPECT_EQ(button(rows[2], "notificationAccept")->text(), QStringLiteral("Let them in"));
    EXPECT_EQ(button(rows[1], "notificationOpen")->text(), QStringLiteral("Open on the website"));
    EXPECT_EQ(button(rows[0], "notificationOpen"), nullptr);  // answer it first
}

TEST_F(NotificationsBell, MarkReadAndUnreadGoToTheServer) {
    ui::NotificationPanel* panel = openPanel();
    ASSERT_TRUE(waitFor([&] { return panel->list()->row(QStringLiteral("n2")); }));
    http_.reply("/api/notifications/n2/read", 200, { { QStringLiteral("ok"), true } });
    http_.reply("/api/notifications/n3/unread", 200, { { QStringLiteral("ok"), true } });
    panel->list()->row(QStringLiteral("n2"))->findChild<QToolButton*>(QStringLiteral("notificationReadToggle"))->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/n2/read"); }));
    EXPECT_EQ(lastRequest(http_, "POST", "/api/notifications/n2/read")->authorization, QByteArray("Bearer bld_pat_win"));
    panel->list()->row(QStringLiteral("n3"))->findChild<QToolButton*>(QStringLiteral("notificationReadToggle"))->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/n3/unread"); }));
    // Mark all as read.
    http_.reply("/api/notifications/read-all", 200, { { QStringLiteral("ok"), true }, { QStringLiteral("read"), 2 } });
    panel->list()->readAllButton()->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/read-all"); }));
    EXPECT_EQ(lastRequest(http_, "POST", "/api/notifications/read-all")->body, QByteArray("{}"));
}

TEST_F(NotificationsBell, OpenGoesToTheWebsiteOnlyWhenTheAppHasNoPlaceForIt) {
    ui::NotificationPanel* panel = openPanel();
    ASSERT_TRUE(waitFor([&] { return panel->list()->row(QStringLiteral("n2")); }));
    http_.reply("/api/notifications/n2/read", 200, { { QStringLiteral("ok"), true } });
    button(panel->list()->row(QStringLiteral("n2")), "notificationOpen")->click();
    ASSERT_TRUE(waitFor([&] { return !opened_.isEmpty(); }));
    EXPECT_EQ(opened_[0].path(), QStringLiteral("/profile"));
    EXPECT_EQ(opened_[0].fragment(), QStringLiteral("download"));
    EXPECT_EQ(opened_[0].host(), http_.base().host());
    // Opening it marks it read.
    EXPECT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/n2/read"); }));
}

TEST_F(NotificationsBell, AcceptAnswersInTheAppAndSaysSo) {
    ui::NotificationPanel* panel = openPanel();
    ASSERT_TRUE(waitFor([&] { return panel->list()->row(QStringLiteral("n1")); }));
    QJsonObject after = item(QStringLiteral("n1"), QStringLiteral("club.invite"), QStringLiteral("Rail Club invited you"), kNow + 30, true, {},
                             { { QStringLiteral("club"), club("rail", "Rail Club") },
                               { QStringLiteral("outcome"), QStringLiteral("accepted") },
                               { QStringLiteral("doneAt"), static_cast<double>(kNow + 40) },
                               { QStringLiteral("link"), QStringLiteral("/orgs/rail") },
                               { QStringLiteral("outcomeBy"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("me") },
                                                                           { QStringLiteral("name"), QStringLiteral("Me") },
                                                                           { QStringLiteral("you"), true } } } });
    http_.reply("/api/notifications/n1/act", 200,
                { { QStringLiteral("ok"), true }, { QStringLiteral("outcome"), QStringLiteral("accepted") },
                  { QStringLiteral("result"), QJsonObject{ { QStringLiteral("orgId"), QStringLiteral("org-rail") } } },
                  { QStringLiteral("notification"), after } });
    ui::NotificationRow* row = panel->list()->row(QStringLiteral("n1"));
    button(row, "notificationAccept")->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/n1/act"); }));
    EXPECT_EQ(QJsonDocument::fromJson(lastRequest(http_, "POST", "/api/notifications/n1/act")->body).object(),
              (QJsonObject{ { QStringLiteral("action"), QStringLiteral("accept") } }));
    ASSERT_TRUE(waitFor([&] { return outcome(panel->list()->row(QStringLiteral("n1"))) == QStringLiteral("You accepted"); }));
    // No dead end: the club is a place in the app now.
    EXPECT_EQ(button(panel->list()->row(QStringLiteral("n1")), "notificationOpen")->text(), QStringLiteral("Show the club"));
}

TEST_F(NotificationsBell, SomeoneElseAnsweringFirstIsShownNotAnError) {
    ui::NotificationPanel* panel = openPanel();
    ASSERT_TRUE(waitFor([&] { return panel->list()->row(QStringLiteral("n3")); }));
    QJsonObject after = item(QStringLiteral("n3"), QStringLiteral("club.join_request"), QStringLiteral("Bo asked to join Train Club"), kNow + 10,
                             true, {},
                             { { QStringLiteral("club"), club("train", "Train Club") },
                               { QStringLiteral("outcome"), QStringLiteral("accepted") },
                               { QStringLiteral("doneAt"), static_cast<double>(kNow + 15) },
                               { QStringLiteral("outcomeBy"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("ann") },
                                                                           { QStringLiteral("name"), QStringLiteral("Ann") },
                                                                           { QStringLiteral("you"), false } } } });
    http_.reply("/api/notifications/n3/act", 200,
                { { QStringLiteral("ok"), true }, { QStringLiteral("already"), true }, { QStringLiteral("outcome"), QStringLiteral("accepted") },
                  { QStringLiteral("notification"), after } });
    button(panel->list()->row(QStringLiteral("n3")), "notificationAccept")->click();
    ASSERT_TRUE(waitFor([&] {
        return outcome(panel->list()->row(QStringLiteral("n3"))) == QStringLiteral("Ann let them in (someone else answered first)");
    })) << outcome(panel->list()->row(QStringLiteral("n3"))).toStdString();
    EXPECT_TRUE(panel->list()->row(QStringLiteral("n3"))->findChild<QLabel*>(QStringLiteral("notificationError"))->isHidden());
}

TEST_F(NotificationsBell, DecliningARequestToJoinAsksFirstAsTheClubPageDoes) {
    ui::NotificationPanel* panel = openPanel();
    ASSERT_TRUE(waitFor([&] { return panel->list()->row(QStringLiteral("n3")); }));
    answer_ = false;
    button(panel->list()->row(QStringLiteral("n3")), "notificationDecline")->click();
    ASSERT_EQ(asked_.size(), 1);
    EXPECT_EQ(asked_[0], QStringLiteral("Decline this request?"));
    waitFor([] { return false; }, 150);
    EXPECT_FALSE(lastRequest(http_, "POST", "/api/notifications/n3/act"));
    // An invite's Decline doesn't ask.
    http_.reply("/api/notifications/n1/act", 403, { { QStringLiteral("error"), QStringLiteral("email_not_verified") },
                                                    { QStringLiteral("message"), QStringLiteral("Confirm your email address first.") } });
    button(panel->list()->row(QStringLiteral("n1")), "notificationDecline")->click();
    EXPECT_EQ(asked_.size(), 1);
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/n1/act"); }));
    EXPECT_EQ(QJsonDocument::fromJson(lastRequest(http_, "POST", "/api/notifications/n1/act")->body).object().value("action").toString(),
              QStringLiteral("decline"));
    // The server's refusal, in its words; the answers stay to try again.
    auto* err = panel->list()->row(QStringLiteral("n1"))->findChild<QLabel*>(QStringLiteral("notificationError"));
    ASSERT_TRUE(waitFor([&] { return !err->isHidden(); }));
    EXPECT_EQ(err->text(), QStringLiteral("Confirm your email address first."));
    EXPECT_TRUE(button(panel->list()->row(QStringLiteral("n1")), "notificationAccept")->isEnabled());
}

TEST_F(NotificationsBell, TheWindowHasEverythingWithSearch) {
    http_.reply("/api/notifications?q=rail&limit=30", 200,
                page({ item(QStringLiteral("n1"), QStringLiteral("club.invite"), QStringLiteral("Rail Club invited you"), kNow + 30) }, 2));
    http_.reply("/api/notifications?limit=30", 200,
                page({ item(QStringLiteral("n1"), QStringLiteral("club.invite"), QStringLiteral("Rail Club invited you"), kNow + 30),
                       item(QStringLiteral("n2"), QStringLiteral("account.export_ready"), QStringLiteral("Your data download is ready"), kNow + 20) },
                     2));
    window_->showNotificationsWindow();
    ui::NotificationsWindow* w = window_->notificationsWindow();
    ASSERT_NE(w, nullptr);
    ASSERT_TRUE(waitFor([&] { return w->list()->rows().size() == 2; }));
    w->list()->searchBox()->setText(QStringLiteral("rail"));
    ASSERT_TRUE(waitFor([&] { return w->list()->rows().size() == 1; }));
    EXPECT_TRUE(lastRequest(http_, "GET", "/api/notifications?q=rail"));
    // The type filter lists the server's types by their names.
    EXPECT_GE(w->list()->typePicker()->findText(QStringLiteral("    Invites to a club")), 0);
    // One server: no server picker.
    EXPECT_TRUE(w->list()->serverPicker()->isHidden());
}

class NotificationsWarning : public NotificationsWindow {
protected:
    void beforeOpen() override {
        const QJsonObject warning = item(QStringLiteral("w1"), QStringLiteral("moderation.warning"), QStringLiteral("Warning from the site team"),
                                         kNow, false, { QStringLiteral("acknowledge") },
                                         { { QStringLiteral("body"), QStringLiteral("Please keep layout names friendly.") } });
        http_.reply("/api/notifications/count", 200, counts(1, 0, 1));
        http_.reply("/api/notifications/count", 200, counts(0, 0, 0));
        http_.reply(kUnreadPath, 200, page({ warning }, 1, 0, 1));
        http_.reply(kWarningsPath, 200, page({ warning }, 1, 0, 1));
        http_.reply("/api/notifications/w1/acknowledge", 200, { { QStringLiteral("ok"), true } });
        http_.reply("/api/notifications/types", 200, types());
    }
};

TEST_F(NotificationsWarning, AWarningWaitsForIUnderstandThroughTheNewRoute) {
    ui::NoticeArea* notices = window_->notices();
    const QString id = QStringLiteral("notification-warning");
    ASSERT_TRUE(waitFor([&] { return notices->isShown(id); }));
    QFrame* card = notices->card(id);
    bool saysWhy = false;
    for (auto* l : card->findChildren<QLabel*>()) saysWhy |= l->text().contains(QStringLiteral("Please keep layout names friendly."));
    EXPECT_TRUE(saysWhy);
    QPushButton* ok = nullptr;
    QPushButton* all = nullptr;
    for (auto* b : card->findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("I understand")) ok = b;
        if (b->text() == QStringLiteral("All notifications")) all = b;
    }
    ASSERT_NE(ok, nullptr);
    ASSERT_NE(all, nullptr);  // in the app, not the website
    ok->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/w1/acknowledge"); }));
    EXPECT_FALSE(lastRequest(http_, "POST", "/api/notices"));
    ASSERT_TRUE(waitFor([&] { return !notices->isShown(id); }));
    // The older notices aren't asked for on a server with notifications.
    EXPECT_FALSE(lastRequest(http_, "GET", "/api/notices"));
    EXPECT_FALSE(notices->isShown(QStringLiteral("server-warning")));
}

class NotificationsOldServer : public NotificationsWindow {
protected:
    void beforeOpen() override {
        // No /api/notifications here (404); the older notices are.
        http_.clear("/api/notices");
        http_.reply("/api/notices", 200,
                    { { QStringLiteral("notices"),
                        QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("w1") }, { QStringLiteral("scope"), QStringLiteral("site") },
                                                 { QStringLiteral("severity"), QStringLiteral("warning") },
                                                 { QStringLiteral("reason"), QStringLiteral("Be kind") },
                                                 { QStringLiteral("acknowledgedAt"), QJsonValue() } } } } });
        http_.reply("/api/club-requests/count", 200,
                    { { QStringLiteral("clubs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") },
                                                                          { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                          { QStringLiteral("count"), 1 } } } } });
    }
};

TEST_F(NotificationsOldServer, AnOlderServerKeepsItsNoticesAndWebsiteLinksQuietly) {
    ui::NoticeArea* notices = window_->notices();
    ASSERT_TRUE(waitFor([&] { return notices->isShown(QStringLiteral("server-warning")); }));
    EXPECT_FALSE(notices->isShown(QStringLiteral("notification-warning")));
    EXPECT_EQ(window_->notifications()->support(http_.base()), sync::NotificationCenter::Support::No);
    // The bell is there, with no number, and its list says so in plain words.
    EXPECT_EQ(window_->notificationBell()->badgeText(), QString());
    ui::NotificationPanel* panel = openPanel();
    ASSERT_TRUE(waitFor([&] { return panel->list()->emptyLine()->isVisible(); }));
    EXPECT_EQ(panel->list()->emptyLine()->text(),
              QStringLiteral("Your server doesn't have notifications yet. Its warnings and notes still show over the map."));
    panel->close();
    // A club's requests still point to the website there.
    window_->showServerWindow(static_cast<int>(ui::ServerWindow::Tab::Modules));
    ui::ServerWindow* sw = window_->serverWindow();
    ASSERT_TRUE(waitFor([&] { return sw->clubWaitingNote()->findChild<QPushButton*>(QStringLiteral("openClubPartners")); }));
}

class NotificationsClubRequests : public NotificationsWindow {
protected:
    void beforeOpen() override {
        http_.reply("/api/club-requests/count", 200,
                    { { QStringLiteral("clubs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") },
                                                                          { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                          { QStringLiteral("count"), 1 } } } } });
        const QJsonObject partner = item(QStringLiteral("p1"), QStringLiteral("club.partner_request"),
                                         QStringLiteral("Rail Club asked Train Club to be partners"), kNow, false,
                                         { QStringLiteral("accept"), QStringLiteral("decline") }, { { QStringLiteral("club"), club("train", "Train Club") } });
        serveNotifications(http_, 1, { partner }, 1);
        http_.reply("/api/notifications?type=club.partner_request,club.offer_received&limit=50", 200, page({ partner }, 1, 1));
    }
};

TEST_F(NotificationsClubRequests, AClubsPartnerRequestIsAnsweredOnTheModulesTab) {
    ASSERT_TRUE(waitFor([&] { return window_->notifications()->support(http_.base()) == sync::NotificationCenter::Support::Yes; }));
    window_->showServerWindow(static_cast<int>(ui::ServerWindow::Tab::Modules));
    ui::ServerWindow* sw = window_->serverWindow();
    QWidget* note = sw->clubWaitingNote();
    ASSERT_TRUE(waitFor([&] { return note->findChild<ui::NotificationRow*>(QStringLiteral("notificationRow:p1")); }));
    EXPECT_EQ(note->findChild<QPushButton*>(QStringLiteral("openClubPartners")), nullptr);  // not the website any more
    http_.reply("/api/notifications/p1/act", 200,
                { { QStringLiteral("ok"), true }, { QStringLiteral("outcome"), QStringLiteral("declined") },
                  { QStringLiteral("notification"),
                    item(QStringLiteral("p1"), QStringLiteral("club.partner_request"), QStringLiteral("Rail Club asked Train Club to be partners"),
                         kNow, true, {},
                         { { QStringLiteral("club"), club("train", "Train Club") }, { QStringLiteral("outcome"), QStringLiteral("declined") },
                           { QStringLiteral("doneAt"), static_cast<double>(kNow + 5) },
                           { QStringLiteral("outcomeBy"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("me") },
                                                                       { QStringLiteral("name"), QStringLiteral("Me") },
                                                                       { QStringLiteral("you"), true } } } }) } });
    auto* row = note->findChild<ui::NotificationRow*>(QStringLiteral("notificationRow:p1"));
    button(row, "notificationDecline")->click();
    EXPECT_TRUE(asked_.isEmpty());  // the web asks only before turning down a request to join
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notifications/p1/act"); }));
    ASSERT_TRUE(waitFor([&] {
        auto* r = note->findChild<ui::NotificationRow*>(QStringLiteral("notificationRow:p1"));
        return r && outcome(r) == QStringLiteral("You declined");
    }));
}

class NotificationsTwoServers : public NotificationsWindow {
protected:
    void SetUp() override {
        second_ = true;
        NotificationsWindow::SetUp();
    }
    void beforeOpen() override {
        serveNotifications(http_, 2,
                           { item(QStringLiteral("a1"), QStringLiteral("club.role_changed"), QStringLiteral("You're a Manager of Train Club now"), kNow + 5) });
        serveNotifications(other_, 1,
                           { item(QStringLiteral("b1"), QStringLiteral("sharing.module_shared"), QStringLiteral("Sam shared “Crane”"), kNow + 9) });
    }
};

TEST_F(NotificationsTwoServers, TheBellCountsEveryServerAndEachItemNamesItsServer) {
    ui::NotificationBell* bell = window_->notificationBell();
    ASSERT_TRUE(waitFor([&] { return bell->badgeText() == QStringLiteral("3"); })) << bell->badgeText().toStdString();
    ui::NotificationPanel* panel = openPanel();
    ASSERT_TRUE(waitFor([&] { return panel->list()->rows().size() == 2; }));
    EXPECT_FALSE(panel->list()->serverPicker()->isHidden());
    const auto rows = panel->list()->rows();
    EXPECT_EQ(rows[0]->item().id, QStringLiteral("b1"));  // newest first, across servers
    EXPECT_TRUE(rows[0]->findChild<QLabel*>(QStringLiteral("notificationMeta"))->text().contains(QStringLiteral("Friend's server")));
    EXPECT_TRUE(rows[1]->findChild<QLabel*>(QStringLiteral("notificationMeta"))->text().contains(QStringLiteral("Club server")));
    EXPECT_EQ(lastRequest(other_, "GET", "/api/notifications")->authorization, QByteArray("Bearer bld_pat_other"));
    // One server picked: only its own.
    QComboBox* pick = panel->list()->serverPicker();
    pick->setCurrentIndex(pick->findText(QStringLiteral("Friend's server")));
    emit pick->activated(pick->currentIndex());
    ASSERT_TRUE(waitFor([&] { return panel->list()->rows().size() == 1; }));
    EXPECT_EQ(panel->list()->rows()[0]->item().id, QStringLiteral("b1"));
    // Marking one read goes to its own server.
    other_.reply("/api/notifications/b1/read", 200, { { QStringLiteral("ok"), true } });
    panel->list()->rows()[0]->findChild<QToolButton*>(QStringLiteral("notificationReadToggle"))->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(other_, "POST", "/api/notifications/b1/read"); }));
    EXPECT_FALSE(lastRequest(http_, "POST", "/api/notifications/b1/read"));
}

TEST_F(NotificationsBell, TheClubsTourStopsAtTheBell) {
    const ui::tours::Tour* clubs = ui::tours::findTour(QStringLiteral("clubs"));
    ASSERT_NE(clubs, nullptr);
    bool stops = false;
    for (const auto& s : clubs->steps) stops |= s.target == QLatin1String("notifications.bell");
    EXPECT_TRUE(stops);
    window_->resize(1280, 800);
    window_->show();
    ASSERT_TRUE(waitFor([&] { return window_->notificationBell()->isVisible(); }));
    EXPECT_EQ(ui::tours::findTarget(window_.get(), QStringLiteral("notifications.bell")), window_->notificationBell());
}

// ---------- Pictures for the pull request (BLD_REVIEW_SHOTS) ----------------

namespace {

template <typename Shoot>
void inBothThemes(const char* storeName, Shoot shoot) {
    ui::theme::PrefsStore store{ QLatin1String(storeName) };
    ui::theme::ThemeManager themer(store);
    for (const auto mode : { ui::theme::ThemeChoice::Light, ui::theme::ThemeChoice::Dark }) {
        ui::theme::AppPrefs p = store.prefs();
        p.theme = mode;
        store.update(p);
        waitFor([] { return false; }, 200);
        shoot(mode == ui::theme::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    }
}

QImage grabTop(QWidget* w, int height) {
    const QPixmap all = w->grab();
    return all.copy(0, 0, all.width(), std::min(height, all.height())).toImage();
}

}  // namespace

TEST_F(NotificationsBell, PicturesOfTheBellPanelAndWindow) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    window_->resize(1280, 800);
    window_->show();
    ASSERT_TRUE(waitFor([&] { return window_->notificationBell()->badgeText() == QStringLiteral("2"); }));
    http_.reply("/api/notifications?limit=30", 200, page({}, 2));
    inBothThemes("notification-shots", [&](const QString& scheme) {
        EXPECT_TRUE(grabTop(window_.get(), 140).save(out + QStringLiteral("/notifications-bell-%1.png").arg(scheme)));
        ui::NotificationPanel* panel = openPanel();
        ASSERT_TRUE(waitFor([&] { return panel->list()->rows().size() == 3; }));
        waitFor([] { return false; }, 300);
        EXPECT_TRUE(panel->grab().save(out + QStringLiteral("/notifications-panel-%1.png").arg(scheme)));
        panel->close();
        http_.clear("/api/notifications?limit=30");
        http_.reply("/api/notifications?limit=30", 200,
                    page({ item(QStringLiteral("n1"), QStringLiteral("club.invite"), QStringLiteral("Rail Club invited you"), QDateTime::currentMSecsSinceEpoch() - 120000, false,
                                { QStringLiteral("accept"), QStringLiteral("decline") }, { { QStringLiteral("club"), club("rail", "Rail Club") } }),
                           item(QStringLiteral("n4"), QStringLiteral("club.join_request"), QStringLiteral("Bo asked to join Train Club"),
                                QDateTime::currentMSecsSinceEpoch() - 3600000, true, {},
                                { { QStringLiteral("club"), club("train", "Train Club") }, { QStringLiteral("outcome"), QStringLiteral("accepted") },
                                  { QStringLiteral("doneAt"), 1.0 },
                                  { QStringLiteral("outcomeBy"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("a") }, { QStringLiteral("name"), QStringLiteral("Ann") },
                                                                              { QStringLiteral("you"), false } } } }),
                           item(QStringLiteral("n2"), QStringLiteral("account.export_ready"), QStringLiteral("Your data download is ready"),
                                QDateTime::currentMSecsSinceEpoch() - 2 * 86400000, true, {},
                                { { QStringLiteral("link"), QStringLiteral("/profile#download") },
                                  { QStringLiteral("body"), QStringLiteral("It's ready for 7 days.") } }) },
                         2));
        window_->showNotificationsWindow();
        ui::NotificationsWindow* w = window_->notificationsWindow();
        ASSERT_TRUE(waitFor([&] { return w->list()->rows().size() == 3; }));
        waitFor([] { return false; }, 300);
        EXPECT_TRUE(w->grab().save(out + QStringLiteral("/notifications-window-%1.png").arg(scheme)));
        w->close();
    });
}

TEST_F(NotificationsWarning, PicturesOfTheWarningCard) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    window_->resize(1280, 800);
    window_->show();
    ASSERT_TRUE(waitFor([&] { return window_->notices()->isShown(QStringLiteral("notification-warning")); }));
    inBothThemes("notification-warning-shots", [&](const QString& scheme) {
        waitFor([] { return false; }, 200);
        EXPECT_TRUE(window_->notices()->card(QStringLiteral("notification-warning"))->grab().save(
            out + QStringLiteral("/notifications-warning-%1.png").arg(scheme)));
    });
}

TEST_F(NotificationsOldServer, PicturesOfBeforeTheOlderNoticesAndWebsiteLine) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    window_->resize(1280, 800);
    window_->show();
    ASSERT_TRUE(waitFor([&] { return window_->notices()->isShown(QStringLiteral("server-warning")); }));
    window_->showServerWindow(static_cast<int>(ui::ServerWindow::Tab::Modules));
    ui::ServerWindow* sw = window_->serverWindow();
    sw->resize(900, 560);
    ASSERT_TRUE(waitFor([&] { return sw->clubWaitingNote()->findChild<QPushButton*>(QStringLiteral("openClubPartners")); }));
    inBothThemes("notification-before-shots", [&](const QString& scheme) {
        waitFor([] { return false; }, 200);
        EXPECT_TRUE(window_->notices()->card(QStringLiteral("server-warning"))->grab().save(
            out + QStringLiteral("/before-warning-%1.png").arg(scheme)));
        EXPECT_TRUE(sw->grab().save(out + QStringLiteral("/before-club-requests-%1.png").arg(scheme)));
    });
}

TEST_F(NotificationsClubRequests, PicturesOfAnsweringOnTheModulesTab) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    ASSERT_TRUE(waitFor([&] { return window_->notifications()->support(http_.base()) == sync::NotificationCenter::Support::Yes; }));
    window_->showServerWindow(static_cast<int>(ui::ServerWindow::Tab::Modules));
    ui::ServerWindow* sw = window_->serverWindow();
    sw->resize(900, 560);
    ASSERT_TRUE(waitFor([&] { return sw->clubWaitingNote()->findChild<ui::NotificationRow*>(QStringLiteral("notificationRow:p1")); }));
    inBothThemes("notification-club-shots", [&](const QString& scheme) {
        waitFor([] { return false; }, 300);
        EXPECT_TRUE(sw->grab().save(out + QStringLiteral("/notifications-club-requests-%1.png").arg(scheme)));
    });
}
