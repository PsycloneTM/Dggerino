#include "providers/dgg/DggWebSocket.hpp"

#include "common/QLogging.hpp"
#include "util/PostToThread.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace chatterino {

using namespace Qt::Literals;

namespace {

/// A connection must stay open this long to count as healthy. The server
/// completes the handshake before it authenticates, so a rejected connection
/// still opens first.
constexpr std::chrono::seconds STABLE_CONNECTION{5};

constexpr int64_t RETRY_WINDOW_MS = 3000;
constexpr int64_t RETRY_WINDOW_CAP_MS = 60000;
constexpr int64_t RETRY_MIN_MS = 500;

}  // namespace

struct DggWebSocket::Listener final : public WebSocketListener {
    Listener(QPointer<DggWebSocket> owner, uint64_t id)
        : owner(std::move(owner))
        , id(id)
    {
    }

    void onOpen() override
    {
        runInGuiThread([owner = this->owner, id = this->id] {
            if (owner)
            {
                owner->handleOpen(id);
            }
        });
    }

    void onTextMessage(QByteArray data) override
    {
        auto frame = parseDggFrame(data);
        if (!frame)
        {
            return;
        }

        std::vector<DggFrame> frames;
        if (frame->name == "HISTORY"_L1)
        {
            expandDggHistory(frame->payload, frames);
        }
        else if (frame->name == "NAMES"_L1)
        {
            // ~1500 user objects. They are forwarded with the counts: the
            // channel remembers their nicks (for clickable names and the
            // user card). Implicitly shared, so the hand-over is cheap.
            const auto obj = frame->payload.toObject();
            const auto users = obj.value("users"_L1).toArray();
            QJsonObject out;
            out.insert("connectioncount"_L1, obj.value("connectioncount"_L1));
            out.insert("usercount"_L1, static_cast<int>(users.size()));
            out.insert("users"_L1, users);
            frame->payload = out;
            frames.push_back(std::move(*frame));
        }
        else
        {
            frames.push_back(std::move(*frame));
        }

        if (frames.empty())
        {
            return;
        }

        runInGuiThread([owner = this->owner, id = this->id,
                        frames = std::move(frames)]() mutable {
            if (owner)
            {
                owner->handleFrames(id, std::move(frames));
            }
        });
    }

    void onBinaryMessage(QByteArray /*data*/) override
    {
        // DGG only sends text frames.
    }

    void onClose(std::unique_ptr<WebSocketListener> /*self*/) override
    {
        runInGuiThread([owner = this->owner, id = this->id] {
            if (owner)
            {
                owner->handleClose(id);
            }
        });
    }

    QPointer<DggWebSocket> owner;
    uint64_t id;
};

QUrl DggWebSocket::defaultUrl()
{
    return QUrl(QStringLiteral("wss://chat.destiny.gg/ws"));
}

DggWebSocket::DggWebSocket(QUrl url)
    : url_(std::move(url))
{
    this->retryTimer_.setSingleShot(true);
    QObject::connect(&this->retryTimer_, &QTimer::timeout, this, [this] {
        this->connectNow();
    });
}

DggWebSocket::~DggWebSocket()
{
    this->stop();
    this->pool_.reset();
}

bool dggIsPlausibleLoginKey(const QString &key)
{
    if (key.isEmpty() || key.size() > 256)
    {
        return false;
    }
    for (const QChar c : key)
    {
        if (c.unicode() <= 0x20 || c.unicode() >= 0x7F || c == u';' ||
            c == u',' || c == u'"' || c == u'\\')
        {
            return false;
        }
    }
    return true;
}

void DggWebSocket::setAuthToken(const QString &key)
{
    const auto trimmed = key.trimmed();
    if (!trimmed.isEmpty() && !dggIsPlausibleLoginKey(trimmed))
    {
        // Not the key itself: it must never reach a log
        qCWarning(chatterinoDgg) << "The login key has characters that can't "
                                    "be sent; connecting as a guest";
        this->authToken_.clear();
        return;
    }
    this->authToken_ = trimmed;
}

bool DggWebSocket::send(const QString &name, const QJsonObject &payload)
{
    if (!this->isOpen())
    {
        return false;
    }

    QByteArray data = name.toUtf8();
    data.append(' ');
    data.append(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    this->handle_.sendText(data);
    return true;
}

void DggWebSocket::start()
{
    if (this->wanted_)
    {
        return;
    }

    this->wanted_ = true;
    this->retryEnabled_ = true;
    this->retryAttempts_ = 0;
    this->connectNow();
}

void DggWebSocket::stop()
{
    this->wanted_ = false;
    this->retryTimer_.stop();
    this->generation_++;
    this->openedAt_.reset();
    this->handle_ = WebSocketHandle{};
}

void DggWebSocket::connectNow()
{
    if (!this->wanted_)
    {
        return;
    }

    this->generation_++;
    this->retryAttempts_++;
    this->openedAt_.reset();

    if (!this->pool_)
    {
        this->pool_.emplace(QStringLiteral("dgg"));
    }

    // The browser sends this Origin, and the server may check it.
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("Origin", "https://www.destiny.gg");
    if (!this->authToken_.isEmpty())
    {
        // The login key. Only the fact that there is one is ever logged.
        headers.emplace_back("Cookie",
                             "authtoken=" + this->authToken_.toStdString());
    }

    qCDebug(chatterinoDgg) << "Connecting to" << this->url_
                           << "attempt:" << this->retryAttempts_ << "as"
                           << (this->authToken_.isEmpty() ? "guest"
                                                          : "a logged-in user");

    this->handle_ = this->pool_->createSocket(
        WebSocketOptions{
            .url = this->url_,
            .headers = std::move(headers),
        },
        std::make_unique<Listener>(QPointer<DggWebSocket>(this),
                                   this->generation_));
}

std::chrono::milliseconds DggWebSocket::retryDelay() const
{
    // Full jitter over a window that doubles with every failed attempt. The
    // jitter matters as much as the backoff, because a server restart
    // disconnects every client at once.
    const auto shift = std::min(this->retryAttempts_, 5U);
    const int64_t window =
        std::min<int64_t>(RETRY_WINDOW_CAP_MS, RETRY_WINDOW_MS << shift);
    const auto jitter = QRandomGenerator::global()->bounded(
        static_cast<qint64>(window - RETRY_MIN_MS));
    return std::chrono::milliseconds{RETRY_MIN_MS +
                                     static_cast<int64_t>(jitter)};
}

void DggWebSocket::handleOpen(uint64_t id)
{
    if (id != this->generation_)
    {
        return;
    }

    qCDebug(chatterinoDgg) << "Connection opened";
    this->openedAt_ = std::chrono::steady_clock::now();
    if (this->onOpened)
    {
        this->onOpened();
    }
}

void DggWebSocket::handleFrames(uint64_t id, std::vector<DggFrame> frames)
{
    for (const auto &frame : frames)
    {
        if (id != this->generation_)
        {
            // stop() or a reconnect happened inside a callback
            return;
        }

        if (frame.name == "PING"_L1)
        {
            // chat-gui answers with the same data
            QByteArray pong = "PONG";
            if (frame.payload.isObject())
            {
                pong.append(' ');
                pong.append(QJsonDocument(frame.payload.toObject())
                                .toJson(QJsonDocument::Compact));
            }
            if (this->isOpen())
            {
                this->handle_.sendText(pong);
            }
            continue;
        }

        if (frame.name == "ERR"_L1)
        {
            const auto desc =
                frame.payload.toObject().value("description"_L1).toString();
            qCDebug(chatterinoDgg) << "ERR" << desc;
            if (desc == "banned"_L1 || desc == "toomanyconnections"_L1)
            {
                this->retryEnabled_ = false;
            }
        }

        if (this->onFrameReceived)
        {
            this->onFrameReceived(frame);
        }
    }
}

void DggWebSocket::handleClose(uint64_t id)
{
    if (id != this->generation_)
    {
        return;
    }

    this->handle_ = WebSocketHandle{};

    const bool stable = this->openedAt_.has_value() &&
                        (std::chrono::steady_clock::now() - *this->openedAt_) >=
                            STABLE_CONNECTION;
    this->openedAt_.reset();
    if (stable)
    {
        this->retryAttempts_ = 0;
    }

    if (!this->wanted_ || !this->retryEnabled_)
    {
        qCDebug(chatterinoDgg) << "Connection closed, not retrying";
        if (this->onClosed)
        {
            this->onClosed(std::chrono::milliseconds{0});
        }
        return;
    }

    const auto delay = this->retryDelay();
    qCDebug(chatterinoDgg) << "Connection closed, retrying in" << delay.count()
                           << "ms";
    this->retryTimer_.start(delay);
    if (this->onClosed)
    {
        this->onClosed(delay);
    }
}

}  // namespace chatterino
