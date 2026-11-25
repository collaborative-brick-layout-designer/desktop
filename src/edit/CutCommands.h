#pragma once

// Cars on the track (Live Ops design 2.2, 2.3): what a drag, Put on the
// track, Take off the track and a car's load do, each one undo step. An
// undo puts back only what nobody has changed since: a cut a split made is
// left empty, not deleted, so a car someone else coupled there stays where
// it stands, and a car someone else moved stays moved. The web's
// apps/web/src/editor/cuts.ts does the same.

#include "../core/Map.h"
#include "../track/Cuts.h"

#include <QString>
#include <QUndoCommand>

namespace bld::edit {

class CutCommand : public QUndoCommand {
public:
    CutCommand(core::Map& map, const track::CutChange& change, const QString& text, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    struct TrainStep {
        core::Train before;
        core::Train after;
    };
    struct CarStep {
        QString id;
        QString before;
        QString after;
    };
    void apply(bool forward, bool force);
    core::Map& map_;
    std::vector<TrainStep> trains_;
    std::vector<CarStep> cars_;
    bool done_ = false;
};

// A car's load (empty: it doesn't apply).
class SetLoadCommand : public QUndoCommand {
public:
    SetLoadCommand(core::Map& map, const QString& car, const QString& load, const QString& text, QUndoCommand* parent = nullptr);
    void undo() override { set(after_, before_); }
    void redo() override;

private:
    void set(const QString& from, const QString& to);
    core::Map& map_;
    QString id_;
    QString before_;
    QString after_;
};

// A whole cut off the track: its cars stay in the layout.
track::CutChange takeCutOff(const core::Sidecar& sidecar, const QString& id);

}  // namespace bld::edit
