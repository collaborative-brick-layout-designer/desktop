#pragma once

// A whole theme from any one color: the accent (buttons, focus rings, the
// selected item), its soft tint and accent text, and for "The whole app"
// the tinted toolbars, menu bar, panel headers and status bar and the
// window behind them.
//
// Every value is worked out so it reads, whatever the color (WCAG 2.1):
// - the accent stands out from the panel (at least 3:1), so in dark mode a
//   dark color is lightened and in light mode a pale one darkened; the
//   text on it, white or dark ink, whichever reads better, is at least
//   4.5:1 (the color moves further until one does);
// - accent text is at least 4.5:1 on every surface it sits on;
// - the toolbar and window tints keep the normal and the muted text at
//   4.5:1 or better (the tint gets fainter until they do).
// Warning and error colors (danger, ok) never change.
//
// The web app does the same sums (apps/web/src/theme/colorTheme.ts); the
// tests in both pin the same results for White, Black, Bright Yellow and
// Earth Blue, so the two apps agree.

#include "Tokens.h"

#include <QColor>

namespace bld::ui::theme {

struct AppPrefs;

// WCAG contrast ratio, 1 to 21.
double contrast(const QColor& a, const QColor& b);
// `pct` percent of the way from `a` to `b`, each channel rounded.
QColor mixColors(const QColor& a, const QColor& b, int pct);

struct ColorTheme {
    QColor main, onMain, soft, text;
};
ColorTheme colorTheme(const QColor& base, Mode mode);

struct ChromeTheme {
    QColor chrome;      // toolbars, menu bar, panel headers, status bar
    QColor chromeLine;  // their edges
    QColor bg;          // the window behind the panels
};
ChromeTheme chromeTheme(const QColor& main, Mode mode);

// The accent the app uses now: a LEGO color's (worked out for `mode`) or
// the chosen preset's.
Accent effectiveAccent(const AppPrefs& prefs, Mode mode);

}  // namespace bld::ui::theme
