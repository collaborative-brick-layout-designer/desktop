#include "PartTrack.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace bld::track {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kStraightTurn = 2.0;   // degrees: less than this is no turn
constexpr double kStraightSide = 0.15;  // sin of the chord's angle: less is no side step (a 4DBrix diverger's straight leg steps 0.09)
constexpr double kSameTurn = 0.5;       // degrees: turns closer than this tie
constexpr double kSameSide = 0.05;

QJsonObject readResource(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// Sideways step from `from` to `to` against the way in: sin of the chord's
// angle, + to the right.
double sideStep(const QVector<TrackConn>& conns, int from, int to) {
    const TrackConn& a = conns[from];
    const TrackConn& b = conns[to];
    const double r = (a.angle + 180.0) * kPi / 180.0;
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    if (len <= 1e-9) return 0.0;
    return (std::cos(r) * dy - std::sin(r) * dx) / len;
}

int sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

}  // namespace

TrackKindsTable trackKindsFromJson(const QJsonObject& o) {
    TrackKindsTable t;
    const QJsonObject types = o.value(QLatin1String("types")).toObject();
    for (auto it = types.begin(); it != types.end(); ++it) t.types.insert(it.key(), it.value().toString());
    const QJsonObject prefixes = o.value(QLatin1String("prefixes")).toObject();
    // (QJsonObject keeps keys sorted; the web reads them in file order. No
    // prefix in the table starts another, so the order doesn't matter.)
    for (auto it = prefixes.begin(); it != prefixes.end(); ++it) t.prefixes.append({ it.key(), it.value().toString() });
    for (const auto& v : o.value(QLatin1String("notTrack")).toArray()) t.notTrack << v.toString();
    t.fallback = o.value(QLatin1String("fallback")).toString(QStringLiteral("rail"));
    t.maxTurnDegrees = o.value(QLatin1String("maxTurnDegrees")).toDouble(60.0);
    t.kinds = o.value(QLatin1String("kinds")).toObject().keys();
    return t;
}

TrackPathsTable trackPathsFromJson(const QJsonObject& o) {
    TrackPathsTable t;
    const QJsonObject parts = o.value(QLatin1String("parts")).toObject();
    for (auto it = parts.begin(); it != parts.end(); ++it) {
        QVector<std::pair<int, int>> paths;
        for (const auto& p : it.value().toObject().value(QLatin1String("paths")).toArray()) {
            const QJsonArray pair = p.toArray();
            if (pair.size() == 2) paths.append({ pair[0].toInt(), pair[1].toInt() });
        }
        t.parts.insert(it.key().toLower(), paths);
    }
    return t;
}

const TrackKindsTable& trackKinds() {
    static const TrackKindsTable t = trackKindsFromJson(readResource(QStringLiteral(":/bld/track/track-kinds.json")));
    return t;
}

const TrackPathsTable& trackPaths() {
    static const TrackPathsTable t = trackPathsFromJson(readResource(QStringLiteral(":/bld/track/track-paths.json")));
    return t;
}

double wrapDegrees(double a) {
    double x = std::fmod(std::fmod(a, 360.0) + 360.0, 360.0);
    if (x > 180.0) x -= 360.0;
    return x;
}

std::optional<QString> trackKindOf(const QString& type, bool inCircuit, const TrackKindsTable& table) {
    if (table.notTrack.contains(type)) return std::nullopt;
    if (const auto it = table.types.constFind(type); it != table.types.constEnd()) return *it;
    for (const auto& [prefix, kind] : table.prefixes)
        if (type.startsWith(prefix)) return kind;
    if (inCircuit) return table.fallback;
    return std::nullopt;
}

QVector<std::pair<int, int>> electricCircuits(const QVector<TrackConn>& conns) {
    QVector<std::pair<int, int>> out;
    for (int i = 0; i < conns.size(); ++i) {
        const int p = conns[i].plug;
        if (p == 0) continue;
        for (int j = i + 1; j < conns.size(); ++j)
            if (conns[j].plug == -p) out.append({ i, j });
    }
    return out;
}

double turnThrough(const QVector<TrackConn>& conns, int from, int to) {
    return wrapDegrees(conns[to].angle - (conns[from].angle + 180.0));
}

PartTrack partTrack(const QString& key, const QVector<TrackConn>& conns, const TrackKindsTable& kindsTable,
                    const TrackPathsTable& pathsTable) {
    PartTrack out;
    const auto circuits = electricCircuits(conns);
    QSet<int> inCircuit;
    for (const auto& [i, j] : circuits) inCircuit << i << j;
    for (int i = 0; i < conns.size(); ++i) out.kinds.append(trackKindOf(conns[i].type, inCircuit.contains(i), kindsTable));
    const auto sameKind = [&](int i, int j) { return out.kinds[i].has_value() && out.kinds[i] == out.kinds[j]; };

    const int n = static_cast<int>(conns.size());
    if (const auto it = pathsTable.parts.constFind(key.toLower()); it != pathsTable.parts.constEnd()) {
        for (auto [i, j] : *it) {
            if (i < 0 || j < 0 || i >= n || j >= n || i == j) continue;
            out.paths.append({ std::min(i, j), std::max(i, j) });
        }
    } else {
        for (const auto& [i, j] : circuits)
            if (sameKind(i, j)) out.paths.append({ i, j });
        if (out.paths.isEmpty()) {
            QVector<int> track;
            for (int i = 0; i < n; ++i)
                if (out.kinds[i]) track << i;
            if (track.size() == 2) {
                if (sameKind(track[0], track[1])) out.paths.append({ track[0], track[1] });
            } else if (track.size() > 2) {
                for (int a = 0; a < track.size(); ++a)
                    for (int b = a + 1; b < track.size(); ++b) {
                        const int i = track[a], j = track[b];
                        if (sameKind(i, j) && std::abs(turnThrough(conns, i, j)) <= kindsTable.maxTurnDegrees + 1e-9)
                            out.paths.append({ i, j });
                    }
            }
        }
    }
    std::sort(out.paths.begin(), out.paths.end());

    for (int heel = 0; heel < n; ++heel) {
        QVector<int> exits;
        for (const auto& [i, j] : out.paths) {
            if (i == heel) exits << j;
            else if (j == heel) exits << i;
        }
        if (exits.size() < 2) continue;
        std::sort(exits.begin(), exits.end());
        struct Leg {
            int exit;
            double turn, side;
            int dir;
        };
        QVector<Leg> legs;
        for (int exit : exits) {
            const double turn = turnThrough(conns, heel, exit);
            const double side = sideStep(conns, heel, exit);
            const int dir = std::abs(turn) >= kStraightTurn ? sign(turn) : std::abs(side) >= kStraightSide ? sign(side) : 0;
            legs.append({ exit, turn, side, dir });
        }
        std::stable_sort(legs.begin(), legs.end(), [](const Leg& p, const Leg& q) {
            const double t = std::abs(p.turn) - std::abs(q.turn);
            if (std::abs(t) > kSameTurn) return t < 0;
            const double s = std::abs(p.side) - std::abs(q.side);
            if (std::abs(s) > kSameSide) return s < 0;
            return p.exit < q.exit;
        });
        PartSwitch sw;
        sw.heel = heel;
        sw.exits = exits;
        sw.defaultExit = legs.front().exit;
        QHash<QString, int> used;
        for (const Leg& leg : legs) {
            const QString base = leg.dir == 0 ? QStringLiteral("straight") : leg.dir < 0 ? QStringLiteral("left") : QStringLiteral("right");
            const int count = ++used[base];
            sw.legs.insert(leg.exit, count == 1 ? base : QStringLiteral("%1 %2").arg(base).arg(count));
        }
        out.switches.append(sw);
    }
    return out;
}

}  // namespace bld::track
