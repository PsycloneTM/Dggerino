#include "providers/dgg/DggMessage.hpp"

#include "util/FormatTime.hpp"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <limits>
#include <vector>

namespace chatterino {

using namespace Qt::Literals;

namespace {

QStringList toStringList(const QJsonValue &value)
{
    QStringList out;
    for (const auto &entry : value.toArray())
    {
        out.append(entry.toString());
    }
    return out;
}

qint64 toInt64(const QJsonValue &value)
{
    return static_cast<qint64>(value.toDouble());
}

/// A number that the server may have sent as a string.
qint64 toLooseInt64(const QJsonValue &value, qint64 fallback)
{
    if (value.isDouble())
    {
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString())
    {
        bool ok = false;
        const auto number = value.toString().toLongLong(&ok);
        if (ok)
        {
            return number;
        }
    }
    return fallback;
}

/// A point in time as the poll events spell it: an ISO date (what JavaScript's
/// `toISOString()` makes) or Unix milliseconds.
qint64 toDateMs(const QJsonValue &value, qint64 fallback)
{
    if (value.isDouble())
    {
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString())
    {
        const auto date =
            QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
        if (date.isValid())
        {
            return date.toMSecsSinceEpoch();
        }
    }
    return fallback;
}

/// Mute lengths are seconds. This keeps a bogus huge value from overflowing
/// formatTime()'s int parameter.
QString durationText(qint64 seconds)
{
    constexpr qint64 max = std::numeric_limits<int>::max();
    return formatTime(static_cast<int>(std::min(seconds, max)));
}

/// " by <nick>" for a real moderator, nothing for the system user or when the
/// event doesn't name one.
QString byText(const DggUser &moderator)
{
    if (moderator.isSystem() || moderator.nick.isEmpty())
    {
        return {};
    }
    return u" by "_s + moderator.nick;
}

QString tierText(int tier)
{
    if (tier > 1)
    {
        return u" (Tier %1+)"_s.arg(tier);
    }
    return {};
}

/// The texts chat-gui has for error codes that have no special handling.
const QHash<QString, QString> &errorStrings()
{
    static const QHash<QString, QString> strings{
        {u"unknown"_s,
         u"Unknown error, this usually indicates an internal problem :("_s},
        {u"nopermission"_s,
         u"You do not have the required permissions to use that"_s},
        {u"protocolerror"_s, u"Invalid or badly formatted"_s},
        {u"needlogin"_s, u"You have to be logged in to use that"_s},
        {u"msgempty"_s, u"Message cannot be empty"_s},
        {u"msgtoolong"_s, u"Message cannot be longer than 512 characters"_s},
        {u"invalidmsg"_s, u"The message was invalid"_s},
        {u"throttled"_s,
         u"Throttled! You were trying to send messages too fast"_s},
        {u"duplicate"_s,
         u"The message is identical to the last one you sent"_s},
        {u"submode"_s, u"The channel is currently in subscriber only mode"_s},
        {u"needbanreason"_s, u"Providing a reason for the ban is mandatory"_s},
        {u"privmsgbanned"_s, u"Cannot send private messages while banned"_s},
        {u"requiresocket"_s, u"This chat requires WebSockets"_s},
        {u"toomanyconnections"_s, u"Only 5 concurrent connections allowed"_s},
        {u"socketerror"_s, u"Error contacting server"_s},
        {u"privmsgaccounttooyoung"_s,
         u"Your account is too recent to send private messages"_s},
        {u"notfound"_s, u"The user was not found"_s},
        {u"notconnected"_s, u"You have to be connected to use that"_s},
        {u"activepoll"_s, u"Poll already started."_s},
        {u"noactivepoll"_s, u"No poll started."_s},
        {u"alreadyvoted"_s, u"You have already voted!"_s},
        {u"nochatting"_s,
         u"You aren't allowed to chat. Either you haven't picked a username, "
         u"or a mod disabled your privileges."_s},
        {u"duplicatephrase"_s, u"Banned phrase already exists."_s},
        {u"nophrase"_s, u"Banned phrase doesn't exist."_s},
        {u"invalidphrase"_s,
         u"Invalid phrase provided. Either the phrase was empty, or its "
         u"regex was invalid."_s},
        {u"cantbanprotected"_s, u"Protected users can't die."_s},
    };
    return strings;
}

}  // namespace

DggUser DggUser::fromJson(const QJsonObject &obj)
{
    DggUser user;
    user.id = toInt64(obj.value("id"_L1));
    user.nick = obj.value("nick"_L1).toString();
    user.roles = toStringList(obj.value("roles"_L1));
    user.features = toStringList(obj.value("features"_L1));
    // `subscription` is null for users without one, which toObject() turns
    // into an empty object (tier 0).
    user.subTier =
        obj.value("subscription"_L1).toObject().value("tier"_L1).toInt();
    user.patreonMember = obj.value("patreonMember"_L1).toBool();
    user.photo = obj.value("photo"_L1).toString();
    user.createdDate = QDateTime::fromString(
        obj.value("createdDate"_L1).toString(), Qt::ISODate);
    const auto watching = obj.value("watching"_L1).toObject();
    user.watchingPlatform = watching.value("platform"_L1).toString();
    user.watchingId = watching.value("id"_L1).toString();
    return user;
}

namespace {

QHash<QString, QUrl> &siteImages()
{
    static QHash<QString, QUrl> images;
    return images;
}

QUrl siteBase()
{
    const auto fromEnv = qEnvironmentVariable("CHATTERINO_DGG_SITE");
    return QUrl(fromEnv.isEmpty() ? u"https://www.destiny.gg"_s : fromEnv);
}

}  // namespace

QUrl dggSiteUrl(const QString &path)
{
    auto base = siteBase();
    if (path.isEmpty())
    {
        return base;
    }
    return base.resolved(QUrl(path.startsWith(u'/') ? path : u'/' + path));
}

QUrl dggChatPageUrl()
{
    return dggSiteUrl(u"/embed/chat"_s);
}

QUrl dggUserInfoUrl(const QString &nick)
{
    auto url = dggSiteUrl(u"/api/chat/userinfo"_s);
    QUrlQuery query;
    query.addQueryItem(u"username"_s, nick);
    url.setQuery(query);
    return url;
}

QUrl dggProfileUrl(const QString &nick)
{
    return dggSiteUrl(u"/u/"_s +
                      QString::fromUtf8(QUrl::toPercentEncoding(nick)));
}

QUrl dggBigscreenUrl(const QString &platform, const QString &id)
{
    // the link chat-gui makes: the bigscreen page of that stream
    auto url = dggSiteUrl(u"/bigscreen"_s);
    url.setFragment(platform + u'/' + id);
    return url;
}

QUrl dggLogsUrl(const QString &nick)
{
    const auto fromEnv = qEnvironmentVariable("CHATTERINO_DGG_LOGS");
    QUrl url(fromEnv.isEmpty() ? u"https://rustlesearch.dev/"_s : fromEnv);
    QUrlQuery query;
    query.addQueryItem(u"username"_s, nick.toLower());
    url.setQuery(query);
    return url;
}

int dggLearnSiteImages(const QByteArray &page, const QUrl &pageUrl)
{
    // [path/]<name>.<hash>.png, the path being a URL, an absolute path or a
    // relative one (`url(img/icons.<hash>.png)` in the site's CSS, resolved
    // against the stylesheet's address)
    static const QRegularExpression re(QStringLiteral(
        R"(((?:https?:)?//[^\s"'()<>]*?/|/[^\s"'()<>]*?/|(?:[\w.-]+/)+)?([\w-]+)\.([0-9a-f]{8,})\.png)"));

    struct Found {
        QString prefix;
        QString name;
        QString file;
    };
    std::vector<Found> found;
    QString commonPrefix;

    // A page that carries its config as JSON escapes the slashes of its URLs
    // (`https:\/\/cdn.example.org\/img\/x.hash.png`): read them as plain ones.
    auto text = QString::fromUtf8(page);
    text.replace(u"\\/"_s, u"/"_s);
    text.replace(u"\\u002f"_s, u"/"_s, Qt::CaseInsensitive);
    auto matches = re.globalMatch(text);
    while (matches.hasNext())
    {
        const auto m = matches.next();
        Found f{m.captured(1), m.captured(2),
                m.captured(2) + u'.' + m.captured(3) + u".png"};
        if (commonPrefix.isEmpty() && !f.prefix.isEmpty())
        {
            commonPrefix = f.prefix;
        }
        found.push_back(std::move(f));
    }

    auto &images = siteImages();
    for (const auto &f : found)
    {
        const auto &prefix = f.prefix.isEmpty() ? commonPrefix : f.prefix;
        images.insert(f.name, pageUrl.resolved(QUrl(prefix + f.file)));
    }
    return static_cast<int>(found.size());
}

QUrl dggSiteImage(const QString &name)
{
    return siteImages().value(name);
}

QUrl dggPhotoUrl(const QString &photo)
{
    if (photo.isEmpty())
    {
        return {};
    }
    if (photo.startsWith(u"https://"_s))
    {
        return QUrl(photo);
    }
    return dggSiteImage(u"profile-photo-"_s + photo);
}

std::optional<DggProfile> parseDggUserInfo(const QJsonObject &root)
{
    const auto data = root.value("data"_L1).toObject();
    if (!root.value("success"_L1).toBool() || data.isEmpty())
    {
        return std::nullopt;
    }
    DggProfile profile;
    profile.user = DggUser::fromJson(data);
    profile.bio = data.value("bio"_L1).toString().trimmed();
    profile.gender = data.value("gender"_L1).toString();
    profile.age = data.value("age"_L1).toString();
    return profile;
}

bool DggUser::hasRole(const QString &role) const
{
    return this->roles.contains(role);
}

bool DggUser::isPrivileged() const
{
    return this->hasRole(u"MODERATOR"_s) || this->hasRole(u"PROTECTED"_s) ||
           this->hasRole(u"ADMIN"_s);
}

bool DggUser::isSubscriber() const
{
    return this->subTier > 0;
}

bool DggUser::isSystem() const
{
    return this->id == -1;
}

QDateTime DggChatMessage::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

QDateTime DggDeath::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

QString dggMessageHash(qint64 timestampMs, qint64 userId, const QString &text)
{
    QByteArray input = QByteArray::number(timestampMs);
    if (userId != 0)
    {
        input += QByteArray::number(userId);
    }
    input += text.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(input, QCryptographicHash::Md5).toHex());
}

qint64 dggTimestampMs(const QJsonObject &obj, qint64 nowMs)
{
    const auto value = obj.value("timestamp"_L1);
    if (!value.isDouble())
    {
        return nowMs;
    }
    return toInt64(value);
}

std::optional<DggChatMessage> parseDggChatMessage(const QJsonObject &obj,
                                                  qint64 nowMs)
{
    DggChatMessage msg;
    msg.user = DggUser::fromJson(obj);
    msg.rawText = obj.value("data"_L1).toString();
    if (msg.user.nick.isEmpty() || msg.rawText.isEmpty())
    {
        return std::nullopt;
    }

    msg.timestampMs = dggTimestampMs(obj, nowMs);
    msg.hash = dggMessageHash(msg.timestampMs, msg.user.id, msg.rawText);

    // chat-gui strips a leading "/me " (any case) and styles the line as an
    // action.
    msg.isAction = msg.rawText.startsWith(u"/me "_s, Qt::CaseInsensitive);
    msg.text = msg.isAction ? msg.rawText.mid(4) : msg.rawText;

    msg.greentext = msg.text.startsWith(u'>') &&
                    (msg.user.isSubscriber() || msg.user.isPrivileged());
    return msg;
}

std::optional<DggChatMessage> parseDggWhisper(const QJsonObject &obj,
                                              qint64 nowMs)
{
    DggChatMessage msg;
    msg.user = DggUser::fromJson(obj);
    msg.rawText = obj.value("data"_L1).toString();
    if (msg.user.nick.isEmpty() || msg.rawText.isEmpty())
    {
        return std::nullopt;
    }

    msg.timestampMs = dggTimestampMs(obj, nowMs);

    // `messageid` is a number or a string depending on the server
    const auto messageId = obj.value("messageid"_L1);
    QString key;
    if (messageId.isDouble())
    {
        key = QString::number(toInt64(messageId));
    }
    else
    {
        key = messageId.toString();
    }
    msg.hash = key.isEmpty() ? dggMessageHash(msg.timestampMs, msg.user.id,
                                              u"pm|"_s + msg.rawText)
                             : u"pm|"_s + key;

    // chat-gui shows a whisper as plain text: no /me, no greentext
    msg.text = msg.rawText;
    msg.isWhisper = true;
    return msg;
}

std::optional<DggDeath> parseDggDeath(const QJsonObject &obj, qint64 nowMs)
{
    DggDeath death;
    death.user = DggUser::fromJson(obj);
    if (death.user.nick.isEmpty())
    {
        return std::nullopt;
    }

    death.cause = obj.value("data"_L1).toString().trimmed();
    death.durationSeconds = toInt64(obj.value("duration"_L1));
    death.timestampMs = dggTimestampMs(obj, nowMs);
    return death;
}

QDateTime DggEvent::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

bool isDggEventName(QStringView name)
{
    return name == "SUBSCRIPTION"_L1 || name == "GIFTSUB"_L1 ||
           name == "MASSGIFT"_L1 || name == "DONATION"_L1 ||
           name == "SPOTLIGHT"_L1;
}

std::optional<DggEvent> parseDggEvent(QStringView name, const QJsonObject &obj,
                                      qint64 nowMs)
{
    DggEvent event;
    if (name == "SUBSCRIPTION"_L1)
    {
        event.kind = DggEvent::Kind::Subscription;
    }
    else if (name == "GIFTSUB"_L1)
    {
        event.kind = DggEvent::Kind::GiftSub;
    }
    else if (name == "MASSGIFT"_L1)
    {
        event.kind = DggEvent::Kind::MassGift;
    }
    else if (name == "DONATION"_L1)
    {
        event.kind = DggEvent::Kind::Donation;
    }
    else if (name == "SPOTLIGHT"_L1)
    {
        event.kind = DggEvent::Kind::Spotlight;
    }
    else
    {
        return std::nullopt;
    }

    event.user = DggUser::fromJson(obj.value("user"_L1).toObject());
    if (event.kind == DggEvent::Kind::GiftSub)
    {
        event.recipient =
            DggUser::fromJson(obj.value("recipient"_L1).toObject());
    }
    if (event.kind == DggEvent::Kind::Spotlight && event.user.nick.isEmpty())
    {
        // the payload also has the author's nick at the top
        event.user.nick = obj.value("nick"_L1).toString();
    }

    event.text = obj.value("data"_L1).toString().trimmed();
    event.tier = static_cast<int>(toInt64(obj.value("tier"_L1)));
    event.tierLabel = obj.value("tierLabel"_L1).toString();
    if (event.tierLabel.isEmpty() && event.tier > 0)
    {
        event.tierLabel = u"Tier %1"_s.arg(event.tier);
    }
    event.amountCents = toInt64(obj.value("amount"_L1));
    event.streak = static_cast<int>(toInt64(obj.value("streak"_L1)));
    event.quantity = static_cast<int>(toInt64(obj.value("quantity"_L1)));
    event.spotlightedBy = obj.value("spotlightedBy"_L1).toString();
    event.messageKey = obj.value("key"_L1).toString();
    event.uuid = obj.value("uuid"_L1).toString();
    event.timestampMs = dggTimestampMs(obj, nowMs);

    if (event.kind == DggEvent::Kind::GiftSub && event.recipient.nick.isEmpty())
    {
        return std::nullopt;
    }
    if (event.kind == DggEvent::Kind::Spotlight &&
        (event.user.nick.isEmpty() || event.text.isEmpty()))
    {
        return std::nullopt;
    }
    return event;
}

QDateTime DggXPost::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

std::optional<DggXPost> parseDggXPost(const QJsonObject &obj, qint64 nowMs)
{
    DggXPost post;
    post.handle = obj.value("handle"_L1).toString().trimmed();
    if (post.handle.startsWith(u'@'))
    {
        post.handle.remove(0, 1);
    }
    post.text = obj.value("text"_L1).toString().trimmed();
    post.url = obj.value("url"_L1).toString().trimmed();
    post.uuid = obj.value("uuid"_L1).toString();
    post.timestampMs = dggTimestampMs(obj, nowMs);

    if (post.handle.isEmpty())
    {
        return std::nullopt;
    }
    return post;
}

QDateTime DggPin::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

bool DggPin::isRemoval() const
{
    return this->text.isEmpty();
}

DggPin parseDggPin(const QJsonObject &obj, qint64 nowMs)
{
    DggPin pin;
    pin.user = DggUser::fromJson(obj);
    pin.text = obj.value("data"_L1).toString().trimmed();
    pin.uuid = obj.value("uuid"_L1).toString();
    pin.timestampMs = dggTimestampMs(obj, nowMs);
    return pin;
}

std::optional<DggPoll> parseDggPollStart(const QJsonObject &obj, qint64 nowMs)
{
    DggPoll poll;
    poll.nick = obj.value("nick"_L1).toString();
    poll.question = obj.value("question"_L1).toString().trimmed();
    poll.options = toStringList(obj.value("options"_L1));
    if (poll.question.isEmpty() || poll.options.isEmpty())
    {
        return std::nullopt;
    }

    // One total for each option, whatever the server sent
    poll.totals.assign(static_cast<size_t>(poll.options.size()), 0);
    qint64 sum = 0;
    const auto totals = obj.value("totals"_L1).toArray();
    for (qsizetype i = 0; i < totals.size() && i < poll.options.size(); i++)
    {
        const auto total = std::max<qint64>(0, toLooseInt64(totals[i], 0));
        poll.totals[static_cast<size_t>(i)] = total;
        sum += total;
    }
    poll.votesCast =
        std::max<qint64>(0, toLooseInt64(obj.value("totalvotes"_L1), sum));

    poll.weighted = obj.value("weighted"_L1).toBool();
    poll.hidden = obj.value("hidden"_L1).toBool();
    poll.canVote = obj.value("canvote"_L1).toBool();
    poll.myVote = static_cast<int>(
        std::max<qint64>(0, toLooseInt64(obj.value("myvote"_L1), 0)));

    poll.durationMs = toLooseInt64(obj.value("time"_L1), 0);
    if (poll.durationMs <= 0)
    {
        // chat-gui's default
        poll.durationMs = 30000;
    }

    // `start` and `now` are on the server's clock. The poll's end is
    // start + duration there; the difference between `now` and ours moves it
    // to our clock (what chat-gui does with its `offset`).
    const auto startMs = toDateMs(obj.value("start"_L1), nowMs);
    const auto serverNowMs = toDateMs(obj.value("now"_L1), nowMs);
    const auto offsetMs = serverNowMs - nowMs;
    poll.id =
        u"%1|%2|%3"_s.arg(poll.nick, QString::number(startMs), poll.question);
    poll.startedAtMs = startMs - offsetMs;
    poll.endsAtMs = poll.startedAtMs + poll.durationMs;
    return poll;
}

std::optional<DggPollEnd> parseDggPollStop(const QJsonObject &obj)
{
    const auto totals = obj.value("totals"_L1);
    if (!totals.isArray())
    {
        return std::nullopt;
    }

    DggPollEnd end;
    qint64 sum = 0;
    for (const auto &entry : totals.toArray())
    {
        const auto total = std::max<qint64>(0, toLooseInt64(entry, 0));
        end.totals.push_back(total);
        sum += total;
    }
    end.votesCast =
        std::max<qint64>(0, toLooseInt64(obj.value("totalvotes"_L1), sum));
    return end;
}

DggPollVote parseDggPollVote(const QJsonObject &obj)
{
    DggPollVote vote;
    vote.option = static_cast<int>(
        std::max<qint64>(0, toLooseInt64(obj.value("vote"_L1), 0)));
    vote.quantity =
        std::max<qint64>(0, toLooseInt64(obj.value("quantity"_L1), 1));
    return vote;
}

QDateTime DggVestaboard::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

bool isDggVestaboardName(QStringView name)
{
    return name == "VESTABOARD_LEAD"_L1 || name == "VESTABOARD_HOURLY"_L1 ||
           name == "VESTABOARD_RESET"_L1;
}

std::optional<DggVestaboard> parseDggVestaboard(QStringView name,
                                                const QJsonObject &obj,
                                                qint64 nowMs)
{
    DggVestaboard board;
    if (name == "VESTABOARD_LEAD"_L1)
    {
        board.kind = DggVestaboard::Kind::Lead;
    }
    else if (name == "VESTABOARD_HOURLY"_L1)
    {
        board.kind = DggVestaboard::Kind::Hourly;
    }
    else if (name == "VESTABOARD_RESET"_L1)
    {
        board.kind = DggVestaboard::Kind::Reset;
    }
    else
    {
        return std::nullopt;
    }

    board.submitter = obj.value("submitter"_L1).toString().trimmed();
    board.artist = obj.value("artist"_L1).toString().trimmed();
    board.totalCents = toInt64(obj.value("total"_L1));
    board.isNewDesign = obj.value("isNewDesign"_L1).toBool();
    board.uuid = obj.value("uuid"_L1).toString();
    board.timestampMs = dggTimestampMs(obj, nowMs);

    for (const auto &rowValue : obj.value("characters"_L1).toArray())
    {
        std::vector<int> row;
        for (const auto &code : rowValue.toArray())
        {
            row.push_back(static_cast<int>(toInt64(code)));
        }
        board.characters.push_back(std::move(row));
    }

    for (const auto &value : obj.value("contributors"_L1).toArray())
    {
        const auto contributor = value.toObject();
        DggVestaboard::Contributor entry;
        entry.name = contributor.value("name"_L1).toString();
        entry.amountCents = toInt64(contributor.value("amountCents"_L1));
        if (!entry.name.isEmpty())
        {
            board.contributors.push_back(std::move(entry));
        }
    }

    // A lead or a win always names who it was
    if (board.kind != DggVestaboard::Kind::Hourly && board.submitter.isEmpty())
    {
        return std::nullopt;
    }
    return board;
}

QString dggModerationText(QStringView event, const QJsonObject &obj)
{
    const auto target = obj.value("data"_L1).toString();
    if (target.isEmpty())
    {
        return {};
    }

    const auto moderator = DggUser::fromJson(obj);
    const auto by = byText(moderator);

    if (event == "MUTE"_L1)
    {
        QString duration;
        const auto seconds = toInt64(obj.value("duration"_L1));
        if (seconds > 0)
        {
            duration = u" for "_s + durationText(seconds);
        }

        if (by.isEmpty())
        {
            return u"%1 has been muted%2."_s.arg(target, duration);
        }
        return u"%1 muted%2%3."_s.arg(target, by, duration);
    }
    if (event == "UNMUTE"_L1)
    {
        if (by.isEmpty())
        {
            return u"%1 has been unmuted."_s.arg(target);
        }
        return u"%1 unmuted%2."_s.arg(target, by);
    }
    if (event == "BAN"_L1)
    {
        if (by.isEmpty())
        {
            return u"%1 has been banned."_s.arg(target);
        }
        return u"%1 banned%2."_s.arg(target, by);
    }
    if (event == "UNBAN"_L1)
    {
        if (by.isEmpty())
        {
            return u"%1 has been unbanned."_s.arg(target);
        }
        return u"%1 unbanned%2."_s.arg(target, by);
    }

    return {};
}

QString dggSubOnlyText(const QJsonObject &obj)
{
    const bool on = obj.value("data"_L1).toString() == "on"_L1;
    const int tier = obj.value("tier"_L1).toInt();

    QString by;
    const auto nick = obj.value("nick"_L1).toString();
    if (!nick.isEmpty())
    {
        by = u" by "_s + nick;
    }

    return u"Subscriber only mode%1 %2%3."_s.arg(
        tierText(on ? std::max(tier, 1) : 0), on ? u"enabled"_s : u"disabled"_s,
        by);
}

QString dggErrorText(const QJsonObject &obj)
{
    const auto desc = obj.value("description"_L1).toString();

    if (desc == "banned"_L1)
    {
        return u"You have been banned! Check your profile "
               u"(%1) for more information. "
               u"Subscribing or donating removes non-permanent bans."_s.arg(
                   dggSiteUrl(u"/profile"_s).toString());
    }
    if (desc == "muted"_L1)
    {
        const auto seconds = toInt64(obj.value("muteTimeLeft"_L1));
        if (seconds > 0)
        {
            return u"You are temporarily muted! You can chat again in %1. "
                   u"Subscribe or donate to remove the mute immediately."_s.arg(
                       durationText(seconds));
        }
        return u"You are temporarily muted! Subscribe or donate to remove "
               u"the mute immediately."_s;
    }
    if (desc == "submode"_L1)
    {
        const int tier = std::max(obj.value("tier"_L1).toInt(), 1);
        return u"The channel is currently in subscriber only mode%1"_s.arg(
            tierText(tier));
    }
    if (desc == "bannedphrase"_L1)
    {
        return u"Your message was blocked because it contained this banned "
               u"phrase: \"%1\"."_s.arg(obj.value("filtered"_L1).toString());
    }

    const auto known = errorStrings().value(desc);
    if (!known.isEmpty())
    {
        return known;
    }
    return desc.isEmpty() ? u"Unknown error."_s : desc;
}

}  // namespace chatterino
