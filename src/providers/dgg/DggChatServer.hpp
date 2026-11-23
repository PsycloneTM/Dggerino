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
/// had nothing. When a DGG account is logged in the login key is sent as the
/// `authtoken` cookie; if the site refuses it (401/403) the request is made
/// again anonymously, and only that second answer is reported. GUI thread
/// only.
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
    /// `ME`: tells who the server thinks we are (a guest has no user).
    void onMe(DggChannel &chan, const QJsonObject &payload);
    /// The active destiny.gg account changed (login, logout, switch):
    /// reconnects so the connection is made as the new user.
    void onAccountChanged();
    /// Entry point for frames from the websocket. Holds them back until the
    /// emote and flair manifests and the site images are loaded, so the
    /// backlog isn't built without emotes or the icon of a death line.
    void onFrame(const DggFrame &frame);
    void handleFrame(const DggFrame &frame);
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
};

}  // namespace chatterino
