#pragma once

#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"

#include <QDateTime>
#include <QString>

#include <utility>

namespace chatterino {

class DggAssets;
class DggChannel;
struct DggChatMessage;
struct DggDeath;
struct DggPin;
struct DggEvent;
struct DggUser;
struct DggVestaboard;
struct DggXPost;

/// What a combo line shows: the emote that was repeated and how often.
struct DggComboLine {
    /// The emote's name, for search and copying.
    QString emoteName;

    /// How the emote looks in a combo line (DggAssets::comboEmoteFor()).
    EmotePtr emote;

    /// The number of lines in the combo (at least 2).
    int count = 2;

    /// Nothing followed the last line yet: the count is still running
    /// ("5 X HITS"). Once something else arrives it is complete
    /// ("5 X C-C-C-COMBO").
    bool complete = false;

    /// The time of the first line, like chat-gui.
    QDateTime time;

    /// The id of the message the combo started with.
    QString id;

    /// The last line came from the history replay.
    bool backlog = false;

    /// For a completed x10+ combo: how far the splatter behind the line has
    /// faded from red to grey, in percent (dggComboGreyPercent()). A combo
    /// from the history is grey already.
    int splatGrey = 0;
};

/// Builds the messages shown in the destiny.gg channel.
///
/// Every `make*` function takes `backlog`: true for events that came out of
/// the `HISTORY` replay. Those are marked as recent messages (so they're
/// faded like Twitch's recent-messages history, if the user has that turned
/// on) and are not written to the chat log again.
class DggMessageBuilder : public MessageBuilder
{
public:
    DggMessageBuilder(SystemMessageTag, DggChannel *channel,
                      const QDateTime &time);
    DggMessageBuilder(DggChannel *channel, const QDateTime &time);
    /// No timestamp element and not a system message: the caller adds what it
    /// needs (event headers have their own timestamp).
    explicit DggMessageBuilder(DggChannel *channel);

    /// A chat line: timestamp, flair badges, nick (in its flair colour) and
    /// the text with emotes and links. Greentext is coloured, `/me` lines
    /// have no colon after the nick.
    ///
    /// The user's highlights run over the line (your own name, highlight
    /// phrases, ...), so it can get the highlight colour. Whispers are not
    /// checked. `alert`, if given, receives the sound and window alert the
    /// highlight asks for; it is left empty for backlog lines and for your own
    /// lines. Nothing is triggered here.
    static MessagePtrMut makeChatMessage(DggChannel *channel,
                                         const DggChatMessage &msg,
                                         bool backlog, const DggAssets &assets,
                                         HighlightAlert *alert = nullptr);

    /// The line chat-gui shows for repeated emotes: the time of the first
    /// line, the emote once, and how many times it was sent. No nick or
    /// flairs, since the lines can come from many users.
    static MessagePtrMut makeComboMessage(DggChannel *channel,
                                          const DggComboLine &combo);

    /// A system line with the given time. `moderation` marks it as a
    /// moderation action (mutes, bans, sub-only mode).
    static MessagePtrMut makeSystemLine(DggChannel *channel,
                                        const QDateTime &time,
                                        const QString &text, bool backlog,
                                        bool moderation);

    /// A `MUTE`, `UNMUTE`, `BAN` or `UNBAN` line (`text` is what
    /// dggModerationText() made of it): a system line in which the target
    /// and the moderator (empty for none) are clickable: a click opens the
    /// user card. They look like the rest of the line.
    static MessagePtrMut makeModerationLine(
        DggChannel *channel, const QDateTime &time, const QString &text,
        const QString &target, const QString &moderator, bool backlog);

    /// A `DEATH` line, like the site shows it: a warning icon, the nick in
    /// its flair colour and the cause in italics (with emotes). Flagged as a
    /// system line, so greying out the dead user's messages skips it.
    static MessagePtrMut makeDeathMessage(DggChannel *channel,
                                          const DggDeath &death, bool backlog,
                                          const DggAssets &assets);

    /// A `SUBSCRIPTION`, `GIFTSUB`, `MASSGIFT`, `DONATION` or `SPOTLIGHT`
    /// line, built like Twitch builds its sub notices: flagged for its own
    /// highlight ("DGG subscriptions", "DGG gifted subscriptions", "DGG
    /// donations" or "DGG spotlights"), the header in system text ("Nick is now a Tier 3 subscriber", "Nick gifted
    /// Other a Tier 1 subscription", "Nick gifted 5 Tier 1 subs to the
    /// community", "Nick donated $5.00", "Nick spotlighted by Mod"), names
    /// in their flair colour and the tier in the colour of its flair. With a
    /// message that came with the event the header is its own line above the
    /// message (shown while "Show subscription header" is on); without one
    /// the whole line is a system message. The alert is what the highlight
    /// asks to play or flash; it is not triggered here.
    static std::pair<MessagePtrMut, HighlightAlert> makeEventMessage(
        DggChannel *channel, const DggEvent &event, bool backlog,
        const DggAssets &assets);

    /// A `BROADCAST` from a moderator, a bot or the system: the sender in
    /// their flair colour (none for the system) and the text with emotes and
    /// clickable links, flagged for the "DGG broadcasts" highlight. The alert
    /// is what that highlight asks to play or flash; it is not triggered here.
    static std::pair<MessagePtrMut, HighlightAlert> makeBroadcastMessage(
        DggChannel *channel, const QDateTime &time, const DggUser &user,
        const QString &text, bool backlog, const DggAssets &assets);

    /// An `XPOST` line, built like the other events: the header "@handle
    /// posted on X" (the handle opens the profile), and the text of the post
    /// as the line below it, followed by a link to the post. Flagged for the
    /// "DGG X posts" highlight.
    static std::pair<MessagePtrMut, HighlightAlert> makeXPostMessage(
        DggChannel *channel, const DggXPost &post, bool backlog);

    /// A Vestaboard event, built like the other events: the header ("Nick
    /// took the lead with a new design · $5.00") with the names as mentions,
    /// then the board as a picture, the top contributors and the link to the
    /// auction. Flagged for the "DGG Vestaboard" highlight.
    static std::pair<MessagePtrMut, HighlightAlert> makeVestaboardMessage(
        DggChannel *channel, const DggVestaboard &board, bool backlog,
        const DggAssets &assets);

    /// The line for a `PIN`: "Nick pinned a message: <text>" in system
    /// text, the name in its flair colour (a click opens the user card) and
    /// the text with emotes, links and names like a chat line.
    static MessagePtrMut makePinMessage(DggChannel *channel, const DggPin &pin,
                                        bool backlog, const DggAssets &assets);

    DggChannel *channel() const
    {
        return this->channel_;
    }

private:
    void markBacklog();

    DggChannel *channel_ = nullptr;
};

}  // namespace chatterino
