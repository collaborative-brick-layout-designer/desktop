#pragma once

// The Routes panel (trains design 6.2; the web's RoutesPanel.tsx): the
// layout's named routes, each with its length, a status (OK, switches set
// the other way, or broken) and an eye that shows or hides it on this
// person's map only. New route draws one on the track; each route can set
// its switches, find a new way when the track under it changed, be renamed
// or deleted (asked first). Every change goes out as a signal the main
// window turns into one undo step.

#include "../core/Route.h"

#include <QDockWidget>
#include <QMap>
#include <QPointF>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class RoutesPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit RoutesPanel(parts::PartsLibrary& parts, QWidget* parent = nullptr);

    // The layout whose routes it lists (null: none). Call again after any change.
    void setMap(const core::Map* map);
    // The routes this person hid on their map.
    void setHidden(const QSet<QString>& ids);
    const QSet<QString>& hidden() const { return hidden_; }
    // Whether a route is being drawn (New route waits meanwhile).
    void setDrawing(bool on);
    // Asks before deleting (default: a Yes / No box) and for a new name (default: an input box).
    void setConfirm(std::function<bool(const QString& name)> f) { confirm_ = std::move(f); }
    void setAskName(std::function<std::optional<QString>(const QString& name)> f) { askName_ = std::move(f); }

    // What the buttons do, callable directly.
    void setShown(const QString& id, bool shown);
    void setSwitches(const QString& id);
    bool findNewWay(const QString& id);
    void rename(const QString& id, const QString& name);
    bool remove(const QString& id);
    // The message under New route ("Switches set for ...").
    QString message() const;

signals:
    void newRouteRequested();
    // `after` unset: delete.
    void routeEdited(const QString& id, const std::optional<core::Route>& after, const QString& what);
    void switchesRequested(const QMap<QString, std::optional<int>>& changes, const QString& what);
    void hiddenChanged(const QSet<QString>& ids);
    // A route check's Show me: the spot on the map (studs).
    void showSpotRequested(QPointF studs);

private:
    void rebuild();
    const core::Route* find(const QString& id) const;
    parts::PartsLibrary& parts_;
    const core::Map* map_ = nullptr;
    QSet<QString> hidden_;
    QVBoxLayout* list_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QLabel* empty_ = nullptr;
    QLabel* message_ = nullptr;
    class FitChecksList* checks_ = nullptr;
    bool drawing_ = false;
    std::function<bool(const QString&)> confirm_;
    std::function<std::optional<QString>(const QString&)> askName_;
};

}  // namespace bld::ui
