#pragma once

// The server half of the Module library, like the web's
// ModuleLibraryPanel and Catalog page:
// - "On the server": your modules and each club's, with pictures and
//   versions. Each row has "Add to layout" (or "Add to this module" while
//   you change one) and ⋯ (Open to change it, Rename…, Delete…); the module
//   you're changing shows "Editing now".
// - "Catalog": the server's public modules, parts and collections, with
//   "Add and insert", "Add to my modules", "Add to my parts" and "Add all".
//
// ServerLibrary holds what both tabs show (the server, its token, your
// modules, clubs and the catalog settings) and keeps the pictures in a
// cache folder per server. Not signed in, offline, or refused: each tab
// says so in a friendly line with a button (Sign in, Try again).

#include "../sync/LibraryApi.h"
#include "../sync/Notifications.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QDialog>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;

namespace bld::sync { class NotificationCenter; }

namespace bld::ui {

struct NotificationPlace;

class ServerLibrary : public QObject {
    Q_OBJECT
public:
    enum class State {
        NoServer,   // no server added yet
        SignedOut,  // a server, but not signed in there
        Loading,
        Ready,
        Offline,    // the server couldn't be reached
        Refused,    // the server said no (stateText says why)
    };

    explicit ServerLibrary(QObject* parent = nullptr);

    // The server the library shows, what you call it, and its token (empty:
    // not signed in). An invalid url: no server.
    void setServer(const QUrl& url, const QString& label, const QString& token);
    QUrl server() const { return server_; }
    QString label() const { return label_; }
    QString token() const { return api_.token(); }
    sync::LibraryApi& api() { return api_; }

    State state() const { return state_; }
    // What the friendly line says, for any state but Ready.
    QString stateText() const;

    // Asks the server again: your modules, your clubs and the catalog's settings.
    void refresh();
    const QList<sync::ServerModule>& modules() const { return modules_; }
    const QList<sync::OrgEntry>& orgs() const { return orgs_; }
    const sync::CatalogSettings& catalog() const { return catalog_; }
    const sync::ServerModule* module(const QString& id) const;
    // Other people's modules shared with you, in person or through a club
    // ("Shared with me", docs/shared-modules.md); empty on a server without
    // the sharedModules feature.
    const QList<sync::ServerModule>& sharedModules() const { return shared_; }
    const sync::ServerModule* sharedModule(const QString& id) const;
    // Partner requests and module offers waiting in the clubs you look after
    // (answered on the website; docs/shared-modules.md, section 11). Empty on
    // a server without shared modules or partner clubs.
    const QList<sync::ClubRequestCount>& clubRequests() const { return clubAsks_; }
    // Your notifications (MainWindow's): where the server has them, what waits
    // in a club is answered right in its line, and Open goes where the app says.
    void setNotifications(sync::NotificationCenter* center,
                          std::function<void(const QUrl&, const sync::NotificationItem&, const NotificationPlace&)> open) {
        notifications_ = center;
        openNotification_ = std::move(open);
    }
    sync::NotificationCenter* notifications() const { return notifications_; }
    void openNotification(const QUrl& server, const sync::NotificationItem& n, const NotificationPlace& place) const {
        if (openNotification_) openNotification_(server, n, place);
    }
    // Another club's module shared with a club of yours (a partner club's,
    // or one accepted one off): on the Clubs tab under that club's name,
    // not under Shared with me.
    static bool fromAnotherClub(const sync::ServerModule& m);
    // Whether the server has shared modules (its features list
    // sync::kSharedModulesFeature, as the server list last heard). Tests
    // and a live layout's server may say so outright.
    bool knowsSharedModules() const;
    void setKnowsSharedModules(std::optional<bool> knows);
    // Your club with this slug, when you may take things off it (its admins
    // and managers: whoever may delete the club's items).
    bool mayRemoveFromClub(const QString& slug) const;
    // Ones you may save a new version of (owner or editor).
    QList<sync::ServerModule> editableModules() const;

    // The module open for changing in this window (shows "Editing now"), or empty.
    void setEditingModule(const QString& id);
    QString editingModule() const { return editing_; }
    // A module being added to the layout ("Adding…"), or empty.
    void setInserting(const QString& id);
    QString inserting() const { return inserting_; }

    // A picture from the server (a module's thumbnail, a catalog preview):
    // from the cache folder when it's there, else fetched and kept. `done`
    // runs later, unless `context` is gone by then; a null image: none.
    void picture(const QString& path, QObject* context, std::function<void(const QImage&)> done);
    // Where this server's pictures are kept.
    QString pictureCacheDir() const;
    void setPictureCacheRoot(const QString& dir) { cacheRoot_ = dir; }

    // Plain words for a refusal: why the server said no.
    static QString refusalText(const sync::ServerRefusal& r);

signals:
    void stateChanged();
    // The modules, clubs or catalog settings changed.
    void changed();
    void editingChanged();
    // The friendly line's buttons.
    void signInRequested();
    void addServerRequested();
    // The rows' buttons (MainWindow does the rest).
    void insertRequested(const QString& moduleId);
    void openRequested(const QString& moduleId);
    // A catalog module: copy it into your modules, then add it to the layout.
    void catalogInsertRequested(const bld::sync::CatalogItem& item);
    // Catalog parts came in (yours or a club's): fetch the server's parts.
    void partsAdded();
    // A catalog layout was copied to you (or a club): open the copy, live.
    void catalogLayoutCopied(const QString& layoutId, const QString& title);
    // A catalog venue was copied to your venues (or a club's): use it.
    void catalogVenueCopied(const QString& venueId, const QString& name);
    // Something to say in the status bar.
    void message(const QString& text);

private:
    void setState(State s, const QString& why = {});
    void fail(const sync::ServerRefusal& r);

    sync::LibraryApi api_;
    QUrl server_;
    QString label_;
    State state_ = State::NoServer;
    QString why_;
    QList<sync::ServerModule> modules_;
    QList<sync::ServerModule> shared_;
    QList<sync::ClubRequestCount> clubAsks_;
    sync::NotificationCenter* notifications_ = nullptr;
    std::function<void(const QUrl&, const sync::NotificationItem&, const NotificationPlace&)> openNotification_;
    std::optional<bool> knowsShared_;
    QList<sync::OrgEntry> orgs_;
    sync::CatalogSettings catalog_;
    QString editing_;
    QString inserting_;
    QString cacheRoot_;
    int generation_ = 0;  // a newer refresh (or server) wins
    int session_ = 0;     // a newer server or sign-in
    QHash<QString, QList<std::function<void(const QImage&)>>> waiting_;
    // Pictures this server said it hasn't got: not asked again until the
    // server or sign-in changes (each refresh redraws the rows, and a burst
    // of 404s from a show's shared address gets it banned by a firewall).
    QSet<QString> missing_;
};

// The rows' look, shared by the Module library's tabs and the Server window:
// a framed row, its picture (a placeholder until the server's comes),
// muted lines and group headers.
QFrame* libraryRowFrame(QWidget* parent);
QLabel* libraryRowPicture(QWidget* parent);
void showLibraryPicture(QLabel* label, const QImage& img);
QLabel* libraryMutedLabel(const QString& text, QWidget* parent);
QLabel* libraryGroupHeader(const QString& text, QWidget* parent);
// A club's group header: its logo (when it has one) and its name.
QWidget* libraryClubHeader(const QString& text, const QUrl& server, const QString& slug, QWidget* parent);
// "960 × 480 studs (7.7 × 3.8 m)"; empty when either is unknown.
QString studsSizeText(int widthStuds, int heightStuds);

// The friendly line over a tab: what's wrong and the one button that helps.
class LibraryStatusLine : public QWidget {
    Q_OBJECT
public:
    LibraryStatusLine(ServerLibrary& library, QWidget* parent);
    void showState();
    QLabel* text() const { return text_; }
    QPushButton* button() const { return button_; }

private:
    ServerLibrary& library_;
    QLabel* text_ = nullptr;
    QPushButton* button_ = nullptr;
};

// "On the server": your modules, then each club's, with pictures and versions.
class ServerModulesTab : public QWidget {
    Q_OBJECT
public:
    ServerModulesTab(ServerLibrary& library, QWidget* parent = nullptr);
    void rebuild();
    // The rows (tests): one widget per module, named "moduleRow:<id>".
    QWidget* row(const QString& id) const;
    QLineEdit* filter() const { return filter_; }
    // Asks for a new name / confirms deleting (tests replace them).
    std::function<QString(const sync::ServerModule&)> askName;
    std::function<bool(const sync::ServerModule&)> confirmDelete;
    // Asks before Take back / Give back (`give`); tests answer instead.
    std::function<bool(const sync::ServerModule&, bool give)> confirmReturn;
    // Take a club's module back (its author), or give it back (`give`, the club's runners).
    void giveOrTakeBack(const QString& id, bool give);
    // Mine / Clubs / Shared with me (on a server with shared modules; hidden otherwise).
    QTabBar* tabs() const { return tabs_; }
    enum Tab { Mine = 0, Clubs = 1, Shared = 2 };
    // Confirms taking a shared module off a club (its admins and managers); tests answer instead.
    std::function<bool(const sync::ServerModule&, const sync::ClubRef& club)> confirmRemoveFromClub;
    // The Share dialog a row's "Share with a club…" opened (tests).
    class ModuleShareDialog* shareDialog() const;
    // Opens a module's Share dialog (a notification's Open for one of yours).
    void share(const QString& id);
    // "Train Club: 2 partner requests or module offers wait for your answer.", each with
    // Accept and Decline right there, and the ⓘ club.moduleOffers: what waits in a club you
    // look after (the Clubs tab, and the Server window's Modules tab). A server before
    // notifications says to answer them on the website, with Open on the Website. Named
    // "clubRequestsWaiting:<slug>".
    static QWidget* clubWaitingLine(ServerLibrary& library, const sync::ClubRequestCount& waiting, QWidget* parent);

private:
    QWidget* makeRow(const sync::ServerModule& m);
    QWidget* makeSharedRow(const sync::ServerModule& m);
    void removeFromClub(const QString& id, const sync::ClubRef& club);
    void rename(const QString& id);
    void remove(const QString& id);

    ServerLibrary& library_;
    LibraryStatusLine* status_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QTabBar* tabs_ = nullptr;
    // (Set to null when it goes: QObject::destroyed.)
    QDialog* shareDialog_ = nullptr;
    QObject* dialogGone_ = nullptr;
    QWidget* listHost_ = nullptr;
    QVBoxLayout* list_ = nullptr;
    QLabel* empty_ = nullptr;
    QHash<QString, QWidget*> rows_;
};

// "Catalog": the server's public modules, parts, layouts, venues and collections.
class CatalogTab : public QWidget {
    Q_OBJECT
public:
    // (Layouts and Venues came later: their numbers follow, so the older buttons keep their names.)
    enum class Kind { Modules, Parts, Collections, Layouts, Venues };
    // `loadWhenShown`: asks the server only while the tab is on screen; a
    // change while it's hidden is fetched when it shows (the Server window's tabs).
    CatalogTab(ServerLibrary& library, QWidget* parent = nullptr, bool loadWhenShown = false);
    // Only these kinds (the Server window keeps Collections on a tab of its
    // own); one kind hides the kind buttons. All of them by default.
    void setKinds(const QList<Kind>& kinds);
    void setKind(Kind kind);
    Kind kind() const { return kind_; }
    void reload();
    // Opens a collection's items ("Back" returns to the list).
    void openCollection(const sync::CatalogCollection& c);
    QWidget* row(const QString& id) const;
    QLineEdit* search() const { return search_; }
    QLabel* note() const { return note_; }
    // Where an "Add" goes: "" for you, a club's slug, or nullopt when
    // cancelled. Asked only when a club of yours takes it: `clubs` are the
    // ones whose roles let you add it, `leftOut` says which aren't and the
    // permission to ask for (tests replace it).
    std::function<std::optional<QString>(const QString& title, const QList<sync::OrgEntry>& clubs,
                                         const QString& leftOut)>
        chooseOwner;

protected:
    void showEvent(QShowEvent* e) override;

private:
    void showItems(const QList<sync::CatalogItem>& items);
    void showCollections(const QList<sync::CatalogCollection>& list);
    QWidget* itemRow(const sync::CatalogItem& it);
    QWidget* collectionRow(const sync::CatalogCollection& c);
    void clearRows();
    void restoreScroll();
    void addItem(const sync::CatalogItem& it, QPushButton* button);
    bool kindOn(Kind k) const;
    void addAll(const sync::CatalogCollection& c);
    std::optional<QString> owner(const QString& title, const QList<sync::AddKind>& kinds);

    ServerLibrary& library_;
    Kind kind_ = Kind::Modules;
    LibraryStatusLine* status_ = nullptr;
    QWidget* kinds_ = nullptr;
    QList<QPushButton*> kindButtons_;
    QLineEdit* search_ = nullptr;
    QPushButton* back_ = nullptr;
    QLabel* note_ = nullptr;
    QVBoxLayout* list_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QHash<QString, QWidget*> rows_;
    QString openCollection_;
    QString added_;  // what the last Add all did
    // What each Add button already did ("Added", "Copied"), so a refresh
    // (a live change, coming back to the window) doesn't undo it.
    QHash<QString, QString> addedItems_;
    // What the rows show (kind, collection, search). A refresh of the
    // same keeps the rows and the scroll place until the new list is in.
    QString shown_;
    int keepScroll_ = -1;
    int generation_ = 0;
    QList<Kind> kinds_on_{ Kind::Modules, Kind::Parts, Kind::Layouts, Kind::Venues, Kind::Collections };
    bool loadWhenShown_ = false;
    bool stale_ = false;  // a reload waits for the tab to show
};

// "Added 2 items · already had 1 · 1 couldn't be added", as the web says it.
QString collectionAddText(const sync::CollectionAddResult& r);
// "1 item" / "3 items"; "1 use" / "3 uses".
QString itemsText(int n);
QString usesText(int n);

}  // namespace bld::ui
