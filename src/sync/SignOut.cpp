#include "SignOut.h"

#include "ServerList.h"
#include "TokenStore.h"

#include <QCoreApplication>
#include <QLocale>

#include <memory>

namespace bld::sync {

namespace {
// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call Text::tr().
struct Text { Q_DECLARE_TR_FUNCTIONS(SignOut) };
}  // namespace

void signOut(TokenStore& tokens, const QUrl& server, QObject* context, std::function<void(const SignedOut&)> done) {
    QDateTime endsBy;
    {
        const ServerList list = ServerList::load();
        if (const ServerEntry* e = list.find(server)) endsBy = e->signIn.endsBy(QDateTime::currentDateTime());
    }
    // Whoever asked is told only while `context` is still there. (A flag, not
    // a QPointer: the analyzer misreads a QPointer copied through these lambdas.)
    auto alive = std::make_shared<bool>(context != nullptr);
    if (context) QObject::connect(context, &QObject::destroyed, [alive] { *alive = false; });
    const auto finish = [alive, done = std::move(done)](const SignedOut& result) {
        if (*alive && done) done(result);
    };
    tokens.load(server, [server, endsBy, finish](const QString& token) {
        if (token.isEmpty()) {
            finish({});
            return;
        }
        // The app's, not the window's: closing the window doesn't stop it.
        auto* api = new ServerApi(QCoreApplication::instance());
        api->setBase(server);
        api->setToken(token);
        QObject::connect(api, &ServerApi::signInEnded, api, [api, endsBy, finish](SignInEnd how) {
            api->deleteLater();
            finish({ how, how == SignInEnd::Ended ? QDateTime() : endsBy });
        });
        api->endSignIn();
    });
    // Whatever the server answers (the read above is ahead of this in the keychain's queue).
    tokens.remove(server);
}

QString signedOutText(const SignedOut& result, const QString& serverLabel) {
    if (result.how == SignInEnd::Ended) return Text::tr("Signed out of %1.").arg(serverLabel);
    // The server answered, and said no: it was reached, so not "couldn't be reached".
    if (result.how == SignInEnd::Refused) {
        if (!result.endsBy.isValid())
            return Text::tr("Signed out here. The server didn’t end that sign-in, so it stays valid until it runs "
                            "out; you can end it from Profile › Browsers and devices on the website.");
        return Text::tr("Signed out here. The server didn’t end that sign-in, so it stays valid until %1; you can "
                        "end it from Profile › Browsers and devices on the website.")
            .arg(QLocale().toString(result.endsBy.toLocalTime().date(), QLocale::LongFormat));
    }
    if (!result.endsBy.isValid())
        return Text::tr("Signed out here. The server couldn’t be reached, so that sign-in stays valid until it runs "
                        "out; you can end it from Profile › Browsers and devices on the website.");
    return Text::tr("Signed out here. The server couldn’t be reached, so that sign-in stays valid until %1; you can "
                    "end it from Profile › Browsers and devices on the website.")
        .arg(QLocale().toString(result.endsBy.toLocalTime().date(), QLocale::LongFormat));
}

}  // namespace bld::sync
