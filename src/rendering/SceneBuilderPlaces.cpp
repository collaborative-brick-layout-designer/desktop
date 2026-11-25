// Places on the map (Live Ops design 6): with View > Places, each spot's
// stretch of track as a bracket in its kind's color with the cars it holds,
// and each place's name beside its first spot. The web's
// apps/web/src/editor/render/PlaceLayer.tsx draws the same.

#include "SceneBuilder.h"

#include "../core/Map.h"
#include "../track/PlacesOnTrack.h"

#include <QBrush>
#include <QFont>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace bld::rendering {

namespace {

constexpr double kZ = 0.965e7;  // over the trains, under the fit checks' rings
constexpr double kTick = 5.0;   // the bracket's end marks reach this far each side of the track (studs)

// The mark across the track at one end of a line: at `a`, the line going on to `b`.
void addTick(QPainterPath& path, QPointF a, QPointF b, double px) {
    const double d = std::max(1e-9, std::hypot(b.x() - a.x(), b.y() - a.y()));
    const QPointF n(-(b.y() - a.y()) / d * kTick, (b.x() - a.x()) / d * kTick);
    path.moveTo((a - n) * px);
    path.lineTo((a + n) * px);
}

}  // namespace

QColor placeColor(const QString& kind) {
    if (kind == QLatin1String("industry")) return { 0x0F, 0x76, 0x6E };
    if (kind == QLatin1String("yard")) return { 0x7C, 0x3A, 0xED };
    if (kind == QLatin1String("staging")) return { 0xB4, 0x53, 0x09 };
    if (kind == QLatin1String("interchange")) return { 0x1D, 0x4E, 0xD8 };
    return { 0x47, 0x55, 0x69 };
}

void SceneBuilder::clearPlaces() {
    for (auto* it : placeItems_) {
        scene_.removeItem(it);
        delete it;
    }
    placeItems_.clear();
    placeLabels_.clear();
}

void SceneBuilder::refreshPlaces(const core::Map& map) {
    clearPlaces();
    addPlaces(map);
}

QList<QJsonArray> SceneBuilder::placesIn(const core::Map& map) {
    QList<QJsonArray> out{ map.sidecar.layoutPlaces };
    for (const auto& m : map.sidecar.modules) out << m.places;
    return out;
}

void SceneBuilder::addPlaces(const core::Map& map) {
    drawnPlaces_ = placesIn(map);
    if (!showPlaces_) return;
    if (std::all_of(drawnPlaces_.begin(), drawnPlaces_.end(), [](const QJsonArray& a) { return a.isEmpty(); })) return;
    const double px = kPixelsPerStud;
    const auto add = [this](QGraphicsItem* it, double z) {
        it->setZValue(z);
        it->setAcceptedMouseButtons(Qt::NoButton);
        scene_.addItem(it);
        placeItems_ << it;
    };
    const track::TrackNetwork net = track::trackNetworkOf(map, parts_);
    const auto runs = track::trackRuns(net);
    QFont f;
    f.setBold(true);
    f.setPixelSize(static_cast<int>(1.6 * px));
    for (const QJsonArray& list : std::as_const(drawnPlaces_)) {
        for (const QJsonValue& pv : list) {
            const QJsonObject place = pv.toObject();
            const QColor color = placeColor(place.value(QLatin1String("kind")).toString());
            const QJsonArray spots = place.value(QLatin1String("spots")).toArray();
            bool first = true;
            for (const QJsonValue& sv : spots) {
                const QJsonObject spot = sv.toObject();
                const track::SpotNow now = track::spotNow(&net, runs, spot);
                if (!now.at || now.at->line.empty()) continue;
                const auto& line = now.at->line;
                if (line.size() > 1) {
                    QPainterPath along;
                    along.moveTo(line.front() * px);
                    for (const QPointF& p : line) along.lineTo(p * px);
                    auto* band = new QGraphicsPathItem(along);
                    QColor faint = color;
                    faint.setAlphaF(0.45F);
                    band->setPen(QPen(faint, px, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
                    band->setData(0, QStringLiteral("place-spot"));
                    add(band, kZ);
                    QPainterPath ends;
                    addTick(ends, line.front(), line[1], px);
                    addTick(ends, line.back(), line[line.size() - 2], px);
                    auto* ticks = new QGraphicsPathItem(ends);
                    ticks->setPen(QPen(color, 0.5 * px));
                    add(ticks, kZ);
                }
                QStringList words;
                if (first) words << place.value(QLatin1String("name")).toString();
                first = false;
                QStringList second;
                if (spots.size() > 1) second << spot.value(QLatin1String("name")).toString();
                if (now.capacity && placeCars_) second << placeCars_(*now.capacity);
                second.removeAll(QString());
                if (!second.isEmpty()) words << second.join(QStringLiteral(": "));
                words.removeAll(QString());
                if (words.isEmpty()) continue;
                auto* text = new QGraphicsSimpleTextItem(words.join(QLatin1Char('\n')));
                text->setFont(f);
                text->setBrush(Qt::white);
                const QRectF b = text->boundingRect();
                const QPointF c = now.at->middle * px;
                auto* tag = new QGraphicsRectItem(QRectF(c.x() - b.width() / 2 - 4, c.y() - b.height() - 8, b.width() + 8, b.height() + 4));
                tag->setBrush(color);
                tag->setPen(Qt::NoPen);
                tag->setData(0, QStringLiteral("place-label"));
                text->setPos(c.x() - b.width() / 2, c.y() - b.height() - 6);
                add(tag, kZ + 0.5);
                add(text, kZ + 0.6);
                placeLabels_ << text->text();
            }
        }
    }
}

}  // namespace bld::rendering
