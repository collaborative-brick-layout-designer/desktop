#pragma once

// "Adding to: <sheet>" over the map: always says which sheet new parts
// go on (with its kind's mark, and "hidden" when it is hidden), and its
// menu switches the active sheet. MainWindow refreshes it whenever the
// Sheets panel is refreshed, so a rename, delete, undo or remote edit
// shows at once. The menu also holds "Fade other sheets while editing"
// (QSettings view/fadeOtherSheets, off by default; MapView draws it).

#include <QToolButton>

namespace bld::core { class Map; }

namespace bld::ui {

class ActiveSheetButton : public QToolButton {
    Q_OBJECT
public:
    explicit ActiveSheetButton(QWidget* parent = nullptr);

    // Read the sheets and the active one from `map` (nullptr: no layout).
    void refresh(const core::Map* map);

    // The saved "Fade other sheets while editing" setting.
    static bool fadeOtherSheetsSetting();

protected:
    // A new theme: the kind's mark again, in the new text color.
    void changeEvent(QEvent* e) override;

signals:
    // A sheet was picked from the menu.
    void sheetPicked(int index);
    // The text changed (its size may have too).
    void changed();
    // "Fade other sheets while editing" was ticked or unticked (and saved).
    void fadeOtherSheetsToggled(bool on);

private:
    int iconKind_ = -1;  // the active sheet's core::LayerKind, -1 for none
};

}  // namespace bld::ui
