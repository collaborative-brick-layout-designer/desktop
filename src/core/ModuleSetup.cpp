#include "ModuleSetup.h"

#include "Module.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <map>

namespace bld::core {

const QStringList& tableTypes() {
    static const QStringList t{ QStringLiteral("mils"), QStringLiteral("baseplate"), QStringLiteral("custom") };
    return t;
}

const QStringList& plugSides() {
    static const QStringList s{ QStringLiteral("front"), QStringLiteral("back"), QStringLiteral("left"),
                                QStringLiteral("right") };
    return s;
}

namespace {

bool inRange(const QJsonValue& v, double lo, double hi) {
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    return std::isfinite(d) && d >= lo && d <= hi;
}

bool isWhole(const QJsonValue& v) {
    const double d = v.toDouble();
    return std::floor(d) == d;
}

double tenth(double v) { return std::round(v * 10.0) / 10.0; }

bool onlyKeys(const QJsonObject& o, const QStringList& keys) {
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (!keys.contains(it.key())) return false;
    return true;
}

}  // namespace

std::optional<ModuleTable> readTable(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    const QJsonObject o = v.toObject();
    const QString type = o.value(QStringLiteral("type")).toString();
    if (!tableTypes().contains(type)) return std::nullopt;
    ModuleTable t;
    t.type = type;
    const QJsonValue h = o.value(QStringLiteral("heightMm"));
    if (inRange(h, 0.0, kMaxHeightMm) && h.toDouble() > 0.0) t.heightMm = h.toDouble();
    t.toRail = o.value(QStringLiteral("measuredTo")).toString() == QLatin1String("rail");
    return t;
}

std::optional<ModulePower> readPower(const QJsonValue& v) {
    if (!v.isObject()) return std::nullopt;
    const QJsonObject o = v.toObject();
    const QJsonValue needed = o.value(QStringLiteral("needed"));
    if (!needed.isBool()) return std::nullopt;
    ModulePower p;
    p.needed = needed.toBool();
    const QJsonValue outlets = o.value(QStringLiteral("outlets"));
    if (inRange(outlets, 0, kMaxOutlets) && isWhole(outlets)) p.outlets = outlets.toInt();
    const QJsonValue watts = o.value(QStringLiteral("watts"));
    if (inRange(watts, 0, kMaxWatts)) p.watts = static_cast<int>(std::round(watts.toDouble()));
    const QString side = o.value(QStringLiteral("plugSide")).toString();
    if (plugSides().contains(side)) p.plugSide = side;
    return p;
}

QJsonObject tableJson(const ModuleTable& t) {
    QJsonObject o{ { QStringLiteral("type"), t.type } };
    if (t.heightMm) o[QStringLiteral("heightMm")] = tenth(*t.heightMm);
    if (t.toRail) o[QStringLiteral("measuredTo")] = QStringLiteral("rail");
    return o;
}

QJsonObject powerJson(const ModulePower& p) {
    QJsonObject o{ { QStringLiteral("needed"), p.needed } };
    if (!p.needed) return o;
    if (p.outlets) o[QStringLiteral("outlets")] = *p.outlets;
    if (p.watts) o[QStringLiteral("watts")] = *p.watts;
    if (!p.plugSide.isEmpty()) o[QStringLiteral("plugSide")] = p.plugSide;
    return o;
}

QJsonObject setupJson(const ModuleSetup& s) {
    QJsonObject o;
    if (s.table) o[QStringLiteral("table")] = tableJson(*s.table);
    if (s.power) o[QStringLiteral("power")] = powerJson(*s.power);
    return o;
}

ModuleSetup setupFromJson(const QJsonObject& o) {
    ModuleSetup s;
    s.table = readTable(o.value(QStringLiteral("table")));
    s.power = readPower(o.value(QStringLiteral("power")));
    // As stored: a height to the tenth of a mm, no power details when none is needed.
    if (s.table && s.table->heightMm) s.table->heightMm = tenth(*s.table->heightMm);
    if (s.power && !s.power->needed) s.power = ModulePower{};
    return s;
}

ModuleSetup setupOf(const Module& m) {
    QJsonObject o;
    if (m.table) o[QStringLiteral("table")] = *m.table;
    if (m.power) o[QStringLiteral("power")] = *m.power;
    return setupFromJson(o);
}

bool validSetup(const QJsonValue& v) {
    if (v.isNull()) return true;
    if (!v.isObject()) return false;
    const QJsonObject o = v.toObject();
    if (!onlyKeys(o, { QStringLiteral("table"), QStringLiteral("power") })) return false;
    const QJsonValue t = o.value(QStringLiteral("table"));
    if (!t.isUndefined() && !t.isNull()) {
        if (!t.isObject()) return false;
        const QJsonObject to = t.toObject();
        if (!onlyKeys(to, { QStringLiteral("type"), QStringLiteral("heightMm"), QStringLiteral("measuredTo") })) return false;
        if (!tableTypes().contains(to.value(QStringLiteral("type")).toString()) || !to.value(QStringLiteral("type")).isString())
            return false;
        const QJsonValue h = to.value(QStringLiteral("heightMm"));
        if (!h.isUndefined() && !inRange(h, 0.0, kMaxHeightMm)) return false;
        const QJsonValue m = to.value(QStringLiteral("measuredTo"));
        if (!m.isUndefined() && m.toString() != QLatin1String("rail")) return false;
    }
    const QJsonValue p = o.value(QStringLiteral("power"));
    if (!p.isUndefined() && !p.isNull()) {
        if (!p.isObject()) return false;
        const QJsonObject po = p.toObject();
        if (!onlyKeys(po, { QStringLiteral("needed"), QStringLiteral("outlets"), QStringLiteral("watts"),
                            QStringLiteral("plugSide") }))
            return false;
        if (!po.value(QStringLiteral("needed")).isBool()) return false;
        const QJsonValue n = po.value(QStringLiteral("outlets"));
        if (!n.isUndefined() && (!inRange(n, 0, kMaxOutlets) || !isWhole(n))) return false;
        const QJsonValue w = po.value(QStringLiteral("watts"));
        if (!w.isUndefined() && !inRange(w, 0, kMaxWatts)) return false;
        const QJsonValue side = po.value(QStringLiteral("plugSide"));
        if (!side.isUndefined() && (!side.isString() || !plugSides().contains(side.toString()))) return false;
    }
    return true;
}

Module withSetup(Module m, const ModuleSetup& s) {
    if (s.table) m.table = tableJson(*s.table);
    else m.table.reset();
    if (s.power) m.power = powerJson(*s.power);
    else m.power.reset();
    return m;
}

Module withLibrarySetup(Module m, const ModuleSetup& library) {
    if (library.empty()) return m;
    return withSetup(std::move(m), library);
}

int outletsOf(const ModulePower& p) { return p.needed ? p.outlets.value_or(1) : 0; }

SetupSummary setupSummary(const std::vector<ModuleSetup>& modules) {
    SetupSummary s;
    s.modules = static_cast<int>(modules.size());
    std::map<QString, TableGroup> groups;
    for (const auto& m : modules) {
        if (!m.power) ++s.powerNotSet;
        else if (!m.power->needed) ++s.noPower;
        else {
            ++s.powerModules;
            s.outlets += outletsOf(*m.power);
            s.watts += m.power->watts.value_or(0);
        }
        if (!m.table) {
            ++s.tablesNotSet;
            continue;
        }
        TableGroup& g = groups[m.table->type];
        g.type = m.table->type;
        ++g.count;
        if (!m.table->heightMm) continue;
        const double h = tenth(*m.table->heightMm);
        auto found = std::find_if(g.heights.begin(), g.heights.end(),
                                  [&](const HeightCount& x) { return x.heightMm == h && x.toRail == m.table->toRail; });
        if (found != g.heights.end()) ++found->count;
        else g.heights.push_back({ h, m.table->toRail, 1 });
    }
    for (const QString& type : tableTypes()) {
        auto it = groups.find(type);
        if (it == groups.end()) continue;
        TableGroup g = it->second;
        std::stable_sort(g.heights.begin(), g.heights.end(), [](const HeightCount& a, const HeightCount& b) {
            if (a.heightMm != b.heightMm) return a.heightMm < b.heightMm;
            return !a.toRail && b.toRail;
        });
        s.tables.push_back(std::move(g));
    }
    return s;
}

SetupSummary setupSummary(const std::vector<Module>& modules) {
    std::vector<ModuleSetup> setups;
    setups.reserve(modules.size());
    for (const auto& m : modules) setups.push_back(setupOf(m));
    return setupSummary(setups);
}

}  // namespace bld::core
