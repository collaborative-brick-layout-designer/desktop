#include "StockGuess.h"

#include <QJsonArray>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bld::track {

namespace {

// The contract's `parts`, word for word (StockGuessTest checks it).
const char* const kAbout =
    "Part ids by what they are on a car. Wheels and bogies under it, couplers at its ends.";
constexpr double kClusterGap = 6;
constexpr double kMinCouplerSpan = 4;
const std::vector<const char*> kBogie = { "4092a", "4092b", "18626", "2686", "2686c01", "2686c02" };
const std::vector<const char*> kMotor = { "2894c01",   "87574c01", "88002*",  "2683c01",   "2684c01",
                                          "579c01",    "579c02",   "579c03",  "579p01c01", "579p01c02",
                                          "579p01c03", "501ac01",  "501bc01", "501cc01" };
const std::vector<const char*> kWheel = { "2878",    "2878c01", "2878c02", "38339",  "38339c01", "38339c02",
                                          "38340",   "57878",   "55423",   "85489a", "85489b",   "85489ac01",
                                          "4180c01", "4180c02", "243c01",  "244c01", "244c02",   "244c03",
                                          "245c01",  "245c02",  "447c01",  "867",    "50254",    "2927" };
const std::vector<const char*> kCoupler = { "2920",     "4022",     "4022c01",  "4022c02",   "4023",
                                            "4023c01",  "4023c02",  "29084",    "64414",     "64422",
                                            "64417c01", "2609b",    "2609bc01", "2609c",     "2609cc01",
                                            "2609d",    "2609dc01", "30159b",   "30159bc01", "753c01",
                                            "753c02",   "2607bc01", "499c01",   "735",       "290" };

enum Class { Bogie, Motor, Wheel, Coupler, None };

const std::array<const std::vector<const char*>*, 4>& classes() {
    static const std::array<const std::vector<const char*>*, 4> all{ &kBogie, &kMotor, &kWheel, &kCoupler };
    return all;
}

Class classOf(const QString& id) {
    for (int c = 0; c < 4; ++c) {
        for (const char* e : *classes()[static_cast<size_t>(c)]) {
            const QLatin1String entry(e);
            const bool prefix = entry.endsWith(QLatin1Char('*'));
            if (prefix ? id.startsWith(entry.chopped(1)) : id == entry) return static_cast<Class>(c);
        }
    }
    return None;
}

// The middles of runs of values no more than `gap` apart, lowest first.
std::vector<double> clusters(std::vector<double> values, double gap) {
    std::sort(values.begin(), values.end());
    std::vector<std::vector<double>> runs;
    for (double v : values) {
        if (!runs.empty() && v - runs.back().back() <= gap) runs.back().push_back(v);
        else runs.push_back({ v });
    }
    std::vector<double> out;
    for (const auto& r : runs) {
        double sum = 0.0;
        for (double v : r) sum += v; // in order, as the web adds them
        out.push_back(sum / static_cast<double>(r.size()));
    }
    return out;
}

double r2(double v) {
    const double r = std::floor(v * 100.0 + 0.5) / 100.0;
    return r == 0.0 ? 0.0 : r;
}

QJsonArray list(const std::vector<const char*>& l) {
    QJsonArray a;
    for (const char* e : l) a.append(QString::fromLatin1(e));
    return a;
}

} // namespace

QJsonObject stockPartsTable() {
    return { { QStringLiteral("_about"), QString::fromUtf8(kAbout) },
             { QStringLiteral("clusterGap"), kClusterGap },
             { QStringLiteral("minCouplerSpan"), kMinCouplerSpan },
             { QStringLiteral("bogie"), list(kBogie) },
             { QStringLiteral("motor"), list(kMotor) },
             { QStringLiteral("wheel"), list(kWheel) },
             { QStringLiteral("coupler"), list(kCoupler) } };
}

QString stockPartId(const QString& file) {
    QString base = file.toLower();
    base.replace(QLatin1Char('\\'), QLatin1Char('/'));
    base = base.mid(base.lastIndexOf(QLatin1Char('/')) + 1);
    if (base.endsWith(QLatin1String(".dat"))) base.chop(4);
    return base;
}

StockGuess guessStock(const std::vector<GuessRef>& refs, const QRectF& bounds, QPointF centre) {
    const bool alongX = bounds.width() >= bounds.height();
    // A clockwise quarter turn takes (x, y) to (-y, x) about the middle.
    const auto along = [&](QPointF p) { return alongX ? p.x() - centre.x() : -(p.y() - centre.y()); };
    const double lo = alongX ? bounds.x() - centre.x() : -(bounds.y() + bounds.height() - centre.y());
    const double hi = alongX ? bounds.x() + bounds.width() - centre.x() : -(bounds.y() - centre.y());
    std::array<std::vector<double>, 4> at;
    for (const GuessRef& r : refs) {
        const Class c = classOf(stockPartId(r.id));
        if (c != None) at[c].push_back(along(r.at));
    }
    std::vector<double> pivotsIn = at[Bogie];
    pivotsIn.insert(pivotsIn.end(), at[Motor].begin(), at[Motor].end());
    const auto pivots = clusters(pivotsIn, kClusterGap);
    const auto wheels = clusters(at[Wheel], kClusterGap);
    StockGuess g;
    g.quarterTurns = alongX ? 0 : 1;
    g.width = r2(alongX ? bounds.height() : bounds.width());
    double front = 0.0, rear = 0.0;
    if (pivots.size() >= 2) {
        g.trucksFrom = QStringLiteral("bogies");
        front = pivots.back();
        rear = pivots.front();
    } else if (wheels.size() >= 2) {
        g.trucksFrom = QStringLiteral("wheels");
        front = wheels.back();
        rear = wheels.front();
    }
    const auto& c = at[Coupler];
    const double span =
        c.size() >= 2 ? *std::max_element(c.begin(), c.end()) - *std::min_element(c.begin(), c.end()) : 0.0;
    const bool found = c.size() >= 2 && span >= kMinCouplerSpan;
    const double body = found ? std::max(1.0, span - 2.0) : hi - lo;
    const double length = found ? span : body + 2.0;
    if (g.trucksFrom == QLatin1String("none")) {
        front = length / 4.0;
        rear = -length / 4.0;
    }
    g.length = r2(length);
    g.bodyLength = r2(body);
    g.front = r2(front);
    g.rear = r2(rear);
    g.couplersFrom = found ? QStringLiteral("found") : QStringLiteral("none");
    return g;
}

} // namespace bld::track
