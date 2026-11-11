#include "ColorTheme.h"

#include "AppPrefs.h"
#include "LegoColors.h"


#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace bld::ui::theme {

namespace {

const QColor kWhite(255, 255, 255);
const QColor kBlack(0, 0, 0);
// The dark ink used on light accents (the light theme's text color).
const QColor kDarkInk(QLatin1String("#1E2124"));

double channel(int v) {
    const double s = v / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& c) {
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

// The first color from `c` toward `target` that is at least 4.5:1 on all
// of `surfaces`.
QColor readableOn(const QColor& c, std::initializer_list<QColor> surfaces, const QColor& target) {
    for (int pct = 0; pct <= 100; ++pct) {
        const QColor t = mixColors(c, target, pct);
        if (std::all_of(surfaces.begin(), surfaces.end(), [&t](const QColor& s) { return contrast(t, s) >= 4.5; }))
            return t;
    }
    return target;
}

// The strongest tint of `surface` toward `color`, up to `maxPct` percent,
// that keeps `ink` and `muted` at 4.5:1.
QColor tint(const QColor& surface, const QColor& color, int maxPct, const QColor& ink, const QColor& muted) {
    for (int pct = maxPct; pct > 0; --pct) {
        const QColor t = mixColors(surface, color, pct);
        if (contrast(ink, t) >= 4.5 && contrast(muted, t) >= 4.5) return t;
    }
    return surface;
}

}  // namespace

double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor mixColors(const QColor& a, const QColor& b, int pct) {
    // Rounded as JavaScript's Math.round does (half up), so the web agrees.
    const auto ch = [pct](int x, int y) { return static_cast<int>(std::floor(x + ((y - x) * pct) / 100.0 + 0.5)); };
    return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
}

ColorTheme colorTheme(const QColor& base, Mode mode) {
    const Neutrals& n = neutrals(mode);
    // Away from the panel first: darker in light mode, lighter in dark mode.
    const QColor away = mode == Mode::Light ? kBlack : kWhite;
    const QColor toward = mode == Mode::Light ? kWhite : kBlack;
    QColor main = base;
    bool found = false;
    for (int pct = 0; pct <= 100 && !found; ++pct) {
        for (const QColor& target : { away, toward }) {
            const QColor c = mixColors(base, target, pct);
            if (contrast(c, n.panel) >= 3 && std::max(contrast(c, kWhite), contrast(c, kDarkInk)) >= 4.5) {
                main = c;
                found = true;
                break;
            }
        }
    }
    ColorTheme t;
    t.main = main;
    t.onMain = contrast(main, kWhite) >= contrast(main, kDarkInk) ? kWhite : kDarkInk;
    t.soft = mixColors(n.panel, main, mode == Mode::Light ? 12 : 20);
    t.text = readableOn(main, { n.bg, n.panel, n.soft, t.soft }, away);
    return t;
}

ChromeTheme chromeTheme(const QColor& main, Mode mode) {
    const Neutrals& n = neutrals(mode);
    const bool light = mode == Mode::Light;
    ChromeTheme c;
    c.chrome = tint(n.panel, main, light ? 16 : 22, n.ink, n.muted);
    c.chromeLine = mixColors(n.line, main, light ? 30 : 35);
    c.bg = tint(n.bg, main, light ? 6 : 8, n.ink, n.muted);
    return c;
}

Accent effectiveAccent(const AppPrefs& prefs, Mode mode) {
    const LegoColor* lego = legoColor(prefs.legoColor);
    if (!lego) return accent(prefs.accent);
    const ColorTheme t = colorTheme(lego->color, mode);
    // One mode's colors in both slots: the app rebuilds this when the mode changes.
    return Accent{ QStringLiteral("lego"), lego->lego, t.main, t.onMain, t.soft, t.soft, t.text, t.text };
}

}  // namespace bld::ui::theme
