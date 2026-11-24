#pragma once

// File › Version History… for the live layout: the versions the server
// keeps of it (web server history/layoutVersions.ts), newest first, with
// when, why, who and how many parts. Editors can keep a version now (with a
// name) and put one back: the server keeps the layout as it was first, so a
// restore can be undone, and everyone in the layout (this window too) sees
// the change at once. Anyone who can open it can copy a version out as a
// new layout. See on the Website shows the picked version drawn.

#include "../sync/LibraryApi.h"

#include <QDialog>

#include <functional>

class QLabel;
class QListWidget;
class QPushButton;

namespace bld::ui {

class VersionHistoryDialog : public QDialog {
    Q_OBJECT
public:
    VersionHistoryDialog(sync::LibraryApi& api, const QString& layoutId, const QString& layoutTitle, QWidget* parent = nullptr);

    // How Restore asks first, how a name is asked for, and how a link opens (tests answer instead).
    static std::function<bool(QWidget*, const QString& when)> confirmRestore;
    static std::function<bool(QWidget*, QString* name)> askName;
    static std::function<void(const QUrl&)> openLink;

    // Words for a version's reason ("While editing", "Before a restore"…).
    static QString reasonText(const QString& reason);

    const QList<sync::LayoutVersion>& versions() const { return versions_; }

signals:
    // A copy was made: the window may open it.
    void copied(const QString& layoutId, const QString& title);

public slots:
    void reload();

private:
    void fill();
    void updateButtons();
    void setBusy(bool busy, const QString& text = {});
    void failed(const sync::ServerRefusal& r);
    int current() const;

    sync::LibraryApi& api_;
    QString layoutId_;
    QList<sync::LayoutVersion> versions_;
    bool canRestore_ = false;
    bool busy_ = false;
    QLabel* status_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* keep_ = nullptr;
    QPushButton* restore_ = nullptr;
    QPushButton* copy_ = nullptr;
    QPushButton* web_ = nullptr;
};

} // namespace bld::ui
