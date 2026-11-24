#pragma once

// The truck guess for an imported model (trains design 3.3, Phase 6): its
// parts seen from above, in the frame the picture is drawn in (x, y = -LDraw
// z; LDD's own axes y = z; 20 LDU a stud). The web's
// apps/web/src/import/client/stockGuess.ts does the same.

#include "../track/StockGuess.h"
#include "ldraw/LDrawReader.h"

#include <QRectF>

#include <vector>

namespace bld::import {

// The model's parts from above, studs.
std::vector<track::GuessRef> topDownRefs(const LDrawReadResult& read);

// The guess for a model drawn as `spriteStuds`, its mesh `bounds` (studs, the same frame).
track::StockGuess stockGuessFor(const LDrawReadResult& read, const QRectF& bounds, const QRectF& spriteStuds);

} // namespace bld::import
