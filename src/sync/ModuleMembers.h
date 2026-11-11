#pragma once

// A module's parts in the shared layout, one key per part (doc shape 3;
// the web's packages/ydoc moduleMembers.ts; docs/module-members.md).
//
// A module is one value, sidecarItems["m:<id>"], and its `members` list
// used to be part of that value: two people changing one module's parts at
// the same instant each wrote the whole value, and one of the two changes
// was lost. Now the value keeps the list the module was made with (its
// starting list, never rewritten), and each part added or taken out since
// has its own key in the same map:
//
//   p:<n>:<moduleId>:<partId>      n: the module id's length (UTF-16 units)
//
//   a number (finite, 0 or more)   the part is in the module; where it
//                                  sorts among the parts added this way
//   false                          the part was taken out of the starting list
//
// The module's parts: the starting list, in order, without the ids whose
// key is false; then the ids whose key is a number and that aren't in the
// starting list, by number then id. A layout from before this (a list, no
// keys) reads as ever, so nothing is converted on opening. The first `p:`
// key moves the doc to shape 3, so builds that read only the starting
// lists refuse it. Shared vectors: fixtures/contracts/module-members.json.

#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

namespace bld::sync {

// The shape that keeps a module's part changes one per key.
constexpr int kDocShapeMemberKeys = 3;

constexpr char kMemberPrefix[] = "p:";

// The sidecarItems key of part `partId` in module `moduleId`.
QString memberKey(const QString& moduleId, const QString& partId);

struct MemberKey {
    QString moduleId;
    QString partId;
};
// The module and part a `p:` key names; nothing when it isn't one a writer makes.
std::optional<MemberKey> parseMemberKey(const QString& key);

// Whether `v` is a value a `p:` key may hold.
bool validMemberValue(const QJsonValue& v);

// A module's keys: part id -> value.
using MemberKeys = QHash<QString, QJsonValue>;

// A module's parts: its starting list (`base`, the `members` of its value;
// anything but an array reads as none) with what the keys changed. Values
// that aren't valid are skipped.
QStringList moduleMembers(const QJsonValue& base, const MemberKeys& keys);

struct MemberWrites {
    // Keys to set, in order: part id and value (a number or false).
    QList<std::pair<QString, QJsonValue>> set;
    // Part ids whose key goes.
    QStringList remove;
};
// What to write so a module with starting list `base` and `keys` holds the
// parts `want`.
MemberWrites memberWrites(const QJsonValue& base, const MemberKeys& keys, const QStringList& want);

}  // namespace bld::sync
