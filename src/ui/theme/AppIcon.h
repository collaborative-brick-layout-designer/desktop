#pragma once

// The app's logo as a window icon: every size rendered at exactly that size
// (16 to 512 px), so the title bar, taskbar and About box get a sharp one
// instead of a scaled-down large image.

#include <QIcon>
#include <QList>

namespace bld::ui::theme {

// The sizes appIcon() holds, smallest first.
QList<int> appIconSizes();

QIcon appIcon();

}  // namespace bld::ui::theme
