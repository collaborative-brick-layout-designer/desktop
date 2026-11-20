#pragma once

// The server's modules, catalog and notices, for the desktop's Module
// library (the web's ModuleLibraryPanel, SaveModuleDialog, Catalog page and
// notice banner): your modules and your clubs' (GET /api/modules), a
// module's contents as a shared document (snapshot), saving one as a new
// version with a "What changed" note, its picture, renaming and deleting;
// the public catalog's modules, parts and collections, and adding them to
// you or a club; and the warnings you haven't acknowledged yet.
//
// Every call answers through a callback, or `failed` with what the server
// said (ServerRefusal: a 403 says why). A destroyed LibraryApi never calls
// back. The token rides in the Authorization header only.

#include "Credit.h"
#include "ServerApi.h"
#include "ServerRefusal.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <optional>

class QNetworkReply;

namespace bld::sync {

// Who owns a module: you ("user") or a club ("org").
struct OwnerTag {
    QString kind;  // user / org; empty when the server didn't say
    QString id;
    QString name;
    QString slug;  // a club's slug; empty for a person
    bool isClub() const { return kind == QLatin1String("org"); }
};

// A club, as the sharing answers name it.
struct ClubRef {
    QString id;
    QString name;
    QString slug;
    int layouts = 0;  // "Used in": how many of its layouts place the module
    // How another club came to have a club's module (docs/shared-modules.md,
    // section 11): "partner" (a partner club), "offer" (accepted one off), or
    // empty (shared with a club its owner is in).
    QString via;
};

// A club's module offered one off to another club, waiting for that club's
// Admins or Managers, or declined. A club that isn't listed has no name
// (only its address) until it accepts.
struct ClubOffer {
    QString id;
    ClubRef club;
    QString state;          // "pending" / "declined"
    qint64 askAgainAt = 0;  // a declined one: when it can be offered again (ms); 0: now
    bool pending() const { return state == QLatin1String("pending"); }
};

// A club found to share a club's module with (GET /api/modules/:id/club-targets).
struct ClubTarget {
    ClubRef club;
    bool partner = false;  // a partner club: shared at once
    bool shared = false;   // already shared with it
    QString offer;         // "pending" / "declined" / empty
};

// What sharing a club's module answered: shared at once (a partner), or offered.
struct ClubShareResult {
    QString status;  // "shared" / "offered"
    QString name;    // empty: a club that isn't listed
    QString slug;
    bool offered() const { return status == QLatin1String("offered"); }
};

// Partner requests and module offers waiting in one club you look after.
struct ClubRequestCount {
    QString slug;
    QString name;
    int count = 0;
};

// An engine or car on your roster or a club's (GET /api/rolling-stock;
// trains design 2.6): `record` is what a layout keeps, linked back by its
// libraryId (this id) and libraryVersion (this version).
struct RosterStock {
    QString id;
    QString number;
    QString name;
    int version = 1;
    bool canManage = false;  // may change or delete it (yours, or a club where you may edit modules)
    OwnerTag owner;
    QJsonObject record;
};

struct ServerModule {
    QString id;
    QString title;
    QString role;  // owner / editor / viewer
    OwnerTag owner;
    int docVersion = 0;
    int latestVersion = 0;  // the newest saved version; 0: no history yet
    bool empty = false;     // no parts yet: adding it would place nothing (older servers leave it out)
    qint64 thumbnailAt = 0;  // when its picture was made (ms); 0: none yet
    QDateTime updatedAt;
    Credit credit; // who made it; Take back / Give back
    // Someone else's module shared with you (GET /api/modules?owner=shared):
    // with you in person, or with clubs you're in.
    bool sharedWithMe = false;
    bool sharedDirect = false;
    QList<ClubRef> sharedClubs;
    // "Alice · via Riverside LUG", "Alice · shared with you" (the web's sharedVia line).
    QString sharedViaText() const;

    bool canEdit() const { return role == QLatin1String("owner") || role == QLatin1String("editor"); }
    bool canDelete() const { return role == QLatin1String("owner"); }
    // "v3": the saved version, else the document's, and never below 1 (a copy made before
    // it had a first version, or one never saved, is version 1).
    int shownVersion() const { return std::max(1, latestVersion > 0 ? latestVersion : docVersion); }
    // Where its picture is (the cache key changes with each new picture).
    QString thumbnailPath() const;
};

// What a module's owner sees about its sharing (GET /api/modules/:id/shares).
struct ModuleShares {
    int latestVersion = 0;
    bool canShareWithClubs = true;  // false for a club's own module: its members already see it
    // A club's module: it goes to the club's partner clubs at once, or is
    // offered to any other club (servers with partner clubs).
    bool canShareWithOtherClubs = false;
    ClubRef club;               // a club's module: its club
    QList<ClubRef> partners;    // a club's module: the club's partner clubs
    QList<ClubOffer> offers;    // a club's module: offers waiting or declined
    QList<ClubRef> clubs;           // shared with these
    QList<ClubRef> myClubs;         // the clubs you're in (it can be shared with those)
    int people = 0;                 // how many people it is shared with
    // Where it's placed: counts by club and by person, never a layout's name.
    struct UsedIn {
        int layouts = 0;
        QList<ClubRef> clubs;
        QList<ClubRef> people;  // (name and layouts only)
        int others = 0;
    } usedIn;
    // "Used in 4 layouts (Riverside LUG: 3, Bob: 1)", or empty when it's placed nowhere.
    QString usedInText() const;
    int layoutsOf(const QString& orgId) const;
};

// One shared module a layout places, as the server sees it
// (GET /api/layouts/:id/shared-modules).
struct LayoutSharedModule {
    QString placedId;   // the module's id in the layout
    QString moduleId;
    QString state;      // shared / unshared (no longer shared with the layout's owner) / gone (deleted)
    int version = 0;    // the version placed; 0 when the layout doesn't say
    QString title;      // its name now; empty once it's no longer shared
    QString ownerName;
    int latest = 0;     // the module's newest version; 0: not known
    QString latestNote; // its "What changed?" note
    bool following() const { return state == QLatin1String("shared"); }
};

// Before placing a shared module: is it shared with this layout's owner too?
struct SharedPlaceCheck {
    QString moduleId;
    QString state;
    QString title;
    struct Owner {
        QString kind;  // user / org
        QString name;
        bool mine = false;
    } moduleOwner, layoutOwner;
};

struct LayoutSharedModules {
    // Updates from shared modules: "ask" (the notice; someone presses Update)
    // or "auto" (the server puts each new version in). Older servers: "ask".
    QString updates = QStringLiteral("ask");
    bool canUpdate = false;  // you may press Update and Go back (you can edit the layout)
    bool canDetach = false;  // you may "Make it mine" (the layout's owner; a club's admins and managers)
    QList<LayoutSharedModule> modules;
    std::optional<SharedPlaceCheck> check;
    const LayoutSharedModule* row(const QString& placedId) const;
};

struct SharedModuleApplied {
    QString placedId;
    bool changed = false;  // false when it already had that version
    int version = 0;
    int from = 0;          // 0: not known
    int parts = 0;
};

// A shared module the server updated automatically since you last had the
// layout open ("Updated while you were away").
struct SharedAwayUpdate {
    QString placedId;
    QString moduleId;
    int from = 0;     // the version it had before (0: not known)
    int to = 0;       // the version the last automatic update put in
    int version = 0;  // the version it has now
    QString title;    // while it is still shared with the layout
};

// One kept version of a module, seen through a layout.
struct SharedModuleVersion {
    int version = 0;
    QString note;
    QString author;
    QDateTime createdAt;
};

// One of your custom parts, or your clubs' (GET /api/custom-parts).
struct ServerPart {
    QString id;
    QString partNumber;   // its number in a layout, and its file's name in the parts library
    QString displayName;
    QString role;         // owner / editor / viewer
    OwnerTag owner;
    QDateTime updatedAt;
    Credit credit;
    bool hasSprite = false;  // the server has its picture

    QString name() const { return displayName.isEmpty() ? partNumber : displayName; }
    // Its picture on the server, or empty.
    QString spritePath() const;
};

struct CatalogSettings {
    bool modules = false;  // the module catalog is on
    bool parts = false;    // the parts catalog is on
    bool layouts = false;  // public layouts are on (servers before them: off)
    bool venues = false;   // public venues are on (servers before them: off)
    bool review = true;    // a moderator looks at what's shared first
    bool on(const QString& kind) const {
        return kind == QLatin1String("module") ? modules : kind == QLatin1String("part") ? parts
             : kind == QLatin1String("layout") ? layouts : kind == QLatin1String("venue") ? venues : false;
    }
};

struct CatalogItem {
    QString id;
    QString kind;  // module / part / layout / venue
    QString title;
    QString description;
    QString by;  // who shared it
    int uses = 0;
    int version = 0;
    QString previewPath;  // the drawn picture, a path on the server
    QString coverPath;    // the card's picture: its owner's own, else the drawn one
    // A layout or venue: its size in studs (0: unknown), a layout's part count (-1: none given).
    int widthStuds = 0;
    int heightStuds = 0;
    int partCount = -1;

    // The picture to show: the cover when the server sends one.
    QString picturePath() const { return coverPath.isEmpty() ? previewPath : coverPath; }
};

struct CatalogCollection {
    QString id;
    QString title;
    QString description;
    QString by;
    bool featured = false;
    bool official = false;
    int itemCount = 0;
    int modules = 0;
    int parts = 0;
    QString coverPath;  // empty: no picture
};

// Something of yours (or your club's) shared to the public catalog (GET /api/catalog/mine).
struct MyCatalogItem {
    QString id;
    QString kind;      // module / part / layout / venue
    QString sourceId;  // the module, part, layout or venue it was shared from
    QString title;
    QString status;    // in_review / public / declined / unpublished / withdrawn
    QString reason;
    int pendingVersion = 0;  // an update waiting for review; 0: none
};

// What a share to the catalog sends (the web's ShareToCatalogDialog).
struct CatalogShare {
    QString kind;
    QString sourceId;
    QString title;
    QString description;   // empty and `update`: kept as it was
    QStringList tags;
    QString note;          // "What changed?" for an update
    QByteArray thumbnailPng;  // a layout's picture for its card; empty: none
    bool update = false;
};

struct CollectionAddResult {
    int added = 0;
    int skipped = 0;  // you (or the club) had them already
    int failed = 0;
};

// A warning sent to you (or a club you run), as the web's notice banner shows it.
struct Notice {
    QString id;
    QString scope;     // site / club
    QString severity;  // note / warning / final
    QString reason;
    QString link;      // a page on the server, or empty
    QDateTime createdAt;
    bool acknowledged = false;
    QString clubName;  // a club warning: the club it's from
    QString toClub;    // sent to a club you run: its name
};

// One of a club's roles, as its members list names it.
struct ClubRoleName {
    QString id;
    QString name;
    QString key;             // admin / manager / member / viewer, or empty for a club's own role
    QStringList permissions; // what the role allows (servers before club roles: empty)
};

// A member of a club, as the server shows them to you (GET
// /api/orgs/:slug/members): their name, the roles they hold (several, in
// the club's order) and roughly when they were last active, which the
// server sends only to those who may see it (empty otherwise).
struct ClubMember {
    QString userId;
    QString name;
    QStringList roleIds;
    QString lastActive; // today / week / month / older, or empty
    QDateTime joinedAt;
};

struct ClubMembers {
    QList<ClubRoleName> roles; // the club's roles, in its order
    QList<ClubMember> members;
    // The names of the roles a member holds, in the club's order.
    QStringList roleNames(const ClubMember& m) const;
    // Strongest first, as the website's member list (byStrongestRole): anyone
    // holding the Admin role, then by how many permissions their roles grant
    // together, then by name.
    QList<ClubMember> sorted() const;
};

class LibraryApi : public QObject {
    Q_OBJECT
public:
    using Fail = std::function<void(const ServerRefusal&)>;

    explicit LibraryApi(QObject* parent = nullptr);

    void setBase(const QUrl& base) { base_ = base; }
    QUrl base() const { return base_; }
    void setToken(const QString& token) { token_ = token; }
    QString token() const { return token_; }

    // A module's page on the website (the web's module editor).
    QUrl moduleWebUrl(const QString& id) const;
    // A server path (a picture) as a full address.
    QUrl url(const QString& pathAndQuery) const;

    void listModules(std::function<void(const QList<ServerModule>&)> done, Fail failed);
    // Other people's modules shared with you, in person or through a club
    // ("Shared with me"; servers with the sharedModules feature).
    void sharedWithMe(std::function<void(const QList<ServerModule>&)> done, Fail failed);
    // For its owner: the clubs it's shared with, the clubs it could be, and where it's used.
    void moduleShares(const QString& id, std::function<void(const ModuleShares&)> done, Fail failed);
    // Share it, read-only, with a club you're in / stop (its owner, or that club's admins and managers).
    void shareWithClub(const QString& id, const QString& orgSlug, std::function<void()> done, Fail failed);
    void stopClubShare(const QString& id, const QString& orgId, std::function<void()> done, Fail failed);
    // A club's module to another club, by its address or a pasted link:
    // shared at once with a partner club, otherwise offered.
    void shareClubModule(const QString& id, const QString& clubSlugOrLink, std::function<void(const ClubShareResult&)> done,
                         Fail failed);
    // Take back an offer of a club's module (or forget a declined one).
    void withdrawClubOffer(const QString& id, const QString& offerId, std::function<void()> done, Fail failed);
    // A club's module: listed clubs matching `q`, to share it with or offer it to.
    void clubTargets(const QString& id, const QString& q, std::function<void(const QList<ClubTarget>&)> done, Fail failed);
    // Partner requests and module offers waiting in the clubs you look after
    // (their Admins and Managers answer them on the website).
    void clubRequestCount(std::function<void(const QList<ClubRequestCount>&)> done, Fail failed);
    // A club's Partner clubs section on the website, where requests and offers are answered.
    QUrl clubPartnersWebUrl(const QString& slug) const;
    // A layout's shared modules. `checkModuleId` (optional): also whether that
    // module would follow its owner if placed in this layout.
    void layoutSharedModules(const QString& layoutId, const QString& checkModuleId,
                             std::function<void(const LayoutSharedModules&)> done, Fail failed);
    // A placed module's kept versions, and one version's contents, seen through
    // the layout (no share of your own needed).
    void sharedModuleVersions(const QString& layoutId, const QString& placedId,
                              std::function<void(const QList<SharedModuleVersion>&)> done, Fail failed);
    void sharedModuleVersionSnapshot(const QString& layoutId, const QString& placedId, int version,
                                     std::function<void(const QByteArray&)> done, Fail failed);
    // The server puts a version in (`version` 0: the newest): Update for a
    // newer one, Go back for an older one; `again` puts the same version back
    // over a copy that was changed outside the app. Everyone in the layout
    // gets the change; pressing it twice changes nothing more.
    void applySharedModule(const QString& layoutId, const QString& placedId, int version, bool again,
                           std::function<void(const SharedModuleApplied&)> done, Fail failed);
    // Update all: how many were updated, and how many couldn't be.
    void applyAllSharedModules(const QString& layoutId, std::function<void(int updated, int skipped)> done, Fail failed);
    // The layout setting "Updates from shared modules": "ask" or "auto" (layout editors).
    void setSharedModuleUpdates(const QString& layoutId, const QString& updates, std::function<void(const QString& now)> done,
                                Fail failed);
    // What the server updated automatically since you last had the layout
    // open. Asked once on opening: the server then notes that you have it
    // open, so the same updates aren't listed again.
    void sharedModulesWhileAway(const QString& layoutId, std::function<void(const QList<SharedAwayUpdate>&)> done, Fail failed);
    // "Make it mine": the copy becomes an ordinary module of the layout.
    void detachSharedModule(const QString& layoutId, const QString& placedId, std::function<void(bool changed)> done,
                            Fail failed);
    // The module's contents: a shared document (Yjs v1 update).
    void moduleSnapshot(const QString& id, std::function<void(const QByteArray&)> done, Fail failed);
    // The same, with which save it is (its docVersion, X-Doc-Version; -1 from a server that doesn't say):
    // the next save names it (If-Match), so it can't overwrite someone else's.
    void moduleSnapshotAt(const QString& id, std::function<void(const QByteArray&, int docVersion)> done,
                          Fail failed);
    // One saved version's contents (Update from library: was it changed here?).
    void moduleVersionSnapshot(const QString& id, int version, std::function<void(const QByteArray&)> done, Fail failed);
    // A new, empty module of yours (orgSlug empty) or a club's.
    void createModule(const QString& title, const QString& orgSlug,
                      std::function<void(const QString& id, const QString& title)> done, Fail failed);
    // Save its contents: each save is a new version, with an optional note.
    // `baseDocVersion` (>= 0): the docVersion this started from; someone else's
    // save since then is refused (409 module_changed) instead of overwritten.
    void saveModuleSnapshot(const QString& id, const QByteArray& snapshot, const QString& note,
                            std::function<void(int version)> done, Fail failed, int baseDocVersion = -1,
                            std::function<void(int docVersion)> savedAs = {});
    void setModuleThumbnail(const QString& id, const QByteArray& png, std::function<void()> done, Fail failed);
    void renameModule(const QString& id, const QString& title, std::function<void()> done, Fail failed);
    void deleteModule(const QString& id, std::function<void()> done, Fail failed);
    // A club's module (`kindPath` "modules", or "layouts", "venues", "custom-parts")
    // back to its author: taken back by them (`give` false) or given back by
    // the club's admins and managers. The club keeps a copy: its id.
    void returnToAuthor(const QString& kindPath, const QString& id, bool give,
                        std::function<void(const QString& keptCopyId)> done, Fail failed);
    // Any picture on the server (a module's thumbnail, a catalog preview).
    void picture(const QString& pathAndQuery, std::function<void(const QByteArray&)> done, Fail failed);

    void catalogSettings(std::function<void(const CatalogSettings&)> done, Fail failed);
    // kind: module / part; `query` searches titles, descriptions and tags.
    void catalogItems(const QString& kind, const QString& query, std::function<void(const QList<CatalogItem>&)> done,
                      Fail failed);
    // A copy of the item for you (orgSlug empty) or a club: its new id.
    void addCatalogItem(const QString& itemId, const QString& orgSlug,
                        std::function<void(const QString& kind, const QString& id)> done, Fail failed);
    // A venue of yours (or a club's): its name and its .bld-venue file.
    void venueFile(const QString& id, std::function<void(const QString& name, const QByteArray& file)> done, Fail failed);
    // A catalog item's page on the website (a layout's viewer, a venue's plan).
    QUrl catalogItemWebUrl(const QString& id) const;
    void collections(std::function<void(const QList<CatalogCollection>&)> done, Fail failed);
    void collectionItems(const QString& id, std::function<void(const QList<CatalogItem>&)> done, Fail failed);
    void addCollection(const QString& id, const QString& orgSlug, std::function<void(const CollectionAddResult&)> done,
                       Fail failed);

    // Yours and your clubs' layouts, venues and custom parts, as the web's
    // Home page lists them (the desktop's Server window).
    void layouts(std::function<void(const QList<LayoutEntry>&)> done, Fail failed);
    void venues(std::function<void(const QList<VenueEntry>&)> done, Fail failed);
    // Rolling stock rosters: yours and your clubs' (token scope layouts:read),
    // and saving a car to one (`orgSlug` empty: yours) or changing one
    // (layouts:write). The record goes without the layout's own id.
    void rollingStock(std::function<void(const QList<RosterStock>&)> done, Fail failed);
    void createStock(const QJsonObject& record, const QString& orgSlug, std::function<void(const RosterStock&)> done, Fail failed);
    void updateStock(const QString& id, const QJsonObject& record, std::function<void(const RosterStock&)> done, Fail failed);
    void deleteStock(const QString& id, std::function<void()> done, Fail failed);
    static RosterStock rosterStockFromJson(const QJsonObject& o);
    void customParts(std::function<void(const QList<ServerPart>&)> done, Fail failed);
    // Only your own custom parts (?owner=me): not your clubs', nor ones shared with you.
    void myCustomParts(std::function<void(const QList<ServerPart>&)> done, Fail failed);
    // A new version of the server's custom part `id` (PUT /api/custom-parts/:id): the
    // body PartsUpload::uploadBody() makes. Its owner stays as it is.
    void replaceCustomPart(const QString& id, const QJsonObject& body, std::function<void()> done, Fail failed);
    // Deletes your custom part or set `id` (DELETE /api/custom-parts/:id; servers
    // with deletePartsFromDesktop take the desktop's token for it).
    void deleteCustomPart(const QString& id, std::function<void()> done, Fail failed);
    // A copy of a layout as a file: the whole layout as a .bld-layout
    // (`native`, servers with layoutDownload), else its .bbm.
    void layoutFile(const QString& id, bool native, std::function<void(const QByteArray&)> done, Fail failed);
    void deleteLayout(const QString& id, std::function<void()> done, Fail failed);
    void deleteVenue(const QString& id, std::function<void()> done, Fail failed);

    // Save a venue (its .bld-venue JSON without the schema tag) as yours
    // (orgSlug empty) or a club's: its new id.
    void createVenue(const QString& name, const QJsonObject& data, const QString& orgSlug,
                     std::function<void(const QString& id)> done, Fail failed);
    // Share to the public catalog (or publish an update): the catalog item's id and its status.
    void shareToCatalog(const CatalogShare& share, std::function<void(const QString& itemId, const QString& status)> done,
                        Fail failed);
    void myCatalogItems(std::function<void(const QList<MyCatalogItem>&)> done, Fail failed);

    // Your clubs and your role in each (where you may save a module).
    void orgs(std::function<void(const QList<OrgEntry>&)> done, Fail failed);

    void notices(std::function<void(const QList<Notice>&)> done, Fail failed);

    // A club's members and roles (read only; members are managed on the website).
    void clubMembers(const QString& slug, std::function<void(const ClubMembers&)> done, Fail failed);
    static ClubMembers clubMembersFromJson(const QJsonObject& o);
    void acknowledge(const QString& id, std::function<void()> done, Fail failed);

    // How many requests are still out (tests).
    int pending() const { return pending_; }

    static ServerModule moduleFromJson(const QJsonObject& o);
    static ModuleShares moduleSharesFromJson(const QJsonObject& o);
    static LayoutSharedModules layoutSharedModulesFromJson(const QJsonObject& o);
    static CatalogItem catalogItemFromJson(const QJsonObject& o);
    static Notice noticeFromJson(const QJsonObject& o);
    static ServerPart partFromJson(const QJsonObject& o);

private:
    // `ok` gets the reply when it is a 2xx; `failed` gets the refusal otherwise.
    void send(const QByteArray& verb, const QString& path, const QByteArray& body,
              const QByteArray& contentType, std::function<void(QNetworkReply*)> ok, Fail failed,
              const QList<QPair<QByteArray, QByteArray>>& headers = {});
    void sendJson(const QByteArray& verb, const QString& path, const QJsonObject& body,
                  std::function<void(const QJsonObject&)> ok, Fail failed);

    QNetworkAccessManager net_;
    QUrl base_;
    QString token_;
    int pending_ = 0;
};

// The path segment for an id (percent-encoded).
QString idPath(const QString& id);

}  // namespace bld::sync
