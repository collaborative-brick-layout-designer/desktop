#pragma once

// Crash recovery. Each open layout autosaves to a file of its own in
// AppData/autosave/, so two copies of the app never write over, or delete,
// each other's: "<id>.bld-layout", with "<id>.json" saying which file it was
// editing and when, and "<id>.lock" held while a window owns it. An autosave
// whose lock nobody holds was left by a copy of the app that closed before
// saving (a crash, a power cut): a leftover, offered at start and in
// File › Recover Unsaved Work…. Older versions kept one autosave.bld-layout
// in AppData; it is a leftover too, so nothing is lost on updating.

#include <QDateTime>
#include <QList>
#include <QString>

#include <memory>

class QLockFile;

namespace bld::ui {

struct AutosaveLeftover {
    QString id;         // "<id>" of the files; "legacy" for the old single autosave
    QString layoutPath; // the autosaved .bld-layout
    QString sourceFile; // the file it was editing; empty for a new layout
    QDateTime savedAt;
    bool legacy() const { return id == QLatin1String("legacy"); }
    // "corner.bbm", or "New layout" for one never saved.
    QString displayName() const;
};

class AutosaveSlot {
public:
    AutosaveSlot();
    ~AutosaveSlot();
    AutosaveSlot(const AutosaveSlot&) = delete;
    AutosaveSlot& operator=(const AutosaveSlot&) = delete;

    // Where this layout autosaves; takes the slot (its lock) the first time.
    QString path();
    // After a successful write to path(): which file it was editing.
    void noteWritten(const QString& sourceFile);
    // The layout was saved or is back as it was saved: its autosave goes
    // (only this window's own). The slot stays this layout's.
    void removeFile();
    // The layout is closed (saved or discarded): its autosave goes, and the
    // next layout gets a slot of its own.
    void release();
    // Takes over a leftover being recovered: it becomes this window's
    // layout's autosave. False when another copy of the app took it first.
    bool adopt(const AutosaveLeftover& leftover);
    bool hasFile() const;

    // AppData/autosave.
    static QString folder();
    // Autosaves no running copy of the app owns, newest first.
    static QList<AutosaveLeftover> leftovers();
    // Deletes a leftover for good.
    static bool discard(const AutosaveLeftover& leftover);

private:
    QString id_;
    std::unique_ptr<QLockFile> lock_;
};

} // namespace bld::ui
