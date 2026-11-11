#pragma once

// The web editor's layout model (as the shared document stores it, see
// DocJson.h) mapped onto core::Map. Sync phase P3a: the desktop reads a
// server layout. Field names follow the web's @cld/model BbmMap; value
// conventions follow the desktop's .bbm reader, so a layout that came from
// a .bbm writes back to the same bytes.

#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

namespace bld::core { class Map; struct Module; }

namespace bld::sync {

// nullptr (and *error set) when the document lacks its header or a layer
// is malformed. Sidecar data (labels, modules, venue) isn't mapped yet.
std::unique_ptr<core::Map> mapFromDocJson(const QJsonObject& doc, QString* error = nullptr);

// A part deleted at the same instant someone adds it to a module stays
// listed in the module (the delete took it out of the modules it was in
// then; the other person's write lists it again). Module members that
// aren't on the map (no part, text or ruler with that id) are dropped, and
// a module left with none goes, as deleting its last part takes it. Both
// readers call it; SyncDoc::writeMap writes modules without them too. The
// web does the same (packages/ydoc readSidecarCache). True when it changed.
bool dropGoneModuleMembers(std::vector<core::Module>& modules, const core::Map& map);

// mapFromDocJson for a document read again after each change (a live
// layout): what each sheet, part, text cell, area cell and ruler, and the
// sidecar, turned into is kept with the JSON it came from, and one whose
// JSON is the same next time is copied instead of converted again. The
// kept document JSON (SyncDoc::toJson) shares what a change left alone, so
// telling those apart costs next to nothing; anything else is compared by
// value, and converted when it differs. The map is always mapFromDocJson's.
class MapReader {
public:
    MapReader();
    ~MapReader();
    MapReader(const MapReader&) = delete;
    MapReader& operator=(const MapReader&) = delete;

    std::unique_ptr<core::Map> read(const QJsonObject& doc, QString* error = nullptr);
    // Forget what was kept (the next read converts everything).
    void clear();

    // Parts, text cells, area cells and rulers converted so far, by either
    // function (the perf tests count them).
    static qint64 entriesConverted();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace bld::sync
