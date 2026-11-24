#include "StockModel.h"

namespace bld::import {

namespace {
constexpr double kLduPerStud = 20.0;
}

std::vector<track::GuessRef> topDownRefs(const LDrawReadResult& read) {
    std::vector<track::GuessRef> out;
    out.reserve(read.parts.size());
    for (const auto& p : read.parts)
        out.push_back({ p.filename, QPointF(p.x / kLduPerStud, (read.lddAxes ? p.z : -p.z) / kLduPerStud) });
    return out;
}

track::StockGuess stockGuessFor(const LDrawReadResult& read, const QRectF& bounds,
                                const QRectF& spriteStuds) {
    return track::guessStock(topDownRefs(read), bounds, spriteStuds.center());
}

} // namespace bld::import
