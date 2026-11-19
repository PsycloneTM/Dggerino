#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "providers/dgg/DggFrame.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace chatterino {

/// The connection to the destiny.gg chat websocket.
///
/// Everything here runs on the GUI thread, except frame parsing, which happens
/// on the websocket thread so the large `NAMES` and `HISTORY` frames don't
/// block the UI. The callbacks are invoked on the GUI thread.
///
/// The server may send `PING`; like chat-gui, it is answered with a `PONG`
/// carrying the same data. A dropped connection is retried with jittered
/// exponential backoff, the same way chat-gui does it.
class DggWebSocket : public QObject
{
public:
    static QUrl defaultUrl();

    explicit DggWebSocket(QUrl url = defaultUrl());
    ~DggWebSocket() override;

    Q_DISABLE_COPY_MOVE(DggWebSocket)

    /// The handshake completed.
    std::function<void()> onOpened;

    /// The connection closed. `retryIn` is how long until the next attempt, or
    /// zero if no retry is scheduled (stopped, banned or too many connections).
    std::function<void(std::chrono::milliseconds retryIn)> onClosed;

    /// A frame arrived. For `NAMES`, the payload is
    /// `{"connectioncount": N, "usercount": M, "users": [...]}`.
    std::function<void(const DggFrame &frame)> onFrameReceived;

    /// The login key to connect with, or empty to connect as a guest. It is
    /// sent as the `authtoken` cookie (confirmed with a real login key: `ME`
    /// then names the user). Takes effect the next time start() connects. A
    /// value that can't safely go in a header is ignored (guest). Never log it.
    void setAuthToken(const QString &key);

    /// Connects, and keeps reconnecting until stop() is called.
    void start();

    /// Disconnects and stops retrying.
    void stop();

    bool isStarted() const
    {
        return this->wanted_;
    }

    /// The handshake completed and the connection hasn't closed since.
    bool isOpen() const
    {
        return this->openedAt_.has_value();
    }

    /// Sends `NAME {json}`. Returns false if the connection isn't open (the
    /// frame is dropped, not queued).
    bool send(const QString &name, const QJsonObject &payload);

private:
    struct Listener;

    void connectNow();
    std::chrono::milliseconds retryDelay() const;

    void handleOpen(uint64_t id);
    void handleFrames(uint64_t id, std::vector<DggFrame> frames);
    void handleClose(uint64_t id);

    QUrl url_;
    QString authToken_;
    std::optional<WebSocketPool> pool_;
    WebSocketHandle handle_;
    QTimer retryTimer_;

    /// Identifies the current connection, so late events from an older one
    /// are ignored.
    uint64_t generation_ = 0;
    unsigned retryAttempts_ = 0;
    std::optional<std::chrono::steady_clock::time_point> openedAt_;
    bool wanted_ = false;
    bool retryEnabled_ = true;
};

/// Whether `key` can be sent as a cookie value as is: not empty, and no spaces,
/// control characters, `;`, `,`, `"` or `\`. A real login key is 64 letters and
/// digits; this only keeps a pasted mistake from breaking the header.
bool dggIsPlausibleLoginKey(const QString &key);

}  // namespace chatterino
