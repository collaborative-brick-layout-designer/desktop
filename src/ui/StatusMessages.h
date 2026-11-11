#pragma once

// Status-bar messages about a file name the file by its name ("Opened
// fordyce-2026.bbm"), with the full path as the status bar's tooltip until
// the next message replaces it.

#include <QDir>
#include <QFileInfo>
#include <QLayout>
#include <QObject>
#include <QSizeGrip>
#include <QStatusBar>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <vector>

namespace bld::ui {

inline QString fileNameOf(const QString& path) { return QFileInfo(path).fileName(); }

inline void showFileMessage(QStatusBar* bar, const QString& text, const QString& path, int timeout = 0) {
    bar->showMessage(text, timeout);
    bar->setToolTip(QDir::toNativeSeparators(path));
    auto* once = new QMetaObject::Connection;
    *once = QObject::connect(bar, &QStatusBar::messageChanged, bar, [bar, once](const QString&) {
        bar->setToolTip(QString());
        QObject::disconnect(*once);
        delete once;
    });
}

// Every readout in the status bar is a permanent widget, and a message only
// gets the room they leave, so in a narrow window a message was cut short or
// didn't show at all. While a message is up, the readouts nearest to it step
// aside (shrunk to nothing, never hidden, so code that shows or hides them
// isn't crossed) until it fits; they come back when it goes.
inline void letMessagesFit(QStatusBar* bar) {
    auto* aside = new std::vector<std::pair<QWidget*, int>>;
    QObject::connect(bar, &QObject::destroyed, bar, [aside] { delete aside; });
    QObject::connect(bar, &QStatusBar::messageChanged, bar, [bar, aside](const QString& text) {
        for (auto [w, maxWidth] : *aside) w->setMaximumWidth(maxWidth);
        aside->clear();
        if (text.isEmpty()) return;
        std::vector<QWidget*> readouts;
        const int gap = std::max(0, bar->layout() ? bar->layout()->spacing() : 0);
        int used = 0;
        for (QWidget* w : bar->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
            if (!w->isVisible() || qobject_cast<QSizeGrip*>(w)) continue;
            readouts.push_back(w);
            used += w->width() + gap;
        }
        std::sort(readouts.begin(), readouts.end(), [](QWidget* a, QWidget* b) { return a->x() < b->x(); });
        const int need = bar->fontMetrics().horizontalAdvance(text) + 48;  // the margins it is drawn in
        // The rest keep the width they have, or they would take the room back.
        for (QWidget* w : readouts) {
            aside->emplace_back(w, w->maximumWidth());
            if (bar->contentsRect().width() - used >= need) {
                w->setMaximumWidth(w->width());
                continue;
            }
            used -= w->width() + gap;
            w->setMaximumWidth(0);
        }
    });
}

}  // namespace bld::ui
