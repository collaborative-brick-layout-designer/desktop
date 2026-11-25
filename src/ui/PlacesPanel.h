#pragma once

// The Places panel (Live Ops design 6; the web's PlacesPanel.tsx): every
// industry, yard, staging and interchange of the layout and its modules,
// with the cars each spot holds and Show me; New place picks its track on
// the map. A place opens to its name, kind, spots, what it takes in and
// sends out, tags and notes. A shared module's places are its owner's:
// shown, not changed. Every change goes out as a signal the main window
// turns into one undo step.

#include "../track/PlacesOnTrack.h"
#include "../track/TrackNetwork.h"

#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QScrollArea;
class QVBoxLayout;

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class PlacesPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit PlacesPanel(parts::PartsLibrary& parts, QWidget* parent = nullptr);

    // The layout whose places it lists (null: none). Call again after any change.
    void setMap(const core::Map* map);
    void setReadOnly(bool readOnly);
    // Whether track is being picked on the map (New place waits meanwhile).
    void setPicking(bool picking);
    // Only this module's places (its menu > Places…); empty: every place.
    void showModule(const QString& moduleId);
    // Open a place; `fresh`: just made, its name ready to type over.
    void openPlace(const QString& owner, const QString& id, bool fresh);
    const QString& openId() const { return openId_; }
    // Asks before deleting (default: a Yes / No box).
    void setConfirm(std::function<bool(const QString& name)> f) { confirm_ = std::move(f); }
    // Draws waiting changes now (they are drawn once the event that made them is done).
    void drawNow();

    // What the controls do, callable directly.
    void newPlace();
    void newPlaceOffLayout();
    void pickTrack(const QString& spotId);
    void addSpot() { pickTrack(QString()); }
    void removeOpen();

signals:
    // `after` unset: delete. `owner`: a module's id, empty for the layout's own.
    void placeEdited(const QString& owner, const QString& id, const std::optional<QJsonObject>& after, const QString& what);
    // Pick track on the map: `owner` unset for whichever the track is in, `place` empty for a new one, `spot` for a new one.
    void pickRequested(const std::optional<QString>& owner, const QString& place, const QString& spot);
    void showSpotRequested(QPointF studs);

private:
    struct Group {
        QString owner;
        QString name;
        bool locked = false;
        QJsonArray places;
    };
    std::vector<Group> groups() const;
    // The open place and its group; nullopt when none is open (any more).
    std::optional<std::pair<Group, QJsonObject>> open() const;
    void later();
    void rebuild();
    void buildList(QVBoxLayout* v);
    void buildEditor(QVBoxLayout* v, const Group& group, const QJsonObject& place);
    void change(const std::function<void(QJsonObject&)>& f, const QString& what);
    void changeSpot(const QString& spotId, const std::function<void(QJsonObject&)>& f, const QString& what);
    track::SpotNow now(const QJsonObject& spot) const;
    QStringList carTypes() const;

    parts::PartsLibrary& parts_;
    const core::Map* map_ = nullptr;
    track::TrackNetwork net_;
    std::vector<track::TrackRun> runs_;
    bool readOnly_ = false;
    bool picking_ = false;
    bool waiting_ = false;
    QString module_;
    QString openOwner_;
    QString openId_;
    bool fresh_ = false;
    QScrollArea* scroll_ = nullptr;
    std::function<bool(const QString&)> confirm_;
};

}  // namespace bld::ui
