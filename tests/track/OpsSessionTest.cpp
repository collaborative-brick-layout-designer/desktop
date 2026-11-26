// Who may move and flip what in an operating session, putting cars back,
// and the log played again, against the shared contract (fixtures/
// contracts/ops-session.json): the web's packages/parts-catalog/src/track/
// opsSession.test.ts checks the same, so both apps grey out the same things
// the server refuses (the web's apps/server/src/ws/opsGuard.ts).

#include "track/OpsSession.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonDocument>

using namespace bld::track;

namespace {

QJsonObject contract() {
    static const QJsonObject d = [] {
        QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts/ops-session.json"));
        EXPECT_TRUE(f.open(QIODevice::ReadOnly));
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return d;
}

QJsonArray sorted(QSet<QString> s) {
    QStringList l(s.begin(), s.end());
    l.sort();
    QJsonArray out;
    for (const auto& x : l) out.append(x);
    return out;
}
QJsonArray list(const QStringList& l) {
    QJsonArray out;
    for (const auto& x : l) out.append(x);
    return out;
}
std::string text(const QJsonValue& v) { return QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact).toStdString(); }

std::vector<std::pair<Job, QString>> jobs() {
    std::vector<std::pair<Job, QString>> out;
    for (const auto& v : contract().value(QLatin1String("jobs")).toArray()) out.emplace_back(asJob(v.toObject()), jobStatus(v.toObject()));
    return out;
}
std::vector<ReachPlace> places() {
    std::vector<ReachPlace> out;
    for (const auto& v : contract().value(QLatin1String("places")).toArray()) {
        ReachPlace p{ v.toObject().value(QLatin1String("id")).toString(), {} };
        for (const auto& b : v.toObject().value(QLatin1String("bricks")).toArray()) p.bricks << b.toString();
        out.push_back(p);
    }
    return out;
}

}  // namespace

TEST(OpsSession, LimitsAreTheContracts) {
    const QJsonObject l = contract().value(QLatin1String("limits")).toObject();
    EXPECT_EQ(l.value(QLatin1String("sessions")).toInt(), kMaxSessions);
    EXPECT_EQ(l.value(QLatin1String("crew")).toInt(), kMaxCrew);
    EXPECT_EQ(l.value(QLatin1String("log")).toInt(), kMaxLogEntries);
    EXPECT_EQ(l.value(QLatin1String("logSize")).toInt(), kMaxLogEntrySize);
    EXPECT_EQ(l.value(QLatin1String("size")).toInt(), kMaxSessionSize);
    EXPECT_EQ(l.value(QLatin1String("clockFast")).toInt(), kMaxClockFast);
}

TEST(OpsSession, EachRoleAgainstEachKey) {
    const QJsonObject d = contract();
    const QStringList switches = [&] {
        QStringList s;
        for (const auto& v : d.value(QLatin1String("switches")).toArray()) s << v.toString();
        return s;
    }();
    const QJsonObject positions = d.value(QLatin1String("positions")).toObject();
    QStringList trainIds;
    for (const auto& v : d.value(QLatin1String("trains")).toArray()) trainIds << v.toString();
    for (const auto& cv : d.value(QLatin1String("reach")).toArray()) {
        const QJsonObject c = cv.toObject();
        SCOPED_TRACE(c.value(QLatin1String("name")).toString().toStdString());
        std::vector<OpsSession> sessions;
        for (const auto& s : c.value(QLatin1String("sessions")).toArray()) sessions.push_back(asSession(s.toObject()));
        QJsonObject people;
        for (const auto& uv : c.value(QLatin1String("users")).toArray()) {
            const QString user = uv.toString();
            const OpsReach r = opsReach(sessions, jobs(), places(), user, c.value(QLatin1String("layoutId")).toString(), user == c.value(QLatin1String("owner")).toString());
            QJsonArray flips, moves;
            for (const auto& k : switches)
                if (mayFlip(r, k)) flips.append(k);
            // (In the contract's order: the positions as written.)
            for (const auto& id : trainIds)
                for (const auto& where : positions.keys())
                    if (mayMove(r, id, positions.value(where))) moves.append(id + QLatin1Char(' ') + where);
            people.insert(user.isEmpty() ? QStringLiteral("(a guest)") : user,
                          QJsonObject{ { QStringLiteral("all"), r.all }, { QStringLiteral("role"), r.role }, { QStringLiteral("bricks"), sorted(r.bricks) }, { QStringLiteral("trains"), sorted(r.trains) },
                                       { QStringLiteral("why"), r.why }, { QStringLiteral("flips"), flips }, { QStringLiteral("moves"), moves } });
        }
        const QJsonObject want = c.value(QLatin1String("expect")).toObject();
        EXPECT_EQ(want.value(QLatin1String("locked")).toBool(), sheetsLocked(sessions));
        const QJsonObject wantPeople = want.value(QLatin1String("people")).toObject();
        for (auto it = wantPeople.begin(); it != wantPeople.end(); ++it) {
            QJsonObject w = it.value().toObject();
            QJsonObject got = people.value(it.key()).toObject();
            // The moves as sets: the web lists the positions in its own key order.
            const auto asSet = [](const QJsonArray& a) {
                QStringList l;
                for (const auto& x : a) l << x.toString();
                l.sort();
                return l;
            };
            EXPECT_EQ(asSet(w.take(QLatin1String("moves")).toArray()), asSet(got.take(QLatin1String("moves")).toArray())) << it.key().toStdString();
            EXPECT_EQ(text(w), text(got)) << it.key().toStdString();
        }
        EXPECT_EQ(wantPeople.size(), people.size());
    }
}

TEST(OpsSession, EndingAndTheLog) {
    const QJsonObject d = contract();
    const QJsonObject start = d.value(QLatin1String("start")).toObject();
    const QJsonObject now = d.value(QLatin1String("now")).toObject();
    const SessionStart s = sessionStart(start.value(QLatin1String("trains")).toArray(), start.value(QLatin1String("cars")).toArray());
    std::vector<LogEntry> log;
    for (const auto& v : d.value(QLatin1String("log")).toArray()) log.push_back(asLogEntry(v.toObject()));
    const QJsonObject want = d.value(QLatin1String("expect")).toObject();

    QJsonArray bricks;
    for (const auto& k : d.value(QLatin1String("switches")).toArray()) bricks.append(switchBrick(k.toString()));
    EXPECT_EQ(text(want.value(QLatin1String("switchBrick"))), text(bricks));
    EXPECT_EQ(text(want.value(QLatin1String("start"))), text(QJsonObject{ { QStringLiteral("trains"), s.trains }, { QStringLiteral("cars"), s.cars } }));
    const PutBack back = putBack(s, now.value(QLatin1String("trains")).toArray(), now.value(QLatin1String("cars")).toArray());
    EXPECT_EQ(text(want.value(QLatin1String("putBack"))), text(QJsonObject{ { QStringLiteral("trains"), back.trains }, { QStringLiteral("cars"), back.cars }, { QStringLiteral("emptied"), list(back.emptied) } }));
    EXPECT_EQ(text(want.value(QLatin1String("replay"))), text(replayLog(s, log, QStringLiteral("s1"))));
    const SessionSummary sum = sessionSummary(QStringLiteral("s1"), log, jobs());
    EXPECT_EQ(text(want.value(QLatin1String("summary"))), text(QJsonObject{ { QStringLiteral("moves"), sum.moves }, { QStringLiteral("cuts"), sum.cuts }, { QStringLiteral("switches"), sum.switches }, { QStringLiteral("jobsLeft"), list(sum.jobsLeft) } }));
    EXPECT_EQ(text(want.value(QLatin1String("inMotion"))), text(list(carsInMotion(now.value(QLatin1String("trains")).toArray(), [](const QString& id) { return id == QLatin1String("cut-1"); }))));
}
