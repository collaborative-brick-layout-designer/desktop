#include "AppIcon.h"

#include <QString>

static void initAppIconResources() { Q_INIT_RESOURCE(appicon); }

namespace bld::ui::theme {

QList<int> appIconSizes() { return { 16, 24, 32, 48, 64, 128, 256, 512 }; }

QIcon appIcon() {
    static const bool registered = [] {
        initAppIconResources();
        return true;
    }();
    Q_UNUSED(registered);
    QIcon icon;
    for (int px : appIconSizes()) icon.addFile(QStringLiteral(":/bld/appicon/%1.png").arg(px), QSize(px, px));
    return icon;
}

}  // namespace bld::ui::theme
