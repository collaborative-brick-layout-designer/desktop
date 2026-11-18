#include "SelectionOverlay.h"

#include "SelectionStyle.h"

#include "../rendering/MapText.h"
#include "../rendering/ModuleLabels.h"

#include <QBrush>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace bld::ui {

SelectionOverlay::SelectionOverlay() {
    setZValue(1e9);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable,    false);
    // Pure visual: never respond to clicks. Without this, the overlay's
    // enormous boundingRect can intercept itemAt() hit-tests and steal
    // the grab away from the brick the user actually clicked.
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    // Its children (the glowing copies) are drawn with it and left out of
    // the scene's index (setGlowing says why). Its shape is empty, so
    // they're never hit by a click either.
    setFlag(QGraphicsItem::ItemContainsChildrenInShape, true);
}

namespace {

// A copy of `source` (text or a label) that glows, or null for another kind.
QGraphicsItem* glowCopyOf(const QGraphicsItem* source, QGraphicsItem* parent) {
    QGraphicsItem* copy = nullptr;
    if (source->type() == QGraphicsPathItem::Type) copy = new QGraphicsPathItem(parent);
    else if (source->type() == QGraphicsSimpleTextItem::Type) copy = new QGraphicsSimpleTextItem(parent);
    if (!copy) return nullptr;
    copy->setAcceptedMouseButtons(Qt::NoButton);
    copy->setAcceptHoverEvents(false);
    auto* effect = new QGraphicsDropShadowEffect();
    effect->setColor(selection::kTextGlow);
    effect->setBlurRadius(selection::kTextGlowBlur);
    effect->setOffset(0, 0);
    copy->setGraphicsEffect(effect);
    return copy;
}

// Makes `copy` look like `source`, where it is; true if anything changed.
bool matchGlowCopy(QGraphicsItem* copy, const QGraphicsItem* source) {
    bool changed = false;
    const auto set = [&changed](auto now, auto want, auto apply) {
        if (now == want) return;
        apply(want);
        changed = true;
    };
    set(copy->transform(), source->sceneTransform(), [copy](const QTransform& t) { copy->setTransform(t); });
    set(copy->opacity(), source->effectiveOpacity(), [copy](qreal o) { copy->setOpacity(o); });
    set(copy->isVisible(), source->isVisible(), [copy](bool v) { copy->setVisible(v); });
    if (source->type() == QGraphicsPathItem::Type) {
        auto* to = static_cast<QGraphicsPathItem*>(copy);
        const auto* from = static_cast<const QGraphicsPathItem*>(source);
        set(to->path(), from->path(), [to](const QPainterPath& v) { to->setPath(v); });
        set(to->pen(), from->pen(), [to](const QPen& v) { to->setPen(v); });
        set(to->brush(), from->brush(), [to](const QBrush& v) { to->setBrush(v); });
    } else {
        auto* to = static_cast<QGraphicsSimpleTextItem*>(copy);
        const auto* from = static_cast<const QGraphicsSimpleTextItem*>(source);
        set(to->text(), from->text(), [to](const QString& v) { to->setText(v); });
        set(to->font(), from->font(), [to](const QFont& v) { to->setFont(v); });
        set(to->pen(), from->pen(), [to](const QPen& v) { to->setPen(v); });
        set(to->brush(), from->brush(), [to](const QBrush& v) { to->setBrush(v); });
    }
    return changed;
}

}  // namespace

void SelectionOverlay::setGlowing(const QList<QGraphicsItem*>& items) {
    bool changed = false;
    QHash<const QGraphicsItem*, QGraphicsItem*> kept;
    for (const QGraphicsItem* source : items) {
        if (!source || kept.contains(source)) continue;
        QGraphicsItem* copy = glows_.take(source);
        // (A new item at a gone one's address may be another kind.)
        if (copy && copy->type() != source->type()) {
            delete copy;
            copy = nullptr;
        }
        if (!copy) {
            copy = glowCopyOf(source, this);
            if (!copy) continue;
            changed = true;
        }
        changed = matchGlowCopy(copy, source) || changed;
        kept.insert(source, copy);
    }
    // The rest are no longer picked (or gone).
    for (QGraphicsItem* copy : std::as_const(glows_)) delete copy;
    if (!glows_.isEmpty()) changed = true;
    glows_ = std::move(kept);
    if (changed) refit();
}


void SelectionOverlay::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    if (polys_.isEmpty() && bands_.isEmpty() && !snapActive_ && !moving_ && names_.isEmpty()) return;
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    using namespace selection;

    for (const RulerBand& b : bands_) {
        QPen band(kRulerHalo);
        band.setWidthF(b.width);
        band.setCapStyle(Qt::FlatCap);
        p->setPen(band);
        p->setBrush(Qt::NoBrush);
        if (b.circle) p->drawEllipse(b.centre, b.radius, b.radius);
        else p->drawLine(b.line);
    }

    const QColor inColor = snapActive_ ? kSnapStroke : kPartTint;
    QColor fillColor = snapActive_ ? kSnapFill : kPartTint;
    if (!snapActive_) fillColor.setAlpha(kPartFillAlpha);
    QPen outer(kPartOuter); outer.setWidthF(kPartOuterWidth); outer.setCosmetic(true);
    outer.setJoinStyle(Qt::MiterJoin);
    QPen inner(inColor); inner.setWidthF(kPartInnerWidth); inner.setCosmetic(true);
    inner.setJoinStyle(Qt::MiterJoin);
    const QBrush fill(fillColor);

    for (const QPolygonF& poly : polys_) {
        p->setPen(outer); p->setBrush(Qt::NoBrush); p->drawPolygon(poly);
        p->setPen(inner); p->setBrush(fill);        p->drawPolygon(poly);
    }

    // Screen-sized marks: radii are divided by the painter's zoom, pens are cosmetic.
    const QTransform& wt = p->worldTransform();
    const double scale = std::max(1e-6, std::hypot(wt.m11(), wt.m12()));
    if (snapActive_) {
        using namespace snapmarks;
        QPen halo(kHalo); halo.setWidthF(kHaloWidth); halo.setCosmetic(true);
        p->setPen(halo);
        p->setBrush(Qt::NoBrush);
        const double hr = (kRingRadius + kRingWidth) / scale;
        p->drawEllipse(snapPoint_, hr, hr);
        QPen ring(kRing); ring.setWidthF(kRingWidth); ring.setCosmetic(true);
        p->setPen(ring);
        p->setBrush(kRingFill);
        p->drawEllipse(snapPoint_, kRingRadius / scale, kRingRadius / scale);
    }
    if (moving_) {
        using namespace snapmarks;
        QPen halo(kHalo); halo.setWidthF(kDotHaloWidth); halo.setCosmetic(true);
        p->setPen(halo);
        p->setBrush(kDot);
        p->drawEllipse(*moving_, kDotRadius / scale, kDotRadius / scale);
    }

    for (const FullName& n : names_) {
        p->save();
        p->setTransform(n.toScene, true);
        p->setPen(Qt::NoPen);
        p->setBrush(rendering::kModuleFullNameBackground);
        p->drawRoundedRect(n.box, n.box.height() / 2, n.box.height() / 2);
        p->setBrush(n.fill);
        p->drawPath(rendering::textPath(n.font, { { n.name, n.textTopLeft.x(), n.textTopLeft.y() } },
                                        rendering::kModuleNameLineHeight));
        p->restore();
    }
    p->restore();
}

void SelectionOverlay::setFullNames(QList<FullName> names) {
    if (names == names_) return;
    names_ = std::move(names);
    refit();
}

void SelectionOverlay::setRulerBands(QList<RulerBand> bands) {
    if (bands == bands_) return;
    bands_ = std::move(bands);
    refit();
}

void SelectionOverlay::setOutlines(QList<QPolygonF> polys) {
    if (polys == polys_) return;
    polys_ = std::move(polys);
    refit();
}

void SelectionOverlay::refit() {
    prepareGeometryChange();
    QRectF total;
    for (const QPolygonF& poly : polys_) {
        total = total.united(poly.boundingRect());
    }
    for (const RulerBand& b : bands_) {
        const QRectF r = b.circle ? QRectF(b.centre - QPointF(b.radius, b.radius), QSizeF(2 * b.radius, 2 * b.radius))
                                  : QRectF(b.line.p1(), b.line.p2()).normalized();
        total = total.united(r.adjusted(-b.width, -b.width, b.width, b.width));
    }
    total = total.united(marksRect());
    for (const FullName& n : names_) total = total.united(n.toScene.mapRect(n.box));
    // The glow reaches its blur (screen px) past each copy.
    const double glow = (selection::kTextGlowBlur + 2.0) / viewScale_;
    for (const QGraphicsItem* copy : std::as_const(glows_))
        total = total.united(copy->sceneBoundingRect().adjusted(-glow, -glow, glow, glow));
    bounds_ = total.isEmpty() ? QRectF() : total.adjusted(-6, -6, 6, 6);
    update();
}

void SelectionOverlay::setSnapState(bool active, QPointF snapPoint, std::optional<QPointF> moving, double viewScale) {
    viewScale = viewScale > 0 ? viewScale : 1.0;
    if (active == snapActive_ && snapPoint == snapPoint_ && moving == moving_ && viewScale == viewScale_) return;
    snapActive_ = active;
    snapPoint_  = snapPoint;
    moving_     = moving;
    viewScale_  = viewScale;
    refit();  // bounds take the marks in
}

QRectF SelectionOverlay::marksRect() const {
    using namespace selection::snapmarks;
    const double r = (kRingRadius + kRingWidth + kHaloWidth + 2.0) / viewScale_;
    QRectF out;
    if (snapActive_) out = QRectF(snapPoint_ - QPointF(r, r), QSizeF(2 * r, 2 * r));
    if (moving_) out = out.united(QRectF(*moving_ - QPointF(r, r), QSizeF(2 * r, 2 * r)));
    return out;
}

}  // namespace bld::ui
