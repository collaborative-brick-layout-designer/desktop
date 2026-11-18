#include "ModuleShareDialog.h"

#include "ConfirmDialog.h"
#include "ServerLibrary.h"
#include "help/HelpButton.h"

#include "../sync/WhileAlive.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
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
    if (r.code == QLatin1String("org_not_found")) return ModuleShareDialog::tr("No club has that address. Check the link and try again.");
    if (r.code == QLatin1String("same_club")) return ModuleShareDialog::tr("That's this module's own club.");
    if (r.code == QLatin1String("declined_recently"))
        return ModuleShareDialog::tr("That club declined this module. You can offer it again a day after it declined.");
    if (r.code == QLatin1String("too_many_offers"))
        return ModuleShareDialog::tr("Your club has many module offers waiting for an answer already. Take some back, or wait for an answer.");
    return ServerLibrary::refusalText(r);
}
// A pasted link to a club's page ("…/orgs/riverside-lug").
bool isClubLink(const QString& text) {
    static const QRegularExpression link(QStringLiteral("/orgs/[a-z0-9-]+"), QRegularExpression::CaseInsensitiveOption);
    return link.match(text).hasMatch();
}

// How a club came to have the module: "(club)", "(partner club)", "(club, accepted)".
QString viaWords(const QString& via) {
    if (via == QLatin1String("partner")) return ModuleShareDialog::tr("(partner club)");
    if (via == QLatin1String("offer")) return ModuleShareDialog::tr("(club, accepted)");
    return ModuleShareDialog::tr("(club)");
}
}  // namespace

QString offerText(const sync::ClubOffer& f, const QString& address, qint64 now) {
    const QString who = f.club.name.isEmpty() ? ModuleShareDialog::tr("the club at %1").arg(address) : f.club.name;
    if (f.pending()) return ModuleShareDialog::tr("%1 · waiting for them to accept").arg(who);
    return f.askAgainAt > now ? ModuleShareDialog::tr("%1 · declined (you can offer it again in a day)").arg(who)
                              : ModuleShareDialog::tr("%1 · declined").arg(who);
}

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
    top->addWidget(new help::HelpButton(module.owner.isClub() ? QStringLiteral("module.shareOtherClubs") : QStringLiteral("module.share"), this), 0, Qt::AlignTop);
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

    // A club's module: partner clubs, and another club.
    otherHost_ = new QWidget(this);
    otherHost_->setObjectName(QStringLiteral("clubToClub"));
    auto* other = new QVBoxLayout(otherHost_);
    other->setContentsMargins(0, 0, 0, 0);
    other->setSpacing(6);
    auto* partnersHead = new QLabel(tr("Partner clubs"), otherHost_);
    partnersHead->setProperty("muted", true);
    other->addWidget(partnersHead);
    partnersNote_ = new QLabel(otherHost_);
    partnersNote_->setObjectName(QStringLiteral("partnersNote"));
    partnersNote_->setWordWrap(true);
    partnersNote_->setProperty("muted", true);
    partnersNote_->setOpenExternalLinks(true);
    other->addWidget(partnersNote_);
    auto* partnerRow = new QHBoxLayout();
    partnerPick_ = new QComboBox(otherHost_);
    partnerPick_->setObjectName(QStringLiteral("partnerClubPick"));
    partnerPick_->setAccessibleName(tr("Share with a partner club"));
    partnerRow->addWidget(partnerPick_, 1);
    partnerShare_ = new QPushButton(tr("Share"), otherHost_);
    partnerShare_->setObjectName(QStringLiteral("shareWithPartner"));
    partnerShare_->setProperty("accent", true);
    partnerRow->addWidget(partnerShare_);
    other->addLayout(partnerRow);
    connect(partnerPick_, &QComboBox::currentIndexChanged, this,
            [this] { partnerShare_->setEnabled(!partnerPick_->currentData().toString().isEmpty()); });
    connect(partnerShare_, &QPushButton::clicked, this, [this] {
        if (const QString slug = partnerPick_->currentData().toString(); !slug.isEmpty()) offerTo(slug);
    });
    auto* otherHead = new QLabel(tr("Another club…"), otherHost_);
    otherHead->setProperty("muted", true);
    other->addWidget(otherHead);
    otherClub_ = new QLineEdit(otherHost_);
    otherClub_->setObjectName(QStringLiteral("otherClubSearch"));
    otherClub_->setPlaceholderText(tr("Search clubs, or paste a club's link"));
    otherClub_->setAccessibleName(tr("Another club: search clubs, or paste a club's link"));
    other->addWidget(otherClub_);
    offerLink_ = new QPushButton(tr("Offer It to This Club"), otherHost_);
    offerLink_->setObjectName(QStringLiteral("offerToLink"));
    offerLink_->setProperty("accent", true);
    offerLink_->hide();
    other->addWidget(offerLink_, 0, Qt::AlignLeft);
    connect(offerLink_, &QPushButton::clicked, this, [this] { offerTo(otherClub_->text().trimmed()); });
    targetsHost_ = new QWidget(otherHost_);
    targets_ = new QVBoxLayout(targetsHost_);
    targets_->setContentsMargins(0, 0, 0, 0);
    targets_->setSpacing(2);
    other->addWidget(targetsHost_);
    search_ = new QTimer(this);
    search_->setSingleShot(true);
    search_->setInterval(300);
    connect(search_, &QTimer::timeout, this, &ModuleShareDialog::searchNow);
    connect(otherClub_, &QLineEdit::textChanged, this, [this](const QString& text) {
        const bool link = isClubLink(text);
        offerLink_->setVisible(link);
        if (link || text.trimmed().size() < 2) {
            search_->stop();
            showTargets({});
        } else {
            search_->start();
        }
    });
    connect(otherClub_, &QLineEdit::returnPressed, this, [this] {
        if (isClubLink(otherClub_->text())) offerTo(otherClub_->text().trimmed());
    });
    said_ = new QLabel(otherHost_);
    said_->setObjectName(QStringLiteral("shareSaid"));
    said_->setWordWrap(true);
    said_->setTextFormat(Qt::PlainText);
    said_->hide();
    other->addWidget(said_);
    offersHost_ = new QWidget(otherHost_);
    offers_ = new QVBoxLayout(offersHost_);
    offers_->setContentsMargins(0, 0, 0, 0);
    offers_->setSpacing(4);
    other->addWidget(offersHost_);
    otherHost_->hide();
    col->addWidget(otherHost_);

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

QPushButton* ModuleShareDialog::targetButton(const QString& slug) const {
    return targetsHost_->findChild<QPushButton*>(QStringLiteral("shareTarget:") + slug);
}

QPushButton* ModuleShareDialog::withdrawButton(const QString& offerId) const {
    return offersHost_->findChild<QPushButton*>(QStringLiteral("withdrawOffer:") + offerId);
}

QPushButton* ModuleShareDialog::offerAgainButton(const QString& offerId) const {
    return offersHost_->findChild<QPushButton*>(QStringLiteral("offerAgain:") + offerId);
}

namespace {
void clearLayout(QVBoxLayout* l) {
    while (QLayoutItem* it = l->takeAt(0)) {
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
}
}  // namespace

void ModuleShareDialog::searchNow() {
    const QString q = otherClub_->text().trimmed();
    if (q.size() < 2 || isClubLink(q)) return;
    library_.api().clubTargets(id_, q, sync::whileAlive(this, [this, q](const QList<sync::ClubTarget>& t) {
        // (An answer to an older search is dropped.)
        if (otherClub_->text().trimmed() == q) showTargets(t);
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) { fail(r); }));
}

void ModuleShareDialog::showTargets(const QList<sync::ClubTarget>& targets) {
    clearLayout(targets_);
    const QString q = otherClub_->text().trimmed();
    if (targets.isEmpty()) {
        if (q.size() >= 2 && !isClubLink(q) && !search_->isActive()) {
            auto* none = new QLabel(tr("No listed club matches “%1”. A club that isn't listed is found by pasting its link.").arg(q),
                                    targetsHost_);
            none->setObjectName(QStringLiteral("noClubMatches"));
            none->setWordWrap(true);
            none->setProperty("muted", true);
            targets_->addWidget(none);
        }
        return;
    }
    for (const auto& t : targets) {
        auto* row = new QWidget(targetsHost_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLabel(t.partner ? tr("%1 (partner club)").arg(t.club.name) : t.club.name, row);
        name->setTextFormat(Qt::PlainText);
        h->addWidget(name, 1);
        if (t.shared) {
            auto* done = new QLabel(tr("Shared"), row);
            done->setProperty("muted", true);
            h->addWidget(done);
        } else if (t.offer == QLatin1String("pending")) {
            auto* waiting = new QLabel(tr("Waiting for them"), row);
            waiting->setProperty("muted", true);
            h->addWidget(waiting);
        } else {
            auto* go = new QPushButton(t.partner ? tr("Share") : tr("Offer"), row);
            go->setObjectName(QStringLiteral("shareTarget:") + t.club.slug);
            go->setAccessibleName(t.partner ? tr("Share with %1").arg(t.club.name) : tr("Offer it to %1").arg(t.club.name));
            const QString slug = t.club.slug;
            connect(go, &QPushButton::clicked, this, [this, slug] { offerTo(slug); });
            h->addWidget(go);
        }
        targets_->addWidget(row);
    }
}

void ModuleShareDialog::showClubToClub(const sync::ModuleShares& s) {
    otherHost_->setVisible(s.canShareWithOtherClubs);
    if (!s.canShareWithOtherClubs) return;
    QList<sync::ClubRef> open;
    for (const auto& p : s.partners)
        if (std::none_of(s.clubs.begin(), s.clubs.end(), [&](const sync::ClubRef& x) { return x.id == p.id; })) open << p;
    if (s.partners.isEmpty()) {
        const QUrl ask = library_.api().clubPartnersWebUrl(s.club.slug);
        partnersNote_->setText(tr("%1 has no partner clubs yet. <a href=\"%2\">Ask a club to be partners</a> on the website.")
                                   .arg(s.club.name.toHtmlEscaped(), ask.toString(QUrl::FullyEncoded).toHtmlEscaped()));
        partnersNote_->setTextFormat(Qt::RichText);
    } else {
        partnersNote_->setTextFormat(Qt::PlainText);
        partnersNote_->setText(open.isEmpty() ? tr("It's shared with every partner club.") : QString());
    }
    partnersNote_->setVisible(!partnersNote_->text().isEmpty());
    partnerPick_->clear();
    partnerPick_->addItem(tr("Pick a partner club…"), QString());
    for (const auto& p : std::as_const(open)) partnerPick_->addItem(p.name, p.slug);
    partnerPick_->setVisible(!open.isEmpty());
    partnerShare_->setVisible(!open.isEmpty());
    partnerShare_->setEnabled(false);

    clearLayout(offers_);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& f : s.offers) {
        auto* row = new QWidget(offersHost_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* words = new QLabel(offerText(f, library_.api().url(QStringLiteral("/orgs/") + f.club.slug).toString(), now), row);
        words->setObjectName(QStringLiteral("offer:") + f.id);
        words->setTextFormat(Qt::PlainText);
        words->setWordWrap(true);
        h->addWidget(words, 1);
        if (!f.pending() && f.askAgainAt <= now) {
            auto* again = new QPushButton(tr("Offer Again"), row);
            again->setObjectName(QStringLiteral("offerAgain:") + f.id);
            const QString slug = f.club.slug;
            connect(again, &QPushButton::clicked, this, [this, slug] { offerTo(slug); });
            h->addWidget(again);
        }
        auto* back = new QPushButton(f.pending() ? tr("Take Back") : tr("Remove"), row);
        back->setObjectName(QStringLiteral("withdrawOffer:") + f.id);
        connect(back, &QPushButton::clicked, this, [this, f] { withdraw(f); });
        h->addWidget(back);
        offers_->addWidget(row);
    }
    offersHost_->setVisible(!s.offers.isEmpty());
}

void ModuleShareDialog::offerTo(const QString& slugOrLink) {
    error_->hide();
    said_->hide();
    library_.api().shareClubModule(id_, slugOrLink, sync::whileAlive(this, [this](const sync::ClubShareResult& r) {
        const QString who = r.name.isEmpty() ? tr("the club at %1").arg(library_.api().url(QStringLiteral("/orgs/") + r.slug).toString()) : r.name;
        said_->setText(r.offered() ? tr("Offered “%1” to %2. It's shared once their Admins or Managers accept it.").arg(title_, who)
                                   : tr("“%1” is shared with %2.").arg(title_, who));
        said_->show();
        otherClub_->clear();
        reload();
        library_.refresh();
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
        fail(r);
        reload();
    }));
}

void ModuleShareDialog::withdraw(const sync::ClubOffer& offer) {
    error_->hide();
    library_.api().withdrawClubOffer(id_, offer.id, sync::whileAlive(this, [this] {
        reload();
        library_.refresh();
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
        fail(r);
        reload();
    }));
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

    if (s.canShareWithOtherClubs)
        note_->setText(tr("This is %1's module. Partner clubs get it straight away. Any other club must accept it first. "
                          "Clubs you share with can place it in their layouts and get its updates; they can't change its parts.")
                           .arg(s.club.name));
    else if (!s.canShareWithClubs) note_->setText(tr("This is a club's module: its members already have it."));
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
    showClubToClub(s);
    pick_->clear();
    pick_->addItem(tr("Pick a club…"), QString());
    for (const auto& c : std::as_const(open)) pick_->addItem(c.name, c.slug);
    share_->setEnabled(false);

    for (const auto& c : s.clubs) {
        auto* row = new QWidget(clubsHost_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLabel(tr("%1 %2 · can place it").arg(c.name, viaWords(c.via)), row);
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
