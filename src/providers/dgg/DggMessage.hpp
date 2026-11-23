#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QUrl>

#include <optional>
#include <vector>

namespace chatterino {

/// The user fields that come with every `MSG`, `JOIN`, `QUIT`, `MUTE`, ...
/// event (`id`, `nick`, `roles`, `features`, `subscription`).
///
/// Note that `id` is the *user's* id (it sits next to `createdDate`), not an
/// id of the message.
struct DggUser {
    qint64 id = 0;
    QString nick;
    QStringList roles;

    /// Flair names (`flair17`, `admin`, `subscriber`, ...).
    QStringList features;

    /// 0 if the user isn't subscribed.
    int subTier = 0;

    /// Patreon members can use the Patreon-only emotes.
    bool patreonMember = false;

    /// The profile picture: `buddy` for a stock one (see dggPhotoUrl()), empty
    /// if the event doesn't say (`JOIN`, `NAMES`).
    QString photo;

    /// When the account was made (`createdDate`); invalid if not sent.
    QDateTime createdDate;

    /// What the user is watching: `kick`, `drt0`, ... Empty if not sent.
    QString watchingPlatform;
    QString watchingId;

    static DggUser fromJson(const QJsonObject &obj);

    bool hasRole(const QString &role) const;

    /// Moderators, protected users and admins. Privileges come only from
    /// roles, never from flairs.
    bool isPrivileged() const;

    bool isSubscriber() const;

    /// The server sends events that aren't caused by a person (e.g. the
    /// website lifting a mute) as user -1.
    bool isSystem() const;
};

/// A `MSG` event, with the derived values the message builder needs.
/// Where the site and its services are. Everything that names a host or a
/// path of destiny.gg lives here, nowhere else. `CHATTERINO_DGG_SITE` (e.g.
/// `https://www.destiny.gg`) and `CHATTERINO_DGG_LOGS` (the log search page,
/// to which `?username=<nick>` is added) override the defaults.
QUrl dggSiteUrl(const QString &path = {});
QUrl dggChatPageUrl();
QUrl dggUserInfoUrl(const QString &nick);
QUrl dggProfileUrl(const QString &nick);
QUrl dggBigscreenUrl(const QString &platform, const QString &id);
QUrl dggLogsUrl(const QString &nick);

/// Reads the hashed images (`<name>.<hash>.png`: the stock profile pictures,
/// the icon sheet, ...) out of a page of the site and remembers where each
/// is, replacing what it knew. The hashes change when the site is rebuilt, so
/// nothing about them is built in. A file named with a path is resolved
/// against `pageUrl`; a bare file name gets the path of another image of
/// the page. Returns how many it found. GUI thread only.
int dggLearnSiteImages(const QByteArray &page, const QUrl &pageUrl);

/// A learned image by its name without hash and extension (`icons`,
/// `profile-photo-temmie`). Empty if not learned.
QUrl dggSiteImage(const QString &name);

/// The picture for the `photo` of a user: a full URL stays as it is, a stock
/// name (`buddy`) is looked up among the learned images. Empty if unknown.
QUrl dggPhotoUrl(const QString &photo);

/// What the site's user info (see dggUserInfoUrl()) says of a user: the
/// fields of chat's `DggUser` it has, plus the profile's own.
struct DggProfile {
    DggUser user;
    QString bio;
    QString gender;
    QString age;
};

/// Reads the answer of the site's user info: `{success, data: {...}}`.
/// Nullopt if it says no or has no data.
std::optional<DggProfile> parseDggUserInfo(const QJsonObject &root);

struct DggChatMessage {
    DggUser user;

    /// The text exactly as sent, including a leading `/me `.
    QString rawText;

    /// The text to show: `rawText` without the `/me ` prefix.
    QString text;

    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    /// chat-gui's message hash (see dggMessageHash()). Unique enough to
    /// recognise a message that arrives twice, e.g. in a backlog replay.
    QString hash;

    /// The message started with `/me `.
    bool isAction = false;

    /// The message starts with `>` and the author may use greentext
    /// (subscribers and privileged users only).
    bool greentext = false;

    /// A private message (`PRIVMSG`), not a line of public chat. For one the
    /// user received, `user` is the sender. For one the user sent, `user` is
    /// the user themself and `whisperTarget` is who it went to.
    bool isWhisper = false;
    QString whisperTarget;

    QDateTime time() const;
};

/// A `DEATH` event. Unlike `MUTE` and `BAN`, the user fields are those of the
/// user who died, and `data` is the cause (free text, may contain emotes).
struct DggDeath {
    DggUser user;

    /// What the site shows after the nick, e.g. `engaged in dishonorable PVP.`
    QString cause;

    /// How long the user is silenced for. 0 if the event has no duration.
    qint64 durationSeconds = 0;

    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    QDateTime time() const;
};

/// Parses a `DEATH` payload. Returns nullopt if it has no nick.
std::optional<DggDeath> parseDggDeath(const QJsonObject &obj, qint64 nowMs);

/// The paid and highlight events of the site: `SUBSCRIPTION`, `GIFTSUB`,
/// `MASSGIFT`, `DONATION` and `SPOTLIGHT`. chat-gui shows each as a card in
/// the chat (and in its event bar); here they are lines.
struct DggEvent {
    enum class Kind {
        Subscription,
        GiftSub,
        MassGift,
        Donation,
        Spotlight,
    };

    Kind kind = Kind::Subscription;

    /// Who subscribed, gifted or donated; for a spotlight the author of the
    /// message. Empty nick for an anonymous donation.
    DggUser user;

    /// The receiver of a `GIFTSUB`.
    DggUser recipient;

    /// The message that came with it (or the quoted message of a spotlight).
    QString text;

    int tier = 0;

    /// `tierLabel` of the event, or `Tier N` if it has none.
    QString tierLabel;

    /// `DONATION`: US cents.
    qint64 amountCents = 0;

    /// `SUBSCRIPTION`: months in a row, 0 if not sent.
    int streak = 0;

    /// `MASSGIFT`: how many subs were given away.
    int quantity = 0;

    /// `SPOTLIGHT`: the nick of the moderator.
    QString spotlightedBy;

    /// `SPOTLIGHT`: the hash of the message that was spotlighted (see
    /// dggMessageHash()), which is also the id of its chat line.
    QString messageKey;

    /// Identifies the event (for dropping repeats, e.g. from the backlog).
    QString uuid;

    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    QDateTime time() const;
};

/// Whether `name` is the name of one of the events of DggEvent.
bool isDggEventName(QStringView name);

/// Parses one of those events. Returns nullopt for another name. The users
/// are the nested `user` and `recipient` objects.
std::optional<DggEvent> parseDggEvent(QStringView name, const QJsonObject &obj,
                                      qint64 nowMs);

/// `md5(timestampMs + userId + text)` as lowercase hex, the same recipe
/// chat-gui uses to identify a message (and the server uses to name a
/// spotlighted one). A user id of 0 (unknown) counts as empty.
QString dggMessageHash(qint64 timestampMs, qint64 userId, const QString &text);

/// An `XPOST` event: a post on X that was shared in chat.
struct DggXPost {
    /// The X handle, without the `@`
    QString handle;
    QString text;
    /// The link to the post
    QString url;
    QString uuid;
    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    QDateTime time() const;
};

/// Parses an `XPOST` payload. Returns nullopt without a handle or a link.
std::optional<DggXPost> parseDggXPost(const QJsonObject &obj, qint64 nowMs);

/// `VESTABOARD_LEAD`, `VESTABOARD_HOURLY` and `VESTABOARD_RESET`: the
/// announcements of the Vestaboard auction. They carry the board of the
/// leading design (a matrix of tile codes).
struct DggVestaboard {
    enum class Kind {
        /// A design took the lead (a new one, or a contribution lifted an
        /// existing one)
        Lead,
        /// The hourly reminder of the leader
        Hourly,
        /// The auction ended and a new one began
        Reset,
    };

    struct Contributor {
        QString name;
        qint64 amountCents = 0;
    };

    Kind kind = Kind::Lead;
    /// Empty for an hourly reminder when nobody has submitted yet
    QString submitter;
    /// Who made the design
    QString artist;
    qint64 totalCents = 0;
    bool isNewDesign = false;
    /// Rows of tile codes (0 blank, 1-26 A-Z, 27-36 digits, 37-60
    /// punctuation, 63-71 colours)
    std::vector<std::vector<int>> characters;
    std::vector<Contributor> contributors;
    QString uuid;
    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    QDateTime time() const;
};

/// Whether `name` is one of the three Vestaboard events.
bool isDggVestaboardName(QStringView name);

/// Parses one of the Vestaboard events. Returns nullopt for another name.
std::optional<DggVestaboard> parseDggVestaboard(QStringView name,
                                                const QJsonObject &obj,
                                                qint64 nowMs);

/// A `PIN` event: the message a moderator pinned. An empty `text` means the
/// pin was removed (the site sends the same event for both).
struct DggPin {
    /// Who pinned it: the nick and, if the event has them, the flairs.
    DggUser user;

    QString text;

    /// Identifies the pin (a repeat of the same pin, e.g. after a
    /// reconnect, has the same uuid).
    QString uuid;

    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    QDateTime time() const;

    /// The pin was removed: there is nothing pinned anymore.
    bool isRemoval() const;
};

/// Parses a `PIN` payload. Never fails: a payload without text is a removal.
DggPin parseDggPin(const QJsonObject &obj, qint64 nowMs);

/// A `POLLSTART` event: a poll that is running. `VOTECAST` and `POLLSTOP`
/// update it afterwards (see DggChannel).
struct DggPoll {
    /// Who started it.
    QString nick;
    QString question;
    QStringList options;

    /// The votes of each option, one entry for each of `options`. For a
    /// weighted poll these are weighted. All zero while a hidden poll runs.
    std::vector<qint64> totals;

    /// The votes cast in all (weighted for a weighted poll).
    qint64 votesCast = 0;

    /// Votes count by the tier of the subscriber.
    bool weighted = false;

    /// The results are only sent when the poll ends.
    bool hidden = false;

    /// The user may still vote (always false for a guest).
    bool canVote = false;

    /// The option (1-based) the user voted for, 0 if none.
    int myVote = 0;

    /// How long the poll runs.
    qint64 durationMs = 0;

    /// When the poll started and when it ends, in Unix ms of the *local*
    /// clock (the server's clock offset is applied to both).
    qint64 startedAtMs = 0;
    qint64 endsAtMs = 0;

    /// `POLLSTOP` came (or the connection was lost).
    bool ended = false;

    /// Identifies the poll: a `POLLSTART` with the same key is the same poll
    /// being sent again (after a reconnect). Made of who started it, the
    /// question and the start on the server's clock (so it doesn't change
    /// with the clock offset of the connection).
    QString id;

    const QString &key() const
    {
        return this->id;
    }
};

/// Parses a `POLLSTART` payload. Returns nullopt without a question or
/// options.
std::optional<DggPoll> parseDggPollStart(const QJsonObject &obj, qint64 nowMs);

/// The final numbers of a `POLLSTOP`.
struct DggPollEnd {
    std::vector<qint64> totals;
    qint64 votesCast = 0;
};

/// Parses a `POLLSTOP` payload. Returns nullopt if it carries no totals (a
/// stop of a poll the server did not describe).
std::optional<DggPollEnd> parseDggPollStop(const QJsonObject &obj);

/// A `VOTECAST` (somebody voted: for a hidden poll the option is not sent)
/// or `VOTECOUNTED` (your own vote arrived) payload.
struct DggPollVote {
    /// 1-based, 0 if not sent.
    int option = 0;
    qint64 quantity = 1;
};

DggPollVote parseDggPollVote(const QJsonObject &obj);

/// The event's `timestamp` (Unix ms), or `nowMs` if it has none.
qint64 dggTimestampMs(const QJsonObject &obj, qint64 nowMs);

/// Fills in what is derived from `rawText` and `user` of a public chat line:
/// `isAction` and `text` (a leading `/me ` is cut off, in any case) and
/// `greentext` (only subscribers and moderators get it). Used for lines from
/// `MSG` and from the mentions API, so both are read the same way.
void dggApplyChatText(DggChatMessage &msg);

/// Parses a `MSG` payload. Returns nullopt if it has no nick or no text.
std::optional<DggChatMessage> parseDggChatMessage(const QJsonObject &obj,
                                                  qint64 nowMs);

/// Parses a `PRIVMSG` payload (a whisper somebody sent to the user). Returns
/// nullopt if it has no nick or no text. `hash` is built from `messageid`
/// when the server sends one, so a reconnect can't show a whisper twice.
std::optional<DggChatMessage> parseDggWhisper(const QJsonObject &obj,
                                              qint64 nowMs);

/// The line for `MUTE`, `UNMUTE`, `BAN` and `UNBAN`, worded like chat-gui.
/// Returns an empty string for any other event or a payload without a target.
QString dggModerationText(QStringView event, const QJsonObject &obj);

/// The line for `SUBONLY`.
QString dggSubOnlyText(const QJsonObject &obj);

/// A readable message for an `ERR` payload (`{"description": "<code>"}`).
QString dggErrorText(const QJsonObject &obj);

}  // namespace chatterino
