#include "SceneBuilder.h"
#include "MapText.h"
#include "UnknownPart.h"
#include "SceneBuilderInternal.h"

#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QSettings>

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace bld::rendering {

// Pull the shared helpers (kPx, studToPx, LayerSink, kBrickData* keys)
// from detail:: so the per-layer free helpers below read naturally.
// SceneBuilderElectric.cpp and SceneBuilderSidecar.cpp do the same.
using detail::kPx;
using detail::studToPx;
using detail::LayerSink;
using detail::kBrickDataLayerIndex;
using detail::kBrickDataGuid;
using detail::kBrickDataKind;

namespace {

// Selection is now painted from MapView::drawForeground so every item kind
// (pixmap, rect, line, ellipse) gets a consistent, unmistakable highlight
// regardless of subclass-specific paint overrides. This helper stays only
// to hide connection-point dots on selection changes below.

// Palette matching BlueBrick's ConnectionType.Color-ish defaults: rail (cyan),
// road (orange), monorail (purple), and a fallback green. Type 0 means "no
// type" and never rendered.
QColor colorForConnectionType(const QString& /*type*/) {
    // User prefers all connection markers in a single bright red so they
    // stand out consistently against every brick color (green baseplates,
    // grey road, blue sky, etc.). Per-type color palettes can come back
    // later if users ask for them.
    return QColor(230, 40, 40);
}

// Toggles visibility of every connection-point marker child so they only
// appear while the parent brick is selected. Children are tagged via
// setData(kBrickDataKind, "connDot") by addBrickLayer so we pick them out
// without disturbing anchored-label children etc.
void setConnectionDotsVisible(QGraphicsItem* parent, bool visible) {
    for (QGraphicsItem* child : parent->childItems()) {
        if (child->data(kBrickDataKind).toString() == QStringLiteral("connDot")) {
            child->setVisible(visible);
        }
    }
}

// A part's picture: its connection dots show while it is picked. MapView
// moves and snaps the parts it drags (MapViewDrag.cpp).
class SnappingPixmap : public QGraphicsPixmapItem {
public:
    using QGraphicsPixmapItem::QGraphicsPixmapItem;
protected:
    QVariant itemChange(GraphicsItemChange c, const QVariant& v) override {
        if (c == ItemSelectedChange) {
            setConnectionDotsVisible(this, v.toBool());
        }
        return QGraphicsPixmapItem::itemChange(c, v);
    }
};

// One text cell (in the sheet at `layerIndex`).
void addTextCell(const core::TextCell& cell, LayerSink& sink, int layerIndex) {
    // The web's text (MapText.h): the bundled font, fitted into the
    // display area BlueBrick measured, lines aligned as the cell says.
    const TextCellLayout at = textCellLayout(cell, mapLineWidth(cell.font.styleString));
    const QFont f = mapFont(cell.font.styleString, at.fontPx);
    auto* t = new QGraphicsPathItem(textPath(f, at.lines, kMapLineHeight));
    t->setPen(Qt::NoPen);
    t->setBrush(QBrush(cell.fontColor.color));
    QTransform tr;
    tr.translate(at.centre.x(), at.centre.y());
    tr.rotate(at.rotation);
    tr.translate(-at.width / 2.0, -at.height / 2.0);
    t->setTransform(tr);
    t->setFlag(QGraphicsItem::ItemIsSelectable, true);
    t->setFlag(QGraphicsItem::ItemIsMovable,    true);  // BlueBrick MoveText
    t->setData(kBrickDataLayerIndex, layerIndex);
    t->setData(kBrickDataGuid,       cell.guid);
    t->setData(kBrickDataKind,       QStringLiteral("text"));
    sink.add(t);
}

}  // namespace

QColor areaCellColor(const QColor& cell, int transparency) {
    QColor c = cell;
    c.setAlpha((255 * std::clamp(transparency, 0, 100)) / 100);
    return c;
}

namespace {

// One painted area cell: its RGB at the sheet's alpha. Vanilla BlueBrick
// draws a cell in (255 * transparency) / 100 in whole numbers, replacing the
// cell's own alpha (LayerArea.cs paintCell / AlphaValue); the web does the same.
QGraphicsItem* addAreaCell(const core::AreaCell& cell, double sizePx, int transparency, LayerSink& sink) {
    auto* r = new QGraphicsRectItem(cell.x * sizePx, cell.y * sizePx, sizePx, sizePx);
    r->setPen(Qt::NoPen);
    r->setBrush(QBrush(areaCellColor(cell.color, transparency)));
    sink.add(r);
    return r;
}


QString formatDistance(double studs, int unit) {
    // Upstream Tools/Distance.cs Unit enum:
    //   0 STUD, 1 LDU, 2 STRAIGHT_TRACK, 3 MODULE, 4 METER, 5 FEET.
    // 1 stud = 20 LDU = 1/16 track = 1/96 AFOL module = 8 mm = 0.026248 ft.
    switch (unit) {
        case 1:  return QStringLiteral("%1 LDU").arg(studs * 20.0, 0, 'f', 0);
        case 2:  return QStringLiteral("%1 tracks").arg(studs / 16.0, 0, 'f', 2);
        case 3:  return QStringLiteral("%1 mod").arg(studs / 96.0, 0, 'f', 2);
        case 4:  return QStringLiteral("%1 m").arg(studs * 0.008, 0, 'f', 3);
        case 5:  return QStringLiteral("%1 ft").arg(studs * 0.026248, 0, 'f', 2);
        default: return QStringLiteral("%1 studs").arg(studs, 0, 'f', 2);
    }
}

// Add an ephemeral text label (measurement readout) to the ruler group at
// the given scene-pixel position, rotated to `rotationDeg`, in the chosen
// font + color.
void addRulerLabel(LayerSink& sink, const QString& text,
                   QPointF scenePosPx, double rotationDeg,
                   const core::FontSpec& fontSpec, const core::ColorSpec& colorSpec) {
    if (text.isEmpty()) return;
    auto* t = new QGraphicsSimpleTextItem(text);
    QFont f(mapFontFamily());
    f.setBold(fontSpec.styleString.contains(QStringLiteral("Bold")));
    f.setItalic(fontSpec.styleString.contains(QStringLiteral("Italic")));
    f.setPixelSize(std::max(6, static_cast<int>(fontSpec.sizePt * 1.6)));
    t->setFont(f);
    t->setBrush(QBrush(colorSpec.color));
    const QRectF bb = t->boundingRect();
    QTransform tr;
    tr.translate(scenePosPx.x(), scenePosPx.y());
    tr.rotate(rotationDeg);
    tr.translate(-bb.width() / 2.0, -bb.height() - 4.0);  // sit just above the line
    t->setTransform(tr);
    sink.add(t);
}

// A ruler's ends (a circle's centre is end1), in studs: on the pivot of
// the part it is fixed to, else where it was put. Attached rulers track
// their parts: the centre is read again on every draw.
std::pair<QPointF, QPointF> rulerEnds(const core::LayerRuler::AnyRuler& any,
                                      const QHash<QString, QPointF>& brickCentreByGuid) {
    const auto resolveAnchor = [&](const QString& brickId, QPointF fallbackStud) -> QPointF {
        if (brickId.isEmpty()) return fallbackStud;
        const auto it = brickCentreByGuid.constFind(brickId);
        return it == brickCentreByGuid.constEnd() ? fallbackStud : it.value();
    };
    if (any.kind == core::RulerKind::Linear)
        return { resolveAnchor(any.linear.attachedBrick1Id, any.linear.point1),
                 resolveAnchor(any.linear.attachedBrick2Id, any.linear.point2) };
    return { resolveAnchor(any.circular.attachedBrickId, any.circular.center), QPointF() };
}

// One ruler's pieces, with its ends at `end1` and `end2` (rulerEnds).
void addRuler(const core::LayerRuler::AnyRuler& any, LayerSink& sink, int layerIndex, QPointF end1, QPointF end2) {
    if (any.kind == core::RulerKind::Linear) {
        const auto& r = any.linear;
        // Resolve each endpoint: attached → brick centre, unattached → stored point.
        const QPointF p1Stud = end1;
        const QPointF p2Stud = end2;
        const QPointF anchor1(studToPx(p1Stud.x()), studToPx(p1Stud.y()));
        const QPointF anchor2(studToPx(p2Stud.x()), studToPx(p2Stud.y()));

        // The MAIN drawn line is between the *offsetted* points when
        // AllowOffset is on — vanilla moves the visible measure line
        // away from the anchors. When AllowOffset is off, offsetP == anchor.
        QPointF offsetP1 = anchor1, offsetP2 = anchor2;
        const QPointF dir = anchor2 - anchor1;
        const double  len = std::hypot(dir.x(), dir.y());
        // Perpendicular-offset vector: vanilla BlueBrick computes this
        // as (Uy, -Ux) where U is the unit vector along the ruler line
        // (LayerRulerItem.cs updateDisplayData line 877). Our previous
        // (-Uy, Ux) was opposite sign, so an OffsetDistance of -48 on a
        // left-to-right ruler moved the line upward instead of downward
        // and the red Nikki-area ruler ended up above the map.
        QPointF nrm;
        if (len > 0.001) nrm = QPointF(dir.y() / len, -dir.x() / len);
        const bool needOffset = r.allowOffset && std::abs(r.offsetDistance) > 0.001f && len > 0.001;
        if (needOffset) {
            const double off = studToPx(r.offsetDistance);
            offsetP1 += nrm * off;
            offsetP2 += nrm * off;
        }

        QPen mainPen(r.color.color);
        mainPen.setWidthF(r.lineThickness);
        mainPen.setCapStyle(Qt::FlatCap);

        // Measurement label + segmented main line (split around the text)
        // when displayDistance is on; otherwise one solid line.
        QGraphicsLineItem* selectableLine = nullptr;
        if (r.displayDistance) {
            // Distance uses the RESOLVED endpoints so attached rulers
            // show the current distance between their bricks, not the
            // frozen-at-creation distance between stored points.
            const QPointF delta = p2Stud - p1Stud;
            const double distStuds = std::hypot(delta.x(), delta.y());
            const QString text = r.displayUnit
                ? formatDistance(distStuds, r.unit)
                : QString::number(distStuds, 'f', 2);
            const QPointF midPx = (offsetP1 + offsetP2) / 2.0;

            // Keep the label readable: if the ruler's line angle would
            // rotate the text upside-down (90° < |angle| ≤ 180°), flip
            // by 180° so it stays right-reading. Upstream uses the same
            // "always readable" convention.
            double angleDeg = std::atan2(offsetP2.y() - offsetP1.y(),
                                          offsetP2.x() - offsetP1.x()) * 180.0 / M_PI;
            if (angleDeg > 90.0 || angleDeg < -90.0) angleDeg += 180.0;

            // Build label sized proportionally to the ruler's length.
            // Scaling to length (not to zoom) keeps the text legible
            // without ballooning at high zoom. Targets ~6% of the
            // ruler's pixel length for readout height, capped so a
            // huge ruler (like a 64-stud baseplate measurement)
            // doesn't get a heading-sized label.
            QFont f(mapFontFamily());
            f.setBold(r.measureFont.styleString.contains(QStringLiteral("Bold")));
            f.setItalic(r.measureFont.styleString.contains(QStringLiteral("Italic")));
            const double targetLabelPx = std::clamp(len * 0.06, 9.0, 36.0);
            f.setPixelSize(std::max(8, static_cast<int>(targetLabelPx)));
            QFontMetricsF fm(f);
            const double halfText = fm.horizontalAdvance(text) / 2.0 + 6.0;

            // Unit vector along the ruler line (in offset-line coords).
            QPointF unit(dir.x() / len, dir.y() / len);
            const QPointF mid1 = midPx - unit * halfText;
            const QPointF mid2 = midPx + unit * halfText;

            auto* seg1 = new QGraphicsLineItem(offsetP1.x(), offsetP1.y(), mid1.x(), mid1.y());
            auto* seg2 = new QGraphicsLineItem(offsetP2.x(), offsetP2.y(), mid2.x(), mid2.y());
            seg1->setPen(mainPen);
            seg2->setPen(mainPen);
            sink.add(seg1);
            sink.add(seg2);
            // Both segments AND the label tag with the same ruler guid
            // so clicking any piece selects the logical ruler — and
            // so refreshSelectionOverlay can union all pieces into one
            // highlight rather than outlining each segment separately.
            seg2->setFlag(QGraphicsItem::ItemIsSelectable, true);
            seg2->setFlag(QGraphicsItem::ItemIsMovable,    true);
            seg2->setData(kBrickDataLayerIndex, layerIndex);
            seg2->setData(kBrickDataGuid,       r.guid);
            seg2->setData(kBrickDataKind,       QStringLiteral("ruler"));
            selectableLine = seg1;

            // Label centred on midPx, rotated to match the (possibly
            // flipped) line angle so text always reads left-to-right.
            auto* t = new QGraphicsSimpleTextItem(text);
            t->setFont(f);
            t->setBrush(QBrush(r.measureFontColor.color));
            const QRectF bb = t->boundingRect();
            QTransform tr;
            tr.translate(midPx.x(), midPx.y());
            tr.rotate(angleDeg);
            tr.translate(-bb.width() / 2.0, -bb.height() / 2.0);
            t->setTransform(tr);
            t->setFlag(QGraphicsItem::ItemIsSelectable, true);
            t->setFlag(QGraphicsItem::ItemIsMovable,    true);
            t->setData(kBrickDataLayerIndex, layerIndex);
            t->setData(kBrickDataGuid,       r.guid);
            t->setData(kBrickDataKind,       QStringLiteral("ruler"));
            sink.add(t);
        } else {
            // Single solid line from offsetP1 to offsetP2.
            auto* line = new QGraphicsLineItem(offsetP1.x(), offsetP1.y(),
                                                offsetP2.x(), offsetP2.y());
            line->setPen(mainPen);
            sink.add(line);
            selectableLine = line;
        }

        // Tag the first main-line segment as the ruler's selectable
        // proxy so right-click / double-click → Properties works.
        if (selectableLine) {
            selectableLine->setFlag(QGraphicsItem::ItemIsSelectable, true);
            selectableLine->setFlag(QGraphicsItem::ItemIsMovable,    true);
            selectableLine->setData(kBrickDataLayerIndex, layerIndex);
            selectableLine->setData(kBrickDataGuid,       r.guid);
            selectableLine->setData(kBrickDataKind,       QStringLiteral("ruler"));
            selectableLine->setData(SceneBuilder::kRulerBandRole, QLineF(offsetP1, offsetP2));
            selectableLine->setData(SceneBuilder::kRulerThicknessRole, static_cast<double>(r.lineThickness));
        }

        // Vanilla BlueBrick draws perpendicular tick caps on each end
        // of a ruler when the distance isn't shown AND the ruler is
        // shorter than 4 studs — without the caps, a very short
        // ruler reads as "just a short line" with no indication of
        // where it begins and ends. The caps are 4 studs long,
        // centred on each endpoint, perpendicular to the ruler
        // direction. Matches the `needToDrawArrowForSmallDistance`
        // branch of LayerRulerItem.cs::draw.
        const double lenStuds = std::hypot(p2Stud.x() - p1Stud.x(),
                                            p2Stud.y() - p1Stud.y());
        const bool drawEndCaps = !r.displayDistance && lenStuds < 4.0 && len > 0.001;
        if (drawEndCaps) {
            // Perpendicular unit vector (same sign as vanilla's
            // offsetNormalizedVector). Caps extend 4 studs in each
            // direction = 8 studs total width.
            const QPointF perp(dir.y() / len, -dir.x() / len);
            const double capPx = studToPx(4.0);
            QPen capPen(r.guidelineColor.color);
            capPen.setWidthF(std::max(0.5f, r.guidelineThickness));
            capPen.setCosmetic(true);
            QList<qreal> dashes;
            for (float d : r.guidelineDashPattern) if (d > 0) dashes.append(d);
            if (dashes.size() >= 2) capPen.setDashPattern(dashes);
            auto addCap = [&](QPointF p){
                const QPointF a = p + perp * capPx;
                const QPointF b = p - perp * capPx;
                auto* cap = new QGraphicsLineItem(a.x(), a.y(), b.x(), b.y());
                cap->setPen(capPen);
                cap->setData(kBrickDataLayerIndex, layerIndex);
                cap->setData(kBrickDataGuid,       r.guid);
                cap->setData(kBrickDataKind,       QStringLiteral("ruler"));
                sink.add(cap);
            };
            addCap(offsetP1);
            addCap(offsetP2);
        }

        // Dashed guidelines from the anchor points to the offset line's
        // matching endpoints — only when allowOffset actually moves the
        // line away from the anchors. Matches vanilla's penForGuideline.
        if (needOffset) {
            QPen gpen(r.guidelineColor.color);
            gpen.setWidthF(std::max(0.5f, r.guidelineThickness));
            gpen.setCosmetic(true);
            QList<qreal> dashes;
            for (float d : r.guidelineDashPattern) if (d > 0) dashes.append(d);
            if (dashes.size() >= 2) gpen.setDashPattern(dashes);
            else                     gpen.setStyle(Qt::DashLine);

            auto* g1 = new QGraphicsLineItem(anchor1.x(), anchor1.y(), offsetP1.x(), offsetP1.y());
            auto* g2 = new QGraphicsLineItem(anchor2.x(), anchor2.y(), offsetP2.x(), offsetP2.y());
            g1->setPen(gpen);
            g2->setPen(gpen);
            sink.add(g1);
            sink.add(g2);

            // Small filled circles at the actual anchor points so the
            // user can see where the ruler is anchored when it's been
            // offset away from the visible measure line. Colored to
            // match whatever the ruler is attached to: orange = free,
            // green = attached to a brick.
            constexpr double kAnchorRadiusPx = 4.0;
            auto addAnchorMarker = [&](QPointF p, bool attached) {
                auto* dot = new QGraphicsEllipseItem(
                    p.x() - kAnchorRadiusPx, p.y() - kAnchorRadiusPx,
                    kAnchorRadiusPx * 2, kAnchorRadiusPx * 2);
                QPen pen(QColor(40, 40, 40));
                pen.setWidthF(1.5);
                pen.setCosmetic(true);
                dot->setPen(pen);
                dot->setBrush(QBrush(attached ? QColor(30, 180, 60)
                                               : QColor(240, 140, 30)));
                dot->setData(kBrickDataLayerIndex, layerIndex);
                dot->setData(kBrickDataGuid,       r.guid);
                dot->setData(kBrickDataKind,       QStringLiteral("ruler"));
                sink.add(dot);
            };
            addAnchorMarker(anchor1, !r.attachedBrick1Id.isEmpty());
            addAnchorMarker(anchor2, !r.attachedBrick2Id.isEmpty());
        }
    } else {
        const auto& r = any.circular;
        const double rPx = studToPx(r.radius);
        // Resolve centre: attached → brick centre; unattached → stored point.
        const QPointF cStud = end1;
        const QPointF cPx(studToPx(cStud.x()), studToPx(cStud.y()));

        auto* el = new QGraphicsEllipseItem(
            cPx.x() - rPx, cPx.y() - rPx, 2 * rPx, 2 * rPx);
        QPen pen(r.color.color);
        pen.setWidthF(r.lineThickness);
        el->setPen(pen);
        el->setBrush(Qt::NoBrush);
        el->setFlag(QGraphicsItem::ItemIsSelectable, true);
        el->setFlag(QGraphicsItem::ItemIsMovable,    true);
        el->setData(kBrickDataLayerIndex, layerIndex);
        el->setData(kBrickDataGuid,       r.guid);
        el->setData(kBrickDataKind,       QStringLiteral("ruler"));
        el->setData(SceneBuilder::kRulerBandRole, el->rect());
        el->setData(SceneBuilder::kRulerThicknessRole, static_cast<double>(r.lineThickness));
        sink.add(el);

        // Radius label to the right of centre.
        if (r.displayDistance) {
            const QString text = r.displayUnit
                ? formatDistance(r.radius, r.unit)
                : QString::number(r.radius, 'f', 2);
            addRulerLabel(sink, text, QPointF(cPx.x() + rPx, cPx.y()), 0.0,
                          r.measureFont, r.measureFontColor);
        }
    }
}

}  // namespace

SceneBuilder::PartLook SceneBuilder::readPartLook() const {
    // Scene-level render toggles every part reads, under view/ and
    // appearance/ in QSettings. PreferencesDialog writes the "always show
    // connection points" toggle to appearance/alwaysShowConnections.
    QSettings settings;
    PartLook look;
    look.electric = settings.value(QStringLiteral("view/electricCircuits"), false).toBool();
    look.alwaysShowConns = settings.value(QStringLiteral("appearance/alwaysShowConnections"), false).toBool();
    look.hulls = settings.value(QStringLiteral("view/brickHulls"), false).toBool();
    look.elevation = settings.value(QStringLiteral("view/brickElevation"), false).toBool();
    return look;
}

// One part's item (with its connection dots, hull and elevation as
// children). `resolvedKeys`: part number -> library key actually used (after
// the prefix fallback below), memoised so a map with many copies of an
// unresolved part scans the library once per distinct part number.
QGraphicsItem* SceneBuilder::makeBrickItem(const core::Brick& brick, const core::Layer& L, int layerIndex,
                                           QHash<QString, QString>& resolvedKeys) {
    parts::PartsLibrary& lib = parts_;
    // BlueBrick bakes the color suffix into the PartNumber string itself
    // (e.g. "3811.1" for a blue 32x32 baseplate, or just "TABLE96X190"
    // for an uncolored composite). Lookup is case-insensitive because
    // upstream stores the part number upper-cased in .bbm but the vendored
    // library uses mixed case on disk.
    auto resolved = resolvedKeys.constFind(brick.partNumber);
    if (resolved == resolvedKeys.constEnd()) {
        QString key = brick.partNumber;
        if (!lib.metadata(key)) {
            // Fallback: match by prefix so a .bbm referencing "3811" picks
            // up any color-specific variant.
            key.clear();
            const QString needle = brick.partNumber.toLower() + QLatin1Char('.');
            for (const QString& candidate : lib.keys()) {
                if (candidate.toLower().startsWith(needle)) { key = candidate; break; }
            }
        }
        resolved = resolvedKeys.insert(brick.partNumber, key);
    }
    const QString& partKey = resolved.value();
    std::optional<parts::PartMetadata> meta;
    if (!partKey.isEmpty()) meta = lib.metadata(partKey);

    const QRectF areaPx(studToPx(brick.displayArea.x()),
                        studToPx(brick.displayArea.y()),
                        studToPx(brick.displayArea.width()),
                        studToPx(brick.displayArea.height()));
    const QPointF centerPx = areaPx.center();

    QGraphicsItem* item = nullptr;
    if (meta && !meta->gifFilePath.isEmpty()) {
        // Shared, decoded-once pixmap from the library cache — loading
        // meta->gifFilePath here re-read and re-decoded the GIF for
        // every brick on every scene rebuild.
        const QPixmap pm = lib.pixmap(partKey);
        if (!pm.isNull()) {
            auto* p = new SnappingPixmap(pm);
            p->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
            p->setTransformationMode(Qt::SmoothTransformation);
            // Use the full bounding rect as the clickable / selection
            // shape, not the pixmap alpha mask. BlueBrick parts are GIFs
            // with transparent backgrounds — the default MaskShape makes
            // clicks on transparent pixels miss entirely, so the user
            // can click on what looks like the brick and get no
            // selection because the cursor landed on a transparent
            // pixel of the sprite.
            // Imported models are big and full of empty space
            // (courtyards, the inside of a ring of track): clicks there
            // reach what is underneath.
            p->setShapeMode(lib.isImported(partKey) ? QGraphicsPixmapItem::MaskShape
                                                    : QGraphicsPixmapItem::BoundingRectShape);
            p->setOffset(-pm.width() / 2.0, -pm.height() / 2.0);
            p->setTransformOriginPoint(0, 0);
            // High-DPI imports: a part authored at e.g. 16 px/stud
            // has 2× the pixel dimensions of the same brick at 8.
            // The scene runs at 8 px/stud (kPixelsPerStud), so we
            // scale the pixmap item by (kPixelsPerStud / partPxPerStud)
            // to land at the correct stud footprint. Vanilla 8-px
            // parts get scale 1.0 (no-op).
            const int authoredPxPerStud = (meta->pxPerStud > 0)
                ? meta->pxPerStud : SceneBuilder::kPixelsPerStud;
            if (authoredPxPerStud != SceneBuilder::kPixelsPerStud) {
                const double s = static_cast<double>(SceneBuilder::kPixelsPerStud)
                               / static_cast<double>(authoredPxPerStud);
                p->setScale(s);
            }
            p->setRotation(brick.orientation);
            // The item sits at the displayArea centre (what drag and
            // snap move); parts with an XML hull draw their sprite
            // off it, like BlueBrick. transform() applies after the
            // rotation, so this is a scene-space shift that also
            // carries the connection dots and hull outline.
            const QPointF offset = lib.imageOffset(partKey, brick.orientation);
            if (!offset.isNull()) p->setTransform(QTransform::fromTranslate(offset.x() * kPx, offset.y() * kPx));
            p->setPos(centerPx);
            // Drawn in the sheet's order, like BlueBrick: Bring to Front
            // works whatever the altitude (an altitude edit sorts the
            // sheet instead, EditBrickCommand).
            item = p;
        }
    }
    if (!item) {
        // A part the library doesn't know: vanilla's red cross and part
        // number (UnknownPart.h), turned with the brick. The clear rect
        // takes the clicks and moves like a part.
        const UnknownPartLook look = unknownPartLook(brick.partNumber, brick.displayArea.width(),
                                                     brick.displayArea.height());
        auto* r =
            new QGraphicsRectItem(QRectF(-look.width / 2.0, -look.height / 2.0, look.width, look.height));
        r->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        r->setPos(centerPx);
        r->setRotation(brick.orientation);
        r->setPen(Qt::NoPen);
        r->setBrush(Qt::NoBrush);
        addUnknownPartDrawing(r, brick.partNumber, look);
        item = r;
    }

    item->setFlag(QGraphicsItem::ItemIsSelectable, true);
    item->setFlag(QGraphicsItem::ItemIsMovable,    true);
    item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    item->setData(kBrickDataLayerIndex, layerIndex);
    item->setData(kBrickDataGuid,       brick.guid);
    item->setData(kBrickDataKind,       QStringLiteral("brick"));
    item->setData(SceneBuilder::kBrickPartRole, partKey);

    decorateBrickItem(item, brick, L, meta ? &*meta : nullptr);
    return item;
}

// A part's children drawn from its state: its free connection points, its
// hull and its elevation (labels on it are drawn by addAnchoredLabels).
void SceneBuilder::decorateBrickItem(QGraphicsItem* item, const core::Brick& brick, const core::Layer& L,
                                     const parts::PartMetadata* meta) {
    parts::PartsLibrary& lib = parts_;
    const bool alwaysShowConns = partLook_.alwaysShowConns;
    const bool displayHulls    = partLook_.hulls || L.hull.displayHulls;
    const bool displayElev     = partLook_.elevation;
    const QRectF areaPx(studToPx(brick.displayArea.x()), studToPx(brick.displayArea.y()),
                        studToPx(brick.displayArea.width()), studToPx(brick.displayArea.height()));
    const QPointF centerPx = areaPx.center();
    // Connection-point markers: child ellipses in the brick's local coord
    // system so they transform with the brick for free. Hidden by default;
    // shown on selection via the ItemSelectedChange hook above (or
    // unconditionally if view/connectionPoints is on). Radius bumped to
    // 6px with a white ring + type-colored fill so they stand out
    // clearly on any brick color — user asked for larger/more obvious
    // markers.
    if (meta && !meta->connections.isEmpty()) {
        // Only render markers for FREE connections — ones whose
        // linkedToId is empty on the brick instance. A connection
        // that's already glued to another brick isn't a snap
        // target, so showing a marker there just clutters the view.
        //
        // The active connection (brick.activeConnectionPointIndex)
        // gets a bigger, brighter gold marker so the user can see at
        // a glance which end will lead the next snap.
        constexpr double kDotRadiusPx       = 10.0;
        constexpr double kActiveRadiusPx    = 13.0;
        const int nConns    = meta->connections.size();
        const int activeIdx = brick.activeConnectionPointIndex;
        for (int ci = 0; ci < nConns; ++ci) {
            const auto& c = meta->connections[ci];
            if (c.type.isEmpty()) continue;
            // Only render dots for STANDARD connection types
            // (0=none, 1=rail, 2=road, 3=monorail standard, 4=monorail
            // short curve — per the BlueBrickParts XML comment on
            // 2865.8.xml). Custom library-specific types like
            // "bricktracks_lever" tag internal non-user-facing joints
            // (switch stand to switch), and sets often leave them
            // geometrically unpaired. Rendering them as free red
            // dots just clutters the view with markers the user
            // can't act on — vanilla BlueBrick hides them.
            {
                bool ok;
                c.type.toInt(&ok);
                if (!ok) continue;
            }
            const bool linked =
                ci < static_cast<int>(brick.connections.size())
                && !brick.connections[ci].linkedToId.isEmpty();
            if (linked) continue;
            const bool isActive = (ci == activeIdx);
            const double r = isActive ? kActiveRadiusPx : kDotRadiusPx;
            const QPointF localPx(c.position.x() * kPx, c.position.y() * kPx);
            auto* dot = new QGraphicsEllipseItem(
                localPx.x() - r, localPx.y() - r, r * 2, r * 2, item);
            const QColor fill = isActive
                ? QColor(255, 215, 0)              // gold for active
                : colorForConnectionType(c.type);   // type color (red) for other free
            QPen pen(isActive ? QColor(30, 30, 30) : QColor(255, 255, 255));
            pen.setWidthF(isActive ? 3.0 : 2.5);
            pen.setCosmetic(true);
            dot->setPen(pen);
            dot->setBrush(QBrush(fill));
            dot->setZValue(isActive ? 1001 : 1000);
            dot->setData(kBrickDataKind, QStringLiteral("connDot"));
            dot->setVisible(alwaysShowConns);
        }
    }

    // Hull outline — vanilla's DisplayHulls renders the brick's selection
    // polygon (we approximate with the displayArea rect for now). Drawn
    // as a sibling polygon in scene coords so it can't be occluded by
    // the parent pixmap. Picks up the layer's configured hull color +
    // thickness; falls back to a sensible default if never configured.
    if (displayHulls) {
        // Prefer the part's alpha-derived convex hull (tight fit
        // around the sprite's opaque pixels) when available; fall
        // back to the axis-aligned bounding rect for parts with no
        // pixmap. Rendered in scene coords so it rotates with the
        // brick's transform just like any child item.
        QGraphicsItem* hull = nullptr;
        QPolygonF hullStuds = lib.hullPolygonStuds(brick.partNumber);
        if (!hullStuds.isEmpty()) {
            QPolygonF hullPx;
            hullPx.reserve(hullStuds.size());
            for (const QPointF& p : hullStuds) {
                hullPx.append(QPointF(p.x() * kPx, p.y() * kPx));
            }
            auto* poly = new QGraphicsPolygonItem(hullPx);
            poly->setBrush(Qt::NoBrush);
            // Attach to the brick item so the polygon rotates along
            // with it (transform inheritance). Local coords are
            // centered on the pixmap, matching the hull polygon's
            // reference frame.
            poly->setParentItem(item);
            hull = poly;
        } else {
            // A child of the part too, so it moves with a drag.
            auto* rect = new QGraphicsRectItem(item->mapRectFromScene(areaPx));
            rect->setBrush(Qt::NoBrush);
            rect->setParentItem(item);
            rect->setAcceptedMouseButtons(Qt::NoButton);
            hull = rect;
        }
        QPen p(L.hull.color.color.isValid() ? L.hull.color.color : QColor(0, 0, 0));
        p.setWidthF(L.hull.thickness > 0 ? L.hull.thickness : 1);
        p.setCosmetic(true);
        if (auto* asPoly = qgraphicsitem_cast<QGraphicsPolygonItem*>(hull)) asPoly->setPen(p);
        else if (auto* asRect = qgraphicsitem_cast<QGraphicsRectItem*>(hull)) asRect->setPen(p);
        hull->setZValue(brick.altitude + 0.5);
        hull->setData(kBrickDataKind, QStringLiteral("partDecor"));
    }

    // Altitude label centered on the brick — matches vanilla's
    // DisplayBrickElevation (LayerBrick.cs ~line 806).
    if (displayElev && std::abs(brick.altitude) > 0.001f) {
        auto* alt = new QGraphicsSimpleTextItem(QString::number(brick.altitude, 'f', 1));
        QFont f(mapFontFamily());
        f.setPixelSize(10);
        alt->setFont(f);
        alt->setBrush(QBrush(QColor(40, 40, 40)));
        const QRectF bb = alt->boundingRect();
        // A child of the part (centred on it), so it moves with a drag.
        alt->setParentItem(item);
        alt->setAcceptedMouseButtons(Qt::NoButton);
        alt->setPos(item->mapFromScene(centerPx) - QPointF(bb.width() / 2.0, bb.height() / 2.0));
        alt->setZValue(brick.altitude + 0.6);
        alt->setData(kBrickDataKind, QStringLiteral("partDecor"));
    }

}

bool SceneBuilder::redrawBrickInPlace(QGraphicsItem* item, const core::Brick& brick, const core::Layer& L) {
    // Only a part drawn from its picture, as the same part: it is turned and
    // moved where it is now and its children drawn again. The item stays
    // (picked or not, in its place in the order), which is much less work
    // for the scene than taking one out and putting another in.
    auto* p = dynamic_cast<SnappingPixmap*>(item);
    if (!p) return false;
    const QString partKey = item->data(SceneBuilder::kBrickPartRole).toString();
    if (partKey.isEmpty() || brick.partNumber.compare(partKey, Qt::CaseInsensitive) != 0) return false;
    const auto meta = parts_.metadata(partKey);
    if (!meta) return false;
    for (QGraphicsItem* child : item->childItems()) {
        const QString kind = child->data(kBrickDataKind).toString();
        if (kind == QLatin1String("connDot") || kind == QLatin1String("partDecor")) delete child;
    }
    p->setRotation(brick.orientation);
    const QPointF offset = parts_.imageOffset(partKey, brick.orientation);
    p->setTransform(offset.isNull() ? QTransform() : QTransform::fromTranslate(offset.x() * kPx, offset.y() * kPx));
    p->setPos(QPointF(studToPx(brick.displayArea.center().x()), studToPx(brick.displayArea.center().y())));
    decorateBrickItem(item, brick, L, &*meta);
    if (item->isSelected()) setConnectionDotsVisible(item, true);
    return true;
}


namespace {

// Things on a sheet are drawn in its order, like BlueBrick (Bring to Front
// works whatever the altitude): each a step above the one before.
constexpr double kOrderStep = 1e-3;

void deleteItems(QGraphicsScene& scene, const QList<QGraphicsItem*>& items) {
    for (auto* it : items) {
        scene.removeItem(it);
        delete it;
    }
}

bool anySelected(const QList<QGraphicsItem*>& items) {
    return std::any_of(items.begin(), items.end(), [](const QGraphicsItem* it) { return it->isSelected(); });
}

void selectAll(const QList<QGraphicsItem*>& items) {
    for (auto* it : items)
        if (it->flags() & QGraphicsItem::ItemIsSelectable) it->setSelected(true);
}

QString rulerGuid(const core::LayerRuler::AnyRuler& any) {
    return any.kind == core::RulerKind::Linear ? any.linear.guid : any.circular.guid;
}

// The same part as drawn, cheapest fields first (exactly: a part moved by a
// hair is drawn again). Of its connections only which are free shows (their
// ids don't: a copy read again can give them new ones).
bool sameBrick(const core::Brick& a, const core::Brick& b) {
    const QRectF& ra = a.displayArea;
    const QRectF& rb = b.displayArea;
    if (!(ra.x() == rb.x() && ra.y() == rb.y() && ra.width() == rb.width() && ra.height() == rb.height()
          && a.orientation == b.orientation && a.altitude == b.altitude
          && a.activeConnectionPointIndex == b.activeConnectionPointIndex
          && a.connections.size() == b.connections.size() && a.partNumber == b.partNumber
          && a.myGroupId == b.myGroupId && a.guid == b.guid))
        return false;
    for (size_t i = 0; i < a.connections.size(); ++i)
        if (a.connections[i].linkedToId.isEmpty() != b.connections[i].linkedToId.isEmpty()) return false;
    return true;
}

QPointF brickPosPx(const core::Brick& b) {
    return QPointF(studToPx(b.displayArea.center().x()), studToPx(b.displayArea.center().y()));
}

}  // namespace

SceneBuilder::SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts)
    : scene_(scene), parts_(parts) {}

QPointF SceneBuilder::brickCentre(const core::Brick& b) const {
    // Rulers attach to the brick's pivot (its sprite centre).
    return parts::placement::imageCentre(b, parts_);
}

void SceneBuilder::clearLabels() {
    // Labels on a part are its children: deleting one takes it off its part.
    for (auto* it : labelItems_) {
        if (!it->parentItem()) scene_.removeItem(it);
        delete it;
    }
    labelItems_.clear();
    worldLabelItems_.clear();
}

void SceneBuilder::clearVenue() {
    deleteItems(scene_, venueItems_);
    venueItems_.clear();
}

void SceneBuilder::clearModuleLabels() {
    deleteItems(scene_, moduleLabelItems_);
    moduleLabelItems_.clear();
    moduleAnnotationRects_.clear();
    shortenedModuleNames_.clear();
}

void SceneBuilder::clearElectric() {
    deleteItems(scene_, electricItems_);
    electricItems_.clear();
}

void SceneBuilder::takeLabelsOff(QGraphicsItem* partItem) {
    for (QGraphicsItem* child : partItem->childItems()) {
        if (child->data(kBrickDataKind).toString() != QLatin1String("label")) continue;
        labelItems_.removeOne(child);
        labelsTakenOff_ << child;  // (update() reports it gone)
        delete child;
        labelsDirty_ = true;
    }
}

void SceneBuilder::clear() {
    // Items added since the event loop last ran wait in the scene's
    // "unpolished" list, and removing each one scans that list: clearing
    // right after a build (two rebuilds in one event-loop turn) went
    // quadratic, ~250 ms for 10 000 items. Polish them first (a queued
    // call on the scene) so removal stays linear.
    QCoreApplication::sendPostedEvents(&scene_, QEvent::MetaCall);
    ++generation_;
    clearLabels();  // first: some are children of parts
    for (auto& d : layers_) deleteItems(scene_, d.items);
    layers_.clear();
    brickByGuid_.clear();
    clearVenue();
    clearModuleLabels();
    clearElectric();
    drawnVenue_.reset();
    drawnLabels_.clear();
    drawnModules_.clear();
    built_ = false;
}

void SceneBuilder::build(const core::Map& map) {
    clear();
    ++generation_;
    partLook_ = readPartLook();
    // Precompute every brick's world centre in studs so attached rulers,
    // anchored labels, and any future cross-item feature can resolve the
    // partner position by guid without a linear scan.
    brickCentreByGuid_.clear();
    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layerPtr).bricks)
            brickCentreByGuid_.insert(b.guid, brickCentre(b));
    }
    addVenue(map);  // z = -100 so it sits beneath every layer
    drawnVenue_ = map.sidecar.venue;
    layers_.assign(map.layers().size(), LayerDrawn{});
    for (size_t i = 0; i < map.layers().size(); ++i) addLayer(*map.layers()[i], static_cast<int>(i));
    addAnchoredLabels(map);
    drawnLabels_ = map.sidecar.anchoredLabels;
    addModuleLabels(map);
    drawnModules_ = map.sidecar.modules;
    addElectricCircuits(map);
    built_ = true;
}

void SceneBuilder::refreshModuleLabels(const core::Map& map) {
    clearModuleLabels();
    addModuleLabels(map);
    drawnModules_ = map.sidecar.modules;
}

void SceneBuilder::rebuildFollowers(const core::Map& posed, const QSet<QString>& moving,
                                    const QSet<int>& keepRulerLayers) {
    clearModuleLabels();
    clearElectric();
    addModuleLabels(posed, true);
    addElectricCircuits(posed);

    // Rulers fixed to a moving part: their ends follow its pivot.
    for (const auto& layerPtr : posed.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layerPtr).bricks)
            if (moving.contains(b.guid)) brickCentreByGuid_.insert(b.guid, brickCentre(b));
    }
    for (int i = 0; i < static_cast<int>(posed.layers().size()); ++i) {
        const auto& layerPtr = posed.layers()[static_cast<size_t>(i)];
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Ruler || keepRulerLayers.contains(i)) continue;
        if (i >= static_cast<int>(layers_.size()) || !layers_[static_cast<size_t>(i)].present) continue;
        bool fixed = false;
        for (const auto& any : static_cast<const core::LayerRuler&>(*layerPtr).rulers) {
            if (any.kind == core::RulerKind::Linear)
                fixed = fixed || moving.contains(any.linear.attachedBrick1Id)
                        || moving.contains(any.linear.attachedBrick2Id);
            else fixed = fixed || moving.contains(any.circular.attachedBrickId);
        }
        if (!fixed) continue;
        ++generation_;
        const bool shown = layers_[static_cast<size_t>(i)].shown;
        clearLayer(i);
        addLayer(*layerPtr, i);
        setLayerVisible(i, shown);
    }
}

void SceneBuilder::clearLayer(int layerIndex) {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) return;
    LayerDrawn& d = layers_[static_cast<size_t>(layerIndex)];
    for (auto it = d.bricks.cbegin(); it != d.bricks.cend(); ++it) {
        if (!it->items.isEmpty() && brickByGuid_.value(it.key()) == it->items.front()) brickByGuid_.remove(it.key());
        for (auto* item : it->items) takeLabelsOff(item);
    }
    deleteItems(scene_, d.items);
    d = LayerDrawn{};
}

void SceneBuilder::addLayer(const core::Layer& L, int layerIndex) {
    // Each per-layer item goes DIRECTLY into the scene — no parent group —
    // so hit-testing / selection / mouse events aren't intercepted by any
    // container. Sheets stack by baseZ; things on a sheet by its order.
    if (layerIndex >= static_cast<int>(layers_.size())) layers_.resize(static_cast<size_t>(layerIndex) + 1);
    LayerDrawn& d = layers_[static_cast<size_t>(layerIndex)];
    d = LayerDrawn{};
    d.present = true;
    d.kind = L.kind();
    d.guid = L.guid;
    d.transparency = L.transparency;
    d.hullsShown = L.hull.displayHulls;
    d.hullColor = L.hull.color.color.rgba();
    d.hullThickness = L.hull.thickness;
    d.visible = L.visible;
    d.shown = L.visible;
    d.baseZ = static_cast<double>(layerIndex) * 1000.0;
    LayerSink sink{ scene_, d.items, d.baseZ, L.visible };
    // Per-layer transparency scales each item's opacity (a painted area
    // already has it in its colors).
    const double opacity = std::clamp(L.transparency, 0, 100) / 100.0;
    const auto look = [opacity](const QList<QGraphicsItem*>& items, double z) {
        for (auto* it : items) {
            it->setZValue(z);
            if (opacity < 1.0) it->setOpacity(opacity);
        }
    };
    double z = d.baseZ;

    switch (L.kind()) {
        case core::LayerKind::Grid:
            // Grid lines are painted by MapView::drawBackground, not as scene
            // items. Nothing to add here.
            break;
        case core::LayerKind::Brick: {
            const auto& BL = static_cast<const core::LayerBrick&>(L);
            QHash<QString, QString> resolvedKeys;
            d.bricks.reserve(static_cast<qsizetype>(BL.bricks.size()));
            for (const auto& b : BL.bricks) {
                QGraphicsItem* item = makeBrickItem(b, L, layerIndex, resolvedKeys);
                sink.add(item);
                look({ item }, z += kOrderStep);
                brickByGuid_.insert(b.guid, item);
                d.bricks.insert(b.guid, Thing<core::Brick>{ { item }, z, epoch_, b });
                ++drawnTotal_;
            }
            d.groups = BL.groups;
            break;
        }
        case core::LayerKind::Text:
            for (const auto& cell : static_cast<const core::LayerText&>(L).textCells) {
                QList<QGraphicsItem*> items;
                LayerSink one{ scene_, items, d.baseZ, L.visible };
                addTextCell(cell, one, layerIndex);
                look(items, z += kOrderStep);
                d.items += items;
                d.texts.insert(cell.guid, Thing<core::TextCell>{ items, z, epoch_, cell });
                ++drawnTotal_;
            }
            break;
        case core::LayerKind::Area: {
            const auto& A = static_cast<const core::LayerArea&>(L);
            d.areaCellSize = A.areaCellSizeInStud;
            const double sizePx = studToPx(A.areaCellSizeInStud);
            for (const auto& cell : A.cells) {
                QGraphicsItem* item = addAreaCell(cell, sizePx, A.transparency, sink);
                d.cells.insert(QPoint(cell.x, cell.y), { item, cell.color.rgba() });
                ++drawnTotal_;
            }
            break;
        }
        case core::LayerKind::Ruler:
            for (const auto& any : static_cast<const core::LayerRuler&>(L).rulers) {
                const auto ends = rulerEnds(any, brickCentreByGuid_);
                QList<QGraphicsItem*> items;
                LayerSink one{ scene_, items, d.baseZ, L.visible };
                addRuler(any, one, layerIndex, ends.first, ends.second);
                look(items, z += kOrderStep);
                d.items += items;
                d.rulers.insert(rulerGuid(any), Thing<RulerDrawn>{ items, z, epoch_, { any, ends.first, ends.second } });
                ++drawnTotal_;
            }
            break;
        case core::LayerKind::AnchoredText:
            break;
    }
}

bool SceneBuilder::setLayerVisible(int layerIndex, bool visible) {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) return false;
    LayerDrawn& d = layers_[static_cast<size_t>(layerIndex)];
    if (!d.present) return false;
    // Every item of the sheet shows as `shown` says (new ones are made so).
    if (d.shown == visible) return true;
    d.shown = visible;
    for (auto* item : d.items) item->setVisible(visible);
    return true;
}

template <class Stored>
void SceneBuilder::orderSheet(LayerDrawn& d, QHash<QString, Thing<Stored>>& things,
                              const std::function<QString(int)>& guidAt, const std::vector<double>& zs) {
    // `zs`: each thing's z in the sheet's order now, NaN for the new ones.
    const auto setZ = [&](const QString& guid, double z) {
        auto& t = things[guid];
        t.z = z;
        for (auto* it : t.items) it->setZValue(z);
    };
    const auto renumber = [&] {
        double z = d.baseZ;
        for (int k = 0; k < static_cast<int>(zs.size()); ++k) setZ(guidAt(k), z += kOrderStep);
    };
    const int n = static_cast<int>(zs.size());
    double prev = -std::numeric_limits<double>::infinity();
    bool fresh = false;
    for (int k = 0; k < n; ++k) {
        if (std::isnan(zs[static_cast<size_t>(k)])) {
            fresh = true;
            continue;
        }
        if (zs[static_cast<size_t>(k)] <= prev) return renumber();  // the order changed
        prev = zs[static_cast<size_t>(k)];
    }
    if (!fresh) return;
    // New things between their neighbours.
    for (int k = 0; k < n;) {
        if (!std::isnan(zs[static_cast<size_t>(k)])) {
            ++k;
            continue;
        }
        int e = k;
        while (e < n && std::isnan(zs[static_cast<size_t>(e)])) ++e;
        const int run = e - k;
        const double lo = k > 0 ? zs[static_cast<size_t>(k - 1)]
                          : e < n ? zs[static_cast<size_t>(e)] - (run + 1) * kOrderStep
                                  : d.baseZ;
        const double hi = e < n ? zs[static_cast<size_t>(e)] : lo + (run + 1) * kOrderStep;
        const double step = (hi - lo) / (run + 1);
        if (!(step > 1e-9)) return renumber();
        for (int j = k; j < e; ++j) setZ(guidAt(j), lo + step * (j - k + 1));
        k = e;
    }
}

template <class Model, class Stored, class Hooks>
bool SceneBuilder::diffThings(LayerDrawn& d, QHash<QString, Thing<Stored>>& things,
                              const std::vector<Model>& models, Hooks& hooks, Change& change,
                              QSet<QGraphicsItem*>& gone) {
    const quint64 epoch = epoch_;
    std::vector<double> zs;
    zs.reserve(models.size());
    int matched = 0;
    const auto redrawInto = [&](Thing<Stored>& t, const Model& m) {
        ++change.redrawn;
        ++drawnTotal_;
        if (hooks.inPlace(t, m)) {  // the same items, drawn again
            hooks.replaced(t.model, m);
            t.model = hooks.store(m);
            return;
        }
        const bool picked = anySelected(t.items);
        hooks.erasing(t, true);
        for (auto* it : t.items) {
            change.removed << it;
            gone.insert(it);
        }
        deleteItems(scene_, t.items);
        t.items = hooks.make(m);
        hooks.replaced(t.model, m);
        t.model = hooks.store(m);
        for (auto* it : t.items) it->setZValue(t.z);
        if (picked) selectAll(t.items);
        change.added += t.items;
    };
    for (const Model& m : models) {
        const QString guid = hooks.guid(m);
        auto found = things.find(guid);
        // Two things with one guid can't be told apart: the caller draws the sheet whole.
        if (found != things.end() && found->seen == epoch) return false;
        if (found == things.end()) {
            Thing<Stored> t;
            t.items = hooks.make(m);
            t.model = hooks.store(m);
            t.seen = epoch;
            t.z = std::numeric_limits<double>::quiet_NaN();
            hooks.added(m, t);
            change.added += t.items;
            ++change.redrawn;
            ++drawnTotal_;
            zs.push_back(t.z);
            things.insert(guid, std::move(t));
                continue;
        }
        ++matched;
        found->seen = epoch;
        zs.push_back(found->z);
        if (!hooks.asDrawn(m, *found)) redrawInto(*found, m);
    }
    if (matched < things.size()) {
        for (auto it = things.begin(); it != things.end();) {
            if (it->seen == epoch) {
                ++it;
                continue;
            }
            hooks.erasing(*it, false);
            for (auto* item : it->items) {
                change.removed << item;
                gone.insert(item);
            }
            deleteItems(scene_, it->items);
            ++change.erased;
                it = things.erase(it);
        }
    }
    // (Also when nothing changed: Bring to Front only reorders the sheet.)
    orderSheet(d, things, [&](int k) { return hooks.guid(models[static_cast<size_t>(k)]); }, zs);
    return true;
}

void SceneBuilder::redrawLayer(const core::Layer& layer, int layerIndex, Change& change) {
    LayerDrawn& old = layers_[static_cast<size_t>(layerIndex)];
    const bool shown = old.shown;
    QSet<QString> picked;
    QSet<QGraphicsItem*> oldItems(old.items.begin(), old.items.end());
    change.added.removeIf([&oldItems](QGraphicsItem* it) { return oldItems.contains(it); });
    for (auto* it : old.items) {
        change.removed << it;
        if (it->isSelected()) picked.insert(it->data(kBrickDataGuid).toString());
    }
    for (auto it = old.bricks.cbegin(); it != old.bricks.cend(); ++it) change.partsRemoved.insert(it.key());
    change.erased += static_cast<int>(old.bricks.size() + old.texts.size() + old.rulers.size() + old.cells.size());
    clearLayer(layerIndex);
    addLayer(layer, layerIndex);
    LayerDrawn& d = layers_[static_cast<size_t>(layerIndex)];
    setLayerVisible(layerIndex, shown);
    for (auto* it : d.items) {
        change.added << it;
        if (picked.contains(it->data(kBrickDataGuid).toString())) selectAll({ it });
    }
    for (auto it = d.bricks.begin(); it != d.bricks.end(); ++it) {
        change.partsAdded.insert(it.key());
        brickCentreByGuid_.insert(it.key(), brickCentre(it->model));
    }
    change.redrawn += static_cast<int>(d.bricks.size() + d.texts.size() + d.rulers.size() + d.cells.size());
    if (layer.kind() == core::LayerKind::Brick) change.sheetsRegrouped.insert(layerIndex);
}

bool SceneBuilder::drawnAsIs(int layerIndex, const core::Brick& brick) const {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) return false;
    const auto& bricks = layers_[static_cast<size_t>(layerIndex)].bricks;
    const auto found = bricks.constFind(brick.guid);
    return found != bricks.constEnd() && sameBrick(found->model, brick);
}

std::optional<SceneBuilder::Change> SceneBuilder::update(const core::Map& map) {
    if (!built_) return std::nullopt;
    const auto& layers = map.layers();
    // Structural: sheets added, removed or reordered, or a sheet's look
    // (transparency, hulls, area cell size) or a setting every part reads.
    if (layers.size() != layers_.size()) return std::nullopt;
    for (size_t i = 0; i < layers.size(); ++i) {
        const core::Layer& L = *layers[i];
        const LayerDrawn& d = layers_[i];
        if (!d.present || d.kind != L.kind() || d.guid != L.guid || d.transparency != L.transparency
            || d.hullsShown != L.hull.displayHulls || d.hullColor != L.hull.color.color.rgba()
            || d.hullThickness != L.hull.thickness)
            return std::nullopt;
        if (L.kind() == core::LayerKind::Area
            && d.areaCellSize != static_cast<const core::LayerArea&>(L).areaCellSizeInStud)
            return std::nullopt;
    }
    const PartLook look = readPartLook();
    if (look != partLook_) return std::nullopt;
    partLook_.electric = look.electric;

    // Removing items made since the event loop last ran scans the scene's
    // unpolished list (see clear()).
    QCoreApplication::sendPostedEvents(&scene_, QEvent::MetaCall);
    ++epoch_;
    labelsDirty_ = false;
    labelsTakenOff_.clear();
    Change change;
    // Qt moves picked items in a drag: one that isn't where its part, text
    // or ruler is drawn (the drag ended without changing the layout) is
    // drawn again where it belongs. Only picked items can have moved.
    QSet<QString> displaced;
    for (QGraphicsItem* it : scene_.selectedItems()) {
        const int li = it->data(kBrickDataLayerIndex).toInt();
        if (li < 0 || li >= static_cast<int>(layers_.size())) continue;
        const LayerDrawn& d = layers_[static_cast<size_t>(li)];
        const QString guid = it->data(kBrickDataGuid).toString();
        if (const auto b = d.bricks.constFind(guid); b != d.bricks.constEnd()) {
            if (it->pos() != brickPosPx(b->model)
                || std::abs(it->rotation() - static_cast<double>(b->model.orientation)) > 1e-6)
                displaced.insert(guid);
        } else if (d.texts.contains(guid) || d.rulers.contains(guid)) {
            if (!it->pos().isNull()) displaced.insert(guid);
        }
    }

    // Parts first: rulers read where they are.
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->kind() != core::LayerKind::Brick) continue;
        const auto& BL = static_cast<const core::LayerBrick&>(*layers[i]);
        const int li = static_cast<int>(i);
        LayerDrawn& d = layers_[i];
        LayerSink sink{ scene_, d.items, d.baseZ, d.shown };
        const double opacity = std::clamp(BL.transparency, 0, 100) / 100.0;
        QHash<QString, QString> resolvedKeys;
        bool regrouped = false;
        struct Hooks {
            SceneBuilder& sb;
            const core::LayerBrick& BL;
            int li;
            LayerSink& sink;
            double opacity;
            QHash<QString, QString>& resolvedKeys;
            Change& change;
            bool& regrouped;
            const QSet<QString>& displaced;
            QString guid(const core::Brick& b) const { return b.guid; }
            bool asDrawn(const core::Brick& b, const Thing<core::Brick>& t) const {
                return sameBrick(t.model, b) && !displaced.contains(b.guid);
            }
            QList<QGraphicsItem*> make(const core::Brick& b) {
                QGraphicsItem* item = sb.makeBrickItem(b, BL, li, resolvedKeys);
                sink.add(item);
                if (opacity < 1.0) item->setOpacity(opacity);
                sb.brickByGuid_.insert(b.guid, item);
                sb.brickCentreByGuid_.insert(b.guid, sb.brickCentre(b));
                return { item };
            }
            core::Brick store(const core::Brick& b) const { return b; }
            bool inPlace(Thing<core::Brick>& t, const core::Brick& b) {
                if (t.items.isEmpty() || !sb.redrawBrickInPlace(t.items.front(), b, BL)) return false;
                sb.brickCentreByGuid_.insert(b.guid, sb.brickCentre(b));
                return true;
            }
            void replaced(const core::Brick& was, const core::Brick& now) {
                if (was.myGroupId != now.myGroupId) regrouped = true;
            }
            void added(const core::Brick& b, Thing<core::Brick>&) {
                change.partsAdded.insert(b.guid);
                if (!b.myGroupId.isEmpty()) regrouped = true;
            }
            void erasing(Thing<core::Brick>& t, bool redraw) {
                for (auto* item : t.items) sb.takeLabelsOff(item);
                if (redraw) return;
                change.partsRemoved.insert(t.model.guid);
                // (Moved to another sheet drawn already: that one stays.)
                if (!t.items.isEmpty() && sb.brickByGuid_.value(t.model.guid) == t.items.front()) {
                    sb.brickByGuid_.remove(t.model.guid);
                    sb.brickCentreByGuid_.remove(t.model.guid);
                }
                if (!t.model.myGroupId.isEmpty()) regrouped = true;
            }
        } hooks{ *this, BL, li, sink, opacity, resolvedKeys, change, regrouped, displaced };
        QSet<QGraphicsItem*> gone;
        if (!diffThings(d, d.bricks, BL.bricks, hooks, change, gone)) {
            d.items.removeIf([&gone](QGraphicsItem* it) { return gone.contains(it); });
            redrawLayer(BL, li, change);
            continue;
        }
        if (!gone.isEmpty()) d.items.removeIf([&gone](QGraphicsItem* it) { return gone.contains(it); });
        if (BL.groups != d.groups) {
            d.groups = BL.groups;
            regrouped = true;
        }
        if (regrouped) change.sheetsRegrouped.insert(li);
    }
    const bool partsChanged = change.redrawn > 0 || change.erased > 0;
    change.removed += labelsTakenOff_;
    labelsTakenOff_.clear();

    for (size_t i = 0; i < layers.size(); ++i) {
        const core::Layer& L = *layers[i];
        const int li = static_cast<int>(i);
        LayerDrawn& d = layers_[i];
        const double opacity = std::clamp(L.transparency, 0, 100) / 100.0;
        QSet<QGraphicsItem*> gone;
        if (L.kind() == core::LayerKind::Text) {
            struct Hooks {
                SceneBuilder& sb;
                LayerDrawn& d;
                int li;
                double opacity;
                const QSet<QString>& displaced;
                QString guid(const core::TextCell& c) const { return c.guid; }
                bool asDrawn(const core::TextCell& c, const Thing<core::TextCell>& t) const {
                    return t.model == c && !displaced.contains(c.guid);
                }
                QList<QGraphicsItem*> make(const core::TextCell& c) {
                    QList<QGraphicsItem*> items;
                    LayerSink one{ sb.scene_, items, d.baseZ, d.shown };
                    addTextCell(c, one, li);
                    for (auto* it : items)
                        if (opacity < 1.0) it->setOpacity(opacity);
                    d.items += items;
                    return items;
                }
                core::TextCell store(const core::TextCell& c) const { return c; }
                bool inPlace(Thing<core::TextCell>&, const core::TextCell&) { return false; }
                void replaced(const core::TextCell&, const core::TextCell&) {}
                void added(const core::TextCell&, Thing<core::TextCell>&) {}
                void erasing(Thing<core::TextCell>&, bool) {}
            } hooks{ *this, d, li, opacity, displaced };
            if (!diffThings(d, d.texts, static_cast<const core::LayerText&>(L).textCells, hooks, change, gone)) {
                d.items.removeIf([&gone](QGraphicsItem* it) { return gone.contains(it); });
                redrawLayer(L, li, change);
                continue;
            }
        } else if (L.kind() == core::LayerKind::Ruler) {
            struct Hooks {
                SceneBuilder& sb;
                LayerDrawn& d;
                int li;
                double opacity;
                const QSet<QString>& displaced;
                QString guid(const core::LayerRuler::AnyRuler& r) const { return rulerGuid(r); }
                bool asDrawn(const core::LayerRuler::AnyRuler& r, const Thing<RulerDrawn>& t) const {
                    const auto ends = rulerEnds(r, sb.brickCentreByGuid_);
                    return t.model.ruler == r && t.model.end1 == ends.first && t.model.end2 == ends.second
                           && !displaced.contains(rulerGuid(r));
                }
                QList<QGraphicsItem*> make(const core::LayerRuler::AnyRuler& r) {
                    const auto ends = rulerEnds(r, sb.brickCentreByGuid_);
                    QList<QGraphicsItem*> items;
                    LayerSink one{ sb.scene_, items, d.baseZ, d.shown };
                    addRuler(r, one, li, ends.first, ends.second);
                    for (auto* it : items)
                        if (opacity < 1.0) it->setOpacity(opacity);
                    d.items += items;
                    return items;
                }
                RulerDrawn store(const core::LayerRuler::AnyRuler& r) const {
                    const auto ends = rulerEnds(r, sb.brickCentreByGuid_);
                    return { r, ends.first, ends.second };
                }
                void replaced(const RulerDrawn&, const core::LayerRuler::AnyRuler&) {}
                bool inPlace(Thing<RulerDrawn>&, const core::LayerRuler::AnyRuler&) { return false; }
                void added(const core::LayerRuler::AnyRuler&, Thing<RulerDrawn>&) {}
                void erasing(Thing<RulerDrawn>&, bool) {}
            } hooks{ *this, d, li, opacity, displaced };
            if (!diffThings(d, d.rulers, static_cast<const core::LayerRuler&>(L).rulers, hooks, change, gone)) {
                d.items.removeIf([&gone](QGraphicsItem* it) { return gone.contains(it); });
                redrawLayer(L, li, change);
                continue;
            }
        } else if (L.kind() == core::LayerKind::Area) {
            const auto& A = static_cast<const core::LayerArea&>(L);
            const double sizePx = studToPx(A.areaCellSizeInStud);
            LayerSink sink{ scene_, d.items, d.baseZ, d.shown };
            QSet<QPoint> seen;
            seen.reserve(static_cast<qsizetype>(A.cells.size()));
            bool twice = false;
            for (const auto& cell : A.cells) {
                const QPoint at(cell.x, cell.y);
                if (seen.contains(at)) {
                    twice = true;
                    break;
                }
                seen.insert(at);
            }
            if (twice || seen.size() < static_cast<qsizetype>(A.cells.size())) {
                redrawLayer(L, li, change);
                continue;
            }
            for (const auto& cell : A.cells) {
                const QPoint at(cell.x, cell.y);
                const QRgb rgba = cell.color.rgba();
                auto found = d.cells.find(at);
                if (found == d.cells.end()) {
                    QGraphicsItem* item = addAreaCell(cell, sizePx, A.transparency, sink);
                    d.cells.insert(at, { item, rgba });
                    change.added << item;
                    ++change.redrawn;
                    ++drawnTotal_;
                } else if (found->second != rgba) {
                    static_cast<QGraphicsRectItem*>(found->first)
                        ->setBrush(QBrush(areaCellColor(cell.color, A.transparency)));
                    found->second = rgba;
                    ++change.redrawn;
                    ++drawnTotal_;
                }
            }
            if (d.cells.size() > seen.size()) {
                for (auto it = d.cells.begin(); it != d.cells.end();) {
                    if (seen.contains(it.key())) {
                        ++it;
                        continue;
                    }
                    change.removed << it->first;
                    gone.insert(it->first);
                    scene_.removeItem(it->first);
                    delete it->first;
                    ++change.erased;
                    it = d.cells.erase(it);
                }
            }
        }
        if (!gone.isEmpty()) d.items.removeIf([&gone](QGraphicsItem* it) { return gone.contains(it); });
    }

    // Sheets shown or hidden: module frames and circuits leave out hidden ones.
    bool visibilityChanged = false;
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers_[i].visible != layers[i]->visible) {
            layers_[i].visible = layers[i]->visible;
            visibilityChanged = true;
        }

    if (map.sidecar.venue != drawnVenue_) {
        change.removed += venueItems_;
        clearVenue();
        addVenue(map);
        drawnVenue_ = map.sidecar.venue;
        change.added += venueItems_;  // (Edit module makes them out of reach)
        change.overlays = true;
    }

    // Labels: drawn again when they changed, when a part with labels was
    // drawn again or one they name came or went, or one was moved by Qt.
    bool labelsAgain = labelsDirty_ || map.sidecar.anchoredLabels != drawnLabels_;
    if (!labelsAgain && !drawnLabels_.empty()) {
        for (const auto& lbl : drawnLabels_)
            if (lbl.kind == core::AnchorKind::Brick
                && (change.partsAdded.contains(lbl.targetId) || change.partsRemoved.contains(lbl.targetId))) {
                labelsAgain = true;
                break;
            }
        if (labelsAgain == false && labelItems_.size() == static_cast<qsizetype>(drawnLabels_.size()))
            for (qsizetype k = 0; k < labelItems_.size(); ++k)
                if (labelItems_[k]->pos() != drawnLabels_[static_cast<size_t>(k)].offset * kPx) {
                    labelsAgain = true;
                    break;
                }
    }
    if (labelsAgain) {
        QSet<QString> picked;
        for (auto* it : labelItems_) {
            change.removed << it;
            if (it->isSelected()) picked.insert(it->data(kBrickDataGuid).toString());
        }
        change.erased += static_cast<int>(labelItems_.size());
        clearLabels();
        addAnchoredLabels(map);
        drawnLabels_ = map.sidecar.anchoredLabels;
        for (auto* it : labelItems_) {
            change.added << it;
            it->setVisible(labelsShown_);
            if (picked.contains(it->data(kBrickDataGuid).toString())) it->setSelected(true);
        }
        change.redrawn += static_cast<int>(labelItems_.size());
        change.overlays = true;
    }

    // Module frames and names, and electric circuits, are drawn from where
    // every part is: again whenever parts changed (as at the end of a drag,
    // which drew them where the parts were being dragged).
    if ((!map.sidecar.modules.empty() || !moduleLabelItems_.isEmpty())
        && (partsChanged || visibilityChanged || map.sidecar.modules != drawnModules_)) {
        refreshModuleLabels(map);
        change.overlays = true;
    }
    if ((partsChanged || visibilityChanged)
        && (!electricItems_.isEmpty() || partLook_.electric)) {
        clearElectric();
        addElectricCircuits(map);
        change.overlays = true;
    }
    return change;
}

}
