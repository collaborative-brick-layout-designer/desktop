#include "SettingsDialog.h"
#include "SettingsSections.h"

#include "help/HelpButton.h"
#include "theme/AppPrefs.h"
#include "theme/ColorTheme.h"
#include "theme/LegoColors.h"
#include "theme/Tokens.h"
#include "tours/Tours.h"

#include "ServerList.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace bld::ui {

using namespace theme;

namespace {

Mode modeOf(const QPalette& pal) { return pal.color(QPalette::Window).lightness() < 128 ? Mode::Dark : Mode::Light; }

QFont headingFont(double scale) {
    QFont f = QApplication::font();
    f.setFamily(QLatin1String(kHeadingFamily));
    f.setWeight(QFont::Bold);
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * scale);
    return f;
}

// The three theme cards' little pictures.
QPixmap themePreview(ThemeChoice t) {
    const Neutrals& l = neutrals(Mode::Light);
    const Neutrals& d = neutrals(Mode::Dark);
    const QSize size(150, 76);
    QPixmap pm(size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(0.5, 0.5, size.width() - 1, size.height() - 1);
    QPainterPath clip;
    clip.addRoundedRect(r, Radius::control, Radius::control);
    p.setClipPath(clip);
    const auto page = [&p](const QRectF& area, const Neutrals& n) {
        p.fillRect(area, n.bg);
        p.setPen(Qt::NoPen);
        p.setBrush(n.panel);
        p.drawRoundedRect(QRectF(area.left() + 7, area.top() + 7, area.width() * 0.22, area.height() - 14), 5, 5);
        p.setBrush(n.gridLine);
        p.drawRoundedRect(QRectF(area.left() + 13 + area.width() * 0.22, area.top() + 7,
                                 area.width() * 0.78 - 20, area.height() - 14), 5, 5);
    };
    if (t == ThemeChoice::System) {
        p.fillRect(QRectF(r.left(), r.top(), r.width() / 2, r.height()), l.bg);
        p.fillRect(QRectF(r.center().x(), r.top(), r.width() / 2, r.height()), d.bg);
    } else {
        page(r, t == ThemeChoice::Dark ? d : l);
    }
    p.setClipping(false);
    p.setPen(QPen(l.line, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, Radius::control, Radius::control);
    return pm;
}

// A color swatch, ringed when chosen.
QIcon swatch(const QColor& color, const QColor& ringGap, bool chosen) {
    const int s = 52;
    QPixmap pm(QSize(s, s) * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    if (chosen) {
        p.setBrush(color);
        p.drawEllipse(QRectF(0.5, 0.5, s - 1, s - 1));
        p.setBrush(ringGap);
        p.drawEllipse(QRectF(3, 3, s - 6, s - 6));
    }
    p.setBrush(color);
    p.drawEllipse(QRectF(5.5, 5.5, s - 11, s - 11));
    return QIcon(pm);
}

// The "Any LEGO color" tile before one is picked: a ring of brick colors.
QIcon legoRainbow() {
    const int s = 52;
    QPixmap pm(QSize(s, s) * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QConicalGradient g(QPointF(s / 2.0, s / 2.0), 90);
    const char* stops[] = { "#B40000", "#FAC80A", "#00852B", "#1E5AA8", "#A06EB9", "#B40000" };
    for (int i = 0; i < 6; ++i) g.setColorAt(i / 5.0, QColor(QLatin1String(stops[i])));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QRectF(5.5, 5.5, s - 11, s - 11));
    return QIcon(pm);
}

}  // namespace

void SettingsDialog::filterLegoColors() {
    const QString q = legoSearch_->text();
    int shown = 0;
    for (int i = 0; i < legoList_->count(); ++i) {
        QListWidgetItem* item = legoList_->item(i);
        const LegoColor* c = legoColor(item->data(Qt::UserRole).toInt());
        const bool match = c && legoColorMatches(*c, q);
        item->setHidden(!match);
        if (match) ++shown;
    }
    const bool none = shown == 0;
    legoList_->setVisible(!none);
    legoNone_->setVisible(none);
    legoShowAll_->setVisible(none);
    if (none)
        legoNone_->setText(tr("No LEGO color matches “%1”. Try a name like Bright Red, or a number like 21.")
                               .arg(q.trimmed()));
}

SettingsDialog::SettingsDialog(PrefsStore& store, const QString& syncedHost,
                               const settings::Changed& changed, QWidget* parent,
                               const std::function<void(const QString&)>& startTour,
                               const std::function<void()>& openPartsFolders)
    : QDialog(parent), store_(store) {
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(tr("Settings"));
    resize(940, 780);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    // The sections down the side: a click goes to that one.
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    auto* nav = new QListWidget(this);
    nav->setObjectName(QStringLiteral("settingsSections"));
    nav->setAccessibleName(tr("Sections"));
    nav->setFrameShape(QFrame::NoFrame);
    nav->setFixedWidth(210);
    nav->setSpacing(2);
    body->addWidget(nav);
    outer->addLayout(body, 1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAccessibleName(tr("Settings"));
    auto* page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("SettingsPage"));
    page->setAttribute(Qt::WA_StyledBackground);
    scroll->setWidget(page);
    body->addWidget(scroll, 1);

    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(4 * kSpacing, 3 * kSpacing, 4 * kSpacing, 3 * kSpacing);
    col->setSpacing(2 * kSpacing + 4);

    auto* title = new QLabel(tr("Settings"), page);
    title->setObjectName(QStringLiteral("SettingsTitle"));
    title->setFont(headingFont(2.0));
    col->addWidget(title);

    QList<QPair<QString, QFrame*>> sections;
    const auto card = [page, col, &sections](const QString& heading, const QString& helpKey = {}) {
        auto* f = new QFrame(page);
        f->setObjectName(QStringLiteral("SettingsCard"));
        sections << qMakePair(heading, f);
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(3 * kSpacing, 3 * kSpacing - 4, 3 * kSpacing, 3 * kSpacing - 4);
        v->setSpacing(2 * kSpacing);
        if (!heading.isEmpty()) {
            auto* h = new QLabel(heading, f);
            h->setObjectName(QStringLiteral("CardHeading"));
            h->setFont(headingFont(1.45));
            v->addWidget(helpKey.isEmpty() ? static_cast<QWidget*>(h) : help::withHelp(h, helpKey, f));
        }
        col->addWidget(f);
        return v;
    };

    // Light or dark.
    {
        auto* v = card(tr("Light or dark"));
        auto* row = new QHBoxLayout;
        row->setSpacing(14);
        auto* group = new QButtonGroup(this);
        const std::pair<ThemeChoice, QString> choices[] = {
            { ThemeChoice::Light, tr("Light") },
            { ThemeChoice::Dark, tr("Dark") },
            { ThemeChoice::System, tr("Match my computer") },
        };
        for (const auto& [choice, label] : choices) {
            auto* b = new QToolButton(this);
            b->setObjectName(QStringLiteral("theme_") + themeChoiceId(choice));
            b->setProperty("themeCard", true);
            b->setCheckable(true);
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIcon(QIcon(themePreview(choice)));
            b->setIconSize(QSize(150, 76));
            b->setText(label);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setCursor(Qt::PointingHandCursor);
            group->addButton(b);
            row->addWidget(b);
            connect(b, &QToolButton::toggled, this, [this, choice](bool on) {
                if (!on || loading_) return;
                AppPrefs p = store_.prefs();
                p.theme = choice;
                store_.update(p);
            });
        }
        v->addLayout(row);
    }

    // Color and bigger text.
    {
        auto* v = card(tr("Color"), QStringLiteral("settings.color"));
        // Two rows of five, so ten colors fit the dialog.
        auto* row = new QGridLayout;
        row->setHorizontalSpacing(2 * kSpacing);
        row->setVerticalSpacing(kSpacing);
        auto* group = new QButtonGroup(this);
        group->setObjectName(QStringLiteral("accentGroup"));
        int index = 0;
        for (const Accent& a : accents()) {
            auto* b = new QToolButton(this);
            b->setObjectName(QStringLiteral("accent_") + a.id);
            b->setProperty("swatch", true);
            b->setCheckable(true);
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIconSize(QSize(52, 52));
            b->setText(QCoreApplication::translate("bld::ui::SettingsDialog", a.label.toUtf8().constData()));
            b->setAccessibleName(b->text());
            b->setCursor(Qt::PointingHandCursor);
            group->addButton(b);
            row->addWidget(b, index / 5, index % 5);
            ++index;
            connect(b, &QToolButton::toggled, this, [this, id = a.id](bool on) {
                if (!on || loading_) return;
                AppPrefs p = store_.prefs();
                p.accent = id;
                p.legoColor = 0;
                store_.update(p);
            });
        }
        // The eleventh tile: any LEGO color. It opens the list below, and
        // shows the LEGO color in use.
        auto* legoTile = new QToolButton(this);
        legoTile->setObjectName(QStringLiteral("legoColorTile"));
        legoTile->setProperty("swatch", true);
        legoTile->setCheckable(true);
        legoTile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        legoTile->setIconSize(QSize(52, 52));
        legoTile->setCursor(Qt::PointingHandCursor);
        row->addWidget(legoTile, index / 5, index % 5);
        row->setColumnStretch(5, 1);
        v->addLayout(row);

        legoPicker_ = new QFrame(this);
        legoPicker_->setObjectName(QStringLiteral("LegoPicker"));
        auto* pv = new QVBoxLayout(legoPicker_);
        pv->setContentsMargins(12, 12, 12, 12);
        pv->setSpacing(kSpacing);
        auto* pickerTitle = new QLabel(tr("Any LEGO color"), legoPicker_);
        pickerTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
        pv->addWidget(help::withHelp(pickerTitle, QStringLiteral("settings.legoColor"), legoPicker_));
        auto* searchLabel = new QLabel(tr("Search by LEGO or BrickLink name or number"), legoPicker_);
        searchLabel->setObjectName(QStringLiteral("Muted"));
        pv->addWidget(searchLabel);
        legoSearch_ = new QLineEdit(legoPicker_);
        legoSearch_->setObjectName(QStringLiteral("legoColorSearch"));
        legoSearch_->setPlaceholderText(tr("For example Bright Red, Dark Blue or 21"));
        legoSearch_->setClearButtonEnabled(true);
        legoSearch_->setAccessibleName(searchLabel->text());
        searchLabel->setBuddy(legoSearch_);
        pv->addWidget(legoSearch_);
        legoList_ = new QListWidget(legoPicker_);
        legoList_->setObjectName(QStringLiteral("legoColorList"));
        legoList_->setAccessibleName(tr("Any LEGO color"));
        legoList_->setIconSize(QSize(28, 28));
        legoList_->setMinimumHeight(260);
        legoList_->setUniformItemSizes(true);
        legoList_->setTextElideMode(Qt::ElideNone);
        // Two lines a row (LEGO name and number, then BrickLink's), sized up
        // front: the list may first be laid out while it is still hidden.
        const QSize rowSize(240, 2 * legoList_->fontMetrics().lineSpacing() + 14);
        for (const LegoColor& c : legoColors()) {
            const QString bl = c.bl.isEmpty() ? tr("No BrickLink match")
                               : c.blId > 0  ? tr("BrickLink: %1 (%2)").arg(c.bl).arg(c.blId)
                                             : tr("BrickLink: %1").arg(c.bl);
            QString text = QStringLiteral("%1 · %2\n%3").arg(c.lego).arg(c.id).arg(bl);
            if (c.pearl) text += QStringLiteral(" · ") + tr("pearl, shown flat");
            auto* item = new QListWidgetItem(text, legoList_);
            item->setData(Qt::UserRole, c.id);
            item->setSizeHint(rowSize);
            item->setData(Qt::AccessibleTextRole, QStringLiteral("%1 (LEGO %2), %3").arg(c.lego).arg(c.id).arg(bl));
            QPixmap pm(QSize(28, 28) * 2);
            pm.setDevicePixelRatio(2);
            pm.fill(Qt::transparent);
            QPainter sp(&pm);
            sp.setRenderHint(QPainter::Antialiasing);
            sp.setPen(QPen(QColor(0, 0, 0, 40), 1));
            sp.setBrush(c.color);
            sp.drawRoundedRect(QRectF(0.5, 0.5, 27, 27), 6, 6);
            item->setIcon(QIcon(pm));
        }
        pv->addWidget(legoList_);
        auto* noneRow = new QHBoxLayout;
        legoNone_ = new QLabel(legoPicker_);
        legoNone_->setObjectName(QStringLiteral("legoColorNone"));
        legoNone_->setWordWrap(true);
        noneRow->addWidget(legoNone_, 1);
        legoShowAll_ = new QPushButton(tr("Show all colors"), legoPicker_);
        legoShowAll_->setObjectName(QStringLiteral("legoColorShowAll"));
        legoShowAll_->setAutoDefault(false);
        noneRow->addWidget(legoShowAll_);
        pv->addLayout(noneRow);
        legoNone_->hide();
        legoShowAll_->hide();
        legoPicker_->hide();
        v->addWidget(legoPicker_);
        connect(legoTile, &QToolButton::clicked, this, [this, legoTile] {
            legoPicker_->setVisible(!legoPicker_->isVisible());
            // The tile is "on" while a LEGO color is in use, whatever the list does.
            legoTile->setChecked(store_.prefs().legoColor > 0);
            if (legoPicker_->isVisible()) legoSearch_->setFocus();
        });
        connect(legoSearch_, &QLineEdit::textChanged, this, [this] { filterLegoColors(); });
        connect(legoShowAll_, &QPushButton::clicked, legoSearch_, &QLineEdit::clear);
        connect(legoList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            if (loading_ || !item) return;
            AppPrefs p = store_.prefs();
            p.legoColor = item->data(Qt::UserRole).toInt();
            store_.update(p);
        });

        // How much color: just the highlights (the original look) or the whole app.
        auto* styleTitle = new QLabel(tr("How much color"), this);
        styleTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
        v->addWidget(help::withHelp(styleTitle, QStringLiteral("settings.colorStyle"), this));
        auto* styleGroup = new QButtonGroup(this);
        styleGroup->setObjectName(QStringLiteral("colorStyleGroup"));
        const std::pair<QString, QString> styles[] = {
            { QStringLiteral("original"),
              tr("Just the highlights: buttons, the selected item and focus rings. The original look.") },
            { QStringLiteral("full"), tr("The whole app: also the toolbars, the menu and status bars, the panel tops and "
                                         "the window behind them.") },
        };
        for (const auto& [id, label] : styles) {
            auto* b = new QRadioButton(label, this);
            b->setObjectName(QStringLiteral("colorStyle_") + id);
            styleGroup->addButton(b);
            v->addWidget(b);
            connect(b, &QRadioButton::toggled, this, [this, id](bool on) {
                if (!on || loading_) return;
                AppPrefs p = store_.prefs();
                p.colorStyle = id;
                store_.update(p);
            });
        }
        auto* large = new QCheckBox(tr("Bigger text and buttons"), this);
        large->setObjectName(QStringLiteral("largeText"));
        v->addWidget(help::withHelp(large, QStringLiteral("settings.largeText"), this));
        connect(large, &QCheckBox::toggled, this, [this](bool on) {
            if (loading_) return;
            AppPrefs p = store_.prefs();
            p.largeText = on;
            store_.update(p);
        });
    }

    // Editing: how strongly parts snap together (SnapFeel.h).
    {
        auto* v = card(tr("Editing"));
        auto* snapTitle = new QLabel(tr("Snap strength"), this);
        snapTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
        v->addWidget(help::withHelp(snapTitle, QStringLiteral("settings.connectionSnap"), this));
        auto* lead = new QLabel(tr("How strongly a part you drag pulls onto a matching connection nearby."), this);
        lead->setObjectName(QStringLiteral("Muted"));
        lead->setWordWrap(true);
        v->addWidget(lead);
        auto* group = new QButtonGroup(this);
        group->setObjectName(QStringLiteral("snapGroup"));
        const std::pair<QString, QString> choices[] = {
            { QStringLiteral("off"), tr("Off: parts never pull onto each other; the grid still lines them up.") },
            { QStringLiteral("gentle"), tr("Gentle: pulls only when the ends are close. Recommended.") },
            { QStringLiteral("strong"), tr("Strong: reaches further, for quick rough placing.") },
        };
        for (const auto& [id, label] : choices) {
            auto* b = new QRadioButton(label, this);
            b->setObjectName(QStringLiteral("snap_") + id);
            group->addButton(b);
            v->addWidget(b);
            connect(b, &QRadioButton::toggled, this, [this, id](bool on) {
                if (!on || loading_) return;
                AppPrefs p = store_.prefs();
                p.connectionSnap = id;
                store_.update(p);
            });
        }
        auto* tip = new QLabel(tr("Tip: hold Alt (Option on a Mac) while dragging to place one part without snapping."),
                               this);
        tip->setObjectName(QStringLiteral("Muted"));
        tip->setWordWrap(true);
        v->addWidget(tip);
        settings::addEditing(v, this, changed);
    }

    settings::addMapLabels(card(tr("Map labels")), this, changed);
    settings::addLanguage(card(tr("Language")), this);
    settings::addStartingUp(card(tr("Starting up")), this);
    settings::addParts(card(tr("Parts and folders")), this, changed, openPartsFolders);
    settings::addImport(card(tr("Bringing in models"), QStringLiteral("prefs.ldraw")), this);

    // Help.
    {
        auto* v = card(tr("Help"));
        auto* helpIcons = new QCheckBox(tr("Show the small \"?\" buttons that explain things"), this);
        helpIcons->setObjectName(QStringLiteral("helpIcons"));
        v->addWidget(help::withHelp(helpIcons, QStringLiteral("settings.helpIcons"), this));
        connect(helpIcons, &QCheckBox::toggled, this, [this](bool on) {
            if (loading_) return;
            AppPrefs p = store_.prefs();
            p.helpIcons = on;
            store_.update(p);
        });

        // Tours: how many are done, Show tours again, and one button per tour.
        auto* toursRow = new QHBoxLayout;
        auto* toursText = new QVBoxLayout;
        auto* toursTitle = new QLabel(tr("Tours"), this);
        toursTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
        toursText->addWidget(help::withHelp(toursTitle, QStringLiteral("settings.tours"), this));
        toursNote_ = new QLabel(this);
        toursNote_->setObjectName(QStringLiteral("Muted"));
        toursText->addWidget(toursNote_);
        toursRow->addLayout(toursText, 1);
        showToursAgain_ = new QPushButton(tr("Show tours again"), this);
        showToursAgain_->setObjectName(QStringLiteral("showToursAgain"));
        showToursAgain_->setAutoDefault(false);
        connect(showToursAgain_, &QPushButton::clicked, this, [this] { tours::forgetSeen(store_); });
        toursRow->addWidget(showToursAgain_);
        v->addLayout(toursRow);
        if (startTour) {
            auto* take = new QHBoxLayout;
            auto* takeLabel = new QLabel(tr("Take a tour:"), this);
            takeLabel->setObjectName(QStringLiteral("Muted"));
            take->addWidget(takeLabel);
            for (const tours::Tour* t : tours::desktopTours()) {
                auto* b = new QPushButton(t->title, this);
                b->setObjectName(QStringLiteral("tour.") + t->id);
                b->setAutoDefault(false);
                connect(b, &QPushButton::clicked, this, [this, startTour, id = t->id] {
                    accept();
                    startTour(id);
                });
                take->addWidget(b);
            }
            take->addStretch(1);
            v->addLayout(take);
        }
    }

    // Servers: where your club's layouts, modules and these settings live.
    {
        auto* v = card(tr("Servers"), QStringLiteral("servers.pick"));
        auto* row = new QHBoxLayout;
        serversNote_ = new QLabel(this);
        serversNote_->setObjectName(QStringLiteral("serversNote"));
        serversNote_->setWordWrap(true);
        row->addWidget(serversNote_, 1);
        manageServers_ = new QPushButton(tr("Manage servers…"), this);
        manageServers_->setObjectName(QStringLiteral("manageServers"));
        manageServers_->setToolTip(tr("Add a server, sign in or out, and pick your Main one"));
        manageServers_->setAutoDefault(false);
        manageServers_->hide();
        connect(manageServers_, &QPushButton::clicked, this, [this] {
            // Not inside the button's own click.
            QTimer::singleShot(0, this, [this] {
                if (manage_) manage_();
                loadServers();
            });
        });
        row->addWidget(manageServers_);
        v->addLayout(row);
        loadServers();
    }

    syncNote_ = new QLabel(page);
    syncNote_->setWordWrap(true);
    syncNote_->setTextFormat(Qt::RichText);
    if (syncedHost.isEmpty()) {
        syncNote_->setObjectName(QStringLiteral("SyncNoteOff"));
        syncNote_->setText(tr("These settings are kept on this computer. While you're connected to a server, "
                              "they sync with your account there, both ways."));
    } else {
        syncNote_->setObjectName(QStringLiteral("SyncNote"));
        syncNote_->setText(tr("<b>Synced with %1</b><br>These settings are for this app. While you're connected "
                              "to a server, they sync with your account there, both ways.")
                               .arg(syncedHost.toHtmlEscaped()));
    }
    col->addWidget(help::withHelp(syncNote_, QStringLiteral("settings.sync"), page));
    col->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(4 * kSpacing, 2 * kSpacing, 4 * kSpacing, 2 * kSpacing);
    auto* saved = new QLabel(tr("Changes are saved as you make them."), this);
    saved->setObjectName(QStringLiteral("Muted"));
    buttons->addWidget(saved);
    buttons->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    buttons->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    outer->addLayout(buttons);

    for (const auto& [heading, frame] : sections) {
        auto* item = new QListWidgetItem(heading, nav);
        item->setData(Qt::UserRole, QVariant::fromValue(static_cast<QObject*>(frame)));
        item->setSizeHint(QSize(0, nav->fontMetrics().height() + 20));
    }
    nav->setCurrentRow(0);
    connect(nav, &QListWidget::currentItemChanged, this, [scroll](QListWidgetItem* item) {
        if (!item) return;
        if (auto* f = qobject_cast<QWidget*>(item->data(Qt::UserRole).value<QObject*>()))
            scroll->verticalScrollBar()->setValue(f->y() - kSpacing);
    });
    // Scrolling marks the section in view.
    connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, nav, [nav](int value) {
        int row = 0;
        for (int i = 0; i < nav->count(); ++i)
            if (auto* f = qobject_cast<QWidget*>(nav->item(i)->data(Qt::UserRole).value<QObject*>());
                f && f->y() - 2 * kSpacing <= value)
                row = i;
        const QSignalBlocker quiet(nav);
        nav->setCurrentRow(row);
    });

    connect(&store_, &PrefsStore::changed, this, [this] { load(); });
    load();
}

void SettingsDialog::setManageServers(const std::function<void()>& manage) {
    manage_ = manage;
    manageServers_->setVisible(static_cast<bool>(manage_));
}

void SettingsDialog::loadServers() {
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* main = list.mainServer();
    if (!main) main = list.lastUsed();
    if (list.servers().isEmpty() || !main) {
        serversNote_->setText(tr("No servers yet. Add your club's server to share layouts and modules with it, "
                                 "and to keep these settings with your account."));
        manageServers_->setText(tr("Add a server…"));
        return;
    }
    const auto others = static_cast<int>(list.servers().size()) - 1;
    QString text = main->label() == main->address() ? tr("Main: %1").arg(main->label())
                                                    : tr("Main: %1 (%2)").arg(main->label(), main->address());
    if (others > 0) text += QLatin1Char(' ') + tr("and %n more", nullptr, others);
    serversNote_->setText(text);
    manageServers_->setText(tr("Manage servers…"));
}

void SettingsDialog::load() {
    loading_ = true;
    const AppPrefs& p = store_.prefs();
    const Mode mode = modeOf(palette());
    const Neutrals& n = neutrals(mode);
    if (auto* b = findChild<QToolButton*>(QStringLiteral("theme_") + themeChoiceId(p.theme))) b->setChecked(true);
    const LegoColor* lego = legoColor(p.legoColor);
    // A LEGO color in use leaves every preset unticked.
    if (auto* group = findChild<QButtonGroup*>(QStringLiteral("accentGroup"))) group->setExclusive(false);
    for (const Accent& a : accents()) {
        auto* b = findChild<QToolButton*>(QStringLiteral("accent_") + a.id);
        if (!b) continue;
        b->setChecked(!lego && a.id == accent(p.accent).id);
        b->setIcon(swatch(a.main, n.panel, b->isChecked()));
    }
    if (auto* group = findChild<QButtonGroup*>(QStringLiteral("accentGroup"))) group->setExclusive(true);
    if (auto* tile = findChild<QToolButton*>(QStringLiteral("legoColorTile"))) {
        tile->setChecked(lego != nullptr);
        tile->setText(lego ? lego->lego : tr("Any LEGO color"));
        tile->setAccessibleName(lego ? tr("Any LEGO color, now %1").arg(lego->lego) : tr("Any LEGO color"));
        tile->setIcon(lego ? swatch(lego->color, n.panel, true) : legoRainbow());
    }
    if (lego) legoPicker_->show();
    for (int i = 0; i < legoList_->count(); ++i) {
        QListWidgetItem* item = legoList_->item(i);
        item->setSelected(lego && item->data(Qt::UserRole).toInt() == lego->id);
    }
    if (auto* style = findChild<QRadioButton*>(QStringLiteral("colorStyle_") + p.colorStyle)) style->setChecked(true);
    findChild<QCheckBox*>(QStringLiteral("largeText"))->setChecked(p.largeText);
    findChild<QCheckBox*>(QStringLiteral("helpIcons"))->setChecked(p.helpIcons);
    if (auto* snap = findChild<QRadioButton*>(QStringLiteral("snap_") + p.connectionSnap)) snap->setChecked(true);
    const auto done = p.toursSeen.size();
    toursNote_->setText(done == 0 ? tr("You haven't finished any tours yet.")
                                  : done == 1 ? tr("You've seen one tour.") : tr("You've seen %1 tours.").arg(done));
    showToursAgain_->setEnabled(done > 0);

    const Accent a = effectiveAccent(p, mode);
    QString css = QStringLiteral(
        "QWidget#SettingsPage { background: %1; }"
        "QFrame#SettingsCard { background: %2; border: 1px solid %3; border-radius: %8px; }"
        "QLabel#Muted { color: %4; }"
        "QToolButton[themeCard=\"true\"] { background: %2; border: 1px solid %5; border-radius: 14px; padding: 10px;"
        " color: %6; font-weight: 700; }"
        "QToolButton[themeCard=\"true\"]:checked { border: 2px solid %7; }"
        "QToolButton[swatch=\"true\"] { border: none; background: transparent; color: %4; font-weight: 600; }"
        "QToolButton[swatch=\"true\"]:checked { color: %6; font-weight: 700; }");
    css = css.arg(n.bg.name(), n.panel.name(), n.line.name(), n.muted.name(), n.border.name(),
                  n.ink.name(), a.main.name())
              .arg(Radius::section);
    css += QStringLiteral("QLabel#SyncNote { background: %1; color: %2; border-radius: %3px; padding: 12px; }"
                          "QLabel#SyncNoteOff { color: %4; padding: 4px; }")
               .arg(n.okSoft.name(), n.ok.name())
               .arg(Radius::card)
               .arg(n.muted.name());
    css += QStringLiteral("QFrame#LegoPicker { background: %1; border: 1px solid %2; border-radius: 14px; }"
                          "QListWidget#legoColorList { background: %3; border: 1px solid %2; border-radius: %4px; }"
                          "QListWidget#legoColorList::item { padding: 4px; border-radius: 8px; color: %5; }"
                          "QListWidget#legoColorList::item:selected { background: %6; color: %5;"
                          " border: 2px solid %7; }")
               .arg(n.soft.name(), n.line.name(), n.panel.name())
               .arg(Radius::control)
               .arg(n.ink.name(), a.soft(mode).name(), a.main.name());
    css += QStringLiteral("QListWidget#settingsSections { background: %1; border: none; padding: 16px 8px; }"
                          "QListWidget#settingsSections::item { padding: 8px 12px; border-radius: %2px; color: %3; }"
                          "QListWidget#settingsSections::item:selected { background: %4; color: %5; font-weight: 700; }")
               .arg(n.bg.name())
               .arg(Radius::control)
               .arg(n.ink.name(), a.soft(mode).name(), a.text(mode).name());
    setStyleSheet(css);
    loading_ = false;
}

}  // namespace bld::ui
