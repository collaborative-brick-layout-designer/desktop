#pragma once

// Every solid LEGO color, for the theme color picker (Settings > Color >
// Any LEGO color): LEGO's number and name, BrickLink's number and name,
// and the color. The list itself is generated (LegoColors.cpp, from
// scripts/gen-lego-colors.py); the web app has the same one.

#include <QColor>
#include <QList>
#include <QString>

namespace bld::ui::theme {

struct LegoColor {
    int id = 0;        // LEGO's color number: what the setting stores
    QString lego;      // LEGO's name, e.g. "Bright Yellow" (a proper name: never translated)
    int blId = 0;      // BrickLink's number, or 0 when BrickLink has no match
    QString bl;        // BrickLink's name, e.g. "Yellow"; empty when it has no match
    QColor color;
    bool pearl = false;  // pearl and metallic colors are used as their flat color
};

// Solid colors first, each group by LEGO name.
const QList<LegoColor>& legoColors();
// The color with LEGO number `id`, or nullptr.
const LegoColor* legoColor(int id);

// Does `c` match every word of `query`, by either name or either number?
// Case, punctuation and "grey"/"gray" don't matter (the web's picker
// matches the same way).
bool legoColorMatches(const LegoColor& c, const QString& query);

}  // namespace bld::ui::theme
