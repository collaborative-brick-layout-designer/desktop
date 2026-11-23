#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "../core/AnchoredLabel.h"
#include "../core/Brick.h"
#include "../core/Group.h"
#include "../core/LayerRuler.h"
#include "../core/Module.h"
#include "../core/Route.h"
#include "../core/Train.h"
#include "../core/TextCell.h"
#include "../core/Venue.h"
#include "../track/Trains.h"
#include "../edit/venue/VenueUnits.h"
#include "ModuleLabels.h"

#include <QColor>
#include <QMap>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsScene;

namespace bld::core { class Layer; class Map; }
namespace bld::parts {
class PartsLibrary;
struct PartMetadata;
} // namespace bld::parts

namespace bld::rendering {

// Populates a QGraphicsScene with items for each layer of a core::Map.
// Scene units: 1 stud = 8 pixels (matches BlueBrickParts GIF sampling).
//
// Items are added directly to the scene (no QGraphicsItemGroup wrapper) so
// hit-testing / selection never gets intercepted by a parent group. We
// track per-layer items in a QHash<int, QList<QGraphicsItem*>> for
// visibility toggling.
// A painted area cell's color: its RGB at the sheet's alpha, as vanilla
// BlueBrick and the web draw it (render-parity/areas.json).
QColor areaCellColor(const QColor& cell, int transparency);

class SceneBuilder {
public:
    static constexpr int kPixelsPerStud = 8;
    // A ruler's selectable item carries its line (QLineF, the offset line)
    // or circle (QRectF) in scene px, and its line thickness, for the
    // selection band.
    static constexpr int kRulerBandRole = 10;
    // A brick item's part key (the library key it was drawn from).
    static constexpr int kBrickPartRole = 12;
    static constexpr int kRulerThicknessRole = 11;

    SceneBuilder(QGraphicsScene& scene, parts::PartsLibrary& parts);

    void build(const core::Map& map);
    void clear();

    // What update() changed in the scene.
    struct Change {
        // Items deleted (their pointers are only good as keys now) and made:
        // parts, text, ruler pieces, area cells and labels (a label on a part
        // is its child).
        QList<QGraphicsItem*> removed;
        QList<QGraphicsItem*> added;
        // Parts drawn now that weren't, and parts no longer drawn.
        QSet<QString> partsAdded;
        QSet<QString> partsRemoved;
        // Part sheets (by index) whose parts came, went or changed group,
        // or whose groups changed.
        QSet<int> sheetsRegrouped;
        // Things drawn again: a part, text cell, ruler, area cell or label
        // each count one (the perf tests count work, not time).
        int redrawn = 0;
        // Things taken away.
        int erased = 0;
        bool nothing() const { return redrawn == 0 && erased == 0 && !overlays; }
        // Module frames, circuits, the venue or labels were drawn again.
        bool overlays = false;
    };
    // Draws again only what differs between `map` and what was drawn last:
    // the parts, text, rulers and area cells that changed, came or went,
    // and what is drawn from them (module frames, circuits, labels). An
    // item Qt moved (a drag) without the layout changing is put back too.
    // Unset, with nothing done, when the change is structural (sheets
    // added, removed or reordered, a sheet's look, a setting every part
    // reads, or nothing drawn yet): build() then.
    std::optional<Change> update(const core::Map& map);
    // Whether `brick` on sheet `layerIndex` is drawn as it is (update()
    // would leave it).
    bool drawnAsIs(int layerIndex, const core::Brick& brick) const;
    // Things (parts, text cells, rulers, area cells, labels) drawn since
    // this builder was made, by build() and update() (tests).
    quint64 drawnTotal() const { return drawnTotal_; }

    // Venue label size in scene px; unset reads Preferences (venue/labelPx).
    // The Venue Designer sets it from the zoom so labels read the same at any scale.
    void setVenueLabelPx(std::optional<double> px) { venueLabelPx_ = px; }
    // The venue is drawn on a dark canvas (the Venue Designer in the dark
    // theme): its dark colors are drawn light (venuedraw::onDarkCanvas).
    void setVenueDarkCanvas(bool dark) { venueDarkCanvas_ = dark; }
    // The Venue Designer: the selected wall keeps its whole label, and
    // labels keep clear of the selection handles (studs; half size in px).
    void setVenueSelection(std::optional<int> edge, QVector<QPointF> handles, double handleHalfPx) {
        venueSelectedEdge_ = edge;
        venueHandles_ = std::move(handles);
        venueHandleHalfPx_ = handleHalfPx;
    }

    // While parts are dragged: draw again what is drawn from the layout but
    // follows parts (module frames and names, electric circuits, and the
    // rulers fixed to `moving` parts), from `posed`, the layout with the
    // dragged parts where the drag has them now (MapView's live pose).
    // Ruler layers in `keepRulerLayers` (being dragged themselves) stay.
    void rebuildFollowers(const core::Map& posed, const QSet<QString>& moving,
                          const QSet<int>& keepRulerLayers);

    // Switches (trains design 6.3): an arrow on each switch along the way
    // it is set (the sidecar's switchStates), the other ways faint. Shown in
    // Run mode, and while building with View > Switches. Drawn again by
    // update() when parts or settings change; refreshSwitches() after a
    // change of setShowSwitches.
    void setShowSwitches(bool on) { switchesShown_ = on; }
    bool showSwitches() const { return switchesShown_; }
    void refreshSwitches(const core::Map& map);
    // The switch arrows drawn, by switch key (tests): the arrow item.
    const QHash<QString, QGraphicsItem*>& switchArrows() const { return switchArrowByKey_; }
    // The arrow's color: the usual way, and set the other way.
    static QColor switchSetColor();
    static QColor switchTurnedColor();

    // Named routes (trains design 6.2): each route not hidden on this
    // person's map, as a wide line in its color along the track; a broken
    // one dashed red as far as it goes; a ring on each switch it needs that
    // is set the other way. The route being drawn: dashed, ends marked.
    void setHiddenRoutes(const QSet<QString>& ids) { hiddenRoutes_ = ids; }
    struct RouteDraftLook {
        std::vector<std::vector<QPointF>> lines;  // studs
        std::vector<QPointF> ends;                // studs
    };
    void setRouteDraft(std::optional<RouteDraftLook> d) { routeDraft_ = std::move(d); }
    void refreshRoutes(const core::Map& map);
    // The routes drawn, by id: their line items (tests).
    const QHash<QString, QList<QGraphicsItem*>>& routeItems() const { return routeItemsById_; }
    static QColor routeBrokenColor();

    // Trains on the track (trains design 3.3, 6.4): each car's body on the
    // chord between its trucks, a box in its color with a tick at each
    // truck (or its part's picture), the head car's front pointed, numbers
    // on. The train being put on the track (or picked) is outlined.
    void setPickedTrain(const QString& id) { pickedTrain_ = id; }
    void refreshTrains(const core::Map& map);
    // Each train drawn, by id: its cars' body items, front first (tests).
    const QHash<QString, QList<QGraphicsItem*>>& trainItems() const { return trainCarsById_; }
    static QColor trainProblemColor();
    // Running trains (trains design 5): the trains in `ids` aren't drawn
    // standing; showRunFrame draws them where the run has them (moving the
    // same items each frame) and a red ring at each spot two would collide.
    void setRunningTrains(const QSet<QString>& ids, const core::Map& map);
    void showRunFrame(const core::Map& map, const std::vector<std::pair<QString, std::optional<track::PlacedTrain>>>& placed,
                      const std::vector<QPointF>& marks);
    void clearRun();
    // The running trains drawn, by id: their cars' body items (tests).
    const QHash<QString, QList<QGraphicsItem*>>& runItems() const { return runCarsById_; }
    int collisionMarks() const { return static_cast<int>(runMarks_.size()); }
    // The switch arrows show these settings, not the layout's (a run's pushes); none: the layout's.
    void setSwitchStatesShown(std::optional<QMap<QString, int>> states) { switchStatesShown_ = std::move(states); }

    // Fit checks on the map (trains design 3.5, 6.2): with View > Siding
    // Lengths, each run of track labeled with the length a train can stand
    // in; a ring at each spot a check is about (red: a problem, amber: a
    // warning); and "Show me"'s spot, ringed.
    void setShowSidingLengths(bool on) { showSidingLengths_ = on; }
    void setSpotlight(std::optional<QPointF> studs) { spotlight_ = studs; }
    void refreshFit(const core::Map& map);
    // What is drawn (tests): the siding labels, the check rings, the spotlight.
    int sidingLabelCount() const { return sidingLabels_; }
    int checkRingCount() const { return checkRings_; }
    bool spotlightShown() const { return spotlightShown_; }

    // Draw every module's frame and name again from `map` (a sheet shown or
    // hidden changes which parts frame a module, and how its frame is dashed).
    void refreshModuleLabels(const core::Map& map);
    // What the badge over each shared (read-only) module says, by module id
    // ("🔗 Alice ● v8 is ready": the app knows which have a version
    // waiting). One not named here says "🔗 <its owner>". Takes effect at
    // the next refreshModuleLabels.
    void setSharedBadges(const QHash<QString, QString>& badges) { sharedBadges_ = badges; }

    // Toggle the visibility of a layer (by index). Returns false if out of range.
    bool setLayerVisible(int layerIndex, bool visible);

    // Every top-level item a sheet drew (empty for an index out of range).
    QList<QGraphicsItem*> layerItems(int layerIndex) const {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) return {};
        return layers_[static_cast<size_t>(layerIndex)].items;
    }

    // Show or hide every anchored label (World, Brick, Group and Module
    // labels), for a saved view with its labels off.
    void setLabelsVisible(bool visible);

    // Each module's frame and name as drawn (View > Module names), in
    // scene pixels, by module id. The name usually sits outside the
    // frame. Empty when module names are off.
    const QList<QPair<QString, QRectF>>& moduleAnnotationRects() const { return moduleAnnotationRects_; }

    // Moves on whenever parts', rulers', text's or labels' items are made
    // or deleted wholesale (build, clear, rulers following a drag): what
    // MapView's pick index was built from is gone then. update() leaves it:
    // its Change says which items came and went.
    quint64 generation() const { return generation_; }

    // Module names cut short to fit (the whole name shows on hover, as a
    // tooltip, and while the module is selected, MapView's pill).
    struct ShortenedModuleName {
        QString id;
        QString name;
        ModuleLabelLayout at;
        QColor fill;
    };
    const QList<ShortenedModuleName>& shortenedModuleNames() const { return shortenedModuleNames_; }

private:
    std::optional<double> venueLabelPx_;
    bool venueDarkCanvas_ = false;
    std::optional<int> venueSelectedEdge_;
    QVector<QPointF> venueHandles_;
    double venueHandleHalfPx_ = 0;
    void addLayer(const core::Layer& layer, int layerIndex);
    void clearLayer(int layerIndex);

    // What each sheet was drawn from, so update() redraws only what differs:
    // a thing (part, text cell, ruler) by guid, with its items, its place in
    // the sheet's z order and what it was drawn from.
    template <class Stored> struct Thing {
        QList<QGraphicsItem*> items;
        double z = 0;
        quint64 seen = 0; // the update that last found it on the sheet
        Stored model;
    };
    // A ruler and where its ends were (they follow parts).
    struct RulerDrawn {
        core::LayerRuler::AnyRuler ruler;
        QPointF end1, end2;
    };
    struct LayerDrawn {
        bool present = false;
        core::LayerKind kind = core::LayerKind::Grid;
        QString guid;
        int transparency = 100;
        bool hullsShown = false;
        QRgb hullColor = 0;
        int hullThickness = 1;
        int areaCellSize = 0;
        bool visible = true; // the sheet's Visible when drawn
        bool shown = true;   // whether its items show now (setLayerVisible)
        double baseZ = 0;
        QList<QGraphicsItem*> items; // every item of the sheet (visibility, clear)
        QHash<QString, Thing<core::Brick>> bricks;
        QHash<QString, Thing<core::TextCell>> texts;
        QHash<QString, Thing<RulerDrawn>> rulers;
        // An area cell's item and color.
        QHash<QPoint, QPair<QGraphicsItem*, QRgb>> cells;
        std::vector<core::Group> groups;
    };
    // Draws again the things on a sheet that differ from `models` (`Hooks`
    // knows how to tell and to draw one); false when two have one guid.
    template <class Model, class Stored, class Hooks>
    bool diffThings(LayerDrawn& d, QHash<QString, Thing<Stored>>& things, const std::vector<Model>& models,
                    Hooks& hooks, Change& change, QSet<QGraphicsItem*>& gone);
    // A sheet drawn again whole (two things with one guid).
    void redrawLayer(const core::Layer& layer, int layerIndex, Change& change);
    std::vector<LayerDrawn> layers_;
    // Settings every part reads (connection points, hulls, elevation).
    struct PartLook {
        bool alwaysShowConns = false;
        bool hulls = false;
        bool elevation = false;
        bool electric = false; // View > Electric circuits (shown or not, the parts look the same)
        bool operator==(const PartLook& o) const {
            return alwaysShowConns == o.alwaysShowConns && hulls == o.hulls && elevation == o.elevation;
        }
    };
    PartLook partLook_;
    PartLook readPartLook() const;
    QGraphicsItem* makeBrickItem(const core::Brick& brick, const core::Layer& layer, int layerIndex,
                                 QHash<QString, QString>& resolvedKeys);
    void decorateBrickItem(QGraphicsItem* item, const core::Brick& brick, const core::Layer& layer,
                           const parts::PartMetadata* meta);
    // Moves and turns a part's item to `brick` and draws its children
    // again; false when it must be made anew (another part, no picture).
    bool redrawBrickInPlace(QGraphicsItem* item, const core::Brick& brick, const core::Layer& layer);
    // Puts the sheet's things in z order (their order on the sheet): new
    // ones between their neighbours, or all again when the order changed.
    template <class Stored>
    void orderSheet(LayerDrawn& d, QHash<QString, Thing<Stored>>& things,
                    const std::function<QString(int)>& guidAt, const std::vector<double>& zs);
    // Deletes the labels on a part's item (they're drawn again after).
    void takeLabelsOff(QGraphicsItem* partItem);
    bool labelsDirty_ = false;
    QList<QGraphicsItem*> labelsTakenOff_; // deleted with their part this update
    quint64 epoch_ = 0;
    void clearLabels();
    void clearVenue();
    void clearModuleLabels();
    void clearElectric();
    void clearSwitches();
    void clearRoutes();
    void addRoutes(const core::Map& map);
    QSet<QString> hiddenRoutes_;
    std::optional<RouteDraftLook> routeDraft_;
    QList<QGraphicsItem*> routeItems_;
    QHash<QString, QList<QGraphicsItem*>> routeItemsById_;
    std::vector<core::Route> drawnRoutes_;
    QMap<QString, int> drawnRouteSwitchStates_;
    QMap<QString, int> drawnTrainSwitchStates_;
    void clearTrains();
    void addTrains(const core::Map& map);
    QGraphicsPathItem* makeCar(const core::Train& t, size_t i, const track::PlacedUnit& u, const core::RollingStock& s,
                               bool offTrack, bool picked);
    QSet<QString> runningTrains_;
    QHash<QString, QList<QGraphicsItem*>> runCarsById_;
    QList<QGraphicsItem*> runMarks_;
    std::vector<QPointF> runMarkSpots_;
    std::optional<QMap<QString, int>> switchStatesShown_;
    QString pickedTrain_;
    QList<QGraphicsItem*> trainItems_;
    QHash<QString, QList<QGraphicsItem*>> trainCarsById_;
    std::vector<core::RollingStock> drawnStock_;
    std::vector<core::Train> drawnTrains_;
    void clearFit();
    void addFit(const core::Map& map);
    bool showSidingLengths_ = false;
    std::optional<QPointF> spotlight_;
    QList<QGraphicsItem*> fitItems_;
    int sidingLabels_ = 0;
    int checkRings_ = 0;
    bool spotlightShown_ = false;
    std::vector<core::Route> drawnFitRoutes_;
    void addSwitches(const core::Map& map);
    bool switchesShown_ = false;
    QList<QGraphicsItem*> switchItems_;
    QHash<QString, QGraphicsItem*> switchArrowByKey_;
    QMap<QString, int> drawnSwitchStates_;
    QPointF brickCentre(const core::Brick& b) const;
    std::optional<core::Venue> drawnVenue_;
    std::vector<core::AnchoredLabel> drawnLabels_;
    std::vector<core::Module> drawnModules_;
    QList<QGraphicsItem*> labelItems_; // every label item, world ones and those on parts
    bool labelsShown_ = true;
    bool built_ = false;
    quint64 drawnTotal_ = 0;
    void addVenue(const core::Map& map);
    void addAnchoredLabels(const core::Map& map);
    // `posed`: parts are being dragged: names keep the places (and modules
    // the colors) they had on the settled layout, so nothing jumps.
    void addModuleLabels(const core::Map& map, bool posed = false);
    void addElectricCircuits(const core::Map& map);

    QGraphicsScene& scene_;
    parts::PartsLibrary& parts_;
    QHash<QString, QGraphicsItem*>    brickByGuid_;   // brick.guid -> QGraphicsItem*
    // brick.guid -> brick world centre in STUDS. Precomputed once per build
    // so ruler rendering (and any other cross-item feature) can resolve
    // attached-brick endpoints without walking the whole map.
    QHash<QString, QPointF>           brickCentreByGuid_;
    QList<QGraphicsItem*>             venueItems_;
    // Transient items for the Electric Circuits render overlay. Cleared +
    // rebuilt on every build().
    QList<QGraphicsItem*>             electricItems_;
    QList<QGraphicsItem*>             worldLabelItems_;
    QList<QGraphicsItem*>             moduleLabelItems_;
    QHash<QString, QString>           sharedBadges_;
    QList<QPair<QString, QRectF>>     moduleAnnotationRects_;
    QList<ShortenedModuleName>        shortenedModuleNames_;
    QHash<QString, QString>           settledNameSlots_;    // module id → slot, from the last settled build
    QHash<QString, QString>           settledModuleColors_; // module id → its default color then
    quint64                           generation_ = 0;
};

}
