#pragma once

// What Live Ops says (the web's apps/web/src/editor/ops.ts says the same).

#include "../edit/OpsCommands.h"
#include "../track/OpsView.h"

#include <QCoreApplication>
#include <QString>

namespace bld::ui {

class OpsText {
    Q_DECLARE_TR_FUNCTIONS(bld::ui::OpsText)
public:
    static QString gonePlace();
    static QString placeName(const track::OpsView& v, const QString& id);
    // "Cannery, Dock".
    static QString spotName(const track::OpsView& v, const QString& place, const QString& spot);
    static QString carName(const track::OpsView& v, const QString& id);
    static QString load(const QString& load);
    // "Loaded to Mill".
    static QString leg(const track::OpsView& v, const track::WaybillLeg& leg);
    // Where a car goes and who takes it.
    static QString next(const track::OpsView& v, const QString& car);
    // Why a car can't be routed.
    static QString cant(const track::OpsView& v, const track::CantRoute& c);
    static QString jobKind(const QString& kind);
    static QString jobKindHint(const QString& kind);
    static QString stops(int n);
    static QString movesText(int n);
    // What deleting places does to what names them (empty: nothing does).
    static QString uses(const edit::PlaceUses& u);
};

}  // namespace bld::ui
