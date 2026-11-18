// The server half of the Module library: the API calls, the live hints,
// a module's contents, the "On the server" and "Catalog" tabs, and the main
// window adding, saving, republishing and changing server modules, and
// showing your warnings.

#include "FakeHttp.h"

#include "EventStream.h"
#include "LibraryApi.h"
#include "ModuleDoc.h"
#include "ServerList.h"
#include "TokenStore.h"
#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/EditCommands.h"
#include "edit/ModuleCommands.h"
#include "edit/ModuleLibraryLink.h"
#include "parts/PartsLibrary.h"
#include "sync/Credit.h"
#include "ui/ConfirmDialog.h"
#include "ui/ImportPipeline.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/ModuleLibraryDialogs.h"
#include "ui/ModuleLibraryPanel.h"
#include "ui/ModuleShareDialog.h"
#include "ui/SharedModulesBar.h"
#include "core/SharedModule.h"
#include "import/LayoutFile.h"
#include "edit/SharedModulePlacement.h"
#include "parts/BrickPlacement.h"
#include <QClipboard>
#include "ui/ModulesPanel.h"
#include "ui/NoticeArea.h"
#include "ui/ReturnWording.h"
#include "ui/SaveModuleDialog.h"
#include "ui/ServerLibrary.h"
#include "ui/ServerWindow.h"
#include "ui/SheetChoiceDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/VenueLibraryPanel.h"

#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"
#include <QMessageBox>
#include <gtest/gtest.h>

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QToolButton>
#include <QUndoStack>
#include <QUrlQuery>

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

QJsonObject moduleJson(const QString& id, const QString& title, const QString& role, const QString& org = {},
                       qint64 thumbnailAt = 0, int version = 1) {
    QJsonObject owner = org.isEmpty() ? QJsonObject{ { QStringLiteral("kind"), QStringLiteral("user") }, { QStringLiteral("id"), QStringLiteral("u1") },
                                                      { QStringLiteral("name"), QStringLiteral("Ann") } }
                                      : QJsonObject{ { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("id"), org },
                                                      { QStringLiteral("name"), QStringLiteral("Train Club") }, { QStringLiteral("slug"), QStringLiteral("train") } };
    QJsonObject m{ { QStringLiteral("id"), id }, { QStringLiteral("title"), title }, { QStringLiteral("role"), role },
                   { QStringLiteral("owner"), owner }, { QStringLiteral("docVersion"), version },
                   { QStringLiteral("latestVersion"), version }, { QStringLiteral("updatedAt"), 1790000000000.0 } };
    m.insert(QStringLiteral("thumbnailAt"), thumbnailAt ? QJsonValue(static_cast<double>(thumbnailAt)) : QJsonValue());
    return m;
}

QJsonObject modules(const QJsonArray& list) { return { { QStringLiteral("modules"), list } }; }
QJsonObject orgs() {
    return { { QStringLiteral("orgs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") },
                                                                 { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                 { QStringLiteral("myRole"), QStringLiteral("member") } } } } };
}
QJsonObject catalogOn(bool on = true) { return { { QStringLiteral("modules"), on }, { QStringLiteral("parts"), on } }; }

// A module of two parts sheets, with parts the bundled library has.
std::unique_ptr<core::Map> sampleModule(int tracks = 2) {
    auto map = std::make_unique<core::Map>();
    auto add = [&](const QString& name, const QString& part, int n, double x) {
        auto l = std::make_unique<core::LayerBrick>();
        l->guid = core::newBbmId();
        l->name = name;
        for (int i = 0; i < n; ++i) {
            core::Brick b;
            b.guid = core::newBbmId();
            b.partNumber = part;
            b.displayArea = QRectF(x + i * 32, 0, 32, 32);
            l->bricks.push_back(b);
        }
        map->layers().push_back(std::move(l));
    };
    add(QStringLiteral("Baseplates"), QStringLiteral("3811.2"), 1, 0);
    add(QStringLiteral("Tracks"), QStringLiteral("3857.0"), tracks, 100);
    map->nbItems = sync::partCount(*map);
    return map;
}

QByteArray snapshotOf(const core::Map& m) { return sync::moduleSnapshotFor(m, {}); }

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

}  // namespace

// ---------- LibraryApi ------------------------------------------------------

TEST(LibraryApi, ListsModulesWithOwnersRolesVersionsAndPictures) {
    FakeHttp http;
    http.reply("/api/modules", 200,
               modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Yard"), QStringLiteral("owner"), {}, 1234, 3),
                         moduleJson(QStringLiteral("m2"), QStringLiteral("Shed"), QStringLiteral("viewer"), QStringLiteral("o1")) }));
    sync::LibraryApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_x"));
    QList<sync::ServerModule> got;
    api.listModules([&](const QList<sync::ServerModule>& l) { got = l; }, {});
    ASSERT_TRUE(waitFor([&] { return got.size() == 2; }));
    EXPECT_EQ(http.requests.back().authorization, QByteArray("Bearer bld_pat_x"));
    EXPECT_EQ(got[0].title, QStringLiteral("Yard"));
    EXPECT_TRUE(got[0].canEdit());
    EXPECT_TRUE(got[0].canDelete());
    EXPECT_EQ(got[0].shownVersion(), 3);
    EXPECT_EQ(got[0].thumbnailPath(), QStringLiteral("/api/modules/m1/thumbnail?v=1234"));
    EXPECT_FALSE(got[0].owner.isClub());
    EXPECT_TRUE(got[1].owner.isClub());
    EXPECT_EQ(got[1].owner.name, QStringLiteral("Train Club"));
    EXPECT_FALSE(got[1].canEdit());
    EXPECT_TRUE(got[1].thumbnailPath().isEmpty());
}

TEST(LibraryApi, SavesANewVersionWithItsNoteAsBytes) {
    FakeHttp http;
    http.reply("/api/modules/m1/snapshot?note=Longer%20siding", 200, { { QStringLiteral("version"), 4 } });
    sync::LibraryApi api;
    api.setBase(http.base());
    int version = 0;
    api.saveModuleSnapshot(QStringLiteral("m1"), QByteArray("\x01\x02", 2), QStringLiteral("  Longer siding "),
                           [&](int v) { version = v; }, {});
    ASSERT_TRUE(waitFor([&] { return version == 4; }));
    EXPECT_EQ(http.requests.back().method, QByteArray("PUT"));
    EXPECT_EQ(http.requests.back().body, QByteArray("\x01\x02", 2));
}

TEST(LibraryApi, AcknowledgesANoticeAndReadsTheList) {
    FakeHttp http;
    http.reply("/api/notices", 200,
               { { QStringLiteral("notices"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("w1") }, { QStringLiteral("scope"), QStringLiteral("club") },
                                            { QStringLiteral("severity"), QStringLiteral("final") }, { QStringLiteral("reason"), QStringLiteral("Stop it") },
                                            { QStringLiteral("acknowledgedAt"), QJsonValue() },
                                            { QStringLiteral("club"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("Train Club") } } },
                                            { QStringLiteral("to"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("name"), QStringLiteral("Train Club") } } } } } } });
    http.reply("/api/notices/w1/acknowledge", 200, { { QStringLiteral("ok"), true } });
    sync::LibraryApi api;
    api.setBase(http.base());
    QList<sync::Notice> got;
    api.notices([&](const QList<sync::Notice>& l) { got = l; }, {});
    ASSERT_TRUE(waitFor([&] { return !got.isEmpty(); }));
    EXPECT_FALSE(got[0].acknowledged);
    EXPECT_EQ(got[0].severity, QStringLiteral("final"));
    EXPECT_EQ(got[0].clubName, QStringLiteral("Train Club"));
    EXPECT_EQ(got[0].toClub, QStringLiteral("Train Club"));
    bool acked = false;
    api.acknowledge(QStringLiteral("w1"), [&] { acked = true; }, {});
    ASSERT_TRUE(waitFor([&] { return acked; }));
    EXPECT_EQ(http.requests.back().method, QByteArray("POST"));
}

// A club's roles say no: the permission the server names, in plain words.
// A server from before club roles names none.
TEST(LibraryApi, ARefusalSaysWhy) {
    FakeHttp http;
    http.reply("/api/modules", 403,
               { { QStringLiteral("error"), QStringLiteral("only_club_admins_can_add") },
                 { QStringLiteral("permission"), QStringLiteral("edit_modules") } });
    http.reply("/api/modules", 403, { { QStringLiteral("error"), QStringLiteral("only_club_admins_can_add") } });
    sync::LibraryApi api;
    api.setBase(http.base());
    QString why;
    const auto create = [&] {
        why.clear();
        api.createModule(QStringLiteral("Yard"), QStringLiteral("train"), {}, [&](const sync::ServerRefusal& r) {
            why = ui::ServerLibrary::refusalText(r);
        });
        return waitFor([&] { return !why.isEmpty(); });
    };
    ASSERT_TRUE(create());
    EXPECT_EQ(why, QStringLiteral("Your roles in this club can't add that. Ask a club admin for the Add or edit modules "
                                  "permission."));
    EXPECT_TRUE(http.requests.back().body.contains("\"orgSlug\":\"train\""));
    ASSERT_TRUE(create());
    EXPECT_EQ(why, QStringLiteral("This club only lets some of its members add things. Ask a club admin."));
}

// GET /api/orgs: each club's permissions, when the server has club roles.
TEST(LibraryApi, ClubsCarryTheirPermissions) {
    FakeHttp http;
    http.reply("/api/orgs", 200,
               { { QStringLiteral("orgs"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("crew") },
                                            { QStringLiteral("name"), QStringLiteral("Track Crew") },
                                            { QStringLiteral("myRole"), QStringLiteral("member") },
                                            { QStringLiteral("canAdd"), false },
                                            { QStringLiteral("permissions"),
                                              QJsonArray{ QStringLiteral("view_items"), QStringLiteral("add_parts") } } },
                               QJsonObject{ { QStringLiteral("slug"), QStringLiteral("old") },
                                            { QStringLiteral("name"), QStringLiteral("Old Club") },
                                            { QStringLiteral("myRole"), QStringLiteral("member") } } } } });
    sync::LibraryApi api;
    api.setBase(http.base());
    QList<sync::OrgEntry> orgs;
    api.orgs([&](const QList<sync::OrgEntry>& l) { orgs = l; }, {});
    ASSERT_TRUE(waitFor([&] { return orgs.size() == 2; }));
    ASSERT_TRUE(orgs[0].permissions);
    EXPECT_EQ(*orgs[0].permissions, (QStringList{ QStringLiteral("view_items"), QStringLiteral("add_parts") }));
    EXPECT_TRUE(orgs[0].canAddKind(sync::AddKind::Part));
    EXPECT_FALSE(orgs[0].canAddKind(sync::AddKind::Module));
    EXPECT_FALSE(orgs[1].permissions);
    EXPECT_TRUE(orgs[1].canAddKind(sync::AddKind::Module));
}

// ---------- Live hints --------------------------------------------------------

TEST(EventStreamParser, ReadsDataMessagesAcrossChunksAndSkipsComments) {
    sync::EventStreamParser p;
    EXPECT_TRUE(p.feed("retry: 5000\n\n: connected\n\n").isEmpty());
    EXPECT_EQ(p.retryMs(), 5000);
    EXPECT_TRUE(p.feed("data: {\"kind\":\"mod").isEmpty());
    const auto got = p.feed("ule\",\"id\":\"m1\"}\r\n\r\n: ping\n\ndata: not json\n\ndata: {\"kind\":\"warning\"}\n\n");
    ASSERT_EQ(got.size(), 2);
    EXPECT_EQ(got[0].value(QLatin1String("kind")).toString(), QStringLiteral("module"));
    EXPECT_EQ(got[0].value(QLatin1String("id")).toString(), QStringLiteral("m1"));
    EXPECT_EQ(got[1].value(QLatin1String("kind")).toString(), QStringLiteral("warning"));
}

TEST(EventStream, SendsHintsReconnectsAndStopsWhenRefused) {
    // A server that streams one hint per connection and then hangs up;
    // the third connection is refused.
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    int connections = 0;
    QByteArray auth;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (QTcpSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QTcpSocket::readyRead, s, [&, s] {
                const QByteArray req = s->readAll();
                if (!req.contains("\r\n\r\n")) return;
                for (const QByteArray& l : req.split('\n'))
                    if (l.toLower().startsWith("authorization:")) auth = l.mid(14).trimmed();
                ++connections;
                if (connections >= 3) {
                    s->write("HTTP/1.1 401 X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else {
                    const QByteArray body = "retry: 10\n\n: connected\n\ndata: {\"kind\":\"module\",\"n\":" + QByteArray::number(connections) + "}\n\n";
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " + QByteArray::number(body.size()) +
                             "\r\nConnection: close\r\n\r\n" + body);
                }
                s->disconnectFromHost();
            });
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    });
    sync::EventStream stream;
    stream.setReconnectDelay(20, 40);
    QList<int> hints;
    int reconnects = 0, refused = 0;
    QObject::connect(&stream, &sync::EventStream::hint, [&](const QJsonObject& h) { hints << h.value(QLatin1String("n")).toInt(); });
    QObject::connect(&stream, &sync::EventStream::reconnected, [&] { ++reconnects; });
    QObject::connect(&stream, &sync::EventStream::refused, [&](int status) { refused = status; });
    stream.start(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())), QStringLiteral("bld_pat_live"));
    ASSERT_TRUE(waitFor([&] { return refused != 0; }));
    EXPECT_EQ(hints, (QList<int>{ 1, 2 }));
    EXPECT_EQ(reconnects, 1);
    EXPECT_EQ(refused, 401);
    EXPECT_FALSE(stream.running());
    EXPECT_EQ(auth, QByteArray("Bearer bld_pat_live"));
}

// ---------- A module's contents ----------------------------------------------

TEST(ModuleDoc, AMapSavedAsAModuleReadsBackSheetBySheet) {
    auto module = sampleModule(3);
    const QByteArray bytes = snapshotOf(*module);
    ASSERT_FALSE(bytes.isEmpty());
    QString error;
    auto back = sync::mapFromModuleSnapshot(bytes, &error);
    ASSERT_TRUE(back) << error.toStdString();
    ASSERT_EQ(back->layers().size(), 2u);
    EXPECT_EQ(back->layers()[0]->name, QStringLiteral("Baseplates"));
    EXPECT_EQ(back->layers()[1]->name, QStringLiteral("Tracks"));
    EXPECT_EQ(sync::partCount(*back), 4);
    EXPECT_EQ(static_cast<const core::LayerBrick&>(*back->layers()[1]).bricks[2].displayArea, QRectF(164, 0, 32, 32));
}

TEST(ModuleDoc, ANewVersionReplacesWhatTheServerHeld) {
    const QByteArray first = snapshotOf(*sampleModule(3));
    auto smaller = sampleModule(1);
    const QByteArray second = sync::moduleSnapshotFor(*smaller, first);
    auto back = sync::mapFromModuleSnapshot(second);
    ASSERT_TRUE(back);
    EXPECT_EQ(sync::partCount(*back), 2);
    QString error;
    EXPECT_FALSE(sync::mapFromModuleSnapshot(QByteArray("nonsense"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(sync::moduleSnapshotFor(*smaller, QByteArray("nonsense")).isEmpty());
}

// A snapshot from the server holding bytes yrs can't take (a clock past
// UpdateGuard's kMaxClock): refused with a reason, for the message the
// window shows, both when opening it and when saving a new version on top.
TEST(ModuleDoc, RejectsMalformedDocumentsWithoutCrashing) {
    for (const char* name :
         { "ydoc-clock-past-the-limit.bin", "ydoc-clock-past-the-limit-after-a-gap.bin" }) {
        SCOPED_TRACE(name);
        QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/fuzz-regressions/") + QLatin1String(name));
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QString error;
        EXPECT_FALSE(sync::mapFromModuleSnapshot(bytes, &error));
        EXPECT_TRUE(error.contains(QLatin1String("clock"))) << error.toStdString();
        error.clear();
        EXPECT_TRUE(sync::moduleSnapshotFor(*sampleModule(1), bytes, &error).isEmpty());
        EXPECT_TRUE(error.contains(QLatin1String("clock"))) << error.toStdString();
    }
}

// ---------- The tabs ----------------------------------------------------------

class LibraryTabs : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        cache_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/library-test");
        QDir(cache_).removeRecursively();
        library_.setPictureCacheRoot(cache_);
        http_.reply("/api/orgs", 200, orgs());
        http_.reply("/api/catalog/settings", 200, catalogOn());
    }
    void TearDown() override { QDir(cache_).removeRecursively(); }
    void serveModules() {
        http_.reply("/api/modules", 200,
                    modules({ moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"), QStringLiteral("editor"), QStringLiteral("o1")),
                              moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner"), {}, 77),
                              moduleJson(QStringLiteral("m2"), QStringLiteral("Station"), QStringLiteral("viewer")) }));
    }
    void connectSignedIn() {
        library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_t"));
        ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready; }));
        QCoreApplication::processEvents();
    }
    // The visible texts of the tab, top to bottom (headers and titles).
    QStringList texts(QWidget* tab) {
        QStringList out;
        QCoreApplication::processEvents();
        QList<QLabel*> labels = tab->findChildren<QLabel*>();
        std::stable_sort(labels.begin(), labels.end(), [](QLabel* a, QLabel* b) {
            return a->mapTo(a->window(), QPoint()).y() < b->mapTo(b->window(), QPoint()).y();
        });
        for (QLabel* l : labels)
            if (l->isVisibleTo(tab) && (l->property("groupHeader").toBool() || l->property("rowTitle").toBool())) out << l->text();
        return out;
    }
    FakeHttp http_;
    ui::ServerLibrary library_;
    QString cache_;
};

// A module copied before copies had a first version has version 0 on the server: the row says v1.
TEST_F(LibraryTabs, ACopyMadeBeforeItHadAFirstVersionShowsVersion1) {
    http_.reply("/api/modules", 200,
                modules({ moduleJson(QStringLiteral("c0"), QStringLiteral("Old copy"), QStringLiteral("owner"), QStringLiteral("o1"), 0, 0),
                          moduleJson(QStringLiteral("c3"), QStringLiteral("Saved twice"), QStringLiteral("owner"), QStringLiteral("o1"), 0, 2) }));
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    EXPECT_EQ(tab.row(QStringLiteral("c0"))->findChild<QLabel*>(QStringLiteral("rowSubtitle"))->text().left(5), QStringLiteral("v1 · "));
    EXPECT_EQ(tab.row(QStringLiteral("c3"))->findChild<QLabel*>(QStringLiteral("rowSubtitle"))->text().left(5), QStringLiteral("v2 · "));
    sync::ServerModule none;
    EXPECT_EQ(none.shownVersion(), 1);
}

TEST_F(LibraryTabs, YoursFirstThenEachClubWithVersionsAndPictures) {
    serveModules();
    QByteArray png;
    {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
    }
    http_.replyRaw("/api/modules/m1/thumbnail?v=77&size=small", 200, png, "image/png");
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Yours"), QStringLiteral("Freight yard"), QStringLiteral("Station"),
                                         QStringLiteral("Train Club"), QStringLiteral("Engine shed") }));
    QWidget* yard = tab.row(QStringLiteral("m1"));
    ASSERT_NE(yard, nullptr);
    EXPECT_EQ(yard->findChild<QLabel*>(QStringLiteral("rowSubtitle"))->text().left(5), QStringLiteral("v1 · "));
    EXPECT_EQ(yard->findChild<QPushButton*>(QStringLiteral("insertModule"))->text(), QStringLiteral("Add to layout"));
    // Its picture comes once, then from the cache folder.
    auto* pic = yard->findChild<QLabel*>(QStringLiteral("rowPicture"));
    ASSERT_TRUE(waitFor([&] { return !pic->pixmap().isNull(); }));
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
    QWidget* before = tab.row(QStringLiteral("m1"));
    library_.refresh();
    ASSERT_TRUE(waitFor([&] {
        QWidget* now = tab.row(QStringLiteral("m1"));
        return now && now != before && !now->findChild<QLabel*>(QStringLiteral("rowPicture"))->pixmap().isNull();
    }));
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
    // A viewer may look, not change.
    auto* more = tab.row(QStringLiteral("m2"))->findChild<QToolButton*>(QStringLiteral("moduleMore"));
    EXPECT_FALSE(more->menu()->findChild<QAction*>(QStringLiteral("openModule"))->isEnabled());
    // Filtering.
    tab.filter()->setText(QStringLiteral("shed"));
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Train Club"), QStringLiteral("Engine shed") }));
    tab.filter()->setText(QStringLiteral("nothing like it"));
    EXPECT_NE(tab.findChild<QLabel*>(QStringLiteral("serverModulesEmpty")), nullptr);
}

TEST_F(LibraryTabs, AModuleWithNoPartsSaysSoAndCantBeAdded) {
    QJsonObject blank = moduleJson(QStringLiteral("m9"), QStringLiteral("Blank"), QStringLiteral("owner"));
    blank.insert(QStringLiteral("empty"), true);
    QJsonObject full = moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner"));
    full.insert(QStringLiteral("empty"), false);
    http_.reply("/api/modules", 200, modules({ full, blank }));
    ui::ServerModulesTab tab(library_);
    tab.show();
    connectSignedIn();
    QString inserted;
    QObject::connect(&library_, &ui::ServerLibrary::insertRequested, [&](const QString& id) { inserted = id; });
    QWidget* row = tab.row(QStringLiteral("m9"));
    ASSERT_NE(row, nullptr);
    auto* note = row->findChild<QLabel*>(QStringLiteral("rowEmpty"));
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->text(), QStringLiteral("No parts yet"));
    auto* add = row->findChild<QPushButton*>(QStringLiteral("insertModule"));
    ASSERT_NE(add, nullptr);
    EXPECT_FALSE(add->isEnabled());
    add->click();
    EXPECT_TRUE(inserted.isEmpty());
    // One with parts is as before.
    QWidget* yard = tab.row(QStringLiteral("m1"));
    EXPECT_EQ(yard->findChild<QLabel*>(QStringLiteral("rowEmpty")), nullptr);
    EXPECT_TRUE(yard->findChild<QPushButton*>(QStringLiteral("insertModule"))->isEnabled());
}

TEST_F(LibraryTabs, APictureOnItsWayDuringARefreshStillArrives) {
    serveModules();
    QByteArray png;
    {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
    }
    http_.replyRaw("/api/modules/m1/thumbnail?v=77&size=small", 200, png, "image/png");
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    // A live hint refreshes the list while the picture is still coming.
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/modules/m1/thumbnail") == 1; }, 5000));
    library_.refresh();
    ASSERT_TRUE(waitFor([&] {
        QWidget* row = tab.row(QStringLiteral("m1"));
        return row && !row->findChild<QLabel*>(QStringLiteral("rowPicture"))->pixmap().isNull();
    }));
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
}

TEST_F(LibraryTabs, APictureTheServerHasntGotIsAskedForOnce) {
    // m1 says it has a picture, but the server answers 404 (FakeHttp's default).
    serveModules();
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/modules/m1/thumbnail") == 1; }));
    waitFor([] { return false; }, 300);  // its 404 lands
    // Every refresh (each live hint) redraws the rows: still asked only once,
    // so a show's shared address never sends a burst of 404s.
    for (int i = 0; i < 3; ++i) {
        QWidget* before = tab.row(QStringLiteral("m1"));
        library_.refresh();
        ASSERT_TRUE(waitFor([&] { QWidget* now = tab.row(QStringLiteral("m1")); return now && now != before; }));
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
    // Signing in again asks afresh.
    library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_u"));
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/modules/m1/thumbnail") == 2; }));
}

TEST_F(LibraryTabs, TheModuleBeingChangedSaysEditingNowAndTheOthersGoIntoIt) {
    serveModules();
    ui::ServerModulesTab tab(library_);
    connectSignedIn();
    QString inserted;
    QObject::connect(&library_, &ui::ServerLibrary::insertRequested, [&](const QString& id) { inserted = id; });
    library_.setEditingModule(QStringLiteral("m1"));
    QWidget* yard = tab.row(QStringLiteral("m1"));
    ASSERT_NE(yard, nullptr);
    EXPECT_NE(yard->findChild<QLabel*>(QStringLiteral("editingNow")), nullptr);
    EXPECT_EQ(yard->findChild<QPushButton*>(QStringLiteral("insertModule")), nullptr);
    auto* add = tab.row(QStringLiteral("c1"))->findChild<QPushButton*>(QStringLiteral("insertModule"));
    EXPECT_EQ(add->text(), QStringLiteral("Add to this module"));
    add->click();
    EXPECT_EQ(inserted, QStringLiteral("c1"));
    library_.setEditingModule({});
    EXPECT_EQ(tab.row(QStringLiteral("m1"))->findChild<QLabel*>(QStringLiteral("editingNow")), nullptr);
}

TEST_F(LibraryTabs, RenameAndDeleteAskTheServerThenListAgain) {
    serveModules();
    http_.reply("/api/modules/m1", 200, { { QStringLiteral("ok"), true } });
    ui::ServerModulesTab tab(library_);
    tab.askName = [](const sync::ServerModule&) { return QStringLiteral("Big yard"); };
    tab.confirmDelete = [](const sync::ServerModule&) { return true; };
    connectSignedIn();
    auto menuAction = [&](const char* name) {
        return tab.row(QStringLiteral("m1"))->findChild<QToolButton*>(QStringLiteral("moduleMore"))->menu()->findChild<QAction*>(QString::fromLatin1(name));
    };
    const int lists = count(http_, "GET", "/api/modules");
    menuAction("renameModule")->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PATCH", "/api/modules/m1") && count(http_, "GET", "/api/modules") > lists; }));
    EXPECT_TRUE(lastRequest(http_, "PATCH", "/api/modules/m1")->body.contains("\"title\":\"Big yard\""));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready && tab.row(QStringLiteral("m1")); }));
    menuAction("deleteModule")->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "DELETE", "/api/modules/m1"); }));
}

// Author credit: "by Sam · in Train Club", and the club's module back to its author.
TEST(Credit, ReadsTheServersCreditAndSaysItInTheWebsitesWords) {
    const QJsonObject item{ { QStringLiteral("credit"),
                              QJsonObject{ { QStringLiteral("by"), QStringLiteral("Sam") },
                                           { QStringLiteral("authorName"), QStringLiteral("Sam") },
                                           { QStringLiteral("club"), QStringLiteral("ArkLUG") },
                                           { QStringLiteral("basedOn"),
                                             QJsonObject{ { QStringLiteral("id"), QStringLiteral("x") },
                                                          { QStringLiteral("title"), QStringLiteral("Yard") },
                                                          { QStringLiteral("by"), QStringLiteral("Bo") } } },
                                           { QStringLiteral("canTakeBack"), true },
                                           { QStringLiteral("canGiveBack"), false } } } };
    const sync::Credit c = sync::creditFromJson(item);
    EXPECT_TRUE(c.canTakeBack);
    EXPECT_FALSE(c.canGiveBack);
    EXPECT_EQ(sync::creditLine(c), QStringLiteral("by Sam · in ArkLUG · based on Yard by Bo"));
    sync::Credit mine;
    mine.by = QStringLiteral("you");
    EXPECT_EQ(sync::creditLine(mine), QStringLiteral("by you"));
    // Older servers send none: nothing shown, nothing offered.
    const sync::Credit none = sync::creditFromJson(QJsonObject{});
    EXPECT_FALSE(none.any());
    EXPECT_TRUE(sync::creditLine(none).isEmpty());
    const auto w = sync::moveToClubWording(QStringLiteral("ArkLUG"));
    EXPECT_EQ(w.removes,
              QStringLiteral("ArkLUG will own this. Members with the Delete club items permission can "
                             "rename, share or delete it."));
    EXPECT_EQ(
        w.keeps,
        QStringLiteral("You stay credited as the author, and you can take it back while you’re a member."));
    // Take back / Give back: who hears of it goes by permission, as on the website.
    sync::Credit given;
    given.club = QStringLiteral("ArkLUG");
    given.authorName = QStringLiteral("Sam");
    for (const bool give : { false, true }) {
        const ui::ConfirmOptions o =
            ui::returnOptions(ui::ReturnKind::Module, QStringLiteral("Yard"), given, give);
        EXPECT_TRUE(
            o.keeps.endsWith(QStringLiteral("Members with the Delete club items permission are told.")))
            << o.keeps.toStdString();
        EXPECT_FALSE(o.keeps.contains(QStringLiteral("admins and managers")));
    }
}

namespace {
QJsonObject withCredit(QJsonObject m, const QString& by, bool take, bool give) {
    m.insert(QStringLiteral("credit"), QJsonObject{ { QStringLiteral("by"), by },
                                                    { QStringLiteral("authorName"), QStringLiteral("Sam") },
                                                    { QStringLiteral("club"), QStringLiteral("Train Club") },
                                                    { QStringLiteral("canTakeBack"), take },
                                                    { QStringLiteral("canGiveBack"), give } });
    return m;
}
} // namespace

TEST_F(LibraryTabs, TheClubsModuleShowsItsAuthorAndGoesBackAfterAsking) {
    http_.reply("/api/modules", 200,
                modules({ withCredit(moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"),
                                                QStringLiteral("editor"), QStringLiteral("o1")),
                                     QStringLiteral("you"), true, false),
                          withCredit(moduleJson(QStringLiteral("c2"), QStringLiteral("Turntable"),
                                                QStringLiteral("owner"), QStringLiteral("o1")),
                                     QStringLiteral("Sam"), false, true),
                          moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"),
                                     QStringLiteral("owner")) }));
    http_.reply("/api/modules/c1/take-back", 200,
                { { QStringLiteral("ok"), true }, { QStringLiteral("keptCopyId"), QStringLiteral("c9") } });
    http_.reply("/api/modules/c2/give-back", 200,
                { { QStringLiteral("ok"), true }, { QStringLiteral("keptCopyId"), QStringLiteral("c8") } });
    ui::ServerModulesTab tab(library_);
    QList<std::pair<QString, bool>> asked;
    bool answer = false;
    tab.confirmReturn = [&](const sync::ServerModule& m, bool give) {
        asked.append({ m.title, give });
        return answer;
    };
    connectSignedIn();
    EXPECT_EQ(tab.row(QStringLiteral("c1"))->findChild<QLabel*>(QStringLiteral("rowCredit"))->text(),
              QStringLiteral("by you · in Train Club"));
    EXPECT_EQ(tab.row(QStringLiteral("c2"))->findChild<QLabel*>(QStringLiteral("rowCredit"))->text(),
              QStringLiteral("by Sam · in Train Club"));
    EXPECT_EQ(tab.row(QStringLiteral("m1"))->findChild<QLabel*>(QStringLiteral("rowCredit")),
              nullptr); // no credit sent
    const auto menu = [&](const char* row) {
        return tab.row(QString::fromLatin1(row))
            ->findChild<QToolButton*>(QStringLiteral("moduleMore"))
            ->menu();
    };
    // The author takes theirs back; a club's runner gives one back. Neither shows where it doesn't apply.
    EXPECT_EQ(menu("c1")->findChild<QAction*>(QStringLiteral("giveBackModule")), nullptr);
    EXPECT_EQ(menu("c2")->findChild<QAction*>(QStringLiteral("takeBackModule")), nullptr);
    EXPECT_EQ(menu("m1")->findChild<QAction*>(QStringLiteral("takeBackModule")), nullptr);
    EXPECT_EQ(menu("c2")->findChild<QAction*>(QStringLiteral("giveBackModule"))->text(),
              QStringLiteral("Give Back to Sam…"));

    // Cancel: nothing is sent.
    menu("c1")->findChild<QAction*>(QStringLiteral("takeBackModule"))->trigger();
    EXPECT_FALSE(
        waitFor([&] { return lastRequest(http_, "POST", "/api/modules/c1/take-back") != nullptr; }, 300));
    // Take back.
    answer = true;
    const int lists = count(http_, "GET", "/api/modules");
    menu("c1")->findChild<QAction*>(QStringLiteral("takeBackModule"))->trigger();
    ASSERT_TRUE(waitFor([&] {
        return lastRequest(http_, "POST", "/api/modules/c1/take-back")
               && count(http_, "GET", "/api/modules") > lists;
    }));
    EXPECT_EQ(lastRequest(http_, "POST", "/api/modules/c1/take-back")->authorization,
              QByteArray("Bearer bld_pat_t"));
    ASSERT_TRUE(waitFor([&] {
        return library_.state() == ui::ServerLibrary::State::Ready && tab.row(QStringLiteral("c2"));
    }));
    menu("c2")->findChild<QAction*>(QStringLiteral("giveBackModule"))->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/modules/c2/give-back") != nullptr; }));
    EXPECT_EQ(lastRequest(http_, "POST", "/api/modules/c2/take-back"), nullptr);
    EXPECT_EQ(asked, (QList<std::pair<QString, bool>>{ { QStringLiteral("Engine shed"), false },
                                                       { QStringLiteral("Engine shed"), false },
                                                       { QStringLiteral("Turntable"), true } }));
}

TEST_F(LibraryTabs, SignedOutOfflineAndRefusedEachSayWhatToDo) {
    ui::ServerModulesTab tab(library_);
    tab.show();
    auto* line = tab.findChild<ui::LibraryStatusLine*>();
    ASSERT_NE(line, nullptr);
    // No server yet.
    EXPECT_EQ(library_.state(), ui::ServerLibrary::State::NoServer);
    EXPECT_EQ(line->button()->text(), QStringLiteral("Add a server…"));
    // Not signed in: Sign in.
    library_.setServer(http_.base(), QStringLiteral("Club server"), QString());
    EXPECT_FALSE(line->isHidden());
    EXPECT_EQ(line->button()->text(), QStringLiteral("Sign in"));
    EXPECT_TRUE(line->text()->text().contains(QStringLiteral("Sign in to Club server")));
    int asked = 0;
    QObject::connect(&library_, &ui::ServerLibrary::signInRequested, [&] { ++asked; });
    line->button()->click();
    EXPECT_EQ(asked, 1);
    // A token the server no longer takes: back to Sign in.
    http_.reply("/api/modules", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
    library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_old"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::SignedOut; }));
    // A 403: why, and Try again.
    http_.clear("/api/modules");
    http_.reply("/api/modules", 403, { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_narrow"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Refused; }));
    EXPECT_TRUE(line->text()->text().contains(QStringLiteral("Sign in again")));
    EXPECT_EQ(line->button()->text(), QStringLiteral("Try again"));
    // Offline: Try again asks once more, and the list comes.
    QTcpServer gone;
    gone.listen(QHostAddress::LocalHost, 0);
    const QUrl unreachable(QStringLiteral("http://127.0.0.1:%1").arg(gone.serverPort()));
    gone.close();
    library_.setServer(unreachable, QStringLiteral("Away"), QStringLiteral("bld_pat_t"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Offline; }));
    // Why, in Connect's words, and that the modules here still work.
    EXPECT_TRUE(line->text()->text().startsWith(QStringLiteral("127.0.0.1 isn't answering."))) << line->text()->text().toStdString();
    EXPECT_TRUE(line->text()->text().endsWith(QStringLiteral("Your modules on this computer still work.")));
    EXPECT_EQ(line->button()->text(), QStringLiteral("Try again"));
}

TEST_F(LibraryTabs, CatalogAddsModulesAndPartsAndWholeCollections) {
    serveModules();
    http_.reply("/api/catalog/items?kind=module&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("i1") }, { QStringLiteral("kind"), QStringLiteral("module") },
                                             { QStringLiteral("title"), QStringLiteral("Harbour crane") }, { QStringLiteral("by"), QStringLiteral("Bob") },
                                             { QStringLiteral("uses"), 1 }, { QStringLiteral("version"), 1 },
                                             { QStringLiteral("previewUrl"), QStringLiteral("/api/catalog/items/i1/preview?v=1") } } } } });
    http_.reply("/api/catalog/items?kind=part&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("p1") }, { QStringLiteral("kind"), QStringLiteral("part") },
                                             { QStringLiteral("title"), QStringLiteral("Signal") }, { QStringLiteral("by"), QStringLiteral("Bob") },
                                             { QStringLiteral("uses"), 3 } } } } });
    http_.reply("/api/catalog/items/i1/add", 201, { { QStringLiteral("kind"), QStringLiteral("module") }, { QStringLiteral("id"), QStringLiteral("m9") } });
    http_.reply("/api/catalog/items/p1/add", 201, { { QStringLiteral("kind"), QStringLiteral("part") }, { QStringLiteral("id"), QStringLiteral("cp9") } });
    http_.reply("/api/catalog/collections", 200,
                { { QStringLiteral("collections"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("k1") }, { QStringLiteral("title"), QStringLiteral("Harbour starter") },
                                             { QStringLiteral("by"), QStringLiteral("Bob") }, { QStringLiteral("featured"), true },
                                             { QStringLiteral("itemCount"), 3 }, { QStringLiteral("modules"), 2 }, { QStringLiteral("parts"), 1 } } } } });
    http_.reply("/api/catalog/collections/k1/add", 200,
                { { QStringLiteral("added"), QJsonArray{ QJsonObject{}, QJsonObject{} } }, { QStringLiteral("skipped"), QJsonArray{ QStringLiteral("x") } },
                  { QStringLiteral("failed"), QJsonArray{} } });
    // Club roles: Train Club takes modules and parts, Track Crew only parts.
    const auto club = [](const char* slug, const char* name, const QStringList& permissions) {
        return QJsonObject{ { QStringLiteral("slug"), QLatin1String(slug) }, { QStringLiteral("name"), QLatin1String(name) },
                            { QStringLiteral("myRole"), QStringLiteral("member") }, { QStringLiteral("canAdd"), false },
                            { QStringLiteral("permissions"), QJsonArray::fromStringList(permissions) } };
    };
    http_.clear("/api/orgs");
    http_.reply("/api/orgs", 200,
                { { QStringLiteral("orgs"), QJsonArray{ club("train", "Train Club", { QStringLiteral("edit_modules"), QStringLiteral("add_parts") }),
                                                        club("crew", "Track Crew", { QStringLiteral("add_parts") }) } } });
    ui::CatalogTab tab(library_);
    // Asked where it goes (a club of yours takes it): to the club. Only the
    // clubs whose roles take that kind are offered; the others say why.
    QStringList asked;
    QList<QStringList> offered;
    QStringList leftOut;
    tab.chooseOwner = [&](const QString& title, const QList<sync::OrgEntry>& clubs,
                          const QString& why) -> std::optional<QString> {
        asked << title;
        QStringList names;
        for (const auto& c : clubs) names << c.name;
        offered << names;
        leftOut << why;
        return QStringLiteral("train");
    };
    connectSignedIn();
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("i1")); }));
    QString toInsert;
    QObject::connect(&library_, &ui::ServerLibrary::catalogInsertRequested, [&](const sync::CatalogItem& it) { toInsert = it.id; });
    tab.row(QStringLiteral("i1"))->findChild<QPushButton*>(QStringLiteral("catalogInsert"))->click();
    EXPECT_EQ(toInsert, QStringLiteral("i1"));
    tab.row(QStringLiteral("i1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/catalog/items/i1/add"); }));
    EXPECT_TRUE(lastRequest(http_, "POST", "/api/catalog/items/i1/add")->body.contains("\"orgSlug\":\"train\""));
    EXPECT_EQ(asked.value(0), QStringLiteral("Add to my modules"));
    EXPECT_EQ(offered.value(0), QStringList{ QStringLiteral("Train Club") });
    EXPECT_EQ(leftOut.value(0), QStringLiteral("Track Crew isn't listed: your roles there can't add modules. Ask a club "
                                               "admin for the Add or edit modules permission."));
    // A refresh (a live change, back in the window) rebuilds the rows but
    // keeps what Add did, instead of offering it again.
    const auto addText = [&] {
        QWidget* r = tab.row(QStringLiteral("i1"));
        return r ? r->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->text() : QString();
    };
    ASSERT_TRUE(waitFor([&] { return addText() == QStringLiteral("Added"); }));
    QWidget* before = tab.row(QStringLiteral("i1"));
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("i1")) && tab.row(QStringLiteral("i1")) != before; }));
    EXPECT_EQ(addText(), QStringLiteral("Added"));
    EXPECT_FALSE(tab.row(QStringLiteral("i1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->isEnabled());

    // Parts: added, then the server's parts are fetched.
    int partsAdded = 0;
    QObject::connect(&library_, &ui::ServerLibrary::partsAdded, [&] { ++partsAdded; });
    tab.setKind(ui::CatalogTab::Kind::Parts);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("p1")); }));
    EXPECT_EQ(tab.row(QStringLiteral("p1"))->findChild<QPushButton*>(QStringLiteral("catalogInsert")), nullptr);
    tab.row(QStringLiteral("p1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->click();
    ASSERT_TRUE(waitFor([&] { return partsAdded == 1; }));
    EXPECT_EQ(offered.value(1), (QStringList{ QStringLiteral("Train Club"), QStringLiteral("Track Crew") }));
    EXPECT_TRUE(leftOut.value(1).isEmpty());

    // Collections: Add all, and what happened.
    tab.setKind(ui::CatalogTab::Kind::Collections);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("k1")); }));
    tab.row(QStringLiteral("k1"))->findChild<QPushButton*>(QStringLiteral("collectionAddAll"))->click();
    ASSERT_TRUE(waitFor([&] { return tab.note()->text() == QStringLiteral("Added 2 items · already had 1"); }));
    EXPECT_EQ(partsAdded, 2);
    // Modules and parts: only a club that takes both.
    EXPECT_EQ(offered.value(2), QStringList{ QStringLiteral("Train Club") });
}

TEST_F(LibraryTabs, CatalogLayoutsAndVenuesShowWhenOnAndCopyThenOpen) {
    serveModules();
    // Modules on; layouts and venues off at first: no tabs for them.
    http_.clear("/api/catalog/settings");
    http_.reply("/api/catalog/settings", 200, { { QStringLiteral("modules"), true }, { QStringLiteral("parts"), false } });
    http_.reply("/api/catalog/items?kind=module&sort=popular", 200, { { QStringLiteral("items"), QJsonArray{} } });
    http_.reply("/api/catalog/items?kind=layout&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("l1") }, { QStringLiteral("kind"), QStringLiteral("layout") },
                                             { QStringLiteral("title"), QStringLiteral("Harbour town") }, { QStringLiteral("by"), QStringLiteral("Bob · in ArkLUG") },
                                             { QStringLiteral("uses"), 2 }, { QStringLiteral("version"), 1 },
                                             { QStringLiteral("previewUrl"), QStringLiteral("/api/catalog/items/l1/preview?v=1") },
                                             { QStringLiteral("coverUrl"), QStringLiteral("/api/catalog/items/l1/cover?image=c1&size=small") },
                                             { QStringLiteral("summary"), QJsonObject{ { QStringLiteral("widthStuds"), 960 }, { QStringLiteral("heightStuds"), 480 },
                                                                                       { QStringLiteral("partCount"), 1204 } } } } } } });
    http_.reply("/api/catalog/items?kind=venue&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("v1") }, { QStringLiteral("kind"), QStringLiteral("venue") },
                                             { QStringLiteral("title"), QStringLiteral("Town hall") }, { QStringLiteral("by"), QStringLiteral("Bob") },
                                             { QStringLiteral("uses"), 0 }, { QStringLiteral("version"), 1 },
                                             { QStringLiteral("previewUrl"), QStringLiteral("/api/catalog/items/v1/preview?v=1") },
                                             { QStringLiteral("summary"), QJsonObject{ { QStringLiteral("widthStuds"), 400 }, { QStringLiteral("heightStuds"), 300 } } } } } } });
    http_.reply("/api/catalog/items/l1/add", 201, { { QStringLiteral("kind"), QStringLiteral("layout") }, { QStringLiteral("id"), QStringLiteral("L9") } });
    http_.reply("/api/catalog/items/v1/add", 201, { { QStringLiteral("kind"), QStringLiteral("venue") }, { QStringLiteral("id"), QStringLiteral("V9") } });
    ui::CatalogTab tab(library_);
    tab.chooseOwner = [](const QString&, const QList<sync::OrgEntry>&, const QString&) -> std::optional<QString> {
        return QString();
    };
    auto kindButton = [&](ui::CatalogTab::Kind k) {
        return tab.findChild<QPushButton*>(QStringLiteral("catalogKind%1").arg(static_cast<int>(k)));
    };
    connectSignedIn();
    ASSERT_TRUE(waitFor([&] { return library_.catalog().modules; }));
    QCoreApplication::processEvents();
    EXPECT_FALSE(kindButton(ui::CatalogTab::Kind::Layouts)->isVisibleTo(&tab));
    EXPECT_FALSE(kindButton(ui::CatalogTab::Kind::Venues)->isVisibleTo(&tab));

    // The admin turns them on: the tabs show after the next refresh.
    http_.clear("/api/catalog/settings");
    http_.reply("/api/catalog/settings", 200,
                { { QStringLiteral("modules"), true }, { QStringLiteral("parts"), false }, { QStringLiteral("layouts"), true }, { QStringLiteral("venues"), true } });
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return library_.catalog().venues; }));
    // The tab shows its buttons when it hears of the new settings, which can
    // be a turn of the event loop after the library has them.
    EXPECT_TRUE(waitFor([&] { return kindButton(ui::CatalogTab::Kind::Layouts)->isVisibleTo(&tab); }));
    EXPECT_TRUE(waitFor([&] { return kindButton(ui::CatalogTab::Kind::Venues)->isVisibleTo(&tab); }));

    // A layout: its size and parts, its cover, and "Open a copy" copies it, then asks to open the copy.
    tab.setKind(ui::CatalogTab::Kind::Layouts);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("l1")); }));
    QStringList rowTexts;
    for (QLabel* l : tab.row(QStringLiteral("l1"))->findChildren<QLabel*>()) rowTexts << l->text();
    // In the machine's own number format (1,204 / 1.204 / 1204).
    const QLocale here;
    EXPECT_TRUE(rowTexts.join(QLatin1Char('|'))
                    .contains(QStringLiteral("960 × 480 studs (%1 × %2 m) · %3 parts")
                                  .arg(here.toString(7.68, 'f', 1), here.toString(3.84, 'f', 1), here.toString(1204))))
        << rowTexts.join(QLatin1Char('|')).toStdString();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "GET", "/api/catalog/items/l1/cover"); }));
    EXPECT_EQ(tab.row(QStringLiteral("l1"))->findChild<QPushButton*>(QStringLiteral("catalogInsert")), nullptr);
    QString opened;
    QObject::connect(&library_, &ui::ServerLibrary::catalogLayoutCopied, [&](const QString& id, const QString&) { opened = id; });
    QPushButton* copy = tab.row(QStringLiteral("l1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"));
    EXPECT_EQ(copy->text(), QStringLiteral("Open a copy"));
    copy->click();
    ASSERT_TRUE(waitFor([&] { return opened == QStringLiteral("L9"); }));
    EXPECT_TRUE(tab.row(QStringLiteral("l1"))->findChild<QPushButton*>(QStringLiteral("catalogWeb")));

    // A venue: "Use this venue" copies it, then asks to use the copy.
    tab.setKind(ui::CatalogTab::Kind::Venues);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("v1")); }));
    QString used;
    QObject::connect(&library_, &ui::ServerLibrary::catalogVenueCopied, [&](const QString& id, const QString&) { used = id; });
    QPushButton* use = tab.row(QStringLiteral("v1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"));
    EXPECT_EQ(use->text(), QStringLiteral("Use this venue"));
    use->click();
    ASSERT_TRUE(waitFor([&] { return used == QStringLiteral("V9"); }));
}

TEST(LibraryApi, ReadsALayoutsSizePartsAndCover) {
    const auto it = sync::LibraryApi::catalogItemFromJson(
        { { QStringLiteral("id"), QStringLiteral("l1") }, { QStringLiteral("kind"), QStringLiteral("layout") },
          { QStringLiteral("previewUrl"), QStringLiteral("/p") }, { QStringLiteral("coverUrl"), QStringLiteral("/c") },
          { QStringLiteral("summary"), QJsonObject{ { QStringLiteral("widthStuds"), 96 }, { QStringLiteral("heightStuds"), 8 }, { QStringLiteral("partCount"), 3 } } } });
    EXPECT_EQ(it.picturePath(), QStringLiteral("/c"));
    EXPECT_EQ(it.widthStuds, 96);
    EXPECT_EQ(it.heightStuds, 8);
    EXPECT_EQ(it.partCount, 3);
    // A module from an older server: its drawn picture, no size.
    const auto old = sync::LibraryApi::catalogItemFromJson({ { QStringLiteral("previewUrl"), QStringLiteral("/p") } });
    EXPECT_EQ(old.picturePath(), QStringLiteral("/p"));
    EXPECT_EQ(old.partCount, -1);
}

TEST_F(LibraryTabs, ACatalogThatIsOffSaysSo) {
    serveModules();
    http_.clear("/api/catalog/settings");
    http_.reply("/api/catalog/settings", 200, catalogOn(false));
    ui::CatalogTab tab(library_);
    connectSignedIn();
    EXPECT_TRUE(tab.note()->text().contains(QStringLiteral("catalog is turned off")));
}

// ---------- The main window --------------------------------------------------

namespace {

class Win : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::setTokenStore;
    using ui::MainWindow::offerToUpdateServerParts;
};

}  // namespace

class ServerModulesWindow : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().remove(QStringLiteral("sync/serverAddress"));
        {
            sync::ServerList list;
            list.add(http_.base(), QStringLiteral("Club server"));
            // A server without parts to download (the bundled library has these).
            sync::ServerInfo info;
            info.features = QStringList{ QStringLiteral("liveSync") };
            list.remember(http_.base(), info);
            list.touch(http_.base());
            list.save();
        }
        tokens_->save(http_.base(), QStringLiteral("bld_pat_win"));
        http_.reply("/api/orgs", 200, orgs());
        http_.reply("/api/catalog/settings", 200, catalogOn());
        http_.reply("/api/notices", 200, { { QStringLiteral("notices"), QJsonArray{} } });
        // The live hints: nothing, then the sign-in no longer works there.
        http_.reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
        beforeOpen();
        window_ = std::make_unique<Win>(bundledParts());
        window_->setTokenStore(tokens_);
        window_->ensureDocument();
        view_ = window_->findChild<ui::MapView*>();
        library_ = window_->serverLibrary();
        ASSERT_NE(library_, nullptr);
        ASSERT_TRUE(waitFor([&] { return library_->state() == ui::ServerLibrary::State::Ready; })) << library_->stateText().toStdString();
    }
    void TearDown() override {
        if (view_) view_->undoStack()->setClean();
        window_.reset();
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QStandardPaths::setTestModeEnabled(false);
    }
    virtual void beforeOpen() {
        http_.reply("/api/modules", 200,
                    modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")),
                              moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"), QStringLiteral("editor"), QStringLiteral("o1")) }));
        http_.replyRaw("/api/modules/m1/snapshot", 200, snapshotOf(*sampleModule()), "application/octet-stream");
    }
    int bricksInMap() { return sync::partCount(*view_->currentMap()); }

    FakeHttp http_;
    std::shared_ptr<sync::MemoryTokenStore> tokens_ = std::make_shared<sync::MemoryTokenStore>();
    std::unique_ptr<Win> window_;
    ui::MapView* view_ = nullptr;
    ui::ServerLibrary* library_ = nullptr;
};

TEST_F(ServerModulesWindow, TheLibraryShowsTheServersModulesAsItsOwnTab) {
    auto* panel = window_->findChild<ui::ModuleLibraryPanel*>();
    ASSERT_EQ(panel->tabs()->count(), 3);
    EXPECT_EQ(panel->tabs()->tabText(1), QStringLiteral("Club server"));
    EXPECT_EQ(library_->modules().size(), 2);
    EXPECT_EQ(lastRequest(http_, "GET", "/api/modules")->authorization, QByteArray("Bearer bld_pat_win"));
}

TEST_F(ServerModulesWindow, TheStatusBarSaysHowTheServerIsAndOpensTheServerWindow) {
    auto* status = window_->findChild<QToolButton*>(QStringLiteral("serverStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), QStringLiteral("Connected to Club server"));
    EXPECT_EQ(status->property("tourTarget").toString(), QStringLiteral("servers.status"));
    // Not signed in.
    library_->setServer(http_.base(), QStringLiteral("Club server"), QString());
    EXPECT_EQ(status->text(), QStringLiteral("Signed out of Club server"));
    // Can't be reached.
    QTcpServer gone;
    gone.listen(QHostAddress::LocalHost, 0);
    const QUrl unreachable(QStringLiteral("http://127.0.0.1:%1").arg(gone.serverPort()));
    gone.close();
    library_->setServer(unreachable, QStringLiteral("Away"), QStringLiteral("bld_pat_t"));
    ASSERT_TRUE(waitFor([&] { return library_->state() == ui::ServerLibrary::State::Offline; }));
    EXPECT_EQ(status->text(), QStringLiteral("Away is offline"));
    // None at all.
    library_->setServer({}, {}, {});
    EXPECT_EQ(status->text(), QStringLiteral("No server"));

    // A click opens the Server window, on the server it shows.
    library_->setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_win"));
    ASSERT_TRUE(waitFor([&] { return status->text() == QStringLiteral("Connected to Club server"); }));
    status->click();
    ASSERT_TRUE(waitFor([&] { return window_->serverWindow() && window_->serverWindow()->isVisible(); }));
    window_->serverWindow()->close();
}

TEST_F(ServerModulesWindow, UseThisVenueKeepsTheCopyAndStartsALayoutInIt) {
    auto* venues = window_->findChild<ui::VenueLibraryPanel*>();
    ASSERT_NE(venues, nullptr);
    const QDir dir(venues->libraryPath());
    const QStringList before = dir.entryList({ QStringLiteral("*.bld-venue") }, QDir::Files);
    const QJsonObject venue{ { QStringLiteral("name"), QStringLiteral("Town hall") },
                             { QStringLiteral("enabled"), true },
                             { QStringLiteral("minWalkwayStuds"), 30 },
                             { QStringLiteral("bounds"), QJsonObject{ { QStringLiteral("x"), 0 }, { QStringLiteral("y"), 0 }, { QStringLiteral("w"), 400 }, { QStringLiteral("h"), 300 } } },
                             { QStringLiteral("edges"), QJsonArray{ QJsonObject{ { QStringLiteral("kind"), 0 }, { QStringLiteral("doorWidthStuds"), 0 }, { QStringLiteral("label"), QString() },
                                                                                  { QStringLiteral("poly"), QJsonArray{ QJsonObject{ { QStringLiteral("x"), 0 }, { QStringLiteral("y"), 0 } },
                                                                                                                        QJsonObject{ { QStringLiteral("x"), 400 }, { QStringLiteral("y"), 0 } } } } } } },
                             { QStringLiteral("obstacles"), QJsonArray{} } };
    http_.reply("/api/venues/V9", 200, { { QStringLiteral("id"), QStringLiteral("V9") }, { QStringLiteral("name"), QStringLiteral("Town hall") }, { QStringLiteral("data"), venue } });
    emit library_->catalogVenueCopied(QStringLiteral("V9"), QStringLiteral("Town hall"));
    ASSERT_TRUE(waitFor([&] { return view_->currentMap()->sidecar.venue.has_value(); }));
    EXPECT_EQ(view_->currentMap()->sidecar.venue->name, QStringLiteral("Town hall"));
    EXPECT_EQ(view_->currentMap()->sidecar.venue->edges.size(), 1);
    // The copy is kept in the Venue library too.
    QStringList added = dir.entryList({ QStringLiteral("*.bld-venue") }, QDir::Files);
    for (const QString& f : before) added.removeAll(f);
    ASSERT_EQ(added.size(), 1);
    EXPECT_TRUE(added.front().startsWith(QStringLiteral("Town hall")));
    QFile::remove(dir.filePath(added.front()));
}

// "Where should these go?" (the layout lacks the module's sheets): new
// sheets by their names, whenever it opens while the timer lives.
static std::unique_ptr<QTimer> answerNewSheets() {
    auto t = std::make_unique<QTimer>();
    QObject::connect(t.get(), &QTimer::timeout, [] {
        auto* d = qobject_cast<ui::SheetChoiceDialog*>(QApplication::activeModalWidget());
        if (!d) return;
        for (auto* box : d->findChildren<QComboBox*>(QStringLiteral("sheetChoice"))) box->setCurrentIndex(box->count() - 1);
        d->placeButton()->click();
    });
    t->start(20);
    return t;
}

TEST_F(ServerModulesWindow, AddToLayoutPutsTheModulesPartsInAsOneModule) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const core::Map& m = *view_->currentMap();
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules.front().name, QStringLiteral("Freight yard"));
    EXPECT_EQ(m.sidecar.modules.front().memberIds.size(), 3);
    EXPECT_TRUE(m.sidecar.modules.front().sourceFile.endsWith(QStringLiteral("/modules/m1")));
    // Its sheets came along, and the parts are selected to move or turn.
    QStringList names;
    for (const auto& l : m.layers()) names << l->name;
    EXPECT_TRUE(names.contains(QStringLiteral("Tracks")));
    int selected = 0;
    for (QGraphicsItem* it : view_->scene()->selectedItems())
        if (it->data(2).toString() == QStringLiteral("brick")) ++selected;
    EXPECT_EQ(selected, 3);
    EXPECT_TRUE(library_->inserting().isEmpty());
    // Undo takes it all back.
    view_->undoStack()->undo();
    EXPECT_EQ(bricksInMap(), 0);
}

// Two parts on the map, picked, in no module yet.
static void placeLoose(ui::MapView* view, int tracks) {
    auto module = sampleModule(tracks);
    const auto answers = answerNewSheets();
    view->placeModule(*module, QStringLiteral("x"), QString(), QPointF(4000, 4000));
    core::Map& m = *view->currentMap();
    view->undoStack()->push(new edit::DeleteModuleCommand(m, m.sidecar.modules.front().id));
    for (QGraphicsItem* it : view->scene()->items())
        if (it->data(2).toString() == QStringLiteral("brick")) it->setSelected(true);
}

// Make a module: the picked parts become a module in this layout, and
// nothing goes to the server.
TEST_F(ServerModulesWindow, MakeAModuleKeepsItInThisLayoutOnly) {
    placeLoose(view_, 1);
    ASSERT_EQ(bricksInMap(), 2);
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::MakeModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        EXPECT_FALSE(d->alsoSaveBox()->isChecked());
        d->nameEdit()->setText(QStringLiteral("Yard"));
        d->accept();
    });
    QMetaObject::invokeMethod(window_.get(), "onMakeModule", Qt::DirectConnection);
    const core::Map& m = *view_->currentMap();
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules.front().name, QStringLiteral("Yard"));
    EXPECT_EQ(m.sidecar.modules.front().memberIds.size(), 2);
    EXPECT_TRUE(m.sidecar.modules.front().libraryModuleId.isEmpty());
    EXPECT_EQ(count(http_, "POST", "/api/modules"), 0);
    EXPECT_EQ(count(http_, "PUT", "/api/modules"), 0);
}

// "Also save to my Module library": made, saved as a new module (centred, with its
// picture), and linked to it.
TEST_F(ServerModulesWindow, MakeAndSaveMakesANewModuleOnTheServerAndLinksIt) {
    // The server's empty layout for a new module.
    core::Map empty;
    http_.replyRaw("/api/modules/new1/snapshot", 200, snapshotOf(empty), "application/octet-stream");
    http_.reply("/api/modules/new1/snapshot", 200, { { QStringLiteral("version"), 1 } });
    http_.reply("/api/modules/new1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    // POST and GET share the path: the create answers first.
    http_.clear("/api/modules");
    http_.reply("/api/modules", 201, { { QStringLiteral("id"), QStringLiteral("new1") }, { QStringLiteral("title"), QStringLiteral("Yard") } });
    http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("new1"), QStringLiteral("Yard"), QStringLiteral("owner"), {}, 5) }));
    placeLoose(view_, 1);
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::MakeModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->nameEdit()->setText(QStringLiteral("Yard"));
        d->alsoSaveBox()->setChecked(true);
        EXPECT_EQ(d->makeButton()->text(), QStringLiteral("Make and save"));
        EXPECT_EQ(d->saveTo()->itemText(1), QStringLiteral("Train Club"));
        d->saveTo()->setCurrentIndex(0);
        d->accept();
    });
    QMetaObject::invokeMethod(window_.get(), "onMakeModule", Qt::DirectConnection);
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PUT", "/api/modules/new1/thumbnail"); }));
    const auto* created = lastRequest(http_, "POST", "/api/modules");
    ASSERT_NE(created, nullptr);
    EXPECT_TRUE(created->body.contains("\"title\":\"Yard\""));
    EXPECT_FALSE(created->body.contains("orgSlug"));
    // The saved contents are the module, centred on the origin as the web saves them.
    const auto* put = lastRequest(http_, "PUT", "/api/modules/new1/snapshot");
    ASSERT_NE(put, nullptr);
    auto saved = sync::mapFromModuleSnapshot(put->body);
    ASSERT_TRUE(saved);
    EXPECT_EQ(sync::partCount(*saved), 2);
    QRectF box;
    for (const auto& l : saved->layers())
        for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) box = box.isNull() ? b.displayArea : box.united(b.displayArea);
    EXPECT_NEAR(box.center().x(), 0, 0.01);
    EXPECT_NEAR(box.center().y(), 0, 0.01);
    // With its picture, as PNG.
    const QJsonObject thumb = QJsonDocument::fromJson(lastRequest(http_, "PUT", "/api/modules/new1/thumbnail")->body).object();
    EXPECT_EQ(thumb.value(QLatin1String("mime")).toString(), QStringLiteral("image/png"));
    QImage img;
    ASSERT_TRUE(img.loadFromData(QByteArray::fromBase64(thumb.value(QLatin1String("data")).toString().toLatin1())));
    EXPECT_GT(std::max(img.width(), img.height()), 256);
    // Linked.
    ASSERT_TRUE(waitFor([&] { return !view_->currentMap()->sidecar.modules.front().libraryModuleId.isEmpty(); }));
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryModuleId, QStringLiteral("new1"));
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);
}

// Save to Module library… from a module's menu: parts on two sheets, the dialog
// says so, and "Put everything on one sheet" saves one sheet named after
// the one with the most parts. The module is linked to what was saved.
TEST_F(ServerModulesWindow, SaveToLibrarySaysWhichSheetsAndLinksTheModule) {
    core::Map empty;
    http_.replyRaw("/api/modules/new2/snapshot", 200, snapshotOf(empty), "application/octet-stream");
    http_.reply("/api/modules/new2/snapshot?note=Two", 200, { { QStringLiteral("version"), 1 } });
    http_.reply("/api/modules/new2/thumbnail", 200, { { QStringLiteral("ok"), true } });
    http_.clear("/api/modules");
    http_.reply("/api/modules", 201, { { QStringLiteral("id"), QStringLiteral("new2") }, { QStringLiteral("title"), QStringLiteral("Depot") } });
    http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("new2"), QStringLiteral("Depot"), QStringLiteral("owner"), {}, 5) }));
    auto module = sampleModule(2);  // Baseplates: 1 part, Tracks: 2
    const auto answers = answerNewSheets();
    view_->placeModule(*module, QStringLiteral("Depot here"), QString(), QPointF(4000, 4000));
    ASSERT_EQ(bricksInMap(), 3);
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    // The module's menu offers Save to Module library….
    auto* panel = window_->findChild<ui::ModulesPanel*>();
    {
        QMenu menu;
        panel->fillMenu(menu, id);
        auto* save = menu.findChild<QAction*>(QStringLiteral("moduleLibrary_save"));
        ASSERT_NE(save, nullptr);
        EXPECT_EQ(menu.findChild<QAction*>(QStringLiteral("moduleLibrary_publish")), nullptr);
    }
    QString shown, oneShown, title;
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::SaveModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        title = d->windowTitle();
        EXPECT_EQ(d->nameEdit()->text(), QStringLiteral("Depot here"));
        EXPECT_TRUE(d->sheetsBox()->isVisibleTo(d));
        shown = d->sheetsLabel()->text();
        d->oneSheetBox()->setChecked(true);
        oneShown = d->sheetsLabel()->text();
        d->nameEdit()->setText(QStringLiteral("Depot"));
        d->noteEdit()->setText(QStringLiteral("Two"));
        d->accept();
    });
    emit panel->libraryActionRequested(id, QStringLiteral("save"));
    EXPECT_EQ(title, QStringLiteral("Save “Depot here” to the Module library"));
    EXPECT_EQ(shown, QStringLiteral("This module uses 2 sheets: <b>Baseplates, Tracks</b>."));
    EXPECT_EQ(oneShown, QStringLiteral("All its parts go on one sheet: <b>Tracks</b>."));
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PUT", "/api/modules/new2/snapshot?note=Two"); }));
    auto saved = sync::mapFromModuleSnapshot(lastRequest(http_, "PUT", "/api/modules/new2/snapshot?note=Two")->body);
    ASSERT_TRUE(saved);
    ASSERT_EQ(saved->layers().size(), 1u);
    EXPECT_EQ(saved->layers().front()->name, QStringLiteral("Tracks"));
    EXPECT_EQ(saved->layers().front()->transparency, 100);
    EXPECT_EQ(sync::partCount(*saved), 3);
    ASSERT_TRUE(waitFor([&] { return view_->currentMap()->sidecar.modules.front().libraryModuleId == QStringLiteral("new2"); }));
    // The layout's sheets stay as they were.
    EXPECT_EQ(sync::partCount(*view_->currentMap()), 3);
    // Linked: the menu now offers Update Module library version.
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("new2")); }));
    QMenu menu;
    panel->fillMenu(menu, id);
    EXPECT_NE(menu.findChild<QAction*>(QStringLiteral("moduleLibrary_publish")), nullptr);
}

// The library module m1 at version `latest`, whose version 1 is `v1` and
// whose newest contents are `now`.
static void serveVersions(FakeHttp& http, int latest, const core::Map& v1, const core::Map& now) {
    http.clear("/api/modules");
    http.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner"), {}, 0, latest) }));
    http.clear("/api/modules/m1/snapshot");
    http.replyRaw("/api/modules/m1/snapshot", 200, snapshotOf(now), "application/octet-stream");
    http.replyRaw("/api/modules/m1/versions/1/snapshot", 200, snapshotOf(v1), "application/octet-stream");
}

// Added from the Module library, the module is linked; Update Module library version
// saves the next version with its note; Update from Module library brings a newer
// version in where the module sits, without asking when it wasn't changed.
TEST_F(ServerModulesWindow, LinkedModulesUpdateTheLibraryAndFromIt) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryModuleId, QStringLiteral("m1"));
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);

    // Update Module library version: version 2, with its note.
    http_.reply("/api/modules/m1/snapshot?note=More%20track", 200, { { QStringLiteral("version"), 2 } });
    http_.reply("/api/modules/m1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    QString text;
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::PublishModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        text = d->text()->text();
        EXPECT_FALSE(d->newerWarning()->isVisibleTo(d));
        d->noteEdit()->setText(QStringLiteral("More track"));
        d->accept();
    });
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("publish"));
    EXPECT_TRUE(text.contains(QStringLiteral("version 2 of <b>Freight yard</b>"))) << text.toStdString();
    ASSERT_TRUE(waitFor([&] { return view_->currentMap()->sidecar.modules.front().libraryVersion == 2; }));
    ASSERT_NE(lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=More%20track"), nullptr);

    // The library moves on to version 3, with a fourth part; this copy is version 1's.
    {
        core::Map& m = *view_->currentMap();
        core::Module linked = m.sidecar.modules.front();
        linked.libraryVersion = 1;
        view_->undoStack()->push(new edit::UpdateModuleCommand(m, linked, QStringLiteral("t")));
    }
    serveVersions(http_, 3, *sampleModule(2), *sampleModule(3));
    library_->refresh();
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("m1")) && library_->module(QStringLiteral("m1"))->latestVersion == 3; }));
    const auto before = edit::placedParts(*view_->currentMap(), view_->currentMap()->sidecar.modules.front().memberIds);
    int asked = 0;
    window_->confirmModuleLibrary_ = [&](const ui::ConfirmOptions&) { ++asked; return true; };
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("pull"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 4; }));
    EXPECT_EQ(asked, 0);
    const core::Module& mod = view_->currentMap()->sidecar.modules.front();
    EXPECT_EQ(mod.id, id);
    EXPECT_EQ(mod.libraryVersion, 3);
    EXPECT_EQ(mod.memberIds.size(), 4);
    // The parts it had stay where they were.
    const auto after = edit::placedParts(*view_->currentMap(), mod.memberIds);
    for (const auto& b : before) {
        bool same = false;
        for (const auto& a : after)
            same = same || (a.partNumber == b.partNumber && std::abs(a.displayArea.x() - b.displayArea.x()) < 0.01 &&
                            std::abs(a.displayArea.y() - b.displayArea.y()) < 0.01);
        QString all;
        for (const auto& a : after) all += QStringLiteral(" %1@%2,%3").arg(a.partNumber).arg(a.displayArea.x()).arg(a.displayArea.y());
        EXPECT_TRUE(same) << b.partNumber.toStdString() << " at " << b.displayArea.x() << "," << b.displayArea.y() << " in" << all.toStdString();
    }
    // Undo puts the old parts back.
    view_->undoStack()->undo();
    EXPECT_EQ(bricksInMap(), 3);
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);
}

// Update Module library version names the version the dialog showed (If-Match),
// as a module being changed here does. Someone else's save since then is
// refused and asked about: Save mine saves over it as a new version.
TEST_F(ServerModulesWindow, UpdateLibraryVersionNamesTheVersionItSawAndSaveMineSavesOverIt) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    // The first save: refused, someone saved meanwhile. The second (Save mine): taken.
    http_.reply("/api/modules/m1/snapshot?note=Mine", 409,
                { { QStringLiteral("error"), QStringLiteral("module_changed") }, { QStringLiteral("docVersion"), 2 } });
    http_.reply("/api/modules/m1/snapshot?note=Mine", 200,
                { { QStringLiteral("version"), 3 }, { QStringLiteral("docVersion"), 3 } });
    http_.reply("/api/modules/m1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    QStringList asked;
    window_->askModuleChanged = [&](const QString& title) {
        asked << title;
        return 2; // Save mine as a new version
    };
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<ui::PublishModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->noteEdit()->setText(QStringLiteral("Mine"));
        d->accept();
    });
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("publish"));
    ASSERT_TRUE(waitFor([&] { return asked.size() == 1; }));
    EXPECT_EQ(asked.front(), QStringLiteral("Freight yard"));
    std::vector<const synctest::Request*> puts;
    ASSERT_TRUE(waitFor([&] {
        puts.clear();
        for (const auto& r : http_.requests)
            if (r.method == "PUT" && r.path == "/api/modules/m1/snapshot?note=Mine") puts.push_back(&r);
        return puts.size() == 2;
    }));
    // The first named the version the dialog showed (the list's docVersion 1); Save mine names none.
    EXPECT_EQ(puts[0]->ifMatch, QByteArray("\"1\""));
    EXPECT_TRUE(puts[1]->ifMatch.isEmpty());
    ASSERT_TRUE(waitFor([&] { return view_->currentMap()->sidecar.modules.front().libraryVersion == 3; }));
}

// Load theirs after a refused Update Module library version: the library's
// newest version replaces this module's parts, without asking again, and
// nothing is written over.
TEST_F(ServerModulesWindow, UpdateLibraryVersionLoadTheirsBringsTheirVersionIn) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    // Someone saved version 3 (a fourth part) meanwhile; the list still says 1 until it's refreshed.
    http_.reply("/api/modules/m1/snapshot?note=Mine", 409,
                { { QStringLiteral("error"), QStringLiteral("module_changed") }, { QStringLiteral("docVersion"), 3 } });
    http_.clear("/api/modules/m1/snapshot");
    http_.replyRaw("/api/modules/m1/snapshot", 200, snapshotOf(*sampleModule(3)), "application/octet-stream");
    http_.clear("/api/modules");
    http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner"), {}, 0, 3) }));
    window_->askModuleChanged = [&](const QString&) { return 1; }; // Load theirs
    int asked = 0;
    window_->confirmModuleLibrary_ = [&](const ui::ConfirmOptions&) { ++asked; return true; };
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<ui::PublishModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->noteEdit()->setText(QStringLiteral("Mine"));
        d->accept();
    });
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("publish"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 4; }));
    const core::Module& mod = view_->currentMap()->sidecar.modules.front();
    EXPECT_EQ(mod.id, id);
    EXPECT_EQ(mod.libraryVersion, 3);
    EXPECT_EQ(asked, 0);
    int puts = 0;
    for (const auto& r : http_.requests) puts += r.method == "PUT";
    EXPECT_EQ(puts, 1);
    ASSERT_NE(lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=Mine"), nullptr);
    EXPECT_EQ(lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=Mine")->ifMatch, QByteArray("\"1\""));
}

// BLD_CONFLICT_SHOTS=<dir>: pictures of the "Someone saved this module" question
// after a refused Update Module library version, in the light and dark themes,
// to look at (not compared).
TEST_F(ServerModulesWindow, UpdateLibraryVersionConflictShots) {
    const QString out = qEnvironmentVariable("BLD_CONFLICT_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_CONFLICT_SHOTS not set";
    QDir().mkpath(out);
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    http_.reply("/api/modules/m1/snapshot?note=Mine", 409,
                { { QStringLiteral("error"), QStringLiteral("module_changed") }, { QStringLiteral("docVersion"), 2 } });
    const auto pause = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    };
    ui::theme::loadFonts();
    const ui::theme::Accent accent = ui::theme::accent(QString::fromLatin1(ui::theme::kDefaultAccent));
    for (const auto mode : { ui::theme::Mode::Light, ui::theme::Mode::Dark }) {
        QApplication::setPalette(ui::theme::buildPalette(mode, accent));
        qApp->setStyleSheet(ui::theme::buildStyleSheet(mode, accent));
        pause(300);
        const QString name = mode == ui::theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        bool shot = false;
        QTimer::singleShot(0, [] {
            auto* d = qobject_cast<ui::PublishModuleDialog*>(QApplication::activeModalWidget());
            ASSERT_NE(d, nullptr);
            d->noteEdit()->setText(QStringLiteral("Mine"));
            d->accept();
        });
        auto poll = std::make_unique<QTimer>();
        QObject::connect(poll.get(), &QTimer::timeout, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box || shot) return;
            box->adjustSize();
            pause(200);
            box->grab().save(out + QStringLiteral("/conflict-%1-%2.png").arg(QStringLiteral("after"), name));
            shot = true;
            box->reject();
        });
        poll->start(50);
        emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("publish"));
        ASSERT_TRUE(waitFor([&] { return shot; }));
        poll->stop();
    }
    qApp->setStyleSheet(QString());
}

// Changed in this layout: Update from Module library asks first, and No keeps it.
TEST_F(ServerModulesWindow, UpdateFromLibraryAsksWhenTheModuleWasChangedHere) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    serveVersions(http_, 3, *sampleModule(2), *sampleModule(3));
    library_->refresh();
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("m1")) && library_->module(QStringLiteral("m1"))->latestVersion == 3; }));
    // A part moves.
    {
        core::Map& m = *view_->currentMap();
        for (auto& l : m.layers())
            if (l->kind() == core::LayerKind::Brick && !static_cast<core::LayerBrick&>(*l).bricks.empty()) {
                static_cast<core::LayerBrick&>(*l).bricks.front().displayArea.translate(8, 0);
                break;
            }
    }
    QStringList asked;
    window_->confirmModuleLibrary_ = [&](const ui::ConfirmOptions& o) { asked << o.title; return false; };
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("pull"));
    ASSERT_TRUE(waitFor([&] { return !asked.isEmpty(); }));
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Replace “Freight yard” with version 3?") });
    EXPECT_EQ(bricksInMap(), 3);
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);
}

// A new module saved straight into a club asks first: the club will own it.
TEST_F(ServerModulesWindow, SavingANewModuleIntoAClubAsksFirst) {
    ui::SaveModuleDialog d(*library_, QString());
    QStringList asked;
    bool answer = false;
    d.confirmSaveToClub = [&](const QString& club) {
        asked << club;
        return answer;
    };
    d.nameEdit()->setText(QStringLiteral("Yard"));
    // Yours: nothing asked.
    d.accept();
    EXPECT_EQ(d.result(), QDialog::Accepted);
    EXPECT_TRUE(asked.isEmpty());
    d.setResult(QDialog::Rejected);
    const int club = d.saveTo()->findData(QStringLiteral("train"));
    ASSERT_GE(club, 0);
    d.saveTo()->setCurrentIndex(club);
    d.accept();
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Train Club") });
    EXPECT_NE(d.result(), QDialog::Accepted);
    answer = true;
    d.accept();
    EXPECT_EQ(d.result(), QDialog::Accepted);
    EXPECT_EQ(d.choice().orgSlug, QStringLiteral("train"));
}

TEST_F(ServerModulesWindow, OpenToChangeItThenSaveMakesANewVersion) {
    http_.reply("/api/modules/m1/snapshot?note=More%20track", 200, { { QStringLiteral("version"), 2 } });
    http_.reply("/api/modules/m1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    emit library_->openRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return window_->editingModule().id == QStringLiteral("m1"); }));
    EXPECT_EQ(library_->editingModule(), QStringLiteral("m1"));
    EXPECT_EQ(bricksInMap(), 3);
    // The server by the name it has in Your servers, as a live layout does.
    EXPECT_TRUE(window_->windowTitle().contains(QStringLiteral("Freight yard — Module on Club server")));
    EXPECT_TRUE(window_->notices()->isShown(QStringLiteral("editing-module")));
    // Take one part out, then Save: a new version with its note.
    {
        core::Map& m = *view_->currentMap();
        for (int i = 0; i < static_cast<int>(m.layers().size()); ++i)
            if (m.layers()[i]->name == QStringLiteral("Baseplates")) {
                const auto& b = static_cast<core::LayerBrick&>(*m.layers()[i]).bricks;
                view_->undoStack()->push(new edit::DeleteBricksCommand(m, { { i, 0, b.front() } }));
                break;
            }
    }
    ASSERT_EQ(bricksInMap(), 2);
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->setTextValue(QStringLiteral("More track"));
        d->accept();
    });
    QAction* save = nullptr;
    for (QAction* a : window_->findChildren<QAction*>())
        if (a->shortcut() == QKeySequence::Save) save = a;
    ASSERT_NE(save, nullptr);
    save->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=More%20track"); }));
    auto saved = sync::mapFromModuleSnapshot(lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=More%20track")->body);
    ASSERT_TRUE(saved);
    EXPECT_EQ(sync::partCount(*saved), 2);
    ASSERT_TRUE(waitFor([&] { return view_->undoStack()->isClean(); }));
    // Another document: not changing the module any more.
    QMetaObject::invokeMethod(window_.get(), "onNew", Qt::DirectConnection);
    EXPECT_TRUE(window_->editingModule().id.isEmpty());
    EXPECT_TRUE(library_->editingModule().isEmpty());
}

// Someone else saved the module while it was changed here: the save names
// the version it started from, the refusal asks, and nothing is overwritten
// unless "Save mine as a new version" is chosen.
class ServerModulesChanged : public ServerModulesWindow {
protected:
    void beforeOpen() override {
        ServerModulesWindow::beforeOpen();
        http_.clear("/api/modules/m1/snapshot");
        http_.replyRaw("/api/modules/m1/snapshot", 200, snapshotOf(*sampleModule()),
                       "application/octet-stream", "\r\nX-Doc-Version: 4");
    }
};

TEST_F(ServerModulesChanged, ASaveFromAnOlderVersionAsksAndSaveMineSavesOverIt) {
    emit library_->openRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return window_->editingModule().id == QStringLiteral("m1"); }));
    EXPECT_EQ(window_->editingModule().docVersion, 4);
    // The first save: refused, someone saved version 5 meanwhile. The second (Save mine): taken.
    http_.reply("/api/modules/m1/snapshot?note=Mine", 409,
                { { QStringLiteral("error"), QStringLiteral("module_changed") },
                  { QStringLiteral("docVersion"), 5 } });
    http_.reply("/api/modules/m1/snapshot?note=Mine", 200,
                { { QStringLiteral("version"), 6 }, { QStringLiteral("docVersion"), 6 } });
    http_.reply("/api/modules/m1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    QStringList asked;
    window_->askModuleChanged = [&](const QString& title) {
        asked << title;
        return 2; // Save mine as a new version
    };
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->setTextValue(QStringLiteral("Mine"));
        d->accept();
    });
    QAction* save = nullptr;
    for (QAction* a : window_->findChildren<QAction*>())
        if (a->shortcut() == QKeySequence::Save) save = a;
    ASSERT_NE(save, nullptr);
    save->trigger();
    ASSERT_TRUE(waitFor([&] { return asked.size() == 1; }));
    EXPECT_EQ(asked.front(), QStringLiteral("Freight yard"));
    std::vector<const synctest::Request*> puts;
    ASSERT_TRUE(waitFor([&] {
        puts.clear();
        for (const auto& r : http_.requests)
            if (r.method == "PUT" && r.path == "/api/modules/m1/snapshot?note=Mine") puts.push_back(&r);
        return puts.size() == 2;
    }));
    // The first named the version it started from; Save mine names none.
    EXPECT_EQ(puts[0]->ifMatch, QByteArray("\"4\""));
    EXPECT_TRUE(puts[1]->ifMatch.isEmpty());
    ASSERT_TRUE(waitFor([&] { return window_->editingModule().docVersion == 6; }));
}

TEST_F(ServerModulesChanged, LoadTheirsOpensTheirVersion) {
    emit library_->openRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return window_->editingModule().id == QStringLiteral("m1"); }));
    http_.reply("/api/modules/m1/snapshot?note=Mine", 409,
                { { QStringLiteral("error"), QStringLiteral("module_changed") },
                  { QStringLiteral("docVersion"), 5 } });
    window_->askModuleChanged = [&](const QString&) { return 1; }; // Load theirs
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->setTextValue(QStringLiteral("Mine"));
        d->accept();
    });
    const auto gets = [&] {
        int n = 0;
        for (const auto& r : http_.requests) n += r.method == "GET" && r.path == "/api/modules/m1/snapshot";
        return n;
    };
    const int before = gets();
    for (QAction* a : window_->findChildren<QAction*>())
        if (a->shortcut() == QKeySequence::Save) a->trigger();
    // Their version is read again (the save itself reads it once first).
    ASSERT_TRUE(waitFor([&] { return gets() >= before + 2; }));
    ASSERT_TRUE(waitFor([&] {
        return view_->undoStack()->isClean() && window_->editingModule().id == QStringLiteral("m1");
    }));
    int puts = 0;
    for (const auto& r : http_.requests) puts += r.method == "PUT";
    EXPECT_EQ(puts, 1);
}

class ServerModulesWarning : public ServerModulesWindow {
protected:
    void beforeOpen() override {
        ServerModulesWindow::beforeOpen();
        http_.clear("/api/notices");
        auto warning = [](bool acked) {
            return QJsonObject{ { QStringLiteral("notices"),
                                  QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("w1") }, { QStringLiteral("scope"), QStringLiteral("site") },
                                                           { QStringLiteral("severity"), QStringLiteral("warning") },
                                                           { QStringLiteral("reason"), QStringLiteral("Please keep titles friendly.") },
                                                           { QStringLiteral("link"), QStringLiteral("/catalog") },
                                                           { QStringLiteral("acknowledgedAt"), acked ? QJsonValue(1.0) : QJsonValue() } } } } };
        };
        http_.reply("/api/notices", 200, warning(false));
        http_.reply("/api/notices", 200, warning(true));
        http_.reply("/api/notices/w1/acknowledge", 200, { { QStringLiteral("ok"), true } });
    }
};

TEST_F(ServerModulesWarning, AWarningStaysUntilIUnderstand) {
    ui::NoticeArea* notices = window_->notices();
    ASSERT_TRUE(waitFor([&] { return notices->isShown(QStringLiteral("server-warning")); }));
    QFrame* card = notices->card(QStringLiteral("server-warning"));
    QStringList texts;
    for (QLabel* l : card->findChildren<QLabel*>()) texts << l->text();
    EXPECT_TRUE(texts.contains(QStringLiteral("Important: Warning · From the site team"))) << texts.join(QLatin1Char('|')).toStdString();
    EXPECT_TRUE(texts.contains(QStringLiteral("Please keep titles friendly.")));
    QPushButton* understand = nullptr;
    QStringList buttons;
    for (QPushButton* b : card->findChildren<QPushButton*>()) {
        buttons << b->text();
        if (b->text() == QStringLiteral("I understand")) understand = b;
    }
    EXPECT_TRUE(buttons.contains(QStringLiteral("See what it's about")));
    ASSERT_NE(understand, nullptr);
    understand->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notices/w1/acknowledge"); }));
    ASSERT_TRUE(waitFor([&] { return !notices->isShown(QStringLiteral("server-warning")); }));
}

class ServerModulesLive : public ServerModulesWindow {
protected:
    void beforeOpen() override {
        // Someone saves a module on the web: the hint, then the new list.
        http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")) }));
        http_.reply("/api/modules", 200,
                    modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")),
                              moduleJson(QStringLiteral("m3"), QStringLiteral("Saved on the web"), QStringLiteral("owner")) }));
        http_.clear("/api/events");
        http_.replyRaw("/api/events", 200, "retry: 5000\n\n: connected\n\ndata: {\"kind\":\"module\",\"action\":\"create\"}\n\n",
                       "text/event-stream");
        http_.reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
    }
};

TEST_F(ServerModulesLive, AHintFromTheServerListsTheModulesAgain) {
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("m3")); }));
    EXPECT_GE(count(http_, "GET", "/api/events"), 1);
    EXPECT_EQ(lastRequest(http_, "GET", "/api/events")->authorization, QByteArray("Bearer bld_pat_win"));
}

// A part you re-imported or replaced, and the server has your copy of it:
// one click updates the server's copy too (and only one you may change).
TEST_F(ServerModulesWindow, AChangedImportedPartIsUpdatedOnTheServerToo) {
    QTemporaryDir home;
    QSettings().setValue(QStringLiteral("modules/libraryPath"), home.path());
    for (const char* key : { "Station", "Shed" }) {
        ui::PreparedPart part;
        part.source = home.filePath(QStringLiteral("station.ldr"));
        part.sprite = QImage(32, 16, QImage::Format_ARGB32);
        part.sprite.fill(Qt::red);
        part.widthStuds = 4;
        part.heightStuds = 2;
        QString err;
        ASSERT_FALSE(ui::writeImportedPart(part, QLatin1String(key), home.filePath(QStringLiteral("imports")), {}, false, &err)
                         .isEmpty()) << err.toStdString();
    }
    const auto serverPart = [](const char* id, const char* number, const char* role) {
        return QJsonObject{ { QStringLiteral("id"), QLatin1String(id) }, { QStringLiteral("partNumber"), QLatin1String(number) },
                            { QStringLiteral("displayName"), QLatin1String(number) }, { QStringLiteral("role"), QLatin1String(role) } };
    };
    http_.reply("/api/custom-parts", 200,
                { { QStringLiteral("parts"), QJsonArray{ serverPart("p1", "station", "owner"), serverPart("p2", "Shed", "viewer") } } });
    http_.reply("/api/custom-parts/p1", 200, { { QStringLiteral("id"), QStringLiteral("p1") } });

    window_->offerToUpdateServerParts({ QStringLiteral("Station"), QStringLiteral("Shed") });
    const QString id = QStringLiteral("updated-part:") + http_.base().toString();
    auto* notices = window_->notices();
    ASSERT_TRUE(waitFor([&] { return notices->isShown(id); }));
    QFrame* card = notices->card(id);
    bool saysWhich = false;
    for (auto* l : card->findChildren<QLabel*>()) saysWhich |= l->text().contains(QStringLiteral("“station” is on Club server"));
    EXPECT_TRUE(saysWhich);
    QPushButton* update = nullptr;
    for (auto* b : card->findChildren<QPushButton*>())
        if (b->text() == QStringLiteral("Update on the server")) update = b;
    ASSERT_NE(update, nullptr);
    update->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PUT", "/api/custom-parts/p1"); }));
    const QJsonObject body = QJsonDocument::fromJson(lastRequest(http_, "PUT", "/api/custom-parts/p1")->body).object();
    EXPECT_EQ(body.value(QStringLiteral("partNumber")).toString(), QStringLiteral("station"));  // as the server has it
    EXPECT_FALSE(body.value(QStringLiteral("xmlBase64")).toString().isEmpty());
    EXPECT_FALSE(body.value(QStringLiteral("spriteBase64")).toString().isEmpty());
    // Shed is only shared with you to look at: not sent.
    waitFor([] { return false; }, 100);
    EXPECT_FALSE(lastRequest(http_, "PUT", "/api/custom-parts/p2"));
    QSettings().remove(QStringLiteral("modules/libraryPath"));
}

// The main window's own questions to the server (add a module, use a
// venue, the notices) go through the ServerLibrary's api. An answer that
// lands after the window is gone must not reach it: the library must not
// count on going first. Here it outlives the window, as it would if it were
// ever owned elsewhere; without the guard each answer is a
// heap-use-after-free under ASan.
class ServerModulesSlowNotices : public ServerModulesWindow {
protected:
    void beforeOpen() override {
        ServerModulesWindow::beforeOpen();
        // The window asks for your notices once the library is Ready (and
        // only then: refresh() while Ready doesn't ask again). Held from the
        // start, that first question is still unanswered when the window goes.
        http_.hold.insert("/api/notices");
    }
};

TEST_F(ServerModulesSlowNotices, AnAnswerAfterTheWindowIsGoneIsDropped) {
    const QByteArray snapshot = "/api/modules/m1/snapshot";
    const QByteArray venue = "/api/venues/V9";
    const QByteArray notices = "/api/notices";
    http_.reply(venue, 200, { { QStringLiteral("id"), QStringLiteral("V9") }, { QStringLiteral("name"), QStringLiteral("Hall") },
                              { QStringLiteral("data"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("Hall") } } } });
    for (const QByteArray& p : { snapshot, venue }) http_.hold.insert(p);
    const auto asked = [&](const QByteArray& path) {
        return std::count_if(http_.requests.cbegin(), http_.requests.cend(), [&](const auto& r) { return r.path == path; });
    };
    emit library_->insertRequested(QStringLiteral("m1"));
    emit library_->catalogVenueCopied(QStringLiteral("V9"), QStringLiteral("Hall"));
    ASSERT_TRUE(waitFor([&] { return asked(snapshot) == 1 && asked(venue) == 1 && asked(notices) >= 1; }));

    std::unique_ptr<ui::ServerLibrary> library(library_);
    library->setParent(nullptr);
    view_ = nullptr;
    window_.reset();

    for (const QByteArray& p : { snapshot, venue, notices }) http_.release(p);
    ASSERT_TRUE(waitFor([&] { return library->api().pending() == 0; }));
    QCoreApplication::processEvents();
}

// ---------- Shared (read-only) modules (docs/shared-modules.md) ----------------

namespace {

const QString kShared = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString kClubOnly = QStringLiteral("22222222-2222-4222-8222-222222222222");

// Someone else's module shared with you: in person (`direct`), and/or through Train Club.
QJsonObject sharedJson(const QString& id, const QString& title, bool direct, bool viaClub, int version = 1) {
    QJsonObject m = moduleJson(id, title, QStringLiteral("viewer"), {}, 0, version);
    m.insert(QStringLiteral("owner"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("user") }, { QStringLiteral("id"), QStringLiteral("u2") },
                                                   { QStringLiteral("name"), QStringLiteral("Alice") } });
    QJsonArray clubs;
    if (viaClub)
        clubs.append(QJsonObject{ { QStringLiteral("id"), QStringLiteral("o1") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                                  { QStringLiteral("slug"), QStringLiteral("train") } });
    m.insert(QStringLiteral("sharedVia"), QJsonObject{ { QStringLiteral("direct"), direct }, { QStringLiteral("clubs"), clubs } });
    return m;
}

void serveShared(FakeHttp& http, int version = 1) {
    http.clear("/api/modules?owner=shared");
    http.reply("/api/modules?owner=shared", 200,
               modules({ sharedJson(kShared, QStringLiteral("Harbor crane"), true, true, version),
                         sharedJson(kClubOnly, QStringLiteral("Club tunnel"), false, true) }));
}

// The server list says this server has shared modules (and no parts to download).
void rememberSharedModules(const QUrl& server) {
    sync::ServerList list = sync::ServerList::load();
    sync::ServerInfo info;
    info.features = QStringList{ QStringLiteral("liveSync"), QStringLiteral("sharedModules") };
    list.remember(server, info);
    list.save();
}

QJsonObject sharesJson(const QJsonArray& clubs, int layouts = 0) {
    QJsonArray used;
    if (layouts > 0)
        used.append(QJsonObject{ { QStringLiteral("orgId"), QStringLiteral("o1") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                                 { QStringLiteral("layouts"), layouts } });
    return { { QStringLiteral("latestVersion"), 7 },
             { QStringLiteral("canShareWithClubs"), true },
             { QStringLiteral("people"), QJsonArray{} },
             { QStringLiteral("invites"), QJsonArray{} },
             { QStringLiteral("clubs"), clubs },
             { QStringLiteral("myClubs"), QJsonArray{ QJsonObject{ { QStringLiteral("orgId"), QStringLiteral("o1") },
                                                                   { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                   { QStringLiteral("slug"), QStringLiteral("train") } } } },
             { QStringLiteral("usedIn"), QJsonObject{ { QStringLiteral("layouts"), layouts }, { QStringLiteral("clubs"), used },
                                                      { QStringLiteral("people"), QJsonArray{} }, { QStringLiteral("others"), 0 } } } };
}

}  // namespace

TEST(LibraryApi, ReadsSharedWithMeASharesAnswerAndALayoutsSharedModules) {
    FakeHttp http;
    sync::LibraryApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_t"));
    serveShared(http, 7);
    QList<sync::ServerModule> shared;
    api.sharedWithMe([&](const QList<sync::ServerModule>& l) { shared = l; }, {});
    ASSERT_TRUE(waitFor([&] { return shared.size() == 2; }));
    EXPECT_TRUE(shared[0].sharedWithMe);
    EXPECT_TRUE(shared[0].sharedDirect);
    EXPECT_EQ(shared[0].latestVersion, 7);
    EXPECT_EQ(shared[0].sharedViaText(), QStringLiteral("Alice · via Train Club"));
    EXPECT_FALSE(shared[1].sharedDirect);
    ASSERT_EQ(shared[1].sharedClubs.size(), 1);
    EXPECT_EQ(shared[1].sharedClubs[0].slug, QStringLiteral("train"));
    sync::ServerModule justMe = shared[0];
    justMe.sharedClubs.clear();
    EXPECT_EQ(justMe.sharedViaText(), QStringLiteral("Alice · shared with you"));

    const sync::ModuleShares s = sync::LibraryApi::moduleSharesFromJson(
        sharesJson({ QJsonObject{ { QStringLiteral("orgId"), QStringLiteral("o1") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                                  { QStringLiteral("slug"), QStringLiteral("train") } } },
                   3));
    EXPECT_EQ(s.latestVersion, 7);
    ASSERT_EQ(s.clubs.size(), 1);
    EXPECT_EQ(s.layoutsOf(QStringLiteral("o1")), 3);
    EXPECT_EQ(s.usedInText(), QStringLiteral("Used in 3 layout(s) (Train Club: 3)"));
    EXPECT_TRUE(sync::LibraryApi::moduleSharesFromJson(sharesJson({})).usedInText().isEmpty());

    const sync::LayoutSharedModules rows = sync::LibraryApi::layoutSharedModulesFromJson(QJsonDocument::fromJson(R"({
        "canUpdate": true, "canDetach": false,
        "modules": [{ "placedId": "p1", "moduleId": "m", "state": "shared", "version": 7, "title": "Harbor crane",
                      "owner": { "kind": "user", "name": "Alice" }, "latest": 8, "latestNote": "longer pier", "hasPicture": true },
                    { "placedId": "p2", "moduleId": "n", "state": "gone", "version": 2, "title": null, "owner": null, "latest": null, "latestNote": null }],
        "check": { "moduleId": "m", "state": "unshared", "title": "Harbor crane",
                   "moduleOwner": { "kind": "user", "name": "Alice", "mine": false },
                   "layoutOwner": { "kind": "org", "name": "Train Club", "mine": false } } })").object());
    EXPECT_TRUE(rows.canUpdate);
    EXPECT_FALSE(rows.canDetach);
    ASSERT_EQ(rows.modules.size(), 2);
    EXPECT_EQ(rows.row(QStringLiteral("p1"))->latest, 8);
    EXPECT_EQ(rows.row(QStringLiteral("p1"))->latestNote, QStringLiteral("longer pier"));
    EXPECT_TRUE(rows.row(QStringLiteral("p1"))->following());
    EXPECT_FALSE(rows.row(QStringLiteral("p2"))->following());
    ASSERT_TRUE(rows.check);
    EXPECT_EQ(rows.check->layoutOwner.kind, QStringLiteral("org"));

    // Update: the version named; Put back: the same one, `again`.
    http.reply("/api/layouts/L1/shared-modules/p1/apply", 200,
               { { QStringLiteral("ok"), true }, { QStringLiteral("placedId"), QStringLiteral("p1") }, { QStringLiteral("changed"), true },
                 { QStringLiteral("version"), 8 }, { QStringLiteral("from"), 7 }, { QStringLiteral("parts"), 12 } });
    sync::SharedModuleApplied applied;
    api.applySharedModule(QStringLiteral("L1"), QStringLiteral("p1"), 8, false, [&](const sync::SharedModuleApplied& a) { applied = a; }, {});
    ASSERT_TRUE(waitFor([&] { return applied.version == 8; }));
    EXPECT_TRUE(applied.changed);
    EXPECT_EQ(applied.from, 7);
    EXPECT_EQ(QJsonDocument::fromJson(lastRequest(http, "POST", "/api/layouts/L1/shared-modules/p1/apply")->body).object(),
              (QJsonObject{ { QStringLiteral("version"), 8 } }));
    applied = {};
    api.applySharedModule(QStringLiteral("L1"), QStringLiteral("p1"), 8, true, [&](const sync::SharedModuleApplied& a) { applied = a; }, {});
    ASSERT_TRUE(waitFor([&] { return applied.version == 8; }));
    EXPECT_EQ(QJsonDocument::fromJson(lastRequest(http, "POST", "/api/layouts/L1/shared-modules/p1/apply")->body).object(),
              (QJsonObject{ { QStringLiteral("version"), 8 }, { QStringLiteral("again"), true } }));
}

// The Module library's tabs on a server with shared modules: Mine, Clubs,
// Shared with me, each with how many; a shared module names its owner and
// how it reached you. A server without them keeps the one list.
TEST_F(LibraryTabs, MineClubsAndSharedWithMe) {
    serveModules();
    serveShared(http_);
    library_.setKnowsSharedModules(true);
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    ASSERT_TRUE(tab.tabs()->isVisibleTo(&tab));
    EXPECT_EQ(tab.tabs()->tabText(ui::ServerModulesTab::Mine), QStringLiteral("Mine (2)"));
    EXPECT_EQ(tab.tabs()->tabText(ui::ServerModulesTab::Clubs), QStringLiteral("Clubs (1)"));
    EXPECT_EQ(tab.tabs()->tabText(ui::ServerModulesTab::Shared), QStringLiteral("Shared with me (2)"));
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Yours"), QStringLiteral("Freight yard"), QStringLiteral("Station") }));
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Clubs);
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Train Club"), QStringLiteral("Engine shed") }));

    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Shared);
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Harbor crane"), QStringLiteral("Club tunnel") }));
    QWidget* crane = tab.row(kShared);
    ASSERT_NE(crane, nullptr);
    EXPECT_EQ(crane->findChild<QLabel*>(QStringLiteral("rowSharedVia"))->text(), QStringLiteral("🔗 Alice · via Train Club"));
    EXPECT_EQ(crane->findChild<QPushButton*>(QStringLiteral("insertModule"))->text(), QStringLiteral("Add to layout"));
    EXPECT_EQ(crane->findChild<QToolButton*>(QStringLiteral("moduleMore")), nullptr) << "not yours to change: no menu";
    EXPECT_EQ(crane->findChild<QPushButton*>(QStringLiteral("removeClubShare")), nullptr) << "a member can't take it off the club";
    EXPECT_NE(tab.findChild<QLabel*>(QStringLiteral("sharedModulesAbout")), nullptr);
    QString inserted;
    QObject::connect(&library_, &ui::ServerLibrary::insertRequested, [&](const QString& id) { inserted = id; });
    crane->findChild<QPushButton*>(QStringLiteral("insertModule"))->click();
    EXPECT_EQ(inserted, kShared);

    // Nothing shared: it says what will show here.
    http_.clear("/api/modules?owner=shared");
    http_.reply("/api/modules?owner=shared", 200, modules({}));
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return tab.tabs()->tabText(ui::ServerModulesTab::Shared) == QStringLiteral("Shared with me"); }));
    auto* nothing = tab.findChild<QLabel*>(QStringLiteral("serverModulesEmpty"));
    ASSERT_NE(nothing, nullptr);
    EXPECT_TRUE(nothing->text().startsWith(QStringLiteral("Nothing is shared with you yet.")));

    // An older server: no tabs, the one list as before, and it is never asked.
    const int asked = count(http_, "GET", "/api/modules?owner=shared");
    library_.setKnowsSharedModules(false);
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return !tab.tabs()->isVisibleTo(&tab); }));
    QCoreApplication::processEvents();
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Yours"), QStringLiteral("Freight yard"), QStringLiteral("Station"),
                                         QStringLiteral("Train Club"), QStringLiteral("Engine shed") }));
    EXPECT_EQ(count(http_, "GET", "/api/modules?owner=shared"), asked);
}

// A club's admins and managers can take a shared module off their club.
TEST_F(LibraryTabs, AClubAdminCanRemoveASharedModuleFromTheClub) {
    serveModules();
    serveShared(http_);
    http_.clear("/api/orgs");
    http_.reply("/api/orgs", 200,
                { { QStringLiteral("orgs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") },
                                                                      { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                      { QStringLiteral("myRole"), QStringLiteral("admin") } } } } });
    library_.setKnowsSharedModules(true);
    ui::ServerModulesTab tab(library_);
    tab.show();
    connectSignedIn();
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Shared);
    auto* remove = tab.row(kClubOnly)->findChild<QPushButton*>(QStringLiteral("removeClubShare"));
    ASSERT_NE(remove, nullptr);
    EXPECT_EQ(remove->text(), QStringLiteral("Remove from Train Club…"));
    // It asks first, saying what stays.
    ui::ConfirmOptions asked;
    tab.confirmRemoveFromClub = [&](const sync::ServerModule& m, const sync::ClubRef& club) {
        asked.title = m.title + QStringLiteral(" / ") + club.name;
        return false;
    };
    remove->click();
    EXPECT_EQ(asked.title, QStringLiteral("Club tunnel / Train Club"));
    EXPECT_EQ(count(http_, "DELETE", "/api/modules/"), 0) << "Cancel: nothing is removed";

    tab.confirmRemoveFromClub = [](const sync::ServerModule&, const sync::ClubRef&) { return true; };
    http_.replyTo("DELETE", ("/api/modules/" + kClubOnly + "/club-shares/o1").toUtf8(), 200, { { QStringLiteral("ok"), true } });
    http_.clear("/api/modules?owner=shared");
    http_.reply("/api/modules?owner=shared", 200, modules({ sharedJson(kShared, QStringLiteral("Harbor crane"), true, true) }));
    QString said;
    QObject::connect(&library_, &ui::ServerLibrary::message, [&](const QString& t) { said = t; });
    tab.row(kClubOnly)->findChild<QPushButton*>(QStringLiteral("removeClubShare"))->click();
    ASSERT_TRUE(waitFor([&] { return tab.row(kClubOnly) == nullptr; })) << "gone from the list without a refresh";
    EXPECT_EQ(count(http_, "DELETE", ("/api/modules/" + kClubOnly + "/club-shares/o1").toUtf8()), 1);
    EXPECT_EQ(said, QStringLiteral("“Club tunnel” is off Train Club's list."));
}

// Sharing your module with a club: the Share dialog lists the clubs, shares
// with one, says where it's used, and stopping asks first, saying what stays.
TEST_F(LibraryTabs, ShareWithAClubAndStopWithWhatThatMeansSaidFirst) {
    serveModules();
    serveShared(http_);
    library_.setKnowsSharedModules(true);
    ui::ServerModulesTab tab(library_);
    tab.show();
    connectSignedIn();
    const QByteArray shares = "/api/modules/m1/shares";
    http_.reply(shares, 200, sharesJson({}));
    QAction* share = tab.row(QStringLiteral("m1"))->findChild<QAction*>(QStringLiteral("shareModule"));
    ASSERT_NE(share, nullptr);
    EXPECT_EQ(tab.row(QStringLiteral("m2"))->findChild<QAction*>(QStringLiteral("shareModule")), nullptr) << "only its owner shares it";
    share->trigger();
    ui::ModuleShareDialog* dlg = tab.shareDialog();
    ASSERT_NE(dlg, nullptr);
    ASSERT_TRUE(waitFor([&] { return dlg->loaded(); }));
    EXPECT_EQ(dlg->windowTitle(), QStringLiteral("Share “Freight yard”"));
    EXPECT_TRUE(dlg->note()->text().contains(QStringLiteral("can't change its parts")));
    ASSERT_EQ(dlg->clubPick()->count(), 2);
    EXPECT_EQ(dlg->clubPick()->itemText(1), QStringLiteral("Train Club"));
    EXPECT_FALSE(dlg->shareButton()->isEnabled()) << "pick a club first";
    EXPECT_TRUE(dlg->usedIn()->isHidden());

    // Share with Train Club.
    const QJsonObject club{ { QStringLiteral("orgId"), QStringLiteral("o1") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                            { QStringLiteral("slug"), QStringLiteral("train") } };
    http_.replyTo("POST", "/api/modules/m1/club-shares", 200, { { QStringLiteral("ok"), true } });
    http_.clear(shares);
    http_.reply(shares, 200, sharesJson({ club }, 3));
    dlg->clubPick()->setCurrentIndex(1);
    ASSERT_TRUE(dlg->shareButton()->isEnabled());
    dlg->shareButton()->click();
    ASSERT_TRUE(waitFor([&] { return dlg->stopButton(QStringLiteral("o1")) != nullptr; }));
    EXPECT_EQ(QJsonDocument::fromJson(lastRequest(http_, "POST", "/api/modules/m1/club-shares")->body).object(),
              (QJsonObject{ { QStringLiteral("orgSlug"), QStringLiteral("train") } }));
    EXPECT_EQ(dlg->usedIn()->text(), QStringLiteral("Used in 3 layout(s) (Train Club: 3)"));
    EXPECT_FALSE(dlg->clubPick()->isVisibleTo(dlg)) << "shared with every club you're in";
    EXPECT_TRUE(dlg->note()->text().contains(QStringLiteral("It's shared with every club you're in.")));

    // Stop: what stays is said first; Cancel stops nothing.
    ui::ConfirmOptions asked;
    dlg->confirmStop = [&](const ui::ConfirmOptions& o) {
        asked = o;
        return false;
    };
    dlg->stopButton(QStringLiteral("o1"))->click();
    EXPECT_EQ(asked.title, QStringLiteral("Stop sharing with Train Club?"));
    EXPECT_EQ(asked.keeps, QStringLiteral("Its 3 layout(s) keep “Freight yard” as it is now (version 7)."));
    EXPECT_EQ(asked.removes, QStringLiteral("They won't get your updates anymore, and can't place it again."));
    EXPECT_EQ(asked.confirmLabel, QStringLiteral("Stop"));
    EXPECT_EQ(count(http_, "DELETE", "/api/modules/m1/club-shares/o1"), 0);

    dlg->confirmStop = [](const ui::ConfirmOptions&) { return true; };
    http_.replyTo("DELETE", "/api/modules/m1/club-shares/o1", 200, { { QStringLiteral("ok"), true } });
    http_.clear(shares);
    http_.reply(shares, 200, sharesJson({}));
    dlg->stopButton(QStringLiteral("o1"))->click();
    ASSERT_TRUE(waitFor([&] { return dlg->stopButton(QStringLiteral("o1")) == nullptr; }));
    EXPECT_EQ(count(http_, "DELETE", "/api/modules/m1/club-shares/o1"), 1);
    EXPECT_TRUE(dlg->clubPick()->isVisibleTo(dlg));
    dlg->close();
}

namespace {
// A club's module's Share dialog answer (docs/shared-modules.md, section 11).
QJsonObject clubSharesJson(const QJsonArray& clubs, const QJsonArray& offers, const QJsonArray& partners) {
    return { { QStringLiteral("latestVersion"), 2 },
             { QStringLiteral("canShareWithClubs"), false },
             { QStringLiteral("canShareWithOtherClubs"), true },
             { QStringLiteral("club"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("o1") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                    { QStringLiteral("slug"), QStringLiteral("train") } } },
             { QStringLiteral("partners"), partners },
             { QStringLiteral("offers"), offers },
             { QStringLiteral("people"), QJsonArray{} },
             { QStringLiteral("invites"), QJsonArray{} },
             { QStringLiteral("clubs"), clubs },
             { QStringLiteral("myClubs"), QJsonArray{} },
             { QStringLiteral("usedIn"), QJsonObject{ { QStringLiteral("layouts"), 0 }, { QStringLiteral("clubs"), QJsonArray{} },
                                                      { QStringLiteral("people"), QJsonArray{} }, { QStringLiteral("others"), 0 } } } };
}
QJsonObject clubJson(const QString& id, const QString& name, const QString& slug) {
    return { { QStringLiteral("orgId"), id }, { QStringLiteral("name"), name }, { QStringLiteral("slug"), slug } };
}
}  // namespace

// A club's module: Share with Other Clubs… shares with a partner club at
// once, offers it to another club (found by search, or by a pasted link),
// and the offer can be taken back, or offered again once declined.
TEST_F(LibraryTabs, AClubsModuleGoesToAPartnerAtOnceOrIsOfferedToAnotherClub) {
    http_.reply("/api/modules", 200,
                modules({ moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"), QStringLiteral("editor"), QStringLiteral("o1")),
                          moduleJson(QStringLiteral("c2"), QStringLiteral("Harbor crane"), QStringLiteral("owner"), QStringLiteral("o1")) }));
    serveShared(http_);
    library_.setKnowsSharedModules(true);
    ui::ServerModulesTab tab(library_);
    tab.show();
    connectSignedIn();
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Clubs);
    QAction* share = tab.row(QStringLiteral("c2"))->findChild<QAction*>(QStringLiteral("shareModule"));
    ASSERT_NE(share, nullptr);
    EXPECT_EQ(share->text(), QStringLiteral("Share with Other Clubs…"));
    EXPECT_EQ(tab.row(QStringLiteral("c1"))->findChild<QAction*>(QStringLiteral("shareModule")), nullptr) << "only the club's Admins and Managers";
    const QByteArray shares = "/api/modules/c2/shares";
    const QJsonArray lug{ clubJson(QStringLiteral("o2"), QStringLiteral("Riverside LUG"), QStringLiteral("lug")) };
    http_.reply(shares, 200, clubSharesJson({}, {}, lug));
    share->trigger();
    ui::ModuleShareDialog* dlg = tab.shareDialog();
    ASSERT_NE(dlg, nullptr);
    ASSERT_TRUE(waitFor([&] { return dlg->loaded(); }));
    EXPECT_TRUE(dlg->note()->text().contains(QStringLiteral("This is Train Club's module. Partner clubs get it straight away. Any other club must accept it first.")));
    EXPECT_FALSE(dlg->clubPick()->isVisibleTo(dlg)) << "not the person's clubs";
    ASSERT_EQ(dlg->partnerPick()->count(), 2);
    EXPECT_EQ(dlg->partnerPick()->itemText(1), QStringLiteral("Riverside LUG"));
    EXPECT_FALSE(dlg->partnerShareButton()->isEnabled());

    // A partner club: shared at once.
    http_.replyTo("POST", "/api/modules/c2/club-shares", 201,
                  { { QStringLiteral("ok"), true }, { QStringLiteral("status"), QStringLiteral("shared") },
                    { QStringLiteral("name"), QStringLiteral("Riverside LUG") }, { QStringLiteral("slug"), QStringLiteral("lug") } });
    QJsonObject lugShared = clubJson(QStringLiteral("o2"), QStringLiteral("Riverside LUG"), QStringLiteral("lug"));
    lugShared.insert(QStringLiteral("via"), QStringLiteral("partner"));
    http_.clear(shares);
    http_.reply(shares, 200, clubSharesJson({ lugShared }, {}, lug));
    dlg->partnerPick()->setCurrentIndex(1);
    dlg->partnerShareButton()->click();
    ASSERT_TRUE(waitFor([&] { return dlg->stopButton(QStringLiteral("o2")) != nullptr; }));
    EXPECT_EQ(QJsonDocument::fromJson(lastRequest(http_, "POST", "/api/modules/c2/club-shares")->body).object(),
              (QJsonObject{ { QStringLiteral("orgSlug"), QStringLiteral("lug") } }));
    EXPECT_EQ(dlg->said()->text(), QStringLiteral("“Harbor crane” is shared with Riverside LUG."));
    QStringList rows;
    for (QLabel* l : dlg->findChildren<QLabel*>()) rows << l->text();
    EXPECT_TRUE(rows.contains(QStringLiteral("Riverside LUG (partner club) · can place it")));
    EXPECT_FALSE(dlg->partnerPick()->isVisibleTo(dlg));
    EXPECT_EQ(dlg->findChild<QLabel*>(QStringLiteral("partnersNote"))->text(), QStringLiteral("It's shared with every partner club."));

    // Another club, found by search: offered, waiting, taken back.
    http_.reply("/api/modules/c2/club-targets?q=hill", 200,
                { { QStringLiteral("clubs"), QJsonArray{ QJsonObject{ { QStringLiteral("orgId"), QStringLiteral("o3") }, { QStringLiteral("name"), QStringLiteral("Hill Club") },
                                                                      { QStringLiteral("slug"), QStringLiteral("hill") }, { QStringLiteral("partner"), false },
                                                                      { QStringLiteral("shared"), false }, { QStringLiteral("offer"), QJsonValue() } } } } });
    dlg->otherClub()->setText(QStringLiteral("hill"));
    dlg->searchNow();
    ASSERT_TRUE(waitFor([&] { return dlg->targetButton(QStringLiteral("hill")) != nullptr; }));
    EXPECT_EQ(dlg->targetButton(QStringLiteral("hill"))->text(), QStringLiteral("Offer"));
    http_.clear("POST /api/modules/c2/club-shares");
    http_.replyTo("POST", "/api/modules/c2/club-shares", 202,
                  { { QStringLiteral("ok"), true }, { QStringLiteral("status"), QStringLiteral("offered") }, { QStringLiteral("offerId"), QStringLiteral("f1") },
                    { QStringLiteral("name"), QStringLiteral("Hill Club") }, { QStringLiteral("slug"), QStringLiteral("hill") } });
    const QJsonObject hill{ { QStringLiteral("orgId"), QStringLiteral("o3") }, { QStringLiteral("name"), QStringLiteral("Hill Club") }, { QStringLiteral("slug"), QStringLiteral("hill") } };
    http_.clear(shares);
    http_.reply(shares, 200,
                clubSharesJson({ lugShared }, { QJsonObject{ { QStringLiteral("id"), QStringLiteral("f1") }, { QStringLiteral("club"), hill },
                                                             { QStringLiteral("state"), QStringLiteral("pending") }, { QStringLiteral("askAgainAt"), QJsonValue() } } },
                               lug));
    dlg->targetButton(QStringLiteral("hill"))->click();
    ASSERT_TRUE(waitFor([&] { return dlg->said()->text().startsWith(QStringLiteral("Offered")) && dlg->withdrawButton(QStringLiteral("f1")) != nullptr; }));
    EXPECT_EQ(dlg->said()->text().toStdString(), "Offered “Harbor crane” to Hill Club. It's shared once their Admins or Managers accept it.");
    EXPECT_EQ(dlg->findChild<QLabel*>(QStringLiteral("offer:f1"))->text(), QStringLiteral("Hill Club · waiting for them to accept"));
    EXPECT_EQ(dlg->withdrawButton(QStringLiteral("f1"))->text(), QStringLiteral("Take Back"));
    EXPECT_EQ(dlg->offerAgainButton(QStringLiteral("f1")), nullptr);

    // Declined (their answer): Offer Again once the day is over, or Remove.
    http_.replyTo("DELETE", "/api/modules/c2/club-offers/f1", 200, { { QStringLiteral("ok"), true } });
    http_.clear(shares);
    http_.reply(shares, 200,
                clubSharesJson({ lugShared }, { QJsonObject{ { QStringLiteral("id"), QStringLiteral("f1") }, { QStringLiteral("club"), hill },
                                                             { QStringLiteral("state"), QStringLiteral("declined") }, { QStringLiteral("askAgainAt"), 1000.0 } } },
                               lug));
    dlg->reload();
    ASSERT_TRUE(waitFor([&] { return dlg->offerAgainButton(QStringLiteral("f1")) != nullptr; }));
    EXPECT_EQ(dlg->findChild<QLabel*>(QStringLiteral("offer:f1"))->text(), QStringLiteral("Hill Club · declined"));
    EXPECT_EQ(dlg->withdrawButton(QStringLiteral("f1"))->text(), QStringLiteral("Remove"));
    dlg->withdrawButton(QStringLiteral("f1"))->click();
    ASSERT_TRUE(waitFor([&] { return count(http_, "DELETE", "/api/modules/c2/club-offers/f1") == 1; }));

    // A pasted link to a club that isn't listed: offered, named by its address.
    http_.clear("POST /api/modules/c2/club-shares");
    http_.replyTo("POST", "/api/modules/c2/club-shares", 202,
                  { { QStringLiteral("ok"), true }, { QStringLiteral("status"), QStringLiteral("offered") }, { QStringLiteral("offerId"), QStringLiteral("f2") },
                    { QStringLiteral("name"), QJsonValue() }, { QStringLiteral("slug"), QStringLiteral("secret") } });
    dlg->otherClub()->setText(QStringLiteral("https://example.org/orgs/secret"));
    ASSERT_TRUE(dlg->offerLinkButton()->isVisibleTo(dlg));
    dlg->offerLinkButton()->click();
    ASSERT_TRUE(waitFor([&] { return dlg->said()->text().contains(QStringLiteral("secret")); }));
    EXPECT_EQ(dlg->said()->text(), QStringLiteral("Offered “Harbor crane” to the club at %1/orgs/secret. It's shared once their Admins or Managers accept it.").arg(http_.base().toString()));
    EXPECT_EQ(QJsonDocument::fromJson(lastRequest(http_, "POST", "/api/modules/c2/club-shares")->body).object(),
              (QJsonObject{ { QStringLiteral("orgSlug"), QStringLiteral("https://example.org/orgs/secret") } }));
    dlg->close();
}

// With no partner clubs, the dialog says so and points to asking one on the website.
TEST_F(LibraryTabs, AClubsModuleWithNoPartnersSaysHowToGetOne) {
    http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("c2"), QStringLiteral("Harbor crane"), QStringLiteral("owner"), QStringLiteral("o1")) }));
    serveShared(http_);
    library_.setKnowsSharedModules(true);
    ui::ServerModulesTab tab(library_);
    tab.show();
    connectSignedIn();
    http_.reply("/api/modules/c2/shares", 200, clubSharesJson({}, {}, {}));
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Clubs);
    tab.row(QStringLiteral("c2"))->findChild<QAction*>(QStringLiteral("shareModule"))->trigger();
    ui::ModuleShareDialog* dlg = tab.shareDialog();
    ASSERT_TRUE(waitFor([&] { return dlg->loaded(); }));
    auto* note = dlg->findChild<QLabel*>(QStringLiteral("partnersNote"));
    EXPECT_TRUE(note->text().contains(QStringLiteral("Train Club has no partner clubs yet.")));
    EXPECT_TRUE(note->text().contains(QStringLiteral("/orgs/train?tab=partners")));
    EXPECT_FALSE(dlg->partnerPick()->isVisibleTo(dlg));
    dlg->close();
}

// Another club's module shared with a club of yours is on the Clubs tab,
// under "From ‹that club›", not under Shared with me; what waits for an
// answer in a club you look after points to the website.
TEST_F(LibraryTabs, AnotherClubsModuleIsOnTheClubsTabAndWhatWaitsPointsToTheWebsite) {
    serveModules();
    QJsonObject tower = sharedJson(QStringLiteral("33333333-3333-4333-8333-333333333333"), QStringLiteral("Signal tower"), false, true);
    tower.insert(QStringLiteral("owner"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("id"), QStringLiteral("o9") },
                                                       { QStringLiteral("name"), QStringLiteral("Hill Club") }, { QStringLiteral("slug"), QStringLiteral("hill") } });
    http_.reply("/api/modules?owner=shared", 200,
                modules({ sharedJson(kShared, QStringLiteral("Harbor crane"), true, true), sharedJson(kClubOnly, QStringLiteral("Club tunnel"), false, true), tower }));
    http_.reply("/api/club-requests/count", 200,
                { { QStringLiteral("count"), 2 },
                  { QStringLiteral("clubs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                      { QStringLiteral("count"), 2 } } } } });
    library_.setKnowsSharedModules(true);
    ui::ServerModulesTab tab(library_);
    tab.resize(360, 900);
    tab.show();
    connectSignedIn();
    EXPECT_EQ(tab.tabs()->tabText(ui::ServerModulesTab::Clubs), QStringLiteral("Clubs (2)"));
    EXPECT_EQ(tab.tabs()->tabText(ui::ServerModulesTab::Shared), QStringLiteral("Shared with me (2)"));
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Clubs);
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Train Club"), QStringLiteral("Engine shed"), QStringLiteral("From Hill Club"), QStringLiteral("Signal tower") }));
    QWidget* row = tab.row(QStringLiteral("33333333-3333-4333-8333-333333333333"));
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->findChild<QLabel*>(QStringLiteral("rowSharedVia"))->text(), QStringLiteral("🔗 shared with Train Club"));
    EXPECT_NE(row->findChild<QPushButton*>(QStringLiteral("insertModule")), nullptr);
    QWidget* waiting = tab.findChild<QWidget*>(QStringLiteral("clubRequestsWaiting:train"));
    ASSERT_NE(waiting, nullptr);
    QStringList words;
    for (QLabel* l : waiting->findChildren<QLabel*>()) words << l->text();
    EXPECT_TRUE(words.contains(QStringLiteral("Train Club: 2 partner request(s) or module offer(s) wait for an answer. Answer them on the website.")));
    EXPECT_NE(waiting->findChild<QPushButton*>(QStringLiteral("openClubPartners")), nullptr);
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Shared);
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Harbor crane"), QStringLiteral("Club tunnel") }));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(tab.findChild<QWidget*>(QStringLiteral("clubRequestsWaiting:train")), nullptr);

    // Answered on the website: the next refresh (a club hint) drops the line.
    http_.clear("/api/club-requests/count");
    http_.reply("/api/club-requests/count", 200, { { QStringLiteral("count"), 0 }, { QStringLiteral("clubs"), QJsonArray{} } });
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Clubs);
    library_.refresh();
    ASSERT_TRUE(waitFor([&] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return tab.findChild<QWidget*>(QStringLiteral("clubRequestsWaiting:train")) == nullptr;
    }));
}

// BLD_REVIEW_SHOTS=<dir>: pictures of club-to-club sharing for a PR, light
// and dark (skipped otherwise). "Before": a server without partner clubs (a
// club's module can't be shared). "After": the Clubs tab with "From ‹club›" and what waits, and the
// Share dialog of a club's module with a partner club and an offer.
TEST_F(LibraryTabs, PicturesOfClubToClubForReview) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    QDir().mkpath(out);
    QTranslator english;
    if (english.load(QStringLiteral("bld_en"), QCoreApplication::applicationDirPath() + QStringLiteral("/../..")))
        QCoreApplication::installTranslator(&english);
    ui::theme::PrefsStore store(QStringLiteral("review-club-partners-shots"));
    ui::theme::ThemeManager themer(store);
    const auto shoot = [&](QWidget* w, const QString& name) {
        for (const auto mode : { ui::theme::ThemeChoice::Light, ui::theme::ThemeChoice::Dark }) {
            ui::theme::AppPrefs p = store.prefs();
            p.theme = mode;
            store.update(p);
            waitFor([] { return false; }, 400);
            EXPECT_TRUE(w->grab().save(out + QStringLiteral("/%1-%2.png").arg(name, mode == ui::theme::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light"))));
        }
    };
    const QString towerId = QStringLiteral("33333333-3333-4333-8333-333333333333");
    QJsonObject tower = sharedJson(towerId, QStringLiteral("Signal tower"), false, true);
    tower.insert(QStringLiteral("owner"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("id"), QStringLiteral("o9") },
                                                       { QStringLiteral("name"), QStringLiteral("Riverside LUG") }, { QStringLiteral("slug"), QStringLiteral("lug") } });
    http_.reply("/api/modules", 200,
                modules({ moduleJson(QStringLiteral("c2"), QStringLiteral("Harbor crane"), QStringLiteral("owner"), QStringLiteral("o1")),
                          moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"), QStringLiteral("owner"), QStringLiteral("o1")),
                          moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")) }));
    http_.reply("/api/modules?owner=shared", 200, modules({ sharedJson(kShared, QStringLiteral("Water mill"), true, false), tower }));
    library_.setKnowsSharedModules(true);
    ui::ServerModulesTab tab(library_);
    tab.resize(380, 620);
    tab.show();

    // Before: a server without partner clubs (a club's module can't be shared).
    connectSignedIn();
    QJsonObject oldShares = clubSharesJson({}, {}, {});
    oldShares.insert(QStringLiteral("canShareWithOtherClubs"), QJsonValue());
    http_.reply("/api/modules/c2/shares", 200, oldShares);
    tab.tabs()->setCurrentIndex(ui::ServerModulesTab::Clubs);
    QCoreApplication::processEvents();
    tab.row(QStringLiteral("c2"))->findChild<QAction*>(QStringLiteral("shareModule"))->trigger();
    ui::ModuleShareDialog* dlg = tab.shareDialog();
    ASSERT_TRUE(waitFor([&] { return dlg->loaded(); }));
    shoot(dlg, QStringLiteral("before-share-dialog"));
    dlg->close();

    // After: the Clubs tab.
    http_.reply("/api/club-requests/count", 200,
                { { QStringLiteral("count"), 2 },
                  { QStringLiteral("clubs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                      { QStringLiteral("count"), 2 } } } } });
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return tab.findChild<QWidget*>(QStringLiteral("clubRequestsWaiting:train")) != nullptr; }));
    tab.resize(380, 720);
    QCoreApplication::processEvents();
    shoot(&tab, QStringLiteral("after-library-clubs"));

    // After: a club's module's Share dialog, with a partner club and an offer waiting.
    QJsonObject lugShared = clubJson(QStringLiteral("o9"), QStringLiteral("Riverside LUG"), QStringLiteral("lug"));
    lugShared.insert(QStringLiteral("via"), QStringLiteral("partner"));
    const QJsonObject hill{ { QStringLiteral("orgId"), QStringLiteral("o3") }, { QStringLiteral("name"), QStringLiteral("Hill Club") }, { QStringLiteral("slug"), QStringLiteral("hill") } };
    http_.clear("/api/modules/c2/shares");
    http_.reply("/api/modules/c2/shares", 200,
                clubSharesJson({ lugShared },
                               { QJsonObject{ { QStringLiteral("id"), QStringLiteral("f1") }, { QStringLiteral("club"), hill }, { QStringLiteral("state"), QStringLiteral("pending") } } },
                               { clubJson(QStringLiteral("o9"), QStringLiteral("Riverside LUG"), QStringLiteral("lug")),
                                 clubJson(QStringLiteral("o4"), QStringLiteral("Bay Bricks"), QStringLiteral("bay")) }));
    http_.reply("/api/modules/c2/club-targets?q=hill", 200,
                { { QStringLiteral("clubs"), QJsonArray{ QJsonObject{ { QStringLiteral("orgId"), QStringLiteral("o3") }, { QStringLiteral("name"), QStringLiteral("Hill Club") },
                                                                      { QStringLiteral("slug"), QStringLiteral("hill") }, { QStringLiteral("offer"), QStringLiteral("pending") } },
                                                         QJsonObject{ { QStringLiteral("orgId"), QStringLiteral("o5") }, { QStringLiteral("name"), QStringLiteral("Hillside Trains") },
                                                                      { QStringLiteral("slug"), QStringLiteral("hillside") } } } } });
    tab.row(QStringLiteral("c2"))->findChild<QAction*>(QStringLiteral("shareModule"))->trigger();
    dlg = tab.shareDialog();
    ASSERT_TRUE(waitFor([&] { return dlg->loaded() && dlg->withdrawButton(QStringLiteral("f1")) != nullptr; }));
    dlg->otherClub()->setText(QStringLiteral("hill"));
    dlg->searchNow();
    ASSERT_TRUE(waitFor([&] { return dlg->targetButton(QStringLiteral("hillside")) != nullptr; }));
    dlg->adjustSize();
    shoot(dlg, QStringLiteral("after-share-dialog"));
    dlg->close();
}

// ---- In the window, with a layout from a file -------------------------------

namespace {

class SharedModulesWindow : public ServerModulesWindow {
protected:
    void beforeOpen() override {
        rememberSharedModules(http_.base());
        QSettings().remove(QStringLiteral("sharedModules/tipShown"));
        http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")) }));
        serveShared(http_);
        serveVersion(1, *sampleModule(2));
        http_.replyRaw(("/api/modules/" + kShared + "/snapshot").toUtf8(), 200, snapshotOf(*sampleModule(2)), "application/octet-stream");
    }
    void serveVersion(int n, const core::Map& contents) {
        http_.replyRaw(QStringLiteral("/api/modules/%1/versions/%2/snapshot").arg(kShared).arg(n).toUtf8(), 200, snapshotOf(contents),
                       "application/octet-stream");
    }
    // Alice saves version `n`: the library's list says so (a live hint would ask for it).
    void ownerSaves(int n, const core::Map& contents) {
        serveVersion(n, contents);
        serveShared(http_, n);
        library_->refresh();
        ASSERT_TRUE(waitFor([&] { return library_->sharedModule(kShared) && library_->sharedModule(kShared)->latestVersion == n; }));
    }
    // "Harbor crane" placed from Shared with me.
    void placeShared() {
        const auto answers = answerNewSheets();
        emit library_->insertRequested(kShared);
        ASSERT_TRUE(waitFor([&] { return view_->currentMap() && !view_->currentMap()->sidecar.modules.empty(); }));
        ASSERT_TRUE(waitFor([&] { return window_->sharedModuleRowsKnown(); }));
    }
    const core::Module& mod() { return view_->currentMap()->sidecar.modules.front(); }
    ui::SharedModulesBar* bar() { return view_->sharedModulesBar(); }
    ui::NoticeArea* notices() { return window_->findChild<ui::NoticeArea*>(); }
    QString panelLine() {
        auto* list = window_->findChild<ui::ModulesPanel*>()->findChild<QListWidget*>();
        return list && list->count() > 0 ? list->item(0)->text() : QString();
    }
};

}  // namespace

// Placed from Shared with me, a module is shared: locked, with its owner's
// name, its server and where it sits; a one-time tip says what that means.
TEST_F(SharedModulesWindow, AModuleFromSharedWithMeIsPlacedLockedAndFollowsItsOwner) {
    placeShared();
    const auto shared = core::sharedPlaced(mod());
    ASSERT_TRUE(shared);
    EXPECT_EQ(shared->moduleId, kShared);
    EXPECT_EQ(shared->version, 1);
    EXPECT_EQ(shared->by, QStringLiteral("Alice"));
    EXPECT_EQ(shared->server, http_.base().toString(QUrl::StripTrailingSlash));
    ASSERT_TRUE(shared->placement && shared->placement->sheets);
    EXPECT_EQ(shared->placement->sheets->size(), 2) << "each of its sheets, with the sheet it went on";
    EXPECT_EQ(mod().name, QStringLiteral("Harbor crane"));
    EXPECT_EQ(bricksInMap(), 3);
    // Where it sits is its version placed by the placement.
    auto version = sampleModule(2);
    parts::placement::fixStaleAreas(*version, bundledParts());
    EXPECT_TRUE(edit::matchesVersion(edit::batchesOfModule(*version), edit::placedParts(*view_->currentMap(), mod().memberIds),
                                     bundledParts(), shared->placement));
    EXPECT_EQ(edit::partsInPlace(edit::batchesOfModule(*version), edit::placedParts(*view_->currentMap(), mod().memberIds),
                                 *shared->placement, bundledParts()),
              3);
    EXPECT_TRUE(notices()->isShown(QStringLiteral("shared-tip")));
    EXPECT_TRUE(panelLine().contains(QStringLiteral("🔗 Shared by Alice · v1"))) << panelLine().toStdString();

    // Its parts are its owner's: Edit module and Rename say why instead.
    const QString id = mod().id;
    view_->setEditingModule(id);
    EXPECT_TRUE(view_->editingModule().isEmpty());
    EXPECT_TRUE(notices()->isShown(QStringLiteral("shared-lock")));
    view_->undoStack()->push(new edit::RenameModuleCommand(*view_->currentMap(), id, QStringLiteral("Mine")));
    EXPECT_EQ(mod().name, QStringLiteral("Harbor crane"));

    // The tip comes once.
    notices()->hideNotice(QStringLiteral("shared-tip"));
    placeShared();
    EXPECT_FALSE(notices()->isShown(QStringLiteral("shared-tip")));
}

// A module shared with a club only is for the club's layouts: in a file of
// your own it says whom to ask, with a message to send, and places nothing.
TEST_F(SharedModulesWindow, AModuleSharedOnlyWithAClubSaysWhomToAskInsteadOfBeingPlaced) {
    ui::ConfirmOptions asked;
    window_->confirmModuleLibrary_ = [&](const ui::ConfirmOptions& o) {
        asked = o;
        return true;
    };
    emit library_->insertRequested(kClubOnly);
    EXPECT_EQ(asked.title, QStringLiteral("“Club tunnel” can't go in this layout yet"));
    EXPECT_EQ(asked.keeps, QStringLiteral("Alice shared “Club tunnel” with a club you're in, for the club's layouts, not with you. "
                                          "Ask Alice to share it with you too."));
    EXPECT_EQ(asked.confirmLabel, QStringLiteral("Copy a Message to Send"));
    EXPECT_TRUE(QGuiApplication::clipboard()->text().startsWith(QStringLiteral("Hi Alice, could you share your module “Club tunnel” with me as well?")));
    QCoreApplication::processEvents();
    EXPECT_EQ(bricksInMap(), 0);
    EXPECT_EQ(count(http_, "GET", ("/api/modules/" + kClubOnly + "/snapshot").toUtf8()), 0);
}

// The owner saves a new version: the layout says so (Show / Update / Not
// now). Update puts it in where the module sits; then Go back and OK.
TEST_F(SharedModulesWindow, AnUpdateIsOfferedPutInGoneBackOnAndMarkedSeen) {
    placeShared();
    EXPECT_FALSE(bar()->updatesShown());
    const QString id = mod().id;
    view_->undoStack()->push(new edit::MoveModuleCommand(*view_->currentMap(), id, QPointF(64, 32)));
    const QPointF where = edit::placedParts(*view_->currentMap(), mod().memberIds).front().displayArea.topLeft();

    ownerSaves(2, *sampleModule(3));
    ASSERT_TRUE(waitFor([&] { return bar()->updatesShown(); }));
    EXPECT_EQ(bar()->updatesText(), QStringLiteral("“Harbor crane” has a new version from Alice (v2)."));
    EXPECT_TRUE(panelLine().contains(QStringLiteral("v2 is ready"))) << panelLine().toStdString();
    ASSERT_FALSE(bar()->updateButton()->isHidden());

    bar()->updateButton()->click();
    ASSERT_TRUE(waitFor([&] { return mod().libraryVersion == 2; }));
    EXPECT_EQ(bricksInMap(), 4);
    EXPECT_EQ(mod().id, id);
    EXPECT_TRUE(core::isSharedModule(mod())) << "still locked";
    // Where the module sat: its first part hasn't moved.
    bool there = false;
    for (const auto& b : edit::placedParts(*view_->currentMap(), mod().memberIds))
        there = there || (std::abs(b.displayArea.x() - where.x()) < 0.01 && std::abs(b.displayArea.y() - where.y()) < 0.01);
    EXPECT_TRUE(there);
    EXPECT_FALSE(bar()->updatesShown());
    ASSERT_TRUE(bar()->reviewShown());
    EXPECT_TRUE(bar()->reviewText().startsWith(QStringLiteral("“Harbor crane” is now version 2."))) << bar()->reviewText().toStdString();
    EXPECT_EQ(bar()->goBackButton()->text(), QStringLiteral("Go back to version 1"));

    // Go back: the earlier version, the same way.
    bar()->goBackButton()->click();
    ASSERT_TRUE(waitFor([&] { return mod().libraryVersion == 1; }));
    EXPECT_EQ(bricksInMap(), 3);
    EXPECT_TRUE(bar()->reviewText().startsWith(QStringLiteral("“Harbor crane” is now version 1.")));
    // OK: the notice is read.
    bar()->okButton()->click();
    EXPECT_FALSE(bar()->reviewShown());
    EXPECT_FALSE(core::sharedPlaced(mod())->review);
    // Version 2 is still there to take: offered again.
    ASSERT_TRUE(waitFor([&] { return bar()->updatesShown(); }));
    // Not now: quiet until a newer version.
    bar()->notNowButton()->click();
    EXPECT_FALSE(bar()->updatesShown());

    // In a file an update is the person's own step: Undo takes it back.
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("shared-update"));
    ASSERT_TRUE(waitFor([&] { return mod().libraryVersion == 2; }));
    view_->undoStack()->undo();
    EXPECT_EQ(mod().libraryVersion, 1);
    EXPECT_EQ(bricksInMap(), 3);
}

// A version the server no longer keeps is "no longer available": said once, never asked for in a loop.
TEST_F(SharedModulesWindow, AVersionThatIsGoneIsAskedForOnce) {
    placeShared();
    const QString id = mod().id;
    serveShared(http_, 2);  // (version 2 is listed, its contents aren't there: 404)
    library_->refresh();
    ASSERT_TRUE(waitFor([&] { return bar()->updatesShown(); }));
    const QByteArray path = ("/api/modules/" + kShared + "/versions/2/snapshot").toUtf8();
    bar()->updateButton()->click();
    ASSERT_TRUE(waitFor([&] { return notices()->isShown(QStringLiteral("shared-step")); }));
    EXPECT_EQ(count(http_, "GET", path), 1);
    EXPECT_EQ(mod().libraryVersion, 1);
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_EQ(count(http_, "GET", path), 1) << "not asked again by itself";
    EXPECT_FALSE(bar()->updateButton()->isHidden());
    EXPECT_TRUE(bar()->updateButton()->isEnabled()) << "the person can try again";
}

// Opened from a file that another app changed: the copy says so, and the
// owner's version can be put back.
TEST_F(SharedModulesWindow, ACopyChangedOutsideTheAppOffersToPutTheOwnersVersionBack) {
    placeShared();
    const QString id = mod().id;
    // Saved, changed in another app (a part of the module moved), opened again.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("changed-outside.bld-layout"));
    {
        core::Map& now = *view_->currentMap();
        std::vector<QRectF> was;
        for (auto& L : now.layers())
            for (auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                was.push_back(b.displayArea);
                if (b.partNumber == QStringLiteral("3811.2")) b.displayArea.translate(48, 0);
            }
        QString error;
        ASSERT_TRUE(import::writeLayoutFile(now, path, &error)) << error.toStdString();
    }
    const int asked = count(http_, "GET", ("/api/modules/" + kShared + "/versions/1/snapshot").toUtf8());
    view_->undoStack()->setClean();
    ASSERT_TRUE(window_->openFile(path));
    ASSERT_TRUE(core::isSharedModule(mod())) << "the file keeps the lock";
    const QString drift = QStringLiteral("shared-drift:") + id;
    ASSERT_TRUE(waitFor([&] { return notices()->isShown(drift); }));
    EXPECT_EQ(count(http_, "GET", ("/api/modules/" + kShared + "/versions/1/snapshot").toUtf8()), asked + 1);
    EXPECT_TRUE(panelLine().contains(QStringLiteral("Changed outside the app"))) << panelLine().toStdString();

    // Put back: the owner's version, where the module sits.
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("shared-putback"));
    ASSERT_TRUE(waitFor([&] { return !notices()->isShown(drift); }));
    auto version = sampleModule(2);
    parts::placement::fixStaleAreas(*version, bundledParts());
    EXPECT_TRUE(edit::matchesVersion(edit::batchesOfModule(*version), edit::placedParts(*view_->currentMap(), mod().memberIds),
                                     bundledParts(), core::sharedPlaced(mod())->placement));
    EXPECT_FALSE(core::sharedPlaced(mod())->review) << "the same version put back isn't news";
    EXPECT_FALSE(panelLine().contains(QStringLiteral("Changed outside the app")));
    EXPECT_EQ(mod().libraryVersion, 1);
    view_->undoStack()->setClean();
}

// Make it mine: it asks first; the copy becomes an ordinary module of the
// layout, and (in a file) Undo locks it again.
TEST_F(SharedModulesWindow, MakeItMineUnlocksTheCopyAfterAsking) {
    placeShared();
    const QString id = mod().id;
    ui::ConfirmOptions asked;
    window_->confirmModuleLibrary_ = [&](const ui::ConfirmOptions& o) {
        asked = o;
        return false;
    };
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("shared-detach"));
    EXPECT_EQ(asked.title, QStringLiteral("Make “Harbor crane” yours?"));
    EXPECT_EQ(asked.removes, QStringLiteral("It stops following Alice: no more updates."));
    EXPECT_EQ(asked.undo, QStringLiteral("You can undo this."));
    EXPECT_TRUE(core::isSharedModule(mod())) << "Cancel changes nothing";

    window_->confirmModuleLibrary_ = [](const ui::ConfirmOptions&) { return true; };
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("shared-detach"));
    EXPECT_FALSE(core::isSharedModule(mod()));
    EXPECT_TRUE(mod().libraryModuleId.isEmpty());
    EXPECT_FALSE(panelLine().contains(QStringLiteral("🔗")));
    // Its parts change like any module's now.
    view_->undoStack()->push(new edit::RenameModuleCommand(*view_->currentMap(), id, QStringLiteral("My crane")));
    EXPECT_EQ(mod().name, QStringLiteral("My crane"));
    view_->undoStack()->undo();
    view_->undoStack()->undo();
    EXPECT_TRUE(core::isSharedModule(mod())) << "Undo puts the lock back";
    EXPECT_TRUE(panelLine().contains(QStringLiteral("🔗 Shared by Alice")));
}

// BLD_REVIEW_SHOTS=<dir>: pictures of shared modules for a PR, light and
// dark (skipped otherwise): the Module library's Shared with me tab (and
// the one list a server without shared modules keeps), the Share dialog,
// stopping a share, a shared module on the map with its badge and the
// update notice, the notice after an update, and a refused change.
TEST_F(SharedModulesWindow, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_REVIEW_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_REVIEW_SHOTS not set";
    QDir().mkpath(out);
    // English's plurals, as the app loads them ("2 parts", not "2 part(s)").
    QTranslator english;
    if (english.load(QStringLiteral("bld_en"), QCoreApplication::applicationDirPath() + QStringLiteral("/../..")))
        QCoreApplication::installTranslator(&english);
    ui::theme::PrefsStore store(QStringLiteral("review-shared-modules-shots"));
    ui::theme::ThemeManager themer(store);
    const auto settle = [] { waitFor([] { return false; }, 400); };
    const auto shoot = [&](QWidget* w, const QString& name) {
        for (const auto mode : { ui::theme::ThemeChoice::Light, ui::theme::ThemeChoice::Dark }) {
            ui::theme::AppPrefs p = store.prefs();
            p.theme = mode;
            store.update(p);
            settle();
            EXPECT_TRUE(w->grab().save(out + QStringLiteral("/%1-%2.png").arg(name, mode == ui::theme::ThemeChoice::Dark ? QStringLiteral("dark") : QStringLiteral("light"))));
        }
    };
    window_->resize(1320, 820);
    window_->show();
    auto* panel = window_->findChild<ui::ModuleLibraryPanel*>();
    ASSERT_NE(panel, nullptr);
    panel->tabs()->setCurrentIndex(1);
    auto* tab = window_->findChild<ui::ServerModulesTab*>();
    ASSERT_NE(tab, nullptr);

    // The Module library and Modules panels, in view.
    const auto dockOf = [](QWidget* inside) -> QWidget* {
        for (QWidget* w = inside; w; w = w->parentWidget())
            if (auto* dock = qobject_cast<QDockWidget*>(w)) {
                dock->setFloating(true);
                dock->resize(460, 420);
                dock->show();
                dock->raise();
                return dock;
            }
        return inside;
    };
    QWidget* libraryDock = dockOf(panel);
    QWidget* modulesDock = dockOf(window_->findChild<ui::ModulesPanel*>());
    settle();

    // Before: a server without shared modules keeps its one list (as on main).
    library_->setKnowsSharedModules(false);
    settle();
    shoot(libraryDock, QStringLiteral("before-library"));
    library_->setKnowsSharedModules(std::nullopt);
    library_->refresh();
    ASSERT_TRUE(waitFor([&] { return tab->tabs()->isVisibleTo(tab) && library_->sharedModules().size() == 2; }));
    shoot(libraryDock, QStringLiteral("after-library-mine"));
    tab->tabs()->setCurrentIndex(ui::ServerModulesTab::Shared);
    shoot(libraryDock, QStringLiteral("after-library-shared-with-me"));

    // The Share dialog, shared with a club that uses it in three layouts.
    const QJsonObject club{ { QStringLiteral("orgId"), QStringLiteral("o1") }, { QStringLiteral("name"), QStringLiteral("Train Club") },
                            { QStringLiteral("slug"), QStringLiteral("train") } };
    http_.reply("/api/modules/m1/shares", 200, sharesJson({ club }, 3));
    tab->tabs()->setCurrentIndex(ui::ServerModulesTab::Mine);
    settle();
    tab->row(QStringLiteral("m1"))->findChild<QAction*>(QStringLiteral("shareModule"))->trigger();
    ui::ModuleShareDialog* share = tab->shareDialog();
    ASSERT_NE(share, nullptr);
    ASSERT_TRUE(waitFor([&] { return share->loaded(); }));
    shoot(share, QStringLiteral("after-share-dialog"));
    {
        ui::ConfirmDialog stop(ui::stopShareOptions(QStringLiteral("Freight yard"), QStringLiteral("Train Club"), 3, 7));
        stop.show();
        shoot(&stop, QStringLiteral("after-stop-sharing"));
    }
    share->close();

    // Placed: the badge and the dashed frame on the map, "Shared by" in the Modules panel.
    tab->tabs()->setCurrentIndex(ui::ServerModulesTab::Shared);
    placeShared();
    notices()->hideNotice(QStringLiteral("shared-tip"));
    view_->scene()->clearSelection();
    view_->fitInView(view_->scene()->itemsBoundingRect().adjusted(-200, -200, 200, 200), Qt::KeepAspectRatio);
    shoot(window_.get(), QStringLiteral("after-placed"));

    // Its owner saved version 2: the notice, and "v2 is ready".
    ownerSaves(2, *sampleModule(3));
    ASSERT_TRUE(waitFor([&] { return bar()->updatesShown(); }));
    shoot(window_.get(), QStringLiteral("after-update-notice"));
    shoot(modulesDock, QStringLiteral("after-modules-panel"));
    bar()->updateButton()->click();
    ASSERT_TRUE(waitFor([&] { return mod().libraryVersion == 2 && bar()->reviewShown(); }));
    view_->scene()->clearSelection();
    shoot(window_.get(), QStringLiteral("after-updated"));
    bar()->okButton()->click();

    // A change to its parts is refused, with the reason.
    view_->undoStack()->push(new edit::RenameModuleCommand(*view_->currentMap(), mod().id, QStringLiteral("My crane")));
    ASSERT_TRUE(notices()->isShown(QStringLiteral("shared-lock")));
    shoot(window_.get(), QStringLiteral("after-locked"));

    // No part drew as a placeholder.
    int missing = 0;
    for (const auto& L : view_->currentMap()->layers())
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (bundledParts().pixmap(b.partNumber.toLower()).isNull()) ++missing;
    EXPECT_EQ(missing, 0);
    view_->undoStack()->setClean();
}
