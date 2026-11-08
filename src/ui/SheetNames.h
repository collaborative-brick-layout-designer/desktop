#pragma once

// A sheet's name as shown. BlueBrick names new layers "Layer 1", "Layer 2"…;
// this app says "sheet", so an untouched default shows as "Sheet 1". The
// map keeps "Layer 1" (a .bbm export is byte for byte what was read), and a
// name someone typed is shown as typed. The web does the same
// (activeSheet.ts displaySheetName).

#include "../core/Layer.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QString>

namespace bld::ui {

inline QString displaySheetName(const QString& name) {
    static const QRegularExpression re(QStringLiteral("^Layer (\\d+)$"));
    const auto m = re.match(name);
    return m.hasMatch() ? QCoreApplication::translate("SheetNames", "Sheet %1").arg(m.captured(1)) : name;
}

// The name shown for `layer`, "(untitled)" when it has none.
inline QString shownSheetName(const core::Layer& layer) {
    return layer.name.isEmpty() ? QCoreApplication::translate("SheetNames", "(untitled)") : displaySheetName(layer.name);
}

// The name to save from a rename box, or empty when nothing changed (the
// shown default "Sheet 1" kept, or the box emptied).
inline QString renamedSheet(const QString& current, const QString& typed) {
    QString next = typed.trimmed();
    if (next.isEmpty() || next == current || next == displaySheetName(current)) return {};
    return next;
}

}  // namespace bld::ui
