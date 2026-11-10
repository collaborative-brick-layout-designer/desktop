#pragma once

// The parts of Edit > Settings… that used to be the separate Preferences
// window: editing defaults, the language, starting up, the map's labels,
// parts and folders, and bringing in models. Each control saves the moment
// it changes (as the rest of Settings does) and says what it changed, so
// the window can follow at once.

#include <QString>
#include <QStringList>

#include <functional>

class QWidget;
class QVBoxLayout;

namespace bld::ui::settings {

enum class Change {
    SnapStep,       // editing/snapStepStuds
    TurnStep,       // editing/rotationStepDegrees
    PaintColor,     // editing/paintColor
    ZoomSpeed,      // general/wheelZoomFactor
    MapLook,        // connection points, module frames and names, venue labels: redraw
    ModuleFolder,   // modules/libraryPath
    PartsList,      // what the Parts panel lists
};
using Changed = std::function<void(Change)>;

// Each fills `into` (a Settings card) with its controls.
void addEditing(QVBoxLayout* into, QWidget* parent, const Changed& changed);
void addLanguage(QVBoxLayout* into, QWidget* parent);
void addStartingUp(QVBoxLayout* into, QWidget* parent);
void addMapLabels(QVBoxLayout* into, QWidget* parent, const Changed& changed);
// `openPartsFolders` opens Parts Folders… (left out when empty).
void addParts(QVBoxLayout* into, QWidget* parent, const Changed& changed,
              const std::function<void()>& openPartsFolders);
void addImport(QVBoxLayout* into, QWidget* parent);

// The languages the picker offers: code and name. "" is Match my computer.
struct Language {
    QString code;
    QString name;
};
QList<Language> languages();

// The zoom speeds offered, as the factor one wheel notch zooms by.
inline constexpr double kZoomGentle = 1.1, kZoomNormal = 1.2, kZoomFast = 1.4;

}  // namespace bld::ui::settings
