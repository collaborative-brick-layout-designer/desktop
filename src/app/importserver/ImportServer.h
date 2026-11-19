#pragma once

// The website's model import (docs/import-server.md): the server uploads an
// LDraw, Studio or LDD file and runs bld-import-server on it, which calls
// exactly what the app's Tools › Import (ui/ImportPipeline) and File › Open
// (import/mapformats/LDrawMap) call, so the result is the app's.
//
//   analyse   what the preview shows: the part as prepared (picture, size,
//             connection points, counts, warnings), and for .ldr / .mpd also
//             the layout File › Open would make of it
//   convert   the result for the choices made in the preview: a custom part
//             (its XML and picture, as Send to Server uploads them) or a
//             .bbm of the parts placed
//
// Requests and results are JSON. Nothing here reads the network or writes
// outside the request's out folder (and the temporary folder a Studio file's
// own parts are unpacked into).

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace bld::importserver {

// Archive limits checked before any reader sees a .io / .lxf (zip) file.
struct ArchiveLimits {
    int    maxEntries = 5000;
    qint64 maxTotalBytes = qint64(768) << 20;  // declared uncompressed, all entries
    double maxRatio = 100.0;                   // uncompressed / compressed, per entry over 1 MB
};

struct Libraries {
    QStringList parts;     // BlueBrickParts folders (snap points, the fallback picture)
    QString ldraw;         // the official LDraw library (complete.zip, unpacked)
    QString studioLdraw;   // Studio's own LDraw library, optional
    QString lddAssets;     // LDD's Assets.lif / db.lif or its folder, optional
    QString lddLdrawXml;   // a newer ldraw.xml than LDD's own, optional
};

// The request the server writes (the "request" object of docs/import-server.md).
struct Request {
    QString command;       // "analyse" or "convert"
    QString input;         // the uploaded file, saved under its own name
    QString outDir;        // where results go; must exist
    Libraries libraries;
    qint64 maxInputBytes = qint64(64) << 20;
    ArchiveLimits archive;
    // convert only
    QString mode = QStringLiteral("part");  // "part" (one custom part) or "parts" (the layout)
    int quarterTurns = 0;
    QString align;         // "", "bottom" or "box"
    double nudgeX = 0, nudgeY = 0;
    QList<int> drop;       // connection points left unchecked, by index
    QString name;          // "Save as"
    QString designer;
    QString designerUrl;
    QString author;
};

// Reads the request JSON; false with *error set when it is malformed.
bool parseRequest(const QJsonObject& json, Request* out, QString* error);

// Runs the request. The result always has "ok"; on failure "error" (a code
// from docs/import-server.md) and "message" (in words, English).
QJsonObject run(const Request& request);

// The archive check alone, for tests: empty when `path` (a zip) is within
// `limits`, else the error code.
QString checkArchive(const QString& path, const ArchiveLimits& limits);

}  // namespace bld::importserver
