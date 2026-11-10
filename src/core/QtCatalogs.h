#pragma once

// Qt's own translations (the words in Qt's dialogs and buttons) for the app's
// UI languages. Packages carry them under different names: windeployqt merges
// them into qt_<code>.qm, Linux and the macOS bundle keep qtbase_<code>.qm,
// and some languages are only there with a region or a sibling: Portuguese
// as pt_BR, Chinese as zh_CN or zh_TW, Norwegian Bokmål ("no" in the app) as
// nb, or nn where Qt has no nb.

#include <QString>
#include <QStringList>

namespace bld::core {

// The app's UI languages besides English, as Settings offers them.
QStringList uiLanguages();

// File names (without .qm) to try for `uiCode`, best first. `systemLocale`
// ("zh_TW", "pt_PT") picks the region when the app's language has several.
QStringList qtCatalogNames(const QString& uiCode, const QString& systemLocale);

// The first catalog found in `dirs` for `uiCode`, as a full path to its .qm;
// empty when there is none.
QString findQtCatalog(const QString& uiCode, const QString& systemLocale, const QStringList& dirs);

}  // namespace bld::core
