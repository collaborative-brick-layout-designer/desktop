#include "ModuleShareDialog.h"

#include "ConfirmDialog.h"
#include "ServerLibrary.h"
#include "help/HelpButton.h"

#include "../sync/WhileAlive.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {
// What the server said, in this dialog's words where it has some.
QString shareRefusal(const sync::ServerRefusal& r) {
    if (r.code == QLatin1String("club_module_not_shareable"))
        return ModuleShareDialog::tr("This module belongs to a club, so its members already have it. A club's module "
                                     "can't be shared with another club.");
    if (r.code == QLatin1String("too_many_club_shares"))
        return ModuleShareDialog::tr("This module is shared with as many clubs as it can be. Stop sharing with one first.");
    if (r.code == QLatin1String("not_shared")) return ModuleShareDialog::tr("It isn't shared with that club any more.");
    return ServerLibrary::refusalText(r);
}
}  // namespace

ConfirmOptions stopShareOptions(const QString& title, const QString& who, int layouts, int version) {
    const QString v = version > 0 ? ModuleShareDialog::tr(" (version %1)").arg(version) : QString();
    ConfirmOptions o;
    o.title = ModuleShareDialog::tr("Stop sharing with %1?").arg(who);
    o.keeps = layouts > 0 ? ModuleShareDialog::tr("Its %n layout(s) keep “%1” as it is now%2.", nullptr, layouts).arg(title, v)
                          : ModuleShareDialog::tr("Nothing changes in any layout: “%1” isn't placed in one of theirs.").arg(title);
    o.removes = ModuleShareDialog::tr("They won't get your updates anymore, and can't place it again.");
    o.confirmLabel = ModuleShareDialog::tr("Stop");
    o.danger = false;
    return o;
}

ModuleShareDialog::ModuleShareDialog(ServerLibrary& library, const sync::ServerModule& module, QWidget* parent)
    : QDialog(parent), library_(library), id_(module.id), title_(module.title) {
    setObjectName(QStringLiteral("moduleShareDialog"));
    setWindowTitle(tr("Share “%1”").arg(title_));
    setMinimumWidth(440);
    auto* col = new QVBoxLayout(this);
    col->setSpacing(10);

    auto* top = new QHBoxLayout();
    note_ = new QLabel(tr("A club you share with can place this module in the club's layouts and gets your updates. "
                          "Its members can't change its parts."),
                       this);
    note_->setObjectName(QStringLiteral("shareNote"));
    note_->setWordWrap(true);
    top->addWidget(note_, 1);
    top->addWidget(new help::HelpButton(QStringLiteral("module.share"), this), 0, Qt::AlignTop);
    col->addLayout(top);

    addRow_ = new QWidget(this);
    auto* add = new QHBoxLayout(addRow_);
    add->setContentsMargins(0, 0, 0, 0);
    pick_ = new QComboBox(addRow_);
    pick_->setObjectName(QStringLiteral("shareClubPick"));
    pick_->setAccessibleName(tr("Share with a club"));
    add->addWidget(pick_, 1);
    share_ = new QPushButton(tr("Share"), addRow_);
    share_->setObjectName(QStringLiteral("shareWithClub"));
    share_->setProperty("accent", true);
    add->addWidget(share_);
    col->addWidget(addRow_);
    connect(pick_, &QComboBox::currentIndexChanged, this,
            [this] { share_->setEnabled(!pick_->currentData().toString().isEmpty()); });
    connect(share_, &QPushButton::clicked, this, [this] {
        if (const QString slug = pick_->currentData().toString(); !slug.isEmpty()) shareWith(slug);
    });

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("shareError"));
    error_->setProperty("danger", true);
    error_->setWordWrap(true);
    error_->hide();
    col->addWidget(error_);

    clubsHost_ = new QWidget(this);
    clubs_ = new QVBoxLayout(clubsHost_);
    clubs_->setContentsMargins(0, 0, 0, 0);
    clubs_->setSpacing(4);
    col->addWidget(clubsHost_);

    usedIn_ = new QLabel(this);
    usedIn_->setObjectName(QStringLiteral("shareUsedIn"));
    usedIn_->setWordWrap(true);
    usedIn_->setProperty("muted", true);
    usedIn_->hide();
    col->addWidget(usedIn_);

    auto* person = new QHBoxLayout();
    auto* personNote = new QLabel(tr("To share with one person by email, open this module on the website."), this);
    personNote->setWordWrap(true);
    personNote->setProperty("muted", true);
    person->addWidget(personNote, 1);
    auto* web = new QPushButton(tr("Open on the Website"), this);
    web->setObjectName(QStringLiteral("shareOnWebsite"));
    connect(web, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(library_.api().moduleWebUrl(id_)); });
    person->addWidget(web);
    col->addLayout(person);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addWidget(buttons);

    confirmStop = [this](const ConfirmOptions& o) { return ConfirmDialog::ask(this, o); };
    addRow_->hide();
    // Someone else's change to who it's shared with, or where it's used, shows here too.
    connect(&library_, &ServerLibrary::changed, this, &ModuleShareDialog::reload);
    reload();
}

QPushButton* ModuleShareDialog::stopButton(const QString& orgId) const {
    return clubsHost_->findChild<QPushButton*>(QStringLiteral("stopShare:") + orgId);
}

void ModuleShareDialog::reload() {
    library_.api().moduleShares(id_, sync::whileAlive(this, [this](const sync::ModuleShares& s) { show(s); }),
                                sync::whileAlive(this, [this](const sync::ServerRefusal& r) { fail(r); }));
}

void ModuleShareDialog::fail(const sync::ServerRefusal& r) {
    error_->setText(shareRefusal(r));
    error_->show();
}

void ModuleShareDialog::show(const sync::ModuleShares& s) {
    shares_ = s;
    loaded_ = true;
    while (QLayoutItem* it = clubs_->takeAt(0)) {
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    QList<sync::ClubRef> open;
    for (const auto& c : s.myClubs)
        if (std::none_of(s.clubs.begin(), s.clubs.end(), [&](const sync::ClubRef& x) { return x.id == c.id; })) open << c;

    if (!s.canShareWithClubs) note_->setText(tr("This is a club's module: its members already have it."));
    else if (s.myClubs.isEmpty())
        note_->setText(tr("A club you share with can place this module in the club's layouts and gets your updates. "
                          "You're not in a club yet: join or start one to share with it."));
    else if (open.isEmpty())
        note_->setText(tr("A club you share with can place this module in the club's layouts and gets your updates. "
                          "Its members can't change its parts. It's shared with every club you're in."));
    else
        note_->setText(tr("A club you share with can place this module in the club's layouts and gets your updates. "
                          "Its members can't change its parts."));
    addRow_->setVisible(s.canShareWithClubs && !open.isEmpty());
    pick_->clear();
    pick_->addItem(tr("Pick a club…"), QString());
    for (const auto& c : std::as_const(open)) pick_->addItem(c.name, c.slug);
    share_->setEnabled(false);

    for (const auto& c : s.clubs) {
        auto* row = new QWidget(clubsHost_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLabel(tr("%1 (club) · can place it").arg(c.name), row);
        name->setTextFormat(Qt::PlainText);
        h->addWidget(name, 1);
        auto* stopButton = new QPushButton(tr("Stop"), row);
        stopButton->setObjectName(QStringLiteral("stopShare:") + c.id);
        stopButton->setAccessibleName(tr("Stop sharing with %1").arg(c.name));
        connect(stopButton, &QPushButton::clicked, this, [this, c] { stop(c); });
        h->addWidget(stopButton);
        clubs_->addWidget(row);
    }
    const QString used = s.usedInText();
    usedIn_->setText(used);
    usedIn_->setVisible(!used.isEmpty());
    // Tall enough for what it now says (the rows come and go).
    // (Wrapped words don't tell a dialog how tall they are: say it for them.)
    const int wide = std::max(width(), minimumWidth());
    note_->setMinimumHeight(note_->heightForWidth(wide - 70));
    layout()->activate();
    adjustSize();
    resize(wide, std::max(height(), sizeHint().height()));
}

void ModuleShareDialog::shareWith(const QString& slug) {
    error_->hide();
    share_->setEnabled(false);
    library_.api().shareWithClub(id_, slug, sync::whileAlive(this, [this] {
        reload();
        library_.refresh();
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
        fail(r);
        reload();
    }));
}

void ModuleShareDialog::stop(const sync::ClubRef& club) {
    if (!confirmStop(stopShareOptions(title_, club.name, shares_.layoutsOf(club.id), shares_.latestVersion))) return;
    error_->hide();
    library_.api().stopClubShare(id_, club.id, sync::whileAlive(this, [this] {
        reload();
        library_.refresh();
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
        // Already stopped (from the website, or by the club): the list catches up.
        if (r.code != QLatin1String("not_shared")) fail(r);
        reload();
    }));
}

}  // namespace bld::ui
