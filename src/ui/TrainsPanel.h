#pragma once

// The Trains panel (trains design 6.4; the web's TrainsPanel.tsx): two
// tabs. Rolling stock is the layout's engines and cars: make a quick one,
// change it, bring one in from your roster or a club's, save one to a
// roster, or tick a few and make a train of them. Trains are cars in
// order: put one on the track with a click, turn it around, take it off.
// Every change goes out as a signal the main window turns into one undo step.

#include "../core/Train.h"
#include "../sync/LibraryApi.h"

#include <QDockWidget>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

class QLabel;
class QPushButton;
class QTabWidget;
class QVBoxLayout;

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class ServerLibrary;
class RosterDialog;

class TrainsPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit TrainsPanel(parts::PartsLibrary& parts, QWidget* parent = nullptr);

    // The layout whose cars and trains it lists (null: none). Call again after any change.
    void setMap(const core::Map* map);
    // The train being put on the track (empty: none).
    void setPlacing(const QString& trainId);
    // Where rosters come from (null, or not signed in: the roster buttons say so).
    void setServerLibrary(ServerLibrary* library);
    // May the layout be changed here (a view-only live layout can't).
    void setReadOnly(bool readOnly);

    // Dialogs, replaceable (tests): a car made or changed, a train's cars, and Yes / No.
    void setEditStock(std::function<std::optional<core::RollingStock>(const core::RollingStock&, bool isNew)> f) { editStock_ = std::move(f); }
    void setEditTrain(std::function<std::optional<core::Train>(const core::Train&)> f) { editTrain_ = std::move(f); }
    void setConfirm(std::function<bool(const QString& question)> f) { confirm_ = std::move(f); }

    // What the buttons do, callable directly.
    void newStock();
    void changeStock(const QString& id);
    bool removeStock(const QString& id);
    void setTicked(const QString& stockId, bool ticked);
    // A train of the ticked cars, in the order ticked; its id.
    QString makeTrainOfTicked();
    void newTrain();
    void changeTrain(const QString& id);
    void turnAround(const QString& id);
    void takeOff(const QString& id);
    bool removeTrain(const QString& id);
    // Rosters: add an entry to this layout (or bring its copy up to date), and save a car to one.
    void addFromRoster(const sync::RosterStock& entry);
    void saveToRoster(const QString& stockId, const QString& orgSlug);
    void openRoster();
    // The roster entries last read (by id), for "Update from roster".
    void setRoster(const QList<sync::RosterStock>& entries);

    QTabWidget* tabs() const { return tabs_; }
    QString message() const;

signals:
    // `after` unset: delete.
    void stockEdited(const QString& id, const std::optional<core::RollingStock>& after, const QString& what);
    void stockRemoved(const QString& id, const QString& what);
    void trainEdited(const QString& id, const std::optional<core::Train>& after, const QString& what);
    void placeRequested(const QString& trainId);
    void placeCancelled();

private:
    void rebuild();
    void say(const QString& text);
    const core::RollingStock* stockById(const QString& id) const;
    const core::Train* trainById(const QString& id) const;
    void fetchRoster(const std::function<void()>& then = {});
    void removeFromRoster(const sync::RosterStock& entry);
    sync::LibraryApi& api();
    sync::LibraryApi api_;
    parts::PartsLibrary& parts_;
    const core::Map* map_ = nullptr;
    ServerLibrary* library_ = nullptr;
    bool readOnly_ = false;
    QString placing_;
    QStringList ticked_;
    QHash<QString, sync::RosterStock> roster_;
    QTabWidget* tabs_ = nullptr;
    QVBoxLayout* trainList_ = nullptr;
    QVBoxLayout* stockList_ = nullptr;
    QLabel* trainsEmpty_ = nullptr;
    QLabel* stockEmpty_ = nullptr;
    QPushButton* newTrain_ = nullptr;
    QPushButton* newStock_ = nullptr;
    QPushButton* fromRoster_ = nullptr;
    QPushButton* makeTrain_ = nullptr;
    QLabel* message_ = nullptr;
    std::function<std::optional<core::RollingStock>(const core::RollingStock&, bool)> editStock_;
    std::function<std::optional<core::Train>(const core::Train&)> editTrain_;
    std::function<bool(const QString&)> confirm_;
};

}  // namespace bld::ui
