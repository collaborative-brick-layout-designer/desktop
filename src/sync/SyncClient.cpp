#include "SyncClient.h"

#include "Compat.h"
#include "ServerApi.h"
#include "SyncDoc.h"
#include "SyncProtocol.h"
#include "UpdateGuard.h"

#include <QDebug>
#include <QNetworkRequest>
#include <QWebSocketProtocol>

#include <utility>

namespace bld::sync {

using protocol::Kind;

SyncClient::SyncClient(SyncDoc& doc, QObject* parent) : QObject(parent), doc_(doc) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &SyncClient::connectSocket);
    connect(&socket_, &QWebSocket::connected, this, &SyncClient::onConnected);
    connect(&socket_, &QWebSocket::disconnected, this, &SyncClient::onDisconnected);
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, &SyncClient::onMessage);
    ping_.setInterval(15000);
    connect(&ping_, &QTimer::timeout, this, &SyncClient::ping);
    answer_.setSingleShot(true);
    answer_.setInterval(10000);
    connect(&answer_, &QTimer::timeout, this, &SyncClient::noAnswer);
    connect(&socket_, &QWebSocket::pong, this, [this] { heard(); });
    // Qt drops a bigger message (closing with 1009) before buffering it all.
    // A few bytes over the limit for the message's own header.
    socket_.setMaxAllowedIncomingMessageSize(static_cast<quint64>(guard::kMaxUpdateBytes) + 64);
}

SyncClient::~SyncClient() {
    wantOpen_ = false;
    socket_.disconnect(this);
    socket_.abort();
}

void SyncClient::setReconnectDelays(std::chrono::milliseconds initial, std::chrono::milliseconds max) {
    initialDelay_ = delay_ = initial;
    maxDelay_ = max;
}

void SyncClient::setKeepAlive(std::chrono::milliseconds every, std::chrono::milliseconds answerWithin,
                              std::chrono::milliseconds firstSyncWithin) {
    ping_.setInterval(every);
    answer_.setInterval(answerWithin);
    firstSyncWithin_ = firstSyncWithin;
}

void SyncClient::open(const QUrl& url, const QString& bearerToken) {
    url_ = url;
    token_ = bearerToken;
    wantOpen_ = true;
    waiting_.clear();
    resyncAsked_ = false;
    closedForSize_ = false;
    delay_ = initialDelay_;
    connectSocket();
}

void SyncClient::close() {
    wantOpen_ = false;
    waiting_.clear();
    retry_.stop();
    ping_.stop();
    answer_.stop();
    socket_.close();
    setStatus(Status::Offline);
}

void SyncClient::ping() {
    if (socket_.state() != QAbstractSocket::ConnectedState) return;
    socket_.ping();
    if (!answer_.isActive()) answer_.start();
}

void SyncClient::heard() {
    answer_.stop();
}

void SyncClient::noAnswer() {
    if (socket_.state() != QAbstractSocket::ConnectedState) return;
    // The layout's first copy may still be arriving, with the answer behind it.
    if (status_ == Status::Syncing && std::chrono::steady_clock::now() - openedAt_ < firstSyncWithin_) return;
    qWarning().noquote() << "Live layout: no answer from the server for"
                         << (ping_.interval() + answer_.interval()) / 1000 << "s; connecting again";
    ping_.stop();
    // Not close(): its closing handshake would wait on the dead network too.
    socket_.abort();
}

void SyncClient::connectSocket() {
    if (!wantOpen_) return;
    setStatus(Status::Connecting);
    QNetworkRequest req(url_);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    // Tokens only ever travel in the header (the server ignores query tokens).
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    socket_.open(req);
}

void SyncClient::onConnected() {
    delay_ = initialDelay_;
    openedAt_ = std::chrono::steady_clock::now();
    answer_.stop();
    ping_.start();
    // Our step 1 below asks for everything we're missing anyway.
    resyncAsked_ = false;
    setStatus(Status::Syncing);
    send(protocol::encode(Kind::SyncStep1, doc_.stateVector()));
}

void SyncClient::onDisconnected() {
    ping_.stop();
    answer_.stop();
    const int code = socket_.closeCode();
    const QString reason = socket_.closeReason();
    if (!wantOpen_) {
        setStatus(Status::Offline);
        return;
    }
    // Revoked sign-in / unauthorized, the layout is gone or no longer shared,
    // or the server's limit on people in one layout (retrying would only
    // be refused again; the person reopens it later).
    // 4426: this app is older than the server allows (update_required).
    if (code == 1008 || code == 4404 || code == 4426 || (code == 4429 && reason == QLatin1String("limit_reached"))) {
        wantOpen_ = false;
        setStatus(Status::Offline);
        emit closedForGood(code, reason);
        return;
    }
    // A message over the size limit (Qt closes with 1009 before reading it
    // all): reconnecting is the resync; the same again ends the session.
    if (code == QWebSocketProtocol::CloseCodeTooMuchData) {
        qWarning().noquote() << "Live layout: a message from the server is over the"
                             << guard::kMaxUpdateBytes / guard::kMiB << "MiB limit";
        if (closedForSize_) {
            wantOpen_ = false;
            setStatus(Status::Offline);
            emit closedForGood(kUnreadableUpdateCode, QStringLiteral("unreadable_update"));
            return;
        }
        closedForSize_ = true;
    }
    setStatus(Status::Connecting);
    retry_.start(delay_);
    delay_ = std::min(delay_ * 2, maxDelay_);
}

void SyncClient::onMessage(const QByteArray& message) {
    heard();
    const auto msg = protocol::decode(message);
    if (!msg) return;  // unknown or malformed: ignored, like y-websocket
    switch (msg->kind) {
    case Kind::SyncStep1:
        // An unreadable state vector gets the whole document (SyncDoc::diffSince).
        if (const QString refused = guard::checkStateVector(msg->payload); !refused.isEmpty())
            qWarning().noquote() << "Live layout: the server's state vector was refused:" << refused;
        send(protocol::encode(Kind::SyncStep2, doc_.diffSince(msg->payload)));
        break;
    case Kind::SyncStep2:
    case Kind::Update:
        if (held_) waiting_.emplace_back(msg->kind == Kind::SyncStep2, msg->payload);
        else incoming(msg->kind == Kind::SyncStep2, msg->payload);
        break;
    case Kind::Awareness:
        emit awarenessReceived(msg->payload);
        break;
    }
}

bool SyncClient::takeIn(bool syncStep2, const QByteArray& payload) {
    QString error;
    if (doc_.applyUpdate(payload, &error)) return true;
    unreadableUpdate(syncStep2, error);
    return false;
}

void SyncClient::inStep() {
    resyncAsked_ = false;
    closedForSize_ = false;
    setStatus(Status::Synced);
}

void SyncClient::incoming(bool syncStep2, const QByteArray& payload) {
    if (!takeIn(syncStep2, payload)) return;
    emit remoteChange();
    // The answer to our step 1: we now have everything the server had.
    if (syncStep2) inStep();
}

void SyncClient::holdIncoming(bool hold) {
    held_ = hold;
    if (held_ || waiting_.empty()) return;
    // Everything that waited goes in, then the editor is told once (it
    // reads the layout again for each telling).
    const auto waited = std::exchange(waiting_, {});
    bool any = false;
    bool answered = false;
    for (const auto& [syncStep2, payload] : waited) {
        // Closed meanwhile (an update that can't be read ends the session).
        if (!wantOpen_) break;
        if (!takeIn(syncStep2, payload)) continue;
        any = true;
        answered = answered || syncStep2;
    }
    if (any) emit remoteChange();
    // A step 2 from a connection that has since dropped held changes to
    // take in, but it no longer says we're in step.
    if (answered && wantOpen_ && socket_.state() == QAbstractSocket::ConnectedState) inStep();
}

void SyncClient::unreadableUpdate(bool syncStep2, const QString& why) {
    qWarning().noquote() << "Live layout: an update from the server couldn't be read:" << why;
    if (!resyncAsked_) {
        // Ask once for the whole layout again (a state vector of no clients):
        // a damaged relay of one change is fixed by the server's own copy.
        resyncAsked_ = true;
        send(protocol::encode(Kind::SyncStep1, QByteArray(1, '\0')));
        return;
    }
    // A change arriving while the resync is on its way is dropped: the
    // server's whole layout, the answer, decides.
    if (!syncStep2) return;
    wantOpen_ = false;
    retry_.stop();
    socket_.close();
    setStatus(Status::Offline);
    emit closedForGood(kUnreadableUpdateCode, QStringLiteral("unreadable_update"));
}

void SyncClient::sendLocal(const QByteArray& update) {
    if (update.isEmpty()) return;
    send(protocol::encode(Kind::Update, update));
}

void SyncClient::sendAwareness(const QByteArray& awarenessUpdate) {
    send(protocol::encode(Kind::Awareness, awarenessUpdate));
}

void SyncClient::send(const QByteArray& message) {
    if (socket_.state() == QAbstractSocket::ConnectedState) socket_.sendBinaryMessage(message);
}

void SyncClient::setStatus(Status s) {
    if (s == status_) return;
    status_ = s;
    emit statusChanged(s);
}

}  // namespace bld::sync
