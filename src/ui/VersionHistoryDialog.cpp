#include "VersionHistoryDialog.h"

#include "ConfirmDialog.h"
#include "RecoverDialog.h"
#include "help/HelpButton.h"
#include "theme/AppPrefs.h"
#include "tours/Tours.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

namespace bld::ui {

std::function<bool(QWidget*, const QString&)> VersionHistoryDialog::confirmRestore = [](QWidget* parent, const QString& when) {
    ConfirmOptions o;
    o.title = VersionHistoryDialog::tr("Put the layout back to %1?").arg(when);
    o.removes = VersionHistoryDialog::tr("The layout becomes as it was in this version. Everyone working in it sees the change at once.");
    o.keeps = VersionHistoryDialog::tr("The layout as it is now is kept in this list first (“Before a restore”), so you can go back to it.");
    o.undo = VersionHistoryDialog::tr("To undo, restore the “Before a restore” version.");
    o.confirmLabel = VersionHistoryDialog::tr("Restore This Version");
    o.danger = false;
    return ConfirmDialog::ask(parent, o);
};

std::function<bool(QWidget*, QString*)> VersionHistoryDialog::askName = [](QWidget* parent, QString* name) {
    bool ok = false;
    *name = QInputDialog::getText(parent, VersionHistoryDialog::tr("Keep a version now"),
                                  VersionHistoryDialog::tr("A name for it (you can leave it empty), for example Before the show:"),
                                  QLineEdit::Normal, QString(), &ok);
    return ok;
};

std::function<void(const QUrl&)> VersionHistoryDialog::openLink = [](const QUrl& url) { QDesktopServices::openUrl(url); };

QString VersionHistoryDialog::reasonText(const QString& reason) {
    if (reason == QLatin1String("manual")) return VersionHistoryDialog::tr("Kept by hand");
    if (reason == QLatin1String("before-import")) return VersionHistoryDialog::tr("Before a model was imported");
    if (reason == QLatin1String("before-merge")) return VersionHistoryDialog::tr("Before changes were merged in");
    if (reason == QLatin1String("before-restore")) return VersionHistoryDialog::tr("Before a restore");
    if (reason == QLatin1String("before-bulk-delete")) return VersionHistoryDialog::tr("Before many parts were deleted");
    return VersionHistoryDialog::tr("While editing");
}

VersionHistoryDialog::VersionHistoryDialog(sync::LibraryApi& api, const QString& layoutId, const QString& layoutTitle,
                                           QWidget* parent)
    : QDialog(parent), api_(api), layoutId_(layoutId) {
    setWindowTitle(VersionHistoryDialog::tr("Version history"));
    setObjectName(QStringLiteral("versionHistoryDialog"));
    auto* col = new QVBoxLayout(this);
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(VersionHistoryDialog::tr("Version history of %1").arg(layoutTitle), this);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    title->setFont(tf);
    title->setWordWrap(true);
    head->addWidget(title, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("history.versions"), this));
    auto* tour = new QPushButton(VersionHistoryDialog::tr("Show Me Around"), this);
    tour->setObjectName(QStringLiteral("versionHistoryTour"));
    tour->setFlat(true);
    head->addWidget(tour);
    col->addLayout(head);

    // Said while the site keeps no new versions; the ones kept can still be looked at and copied out.
    off_ = new QLabel(VersionHistoryDialog::tr("Version history is switched off on this server, so no new versions are kept and one can't be put back. "
                                               "The ones already kept can still be copied out as a new layout."), this);
    off_->setObjectName(QStringLiteral("versionHistoryOff"));
    off_->setWordWrap(true);
    off_->setVisible(false);
    col->addWidget(off_);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("versionHistoryStatus"));
    status_->setWordWrap(true);
    col->addWidget(status_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("versionHistoryList"));
    list_->setAccessibleName(VersionHistoryDialog::tr("Versions"));
    list_->setMinimumSize(480, 280);
    list_->setWordWrap(true);
    col->addWidget(list_, 1);
    tours::tag(list_, QStringLiteral("versionHistory.list"));

    auto* buttons = new QDialogButtonBox(this);
    auto* close = buttons->addButton(VersionHistoryDialog::tr("Close"), QDialogButtonBox::RejectRole);
    web_ = buttons->addButton(VersionHistoryDialog::tr("See on the Website"), QDialogButtonBox::ActionRole);
    web_->setObjectName(QStringLiteral("versionHistoryWeb"));
    web_->setToolTip(VersionHistoryDialog::tr("Open the layout's history on the website, which draws each version"));
    copy_ = buttons->addButton(VersionHistoryDialog::tr("Copy as a New Layout"), QDialogButtonBox::ActionRole);
    copy_->setObjectName(QStringLiteral("versionHistoryCopy"));
    copy_->setToolTip(VersionHistoryDialog::tr("Make a new layout of yours from the picked version; this one stays as it is"));
    restore_ = buttons->addButton(VersionHistoryDialog::tr("Restore…"), QDialogButtonBox::ActionRole);
    restore_->setObjectName(QStringLiteral("versionHistoryRestore"));
    restore_->setToolTip(VersionHistoryDialog::tr("Put the layout back to the picked version (the layout as it is now is kept first)"));
    keep_ = buttons->addButton(VersionHistoryDialog::tr("Keep a Version Now…"), QDialogButtonBox::ActionRole);
    keep_->setObjectName(QStringLiteral("versionHistoryKeep"));
    keep_->setToolTip(VersionHistoryDialog::tr("Keep the layout as it is now in this list, with a name if you like"));
    col->addWidget(buttons);
    tours::tag(keep_, QStringLiteral("versionHistory.keep"));
    tours::tag(restore_, QStringLiteral("versionHistory.restore"));
    tours::tag(copy_, QStringLiteral("versionHistory.copy"));

    connect(tour, &QPushButton::clicked, this,
            [this] { tours::startTour(this, QStringLiteral("history"), theme::PrefsStore::instance()); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(web_, &QPushButton::clicked, this, [this] { openLink(api_.layoutHistoryWebUrl(layoutId_)); });
    connect(keep_, &QPushButton::clicked, this, [this] {
        QString name;
        if (!askName(this, &name)) return;
        setBusy(true, VersionHistoryDialog::tr("Keeping a version…"));
        api_.keepLayoutVersion(
            layoutId_, QStringLiteral("manual"), name,
            [this, alive = std::weak_ptr<int>(alive_)](const QString&) {
                if (alive.expired()) return;
                setBusy(false, VersionHistoryDialog::tr("Kept a version of the layout as it is now."));
                reload();
            },
            [this, alive = std::weak_ptr<int>(alive_)](const sync::ServerRefusal& r) { if (!alive.expired()) failed(r); });
    });
    connect(restore_, &QPushButton::clicked, this, [this] {
        const int row = current();
        if (row < 0) return;
        const sync::LayoutVersion v = versions_.at(row);
        if (!confirmRestore(this, RecoverDialog::whenText(v.createdAt))) return;
        setBusy(true, VersionHistoryDialog::tr("Putting the version back…"));
        api_.restoreLayoutVersion(
            layoutId_, v.id,
            [this, alive = std::weak_ptr<int>(alive_), v](const QString&) {
                if (alive.expired()) return;
                setBusy(false, VersionHistoryDialog::tr("The layout is back to how it was %1. How it was just before is kept as “Before a restore”.")
                                         .arg(RecoverDialog::whenText(v.createdAt)));
                reload();
            },
            [this, alive = std::weak_ptr<int>(alive_)](const sync::ServerRefusal& r) { if (!alive.expired()) failed(r); });
    });
    connect(copy_, &QPushButton::clicked, this, [this] {
        const int row = current();
        if (row < 0) return;
        setBusy(true, VersionHistoryDialog::tr("Making a copy…"));
        api_.copyLayoutVersion(
            layoutId_, versions_.at(row).id,
            [this, alive = std::weak_ptr<int>(alive_)](const QString& id, const QString& title) {
                if (alive.expired()) return;
                setBusy(false, VersionHistoryDialog::tr("Made a new layout, “%1”, from this version. It is in File › Open from Server….").arg(title));
                emit copied(id, title);
            },
            [this, alive = std::weak_ptr<int>(alive_)](const sync::ServerRefusal& r) { if (!alive.expired()) failed(r); });
    });
    // Asked for once the dialog is built (the answer may come at any time).
    QTimer::singleShot(0, this, &VersionHistoryDialog::reload);
}

void VersionHistoryDialog::reload() {
    setBusy(true, versions_.isEmpty() ? VersionHistoryDialog::tr("Loading…") : status_->text());
    const QString keepText = status_->text();
    api_.layoutVersions(
        layoutId_,
        [this, alive = std::weak_ptr<int>(alive_), keepText](const sync::LayoutVersions& v) {
            if (alive.expired()) return;
            versions_ = v.versions;
            canRestore_ = v.canRestore && v.enabled;
            enabled_ = v.enabled;
            busy_ = false;
            fill();
            if (keepText != VersionHistoryDialog::tr("Loading…")) status_->setText(keepText);
        },
        [this, alive = std::weak_ptr<int>(alive_)](const sync::ServerRefusal& r) {
            if (alive.expired()) return;
            if (r.status == 404)
                status_->setText(VersionHistoryDialog::tr("This server doesn't keep earlier versions of layouts yet. Ask its admin to update it."));
            else
                failed(r);
            busy_ = false;
            updateButtons();
        });
}

void VersionHistoryDialog::fill() {
    const QString picked = current() >= 0 ? versions_.at(current()).id : QString();
    list_->clear();
    int select = versions_.isEmpty() ? -1 : 0;
    for (const sync::LayoutVersion& v : std::as_const(versions_)) {
        const QString why = v.label.isEmpty() ? reasonText(v.reason) : VersionHistoryDialog::tr("“%1”").arg(v.label);
        const QStringList who = v.editors.isEmpty() && !v.createdBy.isEmpty() ? QStringList{ v.createdBy } : v.editors;
        QString line = VersionHistoryDialog::tr("%1 — %2").arg(RecoverDialog::whenText(v.createdAt), why);
        if (!who.isEmpty()) line += VersionHistoryDialog::tr(" · %1").arg(who.mid(0, 3).join(QStringLiteral(", ")));
        line += QLatin1Char('\n') + VersionHistoryDialog::tr("%n part(s)", "", v.parts) + QStringLiteral(" · ") + VersionHistoryDialog::tr("%n sheet(s)", "", v.sheets);
        auto* item = new QListWidgetItem(line, list_);
        item->setData(Qt::UserRole, v.id);
        if (v.id == picked) select = list_->count() - 1;
    }
    if (versions_.isEmpty()) {
        status_->setText(VersionHistoryDialog::tr("No versions yet. One is kept after the first edits, then every half hour while people work on "
                            "the layout, and just before big changes."));
    } else if (status_->text() == VersionHistoryDialog::tr("Loading…")) {
        status_->setText(canRestore_ ? VersionHistoryDialog::tr("Pick a version to put it back, or to copy it out as a new layout.")
                                     : VersionHistoryDialog::tr("Pick a version to copy it out as a new layout. Only the layout's editors can put one back."));
    }
    list_->setVisible(!versions_.isEmpty());
    if (select >= 0) list_->setCurrentRow(select);
    updateButtons();
}

int VersionHistoryDialog::current() const {
    const int row = list_->currentRow();
    return row >= 0 && row < versions_.size() ? row : -1;
}

void VersionHistoryDialog::setBusy(bool busy, const QString& text) {
    busy_ = busy;
    if (!text.isEmpty()) status_->setText(text);
    updateButtons();
}

void VersionHistoryDialog::failed(const sync::ServerRefusal& r) {
    busy_ = false;
    status_->setText(sync::describe(r));
    updateButtons();
}

void VersionHistoryDialog::updateButtons() {
    const bool picked = current() >= 0;
    off_->setVisible(!enabled_);
    keep_->setVisible(canRestore_);
    restore_->setVisible(canRestore_);
    keep_->setEnabled(!busy_);
    restore_->setEnabled(!busy_ && picked);
    copy_->setEnabled(!busy_ && picked);
    web_->setEnabled(!versions_.isEmpty());
}

} // namespace bld::ui
