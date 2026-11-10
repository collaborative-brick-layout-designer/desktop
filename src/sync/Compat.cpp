#include "Compat.h"

#include "core/Version.h"

#include <QCoreApplication>

namespace bld::sync {

namespace {
// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call Text::tr().
struct Text { Q_DECLARE_TR_FUNCTIONS(Compat) };
}  // namespace

Standing desktopStanding(const QString& app, const QString& minimum, const QString& recommended) {
    if (!minimum.isEmpty() && core::compareVersions(app, minimum) == -1) return Standing::UpdateRequired;
    if (!recommended.isEmpty() && core::compareVersions(app, recommended) == -1) return Standing::UpdateSuggested;
    return Standing::Ok;
}

bool canReadDoc(std::optional<int> docSchema, int schemaVersion, int minReadable) {
    if (!docSchema) return true;
    return *docSchema >= minReadable && *docSchema <= schemaVersion;
}

QStringList desktopFeatures() {
    return { QStringLiteral("liveSync"),      QStringLiteral("signIn"),
             QStringLiteral("publish"),       QStringLiteral("clubs"),
             QStringLiteral("venues"),        QStringLiteral("partsManifest"),
             QStringLiteral("partFilesWithToken"), QStringLiteral("uploadParts"),
             QStringLiteral("preferences"),   QStringLiteral("ownerTags") };
}

QStringList assumedWhenUnlisted() { return { QStringLiteral("liveSync"), QStringLiteral("signIn") }; }

QStringList missingFeatures(const std::optional<QStringList>& serverFeatures) {
    const QStringList has = serverFeatures ? *serverFeatures : assumedWhenUnlisted();
    QStringList out;
    for (const QString& f : desktopFeatures())
        if (!has.contains(f)) out << f;
    return out;
}

QString featureLabel(const QString& id) {
    if (id == QLatin1String("liveSync")) return Text::tr("Editing a layout together, live");
    if (id == QLatin1String("signIn")) return Text::tr("Signing in from the desktop app");
    if (id == QLatin1String("publish")) return Text::tr("Putting a layout on the server");
    if (id == QLatin1String("clubs")) return Text::tr("Your clubs");
    if (id == QLatin1String("venues")) return Text::tr("The venue library");
    if (id == QLatin1String("partsManifest")) return Text::tr("Getting the server's parts");
    if (id == QLatin1String("partFilesWithToken")) return Text::tr("Downloading the server's part pictures");
    if (id == QLatin1String("uploadParts")) return Text::tr("Sending your own parts to the server");
    if (id == QLatin1String("preferences")) return Text::tr("Your settings on every computer");
    if (id == QLatin1String("ownerTags")) return Text::tr("Showing whose layouts and venues they are");
    if (id == QLatin1String("limits")) return Text::tr("Usage limits, with plain messages when one is reached");
    if (id == QLatin1String("layoutDownload")) return Text::tr("Downloading a copy of a layout, venue sizes in the list");
    if (id == QLatin1String("catalogShare")) return Text::tr("Sharing to the public catalog from the desktop app");
    if (id == QLatin1String("uploadSets")) return Text::tr("Uploading sets of your own parts");
    if (id == QLatin1String("deletePartsFromDesktop"))
        return Text::tr("Deleting your own parts and sets from the desktop app");
    return id;
}

}  // namespace bld::sync
