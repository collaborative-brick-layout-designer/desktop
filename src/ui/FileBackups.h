#pragma once

// Earlier saves of each layout file. Just before a save writes over a file,
// what the file held is copied to AppData/file-backups/<file's key>/, named
// by the time it was copied ("20261124-153012-123.bld-layout"; a .bbm's
// labels, modules and venue go beside it as "….bbm.bld"). The newest N are
// kept per file (Settings › Saving, 10 by default; 0 keeps none). File ›
// Earlier Saves… lists them: open one as a new, unsaved layout, or put it
// back in place of the file (the file as it is then is kept first).
//
// Crash recovery (Autosave.h) is a different thing: changes that were never
// saved. These are copies of what was saved.

#include <QDateTime>
#include <QList>
#include <QString>

namespace bld::ui {

struct SavedCopy {
    QString path;    // the copy
    QString sidecar; // its .bld beside it (a .bbm's), or empty
    QDateTime at;    // when it was copied (when the save after it happened)
    qint64 bytes = 0;
};

struct BackedUpFile {
    QString file; // the layout file the copies are of
    int copies = 0;
    QDateTime newest;
};

class FileBackups {
public:
    // AppData/file-backups.
    static QString folder();
    // The folder that holds one file's copies.
    static QString folderFor(const QString& file);

    // How many earlier saves to keep of each file (Settings › Saving).
    static int keepCount();
    static void setKeepCount(int count);
    static constexpr int kDefaultKeep = 10;
    static constexpr int kMaxKeep = 100;

    // Just before a save writes over `file`: copy what is there now (and a
    // .bbm's sidecar). Nothing to do when the file isn't there yet, when
    // keeping is off, or when the newest copy is the same. False, with
    // `error`, only when a copy was wanted and couldn't be made.
    static bool keepBeforeSaving(const QString& file, QString* error = nullptr,
                                 const QDateTime& now = QDateTime::currentDateTime());

    // A file's copies, newest first.
    static QList<SavedCopy> copiesOf(const QString& file);
    // Every file with copies, newest first.
    static QList<BackedUpFile> files();
    // Keep the newest `keep` copies of `file`.
    static void trim(const QString& file, int keep);

    // Put `copy` back in place of `file`: the file as it is is kept first
    // (as an earlier save), then the copy is written over it whole (and a
    // .bbm's sidecar with it, or removed when the copy had none).
    static bool restore(const SavedCopy& copy, const QString& file, QString* error = nullptr);
};

} // namespace bld::ui
