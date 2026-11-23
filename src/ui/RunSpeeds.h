#pragma once

// Train speeds in the account's units (trains design 5): scale speeds in
// mph for feet and inches, km/h otherwise; and how fast the model really
// runs on the table. The web's editor/trainRun.ts (formatScaleSpeed,
// formatRealSpeed) says the same.

#include "../edit/venue/VenueUnits.h"
#include "../track/Animation.h"

#include <QCoreApplication>
#include <QString>

#include <cmath>

namespace bld::ui::RunSpeeds {

constexpr double kKmhPerMph = 1.609344;

inline bool usesMiles(edit::venue::LengthUnit unit = edit::venue::accountLengthUnit()) {
    return unit == edit::venue::LengthUnit::FeetInches;
}

// Scale km/h as the whole number shown (mph for feet and inches).
inline int kmhToShown(double kmh, edit::venue::LengthUnit unit = edit::venue::accountLengthUnit()) {
    return static_cast<int>(std::lround(usesMiles(unit) ? kmh / kKmhPerMph : kmh));
}

// A speed typed in the account's unit (km/h or mph) as scale km/h.
inline double shownToKmh(double v, edit::venue::LengthUnit unit = edit::venue::accountLengthUnit()) {
    return usesMiles(unit) ? v * kKmhPerMph : v;
}

// "40 km/h" or "25 mph".
inline QString scaleSpeed(double kmh, edit::venue::LengthUnit unit = edit::venue::accountLengthUnit()) {
    return usesMiles(unit) ? QCoreApplication::translate("RunPanel", "%1 mph").arg(kmhToShown(kmh, unit))
                           : QCoreApplication::translate("RunPanel", "%1 km/h").arg(kmhToShown(kmh, unit));
}

// How fast the model really runs: "0.29 m/s", "11.5 in/s" or "37 studs/s".
inline QString realSpeed(double kmh, double scale, edit::venue::LengthUnit unit = edit::venue::accountLengthUnit()) {
    const double ms = track::realMetresPerSecond(kmh, scale);
    if (unit == edit::venue::LengthUnit::FeetInches)
        return QCoreApplication::translate("RunPanel", "%1 in/s").arg(QString::number(ms / 0.0254, 'f', 1));
    if (unit == edit::venue::LengthUnit::Studs)
        return QCoreApplication::translate("RunPanel", "%1 studs/s").arg(std::lround(ms * 1000.0 / 8.0));
    return QCoreApplication::translate("RunPanel", "%1 m/s").arg(QString::number(ms, 'f', 2));
}

}  // namespace bld::ui::RunSpeeds
