// Searching the LEGO color list (LegoColors.h); kept apart from the
// generated table.

#include "LegoColors.h"

#include <QRegularExpression>

namespace bld::ui::theme {

namespace {
// Lower case, "grey" as "gray", words split on anything but letters and digits.
QString fold(const QString& s) {
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    QString out = s.toLower();
    out.replace(QLatin1String("grey"), QLatin1String("gray"));
    out.replace(nonWord, QStringLiteral(" "));
    return out.trimmed();
}
}  // namespace

bool legoColorMatches(const LegoColor& c, const QString& query) {
    const QString q = fold(query);
    if (q.isEmpty()) return true;
    const QString hay = QLatin1Char(' ') + fold(c.lego) + QLatin1Char(' ') + fold(c.bl) + QLatin1Char(' ');
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    for (const QString& word : q.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (digits.match(word).hasMatch()) {
            if (QString::number(c.id) != word && !(c.blId > 0 && QString::number(c.blId) == word)) return false;
        } else if (!hay.contains(word)) {
            return false;
        }
    }
    return true;
}

}  // namespace bld::ui::theme
