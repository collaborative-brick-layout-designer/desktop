#include "VenueDraw.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::rendering::venuedraw {

ObstacleStyle obstacleStyle(core::ObstacleKind kind) {
    switch (kind) {
    case core::ObstacleKind::Column: return { QColor(60, 60, 60, 191), QColor(40, 40, 40), 1.0 };
    case core::ObstacleKind::Stairs: return { QColor(214, 180, 196, 140), QColor(120, 80, 100), 1.0 };
    case core::ObstacleKind::Elevator: return { QColor(150, 150, 170, 115), QColor(70, 70, 90), 1.0 };
    case core::ObstacleKind::Counter: return { QColor(170, 125, 70, 115), QColor(110, 80, 40), 1.0 };
    case core::ObstacleKind::Railing: return { std::nullopt, QColor(40, 40, 40), 3.0 };
    case core::ObstacleKind::Other: break;
    }
    return { QColor(120, 120, 120, 102), QColor(90, 90, 90), 1.0 };
}

StairMarks stairMarks(const QVector<QPointF>& poly, std::optional<double> upDegrees) {
    StairMarks out;
    if (!upDegrees || poly.size() < 3) return out;
    const double a = *upDegrees * M_PI / 180.0;
    const QPointF u(std::cos(a), std::sin(a)); // up the stairs
    const QPointF w(-u.y(), u.x());            // across them
    double u0 = std::numeric_limits<double>::max(), u1 = -u0, w0 = u0, w1 = -u0;
    for (const auto& p : poly) {
        const double pu = p.x() * u.x() + p.y() * u.y(), pw = p.x() * w.x() + p.y() * w.y();
        u0 = std::min(u0, pu), u1 = std::max(u1, pu), w0 = std::min(w0, pw), w1 = std::max(w1, pw);
    }
    const auto at = [&](double uu, double ww) { return u * uu + w * ww; };
    for (int i = 1; u0 + i * kTreadSpacingStuds < u1 - 0.001; ++i) {
        const double uu = u0 + i * kTreadSpacingStuds;
        out.treads.append(QLineF(at(uu, w0), at(uu, w1)));
    }
    const double len = u1 - u0, mid = (w0 + w1) / 2;
    const QPointF tail = at(u0 + len * 0.15, mid), tip = at(u1 - len * 0.15, mid);
    const double head = std::min(len * 0.2, (w1 - w0) * 0.3);
    out.arrow = { QLineF(tail, tip), QLineF(tip, at(u1 - len * 0.15 - head, mid - head * 0.6)),
                  QLineF(tip, at(u1 - len * 0.15 - head, mid + head * 0.6)) };
    return out;
}

QVector<QLineF> elevatorCross(const QVector<QPointF>& poly) {
    if (poly.size() < 3) return {};
    double x0 = std::numeric_limits<double>::max(), x1 = -x0, y0 = x0, y1 = -x0;
    for (const auto& p : poly)
        x0 = std::min(x0, p.x()), x1 = std::max(x1, p.x()), y0 = std::min(y0, p.y()),
        y1 = std::max(y1, p.y());
    return { QLineF(x0, y0, x1, y1), QLineF(x1, y0, x0, y1) };
}

QString powerText(const core::VenuePower& p) {
    QStringList parts;
    if (!p.label.isEmpty()) parts << p.label;
    if (p.amps > 0) parts << QStringLiteral("%1 A").arg(p.amps);
    if (p.volts > 0) parts << QStringLiteral("%1 V").arg(p.volts);
    return parts.join(QStringLiteral(" · "));
}

std::optional<DimensionGeometry> dimensionGeometry(QPointF from, QPointF to) {
    const QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < 0.001) return std::nullopt;
    const QPointF n(-d.y() / len, d.x() / len);
    const auto tick = [&](QPointF p) {
        return QLineF(p - n * kDimensionTickStuds, p + n * kDimensionTickStuds);
    };
    double angle = std::atan2(d.y(), d.x()) * 180.0 / M_PI;
    if (angle > 90.0) angle -= 180.0;
    else if (angle <= -90.0) angle += 180.0;
    DimensionGeometry g{
        QLineF(from, to), { tick(from), tick(to) }, (from + to) / 2 - n * kDimensionLabelOffsetStuds, angle
    };
    return g;
}

QString estimatedText(const QString& s) {
    return s + QStringLiteral(" (est.)");
}

QColor onDarkCanvas(const QColor& color) {
    const double r = color.red() / 255.0, g = color.green() / 255.0, b = color.blue() / 255.0;
    const double mx = std::max({ r, g, b }), mn = std::min({ r, g, b });
    const double l = (mx + mn) / 2;
    if (l >= 0.5) return color;
    const double d = mx - mn;
    const double s = d == 0 ? 0 : d / (1 - std::abs(2 * l - 1));
    double h = 0;
    if (d != 0) {
        if (mx == r) h = std::fmod((g - b) / d, 6.0);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
    }
    const double L = 1 - l;
    const double c = (1 - std::abs(2 * L - 1)) * s;
    const double x = c * (1 - std::abs(std::fmod(h, 2.0) - 1));
    const double k = L - c / 2;
    const int hi = static_cast<int>(std::floor(std::fmod(std::fmod(h, 6.0) + 6.0, 6.0)));
    const double rgb[6][3] = { { c, x, 0 }, { x, c, 0 }, { 0, c, x }, { 0, x, c }, { x, 0, c }, { c, 0, x } };
    // Rounded as JavaScript's Math.round (half up), as the web does.
    const auto to = [k](double v) { return static_cast<int>(std::floor((v + k) * 255 + 0.5)); };
    QColor out(to(rgb[hi][0]), to(rgb[hi][1]), to(rgb[hi][2]));
    out.setAlpha(color.alpha());
    return out;
}

} // namespace bld::rendering::venuedraw
