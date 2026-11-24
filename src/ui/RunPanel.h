#pragma once

// The Run panel (trains design 5, 6.3): in the map's bottom-left corner
// while Run mode is on. Play and Pause, Restart, how fast to watch (¼× to
// 4×), which trains run and on which route, how each is getting on, what
// happened (a switch pushed through, two trains that would collide) with
// Show me, and the layout's scale. Running is this person's own: the
// trains move on this screen only. The web's editor/RunPanel.tsx is the twin.

#include "../track/Animation.h"

#include <QFrame>
#include <QHash>
#include <QPointer>
#include <QSet>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace bld::core { class Map; }

namespace bld::ui {

// Where a train runs: a route's id, or empty to follow the switches from where it stands.
struct RunPick {
    bool on = true;
    QString route;
    bool operator==(const RunPick&) const = default;
};

// A note the panel lists (newest first), with an id so rows stay put.
struct ShownNote {
    int id = 0;
    track::RunNote note;
};

// What the panel shows: the run's state as MapView has it.
struct RunView {
    bool playing = false;
    bool hasRun = false;
    double multiplier = 1.0;
    bool realSpeed = false;
    int scale = track::kDefaultScale;
    bool readOnly = false;
    QHash<QString, std::optional<RunPick>> picks;  // none: it can't run
    QHash<QString, track::RunState> states;        // the running trains'
    QSet<QString> onTrack;                         // trains standing on the track
    QSet<QString> brokenRoutes;
    std::vector<ShownNote> notes;
};

class RunPanel : public QFrame {
    Q_OBJECT
public:
    explicit RunPanel(QWidget* parent = nullptr);
    void show(const core::Map& map, const RunView& view);

    // The plain sentence for a note.
    static QString noteText(const track::RunNote& n, const core::Map& map, const QHash<QString, std::optional<RunPick>>& picks);

    // Its parts (tests).
    QPushButton* playButton() const { return play_; }
    QPushButton* restartButton() const { return restart_; }
    QComboBox* multiplierBox() const { return mult_; }
    QSpinBox* scaleBox() const { return scale_; }
    QCheckBox* realSpeedBox() const { return real_; }
    QWidget* trainRow(const QString& trainId) const { return rows_.value(trainId); }
    QList<QWidget*> noteRows() const { return noteRows_; }

signals:
    void playToggled(bool play);
    void restartRequested();
    void multiplierChanged(double m);
    void realSpeedChanged(bool on);
    void pickChanged(const QString& trainId, const RunPick& pick);
    void scaleChanged(int scale);
    void showSpotRequested(QPointF studs);
    void openTrainsRequested();

private:
    QPushButton* play_ = nullptr;
    QPushButton* restart_ = nullptr;
    QComboBox* mult_ = nullptr;
    QToolButton* fold_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* trainsBox_ = nullptr;
    QVBoxLayout* notesBox_ = nullptr;
    QLabel* empty_ = nullptr;
    QPushButton* openTrains_ = nullptr;
    QLabel* finished_ = nullptr;
    QSpinBox* scale_ = nullptr;
    QCheckBox* real_ = nullptr;
    QHash<QString, QWidget*> rows_;
    QList<QWidget*> noteRows_;
    bool playing_ = false;
    bool filling_ = false;
};

}  // namespace bld::ui
