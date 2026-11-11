#pragma once

// The web keeps the shared layout's modules and anchored labels twice: in
// meta.cache (the whole sidecar, one value) and one per key in the
// `sidecarItems` root map ("m:<id>" / "l:<id>", each with its `_order`), so
// two people changing different ones at once both keep their change.
// `sidecarItems.__blob` records which meta.cache the items go with: a hash
// of the blob's key-sorted JSON (web packages/ydoc sidecarBlobHash). Reading
// uses the items only when that hash matches; writing writes both.
//
// A module's value keeps the `members` list it was made with; each part
// added to it or taken out since is a `p:` key of its own (ModuleMembers.h),
// so two people changing one module's parts at once both keep their change.
// meta.cache has every module's full list.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace bld::sync {

// JSON with every object's keys sorted, numbers and strings written as
// JavaScript's JSON.stringify writes them.
QString stableJson(const QJsonValue& value);

// "<UTF-16 length>:<FNV-1a hex>" of stableJson(blob), as the web computes it.
QString sidecarBlobHash(const QJsonValue& blob);

// The sidecar as everyone's edits left it: `blob` with its modules and
// anchored labels taken from `items` when they go with this blob.
// False when a module or label has no id or shares one (read the blob as is).
bool sidecarIdsUnique(const QJsonObject& blob);
QJsonObject mergedSidecarCache(const QJsonObject& blob, const QJsonObject& items);

// What `sidecarItems` should hold for `blob` in a layout that has none
// yet: one entry per module and anchored label, and `__blob`.
QJsonObject sidecarItemsFor(const QJsonObject& blob);

// What to write so the layout holds the sidecar `cache`, given what it
// holds now (`curBlob`: meta.cache, `curItems`: sidecarItems):
//   items: every `m:`, `l:` and `p:` key sidecarItems should have, and
//          `__blob`. A module that is already there keeps its starting
//          list, and the parts added to it or taken out are `p:` keys.
//   blob:  meta.cache, each module's parts in the order the keys give (so
//          every writer, here and on the web, writes the same).
// As the web's writeSidecarCache.
struct SidecarWrite {
    QJsonObject items;
    QJsonObject blob;
};
SidecarWrite sidecarWriteFor(const QJsonObject& cache, const QJsonObject& curBlob, const QJsonObject& curItems);

// Whether `items` holds a module part key (the doc is then shape 3).
bool hasMemberKeys(const QJsonObject& items);

}  // namespace bld::sync
