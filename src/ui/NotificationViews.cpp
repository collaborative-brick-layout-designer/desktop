#include "NotificationViews.h"

#include "ConfirmDialog.h"
#include "help/HelpButton.h"
#include "tours/Tours.h"

#include "../sync/WhileAlive.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStyleOptionToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

// ---------- Words -------------------------------------------------------------

QString NotificationWords::outcome(const sync::NotificationItem& n) {
    const QString who = !n.outcomeBy ? QString() : n.outcomeBy->you ? tr("You") : n.outcomeBy->name.isEmpty() ? tr("Someone") : n.outcomeBy->name;
    const QString someone = who.isEmpty() ? tr("Someone") : who;
    if (n.outcome == QLatin1String("accepted")) {
        if (n.type == QLatin1String("club.join_request"))
            return n.outcomeBy && n.outcomeBy->you ? tr("You let them in") : tr("%1 let them in").arg(someone);
        return n.outcomeBy && n.outcomeBy->you ? tr("You accepted") : tr("%1 accepted").arg(someone);
    }
    if (n.outcome == QLatin1String("declined"))
        return n.outcomeBy && n.outcomeBy->you ? tr("You declined") : tr("%1 declined").arg(someone);
    if (n.outcome == QLatin1String("withdrawn")) return tr("Taken back, so nothing to answer");
    if (n.outcome == QLatin1String("gone")) return tr("No longer waiting");
    if (n.outcome == QLatin1String("acknowledged")) return tr("You understood");
    return {};
}

QString NotificationWords::timeAgo(qint64 ms, qint64 nowMs) {
    const qint64 s = std::max<qint64>(0, (nowMs - ms + 500) / 1000);
    if (s < 60) return tr("just now");
    const qint64 m = (s + 30) / 60;
    if (m < 60) return tr("%1 min ago").arg(m);
    const qint64 h = (m + 30) / 60;
    if (h < 24) return tr("%1 h ago").arg(h);
    const qint64 d = (h + 12) / 24;
    if (d < 7) return tr("%n day(s) ago", nullptr, static_cast<int>(d));
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).date(), QLocale::ShortFormat);
}

QString NotificationWords::category(const QString& c) {
    if (c == QLatin1String("moderation")) return tr("Warnings and notes");
    if (c == QLatin1String("clubs")) return tr("Clubs");
    if (c == QLatin1String("sharing")) return tr("Sharing");
    if (c == QLatin1String("shared_modules")) return tr("Shared modules");
    if (c == QLatin1String("account")) return tr("Your account");
    return c;
}

QString NotificationWords::badge(int n) {
    if (n <= 0) return {};
    return n > 99 ? QStringLiteral("99+") : QString::number(n);
}

QString NotificationWords::acceptLabel(const sync::NotificationItem& n) {
    return n.type == QLatin1String("club.join_request") ? tr("Let them in") : tr("Accept");
}

bool NotificationWords::declineNeedsConfirm(const QString& type) { return type == QLatin1String("club.join_request"); }

ConfirmOptions NotificationWords::declineQuestion() {
    ConfirmOptions o;
    o.title = tr("Decline this request?");
    o.removes = tr("The request to join is turned down.");
    o.keeps = tr("They can ask again later.");
    o.confirmLabel = tr("Decline");
    return o;
}

QString NotificationPlace::openLabel() const {
    switch (kind) {
    case Layout: return NotificationWords::tr("Open the layout");
    case Module: return NotificationWords::tr("Show the module");
    case Club: return NotificationWords::tr("Show the club");
    case Clubs: return NotificationWords::tr("Show your clubs");
    case Web: return NotificationWords::tr("Open on the website");
    case None: break;
    }
    return {};
}

NotificationPlace placeFor(const sync::NotificationItem& n) {
    NotificationPlace p;
    if (n.waits() || n.link.isEmpty()) return p;
    const QString path = n.link.section(QLatin1Char('?'), 0, 0).section(QLatin1Char('#'), 0, 0);
    static const QRegularExpression editor(QStringLiteral("^/editor/([^/]+)$"));
    static const QRegularExpression module(QStringLiteral("^/modules/([^/]+)$"));
    static const QRegularExpression club(QStringLiteral("^/orgs/([^/]+)(/admin)?$"));
    const auto inEditor = editor.match(path);
    const auto inModule = module.match(path);
    const auto inClub = club.match(path);
    if (inEditor.hasMatch()) {
        p.kind = NotificationPlace::Layout;
        p.id = inEditor.captured(1);
    } else if (inModule.hasMatch()) {
        p.kind = NotificationPlace::Module;
        p.id = inModule.captured(1);
    } else if (inClub.hasMatch()) {
        p.kind = NotificationPlace::Club;
        p.id = inClub.captured(1);
    } else if (path == QLatin1String("/orgs")) {
        p.kind = NotificationPlace::Clubs;
    } else if (path == QLatin1String("/notifications")) {
        // Already here.
        return p;
    } else {
        p.kind = NotificationPlace::Web;
    }
    p.link = n.link;
    return p;
}

// ---------- One item ----------------------------------------------------------

std::function<bool(QWidget*, const ConfirmOptions&)> NotificationRow::confirm = [](QWidget* parent, const ConfirmOptions& o) {
    return ConfirmDialog::ask(parent, o);
};

namespace {

QLabel* plainLabel(const QString& text, QWidget* parent, bool muted = false) {
    auto* l = new QLabel(text, parent);
    l->setTextFormat(Qt::PlainText);  // from the server: never markup
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (muted) l->setProperty("muted", true);
    return l;
}

QString clipped(const QString& text, int max) {
    return text.size() <= max ? text : text.left(max - 1).trimmed() + QStringLiteral("…");
}

}  // namespace

NotificationRow::NotificationRow(sync::NotificationCenter& center, const QUrl& server, const sync::NotificationItem& n,
                                 bool compact, const QString& serverLabel, bool answeredFirstElsewhere, QWidget* parent)
    : QFrame(parent), center_(center), server_(server), item_(n), compact_(compact), serverLabel_(serverLabel),
      already_(answeredFirstElsewhere) {
    setObjectName(QStringLiteral("notificationRow:") + n.id);
    setProperty("libraryRow", true);
    build();
}

void NotificationRow::build() {
    if (QLayout* old = layout()) {
        while (QLayoutItem* it = old->takeAt(0)) {
            if (QWidget* w = it->widget()) w->deleteLater();
            else if (QLayout* l = it->layout()) {
                while (QLayoutItem* inner = l->takeAt(0)) {
                    if (QWidget* child = inner->widget()) child->deleteLater();
                    delete inner;
                }
            }
            delete it;
        }
        delete old;
    }
    busyButtons_.clear();
    const sync::NotificationItem& n = item_;
    setProperty("unread", !n.read());
    setProperty("warning", n.isWarning() && !n.done());
    setAccessibleName(n.read() ? n.title : tr("%1 (new)").arg(n.title));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(8, 6, 6, 6);
    h->setSpacing(8);

    auto* dot = new QLabel(this);
    dot->setObjectName(QStringLiteral("notificationDot"));
    dot->setFixedSize(10, 10);
    if (!n.read()) {
        QPixmap pm(10 * devicePixelRatioF(), 10 * devicePixelRatioF());
        pm.setDevicePixelRatio(devicePixelRatioF());
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::Highlight));
        p.drawEllipse(QRectF(1, 1, 8, 8));
        p.end();
        dot->setPixmap(pm);
        dot->setToolTip(tr("New"));
    }
    h->addWidget(dot, 0, Qt::AlignTop);

    auto* col = new QVBoxLayout();
    col->setSpacing(2);
    auto* title = plainLabel(n.title, this);
    title->setObjectName(QStringLiteral("notificationTitle"));
    QFont f = title->font();
    f.setBold(!n.read());
    title->setFont(f);
    col->addWidget(title);
    if (!n.body.isEmpty()) {
        auto* body = plainLabel(compact_ ? clipped(n.body, 160) : n.body, this, true);
        body->setObjectName(QStringLiteral("notificationBody"));
        col->addWidget(body);
    }
    QStringList meta{ NotificationWords::timeAgo(n.createdAt, QDateTime::currentMSecsSinceEpoch()) };
    if (n.club && !n.club->name.isEmpty()) meta << n.club->name;
    if (!serverLabel_.isEmpty()) meta << serverLabel_;
    auto* when = plainLabel(meta.join(QStringLiteral(" · ")), this, true);
    when->setObjectName(QStringLiteral("notificationMeta"));
    col->addWidget(when);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    if (n.waits()) {
        auto* accept = new QPushButton(NotificationWords::acceptLabel(n), this);
        accept->setObjectName(QStringLiteral("notificationAccept"));
        accept->setProperty("accent", true);
        accept->setAccessibleName(tr("%1: %2").arg(NotificationWords::acceptLabel(n), n.title));
        connect(accept, &QPushButton::clicked, this, [this] { act(QStringLiteral("accept")); });
        buttons->addWidget(accept);
        busyButtons_ << accept;
        if (n.actions.contains(QLatin1String("decline"))) {
            auto* decline = new QPushButton(tr("Decline"), this);
            decline->setObjectName(QStringLiteral("notificationDecline"));
            decline->setAccessibleName(tr("Decline: %1").arg(n.title));
            connect(decline, &QPushButton::clicked, this, [this] {
                if (NotificationWords::declineNeedsConfirm(item_.type) && !(confirm && confirm(this, NotificationWords::declineQuestion())))
                    return;
                act(QStringLiteral("decline"));
            });
            buttons->addWidget(decline);
            busyButtons_ << decline;
        }
    }
    if (n.needsAcknowledge()) {
        auto* ack = new QPushButton(tr("I understand"), this);
        ack->setObjectName(QStringLiteral("notificationAcknowledge"));
        ack->setProperty("accent", true);
        connect(ack, &QPushButton::clicked, this, [this] { acknowledge(); });
        buttons->addWidget(ack);
        busyButtons_ << ack;
    }
    NotificationPlace place = placeFor(n);
    if (!resultLayoutId_.isEmpty()) {
        place.kind = NotificationPlace::Layout;
        place.id = resultLayoutId_;
    }
    if (place.kind != NotificationPlace::None) {
        auto* open = new QPushButton(place.openLabel(), this);
        open->setObjectName(QStringLiteral("notificationOpen"));
        if (place.kind == NotificationPlace::Web) open->setToolTip(tr("This one has no place in the app: it opens on the website."));
        connect(open, &QPushButton::clicked, this, [this, place] {
            if (!item_.read()) setRead(true);
            emit openRequested(server_, item_, place);
        });
        buttons->addWidget(open);
    }
    if (buttons->count() > 0) {
        buttons->addStretch(1);
        col->addLayout(buttons);
    } else {
        delete buttons;
    }
    if (const QString done = NotificationWords::outcome(n); !done.isEmpty()) {
        auto* out = plainLabel(already_ ? tr("%1 (someone else answered first)").arg(done) : done, this, true);
        out->setObjectName(QStringLiteral("notificationOutcome"));
        QFont of = out->font();
        of.setBold(true);
        out->setFont(of);
        out->setAccessibleDescription(tr("What happened"));
        col->addWidget(out);
    }
    error_ = plainLabel({}, this);
    error_->setObjectName(QStringLiteral("notificationError"));
    error_->setProperty("error", true);
    error_->hide();
    col->addWidget(error_);
    h->addLayout(col, 1);

    auto* toggle = new QToolButton(this);
    toggle->setObjectName(QStringLiteral("notificationReadToggle"));
    toggle->setAutoRaise(true);
    toggle->setText(n.read() ? QStringLiteral("○") : QStringLiteral("✓"));
    const QString tip = n.read() ? tr("Mark as unread") : tr("Mark as read");
    toggle->setToolTip(tip);
    toggle->setAccessibleName(n.read() ? tr("Mark “%1” as unread").arg(n.title) : tr("Mark “%1” as read").arg(n.title));
    connect(toggle, &QToolButton::clicked, this, [this] { setRead(!item_.read()); });
    h->addWidget(toggle, 0, Qt::AlignTop);
    style()->unpolish(this);
    style()->polish(this);
}

void NotificationRow::setBusy(bool busy) {
    for (QPushButton* b : std::as_const(busyButtons_))
        if (b) b->setEnabled(!busy);
}

void NotificationRow::showError(const QString& text) {
    if (!error_) return;
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
}

void NotificationRow::setRead(bool read) {
    sync::NotificationApi* api = center_.api(server_);
    if (!api) return;
    // Shown at once; the server's answer (and its hint) follows.
    item_.readAt = read ? QDateTime::currentMSecsSinceEpoch() : 0;
    build();
    const QUrl server = server_;
    sync::NotificationCenter* center = &center_;
    api->setRead(item_.id, read, sync::whileAlive(this, [center, server] { center->changedHere(server); }),
                 sync::whileAlive(this, [this, read](const sync::ServerRefusal& r) {
                     item_.readAt = read ? 0 : QDateTime::currentMSecsSinceEpoch();
                     build();
                     showError(sync::describe(r));
                 }));
}

void NotificationRow::act(const QString& action) {
    sync::NotificationApi* api = center_.api(server_);
    if (!api) return;
    setBusy(true);
    showError({});
    const QUrl server = server_;
    sync::NotificationCenter* center = &center_;
    api->act(item_.id, action,
             sync::whileAlive(this, [this, center, server](const sync::NotificationActResult& r) {
                 if (r.item) item_ = *r.item;
                 else {
                     item_.outcome = r.outcome;
                     item_.actions.clear();
                     item_.doneAt = QDateTime::currentMSecsSinceEpoch();
                 }
                 already_ = r.already;
                 resultLayoutId_ = r.result.value(QLatin1String("layoutId")).toString();
                 build();
                 emit answered(item_.id, r.already);
                 center->changedHere(server);
             }),
             sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
                 setBusy(false);
                 showError(r.message.isEmpty() ? sync::describe(r) : r.message);
             }));
}

void NotificationRow::acknowledge() {
    sync::NotificationApi* api = center_.api(server_);
    if (!api) return;
    setBusy(true);
    showError({});
    const QUrl server = server_;
    sync::NotificationCenter* center = &center_;
    api->acknowledge(item_.id,
                     sync::whileAlive(this, [this, center, server] {
                         item_.actions.clear();
                         item_.outcome = QStringLiteral("acknowledged");
                         item_.doneAt = item_.readAt = QDateTime::currentMSecsSinceEpoch();
                         build();
                         center->changedHere(server);
                     }),
                     sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
                         setBusy(false);
                         showError(sync::describe(r));
                     }));
}

// ---------- The list ----------------------------------------------------------

NotificationList::NotificationList(sync::NotificationCenter& center, bool compact, QWidget* parent)
    : QWidget(parent), center_(center), compact_(compact) {
    setObjectName(QStringLiteral("notificationList"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);

    auto* filters = new QHBoxLayout();
    filters->setSpacing(6);
    all_ = new QToolButton(this);
    all_->setObjectName(QStringLiteral("notificationsAll"));
    all_->setText(tr("All"));
    all_->setCheckable(true);
    all_->setChecked(true);
    unread_ = new QToolButton(this);
    unread_->setObjectName(QStringLiteral("notificationsUnread"));
    unread_->setText(tr("Unread"));
    unread_->setCheckable(true);
    for (QToolButton* b : { all_, unread_ }) {
        b->setAutoExclusive(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(b, &QToolButton::toggled, this, [this](bool on) {
            if (on) fetch(false);
        });
        filters->addWidget(b);
    }
    types_ = new QComboBox(this);
    types_->setObjectName(QStringLiteral("notificationType"));
    types_->setAccessibleName(tr("Type of notification"));
    types_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    types_->setMinimumContentsLength(12);
    connect(types_, &QComboBox::activated, this, [this] { fetch(false); });
    filters->addWidget(types_, 1);
    servers_ = new QComboBox(this);
    servers_->setObjectName(QStringLiteral("notificationServer"));
    servers_->setAccessibleName(tr("Server"));
    connect(servers_, &QComboBox::activated, this, [this] { fetch(false); });
    filters->addWidget(servers_);
    readAll_ = new QPushButton(tr("Mark all as read"), this);
    readAll_->setObjectName(QStringLiteral("notificationsReadAll"));
    readAll_->setFlat(true);
    connect(readAll_, &QPushButton::clicked, this, [this] {
        for (const QUrl& u : picked()) {
            sync::NotificationApi* api = center_.api(u);
            if (!api) continue;
            sync::NotificationCenter* bell = &center_;
            api->readAll({}, sync::whileAlive(this, [bell, u](int) { bell->changedHere(u); }),
                         sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
                             status_->setText(sync::describe(r));
                             status_->show();
                         }));
        }
        // Shown at once.
        for (Entry& e : entries_)
            if (!e.item.read()) e.item.readAt = QDateTime::currentMSecsSinceEpoch();
        rebuild();
    });
    if (compact_) {
        col->addLayout(filters);
        auto* second = new QHBoxLayout();
        second->addStretch(1);
        second->addWidget(readAll_);
        col->addLayout(second);
    } else {
        search_ = new QLineEdit(this);
        search_->setObjectName(QStringLiteral("notificationSearch"));
        search_->setPlaceholderText(tr("Search notifications"));
        search_->setClearButtonEnabled(true);
        search_->setAccessibleName(tr("Search notifications"));
        typing_ = new QTimer(this);
        typing_->setSingleShot(true);
        typing_->setInterval(350);
        connect(typing_, &QTimer::timeout, this, [this] { fetch(false); });
        connect(search_, &QLineEdit::textChanged, typing_, qOverload<>(&QTimer::start));
        auto* top = new QHBoxLayout();
        top->addWidget(search_, 1);
        top->addWidget(readAll_);
        col->addLayout(top);
        col->addLayout(filters);
    }

    status_ = plainLabel({}, this, true);
    status_->setObjectName(QStringLiteral("notificationsStatus"));
    status_->hide();
    col->addWidget(status_);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    host_ = new QWidget(scroll);
    rowsLayout_ = new QVBoxLayout(host_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(4);
    empty_ = plainLabel({}, host_, true);
    empty_->setObjectName(QStringLiteral("notificationsEmpty"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->hide();
    rowsLayout_->addWidget(empty_);
    rowsLayout_->addStretch(1);
    scroll->setWidget(host_);
    col->addWidget(scroll, 1);

    more_ = new QPushButton(tr("Show more"), this);
    more_->setObjectName(QStringLiteral("notificationsMore"));
    more_->hide();
    connect(more_, &QPushButton::clicked, this, [this] { fetch(true); });
    col->addWidget(more_, 0, Qt::AlignHCenter);

    soon_ = new QTimer(this);
    soon_->setSingleShot(true);
    soon_->setInterval(150);
    connect(soon_, &QTimer::timeout, this, [this] { fetch(false); });
    connect(&center_, &sync::NotificationCenter::changed, this, [this] { reload(); });
    connect(&center_, &sync::NotificationCenter::sourcesChanged, this, [this] {
        refillPickers();
        reload();
    });
    refillPickers();
}

void NotificationList::reload() {
    stale_ = true;
    // The server may have said its types meanwhile.
    refillPickers();
    if (isVisible()) soon_->start();
}

void NotificationList::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refillPickers();
    if (stale_) soon_->start();
}

void NotificationList::setServerFilter(const QUrl& server) {
    refillPickers();
    const int at = servers_->findData(server.isValid() ? sync::NotificationCenter::keyOf(server) : QString());
    if (at >= 0) servers_->setCurrentIndex(at);
    reload();
}

void NotificationList::setTypeFilter(const QString& value) {
    refillPickers();
    int at = types_->findData(value);
    if (at < 0 && !value.isEmpty()) {
        types_->addItem(value, value);
        at = types_->count() - 1;
    }
    types_->setCurrentIndex(std::max(0, at));
    reload();
}

void NotificationList::setUnreadOnly(bool unread) {
    (unread ? unread_ : all_)->setChecked(true);
    reload();
}

void NotificationList::refillPickers() {
    // Servers: only with more than one that has notifications.
    const QList<QUrl> servers = center_.supported();
    {
        const QString was = servers_->currentData().toString();
        const QSignalBlocker block(servers_);
        servers_->clear();
        servers_->addItem(tr("Every server"), QString());
        for (const QUrl& u : servers)
            if (const auto* s = center_.source(u)) servers_->addItem(s->label, sync::NotificationCenter::keyOf(u));
        servers_->setCurrentIndex(std::max(0, servers_->findData(was)));
        servers_->setVisible(servers.size() > 1);
    }
    // Types: as the server names them, under their categories.
    sync::NotificationTypes known;
    for (const QUrl& u : servers)
        if (const auto* s = center_.source(u); s && !s->types.isEmpty()) {
            known = s->types;
            break;
        }
    const QString was = types_->currentData().toString();
    const QSignalBlocker block(types_);
    types_->clear();
    types_->addItem(tr("Every type"), QString());
    for (const QString& c : known.categories) {
        types_->addItem(tr("All: %1").arg(NotificationWords::category(c)), QStringLiteral("cat:") + c);
        for (const auto& t : known.types)
            if (t.category == c) types_->addItem(QStringLiteral("    ") + t.label, t.type);
    }
    int at = types_->findData(was);
    if (at < 0 && !was.isEmpty()) {
        types_->addItem(was, was);
        at = types_->count() - 1;
    }
    types_->setCurrentIndex(std::max(0, at));
}

QList<QUrl> NotificationList::picked() const {
    const QString key = servers_->currentData().toString();
    QList<QUrl> out;
    for (const QUrl& u : center_.supported())
        if (key.isEmpty() || sync::NotificationCenter::keyOf(u) == key) out << u;
    return out;
}

void NotificationList::fetch(bool more) {
    stale_ = false;
    const QList<QUrl> servers = picked();
    const int gen = ++generation_;
    ++loads_;
    if (!more) cursors_.clear();
    sync::NotificationQuery base;
    base.unreadOnly = unread_->isChecked();
    const QString type = types_->currentData().toString();
    if (type.startsWith(QLatin1String("cat:"))) base.category = type.mid(4);
    else if (!type.isEmpty()) base.types = { type };
    if (search_) base.search = search_->text();
    base.limit = compact_ ? 25 : 30;
    auto waiting = std::make_shared<int>(0);
    auto got = std::make_shared<QList<Entry>>();
    auto failed = std::make_shared<QString>();
    const auto settle = [this, gen, waiting, got, failed, more] {
        if (gen != generation_ || --*waiting > 0) return;
        if (!more) entries_.clear();
        QSet<QString> have;
        for (const Entry& e : std::as_const(entries_)) have.insert(sync::NotificationCenter::keyOf(e.server) + e.item.id);
        for (const Entry& e : std::as_const(*got))
            if (!have.contains(sync::NotificationCenter::keyOf(e.server) + e.item.id)) entries_ << e;
        std::stable_sort(entries_.begin(), entries_.end(),
                         [](const Entry& a, const Entry& b) { return a.item.createdAt > b.item.createdAt; });
        status_->setText(*failed);
        status_->setVisible(!failed->isEmpty());
        rebuild();
    };
    for (const QUrl& u : servers) {
        sync::NotificationApi* api = center_.api(u);
        if (!api) continue;
        const QString key = sync::NotificationCenter::keyOf(u);
        sync::NotificationQuery q = base;
        if (more) {
            if (!cursors_.contains(key)) continue;
            q.cursor = cursors_.value(key);
        }
        ++*waiting;
        api->list(q,
                  sync::whileAlive(this, [this, gen, u, key, got, settle](const sync::NotificationPage& page) {
                      if (gen != generation_) return;
                      for (const auto& n : page.items) *got << Entry{ u, n };
                      if (page.nextCursor.isEmpty()) cursors_.remove(key);
                      else cursors_.insert(key, page.nextCursor);
                      settle();
                  }),
                  sync::whileAlive(this, [this, gen, failed, settle, u](const sync::ServerRefusal& r) {
                      if (gen != generation_) return;
                      const auto* s = center_.source(u);
                      *failed = tr("%1: %2").arg(s ? s->label : u.host(), sync::describe(r));
                      settle();
                  }));
    }
    if (*waiting == 0) {
        ++*waiting;
        settle();
    }
}

void NotificationList::rebuild() {
    // Everything but the empty line and the stretch goes.
    for (int i = rowsLayout_->count() - 1; i >= 0; --i) {
        QLayoutItem* it = rowsLayout_->itemAt(i);
        if (!it->widget() || it->widget() == empty_) continue;
        QWidget* w = it->widget();
        rowsLayout_->takeAt(i);
        w->hide();
        w->deleteLater();
        delete it;
    }
    const bool several = center_.supported().size() > 1 && servers_->currentData().toString().isEmpty();
    QList<sync::NotificationItem> items;
    QHash<QString, QUrl> serverOf;
    for (const Entry& e : std::as_const(entries_)) {
        sync::NotificationItem n = e.item;
        // Groups never reach across servers.
        if (!n.groupKey.isEmpty()) n.groupKey = sync::NotificationCenter::keyOf(e.server) + QLatin1Char('|') + n.groupKey;
        serverOf.insert(n.id, e.server);
        items << n;
    }
    const auto makeRow = [this, several, &serverOf](const sync::NotificationItem& shown, QWidget* parent) {
        sync::NotificationItem n = shown;
        n.groupKey = n.groupKey.section(QLatin1Char('|'), 1);
        const QUrl server = serverOf.value(n.id);
        const auto* s = center_.source(server);
        auto* row = new NotificationRow(center_, server, n, compact_, several && s ? s->label : QString(),
                                        already_.contains(n.id), parent);
        connect(row, &NotificationRow::openRequested, this, &NotificationList::openRequested);
        connect(row, &NotificationRow::answered, this, [this](const QString& id, bool already) {
            if (already) already_.insert(id);
        });
        return row;
    };
    int at = 0;
    for (const sync::NotificationGroup& g : sync::groupNotifications(items)) {
        rowsLayout_->insertWidget(at++, makeRow(g.head, host_));
        if (g.rest.isEmpty()) continue;
        auto* box = new QWidget(host_);
        box->setObjectName(QStringLiteral("notificationGroup:") + g.head.id);
        auto* bcol = new QVBoxLayout(box);
        bcol->setContentsMargins(24, 0, 0, 4);
        bcol->setSpacing(4);
        int unreadRest = 0;
        for (const auto& n : g.rest) unreadRest += n.read() ? 0 : 1;
        const bool open = openGroups_.contains(g.head.groupKey);
        auto* toggle = new QPushButton(box);
        toggle->setObjectName(QStringLiteral("notificationGroupToggle"));
        toggle->setFlat(true);
        toggle->setText(open ? tr("Show fewer")
                             : unreadRest > 0 ? tr("%n more like this (new)", nullptr, static_cast<int>(g.rest.size()))
                                              : tr("%n more like this", nullptr, static_cast<int>(g.rest.size())));
        bcol->addWidget(toggle, 0, Qt::AlignLeft);
        const QString key = g.head.groupKey;
        connect(toggle, &QPushButton::clicked, this, [this, key] {
            if (!openGroups_.remove(key)) openGroups_.insert(key);
            QTimer::singleShot(0, this, &NotificationList::rebuild);
        });
        if (open)
            for (const auto& n : g.rest) bcol->addWidget(makeRow(n, box));
        rowsLayout_->insertWidget(at++, box);
    }
    int unread = 0;
    for (const Entry& e : std::as_const(entries_)) unread += e.item.read() ? 0 : 1;
    int totalUnread = 0;
    for (const QUrl& u : picked())
        if (const auto* s = center_.source(u)) totalUnread += s->counts.unread;
    unread_->setText(totalUnread > 0 ? tr("Unread (%1)").arg(NotificationWords::badge(totalUnread)) : tr("Unread"));
    readAll_->setEnabled(totalUnread > 0 || unread > 0);
    if (center_.supported().isEmpty())
        empty_->setText(center_.servers().isEmpty()
                            ? tr("Sign in to a server to see your notifications: requests, invites and news from your clubs.")
                            : tr("Your server doesn't have notifications yet. Its warnings and notes still show over the map."));
    else if (entries_.isEmpty())
        empty_->setText(unread_->isChecked() ? tr("You're all caught up.")
                        : (search_ && !search_->text().trimmed().isEmpty()) || !types_->currentData().toString().isEmpty()
                            ? tr("No notifications match.")
                            : tr("Nothing yet. Requests, invites and news from your clubs show up here."));
    empty_->setVisible(entries_.isEmpty());
    more_->setVisible(!compact_ && !cursors_.isEmpty());
}

QList<NotificationRow*> NotificationList::rows() const {
    QList<NotificationRow*> out;
    for (auto* r : host_->findChildren<NotificationRow*>())
        if (!r->isHidden() || r->isVisibleTo(host_)) out << r;
    std::stable_sort(out.begin(), out.end(), [](NotificationRow* a, NotificationRow* b) {
        return a->item().createdAt > b->item().createdAt;
    });
    return out;
}

NotificationRow* NotificationList::row(const QString& id) const {
    for (NotificationRow* r : rows())
        if (r->item().id == id) return r;
    return nullptr;
}

// ---------- One club's requests ----------------------------------------------

NotificationRequests::NotificationRequests(sync::NotificationCenter& center, const QUrl& server, const QStringList& types,
                                           const QString& clubSlug, QWidget* parent)
    : QWidget(parent), center_(center), server_(server), types_(types), club_(clubSlug) {
    setObjectName(QStringLiteral("notificationRequests:") + clubSlug);
    rows_ = new QVBoxLayout(this);
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(4);
    connect(&center_, &sync::NotificationCenter::changed, this, [this](const QUrl& u) {
        if (sync::NotificationCenter::keyOf(u) == sync::NotificationCenter::keyOf(server_)) reload();
    });
    reload();
}

QList<NotificationRow*> NotificationRequests::rows() const { return findChildren<NotificationRow*>(); }

void NotificationRequests::reload() {
    sync::NotificationApi* api = center_.api(server_);
    if (!api) return;
    const int gen = ++generation_;
    sync::NotificationQuery q;
    q.types = types_;
    q.limit = 50;
    api->list(q, sync::whileAlive(this, [this, gen](const sync::NotificationPage& page) {
        if (gen != generation_) return;
        // An answered one stays, saying what happened, until it's no longer listed.
        QSet<QString> keep;
        for (NotificationRow* r : rows())
            if (!r->item().waits()) keep.insert(r->item().id);
        while (QLayoutItem* it = rows_->takeAt(0)) {
            if (QWidget* w = it->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete it;
        }
        for (const sync::NotificationItem& n : page.items) {
            if (!n.club || n.club->slug != club_) continue;
            if (!n.waits() && !keep.contains(n.id)) continue;
            auto* row = new NotificationRow(center_, server_, n, true, {}, false, this);
            connect(row, &NotificationRow::openRequested, this, &NotificationRequests::openRequested);
            rows_->addWidget(row);
        }
    }), {});
}

// ---------- The bell ----------------------------------------------------------

namespace {

QIcon bellIcon(const QColor& ink) {
    QIcon icon;
    for (int size : { 20, 40 }) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 24.0, size / 24.0);
        QPen pen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // The web's bell (apps/web/src/notifications/NotificationBell.tsx).
        QPainterPath body;
        body.moveTo(6, 8);
        body.arcTo(QRectF(6, 2, 12, 12), 180, -180);
        body.cubicTo(18, 15, 21, 17, 21, 17);
        body.lineTo(3, 17);
        body.cubicTo(3, 17, 6, 15, 6, 8);
        p.drawPath(body);
        QPainterPath clapper;
        clapper.moveTo(10.3, 21);
        clapper.quadTo(12, 22.6, 13.7, 21);
        p.drawPath(clapper);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

}  // namespace

NotificationBell::NotificationBell(sync::NotificationCenter& center, QWidget* parent) : QToolButton(parent), center_(center) {
    // "tool.": the toolbar shows it as an icon only when the window is narrow.
    setObjectName(QStringLiteral("tool.notifications"));
    setText(tr("Notifications"));
    setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    setIcon(bellIcon(palette().color(QPalette::ButtonText)));
    tours::tag(this, QStringLiteral("notifications.bell"));
    connect(&center_, &sync::NotificationCenter::countsChanged, this, &NotificationBell::updateCount);
    connect(&center_, &sync::NotificationCenter::sourcesChanged, this, &NotificationBell::updateCount);
    updateCount();
}

QString NotificationBell::badgeText() const { return NotificationWords::badge(center_.totalUnread()); }

void NotificationBell::updateCount() {
    const int n = center_.totalUnread();
    const QString label = n > 0 ? tr("Notifications, %n unread", nullptr, n) : tr("Notifications");
    setAccessibleName(label);
    QString tip = n > 0 ? tr("%n notification(s) you haven't read", nullptr, n) : tr("Your notifications: requests, invites and news");
    if (const int waiting = center_.totalWaiting(); waiting > 0) tip += QLatin1Char('\n') + tr("%n wait(s) for your answer", nullptr, waiting);
    setToolTip(tip);
    update();
}

void NotificationBell::changeEvent(QEvent* e) {
    QToolButton::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        setIcon(bellIcon(palette().color(QPalette::ButtonText)));
}

void NotificationBell::paintEvent(QPaintEvent* e) {
    QToolButton::paintEvent(e);
    const QString badge = badgeText();
    if (badge.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(std::max(6.5, f.pointSizeF() * 0.75));
    p.setFont(f);
    const QFontMetrics fm(f);
    const int h = fm.height() + 2;
    const int w = std::max(h, fm.horizontalAdvance(badge) + 8);
    // Over the bell's top right.
    const int iconRight = width() / 2 + iconSize().width() / 2;
    QRect r(std::min(width() - w - 1, iconRight - w / 2), 1, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawRoundedRect(r, h / 2.0, h / 2.0);
    p.setPen(palette().color(QPalette::HighlightedText));
    p.drawText(r, Qt::AlignCenter, badge);
}

// ---------- The panel ---------------------------------------------------------

NotificationPanel::NotificationPanel(sync::NotificationCenter& center, QWidget* parent) : QFrame(parent, Qt::Popup) {
    setObjectName(QStringLiteral("notificationPanel"));
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAccessibleName(tr("Notifications"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(10, 8, 10, 8);
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("Notifications"), this);
    title->setProperty("dialogTitle", true);
    QFont f = title->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    title->setFont(f);
    head->addWidget(title, 1);
    head->addWidget(new help::HelpButton(QStringLiteral("notifications.bell"), this));
    auto* shut = new QToolButton(this);
    shut->setObjectName(QStringLiteral("notificationPanelClose"));
    shut->setAutoRaise(true);
    shut->setText(QStringLiteral("✕"));
    shut->setAccessibleName(tr("Close notifications"));
    connect(shut, &QToolButton::clicked, this, &QWidget::close);
    head->addWidget(shut);
    col->addLayout(head);
    list_ = new NotificationList(center, true, this);
    col->addWidget(list_, 1);
    auto* all = new QPushButton(tr("See all notifications"), this);
    all->setObjectName(QStringLiteral("notificationsSeeAll"));
    all->setFlat(true);
    connect(all, &QPushButton::clicked, this, [this] {
        close();
        emit seeAllRequested();
    });
    col->addWidget(all, 0, Qt::AlignHCenter);
    connect(list_, &NotificationList::openRequested, this, [this] { close(); });
    resize(440, 560);
}

void NotificationPanel::popUp(QWidget* anchor) {
    QPoint at = anchor ? anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height())) : QCursor::pos();
    const QScreen* screen = anchor ? anchor->screen() : QGuiApplication::primaryScreen();
    if (screen) {
        const QRect area = screen->availableGeometry();
        const int h = std::min(height(), area.height() - 20);
        resize(width(), h);
        at.setX(std::clamp(at.x(), area.left() + 4, std::max(area.left() + 4, area.right() - width() - 4)));
        at.setY(std::clamp(at.y(), area.top() + 4, std::max(area.top() + 4, area.bottom() - h - 4)));
    }
    move(at);
    show();
    raise();
    list_->setFocus();
}

// ---------- The window --------------------------------------------------------

NotificationsWindow::NotificationsWindow(sync::NotificationCenter& center, QWidget* parent) : QWidget(parent, Qt::Window) {
    setObjectName(QStringLiteral("notificationsWindow"));
    setWindowTitle(tr("Notifications"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 12, 16, 12);
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("Notifications"), this);
    QFont f = title->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.3);
    title->setFont(f);
    head->addWidget(title);
    head->addWidget(new help::HelpButton(QStringLiteral("notifications.bell"), this));
    head->addStretch(1);
    col->addLayout(head);
    list_ = new NotificationList(center, false, this);
    col->addWidget(list_, 1);
    resize(640, 720);
}

}  // namespace bld::ui
