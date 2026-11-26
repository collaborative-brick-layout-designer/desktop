#pragma once

// Operating sessions (Live Ops design 2.5, 4): who plays which role, what
// each role may move and flip while a session runs, the session's log, and
// putting cars back where the session started. Worked out from the records
// alone, so both apps and the server (the web's apps/server/src/ws/
// opsGuard.ts) agree.
//
// What a role reaches, while the session runs:
//   - the host (who started it) and the layout's owner: every car and switch;
//   - the dispatcher: every car and switch;
//   - a yardmaster: the cuts standing on their yards (the yard's module, or
//     its spots' track for a place on loose track), and the switches there;
//   - a crew: while their job is out, their job's train anywhere, the cuts
//     standing at their job's stops, and the switches there.
// Nobody changes the map's sheets while a session runs; the host pauses it.
//
// The web's packages/parts-catalog/src/track/opsSession.ts is the twin;
// both check fixtures/contracts/ops-session.json.

#include "Ops.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace bld::track {

// What the apps hold a layout to (the server takes twice the counts).
inline constexpr int kMaxSessions = 50;
inline constexpr int kMaxCrew = 100;
inline constexpr int kMaxLogEntries = 20000;
inline constexpr int kMaxLogEntrySize = 2048;
inline constexpr int kMaxSessionSize = 1024 * 1024;
inline constexpr int kMaxClockFast = 24;

struct CrewMember {
    QString userId;  // empty: someone on this device only (the server gives them nothing)
    QString name;
    QString role;    // dispatcher, yardmaster, crew
    QStringList places;  // a yardmaster's yards
    QString job;         // a crew's job
};

struct SessionStart {
    QJsonObject trains;  // id -> { position, units }
    QJsonObject cars;    // id -> its cut ('' none)
};

struct OpsSession {
    QString id;
    QString name;
    QString status = QStringLiteral("setup");  // setup, running, paused, ended
    double seed = 0;
    QString layout;  // the layout it was started in
    QString host;
    std::vector<CrewMember> crew;
    int clockFast = 0;
    QString clockStart;
    double startedAt = 0;
    double endedAt = 0;
    std::optional<SessionStart> start;
};
OpsSession asSession(const QJsonObject& r);

struct LogEntry {
    QString id, session;
    double at = 0;
    QString by, kind, what;
    QJsonValue from, to;
    QString note;
};
LogEntry asLogEntry(const QJsonObject& r);

// A job's status in a session (the record's `status`): waiting, out or done.
QString jobStatus(const QJsonObject& jobRecord);

std::vector<OpsSession> runningSessions(const std::vector<OpsSession>& sessions);
bool sheetsLocked(const std::vector<OpsSession>& sessions);

struct ReachPlace {
    QString id;
    QStringList bricks;
};

struct OpsReach {
    bool all = false;
    QString role;  // host, dispatcher, yardmaster, crew; empty: not in the running session
    QSet<QString> bricks;
    QSet<QString> trains;
    QString why;   // why they may do nothing, in the words people see
};

// `jobs` pairs each job with its record's status.
OpsReach opsReach(const std::vector<OpsSession>& sessions, const std::vector<std::pair<Job, QString>>& jobs, const std::vector<ReachPlace>& places,
                  const QString& userId, const QString& layoutId, bool owner);

QString switchBrick(const QString& key);
bool mayFlip(const OpsReach& reach, const QString& key);
QString standsOn(const QJsonValue& position);
bool mayMove(const OpsReach& reach, const QString& id, const QJsonValue& position);
bool mayPut(const OpsReach& reach, const QString& id, const QJsonValue& position);

// Trains and cars as plain records (id, position, units; id, opsCut).
SessionStart sessionStart(const QJsonArray& trains, const QJsonArray& cars);
struct PutBack {
    QJsonArray trains;  // { id, position, units }
    QJsonObject cars;   // id -> cut
    QStringList emptied;
};
PutBack putBack(const SessionStart& start, const QJsonArray& trains, const QJsonArray& cars);
QJsonObject replayLog(const SessionStart& start, const std::vector<LogEntry>& log, const QString& session);
struct SessionSummary {
    int moves = 0, cuts = 0, switches = 0;
    QStringList jobsLeft;
};
SessionSummary sessionSummary(const QString& session, const std::vector<LogEntry>& log, const std::vector<std::pair<Job, QString>>& jobs);
QStringList carsInMotion(const QJsonArray& trains, const std::function<bool(const QString&)>& onSpot);

}  // namespace bld::track
