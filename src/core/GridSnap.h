#pragma once

// Grid snap, as BlueBrick does it (MapData/Layer.cs snapToGrid and
// updateSnapMargin, MapData/LayerBrick.cs getMovedSnapPoint). The web
// has the same functions in apps/web/src/editor/gridSnap.ts, and both
// apps run the cases in fixtures/grid-snap-vectors.json.
//
// A part snaps by its snap corner: the top-left of its display area plus
// the offset its <SnapMargin> gives at its orientation. A 9V straight is
// 17 studs wide with half a stud of margin each side, so its 16-stud
// rails sit on the grid, not its box.
//
// Points (ruler ends, label corners, text and venue points) snap to the
// nearest grid point. One grid for everything: a whole number of studs a
// step, anchored at the layout's origin (0, 0).

#include <QPointF>
#include <QString>
#include <QtMath>

#include <cmath>

namespace bld::gridsnap {

// A part's <SnapMargin>, in studs.
struct Margin {
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    bool isNull() const { return left == 0.0 && right == 0.0 && top == 0.0 && bottom == 0.0; }
    bool operator==(const Margin&) const = default;
};

// From the top-left of a part's display area to its snap corner, at
// `orientationDeg` (BlueBrick's updateSnapMargin, its branches included).
inline QPointF snapOffset(const Margin& m, double orientationDeg) {
    if (m.isNull()) return {};
    const double a = orientationDeg * M_PI / 180.0;
    double c = std::cos(a);
    double s = std::sin(a);
    double x = 0.0, y = 0.0;
    if (c > 0) {
        x = m.left * c;
        y = m.top * c;
    } else {
        c = -c;
        x = m.right * c;
        y = m.bottom * c;
    }
    if (s > 0) {
        x += m.bottom * s;
        y += m.left * s;
    } else {
        s = -s;
        x += m.top * s;
        y += m.right * s;
    }
    return { x, y };
}

// So a value that is a whole number of steps apart from rounding noise
// counts as one.
inline constexpr double kEpsilon = 1e-6;

// The grid line at or before `v`.
inline double floorToStep(double v, double step) {
    return std::floor(v / step + kEpsilon) * step;
}

// The nearest grid line; halfway goes to the even step (C#'s Math.Round,
// which BlueBrick's centred snap uses).
inline double roundToStep(double v, double step) {
    const double q = v / step;
    double r = std::floor(q + 0.5);
    if (std::abs(q + 0.5 - r) < kEpsilon && std::fmod(r, 2.0) != 0.0) r -= 1.0;
    return r * step;
}

// The nearest grid point to `p`; `p` itself when the step is 0 (off).
inline QPointF snapPoint(QPointF p, double step) {
    if (step <= 0.0) return p;
    return { roundToStep(p.x(), step), roundToStep(p.y(), step) };
}

// Where a moved part's snap corner lands: the nearest grid point, wherever
// the part was grabbed. Dragged, dropped from the list, pasted or put
// down as a module, it goes on the one stud grid anchored at the layout's
// origin (vanilla BlueBrick floors relative to the grab instead).
inline QPointF cornerOnGrid(QPointF rawCorner, double step) {
    return snapPoint(rawCorner, step);
}

// The shift that takes a part whose snap corner the move has at
// `rawCorner` onto the grid.
inline QPointF gridShift(QPointF rawCorner, double step) {
    return cornerOnGrid(rawCorner, step) - rawCorner;
}

// How a move snaps (a per-person choice, editing/snapMode): Grid puts the
// thing on the nearest point of the one stud grid; Steps moves it a whole
// number of steps from where it started, so a piece off the grid stays off
// it by the same amount.
enum class SnapMode { Grid, Steps };
inline constexpr SnapMode kDefaultSnapMode = SnapMode::Grid;

// As saved: "grid" or "steps"; anything else is the default.
inline SnapMode snapModeFromId(const QString& id) {
    return id == QLatin1String("steps") ? SnapMode::Steps : SnapMode::Grid;
}
inline QString snapModeId(SnapMode m) {
    return m == SnapMode::Steps ? QStringLiteral("steps") : QStringLiteral("grid");
}

// In Steps mode, the shift that rounds a move of `delta` to a whole number
// of steps.
inline QPointF stepShift(QPointF delta, double step) {
    if (step <= 0.0) return {};
    return { roundToStep(delta.x(), step) - delta.x(), roundToStep(delta.y(), step) - delta.y() };
}

// The shift for something whose snap corner started at `start` and that the
// move has at `rawCorner`: onto the nearest grid point (Grid), or to a whole
// number of steps from `start` (Steps).
inline QPointF moveShift(SnapMode mode, QPointF start, QPointF rawCorner, double step) {
    if (mode == SnapMode::Steps) return stepShift(rawCorner - start, step);
    return gridShift(rawCorner, step);
}

// The next grid line from `v` in direction `dir` (+1 or -1); the nearest
// when `dir` is 0.
inline double nudgeToStep(double v, int dir, double step) {
    if (dir > 0) return std::floor(v / step + kEpsilon) * step + step;
    if (dir < 0) return std::ceil(v / step - kEpsilon) * step - step;
    return roundToStep(v, step);
}

// Where an arrow key takes a snap corner at `corner`: to the next grid
// line in direction (dx, dy) (each -1, 0 or +1), the other axis on its
// nearest grid line. In Steps mode, one step that way; with snap off, one
// stud.
inline QPointF nudgeCorner(QPointF corner, int dx, int dy, double step, SnapMode mode = SnapMode::Grid) {
    if (step <= 0.0) return corner + QPointF(dx, dy);
    if (mode == SnapMode::Steps) return corner + QPointF(dx * step, dy * step);
    return { nudgeToStep(corner.x(), dx, step), nudgeToStep(corner.y(), dy, step) };
}

// The steps the snap can take, in studs: whole studs only. A saved step
// that is not one of them (vanilla's 0.5) is taken as the nearest one.
inline constexpr double kSnapSteps[] = { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
inline constexpr double kDefaultSnapStep = 1.0;

inline double validSnapStep(double step) {
    if (!(step > 0.0)) return 0.0;
    double best = kSnapSteps[0];
    for (double s : kSnapSteps)
        if (std::abs(s - step) < std::abs(best - step)) best = s;
    return best;
}

}  // namespace bld::gridsnap
