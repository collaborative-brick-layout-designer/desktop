#pragma once

// Presence in the web editor's shape (apps/web/src/editor/awareness.ts),
// built and read the way the web does so cursors look the same in both:
//   { user: {id, displayName, avatarUrl, color}, cursor: {x, y, layerId} | null,
//     selection: {brickIds}, tool, editingModule, lastActivityMs }

#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

namespace bld::sync::presence {

// Presence colors, the same in both apps (web apps/web/src/editor/presenceColors.ts;
// fixtures/contracts/presence-colors.json checks they agree). Each person has a
// first choice on a layout, from a well-mixed hash of their id and the layout's;
// the people here, taken in order of their ids, each get it unless someone
// before them has it, else the next color along nobody has. Two people share a
// color only when more than eight are here.
int firstChoice(const QString& userId, const QString& layoutId);
// The first-choice color (what this app sends as its own `user.color`).
QString colorFor(const QString& userId, const QString& layoutId);
// The color of each person here (`userIds`, yourself included).
QHash<QString, QString> colorsFor(const QStringList& userIds, const QString& layoutId);

struct User {
    QString id;
    QString displayName;
    QString color;
};

// `editingModule`: the placed module being edited ("Edit module"), or empty.
QJsonObject state(const User& user, std::optional<QPointF> cursorStuds, const QStringList& brickIds,
                  qint64 nowMs, const QString& editingModule = {});

// Someone else, read from their state (missing parts are empty).
struct Peer {
    QString userId;
    // A person's presence (a name, a selection, a last-activity time); not an
    // empty one from an app still opening the layout.
    bool person = false;
    QString name;
    QString color;
    std::optional<QPointF> cursor;
    QStringList brickIds;
    QString editingModule;  // empty when not editing a module
};
Peer peerFrom(const QJsonObject& state);

// The on-map label: names over 20 characters are cut with an ellipsis, as on the web.
QString label(const QString& name);

} // namespace bld::sync::presence
