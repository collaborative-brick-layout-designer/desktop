// The Parts list's tooltip: who built an imported model (as the web's parts
// panel shows it) and which of your servers a server part came from.

#include "ui/PartsBrowser.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>

using namespace bld;

TEST(PartsTooltip, ALocalPartShowsItsDescriptionAndKey) {
    parts::PartMetadata meta;
    meta.xmlFilePath = QStringLiteral("/home/me/parts/Bricks/3001.1.xml");
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("3001.1"), QStringLiteral("Brick 2 x 4"), {}),
              QStringLiteral("Brick 2 x 4\n(3001.1)"));
}

TEST(PartsTooltip, NamesWhoBuiltAnImportedModel) {
    parts::PartMetadata meta;
    meta.designer = QStringLiteral("Sam Builder");
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("MARKET"), QString(), {}),
              QStringLiteral("MARKET\nDesigned by Sam Builder"));
}

TEST(PartsTooltip, NamesTheServerAServerPartCameFrom) {
    parts::PartMetadata meta;
    meta.xmlFilePath = QStringLiteral("/data/BLD/server-parts/bricks.example.org_8443/custom/X1.xml");
    const QHash<QString, QString> labels{ { QStringLiteral("bricks.example.org_8443"), QStringLiteral("Club server") } };
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("X1"), QString(), labels),
              QStringLiteral("X1\nFrom the server Club server"));
    // A server no longer in the list: its folder name.
    EXPECT_EQ(ui::partTooltip(meta, QStringLiteral("X1"), QString(), {}),
              QStringLiteral("X1\nFrom the server bricks.example.org_8443"));
}

namespace {
void writePart(const QString& dir, const QString& name) {
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + name + QStringLiteral(".xml"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("<part><Author>t</Author></part>");
    f.close();
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::red);
    ASSERT_TRUE(img.save(dir + QLatin1Char('/') + name + QStringLiteral(".gif.png"), "PNG")
                || img.save(dir + QLatin1Char('/') + name + QStringLiteral(".png"), "PNG"));
}
int listed(ui::PartsBrowser& b) {
    auto* grid = b.findChild<QListWidget*>();
    return grid ? grid->count() : -1;
}
// The part's item in the grid (its tooltip starts with the key, as these parts have no description).
QListWidgetItem* itemOf(ui::PartsBrowser& b, const QString& key) {
    auto* grid = b.findChild<QListWidget*>();
    for (int i = 0; grid && i < grid->count(); ++i)
        if (grid->item(i)->toolTip().startsWith(key, Qt::CaseInsensitive)) return grid->item(i);
    return nullptr;
}
bool canDrag(const QListWidgetItem* item) { return item && (item->flags() & Qt::ItemIsDragEnabled); }
}  // namespace

// Live on the club's server: a part only the friend's server has is listed
// grayed, says why, and can't be dragged out. The club's parts, your own, and a
// part both servers have are as ever. Not live, nothing is grayed.
TEST(PartsBrowserServers, LiveOnAServerAnotherServersPartsAreGrayedAndSayWhy) {
    QTemporaryDir root;
    const QString base = root.path() + QStringLiteral("/server-parts");
    writePart(base + QStringLiteral("/club.example/custom"), QStringLiteral("CLUBPART"));
    writePart(base + QStringLiteral("/friend.example/custom"), QStringLiteral("FRIENDPART"));
    // Both servers have this one; the library shows the friend's copy (read first).
    writePart(base + QStringLiteral("/club.example/custom"), QStringLiteral("SHARED"));
    writePart(base + QStringLiteral("/friend.example/custom"), QStringLiteral("SHARED"));
    writePart(root.path() + QStringLiteral("/imports"), QStringLiteral("MINE"));
    parts::PartsLibrary lib;
    lib.addSearchPath(base + QStringLiteral("/friend.example"));
    lib.addSearchPath(base + QStringLiteral("/club.example"));
    lib.addSearchPath(root.path() + QStringLiteral("/imports"));
    lib.scan();
    ASSERT_EQ(lib.keys().size(), 4);
    ASSERT_TRUE(lib.metadata(QStringLiteral("SHARED"))->xmlFilePath.contains(QStringLiteral("friend.example")));
    EXPECT_EQ(lib.otherCopies(QStringLiteral("SHARED")).size(), 1);
    EXPECT_TRUE(lib.otherCopies(QStringLiteral("MINE")).isEmpty());

    QSettings().remove(QStringLiteral("parts/showAllServers"));
    ui::PartsBrowser b(lib);
    b.rebuild();
    for (const char* key : { "CLUBPART", "FRIENDPART", "SHARED", "MINE" }) {
        ASSERT_NE(itemOf(b, QString::fromLatin1(key)), nullptr) << key;
        EXPECT_TRUE(canDrag(itemOf(b, QString::fromLatin1(key)))) << key;
    }

    b.setLiveServerFolder(QStringLiteral("club.example"));
    EXPECT_EQ(listed(b), 4);
    QListWidgetItem* friends = itemOf(b, QStringLiteral("FRIENDPART"));
    ASSERT_NE(friends, nullptr);
    EXPECT_FALSE(canDrag(friends));
    EXPECT_TRUE(friends->toolTip().endsWith(QStringLiteral(
        "This part is on another server (friend.example). It can’t go in this live layout, because nobody else here "
        "could see it.")))
        << friends->toolTip().toStdString();
    EXPECT_EQ(friends->foreground().color(), QApplication::palette().color(QPalette::Disabled, QPalette::Text));
    for (const char* key : { "CLUBPART", "SHARED", "MINE" }) {
        QListWidgetItem* item = itemOf(b, QString::fromLatin1(key));
        ASSERT_NE(item, nullptr) << key;
        EXPECT_TRUE(canDrag(item)) << key;
        EXPECT_FALSE(item->toolTip().contains(QStringLiteral("another server"))) << key;
    }
    EXPECT_EQ(ui::otherServerOf(lib, QStringLiteral("FRIENDPART"), QStringLiteral("club.example")), QStringLiteral("friend.example"));
    EXPECT_TRUE(ui::otherServerOf(lib, QStringLiteral("SHARED"), QStringLiteral("club.example")).isEmpty());
    EXPECT_TRUE(ui::otherServerOf(lib, QStringLiteral("MINE"), QStringLiteral("club.example")).isEmpty());
    EXPECT_TRUE(ui::otherServerOf(lib, QStringLiteral("FRIENDPART"), QString()).isEmpty());

    // Live on the friend's instead: the club's own part is the grayed one.
    b.setLiveServerFolder(QStringLiteral("friend.example"));
    EXPECT_FALSE(canDrag(itemOf(b, QStringLiteral("CLUBPART"))));
    EXPECT_TRUE(canDrag(itemOf(b, QStringLiteral("FRIENDPART"))));
    // No longer live: nothing is grayed.
    b.setLiveServerFolder(QString());
    EXPECT_TRUE(canDrag(itemOf(b, QStringLiteral("CLUBPART"))));
    EXPECT_FALSE(itemOf(b, QStringLiteral("CLUBPART"))->toolTip().contains(QStringLiteral("another server")));
}

TEST(PartsBrowserServers, LiveOnAServerOtherServersPartsAreListedUnlessTheSettingLeavesThemOut) {
    QTemporaryDir root;
    const QString base = root.path() + QStringLiteral("/server-parts");
    writePart(base + QStringLiteral("/club.example/custom"), QStringLiteral("CLUBPART"));
    writePart(base + QStringLiteral("/friend.example/custom"), QStringLiteral("FRIENDPART"));
    writePart(root.path() + QStringLiteral("/imports"), QStringLiteral("MINE"));
    parts::PartsLibrary lib;
    lib.addSearchPath(base + QStringLiteral("/club.example"));
    lib.addSearchPath(base + QStringLiteral("/friend.example"));
    lib.addSearchPath(root.path() + QStringLiteral("/imports"));
    lib.scan();
    ASSERT_EQ(lib.keys().size(), 3);

    QSettings().setValue(QStringLiteral("parts/showAllServers"), false);
    ui::PartsBrowser b(lib);
    b.rebuild();
    EXPECT_EQ(listed(b), 3);  // not live: everything
    b.setLiveServerFolder(QStringLiteral("club.example"));
    EXPECT_EQ(listed(b), 2);  // the club's part and your own, not the friend's server's
    QSettings().setValue(QStringLiteral("parts/showAllServers"), true);
    b.rebuild();
    EXPECT_EQ(listed(b), 3);
    // Never set: they are listed (grayed).
    QSettings().remove(QStringLiteral("parts/showAllServers"));
    b.rebuild();
    EXPECT_EQ(listed(b), 3);
    b.setLiveServerFolder(QString());
    EXPECT_EQ(listed(b), 3);
}
