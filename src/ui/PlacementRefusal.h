#pragma once

// Why something can't go in the layout right now (MainWindow: live on a
// server, and only another server has one of its parts). The status bar
// shows `brief`, a short phrase a narrow window doesn't cut off, and the
// whole `reason` is its tooltip. Both empty: it can be placed.

#include <QString>

namespace bld::ui {

struct PlacementRefusal {
    QString brief;
    QString reason;
};

}  // namespace bld::ui
