#include "NotificationSettingsPanel.h"

#include "DesktopNotifier.h"
#include "NotificationViews.h"
#include "help/HelpButton.h"

#include "../sync/WhileAlive.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace bld::ui {

namespace {

QLabel* muted(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(true);
    l->setProperty("muted", true);
    return l;
}

QLabel* strong(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

QTimeEdit* timeEdit(const QString& hhmm, const QString& name, QWidget* parent) {
    auto* t = new QTimeEdit(QTime::fromString(hhmm, QStringLiteral("HH:mm")), parent);
    t->setDisplayFormat(QStringLiteral("HH:mm"));
    t->setAccessibleName(name);
    return t;
}

} // namespace

QString NotificationSettingsPanel::computerTimeZone() {
    const QByteArray id = QTimeZone::systemTimeZoneId();
    return id.isEmpty() || !QTimeZone(id).isValid() ? QStringLiteral("UTC") : QString::fromUtf8(id);
}

NotificationSettingsPanel::NotificationSettingsPanel(sync::NotificationCenter* center,
                                                     const std::function<void()>& localChanged,
                                                     QWidget* parent)
    : QWidget(parent), center_(center) {
    setObjectName(QStringLiteral("notificationSettings"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(10);

    // This computer's own pop-ups.
    const bool can = DesktopNotifier::available();
    system_ = new QCheckBox(tr("Show system notifications"), this);
    system_->setObjectName(QLatin1String(DesktopNotifier::kShowKey));
    system_->setChecked(DesktopNotifier::showSetting());
    background_ = new QCheckBox(tr("Only when the app is in the background"), this);
    background_->setObjectName(QLatin1String(DesktopNotifier::kBackgroundOnlyKey));
    background_->setChecked(DesktopNotifier::backgroundOnlySetting());
    background_->setEnabled(system_->isChecked() && can);
    system_->setEnabled(can);
    connect(system_, &QCheckBox::toggled, this, [this, localChanged, can](bool on) {
        QSettings().setValue(QLatin1String(DesktopNotifier::kShowKey), on);
        background_->setEnabled(on && can);
        if (localChanged) localChanged();
    });
    connect(background_, &QCheckBox::toggled, this, [localChanged](bool on) {
        QSettings().setValue(QLatin1String(DesktopNotifier::kBackgroundOnlyKey), on);
        if (localChanged) localChanged();
    });
    col->addWidget(system_);
    auto* bg = new QHBoxLayout();
    bg->setContentsMargins(24, 0, 0, 0);
    bg->addWidget(background_);
    bg->addStretch(1);
    col->addLayout(bg);
    systemNote_ = muted(
        can ? tr("New notifications pop up on this computer while the app runs, as each server's Pop-up "
                 "choices below say, and wait for its quiet hours. Several at once come as one.")
            : tr("This computer has no notification area, so the app can't show system notifications "
                 "here. The bell still counts them."),
        this);
    systemNote_->setObjectName(QStringLiteral("systemNotificationsNote"));
    col->addWidget(systemNote_);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setProperty("muted", true);
    col->addWidget(line);

    // How each server's notifications reach you.
    auto* head = new QHBoxLayout();
    head->addWidget(help::withHelp(strong(tr("How notifications reach you"), this),
                                   QStringLiteral("notifications.settings"), this),
                    1);
    servers_ = new QComboBox(this);
    servers_->setObjectName(QStringLiteral("notificationSettingsServer"));
    servers_->setAccessibleName(tr("Server"));
    connect(servers_, &QComboBox::activated, this, [this] { rebuild(); });
    head->addWidget(servers_);
    col->addLayout(head);
    serverNote_ = muted({}, this);
    serverNote_->setObjectName(QStringLiteral("notificationSettingsNote"));
    col->addWidget(serverNote_);
    body_ = new QWidget(this);
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(0, 0, 0, 0);
    bodyLayout_->setSpacing(10);
    col->addWidget(body_);
    status_ = muted({}, this);
    status_->setObjectName(QStringLiteral("notificationSettingsStatus"));
    status_->setAccessibleDescription(tr("Saved or not"));
    col->addWidget(status_);

    if (center_) {
        connect(center_, &sync::NotificationCenter::settingsChanged, this, [this](const QUrl& u) {
            if (sync::NotificationCenter::keyOf(u) == sync::NotificationCenter::keyOf(current()) && !saving_)
                rebuild();
        });
        connect(center_, &sync::NotificationCenter::sourcesChanged, this,
                &NotificationSettingsPanel::rebuild);
    }
    rebuild();
}

QUrl NotificationSettingsPanel::current() const {
    return servers_->currentData().toUrl();
}

void NotificationSettingsPanel::showServer(const QUrl& server) {
    const int at = servers_->findData(server);
    if (at >= 0) servers_->setCurrentIndex(at);
    rebuild();
}

void NotificationSettingsPanel::save(const QJsonObject& change) {
    if (!center_) return;
    saving_ = true;
    status_->setText(tr("Saving…"));
    center_->saveSettings(current(), change, sync::whileAlive(this, [this](const sync::ServerRefusal* r) {
        saving_ = false;
        status_->setText(r ? (r->message.isEmpty() ? sync::describe(*r) : r->message) : tr("Saved."));
        rebuild();
    }));
}

QWidget* NotificationSettingsPanel::typeRow(const sync::NotificationTypeSetting& t, QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("notificationType:") + t.type);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(12, 2, 4, 2);
    auto* label = new QLabel(t.label, row);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    h->addWidget(label, 1);
    auto* app = new QCheckBox(tr("In the app"), row);
    app->setObjectName(QStringLiteral("app"));
    app->setChecked(t.app);
    app->setEnabled(!t.appLocked);
    app->setAccessibleName(tr("%1: in the app").arg(t.label));
    if (t.appLocked) app->setToolTip(tr("Warnings always show, so you never miss one."));
    const QString type = t.type;
    connect(app, &QCheckBox::toggled, this, [this, type](bool on) {
        save({ { QStringLiteral("types"),
                 QJsonObject{ { type, QJsonObject{ { QStringLiteral("app"), on } } } } } });
    });
    h->addWidget(app);
    if (!t.emailApart.isEmpty()) {
        auto* apart =
            muted(t.emailApart == QLatin1String("invite") ? tr("Emailed with the invite’s link, always.")
                                                          : tr("Always emailed, for your account’s safety."),
                  row);
        apart->setObjectName(QStringLiteral("emailApart"));
        apart->setMinimumWidth(180);
        h->addWidget(apart);
    } else {
        auto* emailLabel = muted(tr("Email"), row);
        h->addWidget(emailLabel);
        auto* email = new QComboBox(row);
        email->setObjectName(QStringLiteral("email"));
        email->setAccessibleName(tr("%1: email").arg(t.label));
        email->addItem(tr("Right away"), QStringLiteral("now"));
        email->addItem(tr("Daily summary"), QStringLiteral("daily"));
        email->addItem(tr("Weekly summary"), QStringLiteral("weekly"));
        email->addItem(tr("Off"), QStringLiteral("off"));
        email->setCurrentIndex(std::max(0, email->findData(t.email)));
        connect(email, &QComboBox::activated, this, [this, type, email] {
            save({ { QStringLiteral("types"),
                     QJsonObject{ { type, QJsonObject{ { QStringLiteral("email"),
                                                         email->currentData().toString() } } } } } });
        });
        h->addWidget(email);
    }
    auto* push = new QCheckBox(tr("Pop-up"), row);
    push->setObjectName(QStringLiteral("push"));
    push->setChecked(t.push);
    push->setAccessibleName(tr("%1: pop-up").arg(t.label));
    connect(push, &QCheckBox::toggled, this, [this, type](bool on) {
        save({ { QStringLiteral("types"),
                 QJsonObject{ { type, QJsonObject{ { QStringLiteral("push"), on } } } } } });
    });
    h->addWidget(push);
    if (t.appLocked) {
        auto* why = muted(tr("Warnings always show, so you never miss one."), row);
        why->setObjectName(QStringLiteral("appLockedNote"));
        auto* wrap = new QWidget(parent);
        auto* v = new QVBoxLayout(wrap);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        row->setParent(wrap);
        v->addWidget(row);
        why->setParent(wrap);
        why->setContentsMargins(12, 0, 0, 0);
        v->addWidget(why);
        return wrap;
    }
    return row;
}

void NotificationSettingsPanel::rebuild() {
    // The servers that have notifications.
    {
        const QUrl was = current();
        const QSignalBlocker block(servers_);
        servers_->clear();
        if (center_)
            for (const QUrl& u : center_->supported())
                if (const auto* s = center_->source(u)) servers_->addItem(s->label, u);
        const int at = servers_->findData(was);
        servers_->setCurrentIndex(at >= 0 ? at : 0);
        servers_->setVisible(servers_->count() > 1);
    }
    while (QLayoutItem* it = bodyLayout_->takeAt(0)) {
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    const QUrl server = current();
    const auto* src = center_ && server.isValid() ? center_->source(server) : nullptr;
    if (!src) {
        serverNote_->setText(
            center_ && !center_->servers().isEmpty()
                ? tr("Your server doesn't have notification settings yet. Choose them on its website.")
                : tr("Sign in to a server to choose how its notifications reach you."));
        serverNote_->show();
        return;
    }
    if (!src->settings) {
        serverNote_->setText(tr("Loading…"));
        serverNote_->show();
        return;
    }
    const sync::NotificationSettings& v = *src->settings;
    // The time zone follows this computer until it's picked (as the web follows the browser).
    if (v.timeZone.isEmpty() && !saving_) {
        QTimer::singleShot(0, this, [this] { save({ { QStringLiteral("timeZone"), computerTimeZone() } }); });
    }
    serverNote_->setText(servers_->count() > 1 ? tr("These are your settings on %1.").arg(src->label)
                                               : QString());
    serverNote_->setVisible(!serverNote_->text().isEmpty());

    QWidget* b = body_;
    if (!v.emailAvailable) {
        auto* l = muted(tr("This site can’t send email right now. Your email choices are kept, and work once "
                           "a site admin "
                           "sets up email."),
                        b);
        l->setObjectName(QStringLiteral("emailUnavailable"));
        bodyLayout_->addWidget(l);
    }
    const auto pause = [this, b](const QString& name, const QString& title, const QString& text,
                                 const char* key, bool on) {
        auto* box = new QWidget(b);
        auto* lines = new QVBoxLayout(box);
        lines->setContentsMargins(0, 0, 0, 0);
        lines->setSpacing(2);
        auto* c = new QCheckBox(title, box);
        c->setObjectName(name);
        c->setChecked(on);
        QFont f = c->font();
        f.setBold(true);
        c->setFont(f);
        connect(c, &QCheckBox::toggled, this, [this, key](bool x) { save({ { QLatin1String(key), x } }); });
        lines->addWidget(c);
        auto* d = muted(text, box);
        d->setContentsMargins(24, 0, 0, 0);
        lines->addWidget(d);
        bodyLayout_->addWidget(box);
    };
    pause(QStringLiteral("pauseEmail"), tr("Pause all email"),
          tr("No notification emails at all, summaries included, until you turn this off. Invites and "
             "account emails still come."),
          "pauseEmail", v.pauseEmail);
    pause(QStringLiteral("pausePush"), tr("Pause all pop-ups"),
          tr("No pop-up notifications on any of your devices until you turn this off."), "pausePush",
          v.pausePush);

    bodyLayout_->addWidget(strong(tr("What you hear about"), b));
    for (const QString& c : v.categories) {
        QList<const sync::NotificationTypeSetting*> types;
        for (const auto& t : v.types)
            if (t.category == c) types << &t;
        if (types.isEmpty()) continue;
        int emailed = 0, off = 0;
        for (const auto* t : types) {
            if (t->email != QLatin1String("off") && t->emailApart.isEmpty()) ++emailed;
            if (!t->app) ++off;
        }
        QStringList summary{ tr("%n kind(s)", nullptr, static_cast<int>(types.size())) };
        summary << (emailed ? tr("%1 by email").arg(emailed) : tr("no email"));
        if (off) summary << tr("%1 off in the app").arg(off);
        auto* group = new QFrame(b);
        group->setObjectName(QStringLiteral("notificationCategory:") + c);
        group->setProperty("libraryRow", true);
        auto* gv = new QVBoxLayout(group);
        gv->setContentsMargins(8, 6, 8, 6);
        auto* toggle = new QToolButton(group);
        toggle->setObjectName(QStringLiteral("categoryToggle"));
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setAutoRaise(true);
        toggle->setCheckable(true);
        const bool open = open_.contains(c);
        toggle->setChecked(open);
        toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        toggle->setText(NotificationWords::category(c) + QStringLiteral("  ·  ")
                        + summary.join(QStringLiteral(" · ")));
        toggle->setAccessibleName(NotificationWords::category(c));
        toggle->setAccessibleDescription(summary.join(QStringLiteral(", ")));
        gv->addWidget(toggle);
        auto* list = new QWidget(group);
        auto* lv = new QVBoxLayout(list);
        lv->setContentsMargins(0, 0, 0, 0);
        lv->setSpacing(2);
        for (const auto* t : std::as_const(types)) lv->addWidget(typeRow(*t, list));
        list->setVisible(open);
        gv->addWidget(list);
        connect(toggle, &QToolButton::toggled, this, [this, c, list, toggle](bool on) {
            if (on) open_.insert(c);
            else open_.remove(c);
            list->setVisible(on);
            toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });
        bodyLayout_->addWidget(group);
    }

    // Quiet hours.
    {
        auto* box = new QGroupBox(tr("Quiet hours"), b);
        box->setObjectName(QStringLiteral("quietHours"));
        auto* qv = new QVBoxLayout(box);
        auto* on = new QCheckBox(tr("Quiet hours"), box);
        on->setObjectName(QStringLiteral("quietOn"));
        on->setChecked(v.quietOn);
        connect(on, &QCheckBox::toggled, this, [this](bool x) {
            save({ { QStringLiteral("quiet"), QJsonObject{ { QStringLiteral("on"), x } } } });
        });
        qv->addWidget(on);
        qv->addWidget(muted(
            tr("Emails and pop-ups that come in quiet hours wait until they end. Summaries aren’t held."),
            box));
        auto* times = new QHBoxLayout();
        auto* start = timeEdit(v.quietStart, tr("Quiet hours start"), box);
        start->setObjectName(QStringLiteral("quietStart"));
        auto* end = timeEdit(v.quietEnd, tr("Quiet hours end"), box);
        end->setObjectName(QStringLiteral("quietEnd"));
        start->setEnabled(v.quietOn);
        end->setEnabled(v.quietOn);
        connect(start, &QTimeEdit::editingFinished, this, [this, start] {
            save({ { QStringLiteral("quiet"),
                     QJsonObject{
                         { QStringLiteral("start"), start->time().toString(QStringLiteral("HH:mm")) } } } });
        });
        connect(end, &QTimeEdit::editingFinished, this, [this, end] {
            save({ { QStringLiteral("quiet"),
                     QJsonObject{
                         { QStringLiteral("end"), end->time().toString(QStringLiteral("HH:mm")) } } } });
        });
        times->addWidget(new QLabel(tr("From"), box));
        times->addWidget(start);
        times->addWidget(new QLabel(tr("to"), box));
        times->addWidget(end);
        times->addStretch(1);
        qv->addLayout(times);
        bodyLayout_->addWidget(box);
    }

    // Summaries and time zone.
    {
        auto* box = new QGroupBox(tr("Summaries and time zone"), b);
        box->setObjectName(QStringLiteral("summaries"));
        auto* sv = new QVBoxLayout(box);
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Daily summary at"), box));
        auto* daily = timeEdit(v.dailyAt, tr("Daily summary time"), box);
        daily->setObjectName(QStringLiteral("dailyAt"));
        connect(daily, &QTimeEdit::editingFinished, this, [this, daily] {
            save({ { QStringLiteral("dailyAt"), daily->time().toString(QStringLiteral("HH:mm")) } });
        });
        row->addWidget(daily);
        row->addSpacing(12);
        row->addWidget(new QLabel(tr("Weekly summary on"), box));
        auto* day = new QComboBox(box);
        day->setObjectName(QStringLiteral("weeklyDay"));
        day->setAccessibleName(tr("Weekly summary day"));
        const QStringList days{ tr("Sunday"),   tr("Monday"), tr("Tuesday"), tr("Wednesday"),
                                tr("Thursday"), tr("Friday"), tr("Saturday") };
        for (int i = 0; i < days.size(); ++i) day->addItem(days[i], i);
        day->setCurrentIndex(std::clamp(v.weeklyDay, 0, 6));
        connect(day, &QComboBox::activated, this,
                [this, day] { save({ { QStringLiteral("weeklyDay"), day->currentData().toInt() } }); });
        row->addWidget(day);
        row->addStretch(1);
        sv->addLayout(row);
        sv->addWidget(muted(tr("The weekly summary goes at the daily summary’s time. Each lists what you "
                               "haven’t read since the last one."),
                            box));
        auto* zoneRow = new QHBoxLayout();
        zoneRow->addWidget(new QLabel(tr("Time zone"), box));
        auto* zone = new QComboBox(box);
        zone->setObjectName(QStringLiteral("timeZone"));
        zone->setAccessibleName(tr("Time zone"));
        zone->setEditable(true);
        zone->setInsertPolicy(QComboBox::NoInsert);
        for (const QByteArray& id : QTimeZone::availableTimeZoneIds()) zone->addItem(QString::fromUtf8(id));
        const QString tz = v.timeZone.isEmpty() ? computerTimeZone() : v.timeZone;
        const int at = zone->findText(tz);
        if (at >= 0) zone->setCurrentIndex(at);
        else zone->setEditText(tz);
        connect(zone, &QComboBox::activated, this, [this, zone, tz] {
            const QString picked = zone->currentText().trimmed();
            if (picked != tz && QTimeZone(picked.toUtf8()).isValid())
                save({ { QStringLiteral("timeZone"), picked } });
        });
        zoneRow->addWidget(zone, 1);
        const QString mine = computerTimeZone();
        if (tz != mine) {
            auto* use = new QPushButton(tr("Use this computer’s (%1)").arg(mine), box);
            use->setObjectName(QStringLiteral("useComputerTimeZone"));
            connect(use, &QPushButton::clicked, this,
                    [this, mine] { save({ { QStringLiteral("timeZone"), mine } }); });
            zoneRow->addWidget(use);
        }
        sv->addLayout(zoneRow);
        bodyLayout_->addWidget(box);
    }

    // Your clubs.
    if (!v.clubs.isEmpty()) {
        auto* box = new QGroupBox(tr("Your clubs"), b);
        box->setObjectName(QStringLiteral("clubs"));
        auto* cv = new QVBoxLayout(box);
        cv->addWidget(muted(tr("Mute a busy club, or keep it in the app but out of your email or pop-ups. "
                               "Warnings always reach you."),
                            box));
        for (const auto& c : v.clubs) {
            auto* row = new QWidget(box);
            row->setObjectName(QStringLiteral("clubNotifications:") + c.id);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(strong(c.name, row), 1);
            const QString id = c.id;
            const auto choice = [this, row, id, h](const QString& name, const QString& text,
                                                   const QString& access, bool on, bool enabled,
                                                   const char* key) {
                auto* tick = new QCheckBox(text, row);
                tick->setObjectName(name);
                tick->setChecked(on);
                tick->setEnabled(enabled);
                tick->setAccessibleName(access);
                connect(tick, &QCheckBox::toggled, this, [this, id, key](bool x) {
                    save({ { QStringLiteral("clubs"),
                             QJsonObject{ { id, QJsonObject{ { QLatin1String(key), x } } } } } });
                });
                h->addWidget(tick);
            };
            choice(QStringLiteral("mute"), tr("Mute"), tr("Mute %1").arg(c.name), c.mute, true, "mute");
            choice(QStringLiteral("email"), tr("Email"), tr("Email from %1").arg(c.name), c.email && !c.mute,
                   !c.mute, "email");
            choice(QStringLiteral("push"), tr("Pop-ups"), tr("Pop-ups from %1").arg(c.name),
                   c.push && !c.mute, !c.mute, "push");
            cv->addWidget(row);
        }
        bodyLayout_->addWidget(box);
    }
}

} // namespace bld::ui
