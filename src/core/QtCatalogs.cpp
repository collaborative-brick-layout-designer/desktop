#include "QtCatalogs.h"

#include <QDir>
#include <QFileInfo>

namespace bld::core {

QStringList uiLanguages() {
    return { QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("nl"), QStringLiteral("pt"),
             QStringLiteral("es"), QStringLiteral("it"), QStringLiteral("no"), QStringLiteral("sv"),
             QStringLiteral("zh"), QStringLiteral("ja") };
}

QStringList qtCatalogNames(const QString& uiCode, const QString& systemLocale) {
    QStringList codes;
    const QString region = systemLocale.section(QLatin1Char('_'), 1, 1).section(QLatin1Char('.'), 0, 0);
    if (uiCode == QLatin1String("pt")) {
        codes = region == QLatin1String("PT") ? QStringList{ QStringLiteral("pt_PT"), QStringLiteral("pt_BR") }
                                              : QStringList{ QStringLiteral("pt_BR"), QStringLiteral("pt_PT") };
        codes << QStringLiteral("pt");
    } else if (uiCode == QLatin1String("zh")) {
        const bool traditional = region == QLatin1String("TW") || region == QLatin1String("HK")
                                 || region == QLatin1String("MO") || systemLocale.contains(QLatin1String("Hant"));
        codes = traditional ? QStringList{ QStringLiteral("zh_TW"), QStringLiteral("zh_CN") }
                            : QStringList{ QStringLiteral("zh_CN"), QStringLiteral("zh_TW") };
        codes << QStringLiteral("zh");
    } else if (uiCode == QLatin1String("no") || uiCode == QLatin1String("nb") || uiCode == QLatin1String("nn")) {
        codes = uiCode == QLatin1String("nn") ? QStringList{ QStringLiteral("nn"), QStringLiteral("nb") }
                                              : QStringList{ QStringLiteral("nb"), QStringLiteral("nn") };
        codes << QStringLiteral("no");
    } else {
        codes << uiCode;
    }
    QStringList names;
    for (const QString& c : codes) names << QStringLiteral("qt_") + c << QStringLiteral("qtbase_") + c;
    return names;
}

QString findQtCatalog(const QString& uiCode, const QString& systemLocale, const QStringList& dirs) {
    for (const QString& name : qtCatalogNames(uiCode, systemLocale)) {
        for (const QString& dir : dirs) {
            if (dir.isEmpty()) continue;
            const QFileInfo f(QDir(dir).filePath(name + QStringLiteral(".qm")));
            if (f.isFile()) return f.absoluteFilePath();
        }
    }
    return {};
}

}  // namespace bld::core
