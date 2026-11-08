#pragma once

// Status-bar messages about a file name the file by its name ("Opened
// fordyce-2026.bbm"), with the full path as the status bar's tooltip until
// the next message replaces it.

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QStatusBar>
#include <QString>

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

}  // namespace bld::ui
