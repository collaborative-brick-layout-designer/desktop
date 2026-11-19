#pragma once

// The desktop's notifications, as the web's bell shows them
// (apps/web/src/notifications in the web repo): the bell in the main
// toolbar with your unread count (99+, every signed-in server added up),
// its panel, and the Notifications window. Both list the newest first,
// grouped, with All or Unread and a type filter (the window adds search and
// more pages) and, with more than one server, a server picker and each
// item's server named.
//
// Each item: mark read or unread, Open (in the app where there is a place
// for it: the layout, the module, the club; else the website, and the
// button says so), and Accept / Decline or I understand where something
// waits for you, answered in the app (POST /api/notifications/:id/act).
// The item then says what happened ("You accepted"), or that someone else
// answered first. Everything follows the server's live hints.

#include "../sync/NotificationCenter.h"

#include <QCoreApplication>
#include <QFrame>
#include <QHash>
#include <QSet>
#include <QToolButton>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace bld::ui {

struct ConfirmOptions;

// The plain words both apps use.
struct NotificationWords {
    Q_DECLARE_TR_FUNCTIONS(NotificationWords)
public:
    // "You accepted", "Ann declined", "Taken back, so nothing to answer"; empty while it waits.
    static QString outcome(const sync::NotificationItem& n);
    // "just now", "5 min ago", "3 h ago", "2 days ago", then the date.
    static QString timeAgo(qint64 ms, qint64 nowMs);
    static QString category(const QString& category);
    // "99+" past 99.
    static QString badge(int n);
    // "Let them in" for a request to join, else "Accept".
    static QString acceptLabel(const sync::NotificationItem& n);
    // Whether Decline asks first (as the club page does).
    static bool declineNeedsConfirm(const QString& type);
    static ConfirmOptions declineQuestion();
};

// Where Open goes.
struct NotificationPlace {
    enum Kind { None, Layout, Module, Club, Clubs, Web };
    Kind kind = None;
    QString id;    // the layout or module id, the club's slug
    QString link;  // the website path (Web)
    // The button's words: "Open the layout", "Show the module", "Show the
    // club", "Open on the website" (which says it leaves the app).
    QString openLabel() const;
};
// From the item's link (a path on the site): /editor/<id> is a layout,
// /modules/<id> a module, /orgs/<slug> a club, /orgs your clubs; any other
// path is the website. Nothing while a request waits for its answer (its
// thing usually isn't yours to open yet).
NotificationPlace placeFor(const sync::NotificationItem& n);

// One item.
class NotificationRow : public QFrame {
    Q_OBJECT
public:
    NotificationRow(sync::NotificationCenter& center, const QUrl& server, const sync::NotificationItem& n, bool compact,
                    const QString& serverLabel, bool answeredFirstElsewhere, QWidget* parent);
    const sync::NotificationItem& item() const { return item_; }
    QUrl server() const { return server_; }

    // How it asks before declining (tests answer instead).
    static std::function<bool(QWidget*, const ConfirmOptions&)> confirm;

signals:
    void openRequested(const QUrl& server, const bld::sync::NotificationItem& n, const bld::ui::NotificationPlace& place);
    // Answered here: `already` when someone else had answered first.
    void answered(const QString& id, bool already);

private:
    void build();
    void act(const QString& action);
    void acknowledge();
    void setRead(bool read);
    void setBusy(bool busy);
    void showError(const QString& text);

    sync::NotificationCenter& center_;
    QUrl server_;
    sync::NotificationItem item_;
    bool compact_ = false;
    QString serverLabel_;
    bool already_ = false;
    QString resultLayoutId_;
    QList<QPushButton*> busyButtons_;
    QLabel* error_ = nullptr;
};

// The list the panel and the window share, with its filters.
class NotificationList : public QWidget {
    Q_OBJECT
public:
    // `compact`: the panel (shorter bodies, no search, no more pages).
    NotificationList(sync::NotificationCenter& center, bool compact, QWidget* parent = nullptr);

    // Asks the servers again (shown) or when next shown.
    void reload();
    // Only these types, on one server (the Modules tab's requests); empty: all.
    void setServerFilter(const QUrl& server);
    void setTypeFilter(const QString& value);  // a type, or "cat:<category>"
    void setUnreadOnly(bool unread);

    // For tests.
    QList<NotificationRow*> rows() const;
    NotificationRow* row(const QString& id) const;
    QLabel* emptyLine() const { return empty_; }
    QPushButton* readAllButton() const { return readAll_; }
    QComboBox* serverPicker() const { return servers_; }
    QComboBox* typePicker() const { return types_; }
    QLineEdit* searchBox() const { return search_; }
    QPushButton* moreButton() const { return more_; }
    int loads() const { return loads_; }

signals:
    void openRequested(const QUrl& server, const bld::sync::NotificationItem& n, const bld::ui::NotificationPlace& place);

protected:
    void showEvent(QShowEvent* e) override;

private:
    struct Entry {
        QUrl server;
        sync::NotificationItem item;
    };
    void fetch(bool more);
    void rebuild();
    void refillPickers();
    QList<QUrl> picked() const;

    sync::NotificationCenter& center_;
    bool compact_;
    QToolButton* all_ = nullptr;
    QToolButton* unread_ = nullptr;
    QComboBox* types_ = nullptr;
    QComboBox* servers_ = nullptr;
    QLineEdit* search_ = nullptr;
    QPushButton* readAll_ = nullptr;
    QPushButton* more_ = nullptr;
    QLabel* empty_ = nullptr;
    QLabel* status_ = nullptr;
    QWidget* host_ = nullptr;
    QVBoxLayout* rowsLayout_ = nullptr;
    QTimer* soon_ = nullptr;
    QTimer* typing_ = nullptr;
    QList<Entry> entries_;
    QHash<QString, QString> cursors_;  // per server key: the next page's cursor
    QSet<QString> already_;            // answered here after someone else had
    QSet<QString> openGroups_;
    bool stale_ = true;
    int generation_ = 0;
    int loads_ = 0;
};

// The requests of some types waiting in one club, answered right there (the
// Modules tab's partner requests and module offers).
class NotificationRequests : public QWidget {
    Q_OBJECT
public:
    NotificationRequests(sync::NotificationCenter& center, const QUrl& server, const QStringList& types,
                         const QString& clubSlug, QWidget* parent = nullptr);
    void reload();
    QList<NotificationRow*> rows() const;

signals:
    void openRequested(const QUrl& server, const bld::sync::NotificationItem& n, const bld::ui::NotificationPlace& place);

private:
    sync::NotificationCenter& center_;
    QUrl server_;
    QStringList types_;
    QString club_;
    QVBoxLayout* rows_ = nullptr;
    QSet<QString> answered_;  // stay shown (with what happened) until the list is asked again
    int generation_ = 0;
};

// The bell in the main toolbar, with the unread count.
class NotificationBell : public QToolButton {
    Q_OBJECT
public:
    NotificationBell(sync::NotificationCenter& center, QWidget* parent = nullptr);
    // What the badge says ("" for none, "3", "99+").
    QString badgeText() const;
    void updateCount();

protected:
    void paintEvent(QPaintEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    sync::NotificationCenter& center_;
};

// The bell's panel: the newest, All or Unread, a type filter, Mark all as
// read and "See all notifications".
class NotificationPanel : public QFrame {
    Q_OBJECT
public:
    NotificationPanel(sync::NotificationCenter& center, QWidget* parent = nullptr);
    // Under `anchor`, kept on its screen.
    void popUp(QWidget* anchor);
    NotificationList* list() const { return list_; }

signals:
    void seeAllRequested();

private:
    NotificationList* list_ = nullptr;
};

// The whole list, with search and more pages.
class NotificationsWindow : public QWidget {
    Q_OBJECT
public:
    NotificationsWindow(sync::NotificationCenter& center, QWidget* parent = nullptr);
    NotificationList* list() const { return list_; }

private:
    NotificationList* list_ = nullptr;
};

}  // namespace bld::ui
