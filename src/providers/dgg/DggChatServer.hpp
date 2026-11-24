#pragma once

#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "providers/dgg/DggFrame.hpp"
#include "providers/dgg/DggMessage.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace chatterino {

class Channel;
class DggAssets;
class DggChannel;
struct DggChatMessage;
struct DggComboLine;
class DggWebSocket;
struct HighlightAlert;

/// Owns the destiny.gg chat channel and its websocket.
///
/// The connection is opened when the channel is first requested and closed
/// again when the last split showing it goes away.
/// Reads the hashed images (profile pictures, icon sheet) from the site once
/// per run, then calls `done` (at once if that already happened; also if it
/// failed, so callers can carry on without images; a failure is retried by
/// the next call). GUI thread only.
void dggLoadSiteImages(std::function<void()> done = {});

/// Asks the site for the profile of `nick`. `done` gets the profile, or
/// nullopt and the HTTP status (0 if there was none) when the site refused or
/// had nothing. The request is anonymous: the login key only goes to the chat
/// socket. GUI thread only.
void dggFetchUserInfo(
    const QString &nick,
    std::function<void(const std::optional<DggProfile> &, int status)> done);

class DggChatServer : public QObject
{
public:
    DggChatServer();
    ~DggChatServer() override;

    Q_DISABLE_COPY_MOVE(DggChatServer)

    void initialize();

    /// Returns the DGG channel, creating it if no split currently holds it.
    /// This also makes sure the websocket is connected.
    ///
    /// Must be called from the GUI thread.
    std::shared_ptr<Channel> getOrCreate();

    /// Returns the DGG channel if it currently exists, otherwise null.
    std::shared_ptr<DggChannel> channel() const;

    /// The emote and flair data (null until the server exists). For the user
    /// card, which draws a user's flairs.
    const DggAssets *assets() const;

    /// Sends `text` to the chat as the logged-in user. Nothing is shown
    /// locally: the message appears when the server echoes it (which also
    /// keeps it from showing twice). Whatever stops it (no login, too long, a
    /// slash command, not connected) is shown to the user as a system line in
    /// `chan` instead. Must be called from the GUI thread.
    void sendChatMessage(DggChannel &chan, const QString &text);

    /// Sends a private message (`/w nick text`) as the logged-in user. Unlike
    /// public chat, the server's `PRIVMSGSENT` carries no text, so the line is
    /// shown here as soon as it was handed to the socket. Problems are shown
    /// as system lines in `chan`. Must be called from the GUI thread.
    void sendWhisper(DggChannel &chan, const QString &nick,
                     const QString &text);

    /// Votes for option `option` (1-based) of the poll that is running, as
    /// chat-gui does when an option is clicked or its number is typed. Does
    /// nothing if no poll is running or the number is not an option. Tells the
    /// user in `chan` if they are not logged in or have already voted. The
    /// banner changes when the server answers (`VOTECOUNTED`). GUI thread.
    void castPollVote(DggChannel &chan, int option);

    /// `/mentions [nick] [limit]` (and `/m`): asks the site for the latest
    /// lines that mention `nick` (the logged-in user if none is given) and
    /// adds them to `chan` as history lines, like chat-gui does. `args` is
    /// the text after the command name. Problems and "none found" are shown
    /// as system lines in `chan`. Only one request runs at a time. GUI
    /// thread only.
    void fetchMentions(DggChannel &chan, const QString &args);

    /// The Reconnect button of the split: drops the connection and connects
    /// again as the active account, whatever state the socket is in (retrying,
    /// or given up after a ban). Does nothing if no split shows the chat. GUI
    /// thread only.
    void reconnect();

private:
    void ensureConnected(const std::shared_ptr<DggChannel> &chan);

    /// Asks the site whether the broadcast is live now and then every
    /// STREAM_POLL_INTERVAL, for as long as the channel exists. The answer
    /// goes to DggChannel::setStreamInfo(), which lights up the tab.
    void startStreamPolling();
    void stopStreamPolling();
    void fetchStreamInfo();

    /// How often the site is asked whether the broadcast is live.
    static constexpr std::chrono::milliseconds STREAM_POLL_INTERVAL{60'000};
    static constexpr int STREAM_FETCH_TIMEOUT_MS = 10'000;

    void onSocketOpened();
    void onSocketClosed(std::chrono::milliseconds retryIn);
    /// The active destiny.gg account changed (login, logout, switch):
    /// reconnects so the connection is made as the new user.
    void onAccountChanged();
    /// Drops the connection and connects again with the active account's key.
    /// `notice` is shown in the chat first.
    void restartSocket(DggChannel &chan, const QString &notice);
    /// Entry point for frames from the websocket. Holds them back until the
    /// emote and flair manifests and the site images are loaded, so the
    /// backlog isn't built without emotes or the icon of a death line.
    void onFrame(const DggFrame &frame);

    /// What a handler below needs to know about the frame it handles.
    struct FrameContext {
        DggChannel &chan;
        const DggFrame &frame;
        /// `frame.payload` as an object (empty if it is not one).
        const QJsonObject &obj;
        /// When the frame was handled, ms since the epoch.
        qint64 nowMs;
    };

    /// Calls the handler of the frame's event, if the chat exists. Held-back
    /// frames (see `onFrame`) come through here too.
    void handleFrame(const DggFrame &frame);

    // One handler per event, called by `handleFrame`. Each one does the
    // work of one `if` branch of what used to be a single function.

    /// `MSG`: a line of chat (or the combo it continues).
    void onChatMessage(const FrameContext &ctx);
    /// `MUTE`, `UNMUTE`, `BAN`, `UNBAN` and `SUBONLY`.
    void onModeration(const FrameContext &ctx);
    /// `DEATH`.
    void onDeath(const FrameContext &ctx);
    /// `BROADCAST`.
    void onBroadcast(const FrameContext &ctx);
    /// `XPOST`.
    void onXPost(const FrameContext &ctx);
    /// `VESTABOARD_*` (see `isDggVestaboardName`).
    void onVestaboard(const FrameContext &ctx);
    /// Subscriptions, gifts, donations and spotlights (see
    /// `isDggEventName`).
    void onSiteEvent(const FrameContext &ctx);
    /// `PIN`: a pinned message, or its removal.
    void onPin(const FrameContext &ctx);
    /// `POLLSTART`, `POLLSTOP`, `VOTECAST` and `VOTECOUNTED`.
    void onPoll(const FrameContext &ctx);
    /// `NAMES`: everyone who is connected.
    void onNames(const FrameContext &ctx);
    /// `ME`: tells who the server thinks we are (a guest has no user).
    void onMe(const FrameContext &ctx);
    /// `PRIVMSG`: a whisper to the user.
    void onWhisper(const FrameContext &ctx);
    /// `ERR`.
    void onError(const FrameContext &ctx);
    /// `REFRESHCHATASSETS`.
    void onRefreshAssets(const FrameContext &ctx);
    /// `UPDATEUSER`.
    void onUserUpdated(const FrameContext &ctx);
    /// `JOIN`, `QUIT` and `USERSDELTA`.
    void onUserPresence(const FrameContext &ctx);

    /// Handles the held frames once the emote and flair manifests and the
    /// site images are both known.
    void flushPendingFrames();

    /// The last chat line, which is what the next one may combine with (like
    /// chat-gui's `lastmessage`). Everything else that is added to the chat
    /// (a system line, a mute) ends the combo.
    struct ComboState {
        /// What the line said, without a slash command.
        QString text;

        /// Lines so far. 1 until an identical one arrives.
        int count = 1;

        /// The line in the channel is a combo line (count >= 2).
        bool isCombo = false;

        /// The message in the channel that stands for the lines so far.
        MessagePtr shown;

        /// Time and id of the first line, which the combo line keeps.
        QDateTime firstTime;
        QString firstId;

        /// The latest line came from the history replay.
        bool backlog = false;

        /// The emote of the combo line and the look it was resolved for
        /// (`x5`, empty before the attributes are there), so a line that
        /// only changes its count keeps the same image and its animation
        /// isn't restarted by every hit.
        EmotePtr emote;
        QString emoteLook;
    };

    /// Adds `msg` to the running combo if it continues it. Returns false if
    /// it doesn't, and nothing changed then.
    bool continueCombo(DggChannel &chan, const DggChatMessage &msg,
                       const QString &textOnly, bool backlog);

    /// Completes the running combo ("5 X C-C-C-COMBO") and forgets it. Call
    /// before anything else is added to the chat.
    void endCombo(DggChannel &chan);

    DggComboLine comboLine(ComboState &state, bool complete) const;

    /// Adds a line for an event (broadcast, X post, ...) and plays its
    /// highlight's sound and window alert, unless the line only comes from the
    /// history replay.
    void addAlertingLine(DggChannel &chan, const MessagePtr &message,
                         const HighlightAlert &alert, bool backlog);

    std::optional<ComboState> combo_;

    std::weak_ptr<DggChannel> channel_;
    std::unique_ptr<DggAssets> assets_;
    std::vector<DggFrame> pendingFrames_;
    bool siteImagesReady_ = false;
    /// `ME` named a user on the current connection.
    bool authenticated_ = false;
    bool accountSignalConnected_ = false;
    /// A `/mentions` request is running (chat-gui's `busymentions`).
    bool mentionsBusy_ = false;
    /// A request for the stream info is running.
    bool streamFetchBusy_ = false;
    std::unique_ptr<QTimer> streamTimer_;
    std::unique_ptr<DggWebSocket> socket_;
    pajlada::Signals::SignalHolder signalHolder_;
    /// The current channel's `aboutToClose`; replaced with each new channel.
    pajlada::Signals::ScopedConnection channelClosedConnection_;
};

}  // namespace chatterino
