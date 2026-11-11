#include "ServerRefusal.h"

#include "ServerApi.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace bld::sync {

namespace {
// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call Text::tr().
struct Text { Q_DECLARE_TR_FUNCTIONS(ServerRefusal) };
}  // namespace

ServerRefusal readRefusal(int status, const QByteArray& body, const QString& networkError) {
    ServerRefusal r;
    r.status = status;
    r.networkError = networkError;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    r.jsonBody = doc.isObject();
    const QJsonObject o = doc.object();
    r.code = o.value(QLatin1String("error")).toString();
    r.message = o.value(QLatin1String("message")).toString().trimmed();
    r.limit = o.value(QLatin1String("limit")).toString();
    r.permission = o.value(QLatin1String("permission")).toString();
    r.downloadUrl = ServerApi::webLink(o.value(QLatin1String("downloadUrl")).toString()).toString();
    r.minimum = o.value(QLatin1String("minimum")).toString();
    return r;
}

bool isLimitRefusal(const ServerRefusal& r) {
    return r.code == QLatin1String("limit_reached") || r.code == QLatin1String("suspended") ||
           r.code == QLatin1String("rate_limited") || r.code == QLatin1String("verify_email_first");
}

bool isAccountState(const ServerRefusal& r) {
    return r.code == QLatin1String("account_pending_deletion") || r.code == QLatin1String("account_restricted");
}

bool isUpdateRequired(const ServerRefusal& r) {
    return r.status == 426 || r.code == QLatin1String("update_required");
}

bool isFirewallBlock(const ServerRefusal& r) {
    return r.status == 403 && !r.jsonBody;
}

bool needsSignIn(const ServerRefusal& r) {
    if (r.status == 401) return true;
    // A token without the rights this needs: a new sign-in asks for them.
    return r.status == 403 && r.code == QLatin1String("insufficient_scope");
}

QString permissionLabel(const QString& permission) {
    // The web's role editor (apps/web/src/orgs/clubPermissions.ts).
    static const std::pair<const char*, const char*> labels[] = {
        { "view_items", QT_TRANSLATE_NOOP("ServerRefusal", "View club items") },
        { "edit_layouts", QT_TRANSLATE_NOOP("ServerRefusal", "Edit club layouts") },
        { "start_layouts", QT_TRANSLATE_NOOP("ServerRefusal", "Start new club layouts") },
        { "edit_modules", QT_TRANSLATE_NOOP("ServerRefusal", "Add or edit modules") },
        { "add_parts", QT_TRANSLATE_NOOP("ServerRefusal", "Add parts") },
        { "manage_venues", QT_TRANSLATE_NOOP("ServerRefusal", "Manage venues") },
        { "delete_items", QT_TRANSLATE_NOOP("ServerRefusal", "Delete club items") },
        { "send_catalog", QT_TRANSLATE_NOOP("ServerRefusal", "Send to the catalog") },
        { "invite", QT_TRANSLATE_NOOP("ServerRefusal", "Invite people") },
        { "manage_members", QT_TRANSLATE_NOOP("ServerRefusal", "Manage members and roles") },
        { "export_data", QT_TRANSLATE_NOOP("ServerRefusal", "Download the club's data") },
    };
    for (const auto& [key, label] : labels)
        if (permission == QLatin1String(key)) return Text::tr(label);
    return permission;
}

QString askForPermission(const QString& permission) {
    return Text::tr("Ask a club admin for the %1 permission.").arg(permissionLabel(permission));
}

namespace {
// A 403 the server explained: who you are here (your role on the thing, a
// club's permissions), not your sign-in.
bool isPermissionRefusal(const ServerRefusal& r) {
    return r.status == 403 && r.jsonBody && !needsSignIn(r) && !isLimitRefusal(r) && !isAccountState(r);
}
}  // namespace

QString describe(const ServerRefusal& r) {
    if (isFirewallBlock(r))
        return Text::tr("The site's firewall blocked this request. Please tell the site admin (what you were doing, and "
                  "the time).");
    if (isLimitRefusal(r)) {
        // The server's own sentence names the limit; keep it short and plain.
        if (!r.message.isEmpty()) return r.message.left(400);
        if (r.code == QLatin1String("suspended")) return Text::tr("This account is read-only for now. Ask the site admin why.");
        if (r.code == QLatin1String("rate_limited")) return Text::tr("Too many requests at once. Please wait a minute and try again.");
        if (r.code == QLatin1String("verify_email_first")) return Text::tr("Please confirm your email address first.");
        return Text::tr("You have reached a limit on this server. Ask the site admin for more room.");
    }
    if (isAccountState(r)) {
        if (!r.message.isEmpty()) return r.message.left(400);
        if (r.code == QLatin1String("account_pending_deletion"))
            return Text::tr("This account is being deleted. To keep it, sign in on the website before then; then this app "
                      "works again.");
        return Text::tr("Your account is on hold (read only) while a privacy request is looked at. You can still see and "
                  "download your things. Ask the site admin.");
    }
    if (isUpdateRequired(r)) {
        if (!r.message.isEmpty()) return r.message.left(400);
        return Text::tr("This server needs a newer Collaborative Brick Layout Designer. Please download the new version.");
    }
    if (r.status == 401) return Text::tr("Your sign-in has run out. Sign in again (File › Servers…).");
    if (r.code == QLatin1String("insufficient_scope") || r.code == QLatin1String("token_not_allowed"))
        return Text::tr("This sign-in can't do that. Sign in again (File › Servers…) to give the app the rights it "
                  "needs.");
    if (r.code == QLatin1String("not_an_org_member")) return Text::tr("You're not a member of that club.");
    if (r.code.startsWith(QLatin1String("demo_account"))) return Text::tr("The demo account can't do that.");
    if (!r.permission.isEmpty()) {
        const QString why = r.code == QLatin1String("only_club_admins_can_add")
                                ? Text::tr("Your roles in this club can't add that.")
                                : Text::tr("Your roles in this club don't allow that.");
        return why + QLatin1Char(' ') + askForPermission(r.permission);
    }
    // Servers before club roles name no permission.
    if (r.code == QLatin1String("only_club_admins_can_add"))
        return Text::tr("This club only lets some of its members add things. Ask a club admin.");
    if (isPermissionRefusal(r))
        return Text::tr("You don't have permission to do that. Ask its owner, or a club admin, for access.");
    if (!r.code.isEmpty()) return r.code;
    if (r.status == 0) return r.networkError.isEmpty() ? Text::tr("The server could not be reached.") : r.networkError;
    return Text::tr("The server answered %1").arg(r.status);
}

namespace {
QString networkFailureWords(QNetworkReply::NetworkError error, const QString& h) {
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
        return Text::tr(
                   "%1 isn't answering. The server may be down or restarting: try again in a minute. If it "
                   "keeps happening, check the address with whoever runs the server.")
            .arg(h);
    case QNetworkReply::HostNotFoundError:
        return Text::
            tr("Couldn't find a server at %1. Check the address for typing mistakes, and that this computer "
               "is online.")
                .arg(h);
    case QNetworkReply::SslHandshakeFailedError:
    case QNetworkReply::InsecureRedirectError:
        return Text::
            tr("%1's security certificate isn't valid, so the app didn't connect, to keep your sign-in safe. "
               "Whoever runs the server needs to fix its certificate. (A server on this computer or your "
               "own network may use http:// instead of https://.)")
                .arg(h);
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: // the transfer timeout
        return Text::tr(
                   "%1 took too long to answer. Check this computer's internet connection, then try again.")
            .arg(h);
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
        return Text::tr(
                   "The connection to %1 dropped. Check this computer's internet connection, then try again.")
            .arg(h);
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
        return Text::
            tr("Couldn't reach %1 through this computer's proxy. Check the proxy settings, then try again.")
                .arg(h);
    default:
        return Text::tr(
                   "Couldn't reach %1. Check the address and this computer's internet connection, then try "
                   "again.")
            .arg(h);
    }
}
} // namespace

QString networkFailureText(QNetworkReply::NetworkError error, const QString& host) {
    if (!host.isEmpty()) return networkFailureWords(error, host);
    // "the server isn't answering" starts a sentence.
    QString text = networkFailureWords(error, Text::tr("the server"));
    if (!text.isEmpty()) text[0] = text[0].toUpper();
    return text;
}

QString networkFailureText(const QNetworkReply* reply) {
    if (!reply) return networkFailureText(QNetworkReply::UnknownNetworkError, QString());
    return networkFailureText(reply->error(), reply->url().host());
}

bool isNotOurServer(const ServerRefusal& r) {
    return r.status != 0 && !r.jsonBody && !isFirewallBlock(r) && r.status < 500;
}

bool isServerDown(const ServerRefusal& r) {
    return !r.jsonBody && (r.status == 502 || r.status == 503 || r.status == 504);
}

QString notOurServerText(const QString& host, int status) {
    return Text::tr("%1 answered, but it isn't a Collaborative Brick Layout Designer server (it said %2). Check the "
                    "address.")
        .arg(host)
        .arg(status);
}

QString failureText(const ServerRefusal& r) {
    if (isFirewallBlock(r) || isLimitRefusal(r) || isAccountState(r) || isUpdateRequired(r) || isPermissionRefusal(r) ||
        r.status == 0)
        return describe(r);
    return Text::tr("The server answered %1").arg(r.status);
}

bool isSignInEnded(int code, const QString& reason) {
    return code == 1008 && (reason == QLatin1String("token_revoked") || reason == QLatin1String("session_revoked") ||
                            reason == QLatin1String("unauthorized"));
}

namespace {
// Why, in whole sentences; empty for an ending this version doesn't know.
QString liveCloseWhy(int code, const QString& reason) {
    if (code == 4415 && reason == QLatin1String("unreadable_doc"))
        return Text::tr("This layout was saved by a newer version of Collaborative Brick Layout Designer, so this "
                        "version can't keep it live. Download the new version (Help > Check for Updates) to work on "
                        "it live again.");
    if (code == 4416 && reason == QLatin1String("unreadable_update"))
        return Text::tr("A live update to this layout couldn't be read, so it's no longer live here. Open it from the "
                        "server again to keep working on it with everyone.");
    if (code == 4426)
        return Text::tr("This version of Collaborative Brick Layout Designer is too old for this server, so the layout "
                        "is no longer live here. Download the new version (Help > Check for Updates).");
    if (code == 1008 && reason == QLatin1String("account_pending_deletion"))
        return Text::tr("This account is being deleted, so the layout is no longer live here. To keep the account, "
                        "sign in on the website before the date in your email, then open the layout again.");
    if (code == 1008 && reason == QLatin1String("account_restricted"))
        return Text::tr("Your account is on hold (read only) while a privacy request is looked at, so the layout is no "
                        "longer live here. Ask the site admin.");
    // The layout went away, or who may open it changed (apps/server/src/ws/handler.ts, routes/ws.ts).
    if (code == 4404 && reason == QLatin1String("layout_deleted")) return Text::tr("This layout was deleted on the server.");
    if (code == 4404 && (reason == QLatin1String("access_revoked") || reason == QLatin1String("not_found")))
        return Text::tr("You can't open this layout any more: it was deleted, or its owner or a club admin changed who "
                        "can open it.");
    // The sign-in ended: it ran out, or was signed out elsewhere or removed
    // on the website. (The window keeps the layout open for this one and
    // offers to sign in again; this is for a caller that closes it.)
    if (isSignInEnded(code, reason))
        return Text::tr("Your sign-in on this server ended (it ran out, or was signed out elsewhere or removed on the "
                        "website), so the layout is no longer live here. Sign in again (File › Servers…), then open "
                        "the layout again.");
    if (code == 4429 && reason == QLatin1String("limit_reached"))
        return Text::tr("Too many people have this layout open right now, so it's no longer live here. Try again in a "
                        "little while.");
    return {};
}
}  // namespace

bool isKnownLiveClose(int code, const QString& reason) {
    return (code == 1008 && reason == QLatin1String("layout_merge_refused")) || !liveCloseWhy(code, reason).isEmpty();
}

QString liveCloseText(int code, const QString& reason) {
    // The server's merge guard (apps/server/src/ws/mergeGuard.ts): an update
    // that would bring another layout's layers into this one. It says what
    // to do with the copy itself.
    if (code == 1008 && reason == QLatin1String("layout_merge_refused"))
        return Text::tr("Your latest changes weren't taken, because they looked like another layout's parts being "
                        "merged into this one, so the layout is no longer live here. Nothing is lost: your copy stays "
                        "open here, unsaved. Save it (File > Save As) and tell the site admin.");
    QString why = liveCloseWhy(code, reason);
    if (why.isEmpty()) why = Text::tr("The server closed this live layout.");
    // (the sentence that follows why, the same for every ending)
    return why + QLatin1Char(' ') + Text::tr("Your copy stays open here, unsaved. To keep it, save it (File > Save As).");
}

QString liveCloseDetail(int code, const QString& reason) {
    if (isKnownLiveClose(code, reason)) return {};
    return reason.isEmpty() ? Text::tr("The server's close code: %1").arg(code)
                            : Text::tr("The server's close code: %1 (%2)").arg(code).arg(reason);
}

}  // namespace bld::sync
