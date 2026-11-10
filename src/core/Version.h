#pragma once

// Version numbers as the desktop's releases and the server's /api/version
// write them ("v1.3.0", "1.3.0-beta.1", "1.3.0+build.7"). The rules match
// the server's compat.ts; compat/compat.json holds the cases both check.

#include <QString>

#include <optional>

namespace bld::core {

// -1, 0 or 1; nullopt when either can't be read. Numbers compare as
// numbers ("1.10" > "1.9"), missing parts are 0, a pre-release is older
// than its release, and "+build" is ignored.
std::optional<int> compareVersions(const QString& a, const QString& b);

// The build's channel: empty for a release, "nightly" for a nightly build
// (BLD_CHANNEL, set by main.cpp from the build). About says it beside the
// version.
QString buildChannel();
void setBuildChannel(const QString& channel);

// Help > About's "Third-party licenses": the installed THIRD-PARTY-NOTICES.txt
// (beside the app on Windows, in Resources on macOS, in share/doc on Linux),
// or the copy on GitHub when it isn't there (a build run from its folder).
QString thirdPartyNoticesUrl();

// Where this desktop's releases are downloaded (GitHub releases).
QString desktopDownloadUrl();

}  // namespace bld::core
