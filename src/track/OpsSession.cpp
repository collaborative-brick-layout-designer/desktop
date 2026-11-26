#include "OpsSession.h"

#include <QJsonDocument>

#include <algorithm>
#include <map>

namespace bld::track {

namespace {

QString text(const QJsonValue& v) { return v.isString() ? v.toString() : QString(); }
double num(const QJsonValue& v) { return v.isDouble() ? v.toDouble() : 0.0; }
const QStringList& statuses() {
    static const QStringList s{ QStringLiteral("setup"), QStringLiteral("running"), QStringLiteral("paused"), QStringLiteral("ended") };
    return s;
}
const QStringList& roles() {
    static const QStringList r{ QStringLiteral("dispatcher"), QStringLiteral("yardmaster"), QStringLiteral("crew") };
    return r;
}
QString compact(const QJsonValue& v) { return QString::fromUtf8(QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact)); }
bool same(const QJsonValue& a, const QJsonValue& b) {
    const auto n = [](const QJsonValue& v) { return v.isUndefined() ? QJsonValue(QJsonValue::Null) : v; };
    return compact(n(a)) == compact(n(b));
}

}  // namespace

OpsSession asSession(const QJsonObject& r) {
    OpsSession s;
    s.id = text(r.value(QLatin1String("id")));
    s.name = text(r.value(QLatin1String("name")));
    const QString status = text(r.value(QLatin1String("status")));
    s.status = statuses().contains(status) ? status : QStringLiteral("setup");
    s.seed = num(r.value(QLatin1String("seed")));
    s.layout = text(r.value(QLatin1String("layout")));
    s.host = text(r.value(QLatin1String("host")));
    const QJsonArray crew = r.value(QLatin1String("crew")).toArray();
    for (qsizetype i = 0; i < crew.size() && i < qsizetype{ 2 } * kMaxCrew; ++i) {
        if (!crew[i].isObject()) continue;
        const QJsonObject c = crew[i].toObject();
        const QString role = text(c.value(QLatin1String("role")));
        if (!roles().contains(role)) continue;
        CrewMember m{ text(c.value(QLatin1String("userId"))), text(c.value(QLatin1String("name"))), role, {}, text(c.value(QLatin1String("job"))) };
        for (const auto& p : c.value(QLatin1String("places")).toArray())
            if (p.isString()) m.places << p.toString();
        s.crew.push_back(std::move(m));
    }
    s.clockFast = std::clamp(static_cast<int>(num(r.value(QLatin1String("clockFast")))), 0, kMaxClockFast);
    s.clockStart = text(r.value(QLatin1String("clockStart")));
    s.startedAt = num(r.value(QLatin1String("startedAt")));
    s.endedAt = num(r.value(QLatin1String("endedAt")));
    const QJsonValue st = r.value(QLatin1String("start"));
    if (st.isObject() && st.toObject().value(QLatin1String("trains")).isObject() && st.toObject().value(QLatin1String("cars")).isObject()) {
        SessionStart out;
        const QJsonObject trains = st.toObject().value(QLatin1String("trains")).toObject();
        for (auto it = trains.begin(); it != trains.end(); ++it) {
            if (!it.value().isObject()) continue;
            const QJsonObject t = it.value().toObject();
            const QJsonValue pos = t.value(QLatin1String("position"));
            const QJsonValue units = t.value(QLatin1String("units"));
            out.trains.insert(it.key(), QJsonObject{ { QStringLiteral("position"), pos.isUndefined() ? QJsonValue(QJsonValue::Null) : pos }, { QStringLiteral("units"), units.isUndefined() ? QJsonValue(QJsonArray()) : units } });
        }
        const QJsonObject cars = st.toObject().value(QLatin1String("cars")).toObject();
        for (auto it = cars.begin(); it != cars.end(); ++it) out.cars.insert(it.key(), text(it.value()));
        s.start = out;
    }
    return s;
}

LogEntry asLogEntry(const QJsonObject& r) {
    const auto orNull = [](const QJsonValue& v) { return v.isUndefined() ? QJsonValue(QJsonValue::Null) : v; };
    return { text(r.value(QLatin1String("id"))), text(r.value(QLatin1String("session"))), num(r.value(QLatin1String("at"))), text(r.value(QLatin1String("by"))),
             text(r.value(QLatin1String("kind"))), text(r.value(QLatin1String("what"))), orNull(r.value(QLatin1String("from"))), orNull(r.value(QLatin1String("to"))),
             text(r.value(QLatin1String("note"))) };
}

QString jobStatus(const QJsonObject& jobRecord) {
    const QString s = text(jobRecord.value(QLatin1String("status")));
    return s == QLatin1String("out") || s == QLatin1String("done") ? s : QStringLiteral("waiting");
}

std::vector<OpsSession> runningSessions(const std::vector<OpsSession>& sessions) {
    std::vector<OpsSession> out;
    for (const auto& s : sessions)
        if (s.status == QLatin1String("running")) out.push_back(s);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return out;
}

bool sheetsLocked(const std::vector<OpsSession>& sessions) { return !runningSessions(sessions).empty(); }

OpsReach opsReach(const std::vector<OpsSession>& sessions, const std::vector<std::pair<Job, QString>>& jobs, const std::vector<ReachPlace>& places,
                  const QString& userId, const QString& layoutId, bool owner) {
    const auto none = [](const QString& why) {
        OpsReach r;
        r.why = why;
        return r;
    };
    const auto running = runningSessions(sessions);
    if (running.empty()) return none(QStringLiteral("No session is running."));
    if (running.size() > 1) return none(QStringLiteral("Two sessions are running; the host ends one."));
    const OpsSession& s = running.front();
    if (userId.isEmpty()) return none(QStringLiteral("You aren’t in this session’s crew."));
    if (owner || s.host == userId) {
        OpsReach r;
        r.all = true;
        r.role = QStringLiteral("host");
        return r;
    }
    if (s.layout != layoutId) return none(QStringLiteral("This session was started in another layout; the host starts it again here."));
    std::vector<CrewMember> me;
    for (const auto& c : s.crew)
        if (c.userId == userId) me.push_back(c);
    if (me.empty()) return none(QStringLiteral("You aren’t in this session’s crew."));
    OpsReach reach;
    reach.role = me.front().role;
    std::map<QString, const ReachPlace*> byId;
    for (const auto& p : places) byId.emplace(p.id, &p);
    std::map<QString, const std::pair<Job, QString>*> jobById;
    for (const auto& j : jobs) jobById.emplace(j.first.id, &j);
    bool crewWaiting = false;
    for (const auto& c : me) {
        if (c.role == QLatin1String("dispatcher")) {
            OpsReach r;
            r.all = true;
            r.role = QStringLiteral("dispatcher");
            return r;
        }
        if (c.role == QLatin1String("yardmaster")) {
            if (reach.role == QLatin1String("crew")) reach.role = QStringLiteral("yardmaster");
            for (const auto& id : c.places)
                if (auto it = byId.find(id); it != byId.end())
                    for (const auto& b : it->second->bricks) reach.bricks.insert(b);
            continue;
        }
        const auto jt = jobById.find(c.job);
        if (jt == jobById.end()) continue;
        if (jt->second->second != QLatin1String("out")) {
            crewWaiting = true;
            continue;
        }
        const Job& job = jt->second->first;
        if (!job.train.isEmpty()) reach.trains.insert(job.train);
        for (const auto& stop : job.stops)
            if (auto it = byId.find(stop.place); it != byId.end())
                for (const auto& b : it->second->bricks) reach.bricks.insert(b);
    }
    if (reach.bricks.isEmpty() && reach.trains.isEmpty())
        reach.why = crewWaiting ? QStringLiteral("Your job isn’t out yet; the dispatcher sends it out.") : QStringLiteral("Your role has nothing to move yet.");
    return reach;
}

QString switchBrick(const QString& key) {
    const qsizetype at = key.lastIndexOf(QLatin1Char('/'));
    return at > 0 ? key.left(at) : QString();
}

bool mayFlip(const OpsReach& reach, const QString& key) { return reach.all || reach.bricks.contains(switchBrick(key)); }

QString standsOn(const QJsonValue& position) {
    return position.isObject() && position.toObject().value(QLatin1String("brick")).isString() ? position.toObject().value(QLatin1String("brick")).toString() : QString();
}

bool mayMove(const OpsReach& reach, const QString& id, const QJsonValue& position) {
    if (reach.all || reach.trains.contains(id)) return true;
    const QString on = standsOn(position);
    return !on.isEmpty() && reach.bricks.contains(on);
}

bool mayPut(const OpsReach& reach, const QString& id, const QJsonValue& position) { return mayMove(reach, id, position); }

namespace {
std::vector<QJsonObject> byIdOrder(const QJsonArray& list) {
    std::vector<QJsonObject> out;
    for (const auto& v : list) out.push_back(v.toObject());
    std::sort(out.begin(), out.end(), [](const QJsonObject& a, const QJsonObject& b) { return text(a.value(QLatin1String("id"))) < text(b.value(QLatin1String("id"))); });
    return out;
}
}  // namespace

SessionStart sessionStart(const QJsonArray& trains, const QJsonArray& cars) {
    SessionStart out;
    for (const auto& t : byIdOrder(trains)) {
        const QJsonValue pos = t.value(QLatin1String("position"));
        const QJsonValue units = t.value(QLatin1String("units"));
        out.trains.insert(text(t.value(QLatin1String("id"))), QJsonObject{ { QStringLiteral("position"), pos.isUndefined() ? QJsonValue(QJsonValue::Null) : pos }, { QStringLiteral("units"), units.isUndefined() ? QJsonValue(QJsonArray()) : units } });
    }
    for (const auto& c : byIdOrder(cars)) out.cars.insert(text(c.value(QLatin1String("id"))), text(c.value(QLatin1String("opsCut"))));
    return out;
}

PutBack putBack(const SessionStart& start, const QJsonArray& trains, const QJsonArray& cars) {
    PutBack out;
    for (const auto& t : byIdOrder(trains)) {
        const QString id = text(t.value(QLatin1String("id")));
        if (!start.trains.contains(id)) {
            if (!t.value(QLatin1String("units")).toArray().isEmpty()) out.emptied << id;
            continue;
        }
        const QJsonObject was = start.trains.value(id).toObject();
        if (!same(was.value(QLatin1String("position")), t.value(QLatin1String("position"))) || !same(was.value(QLatin1String("units")), t.value(QLatin1String("units"))))
            out.trains.append(QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("position"), was.value(QLatin1String("position")) }, { QStringLiteral("units"), was.value(QLatin1String("units")) } });
    }
    for (const auto& c : byIdOrder(cars)) {
        const QString id = text(c.value(QLatin1String("id")));
        if (start.cars.contains(id) && start.cars.value(id).toString() != text(c.value(QLatin1String("opsCut")))) out.cars.insert(id, start.cars.value(id));
    }
    return out;
}

QJsonObject replayLog(const SessionStart& start, const std::vector<LogEntry>& log, const QString& session) {
    QJsonObject at;
    for (auto it = start.trains.begin(); it != start.trains.end(); ++it) at.insert(it.key(), it.value().toObject().value(QLatin1String("position")));
    std::vector<LogEntry> moves;
    for (const auto& e : log)
        if (e.session == session && e.kind == QLatin1String("move")) moves.push_back(e);
    std::stable_sort(moves.begin(), moves.end(), [](const LogEntry& a, const LogEntry& b) { return a.at != b.at ? a.at < b.at : a.id < b.id; });
    for (const auto& e : moves) at.insert(e.what, e.to);
    return at;
}

SessionSummary sessionSummary(const QString& session, const std::vector<LogEntry>& log, const std::vector<std::pair<Job, QString>>& jobs) {
    SessionSummary out;
    QSet<QString> cuts;
    for (const auto& e : log) {
        if (e.session != session) continue;
        if (e.kind == QLatin1String("move")) {
            ++out.moves;
            cuts.insert(e.what);
        } else if (e.kind == QLatin1String("switch")) {
            ++out.switches;
        }
    }
    out.cuts = static_cast<int>(cuts.size());
    for (const auto& [job, status] : jobs)
        if (status != QLatin1String("done")) out.jobsLeft << job.id;
    out.jobsLeft.sort();
    return out;
}

QStringList carsInMotion(const QJsonArray& trains, const std::function<bool(const QString&)>& onSpot) {
    QStringList out;
    for (const auto& v : trains) {
        const QJsonObject t = v.toObject();
        const QString id = text(t.value(QLatin1String("id")));
        if (!standsOn(t.value(QLatin1String("position"))).isEmpty() && !t.value(QLatin1String("units")).toArray().isEmpty() && !onSpot(id)) out << id;
    }
    out.sort();
    return out;
}

}  // namespace bld::track
