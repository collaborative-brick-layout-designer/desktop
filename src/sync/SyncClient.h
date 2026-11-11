#pragma once

// Keeps a SyncDoc in step with a collaborative server over its
// y-websocket endpoint (wss://host/ws/layout/<id>), authenticated with a
// desktop API token. Sync phase P3c.
//
// On connect both sides send sync step 1 and answer the other's, which
// also delivers edits made while offline (they are in the local doc, so
// nothing is queued). Afterwards remote updates are applied as they come
// and local ones (SyncDoc::writeMap's result) sent with sendLocal. Lost
// connections are retried with backoff; a revoked sign-in (1008) or a
// deleted / no longer shared layout (4404) ends the session.
//
// An update that can't be read (damaged, or refused by UpdateGuard before
// yrs sees it) is logged and dropped, never half applied, and the client
// asks the server once for the whole layout again. If that can't be read
// either, the session ends with kUnreadableUpdateCode.
//
// A network that goes dead without closing anything (Wi-Fi gone, a laptop
// asleep) is noticed by a ping every 15 s: with no answer and no message in
// 10 s the connection counts as lost and is opened again, so edits wait as
// offline edits and the status stops saying "Connected". While the layout's
// first copy is still arriving (it can take minutes on a slow link, and the
// answer waits behind it) a missing answer is given longer.

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <chrono>
#include <utility>
#include <vector>

namespace bld::sync {

class SyncDoc;

class SyncClient : public QObject {
    Q_OBJECT
public:
    enum class Status {
        Offline,     // not connected (before open, after close or a final error)
        Connecting,  // opening, or waiting to retry
        Syncing,     // connected, exchanging what each side is missing
        Synced,      // up to date; changes flow both ways
    };
    Q_ENUM(Status)

    explicit SyncClient(SyncDoc& doc, QObject* parent = nullptr);
    ~SyncClient() override;

    void open(const QUrl& url, const QString& bearerToken);
    void close();

    // Send a local change (from SyncDoc::writeMap). While not connected it
    // needs no queueing: the next handshake carries it.
    void sendLocal(const QByteArray& update);
    void sendAwareness(const QByteArray& awarenessUpdate);

    Status status() const { return status_; }

    // While held (the editor is in the middle of a drag), the layout's
    // changes from the server wait instead of going into the document; on
    // release they go in, in the order they came. So the document stays
    // what the editor is showing, and the edit the drag ends with is a
    // change to that: applied after it, what the others did meanwhile
    // merges with it. (Written into a document that already had their
    // changes, the editor's older picture of the layout would take them
    // back.) Presence isn't held.
    void holdIncoming(bool hold);

    // Retry backoff: `initial`, doubling up to `max`.
    void setReconnectDelays(std::chrono::milliseconds initial, std::chrono::milliseconds max);
    // How often to ping, how long to wait for the answer, and how long the
    // first copy of the layout may take (tests shorten them).
    void setKeepAlive(std::chrono::milliseconds every, std::chrono::milliseconds answerWithin,
                      std::chrono::milliseconds firstSyncWithin);
    // For tests: the largest message accepted (UpdateGuard's limit otherwise).
    void setMaxMessageSize(quint64 bytes) { socket_.setMaxAllowedIncomingMessageSize(bytes); }

signals:
    void statusChanged(bld::sync::SyncClient::Status status);
    // The document changed from the network; re-read it.
    void remoteChange();
    void awarenessReceived(const QByteArray& awarenessUpdate);
    // The server ended the session for good (1008, 4404), or the layout's
    // updates can't be read (kUnreadableUpdateCode): no more retries.
    void closedForGood(int code, const QString& reason);

private:
    void setStatus(Status s);
    void connectSocket();
    void onConnected();
    void onDisconnected();
    void onMessage(const QByteArray& message);
    // A sync step 2 or an update from the server goes into the document,
    // and the editor is told.
    void incoming(bool syncStep2, const QByteArray& payload);
    // Into the document only; false when it couldn't be read (dealt with).
    bool takeIn(bool syncStep2, const QByteArray& payload);
    // The server's answer to our step 1 is in: we have all it had.
    void inStep();
    void unreadableUpdate(bool syncStep2, const QString& why);
    void send(const QByteArray& message);
    void ping();
    void noAnswer();
    void heard();

    SyncDoc& doc_;
    QWebSocket socket_;
    QTimer retry_;
    QTimer ping_;
    QTimer answer_;
    // When this connection opened (for the first copy's longer wait).
    std::chrono::steady_clock::time_point openedAt_{};
    QUrl url_;
    QString token_;
    Status status_ = Status::Offline;
    bool held_ = false;
    // What arrived while held: whether it was a sync step 2, and its payload.
    std::vector<std::pair<bool, QByteArray>> waiting_;
    bool wantOpen_ = false;
    // A full resync asked for after an unreadable update, not answered yet.
    bool resyncAsked_ = false;
    // The connection closed for a message over the size limit; once more ends the session.
    bool closedForSize_ = false;
    std::chrono::milliseconds initialDelay_{ 1000 };
    std::chrono::milliseconds maxDelay_{ 30000 };
    std::chrono::milliseconds delay_{ 1000 };
    std::chrono::milliseconds firstSyncWithin_{ std::chrono::minutes(5) };
};

}  // namespace bld::sync
