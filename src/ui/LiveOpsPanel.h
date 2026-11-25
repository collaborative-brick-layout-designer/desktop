#pragma once

// The Live Ops panel (Live Ops design 6; the web's LiveOpsPanel.tsx): what
// each car does next. Cars: where each is and goes, its home, and Set aside.
// Waybills: each car's orders, by hand or made for every car at once. Jobs:
// one crew's work each, with its switch list, which follows every move. Cars
// that can't be routed are listed above, each with why. Every change goes
// out as a signal the main window turns into one undo step.

#include "../edit/OpsCommands.h"
#include "../track/OpsView.h"

#include <QDockWidget>
#include <QPointF>
#include <QString>

#include <functional>
#include <optional>

class QScrollArea;
class QVBoxLayout;

namespace bld::core { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class LiveOpsPanel : public QDockWidget {
    Q_OBJECT
public:
    enum class Tab { Cars, Waybills, Jobs };

    explicit LiveOpsPanel(parts::PartsLibrary& parts, QWidget* parent = nullptr);

    // The layout it is about (null: none). Call again after any change.
    void setMap(const core::Map* map);
    void setReadOnly(bool readOnly);
    void showTab(Tab tab);
    void openWaybill(const QString& id);
    void openJob(const QString& id);
    const track::OpsView& view() const { return view_; }
    const QString& openWaybillId() const { return waybill_; }
    const QString& openJobId() const { return job_; }
    // Asks before deleting (default: a Yes / No box).
    void setConfirm(std::function<bool(const QString& question)> f) { confirm_ = std::move(f); }
    // Draws waiting changes now (they are drawn once the event that made them is done).
    void drawNow();

    // What the controls do, callable directly.
    void addWaybill(const QString& car);
    void startMaking();
    void shuffle();
    void addMade();
    void delivered(const QString& car);
    void addJob();
    void removeOpen();

signals:
    void changed(const bld::edit::OpsChange& change, const QString& what);
    // Delivered to a place off the layout: the car leaves the track, and the change is made, as one step.
    void carLeaves(const QString& car, const bld::edit::OpsChange& change, const QString& what);
    void showSpotRequested(QPointF studs);
    void openPlacesRequested();
    void openTrainsRequested();

protected:
    void showEvent(QShowEvent* e) override;

private:
    void later();
    void rebuild();
    void build(QVBoxLayout* v);
    void buildCant(QVBoxLayout* v);
    void buildCars(QVBoxLayout* v);
    void buildWaybills(QVBoxLayout* v);
    void buildMaking(QVBoxLayout* v);
    void buildWaybill(QVBoxLayout* v, const track::Waybill& w);
    void buildJobs(QVBoxLayout* v);
    void buildJob(QVBoxLayout* v, const track::Job& j);
    void change(const edit::OpsChange& c, const QString& what);
    void editCar(const QString& car, const std::function<void(edit::CarOps&)>& f, const QString& what);

    parts::PartsLibrary& parts_;
    const core::Map* map_ = nullptr;
    track::OpsView view_;
    bool readOnly_ = false;
    bool waiting_ = false;
    bool making_ = false;
    quint32 seed_ = 0;
    Tab tab_ = Tab::Cars;
    QString waybill_;
    QString job_;
    QScrollArea* scroll_ = nullptr;
    std::function<bool(const QString&)> confirm_;
};

}  // namespace bld::ui
