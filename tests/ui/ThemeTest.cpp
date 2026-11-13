// The redesign's look: the tokens match the web's (apps/web/src/theme/
// tokens.ts), the palette in light and dark, "match my computer" following
// the system color scheme, the colors and bigger text, and the settings'
// JSON as the server's /api/me/preferences has it.

#include "ui/theme/AppPrefs.h"
#include "ui/theme/ColorTheme.h"
#include "ui/theme/LegoColors.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

#include <QApplication>
#include <QCheckBox>
#include <QImage>
#include <QJsonArray>
#include <QRadioButton>
#include <QStyleOptionButton>
#include <QSettings>
#include <QStyle>

using namespace bld::ui::theme;

namespace {

QString hex(const QColor& c) { return c.name(QColor::HexRgb).toUpper(); }

// Puts the app's palette, font and stylesheet back after each test.
class Theme : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings().remove(kGroup);
        palette_ = QApplication::palette();
        font_ = QApplication::font();
        css_ = qApp->styleSheet();
    }
    void TearDown() override {
        qApp->setStyleSheet(css_);
        QApplication::setPalette(palette_);
        QApplication::setFont(font_);
        QSettings().remove(kGroup);
    }
    static constexpr const char* kGroup = "test-theme";
    QPalette palette_;
    QFont font_;
    QString css_;
};

}  // namespace

TEST(Tokens, MatchTheWebValues) {
    EXPECT_EQ(hex(neutrals(Mode::Light).bg), QStringLiteral("#F6F4EF"));
    EXPECT_EQ(hex(neutrals(Mode::Light).ink), QStringLiteral("#1E2124"));
    EXPECT_EQ(hex(neutrals(Mode::Light).muted), QStringLiteral("#5B6168"));
    EXPECT_EQ(hex(neutrals(Mode::Dark).panel), QStringLiteral("#1F2226"));
    EXPECT_EQ(hex(neutrals(Mode::Dark).okSoft), QStringLiteral("#1D3326"));
    EXPECT_EQ(hex(neutrals(Mode::Dark).tourMuted), QStringLiteral("#4A5057"));
    EXPECT_EQ(hex(accent(QStringLiteral("ocean")).main), QStringLiteral("#2459C4"));
    EXPECT_EQ(hex(accent(QStringLiteral("forest")).text(Mode::Dark)), QStringLiteral("#8FD1A8"));
    EXPECT_EQ(hex(accent(QStringLiteral("plum")).soft(Mode::Light)), QStringLiteral("#F1E8F9"));
    EXPECT_EQ(accents().size(), 10);
    EXPECT_EQ(hex(accent(QStringLiteral("teal")).main), QStringLiteral("#0F7C80"));
    EXPECT_EQ(hex(accent(QStringLiteral("slate")).text(Mode::Dark)), QStringLiteral("#B6C0CC"));
    EXPECT_EQ(accents().first().id, QStringLiteral("brick"));
    EXPECT_EQ(accent(QStringLiteral("no-such")).id, QStringLiteral("brick"));
    EXPECT_EQ(Radius::control, 10);
    EXPECT_EQ(Radius::section, 18);
    EXPECT_DOUBLE_EQ(kLargeTextScale, 1.125);
}

TEST(Tokens, SunnyKeepsItsYellowWithDarkText) {
    const Accent sunny = accent(QStringLiteral("sunny"));
    EXPECT_EQ(hex(sunny.main), QStringLiteral("#B8860B"));
    EXPECT_EQ(hex(sunny.onMain), QStringLiteral("#1E2124"));
    for (const Accent& a : accents()) {
        if (a.id != QLatin1String("sunny")) {
            EXPECT_EQ(hex(a.onMain), QStringLiteral("#FFFFFF")) << a.id.toStdString();
        }
    }
}

TEST(Tokens, EmbeddedFontsLoad) {
    EXPECT_TRUE(loadFonts());
}

TEST_F(Theme, PaletteInLightAndDark) {
    const QPalette light = buildPalette(Mode::Light, accent(QStringLiteral("brick")));
    EXPECT_EQ(hex(light.color(QPalette::Window)), QStringLiteral("#FFFFFF"));
    EXPECT_EQ(hex(light.color(QPalette::WindowText)), QStringLiteral("#1E2124"));
    EXPECT_EQ(hex(light.color(QPalette::Highlight)), QStringLiteral("#C2412D"));
    EXPECT_EQ(hex(light.color(QPalette::HighlightedText)), QStringLiteral("#FFFFFF"));
    EXPECT_EQ(hex(light.color(QPalette::Link)), QStringLiteral("#A8331F"));
    EXPECT_EQ(hex(light.color(QPalette::Disabled, QPalette::Text)), QStringLiteral("#5B6168"));

    const QPalette dark = buildPalette(Mode::Dark, accent(QStringLiteral("forest")));
    EXPECT_EQ(hex(dark.color(QPalette::Window)), QStringLiteral("#1F2226"));
    EXPECT_EQ(hex(dark.color(QPalette::Text)), QStringLiteral("#ECEDEF"));
    EXPECT_EQ(hex(dark.color(QPalette::Highlight)), QStringLiteral("#2E7D4F"));
    EXPECT_EQ(hex(dark.color(QPalette::Link)), QStringLiteral("#8FD1A8"));

    const QPalette sunny = buildPalette(Mode::Light, accent(QStringLiteral("sunny")));
    EXPECT_EQ(hex(sunny.color(QPalette::HighlightedText)), QStringLiteral("#1E2124"));
    EXPECT_TRUE(buildStyleSheet(Mode::Dark, accent(QStringLiteral("ocean"))).contains(QStringLiteral("#93b4f0")));
}

TEST_F(Theme, MatchMyComputerFollowsTheColorScheme) {
    EXPECT_EQ(resolveMode(ThemeChoice::System, Qt::ColorScheme::Dark), Mode::Dark);
    EXPECT_EQ(resolveMode(ThemeChoice::System, Qt::ColorScheme::Light), Mode::Light);
    EXPECT_EQ(resolveMode(ThemeChoice::System, Qt::ColorScheme::Unknown), Mode::Light);
    EXPECT_EQ(resolveMode(ThemeChoice::Light, Qt::ColorScheme::Dark), Mode::Light);
    EXPECT_EQ(resolveMode(ThemeChoice::Dark, Qt::ColorScheme::Light), Mode::Dark);

    PrefsStore store(QString::fromLatin1(kGroup));
    ThemeManager manager(store);
    AppPrefs p = store.prefs();
    p.theme = ThemeChoice::System;
    store.update(p);
    manager.onColorSchemeChanged(Qt::ColorScheme::Dark);
    EXPECT_EQ(manager.mode(), Mode::Dark);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Window)), QStringLiteral("#1F2226"));
    manager.onColorSchemeChanged(Qt::ColorScheme::Light);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Window)), QStringLiteral("#FFFFFF"));

    // A chosen theme ignores the system.
    p.theme = ThemeChoice::Dark;
    store.update(p);
    manager.onColorSchemeChanged(Qt::ColorScheme::Light);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Window)), QStringLiteral("#1F2226"));
}

TEST_F(Theme, ColorsAndBiggerTextApply) {
    PrefsStore store(QString::fromLatin1(kGroup));
    ThemeManager manager(store);
    manager.apply();
    const double base = manager.baseFont().pointSizeF();
    ASSERT_GT(base, 0);
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), base);
    EXPECT_EQ(QApplication::font().family(), QStringLiteral("Figtree"));

    AppPrefs p = store.prefs();
    p.accent = QStringLiteral("plum");
    p.largeText = true;
    store.update(p);  // the manager follows the store
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Highlight)), QStringLiteral("#8A4FBF"));
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), base * 1.125);

    p.largeText = false;
    store.update(p);
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), base);
}

// The whole app is the default, also for settings saved before there was
// a choice (no colorStyle key) and for a saved "original" without the
// marker (every save stored it then, chosen or not); a marked "original"
// is a choice and stays.
TEST_F(Theme, TheWholeAppIsTheDefaultAndAChosenHighlightsLookStays) {
    {
        QSettings s;
        s.beginGroup(QString::fromLatin1(kGroup));
        s.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
    }
    EXPECT_EQ(PrefsStore(QString::fromLatin1(kGroup)).prefs().colorStyle, QStringLiteral("full"));
    {
        QSettings s;
        s.beginGroup(QString::fromLatin1(kGroup));
        s.setValue(QStringLiteral("colorStyle"), QStringLiteral("original"));
    }
    EXPECT_EQ(PrefsStore(QString::fromLatin1(kGroup)).prefs().colorStyle, QStringLiteral("full"));
    {
        QSettings s;
        s.beginGroup(QString::fromLatin1(kGroup));
        s.setValue(QStringLiteral("colorStyleChosen"), true);
    }
    PrefsStore chose(QString::fromLatin1(kGroup));
    EXPECT_TRUE(chose.prefs().colorStyleChosen);
    EXPECT_EQ(chose.prefs().colorStyle, QStringLiteral("original"));
    AppPrefs p = chose.prefs();
    p.accent = QStringLiteral("ocean");
    chose.update(p);
    EXPECT_EQ(PrefsStore(QString::fromLatin1(kGroup)).prefs().colorStyle, QStringLiteral("original"));
}

TEST_F(Theme, SettingsKeepTheirValuesAndTheServersJson) {
    {
        PrefsStore store(QString::fromLatin1(kGroup));
        EXPECT_EQ(store.prefs().theme, ThemeChoice::System);
        EXPECT_EQ(store.prefs().accent, QStringLiteral("brick"));
        EXPECT_TRUE(store.prefs().helpIcons);
        EXPECT_FALSE(store.prefs().updatedAt.isValid());
        AppPrefs p;
        p.theme = ThemeChoice::Dark;
        p.accent = QStringLiteral("sunny");
        p.expertMode = true;
        p.toursSeen = { QStringLiteral("editor.intro") };
        store.update(p);
        EXPECT_TRUE(store.prefs().updatedAt.isValid());
    }
    PrefsStore again(QString::fromLatin1(kGroup));
    EXPECT_EQ(again.prefs().theme, ThemeChoice::Dark);
    EXPECT_EQ(again.prefs().accent, QStringLiteral("sunny"));
    EXPECT_TRUE(again.prefs().expertMode);  // kept for the server, though not shown
    EXPECT_TRUE(again.prefs().updatedAt.isValid());

    const QJsonObject json = again.prefs().toJson();
    EXPECT_EQ(json.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
    EXPECT_EQ(json.value(QStringLiteral("toursSeen")).toArray().size(), 1);
    EXPECT_EQ(json.keys().size(), 11);  // with legoColor, colorStyle and colorStyleChosen
    EXPECT_FALSE(json.value(QStringLiteral("colorStyleChosen")).toBool());
    EXPECT_EQ(json.value(QStringLiteral("partsIconSize")).toInt(), 96);
    EXPECT_EQ(json.value(QStringLiteral("connectionSnap")).toString(), QStringLiteral("gentle"));

    // The server's keys laid over; bad values are skipped, as the server refuses them.
    const AppPrefs merged = AppPrefs::fromJson(
        QJsonObject{ { QStringLiteral("accent"), QStringLiteral("ocean") },
                     { QStringLiteral("theme"), QStringLiteral("purple") },
                     { QStringLiteral("largeText"), QStringLiteral("yes") },
                     { QStringLiteral("toursSeen"), QJsonArray{ QStringLiteral("ok.1"), QStringLiteral("bad id") } } },
        again.prefs());
    EXPECT_EQ(merged.accent, QStringLiteral("ocean"));
    EXPECT_EQ(merged.theme, ThemeChoice::Dark);
    EXPECT_FALSE(merged.largeText);
    EXPECT_EQ(merged.toursSeen, QStringList{ QStringLiteral("ok.1") });
}

// contrast() is ColorTheme.h's (WCAG 2.1).

// Text is readable in light and dark, with every accent: at least 4.5:1
// (WCAG AA for normal text) for every pair of text and background the
// palette and the stylesheet draw.
TEST(Tokens, TextIsReadableInLightAndDarkWithEveryAccent) {
    constexpr double kText = 4.5;
    for (Mode m : { Mode::Light, Mode::Dark }) {
        const Neutrals& n = neutrals(m);
        const char* mode = m == Mode::Dark ? "dark" : "light";
        const std::pair<const char*, std::pair<QColor, QColor>> plain[] = {
            { "ink on bg", { n.ink, n.bg } },          { "ink on panel", { n.ink, n.panel } },
            { "ink on soft", { n.ink, n.soft } },      { "muted on bg", { n.muted, n.bg } },
            { "muted on panel", { n.muted, n.panel } }, { "muted on soft", { n.muted, n.soft } },
            { "danger on bg", { n.danger, n.bg } },    { "danger on panel", { n.danger, n.panel } },
            { "panel on danger (danger buttons)", { n.panel, n.danger } },
            { "tour ink", { n.tourInk, n.tourBg } },   { "tour muted", { n.tourMuted, n.tourBg } },
        };
        for (const auto& [what, pair] : plain)
            EXPECT_GE(contrast(pair.first, pair.second), kText) << mode << ": " << what;
        for (const Accent& a : accents()) {
            const QPalette p = buildPalette(m, a);
            const auto check = [&](const char* what, const QColor& fg, const QColor& bg) {
                EXPECT_GE(contrast(fg, bg), kText) << mode << ", " << a.id.toStdString() << ": " << what;
            };
            check("WindowText on Window", p.color(QPalette::WindowText), p.color(QPalette::Window));
            check("Text on Base", p.color(QPalette::Text), p.color(QPalette::Base));
            check("Text on AlternateBase", p.color(QPalette::Text), p.color(QPalette::AlternateBase));
            check("ButtonText on Button", p.color(QPalette::ButtonText), p.color(QPalette::Button));
            check("ToolTipText on ToolTipBase", p.color(QPalette::ToolTipText), p.color(QPalette::ToolTipBase));
            check("HighlightedText on Highlight", p.color(QPalette::HighlightedText), p.color(QPalette::Highlight));
            check("PlaceholderText on Base", p.color(QPalette::PlaceholderText), p.color(QPalette::Base));
            check("Link on Base", p.color(QPalette::Link), p.color(QPalette::Base));
            check("text on the accent (default buttons)", a.onMain, a.main);
            check("accent text on panel", a.text(m), n.panel);
            check("accent text on bg", a.text(m), n.bg);
            check("accent text on its soft color (pills, checked tools)", a.text(m), a.soft(m));
        }
    }
}

// In the light theme an unticked checkbox or radio button had no box at all
// (Fusion draws it in Window and Base, both the panel color here): every
// box now has an edge at least 3:1 against the panel, ticked or not, in
// both themes.
TEST_F(Theme, CheckboxAndRadioBoxesShowInLightAndDark) {
    PrefsStore store(QString::fromLatin1(kGroup));
    ThemeManager manager(store);
    const auto lum = [](const QColor& c) {
        const auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
    };
    for (const ThemeChoice mode : { ThemeChoice::Light, ThemeChoice::Dark }) {
        AppPrefs p = store.prefs();
        p.theme = mode;
        store.update(p);
        const QColor panel = QApplication::palette().color(QPalette::Window);
        for (const bool checked : { false, true }) {
            QCheckBox box(QStringLiteral("Bigger text and buttons"));
            QRadioButton radio(QStringLiteral("Gentle"));
            for (QAbstractButton* b : { static_cast<QAbstractButton*>(&box), static_cast<QAbstractButton*>(&radio) }) {
                b->setChecked(checked);
                b->resize(b->sizeHint());
                const QImage shot = b->grab().toImage();
                QStyleOptionButton opt;
                opt.initFrom(b);
                const QRect ind = b->style()->subElementRect(
                    qobject_cast<QCheckBox*>(b) ? QStyle::SE_CheckBoxIndicator : QStyle::SE_RadioButtonIndicator, &opt, b);
                ASSERT_FALSE(ind.isEmpty());
                // The best-contrast pixel of the box's left edge column.
                double best = 1;
                const double lp = lum(panel);
                const qreal dpr = shot.devicePixelRatio();
                for (int y = ind.top(); y <= ind.bottom(); ++y)
                    for (int x = ind.left(); x <= ind.left() + 2; ++x) {
                        const QColor c = shot.pixelColor(QPoint(x, y) * dpr);
                        const double l = lum(c);
                        best = std::max(best, (std::max(l, lp) + 0.05) / (std::min(l, lp) + 0.05));
                    }
                EXPECT_GE(best, 3.0) << (mode == ThemeChoice::Light ? "light " : "dark ")
                                     << (checked ? "ticked " : "unticked ") << b->metaObject()->className();
            }
        }
    }
}

// A theme from any color: the extremes give exactly the web's colors
// (apps/web/src/theme/test/colorTheme.test.ts pins the same), and every
// LEGO color reads in both modes.
TEST(ColorTheme, ExtremesMatchTheWeb) {
    const auto t = [](int lego, Mode m) {
        const ColorTheme c = colorTheme(legoColor(lego)->color, m);
        return QStringList{ hex(c.main), hex(c.onMain), hex(c.soft), hex(c.text) }.join(QLatin1Char(' '));
    };
    EXPECT_EQ(t(1, Mode::Light), QStringLiteral("#929292 #1E2124 #F2F2F2 #6C6C6C"));     // White
    EXPECT_EQ(t(1, Mode::Dark), QStringLiteral("#F4F4F4 #1E2124 #4A4C4F #F4F4F4"));
    EXPECT_EQ(t(26, Mode::Light), QStringLiteral("#1B2A34 #FFFFFF #E4E5E7 #1B2A34"));    // Black
    EXPECT_EQ(t(26, Mode::Dark), QStringLiteral("#646E75 #FFFFFF #2D3136 #939A9E"));
    EXPECT_EQ(t(24, Mode::Light), QStringLiteral("#B49007 #1E2124 #F6F2E1 #836905"));    // Bright Yellow
    EXPECT_EQ(t(24, Mode::Dark), QStringLiteral("#FAC80A #1E2124 #4B4320 #FAC80A"));
    EXPECT_EQ(t(140, Mode::Light), QStringLiteral("#19325A #FFFFFF #E3E6EB #19325A"));   // Earth Blue
    EXPECT_EQ(t(140, Mode::Dark), QStringLiteral("#5C6D8A #FFFFFF #2B313A #8D99AD"));
    const ChromeTheme light = chromeTheme(QColor(QLatin1String("#B49007")), Mode::Light);
    EXPECT_EQ(hex(light.chrome), QStringLiteral("#F3EDD7"));
    EXPECT_EQ(hex(light.chromeLine), QStringLiteral("#D6C899"));
    EXPECT_EQ(hex(light.bg), QStringLiteral("#F2EEE1"));
    const ChromeTheme dark = chromeTheme(QColor(QLatin1String("#5C6D8A")), Mode::Dark);
    EXPECT_EQ(hex(dark.chrome), QStringLiteral("#2C333C"));
    EXPECT_EQ(hex(dark.chromeLine), QStringLiteral("#3F4856"));
    EXPECT_EQ(hex(dark.bg), QStringLiteral("#1C1F24"));
}

TEST(ColorTheme, EveryLegoColorIsReadableInLightAndDark) {
    EXPECT_EQ(legoColors().size(), 122);
    for (Mode m : { Mode::Light, Mode::Dark }) {
        const Neutrals& n = neutrals(m);
        for (const LegoColor& c : legoColors()) {
            const ColorTheme t = colorTheme(c.color, m);
            const std::string what = c.lego.toStdString() + (m == Mode::Dark ? " dark" : " light");
            EXPECT_GE(contrast(t.onMain, t.main), 4.5) << what << ": text on the accent";
            EXPECT_GE(contrast(t.main, n.panel), 3.0) << what << ": the accent on the panel";
            for (const QColor& s : { n.bg, n.panel, n.soft, t.soft })
                EXPECT_GE(contrast(t.text, s), 4.5) << what << ": accent text on " << hex(s).toStdString();
            const ChromeTheme ch = chromeTheme(t.main, m);
            for (const QColor& s : { ch.chrome, ch.bg }) {
                EXPECT_GE(contrast(n.ink, s), 4.5) << what << ": text on " << hex(s).toStdString();
                EXPECT_GE(contrast(n.muted, s), 4.5) << what << ": muted text on " << hex(s).toStdString();
            }
        }
    }
}

TEST(ColorTheme, NamesEachColorAsLegoAndBrickLinkDoAndLeavesOutSeeThroughOnes) {
    const LegoColor* yellow = legoColor(24);
    ASSERT_NE(yellow, nullptr);
    EXPECT_EQ(yellow->lego, QStringLiteral("Bright Yellow"));
    EXPECT_EQ(yellow->bl, QStringLiteral("Yellow"));
    EXPECT_EQ(yellow->blId, 3);
    EXPECT_EQ(hex(yellow->color), QStringLiteral("#FAC80A"));
    EXPECT_EQ(legoColor(140)->bl, QStringLiteral("Dark Blue"));
    EXPECT_EQ(legoColor(194)->bl, QStringLiteral("Light Bluish Gray"));
    EXPECT_EQ(legoColor(41), nullptr);  // Trans-Red
    for (const LegoColor& c : legoColors())
        for (const char* word : { "trans", "glitter", "chrome", "speckle", "opal", "glow" })
            EXPECT_FALSE(c.lego.contains(QLatin1String(word), Qt::CaseInsensitive)) << c.lego.toStdString();
    // Search: either name, either number, "grey" or "gray".
    const LegoColor& lbg = *legoColor(194);
    EXPECT_TRUE(legoColorMatches(lbg, QStringLiteral("medium stone gray")));
    EXPECT_TRUE(legoColorMatches(lbg, QStringLiteral("Light Bluish Grey")));
    EXPECT_TRUE(legoColorMatches(lbg, QStringLiteral("194")));
    EXPECT_TRUE(legoColorMatches(lbg, QStringLiteral("86")));
    EXPECT_FALSE(legoColorMatches(lbg, QStringLiteral("19")));
    EXPECT_FALSE(legoColorMatches(lbg, QStringLiteral("red")));
}

// The server keeps legoColor (null for none) and colorStyle; a number not
// in the list, or an unknown style, is skipped.
TEST(ColorTheme, LegoColorAndStyleGoToAndFromTheServersJson) {
    AppPrefs p;
    EXPECT_TRUE(p.toJson().value(QLatin1String("legoColor")).isNull());
    EXPECT_EQ(p.toJson().value(QLatin1String("colorStyle")).toString(), QStringLiteral("full"));  // the default
    p.legoColor = 140;
    p.colorStyle = QStringLiteral("original");
    p.colorStyleChosen = true;
    const QJsonObject json = p.toJson();
    EXPECT_TRUE(json.value(QLatin1String("colorStyleChosen")).toBool());
    EXPECT_EQ(json.value(QLatin1String("legoColor")).toInt(), 140);
    const AppPrefs back = AppPrefs::fromJson(json);
    EXPECT_EQ(back.legoColor, 140);
    EXPECT_EQ(back.colorStyle, QStringLiteral("original"));
    EXPECT_TRUE(back.colorStyleChosen);
    EXPECT_TRUE(back.sameSettings(p));
    const AppPrefs bad = AppPrefs::fromJson(
        QJsonObject{ { QStringLiteral("legoColor"), 41 }, { QStringLiteral("colorStyle"), QStringLiteral("loud") } }, back);
    EXPECT_EQ(bad.legoColor, 140);
    EXPECT_EQ(bad.colorStyle, QStringLiteral("original"));
    // A server's prefs saved before there was a choice: the default stays.
    EXPECT_EQ(AppPrefs::fromJson(QJsonObject{ { QStringLiteral("theme"), QStringLiteral("dark") } }).colorStyle, QStringLiteral("full"));
    // An "original" without the marker is the old default, not a choice.
    const QJsonObject unmarked{ { QStringLiteral("colorStyle"), QStringLiteral("original") } };
    EXPECT_EQ(AppPrefs::fromJson(unmarked).colorStyle, QStringLiteral("full"));
    QJsonObject cleared = unmarked;
    cleared.insert(QStringLiteral("colorStyleChosen"), false);
    EXPECT_EQ(AppPrefs::fromJson(cleared, back).colorStyle, QStringLiteral("full"));
    const QJsonObject wrong{ { QStringLiteral("colorStyleChosen"), QStringLiteral("yes") } };
    EXPECT_TRUE(AppPrefs::fromJson(wrong, back).colorStyleChosen);  // skipped: back's stays
    EXPECT_EQ(AppPrefs::fromJson(QJsonObject{ { QStringLiteral("legoColor"), QJsonValue() } }, back).legoColor, 0);
}

// The app follows at once: a LEGO color becomes the accent (lightened in
// dark mode), and "The whole app" tints the toolbars and panel tops.
TEST_F(Theme, ALegoColorAndTheWholeAppApplyAtOnce) {
    PrefsStore store(QString::fromLatin1(kGroup));
    ThemeManager manager(store);
    AppPrefs p;
    p.theme = ThemeChoice::Dark;
    p.legoColor = 26;  // Black
    p.colorStyle = QStringLiteral("original");
    p.colorStyleChosen = true;
    store.update(p);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Accent)), QStringLiteral("#646E75"));
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::HighlightedText)), QStringLiteral("#FFFFFF"));
    EXPECT_FALSE(qApp->styleSheet().contains(QStringLiteral("QMainWindow { background:")));  // just the highlights
    p.colorStyle = QStringLiteral("full");
    store.update(p);
    const ChromeTheme c = chromeTheme(QColor(QLatin1String("#646E75")), Mode::Dark);
    EXPECT_TRUE(qApp->styleSheet().contains(QStringLiteral("QWidget#PanelHeader { background: %1").arg(c.chrome.name())));
    EXPECT_TRUE(qApp->styleSheet().contains(QStringLiteral("QToolBar[mainToolbar=\"true\"] { background: %1").arg(c.chrome.name())));
    // A preset and just the highlights: the original look again.
    p.legoColor = 0;
    p.accent = QStringLiteral("ocean");
    p.colorStyle = QStringLiteral("original");
    store.update(p);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Accent)), QStringLiteral("#2459C4"));
    EXPECT_FALSE(qApp->styleSheet().contains(QStringLiteral("QMainWindow { background:")));
}
