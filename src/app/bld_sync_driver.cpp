// Headless live-sync client for end-to-end tests against a real server
// (the web repo's e2e/test/desktopLiveSync.spec.ts). It holds one
// SyncSession, as the desktop editor does, and is driven by a line-based
// script on stdin; every command answers with exactly one line on stdout,
// "ok[ <result>]" or "error <message>".
//
//   bld_sync_driver <server url>        e.g. http://localhost:3000
//   token in BLD_SYNC_TOKEN (a bld_pat_ API token)
//
// Commands:
//   open <layoutId>            connect to the layout's socket
//   wait-synced [ms]           until connected, in step and loaded
//   status                     Offline | Connecting | Syncing | Synced, then offline edits
//   move <brickId> <dx> <dy>   move a brick (studs), as one edit
//   rotate <brickId> <deg>     turn a brick about its centre, as one edit
//   undo | redo                this desktop's own edits only; ok true|false
//   name <display name>        who we are in presence
//   cursor <x> <y>             publish our cursor (studs)
//   disconnect | reconnect     close the connection / open it again
//   resolve mine|server|merge-default
//                              resolve offline edits: clashes go to my side,
//                              the server's, or the merge default (server)
//   dump [shared]              the editor's bricks as JSON (shared: the shared layout)
//   modules                    the editor's modules as JSON: id, name, members
//   module-add <moduleId> <brickId>
//                              add a part to a module, as one edit
//   module-remove <moduleId> <brickId>
//                              take a part out of a module, as one edit
//   module-rename <moduleId> <name>
//                              rename a module, as one edit
//   module-move <moduleId> <dx> <dy>
//                              move a module's parts together (studs), as one edit
//   brick-delete <brickId>     delete a part, as one edit
//   switches                   the editor's switch settings as JSON ("<brick>/<heel>": exit)
//   routes                     the editor's named routes as JSON: id, name, steps, and
//                              broken (true when its steps no longer join up, worked out
//                              from links alone: no parts library here)
//   switch-set <brick>/<heel> <exit|default>
//                              set a switch (default: back to its usual way), as one edit
//   trains                     the editor's rolling stock and trains as JSON:
//                              {rollingStock: [...], trains: [...]}, as the sidecar keeps them
//   places                     the editor's places as JSON: {layout: [...], modules: [{id, name, places}]}
//   place-set <moduleId|layout> <place JSON>
//                              add or change a place, as one edit
//   place-module <name> <brickId>
//                              make a module of one part (its places go with it), as one edit
//   stock-add <number> <length>
//                              add a quick car (a new id), as one edit; ok <id>
//   (the next two read the parts library at PARTS_DIR, as the server does)
//   cars-place <stockId>... <x> <y>
//                              put cars on the track at a spot (studs), coupled, as one edit
//   car-drag <stockId> <x> <y> drag a car along the track to a spot (studs), as one edit
//   car-load <stockId> <load>  set a car's load (loaded or empty), as one edit
//   ops                        the editor's waybills and jobs as JSON: {waybills: [...], opsJobs: [...]}
//   ops-lists                  each job's switch list, worked out here, as JSON (reads PARTS_DIR too):
//                              [{job, stops: [{place, setOut: [{car, spot}], pickUp: [{car, to}]}]}]
//   waybill-add <stockId|-> <placeId> <load>
//                              a waybill of one leg, for a car or (-) none, as one edit; ok <id>
//   waybill-turn <stockId>     delivered: the car's waybill turns, as one edit
//   job-add <name> <placeId>...  a job and its stops, as one edit; ok <id>
//   job-stop <jobId> <placeId> a stop added at its end, as one edit
//   train-rename <id> <name>   rename a train, as one edit
//   stock-add-model <number> <partKey> <length> <front> <rear>
//                              add an engine drawn with a part (an imported model's), as
//                              Trains › Import a model… makes one, as one edit; ok <id>
//   stock-part <stockId>       the server's parts downloaded as the app does (PartsSync),
//                              then the car's picture looked up: ok {partKey, found,
//                              partNumber, pxPerStud, width, height} (pixels)
//   shared                     the editor's shared (read-only) modules as JSON: id,
//                              moduleId, version, by, members, placement, review
//   shared-rows                what the server says about them (GET
//                              /api/layouts/:id/shared-modules), as JSON
//   shared-apply <moduleId> [version] [again]
//                              the server puts a version in (the newest when
//                              none is named): Update, Go back, Put back
//   library-shared             Shared with me (GET /api/modules?owner=shared) as the
//                              library reads it: id, title, owner, fromAnotherClub, clubs
//   club-shares <moduleId>     a module's Share dialog: canShareWithOtherClubs, club,
//                              partners, offers, clubs (with via)
//   club-share <moduleId> <club slug or link>
//                              a club's module to another club: shared at once with a
//                              partner, else offered; ok {status, name, slug}
//   club-offer-withdraw <moduleId> <offerId>
//   club-requests              what waits in the clubs we look after
//   shared-detach <moduleId>   "Make it mine", the server's step
//   notif-watch                the bell for this server, live (NotificationCenter):
//                              ok {unread, waiting, warnings, supported}
//   notif-count                the bell's counts now, as notif-watch says them
//   notif-wait-unread <n> [ms] until the bell reaches n, from live hints alone
//   notif-list                 the newest notifications: id, type, title, read,
//                              outcome, actions
//   notif-act <id> accept|decline
//                              answer it in the app: ok {outcome, already, layoutId}
//
// Edits go through the app's lock on shared modules (edit/SharedLock.h), as
// MapView's do: one that would change a shared module's parts, name or
// members answers "error locked: <why>" and nothing is sent.
//   peers                      everyone else's presence as JSON
//   quit

#include "sync/LayoutMerge.h"
#include "sync/LibraryApi.h"
#include "sync/PartsSync.h"
#include "sync/NotificationCenter.h"
#include "core/SharedModule.h"
#include "edit/CutCommands.h"
#include "edit/OpsCommands.h"
#include "edit/ModuleCommands.h"
#include "edit/PlaceCommands.h"
#include "edit/SharedLock.h"
#include "parts/PartsLibrary.h"
#include "track/Cuts.h"
#include "track/OpsView.h"
#include "track/Routes.h"
#include "edit/SharedSettle.h"
#include "sync/Presence.h"
#include "sync/ServerApi.h"
#include "sync/SidecarItems.h"
#include "sync/SyncSession.h"
#include "saveload/SidecarIO.h"

#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Places.h"

#include <QDateTime>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

using namespace bld;
using Status = sync::SyncClient::Status;

namespace {

// Reads stdin on its own thread so the event loop keeps the socket alive
// between commands.
class StdinReader : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;
signals:
    void line(const QString& text);
    void closed();

protected:
    void run() override {
        std::string s;
        while (std::getline(std::cin, s)) emit line(QString::fromStdString(s).trimmed());
        emit closed();
    }
};

void reply(const QString& text) {
    std::fputs(text.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Spin the event loop until `done` or `ms` pass.
bool waitFor(const std::function<bool()>& done, int ms) {
    const qint64 until = QDateTime::currentMSecsSinceEpoch() + ms;
    while (!done()) {
        if (QDateTime::currentMSecsSinceEpoch() > until) return false;
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

const char* statusName(Status s) {
    switch (s) {
    case Status::Offline: return "Offline";
    case Status::Connecting: return "Connecting";
    case Status::Syncing: return "Syncing";
    case Status::Synced: return "Synced";
    }
    return "?";
}

core::Brick* findBrick(core::Map& map, const QString& id) {
    for (auto& layer : map.layers())
        if (layer->kind() == core::LayerKind::Brick)
            for (auto& b : static_cast<core::LayerBrick&>(*layer).bricks)
                if (b.guid == id) return &b;
    return nullptr;
}

QJsonArray bricksJson(const core::Map& map) {
    QJsonArray out;
    for (const auto& layer : map.layers())
        if (layer->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
                out.append(QJsonObject{ { QStringLiteral("id"), b.guid },
                                        { QStringLiteral("layer"), layer->guid },
                                        { QStringLiteral("part"), b.partNumber },
                                        { QStringLiteral("x"), b.displayArea.x() },
                                        { QStringLiteral("y"), b.displayArea.y() },
                                        { QStringLiteral("width"), b.displayArea.width() },
                                        { QStringLiteral("height"), b.displayArea.height() },
                                        { QStringLiteral("orientation"), b.orientation } });
    return out;
}

class Driver {
public:
    explicit Driver(QUrl server) : server_(std::move(server)) {}

    // Whether the server lists the feature for module part keys
    // (GET /api/version). No answer in time: it doesn't.
    bool serverReadsMemberKeys() { return serverHas(QLatin1String(sync::kMemberKeysFeature)); }
    // ...and the one for shared, read-only modules (doc shape 4).
    bool serverKnowsSharedModules() { return serverHas(QLatin1String(sync::kSharedModulesFeature)); }
    bool serverHas(QLatin1String feature) {
        sync::ServerApi api;
        api.setBase(server_);
        bool answered = false, reads = false;
        QObject::connect(&api, &sync::ServerApi::versionReady, &api, [&](const sync::ServerInfo& info) {
            answered = true;
            reads = info.features && info.features->contains(feature);
        });
        api.fetchVersion();
        waitFor([&] { return answered; }, 10000);
        return reads;
    }

    // One command; false to quit.
    bool run(const QString& line) {
        const QStringList a = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (a.isEmpty()) return true;
        const QString& cmd = a[0];
        if (cmd == QLatin1String("quit")) {
            session_.close();
            reply(QStringLiteral("ok"));
            return false;
        }
        if (cmd == QLatin1String("open") && a.size() == 2) {
            layoutId_ = a[1];
            socket_ = server_;
            socket_.setScheme(server_.scheme() == QLatin1String("https") ? QStringLiteral("wss") : QStringLiteral("ws"));
            socket_.setPath(QStringLiteral("/ws/layout/") + layoutId_);
            session_.setCacheDir(cache_.path());
            // As the app does when it opens a live layout (MainWindow::openLive):
            // a module's part changes are written one key each only where the
            // server's web app reads them (docs/module-members.md).
            session_.setMemberKeys(serverReadsMemberKeys());
            session_.setSharedModules(serverKnowsSharedModules());
            session_.setKeepIds(serverHas(QLatin1String(sync::kSharedKeepIdsFeature)));
            // An undo that would pull a shared module apart is finished for the whole module (no parts library here: by boxes).
            session_.setStepFinisher([](const core::Map& before, core::Map& after, core::SettledBack* removed) {
                static parts::PartsLibrary none;
                return edit::finishShared(before, after, none, removed);
            });
            session_.open(socket_, token_, false);
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("wait-synced")) {
            const int ms = a.size() > 1 ? a[1].toInt() : 15000;
            reply(waitFor([&] { return session_.status() == Status::Synced && session_.loaded(); }, ms)
                      ? QStringLiteral("ok")
                      : QStringLiteral("error not synced (%1)").arg(QLatin1String(statusName(session_.status()))));
        } else if (cmd == QLatin1String("features")) {
            // What this build does that older ones don't (the web's journeys ask, to know what to expect of it).
            reply(QStringLiteral("ok keepIds stockModels"));
        } else if (cmd == QLatin1String("status")) {
            reply(QStringLiteral("ok %1 %2").arg(QLatin1String(statusName(session_.status()))).arg(session_.unsyncedEdits()));
        } else if (cmd == QLatin1String("move") && a.size() == 4) {
            edit(a[1], [&](core::Brick& b) { b.displayArea.translate(a[2].toDouble(), a[3].toDouble()); });
        } else if (cmd == QLatin1String("rotate") && a.size() == 3) {
            edit(a[1], [&](core::Brick& b) {
                const float deg = a[2].toFloat();
                b.orientation = std::fmod(b.orientation + deg + 360.0f, 360.0f);
                // A quarter turn swaps the box about its centre, as the editor
                // does for a plain rectangular part (no parts library here).
                if (std::fmod(std::abs(deg), 180.0f) == 90.0f) {
                    const QPointF c = b.displayArea.center();
                    b.displayArea.setSize(b.displayArea.size().transposed());
                    b.displayArea.moveCenter(c);
                }
            });
        } else if (cmd == QLatin1String("undo")) {
            reply(session_.undo() ? QStringLiteral("ok true") : QStringLiteral("ok false"));
        } else if (cmd == QLatin1String("redo")) {
            reply(session_.redo() ? QStringLiteral("ok true") : QStringLiteral("ok false"));
        } else if (cmd == QLatin1String("name") && a.size() > 1) {
            name_ = a.mid(1).join(QLatin1Char(' '));
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("cursor") && a.size() == 3) {
            const sync::presence::User user{ QStringLiteral("desktop-driver"), name_,
                                             sync::presence::colorFor(QStringLiteral("desktop-driver"), layoutId_) };
            session_.setPresence(sync::presence::state(user, QPointF(a[1].toDouble(), a[2].toDouble()), {},
                                                       QDateTime::currentMSecsSinceEpoch()));
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("disconnect")) {
            session_.close();
            reply(waitFor([&] { return session_.status() == Status::Offline; }, 5000) ? QStringLiteral("ok")
                                                                                      : QStringLiteral("error still connected"));
        } else if (cmd == QLatin1String("reconnect")) {
            session_.open(socket_, token_, false);
            reply(QStringLiteral("ok"));
        } else if (cmd == QLatin1String("resolve") && a.size() == 2) {
            resolve(a[1]);
        } else if (cmd == QLatin1String("dump")) {
            QString error;
            const bool shared = a.size() > 1 && a[1] == QLatin1String("shared");
            const auto map = shared ? session_.currentMap(&error) : session_.editorMap(&error);
            reply(map ? QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(bricksJson(*map)).toJson(QJsonDocument::Compact))
                      : QStringLiteral("error ") + error);
        } else if (cmd == QLatin1String("modules")) {
            QString error;
            const auto map = session_.editorMap(&error);
            if (!map) {
                reply(QStringLiteral("error ") + error);
                return true;
            }
            QJsonArray out;
            for (const auto& m : map->sidecar.modules) {
                QStringList members(m.memberIds.begin(), m.memberIds.end());
                members.sort();
                out.append(QJsonObject{ { QStringLiteral("id"), m.id },
                                        { QStringLiteral("name"), m.name },
                                        { QStringLiteral("members"), QJsonArray::fromStringList(members) } });
            }
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("module-add") && a.size() == 3) {
            editModule(a[1], [&](core::Module& m, core::Map&) { m.memberIds.insert(a[2]); });
        } else if (cmd == QLatin1String("module-remove") && a.size() == 3) {
            editModule(a[1], [&](core::Module& m, core::Map&) { m.memberIds.remove(a[2]); });
        } else if (cmd == QLatin1String("module-rename") && a.size() > 2) {
            editModule(a[1], [&](core::Module& m, core::Map&) { m.name = a.mid(2).join(QLatin1Char(' ')); });
        } else if (cmd == QLatin1String("module-move") && a.size() == 4) {
            const QPointF by(a[2].toDouble(), a[3].toDouble());
            editModule(a[1], [&](core::Module& m, core::Map& map) {
                for (auto& l : map.layers())
                    if (l->kind() == core::LayerKind::Brick)
                        for (auto& b : static_cast<core::LayerBrick&>(*l).bricks)
                            if (m.memberIds.contains(b.guid)) b.displayArea.translate(by);
            });
        } else if (cmd == QLatin1String("switches")) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(saveload::switchStatesToJson(map->sidecar.switchStates)).toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("routes")) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            // Which connection id is where, to see whether the steps still join.
            QHash<QString, QPair<QString, int>> owner;
            QHash<QString, QStringList> linkOf;  // brick -> each connection's partner id
            for (const auto& l : map->layers())
                if (l->kind() == core::LayerKind::Brick)
                    for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) {
                        QStringList links;
                        for (int i = 0; i < static_cast<int>(b.connections.size()); ++i) {
                            owner.insert(b.connections[i].guid, { b.guid, i });
                            links << b.connections[i].linkedToId;
                        }
                        linkOf.insert(b.guid, links);
                    }
            QJsonArray out;
            for (const auto& r : map->sidecar.routes) {
                bool broken = r.steps.empty();
                for (size_t i = 0; i < r.steps.size() && !broken; ++i) {
                    const auto& st = r.steps[i];
                    if (!linkOf.contains(st.brick)) broken = true;
                    else if (i + 1 < r.steps.size()) {
                        const QStringList& links = linkOf.value(st.brick);
                        const auto p = st.to < links.size() ? owner.value(links[st.to]) : QPair<QString, int>();
                        broken = p.first != r.steps[i + 1].brick || p.second != r.steps[i + 1].from;
                    }
                }
                QJsonObject o = saveload::routeToJson(r);
                o.insert(QStringLiteral("broken"), broken);
                out.append(o);
            }
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("switch-set") && a.size() == 3) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            bool ok = true;
            const int exit = a[2] == QLatin1String("default") ? -1 : a[2].toInt(&ok);
            if (!ok || !a[1].contains(QLatin1Char('/'))) return reply(QStringLiteral("error switch-set <brick>/<heel> <exit|default>")), true;
            // (as SetSwitchStatesCommand: a flip is a sidecar change, never a part edit)
            send(*map, [&] {
                if (exit < 0) map->sidecar.switchStates.remove(a[1]);
                else map->sidecar.switchStates.insert(a[1], exit);
            });
        } else if (cmd == QLatin1String("places")) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            QJsonArray modules;
            for (const auto& m : map->sidecar.modules)
                modules.append(QJsonObject{ { QStringLiteral("id"), m.id }, { QStringLiteral("name"), m.name }, { QStringLiteral("places"), m.places } });
            reply(QStringLiteral("ok ")
                  + QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("layout"), map->sidecar.layoutPlaces }, { QStringLiteral("modules"), modules } })
                                          .toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("place-set") && a.size() >= 3) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            const auto place = core::places::readPlace(QJsonDocument::fromJson(a.mid(2).join(QLatin1Char(' ')).toUtf8()).object());
            if (!place) return reply(QStringLiteral("error not a place")), true;
            edit::SetPlaceCommand step(*map, a[1] == QLatin1String("layout") ? QString() : a[1],
                                       place->value(QLatin1String("id")).toString(), place, QString());
            send(*map, [&] { step.redo(); });
        } else if (cmd == QLatin1String("place-module") && a.size() == 3) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            edit::CreateModuleCommand step(*map, a[1], { { 0, a[2] } });
            send(*map, [&] { step.redo(); }, step.moduleId());
        } else if (cmd == QLatin1String("trains")) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            QJsonArray stock, trains;
            for (const auto& r : map->sidecar.rollingStock) stock.append(saveload::rollingStockToJson(r));
            for (const auto& t : map->sidecar.trains) trains.append(saveload::trainToJson(t));
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("rollingStock"), stock }, { QStringLiteral("trains"), trains } })
                                                               .toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("stock-add") && a.size() == 3) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            bool ok = false;
            const double length = a[2].toDouble(&ok);
            if (!ok || length <= 0) return reply(QStringLiteral("error stock-add <number> <length>")), true;
            core::RollingStock car = core::simpleStock(core::newBbmId(), length);
            car.number = a[1];
            send(*map, [&] {
                auto& list = map->sidecar.rollingStock;
                list.insert(std::find_if(list.begin(), list.end(), [&](const core::RollingStock& x) { return car.id < x.id; }), car);
            }, car.id);
        } else if (cmd == QLatin1String("stock-add-model") && a.size() == 6) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            bool ok1 = false, ok2 = false, ok3 = false;
            const double length = a[3].toDouble(&ok1), front = a[4].toDouble(&ok2), rear = a[5].toDouble(&ok3);
            if (!ok1 || !ok2 || !ok3 || length <= 0)
                return reply(QStringLiteral("error stock-add-model <number> <partKey> <length> <front> <rear>")), true;
            core::RollingStock car = core::simpleStock(core::newBbmId(), length);
            car.number = a[1];
            car.kind = QStringLiteral("engine");
            car.powered = true;
            car.trucks = { { front, 30.0 }, { rear, 30.0 } };
            car.lookMode = QStringLiteral("model");
            car.partKey = a[2];
            car.extras.insert(QStringLiteral("model"), QJsonObject{ { QStringLiteral("partKey"), a[2] },
                                                                    { QStringLiteral("file"), QStringLiteral("driver.ldr") },
                                                                    { QStringLiteral("format"), QStringLiteral("ldraw") } });
            send(*map, [&] {
                auto& list = map->sidecar.rollingStock;
                list.insert(std::find_if(list.begin(), list.end(), [&](const core::RollingStock& x) { return car.id < x.id; }), car);
            }, car.id);
        } else if (cmd == QLatin1String("stock-part") && a.size() == 2) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            const auto it = std::find_if(map->sidecar.rollingStock.begin(), map->sidecar.rollingStock.end(),
                                         [&](const core::RollingStock& x) { return x.id == a[1]; });
            if (it == map->sidecar.rollingStock.end()) return reply(QStringLiteral("error no such car")), true;
            const QString partKey = it->partKey;
            const QString dir = cache_.filePath(QStringLiteral("server-parts"));
            sync::PartsSync job(server_, token_, dir);
            bool done = false;
            QString failed;
            QObject::connect(&job, &sync::PartsSync::finished, [&] { done = true; });
            QObject::connect(&job, &sync::PartsSync::failed, [&](const QString& m, bool) {
                failed = m;
                done = true;
            });
            job.start();
            if (!waitFor([&] { return done; }, 60000) || !failed.isEmpty())
                return reply(QStringLiteral("error parts sync: ") + (failed.isEmpty() ? QStringLiteral("timed out") : failed)), true;
            parts::PartsLibrary lib;
            lib.addSearchPath(dir);
            lib.scan();
            const auto meta = lib.metadata(partKey);
            const QPixmap pm = lib.pixmap(partKey);
            QJsonObject out{ { QStringLiteral("partKey"), partKey }, { QStringLiteral("found"), meta.has_value() && !pm.isNull() } };
            if (meta) {
                out.insert(QStringLiteral("partNumber"), meta->partNumber);
                out.insert(QStringLiteral("pxPerStud"), meta->pxPerStud);
            }
            out.insert(QStringLiteral("width"), pm.width());
            out.insert(QStringLiteral("height"), pm.height());
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        } else if ((cmd == QLatin1String("car-drag") || cmd == QLatin1String("cars-place")) && a.size() >= 4) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            parts::PartsLibrary lib;
            lib.addSearchPath(qEnvironmentVariable("PARTS_DIR"));
            lib.scan();
            const auto stock = track::stockById(map->sidecar);
            const auto trains = track::settleTrains(map->sidecar.trains, stock);
            const auto net = track::trackNetworkOf(*map, lib);
            const auto& states = map->sidecar.switchStates;
            const QPointF at(a[a.size() - 2].toDouble(), a[a.size() - 1].toDouble());
            track::CutChange change;
            if (cmd == QLatin1String("cars-place")) {
                const auto pos = track::nearestPosition(net, at, 3.0);
                if (!pos) return reply(QStringLiteral("error no track there")), true;
                core::TrainPosition p;
                static_cast<core::TrackPosition&>(p) = *pos;
                // Facing the way they fit, as the app does.
                std::vector<core::RollingStock> cars;
                for (const QString& c : a.mid(1, a.size() - 3)) cars.push_back(stock.value(c));
                const auto fits = track::placeTrain(net, track::newCut(QString(), cars, p), stock, states);
                p.facingTo = fits && !fits->offTrack;
                change = track::putCarsOnTrack(net, trains, stock, states, a.mid(1, a.size() - 3), p);
            } else {
                const core::Train* t = track::standingIn(a[1], trains);
                const auto placed = t ? track::placeTrain(net, *t, stock, states) : std::nullopt;
                if (!placed) return reply(QStringLiteral("error it isn't on the track")), true;
                int unit = -1;
                for (size_t i = 0; i < placed->units.size(); ++i)
                    if (placed->units[i].stock == a[1]) unit = static_cast<int>(i);
                const auto drag = track::startDrag(net, trains, stock, states, t->id, unit, core::newBbmId());
                const auto move = drag ? drag->to(at) : std::nullopt;
                if (!move) return reply(QStringLiteral("error it doesn't move")), true;
                change = move->change;
            }
            send(*map, [&] { edit::CutCommand(*map, change, QString()).redo(); });
        } else if (cmd == QLatin1String("car-load") && a.size() == 3) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            send(*map, [&] {
                for (auto& s : map->sidecar.rollingStock)
                    if (s.id == a[1]) s.opsLoad = a[2];
            });
        } else if (cmd == QLatin1String("ops")) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            reply(QStringLiteral("ok ")
                  + QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("waybills"), map->sidecar.waybills }, { QStringLiteral("opsJobs"), map->sidecar.opsJobs } })
                                          .toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("ops-lists")) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            parts::PartsLibrary lib;
            lib.addSearchPath(qEnvironmentVariable("PARTS_DIR"));
            lib.scan();
            const auto net = track::trackNetworkOf(*map, lib);
            QJsonArray out;
            for (const auto& [job, list] : track::opsView(*map, net, track::trackRuns(net)).routing.lists) {
                QJsonArray stops;
                for (const auto& s : list) {
                    QJsonArray setOut, pickUp;
                    for (const auto& m : s.setOut) setOut.append(QJsonObject{ { QStringLiteral("car"), m.car }, { QStringLiteral("spot"), m.spot } });
                    for (const auto& m : s.pickUp) pickUp.append(QJsonObject{ { QStringLiteral("car"), m.car }, { QStringLiteral("to"), m.to } });
                    stops.append(QJsonObject{ { QStringLiteral("place"), s.place }, { QStringLiteral("setOut"), setOut }, { QStringLiteral("pickUp"), pickUp } });
                }
                out.append(QJsonObject{ { QStringLiteral("job"), job }, { QStringLiteral("stops"), stops } });
            }
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("waybill-add") && a.size() == 4) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            const QString id = core::newBbmId();
            edit::OpsCommand step(*map, edit::newWaybill(*map, id, a[1] == QLatin1String("-") ? QString() : a[1], { { a[2], QString(), a[3], QString() } }), QString());
            send(*map, [&] { step.redo(); }, id);
        } else if (cmd == QLatin1String("waybill-turn") && a.size() == 2) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            edit::OpsCommand step(*map, edit::turnedCar(*map, a[1]), QString());
            send(*map, [&] { step.redo(); });
        } else if (cmd == QLatin1String("job-add") && a.size() >= 2) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            const QString id = core::newBbmId();
            send(*map, [&] {
                edit::OpsCommand(*map, edit::newJob(*map, id, QStringLiteral("Job %1")), QString()).redo();
                edit::OpsCommand(*map, edit::changedJob(*map, id, { { QStringLiteral("name"), a[1] } }), QString()).redo();
                for (const QString& place : a.mid(2)) edit::OpsCommand(*map, edit::addedStop(*map, id, place), QString()).redo();
            }, id);
        } else if (cmd == QLatin1String("job-stop") && a.size() == 3) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            edit::OpsCommand step(*map, edit::addedStop(*map, a[1], a[2]), QString());
            send(*map, [&] { step.redo(); });
        } else if (cmd == QLatin1String("train-rename") && a.size() >= 3) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            const QString name = QStringList(a.mid(2)).join(QLatin1Char(' '));
            send(*map, [&] {
                for (auto& t : map->sidecar.trains)
                    if (t.id == a[1]) t.name = name;
            });
        } else if (cmd == QLatin1String("brick-delete") && a.size() == 2) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            if (!findBrick(*map, a[1])) return reply(QStringLiteral("error no brick ") + a[1]), true;
            send(*map, [&] {
                for (auto& l : map->layers())
                    if (l->kind() == core::LayerKind::Brick) {
                        auto& bricks = static_cast<core::LayerBrick&>(*l).bricks;
                        std::erase_if(bricks, [&](const core::Brick& b) { return b.guid == a[1]; });
                    }
                // (as DeleteBricksCommand: a deleted part leaves its module's list)
                for (auto& m : map->sidecar.modules) m.memberIds.remove(a[1]);
            });
        } else if (cmd == QLatin1String("shared")) {
            QString error;
            auto map = session_.editorMap(&error);
            if (!map) return reply(QStringLiteral("error ") + error), true;
            QJsonArray out;
            for (const auto& s : core::sharedModulesIn(map->sidecar.modules)) {
                QStringList members(s.members.begin(), s.members.end());
                members.sort();
                QJsonObject o{ { QStringLiteral("id"), s.placedId },       { QStringLiteral("moduleId"), s.moduleId },
                               { QStringLiteral("version"), s.version },   { QStringLiteral("by"), s.by },
                               { QStringLiteral("name"), s.name },         { QStringLiteral("members"), QJsonArray::fromStringList(members) },
                               { QStringLiteral("placement"), s.placement ? QJsonValue(core::placementJson(*s.placement)) : QJsonValue() },
                               { QStringLiteral("review"),
                                 s.review ? QJsonValue(QJsonObject{ { QStringLiteral("from"), s.review->from }, { QStringLiteral("to"), s.review->to } })
                                          : QJsonValue() } };
                out.append(o);
            }
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        } else if (cmd == QLatin1String("shared-rows")) {
            api().layoutSharedModules(layoutId_, QString(), [this](const sync::LayoutSharedModules& rows) {
                QJsonArray list;
                for (const auto& m : rows.modules)
                    list.append(QJsonObject{ { QStringLiteral("placedId"), m.placedId }, { QStringLiteral("state"), m.state },
                                             { QStringLiteral("version"), m.version }, { QStringLiteral("latest"), m.latest },
                                             { QStringLiteral("latestNote"), m.latestNote }, { QStringLiteral("owner"), m.ownerName } });
                answer(QStringLiteral("ok ")
                       + QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("canUpdate"), rows.canUpdate },
                                                                      { QStringLiteral("canDetach"), rows.canDetach },
                                                                      { QStringLiteral("modules"), list } })
                                               .toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("shared-apply") && a.size() >= 2) {
            const bool again = a.size() > 3 && a[3] == QLatin1String("again");
            api().applySharedModule(layoutId_, a[1], a.size() > 2 ? a[2].toInt() : 0, again, [this](const sync::SharedModuleApplied& r) {
                answer(QStringLiteral("ok ")
                       + QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("changed"), r.changed },
                                                                      { QStringLiteral("version"), r.version },
                                                                      { QStringLiteral("from"), r.from } })
                                               .toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("shared-detach") && a.size() == 2) {
            api().detachSharedModule(layoutId_, a[1], [this](bool changed) {
                answer(changed ? QStringLiteral("ok true") : QStringLiteral("ok false"));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("library-shared")) {
            api().sharedWithMe([this](const QList<sync::ServerModule>& list) {
                QJsonArray out;
                for (const auto& m : list) {
                    QJsonArray clubs;
                    for (const auto& c : m.sharedClubs) clubs.append(c.name);
                    const bool fromClub = m.owner.isClub() && !m.sharedDirect && !m.sharedClubs.isEmpty();
                    out.append(QJsonObject{ { QStringLiteral("id"), m.id }, { QStringLiteral("title"), m.title },
                                            { QStringLiteral("owner"), QJsonObject{ { QStringLiteral("kind"), m.owner.kind }, { QStringLiteral("name"), m.owner.name } } },
                                            { QStringLiteral("fromAnotherClub"), fromClub }, { QStringLiteral("clubs"), clubs } });
                }
                answer(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("club-shares") && a.size() == 2) {
            api().moduleShares(a[1], [this](const sync::ModuleShares& s) {
                QJsonArray partners, offers, clubs;
                for (const auto& p : s.partners) partners.append(p.name);
                for (const auto& f : s.offers)
                    offers.append(QJsonObject{ { QStringLiteral("id"), f.id }, { QStringLiteral("club"), f.club.name.isEmpty() ? f.club.slug : f.club.name },
                                               { QStringLiteral("state"), f.state } });
                for (const auto& c : s.clubs)
                    clubs.append(QJsonObject{ { QStringLiteral("name"), c.name }, { QStringLiteral("via"), c.via } });
                answer(QStringLiteral("ok ")
                       + QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("canShareWithOtherClubs"), s.canShareWithOtherClubs },
                                                                      { QStringLiteral("club"), s.club.name },
                                                                      { QStringLiteral("partners"), partners },
                                                                      { QStringLiteral("offers"), offers },
                                                                      { QStringLiteral("clubs"), clubs } })
                                               .toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("club-share") && a.size() == 3) {
            api().shareClubModule(a[1], a[2], [this](const sync::ClubShareResult& r) {
                answer(QStringLiteral("ok ")
                       + QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("status"), r.status }, { QStringLiteral("name"), r.name },
                                                                      { QStringLiteral("slug"), r.slug } })
                                               .toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("club-offer-withdraw") && a.size() == 3) {
            api().withdrawClubOffer(a[1], a[2], [this] { answer(QStringLiteral("ok")); }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("club-requests")) {
            api().clubRequestCount([this](const QList<sync::ClubRequestCount>& list) {
                QJsonArray out;
                for (const auto& c : list)
                    out.append(QJsonObject{ { QStringLiteral("slug"), c.slug }, { QStringLiteral("name"), c.name }, { QStringLiteral("count"), c.count } });
                answer(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("notif-watch")) {
            // The bell's center for this server, live (its event stream).
            if (!notifications_) {
                notifications_ = std::make_unique<sync::NotificationCenter>();
                notifications_->setReconnectDelay(500, 2000);
            }
            notifications_->setSignedIn(server_, token_);
            const bool known = waitFor([&] { return notifications_->support(server_) != sync::NotificationCenter::Support::Unknown; }, 15000);
            if (!known) reply(QStringLiteral("error no answer from the server"));
            else reply(QStringLiteral("ok ") + notificationCounts());
        } else if (cmd == QLatin1String("notif-count")) {
            if (!notifications_) reply(QStringLiteral("error notif-watch first"));
            else reply(QStringLiteral("ok ") + notificationCounts());
        } else if (cmd == QLatin1String("notif-wait-unread") && a.size() >= 2) {
            // Waits for the bell's number to reach N, from the live hints alone (nothing asks meanwhile).
            const int n = a[1].toInt();
            const int ms = a.size() > 2 ? a[2].toInt() : 20000;
            if (!notifications_) reply(QStringLiteral("error notif-watch first"));
            else if (waitFor([&] { return notifications_->totalUnread() >= n; }, ms)) reply(QStringLiteral("ok ") + notificationCounts());
            else reply(QStringLiteral("error unread is ") + QString::number(notifications_->totalUnread()));
        } else if (cmd == QLatin1String("notif-list")) {
            sync::NotificationApi* api = notifications_ ? notifications_->api(server_) : nullptr;
            if (!api) {
                reply(QStringLiteral("error notif-watch first"));
                return true;
            }
            sync::NotificationQuery q;
            q.limit = 50;
            api->list(q, [this](const sync::NotificationPage& page) {
                QJsonArray out;
                for (const auto& n : page.items)
                    out.append(QJsonObject{ { QStringLiteral("id"), n.id }, { QStringLiteral("type"), n.type },
                                            { QStringLiteral("title"), n.title }, { QStringLiteral("read"), n.read() },
                                            { QStringLiteral("outcome"), n.outcome },
                                            { QStringLiteral("actions"), QJsonArray::fromStringList(n.actions) } });
                answer(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("notif-act") && a.size() == 3) {
            sync::NotificationApi* api = notifications_ ? notifications_->api(server_) : nullptr;
            if (!api) {
                reply(QStringLiteral("error notif-watch first"));
                return true;
            }
            api->act(a[1], a[2], [this](const sync::NotificationActResult& r) {
                answer(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(QJsonObject{
                    { QStringLiteral("outcome"), r.outcome }, { QStringLiteral("already"), r.already },
                    { QStringLiteral("layoutId"), r.result.value(QLatin1String("layoutId")) } }).toJson(QJsonDocument::Compact)));
            }, refused());
            awaitAnswer();
        } else if (cmd == QLatin1String("peers")) {
            QJsonArray peers;
            for (const auto& state : session_.peers()) peers.append(state);
            reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(peers).toJson(QJsonDocument::Compact)));
        } else {
            reply(QStringLiteral("error unknown command: ") + line);
        }
        return true;
    }

    void setToken(const QString& token) { token_ = token; }

private:
    // One editor edit: change a brick of what the editor shows, then hand
    // the whole map to the session, as MapView's undo stack does.
    void edit(const QString& id, const std::function<void(core::Brick&)>& change) {
        QString error;
        auto map = session_.editorMap(&error);
        if (!map) return reply(QStringLiteral("error ") + error);
        core::Brick* b = findBrick(*map, id);
        if (!b) return reply(QStringLiteral("error no brick ") + id);
        send(*map, [&] { change(*b); });
    }

    // The edit `change` makes to `map`, handed to the session, unless it
    // breaks the lock on a shared (read-only) module: then nothing is sent
    // and the answer says why, as the app does (MapView::onSharedLockStep).
    // A shared module moved as a whole has its placement kept up.
    void send(core::Map& map, const std::function<void()>& change, const QString& answer = {}) {
        static parts::PartsLibrary noParts;
        edit::SharedLockState lock;
        lock.rebase(map, noParts);
        change();
        if (const auto broken = lock.check(map)) return reply(QStringLiteral("error locked: ") + broken->message());
        lock.keepPlacements(map, noParts);
        session_.localEdit(map);
        reply(answer.isEmpty() ? QStringLiteral("ok") : QStringLiteral("ok ") + answer);
    }

    // The server's API, signed in as the driver.
    QString notificationCounts() const {
        const auto* src = notifications_->source(server_);
        const sync::NotificationCounts c = src ? src->counts : sync::NotificationCounts{};
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            { QStringLiteral("unread"), c.unread }, { QStringLiteral("waiting"), c.waiting }, { QStringLiteral("warnings"), c.warnings },
            { QStringLiteral("supported"), notifications_->support(server_) == sync::NotificationCenter::Support::Yes } })
                                     .toJson(QJsonDocument::Compact));
    }
    std::unique_ptr<sync::NotificationCenter> notifications_;
    sync::LibraryApi& api() {
        api_.setBase(server_);
        api_.setToken(token_);
        return api_;
    }
    // A request's answer is the command's one reply line.
    void answer(const QString& line) {
        if (answered_) return;  // (it came after the wait gave up)
        answered_ = true;
        reply(line);
    }
    sync::LibraryApi::Fail refused() {
        return [this](const sync::ServerRefusal& r) {
            answer(QStringLiteral("error %1 %2").arg(r.status).arg(r.code.isEmpty() ? r.networkError : r.code));
        };
    }
    void awaitAnswer() {
        answered_ = false;
        if (waitFor([&] { return answered_; }, 30000)) return;
        answered_ = true;
        reply(QStringLiteral("error no answer from the server"));
    }

    // One editor edit to a module of what the editor shows (its parts, its
    // name), handed to the session as the Modules panel's commands do.
    void editModule(const QString& id, const std::function<void(core::Module&, core::Map&)>& change) {
        QString error;
        auto map = session_.editorMap(&error);
        if (!map) return reply(QStringLiteral("error ") + error);
        for (auto& m : map->sidecar.modules) {
            if (m.id != id) continue;
            return send(*map, [&] { change(m, *map); });
        }
        reply(QStringLiteral("error no module ") + id);
    }

    // What MainWindow::onReviewOfflineEdits does, with the compare
    // window's choices given up front.
    void resolve(const QString& how) {
        const auto& off = session_.offlineEdits();
        if (!off) return reply(QStringLiteral("error no offline edits"));
        if (session_.status() != Status::Synced) return reply(QStringLiteral("error not synced"));
        const auto current = session_.currentMap();
        if (!current) return reply(QStringLiteral("error unreadable layout"));
        const auto server = sync::merge::snapshotOf(*current);
        const auto changes = sync::merge::compareLayouts(off->base, off->mine, server);
        QHash<QString, sync::merge::Choice> choices;
        if (how == QLatin1String("mine") || how == QLatin1String("server")) {
            const auto side = how == QLatin1String("mine") ? sync::merge::Choice::Mine : sync::merge::Choice::Server;
            for (const auto& c : changes)
                if (c.status == sync::merge::Status::Conflict) choices.insert(c.key, side);
        } else if (how != QLatin1String("merge-default")) {
            return reply(QStringLiteral("error resolve mine|server|merge-default"));
        }
        QString error;
        const auto merged = sync::merge::mergeLayouts(off->mine, server, changes, choices, &error);
        if (!merged) return reply(QStringLiteral("error ") + error);
        session_.resolveOffline(merged.get());
        QJsonArray out;
        for (const auto& c : changes) out.append(c.key);
        reply(QStringLiteral("ok ") + QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
    }

    QUrl server_;
    QUrl socket_;
    QString token_;
    QString layoutId_;
    QString name_ = QStringLiteral("Desktop");
    sync::SyncSession session_;
    sync::LibraryApi api_;
    bool answered_ = false;
    // Where the session keeps the layout and its offline edits, as the app
    // does: "reconnect" opens the layout again and finds them there.
    QTemporaryDir cache_;
};

}  // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 2) {
        std::fputs("usage: bld_sync_driver <server url>  (token in BLD_SYNC_TOKEN)\n", stderr);
        return 2;
    }
    Driver driver{ QUrl(args[1]) };
    driver.setToken(qEnvironmentVariable("BLD_SYNC_TOKEN"));

    // Commands run one at a time from the main loop; lines that arrive
    // while one waits are queued.
    std::deque<QString> queue;
    bool eof = false;
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::line, &app, [&](const QString& l) { queue.push_back(l); });
    QObject::connect(&reader, &StdinReader::closed, &app, [&] { eof = true; });
    reader.start();

    for (;;) {
        if (!waitFor([&] { return !queue.empty() || eof; }, 1 << 30)) continue;
        if (queue.empty()) break;
        const QString line = queue.front();
        queue.pop_front();
        if (!driver.run(line)) break;
    }
    // stdin may still be open; don't wait on the reader.
    reader.terminate();
    reader.wait(1000);
    return 0;
}

#include "bld_sync_driver.moc"
