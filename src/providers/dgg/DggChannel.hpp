#pragma once

#include "common/Channel.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "providers/dgg/DggStream.hpp"

#include <pajlada/signals/signal.hpp>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <deque>
#include <optional>
#include <vector>

namespace chatterino {

/// The single destiny.gg chat room.
///
/// DGG only has one room, so unlike KickChannel there's no per-channel lookup.
/// Whether the broadcast is live is polled from the site by the chat server
/// (setStreamInfo()). Sending needs a destiny.gg account (a login key);
/// without one the channel can be read but not written to.
class DggChannel : public Channel
{
public:
    /// The name used to identify the DGG channel in layouts and logs.
    static const QString &channelName();

    DggChannel();
    ~DggChannel() override;

    /// Fired from the destructor, so the server can drop the connection once
    /// no split shows DGG chat anymore.
    pajlada::Signals::NoArgSignal aboutToClose;

    const QString &getDisplayName() const override;

    /// Whether the broadcast is live on any platform. Drives the live dot on
    /// the tab. False until the site was asked.
    bool isLive() const override;

    /// What the site last said about the broadcast.
    const DggStreamInfo &streamInfo() const;

    /// Stores the latest answer of the site. Fires streamDataChanged() if
    /// anything differs, and liveStatusChanged() if live turned on or off.
    /// GUI thread only.
    void setStreamInfo(DggStreamInfo info);

    /// Fired after the stream info changed in any way (viewers, title, ...).
    pajlada::Signals::NoArgSignal streamDataChanged;

    /// Fired after the broadcast went live or offline.
    pajlada::Signals::NoArgSignal liveStatusChanged;

    /// Shows the Reconnect entry of the split's menu (and its hotkey), like
    /// Twitch does. Also works after a ban or "too many connections", where
    /// the connection doesn't retry by itself.
    bool canReconnect() const override;
    void reconnect() override;

    bool canSendMessage() const override;
    bool isWritable() const override;

    /// Sends the message as the logged-in user, or tells the user in the chat
    /// why it wasn't sent. The message is shown when the server echoes it.
    void sendMessage(const QString &message) override;

    /// Remembers that an event with this key was shown.
    ///
    /// Returns false if it was already seen, so the caller can skip it. This
    /// is what keeps the backlog the server replays after a reconnect from
    /// showing messages twice. Only the most recent keys are kept.
    ///
    /// Must be called from the GUI thread.
    bool markSeen(const QString &key);

    /// Greys out every message already in the channel from this nick, like a
    /// timeout does on Twitch (the Disabled flag draws the dimmed overlay).
    ///
    /// System lines (including the mute notice itself) are left alone, and
    /// messages that arrive later aren't touched, so someone who is unmuted
    /// speaks normally again. Must be called from the GUI thread.
    void disableMessagesFrom(const QString &nick);

    /// Remembers a user seen in chat (a message, `NAMES`, `JOIN`), so their
    /// name can be clicked in other people's messages and the user card has
    /// their flairs. Must be called from the GUI thread.
    void noteUser(const DggUser &user);

    /// The last data seen of the user. `nick` is matched without case.
    std::optional<DggUser> findUser(const QString &nick) const;

    /// The nick as the user spells it, if `nick` is a user seen in chat.
    std::optional<QString> canonicalNick(QStringView nick) const;

    /// The nick of every user seen in chat, as each spells it (for
    /// completion). In no particular order.
    std::vector<QString> nicks() const;

    /// Fired after the set of users that are connected right now changed
    /// (`NAMES`, `JOIN`, `QUIT`, `USERSDELTA`). GUI thread.
    pajlada::Signals::NoArgSignal onlineChanged;

    /// Replaces the connected users (`NAMES` lists everybody). Also remembers
    /// each of them like noteUser(). GUI thread only.
    void setOnlineUsers(const std::vector<DggUser> &users);

    /// Applies a batch of joins and quits at once (`USERSDELTA`), firing
    /// onlineChanged() only once. A user that quit is found by nick and, like
    /// in chat-gui, by id (the entries of `removed` may carry only one of
    /// them). GUI thread only.
    void updateOnlineUsers(const std::vector<DggUser> &joined,
                           const std::vector<DggUser> &quit);

    /// One user connected (`JOIN`). GUI thread only.
    void userJoined(const DggUser &user);

    /// One user disconnected (`QUIT`). GUI thread only.
    void userQuit(const DggUser &user);

    /// A connected user's data changed (`UPDATEUSER`, e.g. a new flair, which
    /// moves them to another section). Like chat-gui, only a user that is in
    /// the list is replaced. GUI thread only.
    void userUpdated(const DggUser &user);

    /// Everybody connected right now, in no particular order. Each entry has
    /// whatever was last learned of the user (a message may have told more
    /// than `NAMES` did).
    std::vector<DggUser> onlineUsers() const;

    qsizetype onlineCount() const;

    /// The message a moderator pinned, if there is one.
    const std::optional<DggPin> &pinnedMessage() const;

    /// Sets the pinned message, or clears it with nullopt. Fires
    /// pinnedMessageChanged(). GUI thread only.
    void setPinnedMessage(std::optional<DggPin> pin);

    /// Fired after the pinned message was set, replaced or removed.
    pajlada::Signals::NoArgSignal pinnedMessageChanged;

    /// The poll that is running, or the one that ended last (it stays so the
    /// banner can show the result). Nullopt before the first poll.
    const std::optional<DggPoll> &poll() const;

    /// A poll started (`POLLSTART`). It replaces the one that was there; the
    /// same poll sent again (after a reconnect) just refreshes its numbers.
    /// GUI thread only.
    void startPoll(DggPoll poll);

    /// Somebody voted (`VOTECAST`). `option` is 1-based and 0 if the server
    /// did not say (hidden polls): then only the number of votes grows.
    void addPollVotes(int option, qint64 quantity);

    /// The server counted the user's own vote (`VOTECOUNTED`).
    void markPollVote(int option);

    /// The poll is over (`POLLSTOP`, with the final numbers) or was cut off
    /// (the connection dropped: no numbers). Returns false if there was no
    /// running poll. For a hidden poll the numbers are shown from now on.
    bool endPoll(const std::optional<DggPollEnd> &result);

    /// Fired after the poll started, got votes or ended.
    pajlada::Signals::NoArgSignal pollChanged;

private:
    static constexpr qsizetype MAX_USERS = 40000;
    /// Lower-case nick -> user
    QHash<QString, DggUser> users_;
    /// Lower-case nick -> user, for the users connected right now. Unlike
    /// users_ this is an exact set: it shrinks on `QUIT`.
    QHash<QString, DggUser> online_;
    static constexpr size_t MAX_SEEN_KEYS = 2000;

    QSet<QString> seen_;
    std::deque<QString> seenOrder_;

    std::optional<DggPin> pin_;
    std::optional<DggPoll> poll_;

    DggStreamInfo streamInfo_;
};

}  // namespace chatterino
