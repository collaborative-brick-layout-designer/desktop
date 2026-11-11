#pragma once

// Sign Out, and Remove server: the sign-in ends on the server too, not
// only here. The server is asked to end the token it gave this desktop
// (ServerApi::endSignIn), and the token leaves the keychain whatever the
// server says, so signing out always works, offline too. When the server
// couldn't end it, the person is told until when that sign-in would still
// work and where to end it themselves.

#include "ServerApi.h"

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <functional>

class QObject;

namespace bld::sync {

class TokenStore;

struct SignedOut {
    SignInEnd how = SignInEnd::Ended;
    // NotEnded only: the latest the sign-in still works on the server
    // (invalid when the server never said how long its sign-ins last).
    QDateTime endsBy;
};

// Forget `server`'s token here, at once, and ask the server to end it
// (the token is only ever sent to its own server). `done` hears how that
// went, unless `context` has gone by then; the request itself carries on
// even after the window that asked has closed.
void signOut(TokenStore& tokens, const QUrl& server, QObject* context, std::function<void(const SignedOut&)> done);

// What to tell the person: "Signed out of Train club.", or that the
// sign-in stays valid until a date and where to end it.
QString signedOutText(const SignedOut& result, const QString& serverLabel);

}  // namespace bld::sync
