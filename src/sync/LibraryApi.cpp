#include "LibraryApi.h"

#include <QCoreApplication>
#include "ServerApi.h"
#include "ClubLogos.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>

namespace bld::sync {

QString idPath(const QString& id) { return QString::fromLatin1(QUrl::toPercentEncoding(id)); }

namespace {

QDateTime timeOf(const QJsonValue& v) {
    if (v.isDouble()) return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(v.toDouble()));
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

int intOf(const QJsonValue& v) { return v.isDouble() ? static_cast<int>(v.toDouble()) : 0; }

// Who owns it: newer servers tag it ({kind, id, name, slug}); older ones give a club's id only.
OwnerTag ownerOf(const QJsonObject& o) {
    OwnerTag t;
    const QJsonObject owner = o.value(QLatin1String("owner")).toObject();
    t.kind = owner.value(QLatin1String("kind")).toString();
    t.id = owner.value(QLatin1String("id")).toString();
    t.name = owner.value(QLatin1String("name")).toString();
    t.slug = owner.value(QLatin1String("slug")).toString();
    if (t.kind.isEmpty()) {
        const QString org = o.value(QLatin1String("ownerOrgId")).toString();
        t.kind = org.isEmpty() ? QStringLiteral("user") : QStringLiteral("org");
        t.id = org.isEmpty() ? o.value(QLatin1String("ownerUserId")).toString() : org;
    }
    return t;
}

}  // namespace

QString ServerModule::thumbnailPath() const {
    if (thumbnailAt <= 0) return {};
    return QStringLiteral("/api/modules/%1/thumbnail?v=%2").arg(idPath(id)).arg(thumbnailAt);
}

LibraryApi::LibraryApi(QObject* parent) : QObject(parent) {
    net_.setTransferTimeout(kServerStallTimeoutMs);
}

QUrl LibraryApi::moduleWebUrl(const QString& id) const { return url(QStringLiteral("/modules/") + idPath(id)); }

QUrl LibraryApi::url(const QString& pathAndQuery) const {
    QUrl u = base_;
    const qsizetype q = pathAndQuery.indexOf(QLatin1Char('?'));
    u.setPath(q < 0 ? pathAndQuery : pathAndQuery.left(q), QUrl::TolerantMode);
    u.setQuery(q < 0 ? QString() : pathAndQuery.mid(q + 1));
    return u;
}

void LibraryApi::send(const QByteArray& verb, const QString& path, const QByteArray& body,
                      const QByteArray& contentType, std::function<void(QNetworkReply*)> ok, Fail failed,
                      const QList<QPair<QByteArray, QByteArray>>& headers) {
    QNetworkRequest req(url(path));
    for (const auto& [name, value] : headers) req.setRawHeader(name, value);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    if (!contentType.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    QNetworkReply* r = verb == "GET" ? net_.get(req) : net_.sendCustomRequest(req, verb, body);
    ++pending_;
    // The reply belongs to net_, so it (and this connection) goes with us.
    connect(r, &QNetworkReply::finished, this, [this, r, ok = std::move(ok), failed = std::move(failed)] {
        r->deleteLater();
        --pending_;
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            if (ok) ok(r);
            return;
        }
        if (failed) failed(readRefusal(status, r->readAll(), networkFailureText(r)));
    });
}

void LibraryApi::sendJson(const QByteArray& verb, const QString& path, const QJsonObject& body,
                          std::function<void(const QJsonObject&)> ok, Fail failed) {
    const bool get = verb == "GET";
    Fail notJson = failed;
    send(verb, path, get ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact),
         get ? QByteArray() : QByteArrayLiteral("application/json"),
         [ok = std::move(ok), notJson = std::move(notJson)](QNetworkReply* r) {
             const QByteArray raw = r->readAll();
             const QJsonDocument doc = QJsonDocument::fromJson(raw);
             // A web page where the JSON should be: another website at this
             // address answered (its "page not found" page, often with a 200).
             if (doc.isNull() && !raw.trimmed().isEmpty()) {
                 if (notJson) notJson(readRefusal(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), raw));
                 return;
             }
             if (ok) ok(doc.object());
         },
         std::move(failed));
}

ServerModule LibraryApi::moduleFromJson(const QJsonObject& o) {
    ServerModule m;
    m.id = o.value(QLatin1String("id")).toString();
    m.title = o.value(QLatin1String("title")).toString();
    m.role = o.value(QLatin1String("role")).toString();
    m.docVersion = intOf(o.value(QLatin1String("docVersion")));
    m.latestVersion = intOf(o.value(QLatin1String("latestVersion")));
    m.empty = o.value(QLatin1String("empty")).toBool(false);
    m.setup = o.value(QLatin1String("setup")).toObject();
    const QJsonValue at = o.value(QLatin1String("thumbnailAt"));
    m.thumbnailAt = at.isDouble() ? static_cast<qint64>(at.toDouble()) : 0;
    m.updatedAt = timeOf(o.value(QLatin1String("updatedAt")));
    m.credit = creditFromJson(o);
    m.owner = ownerOf(o);
    return m;
}

namespace {
ClubRef clubRefOf(const QJsonObject& o) {
    ClubRef c;
    c.id = o.value(QLatin1String("orgId")).toString(o.value(QLatin1String("id")).toString());
    if (c.id.isEmpty()) c.id = o.value(QLatin1String("userId")).toString();
    c.name = o.value(QLatin1String("name")).toString();
    c.slug = o.value(QLatin1String("slug")).toString();
    c.layouts = intOf(o.value(QLatin1String("layouts")));
    c.via = o.value(QLatin1String("via")).toString();
    return c;
}
SharedPlaceCheck::Owner checkOwnerOf(const QJsonObject& o) {
    return { o.value(QLatin1String("kind")).toString(), o.value(QLatin1String("name")).toString(),
             o.value(QLatin1String("mine")).toBool(false) };
}
struct SharingText { Q_DECLARE_TR_FUNCTIONS(LibraryApi) };
}  // namespace

QString ServerModule::sharedViaText() const {
    QStringList clubs;
    for (const auto& c : sharedClubs) clubs << c.name;
    const QString via = clubs.isEmpty() ? SharingText::tr("shared with you")
                                        : SharingText::tr("via %1").arg(clubs.join(QStringLiteral(", ")));
    return owner.name.isEmpty() ? via : QStringLiteral("%1 · %2").arg(owner.name, via);
}

QString ModuleShares::usedInText() const {
    if (usedIn.layouts <= 0) return {};
    QStringList parts;
    for (const auto& c : usedIn.clubs) parts << QStringLiteral("%1: %2").arg(c.name).arg(c.layouts);
    for (const auto& p : usedIn.people) parts << QStringLiteral("%1: %2").arg(p.name).arg(p.layouts);
    if (usedIn.others > 0) parts << SharingText::tr("others: %1").arg(usedIn.others);
    const QString used = SharingText::tr("Used in %n layout(s)", nullptr, usedIn.layouts);
    return parts.isEmpty() ? used : QStringLiteral("%1 (%2)").arg(used, parts.join(QStringLiteral(", ")));
}

int ModuleShares::layoutsOf(const QString& orgId) const {
    for (const auto& c : usedIn.clubs)
        if (c.id == orgId) return c.layouts;
    return 0;
}

const LayoutSharedModule* LayoutSharedModules::row(const QString& placedId) const {
    for (const auto& m : modules)
        if (m.placedId == placedId) return &m;
    return nullptr;
}

ModuleShares LibraryApi::moduleSharesFromJson(const QJsonObject& o) {
    ModuleShares out;
    out.latestVersion = intOf(o.value(QLatin1String("latestVersion")));
    out.canShareWithClubs = o.value(QLatin1String("canShareWithClubs")).toBool(true);
    out.canShareWithOtherClubs = o.value(QLatin1String("canShareWithOtherClubs")).toBool(false);
    if (const QJsonValue c = o.value(QLatin1String("club")); c.isObject()) out.club = clubRefOf(c.toObject());
    for (const auto& v : o.value(QLatin1String("partners")).toArray()) out.partners << clubRefOf(v.toObject());
    for (const auto& v : o.value(QLatin1String("offers")).toArray()) {
        const QJsonObject fo = v.toObject();
        ClubOffer f;
        f.id = fo.value(QLatin1String("id")).toString();
        f.club = clubRefOf(fo.value(QLatin1String("club")).toObject());
        f.state = fo.value(QLatin1String("state")).toString();
        const QJsonValue at = fo.value(QLatin1String("askAgainAt"));
        f.askAgainAt = at.isDouble() ? static_cast<qint64>(at.toDouble()) : 0;
        out.offers << f;
    }
    for (const auto& v : o.value(QLatin1String("clubs")).toArray()) out.clubs << clubRefOf(v.toObject());
    for (const auto& v : o.value(QLatin1String("myClubs")).toArray()) out.myClubs << clubRefOf(v.toObject());
    for (const auto& v : o.value(QLatin1String("people")).toArray())
        if (v.toObject().value(QLatin1String("role")).toString() != QLatin1String("owner")) ++out.people;
    const QJsonObject used = o.value(QLatin1String("usedIn")).toObject();
    out.usedIn.layouts = intOf(used.value(QLatin1String("layouts")));
    out.usedIn.others = intOf(used.value(QLatin1String("others")));
    for (const auto& v : used.value(QLatin1String("clubs")).toArray()) out.usedIn.clubs << clubRefOf(v.toObject());
    for (const auto& v : used.value(QLatin1String("people")).toArray()) out.usedIn.people << clubRefOf(v.toObject());
    return out;
}

LayoutSharedModules LibraryApi::layoutSharedModulesFromJson(const QJsonObject& o) {
    LayoutSharedModules out;
    out.updates = o.value(QLatin1String("updates")).toString() == QLatin1String("auto") ? QStringLiteral("auto") : QStringLiteral("ask");
    out.canUpdate = o.value(QLatin1String("canUpdate")).toBool(false);
    out.canDetach = o.value(QLatin1String("canDetach")).toBool(false);
    for (const auto& v : o.value(QLatin1String("modules")).toArray()) {
        const QJsonObject m = v.toObject();
        LayoutSharedModule row;
        row.placedId = m.value(QLatin1String("placedId")).toString();
        row.moduleId = m.value(QLatin1String("moduleId")).toString();
        row.state = m.value(QLatin1String("state")).toString();
        row.version = intOf(m.value(QLatin1String("version")));
        row.title = m.value(QLatin1String("title")).toString();
        row.ownerName = m.value(QLatin1String("owner")).toObject().value(QLatin1String("name")).toString();
        row.latest = intOf(m.value(QLatin1String("latest")));
        row.latestNote = m.value(QLatin1String("latestNote")).toString();
        out.modules << row;
    }
    if (const QJsonValue c = o.value(QLatin1String("check")); c.isObject()) {
        const QJsonObject co = c.toObject();
        SharedPlaceCheck check;
        check.moduleId = co.value(QLatin1String("moduleId")).toString();
        check.state = co.value(QLatin1String("state")).toString();
        check.title = co.value(QLatin1String("title")).toString();
        check.moduleOwner = checkOwnerOf(co.value(QLatin1String("moduleOwner")).toObject());
        check.layoutOwner = checkOwnerOf(co.value(QLatin1String("layoutOwner")).toObject());
        out.check = check;
    }
    return out;
}

ServerPart LibraryApi::partFromJson(const QJsonObject& o) {
    ServerPart p;
    p.id = o.value(QLatin1String("id")).toString();
    p.partNumber = o.value(QLatin1String("partNumber")).toString();
    p.displayName = o.value(QLatin1String("displayName")).toString();
    p.role = o.value(QLatin1String("role")).toString();
    p.owner = ownerOf(o);
    p.updatedAt = timeOf(o.value(QLatin1String("updatedAt")));
    p.credit = creditFromJson(o);
    p.hasSprite = !o.value(QLatin1String("spriteMime")).toString().isEmpty();
    return p;
}

QString ServerPart::spritePath() const {
    if (!hasSprite) return {};
    // The date keeps the cached picture until the part changes.
    return QStringLiteral("/api/custom-parts/%1/sprite?v=%2").arg(idPath(id)).arg(updatedAt.toMSecsSinceEpoch());
}

CatalogItem LibraryApi::catalogItemFromJson(const QJsonObject& o) {
    CatalogItem it;
    it.id = o.value(QLatin1String("id")).toString();
    it.kind = o.value(QLatin1String("kind")).toString();
    it.title = o.value(QLatin1String("title")).toString();
    it.description = o.value(QLatin1String("description")).toString();
    it.by = o.value(QLatin1String("by")).toString();
    it.uses = intOf(o.value(QLatin1String("uses")));
    it.version = intOf(o.value(QLatin1String("version")));
    it.previewPath = o.value(QLatin1String("previewUrl")).toString();
    it.coverPath = o.value(QLatin1String("coverUrl")).toString();
    const QJsonObject sum = o.value(QLatin1String("summary")).toObject();
    it.widthStuds = intOf(sum.value(QLatin1String("widthStuds")));
    it.heightStuds = intOf(sum.value(QLatin1String("heightStuds")));
    if (sum.contains(QLatin1String("partCount"))) it.partCount = intOf(sum.value(QLatin1String("partCount")));
    return it;
}

Notice LibraryApi::noticeFromJson(const QJsonObject& o) {
    Notice n;
    n.id = o.value(QLatin1String("id")).toString();
    n.scope = o.value(QLatin1String("scope")).toString();
    n.severity = o.value(QLatin1String("severity")).toString();
    n.reason = o.value(QLatin1String("reason")).toString();
    n.link = o.value(QLatin1String("link")).toString();
    n.createdAt = timeOf(o.value(QLatin1String("createdAt")));
    const QJsonValue ack = o.value(QLatin1String("acknowledgedAt"));
    n.acknowledged = !ack.isNull() && !ack.isUndefined();
    n.clubName = o.value(QLatin1String("club")).toObject().value(QLatin1String("name")).toString();
    const QJsonObject to = o.value(QLatin1String("to")).toObject();
    if (to.value(QLatin1String("kind")).toString() == QLatin1String("org"))
        n.toClub = to.value(QLatin1String("name")).toString();
    return n;
}

void LibraryApi::listModules(std::function<void(const QList<ServerModule>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/modules"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<ServerModule> out;
        for (const auto& v : o.value(QLatin1String("modules")).toArray()) out << moduleFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::sharedWithMe(std::function<void(const QList<ServerModule>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/modules?owner=shared"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<ServerModule> out;
        for (const auto& v : o.value(QLatin1String("modules")).toArray()) {
            const QJsonObject mo = v.toObject();
            ServerModule m = moduleFromJson(mo);
            const QJsonObject via = mo.value(QLatin1String("sharedVia")).toObject();
            m.sharedWithMe = true;
            m.sharedDirect = via.value(QLatin1String("direct")).toBool(false);
            for (const auto& c : via.value(QLatin1String("clubs")).toArray()) m.sharedClubs << clubRefOf(c.toObject());
            out << m;
        }
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::moduleShares(const QString& id, std::function<void(const ModuleShares&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/modules/%1/shares").arg(idPath(id)), {},
             [done = std::move(done)](const QJsonObject& o) {
                 if (done) done(moduleSharesFromJson(o));
             },
             std::move(failed));
}

void LibraryApi::shareWithClub(const QString& id, const QString& orgSlug, std::function<void()> done, Fail failed) {
    sendJson("POST", QStringLiteral("/api/modules/%1/club-shares").arg(idPath(id)),
             QJsonObject{ { QStringLiteral("orgSlug"), orgSlug } },
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

void LibraryApi::stopClubShare(const QString& id, const QString& orgId, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/modules/%1/club-shares/%2").arg(idPath(id), idPath(orgId)), {}, {},
         [done = std::move(done)](QNetworkReply*) {
             if (done) done();
         },
         std::move(failed));
}

void LibraryApi::shareClubModule(const QString& id, const QString& clubSlugOrLink,
                                 std::function<void(const ClubShareResult&)> done, Fail failed) {
    sendJson("POST", QStringLiteral("/api/modules/%1/club-shares").arg(idPath(id)),
             QJsonObject{ { QStringLiteral("orgSlug"), clubSlugOrLink.trimmed() } },
             [done = std::move(done)](const QJsonObject& o) {
                 ClubShareResult r;
                 r.status = o.value(QLatin1String("status")).toString(QStringLiteral("shared"));
                 r.name = o.value(QLatin1String("name")).toString();
                 r.slug = o.value(QLatin1String("slug")).toString();
                 if (done) done(r);
             },
             std::move(failed));
}

void LibraryApi::withdrawClubOffer(const QString& id, const QString& offerId, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/modules/%1/club-offers/%2").arg(idPath(id), idPath(offerId)), {}, {},
         [done = std::move(done)](QNetworkReply*) {
             if (done) done();
         },
         std::move(failed));
}

void LibraryApi::clubTargets(const QString& id, const QString& q, std::function<void(const QList<ClubTarget>&)> done,
                             Fail failed) {
    sendJson("GET",
             QStringLiteral("/api/modules/%1/club-targets?q=").arg(idPath(id)) +
                 QString::fromLatin1(QUrl::toPercentEncoding(q.trimmed())),
             {},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<ClubTarget> out;
                 for (const auto& v : o.value(QLatin1String("clubs")).toArray()) {
                     const QJsonObject co = v.toObject();
                     ClubTarget t;
                     t.club = clubRefOf(co);
                     t.partner = co.value(QLatin1String("partner")).toBool(false);
                     t.shared = co.value(QLatin1String("shared")).toBool(false);
                     t.offer = co.value(QLatin1String("offer")).toString();
                     out << t;
                 }
                 if (done) done(out);
             },
             std::move(failed));
}

void LibraryApi::clubRequestCount(std::function<void(const QList<ClubRequestCount>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/club-requests/count"), {},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<ClubRequestCount> out;
                 for (const auto& v : o.value(QLatin1String("clubs")).toArray()) {
                     const QJsonObject co = v.toObject();
                     ClubRequestCount c;
                     c.slug = co.value(QLatin1String("slug")).toString();
                     c.name = co.value(QLatin1String("name")).toString();
                     c.count = intOf(co.value(QLatin1String("count")));
                     if (c.count > 0) out << c;
                 }
                 if (done) done(out);
             },
             std::move(failed));
}

QUrl LibraryApi::clubPartnersWebUrl(const QString& slug) const {
    return url(QStringLiteral("/orgs/%1?tab=partners").arg(idPath(slug)));
}

void LibraryApi::layoutSharedModules(const QString& layoutId, const QString& checkModuleId,
                                     std::function<void(const LayoutSharedModules&)> done, Fail failed) {
    QString path = QStringLiteral("/api/layouts/%1/shared-modules").arg(idPath(layoutId));
    if (!checkModuleId.isEmpty())
        path += QStringLiteral("?check=") + QString::fromLatin1(QUrl::toPercentEncoding(checkModuleId));
    sendJson("GET", path, {},
             [done = std::move(done)](const QJsonObject& o) {
                 if (done) done(layoutSharedModulesFromJson(o));
             },
             std::move(failed));
}

void LibraryApi::sharedModuleVersions(const QString& layoutId, const QString& placedId,
                                      std::function<void(const QList<SharedModuleVersion>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/layouts/%1/shared-modules/%2/versions").arg(idPath(layoutId), idPath(placedId)),
             {},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<SharedModuleVersion> out;
                 for (const auto& v : o.value(QLatin1String("versions")).toArray()) {
                     const QJsonObject vo = v.toObject();
                     SharedModuleVersion ver;
                     ver.version = intOf(vo.value(QLatin1String("version")));
                     ver.note = vo.value(QLatin1String("note")).toString();
                     ver.author = vo.value(QLatin1String("author")).toString();
                     ver.createdAt = timeOf(vo.value(QLatin1String("createdAt")));
                     out << ver;
                 }
                 if (done) done(out);
             },
             std::move(failed));
}

void LibraryApi::sharedModuleVersionSnapshot(const QString& layoutId, const QString& placedId, int version,
                                             std::function<void(const QByteArray&)> done, Fail failed) {
    send("GET",
         QStringLiteral("/api/layouts/%1/shared-modules/%2/versions/%3/snapshot")
             .arg(idPath(layoutId), idPath(placedId))
             .arg(version),
         {}, {},
         [done = std::move(done)](QNetworkReply* r) {
             if (done) done(r->readAll());
         },
         std::move(failed));
}

void LibraryApi::applySharedModule(const QString& layoutId, const QString& placedId, int version, bool again,
                                   std::function<void(const SharedModuleApplied&)> done, Fail failed) {
    QJsonObject body;
    if (version > 0) body.insert(QStringLiteral("version"), version);
    if (again) body.insert(QStringLiteral("again"), true);
    sendJson("POST", QStringLiteral("/api/layouts/%1/shared-modules/%2/apply").arg(idPath(layoutId), idPath(placedId)),
             body,
             [done = std::move(done), placedId](const QJsonObject& o) {
                 SharedModuleApplied a;
                 a.placedId = o.value(QLatin1String("placedId")).toString(placedId);
                 a.changed = o.value(QLatin1String("changed")).toBool(false);
                 a.version = intOf(o.value(QLatin1String("version")));
                 a.from = intOf(o.value(QLatin1String("from")));
                 a.parts = intOf(o.value(QLatin1String("parts")));
                 if (done) done(a);
             },
             std::move(failed));
}

void LibraryApi::applyAllSharedModules(const QString& layoutId, std::function<void(int, int)> done, Fail failed) {
    sendJson("POST", QStringLiteral("/api/layouts/%1/shared-modules/apply-all").arg(idPath(layoutId)), QJsonObject{},
             [done = std::move(done)](const QJsonObject& o) {
                 if (done)
                     done(static_cast<int>(o.value(QLatin1String("updated")).toArray().size()),
                          static_cast<int>(o.value(QLatin1String("skipped")).toArray().size()));
             },
             std::move(failed));
}

void LibraryApi::setSharedModuleUpdates(const QString& layoutId, const QString& updates, std::function<void(const QString&)> done,
                                        Fail failed) {
    sendJson("PUT", QStringLiteral("/api/layouts/%1/shared-modules/updates").arg(idPath(layoutId)),
             QJsonObject{ { QStringLiteral("updates"), updates } },
             [done = std::move(done), updates](const QJsonObject& o) {
                 if (done) done(o.value(QLatin1String("updates")).toString(updates));
             },
             std::move(failed));
}

void LibraryApi::sharedModulesWhileAway(const QString& layoutId, std::function<void(const QList<SharedAwayUpdate>&)> done,
                                        Fail failed) {
    sendJson("POST", QStringLiteral("/api/layouts/%1/shared-modules/away").arg(idPath(layoutId)), QJsonObject{},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<SharedAwayUpdate> out;
                 for (const auto& v : o.value(QLatin1String("updates")).toArray()) {
                     const QJsonObject u = v.toObject();
                     SharedAwayUpdate a;
                     a.placedId = u.value(QLatin1String("placedId")).toString();
                     a.moduleId = u.value(QLatin1String("moduleId")).toString();
                     a.from = intOf(u.value(QLatin1String("from")));
                     a.to = intOf(u.value(QLatin1String("to")));
                     a.version = intOf(u.value(QLatin1String("version")));
                     a.title = u.value(QLatin1String("title")).toString();
                     if (!a.placedId.isEmpty() && a.to > 0) out << a;
                 }
                 if (done) done(out);
             },
             std::move(failed));
}

void LibraryApi::detachSharedModule(const QString& layoutId, const QString& placedId,
                                    std::function<void(bool)> done, Fail failed) {
    sendJson("POST", QStringLiteral("/api/layouts/%1/shared-modules/%2/detach").arg(idPath(layoutId), idPath(placedId)),
             QJsonObject{},
             [done = std::move(done)](const QJsonObject& o) {
                 if (done) done(o.value(QLatin1String("changed")).toBool(false));
             },
             std::move(failed));
}

void LibraryApi::moduleSnapshot(const QString& id, std::function<void(const QByteArray&)> done, Fail failed) {
    send("GET", QStringLiteral("/api/modules/%1/snapshot").arg(idPath(id)), {}, {},
         [done = std::move(done)](QNetworkReply* r) {
             if (done) done(r->readAll());
         },
         std::move(failed));
}

void LibraryApi::moduleSnapshotAt(const QString& id, std::function<void(const QByteArray&, int)> done,
                                  Fail failed) {
    send(
        "GET", QStringLiteral("/api/modules/%1/snapshot").arg(idPath(id)), {}, {},
        [done = std::move(done)](QNetworkReply* r) {
            bool ok = false;
            const int v = QString::fromLatin1(r->rawHeader("X-Doc-Version")).toInt(&ok);
            if (done) done(r->readAll(), ok ? v : -1);
        },
        std::move(failed));
}

void LibraryApi::moduleVersionSnapshot(const QString& id, int version, std::function<void(const QByteArray&)> done,
                                       Fail failed) {
    send("GET", QStringLiteral("/api/modules/%1/versions/%2/snapshot").arg(idPath(id)).arg(version), {}, {},
         [done = std::move(done)](QNetworkReply* r) {
             if (done) done(r->readAll());
         },
         std::move(failed));
}

void LibraryApi::createModule(const QString& title, const QString& orgSlug,
                              std::function<void(const QString&, const QString&)> done, Fail failed) {
    QJsonObject body{ { QStringLiteral("title"), title } };
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/modules"), body, [done = std::move(done), title](const QJsonObject& o) {
        if (done) done(o.value(QLatin1String("id")).toString(), o.value(QLatin1String("title")).toString(title));
    }, std::move(failed));
}

void LibraryApi::saveModuleSnapshot(const QString& id, const QByteArray& snapshot, const QString& note,
                                    std::function<void(int)> done, Fail failed, int baseDocVersion,
                                    std::function<void(int)> savedAs) {
    QString path = QStringLiteral("/api/modules/%1/snapshot").arg(idPath(id));
    const QString trimmed = note.trimmed();
    if (!trimmed.isEmpty())
        path += QStringLiteral("?note=") + QString::fromLatin1(QUrl::toPercentEncoding(trimmed));
    QList<QPair<QByteArray, QByteArray>> headers;
    if (baseDocVersion >= 0)
        headers.append(
            { QByteArrayLiteral("If-Match"), QByteArray("\"") + QByteArray::number(baseDocVersion) + "\"" });
    send(
        "PUT", path, snapshot, QByteArrayLiteral("application/octet-stream"),
        [done = std::move(done), savedAs = std::move(savedAs)](QNetworkReply* r) {
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            if (savedAs && o.contains(QLatin1String("docVersion")))
                savedAs(intOf(o.value(QLatin1String("docVersion"))));
            if (done) done(intOf(o.value(QLatin1String("version"))));
        },
        std::move(failed), headers);
}

void LibraryApi::setModuleThumbnail(const QString& id, const QByteArray& png, std::function<void()> done, Fail failed) {
    // Base64 in JSON: the site's firewall lets octet-stream through on the snapshot routes only.
    sendJson("PUT", QStringLiteral("/api/modules/%1/thumbnail").arg(idPath(id)),
             QJsonObject{ { QStringLiteral("mime"), QStringLiteral("image/png") },
                          { QStringLiteral("data"), QString::fromLatin1(png.toBase64()) } },
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

void LibraryApi::renameModule(const QString& id, const QString& title, std::function<void()> done, Fail failed) {
    sendJson("PATCH", QStringLiteral("/api/modules/%1").arg(idPath(id)), QJsonObject{ { QStringLiteral("title"), title } },
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

void LibraryApi::setModuleSetup(const QString& id, const QJsonObject& setup, std::function<void()> done, Fail failed) {
    sendJson("PATCH", QStringLiteral("/api/modules/%1").arg(idPath(id)),
             QJsonObject{ { QStringLiteral("setup"), setup.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(setup) } },
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

void LibraryApi::deleteModule(const QString& id, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/modules/%1").arg(idPath(id)), {}, {},
         [done = std::move(done)](QNetworkReply*) {
             if (done) done();
         },
         std::move(failed));
}

void LibraryApi::returnToAuthor(const QString& kindPath, const QString& id, bool give,
                                std::function<void(const QString&)> done, Fail failed) {
    sendJson(
        "POST",
        QStringLiteral("/api/%1/%2/%3")
            .arg(kindPath, idPath(id), give ? QStringLiteral("give-back") : QStringLiteral("take-back")),
        QJsonObject(),
        [done = std::move(done)](const QJsonObject& o) {
            if (done) done(o.value(QLatin1String("keptCopyId")).toString());
        },
        std::move(failed));
}

void LibraryApi::picture(const QString& pathAndQuery, std::function<void(const QByteArray&)> done, Fail failed) {
    send("GET", pathAndQuery, {}, {}, [done = std::move(done)](QNetworkReply* r) {
        if (done) done(r->readAll());
    }, std::move(failed));
}

void LibraryApi::catalogSettings(std::function<void(const CatalogSettings&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/settings"), {}, [done = std::move(done)](const QJsonObject& o) {
        CatalogSettings s;
        s.modules = o.value(QLatin1String("modules")).toBool();
        s.parts = o.value(QLatin1String("parts")).toBool();
        s.layouts = o.value(QLatin1String("layouts")).toBool();
        s.venues = o.value(QLatin1String("venues")).toBool();
        s.review = o.value(QLatin1String("review")).toString() != QLatin1String("none");
        if (done) done(s);
    }, std::move(failed));
}

void LibraryApi::catalogItems(const QString& kind, const QString& query,
                              std::function<void(const QList<CatalogItem>&)> done, Fail failed) {
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("kind"), kind);
    if (!query.trimmed().isEmpty()) q.addQueryItem(QStringLiteral("q"), query.trimmed());
    q.addQueryItem(QStringLiteral("sort"), QStringLiteral("popular"));
    sendJson("GET", QStringLiteral("/api/catalog/items?") + q.toString(QUrl::FullyEncoded), {},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<CatalogItem> out;
                 for (const auto& v : o.value(QLatin1String("items")).toArray()) out << catalogItemFromJson(v.toObject());
                 if (done) done(out);
             },
             std::move(failed));
}

void LibraryApi::addCatalogItem(const QString& itemId, const QString& orgSlug,
                                std::function<void(const QString&, const QString&)> done, Fail failed) {
    QJsonObject body;
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/catalog/items/%1/add").arg(idPath(itemId)), body,
             [done = std::move(done)](const QJsonObject& o) {
                 if (done) done(o.value(QLatin1String("kind")).toString(), o.value(QLatin1String("id")).toString());
             },
             std::move(failed));
}

void LibraryApi::venueFile(const QString& id, std::function<void(const QString&, const QByteArray&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/venues/%1").arg(idPath(id)), {}, [done = std::move(done)](const QJsonObject& o) {
        // Kept as the web wrote it: a .bld-venue file without its schema tag.
        QJsonObject file = o.value(QLatin1String("data")).toObject();
        file.insert(QStringLiteral("schema"), QStringLiteral("bld-venue/1"));
        if (done) done(o.value(QLatin1String("name")).toString(), QJsonDocument(file).toJson(QJsonDocument::Indented));
    }, std::move(failed));
}

void LibraryApi::layouts(std::function<void(const QList<LayoutEntry>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/layouts"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<LayoutEntry> out;
        for (const auto& v : o.value(QLatin1String("layouts")).toArray()) out << ServerApi::layoutEntryFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::venues(std::function<void(const QList<VenueEntry>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/venues"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<VenueEntry> out;
        for (const auto& v : o.value(QLatin1String("venues")).toArray()) out << ServerApi::venueEntryFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

RosterStock LibraryApi::rosterStockFromJson(const QJsonObject& o) {
    RosterStock r;
    r.id = o.value(QLatin1String("id")).toString();
    r.number = o.value(QLatin1String("number")).toString();
    r.name = o.value(QLatin1String("name")).toString();
    r.version = std::max(1, intOf(o.value(QLatin1String("version"))));
    r.canManage = o.value(QLatin1String("canManage")).toBool();
    r.owner = ownerOf(o);
    r.record = o.value(QLatin1String("record")).toObject();
    return r;
}

void LibraryApi::rollingStock(std::function<void(const QList<RosterStock>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/rolling-stock"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<RosterStock> out;
        for (const auto& v : o.value(QLatin1String("stock")).toArray()) out << rosterStockFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::createStock(const QJsonObject& record, const QString& orgSlug, std::function<void(const RosterStock&)> done, Fail failed) {
    QJsonObject body{ { QStringLiteral("record"), record } };
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/rolling-stock"), body, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(rosterStockFromJson(o));
    }, std::move(failed));
}

void LibraryApi::updateStock(const QString& id, const QJsonObject& record, std::function<void(const RosterStock&)> done, Fail failed) {
    sendJson("PATCH", QStringLiteral("/api/rolling-stock/%1").arg(idPath(id)), QJsonObject{ { QStringLiteral("record"), record } },
             [done = std::move(done)](const QJsonObject& o) {
                 if (done) done(rosterStockFromJson(o));
             }, std::move(failed));
}

void LibraryApi::deleteStock(const QString& id, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/rolling-stock/%1").arg(idPath(id)), {}, {}, [done = std::move(done)](QNetworkReply*) {
        if (done) done();
    }, std::move(failed));
}

void LibraryApi::customParts(std::function<void(const QList<ServerPart>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/custom-parts"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<ServerPart> out;
        for (const auto& v : o.value(QLatin1String("parts")).toArray()) out << partFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::myCustomParts(std::function<void(const QList<ServerPart>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/custom-parts?owner=me"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<ServerPart> out;
        for (const auto& v : o.value(QLatin1String("parts")).toArray()) {
            ServerPart p = partFromJson(v.toObject());
            // (A server that doesn't narrow the list: a club's part isn't yours.)
            if (!p.owner.isClub()) out << p;
        }
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::replaceCustomPart(const QString& id, const QJsonObject& body, std::function<void()> done,
                                   Fail failed) {
    sendJson("PUT", QStringLiteral("/api/custom-parts/%1").arg(idPath(id)), body, [done = std::move(done)](const QJsonObject&) {
        if (done) done();
    }, std::move(failed));
}

void LibraryApi::layoutFile(const QString& id, bool native, std::function<void(const QByteArray&)> done, Fail failed) {
    send("GET", QStringLiteral("/api/layouts/%1/%2").arg(idPath(id), native ? QStringLiteral("export.bld-layout") : QStringLiteral("export.bbm")),
         {}, {}, [done = std::move(done)](QNetworkReply* r) {
             if (done) done(r->readAll());
         },
         std::move(failed));
}

void LibraryApi::deleteLayout(const QString& id, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/layouts/%1").arg(idPath(id)), {}, {}, [done = std::move(done)](QNetworkReply*) {
        if (done) done();
    }, std::move(failed));
}

void LibraryApi::deleteCustomPart(const QString& id, std::function<void()> done, Fail failed) {
    send(
        "DELETE", QStringLiteral("/api/custom-parts/%1").arg(idPath(id)), {}, {},
        [done = std::move(done)](QNetworkReply*) {
            if (done) done();
        },
        std::move(failed));
}

void LibraryApi::deleteVenue(const QString& id, std::function<void()> done, Fail failed) {
    send("DELETE", QStringLiteral("/api/venues/%1").arg(idPath(id)), {}, {}, [done = std::move(done)](QNetworkReply*) {
        if (done) done();
    }, std::move(failed));
}

void LibraryApi::createVenue(const QString& name, const QJsonObject& data, const QString& orgSlug,
                             std::function<void(const QString&)> done, Fail failed) {
    QJsonObject body{ { QStringLiteral("name"), name }, { QStringLiteral("data"), data } };
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/venues"), body, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(o.value(QLatin1String("id")).toString());
    }, std::move(failed));
}

void LibraryApi::shareToCatalog(const CatalogShare& share, std::function<void(const QString&, const QString&)> done,
                                Fail failed) {
    QJsonObject body{ { QStringLiteral("kind"), share.kind }, { QStringLiteral("sourceId"), share.sourceId },
                      { QStringLiteral("title"), share.title.trimmed() } };
    // An update keeps its description and tags unless new ones are given (as the web sends it).
    if (!share.description.trimmed().isEmpty() || !share.update)
        body.insert(QStringLiteral("description"), share.description.trimmed());
    if (!share.tags.isEmpty() || !share.update) body.insert(QStringLiteral("tags"), QJsonArray::fromStringList(share.tags));
    if (!share.note.trimmed().isEmpty()) body.insert(QStringLiteral("note"), share.note.trimmed());
    if (!share.thumbnailPng.isEmpty())
        body.insert(QStringLiteral("thumbnail"),
                    QJsonObject{ { QStringLiteral("mime"), QStringLiteral("image/png") },
                                 { QStringLiteral("data"), QString::fromLatin1(share.thumbnailPng.toBase64()) } });
    sendJson("POST", QStringLiteral("/api/catalog/submissions"), body, [done = std::move(done)](const QJsonObject& o) {
        if (done) done(o.value(QLatin1String("id")).toString(), o.value(QLatin1String("status")).toString());
    }, std::move(failed));
}

void LibraryApi::myCatalogItems(std::function<void(const QList<MyCatalogItem>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/mine"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<MyCatalogItem> out;
        for (const auto& v : o.value(QLatin1String("items")).toArray()) {
            const QJsonObject i = v.toObject();
            MyCatalogItem m;
            m.id = i.value(QLatin1String("id")).toString();
            m.kind = i.value(QLatin1String("kind")).toString();
            m.sourceId = i.value(QLatin1String("sourceId")).toString();
            m.title = i.value(QLatin1String("title")).toString();
            m.status = i.value(QLatin1String("status")).toString();
            m.reason = i.value(QLatin1String("reason")).toString();
            m.pendingVersion = intOf(i.value(QLatin1String("pendingVersion")));
            out << m;
        }
        if (done) done(out);
    }, std::move(failed));
}

QUrl LibraryApi::catalogItemWebUrl(const QString& id) const { return url(QStringLiteral("/catalog/items/") + idPath(id)); }

void LibraryApi::collections(std::function<void(const QList<CatalogCollection>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/collections"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<CatalogCollection> out;
        for (const auto& v : o.value(QLatin1String("collections")).toArray()) {
            const QJsonObject c = v.toObject();
            CatalogCollection e;
            e.id = c.value(QLatin1String("id")).toString();
            e.title = c.value(QLatin1String("title")).toString();
            e.description = c.value(QLatin1String("description")).toString();
            e.by = c.value(QLatin1String("by")).toString();
            e.featured = c.value(QLatin1String("featured")).toBool();
            e.official = c.value(QLatin1String("official")).toBool();
            e.itemCount = intOf(c.value(QLatin1String("itemCount")));
            e.modules = intOf(c.value(QLatin1String("modules")));
            e.parts = intOf(c.value(QLatin1String("parts")));
            e.coverPath = c.value(QLatin1String("coverUrl")).toString();
            out << e;
        }
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::collectionItems(const QString& id, std::function<void(const QList<CatalogItem>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/catalog/collections/%1").arg(idPath(id)), {},
             [done = std::move(done)](const QJsonObject& o) {
                 QList<CatalogItem> out;
                 for (const auto& v : o.value(QLatin1String("items")).toArray()) out << catalogItemFromJson(v.toObject());
                 if (done) done(out);
             },
             std::move(failed));
}

void LibraryApi::addCollection(const QString& id, const QString& orgSlug,
                               std::function<void(const CollectionAddResult&)> done, Fail failed) {
    QJsonObject body;
    if (!orgSlug.isEmpty()) body.insert(QStringLiteral("orgSlug"), orgSlug);
    sendJson("POST", QStringLiteral("/api/catalog/collections/%1/add").arg(idPath(id)), body,
             [done = std::move(done)](const QJsonObject& o) {
                 CollectionAddResult r;
                 r.added = static_cast<int>(o.value(QLatin1String("added")).toArray().size());
                 r.skipped = static_cast<int>(o.value(QLatin1String("skipped")).toArray().size());
                 r.failed = static_cast<int>(o.value(QLatin1String("failed")).toArray().size());
                 if (done) done(r);
             },
             std::move(failed));
}

void LibraryApi::orgs(std::function<void(const QList<OrgEntry>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/orgs"), {}, [this, done = std::move(done)](const QJsonObject& o) {
        QList<OrgEntry> out;
        for (const auto& v : o.value(QLatin1String("orgs")).toArray()) {
            out << OrgEntry::fromJson(v.toObject());
        }
        // Their logos, for every window that shows clubs.
        ClubLogos::shared().update(base_, token_, out);
        if (done) done(out);
    }, std::move(failed));
}

void LibraryApi::notices(std::function<void(const QList<Notice>&)> done, Fail failed) {
    sendJson("GET", QStringLiteral("/api/notices"), {}, [done = std::move(done)](const QJsonObject& o) {
        QList<Notice> out;
        for (const auto& v : o.value(QLatin1String("notices")).toArray()) out << noticeFromJson(v.toObject());
        if (done) done(out);
    }, std::move(failed));
}

QStringList ClubMembers::roleNames(const ClubMember& m) const {
    QStringList out;
    for (const ClubRoleName& r : roles)
        if (m.roleIds.contains(r.id)) out << r.name;
    return out;
}

QList<ClubMember> ClubMembers::sorted() const {
    struct Rank {
        bool admin = false;
        qsizetype permissions = 0;
    };
    const auto rankOf = [this](const ClubMember& m) {
        Rank rank;
        QSet<QString> granted;
        for (const ClubRoleName& r : roles) {
            if (!m.roleIds.contains(r.id)) continue;
            rank.admin = rank.admin || r.key == QLatin1String("admin");
            for (const QString& p : r.permissions) granted.insert(p);
        }
        rank.permissions = granted.size();
        return rank;
    };
    QList<ClubMember> out = members;
    std::stable_sort(out.begin(), out.end(), [&rankOf](const ClubMember& a, const ClubMember& b) {
        const Rank ra = rankOf(a);
        const Rank rb = rankOf(b);
        if (ra.admin != rb.admin) return ra.admin;
        if (ra.permissions != rb.permissions) return ra.permissions > rb.permissions;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return out;
}

ClubMembers LibraryApi::clubMembersFromJson(const QJsonObject& o) {
    ClubMembers out;
    for (const auto& v : o.value(QLatin1String("roles")).toArray()) {
        const QJsonObject r = v.toObject();
        ClubRoleName role{ r.value(QLatin1String("id")).toString(),
                           r.value(QLatin1String("name")).toString(),
                           r.value(QLatin1String("key")).toString(),
                           {} };
        for (const auto& p : r.value(QLatin1String("permissions")).toArray())
            role.permissions << p.toString();
        out.roles << role;
    }
    for (const auto& v : o.value(QLatin1String("members")).toArray()) {
        const QJsonObject m = v.toObject();
        ClubMember member;
        member.userId = m.value(QLatin1String("userId")).toString();
        member.name = m.value(QLatin1String("displayName")).toString();
        for (const auto& id : m.value(QLatin1String("roleIds")).toArray()) member.roleIds << id.toString();
        // A server before club roles names one fixed role.
        if (member.roleIds.isEmpty()) {
            const QString role = m.value(QLatin1String("role")).toString();
            for (const ClubRoleName& r : out.roles)
                if (r.id.endsWith(QLatin1Char(':') + role)) member.roleIds << r.id;
        }
        const QString last = m.value(QLatin1String("lastActive")).toString();
        static const QStringList kBuckets{ QStringLiteral("today"), QStringLiteral("week"),
                                           QStringLiteral("month"), QStringLiteral("older") };
        if (kBuckets.contains(last)) member.lastActive = last;
        member.joinedAt = timeOf(m.value(QLatin1String("joinedAt")));
        out.members << member;
    }
    return out;
}

void LibraryApi::clubMembers(const QString& slug, std::function<void(const ClubMembers&)> done, Fail failed) {
    sendJson(
        "GET", QStringLiteral("/api/orgs/%1/members").arg(idPath(slug)), {},
        [done = std::move(done)](const QJsonObject& o) {
            if (done) done(clubMembersFromJson(o));
        },
        std::move(failed));
}

void LibraryApi::acknowledge(const QString& id, std::function<void()> done, Fail failed) {
    sendJson("POST", QStringLiteral("/api/notices/%1/acknowledge").arg(idPath(id)), {},
             [done = std::move(done)](const QJsonObject&) {
                 if (done) done();
             },
             std::move(failed));
}

}  // namespace bld::sync
