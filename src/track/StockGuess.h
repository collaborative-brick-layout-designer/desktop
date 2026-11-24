#pragma once

// Guessing a model's trucks and couplers (trains design 3.3, Phase 6): an
// imported model's parts, seen from above, say where its wheels and magnets
// are. Bogie plates and motors are the trucks' pivots; failing those, wheel
// clusters; failing those, a quarter of the length in from each end. The
// web's packages/parts-catalog/src/track/stockGuess.ts is the twin; both run
// fixtures/contracts/truck-guess.json and keep its `parts` word for word.

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

namespace bld::track {

struct GuessRef {
    QString id; // the part's file name as the model names it (folders and .dat allowed)
    QPointF at; // its origin seen from above, studs (y down)
};

struct StockGuess {
    int quarterTurns = 0; // clockwise quarter turns that make it run along x, its front to the right
    double length = 0.0;  // coupler to coupler
    double width = 0.0;
    double bodyLength = 0.0;
    double front = 0.0; // trucks: studs from the picture's middle along the car
    double rear = 0.0;
    QString trucksFrom = QStringLiteral("none");   // bogies, wheels or none (the quick-car default)
    QString couplersFrom = QStringLiteral("none"); // found or none
    bool operator==(const StockGuess&) const = default;
};

// The part table (the contract's `parts`), as JSON.
QJsonObject stockPartsTable();

// A model's part file name as the table lists it: no folders, no .dat, lower case.
QString stockPartId(const QString& file);

// Where a model's trucks and couplers are. `refs`: its parts from above;
// `bounds`: its footprint; `centre`: the picture's middle (all studs, one frame).
StockGuess guessStock(const std::vector<GuessRef>& refs, const QRectF& bounds, QPointF centre);

} // namespace bld::track
