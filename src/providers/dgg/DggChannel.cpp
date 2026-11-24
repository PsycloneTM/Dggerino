#include "providers/dgg/DggChannel.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageFlag.hpp"
#include "providers/dgg/DggAccountManager.hpp"
#include "providers/dgg/DggChatServer.hpp"

#include <utility>

namespace chatterino {

const QString &DggChannel::channelName()
{
    static const QString name(QStringLiteral("destiny.gg"));
    return name;
}

DggChannel::DggChannel()
    : Channel(DggChannel::channelName(), Channel::Type::Dgg)
{
}

DggChannel::~DggChannel()
{
    this->aboutToClose.invoke();
}

const QString &DggChannel::getDisplayName() const
{
    return this->getName();
}

bool DggChannel::isLive() const
{
    return this->streamInfo_.isLive;
}

const DggStreamInfo &DggChannel::streamInfo() const
{
    return this->streamInfo_;
}

void DggChannel::setStreamInfo(DggStreamInfo info)
{
    if (info == this->streamInfo_)
    {
        return;
    }

    const bool liveChanged = info.isLive != this->streamInfo_.isLive;
    this->streamInfo_ = std::move(info);

    this->streamDataChanged.invoke();
    if (liveChanged)
    {
        this->liveStatusChanged.invoke();
    }
}

bool DggChannel::canReconnect() const
{
    return true;
}

void DggChannel::reconnect()
{
    getApp()->getDggChatServer()->reconnect();
}

bool DggChannel::canSendMessage() const
{
    return getApp()->getAccounts()->dgg.isLoggedIn();
}

bool DggChannel::isWritable() const
{
    return this->canSendMessage();
}

void DggChannel::sendMessage(const QString &message)
{
    getApp()->getDggChatServer()->sendChatMessage(*this, message);
}

bool DggChannel::markSeen(const QString &key)
{
    if (this->seen_.contains(key))
    {
        return false;
    }

    this->seen_.insert(key);
    this->seenOrder_.push_back(key);
    while (this->seenOrder_.size() > MAX_SEEN_KEYS)
    {
        this->seen_.remove(this->seenOrder_.front());
        this->seenOrder_.pop_front();
    }
    return true;
}

void DggChannel::disableMessagesFrom(const QString &nick)
{
    const auto loginName = nick.toLower();
    if (loginName.isEmpty())
    {
        return;
    }

    for (const auto &message : this->getMessageSnapshot())
    {
        if (message->loginName != loginName)
        {
            continue;
        }
        if (message->flags.hasAny({MessageFlag::System, MessageFlag::Timeout,
                                   MessageFlag::Whisper}))
        {
            continue;
        }

        message->flags.set(MessageFlag::Disabled);
    }
}

void DggChannel::noteUser(const DggUser &user)
{
    if (user.nick.isEmpty() || user.isSystem())
    {
        return;
    }
    if (this->users_.size() >= MAX_USERS)
    {
        // only a cache: everyone who speaks again is back
        this->users_.clear();
    }
    auto merged = user;
    if (const auto it = this->users_.constFind(user.nick.toLower());
        it != this->users_.constEnd() && merged.photo.isEmpty() &&
        !merged.createdDate.isValid())
    {
        // `JOIN` and `NAMES` don't carry the picture, the date or what the
        // user watches: keep what a message told
        merged.photo = it->photo;
        merged.createdDate = it->createdDate;
        merged.watchingPlatform = it->watchingPlatform;
        merged.watchingId = it->watchingId;
    }
    this->users_.insert(user.nick.toLower(), merged);
}

std::optional<DggUser> DggChannel::findUser(const QString &nick) const
{
    const auto it = this->users_.constFind(nick.toLower());
    if (it == this->users_.constEnd())
    {
        return std::nullopt;
    }
    return *it;
}

std::optional<QString> DggChannel::canonicalNick(QStringView nick) const
{
    const auto it = this->users_.constFind(nick.toString().toLower());
    if (it == this->users_.constEnd())
    {
        return std::nullopt;
    }
    return it->nick;
}

std::vector<QString> DggChannel::nicks() const
{
    std::vector<QString> out;
    out.reserve(static_cast<size_t>(this->users_.size()));
    for (const auto &user : this->users_)
    {
        out.push_back(user.nick);
    }
    return out;
}

void DggChannel::setOnlineUsers(const std::vector<DggUser> &users)
{
    this->online_.clear();
    this->online_.reserve(static_cast<qsizetype>(users.size()));
    for (const auto &user : users)
    {
        if (user.nick.isEmpty() || user.isSystem())
        {
            continue;
        }
        this->noteUser(user);
        this->online_.insert(user.nick.toLower(), user);
    }
    this->onlineChanged.invoke();
}

void DggChannel::updateOnlineUsers(const std::vector<DggUser> &joined,
                                   const std::vector<DggUser> &quit)
{
    QSet<qint64> quitIds;
    for (const auto &user : quit)
    {
        if (!user.nick.isEmpty())
        {
            this->online_.remove(user.nick.toLower());
        }
        if (user.id > 0)
        {
            quitIds.insert(user.id);
        }
    }
    if (!quitIds.isEmpty())
    {
        // one pass for the whole batch
        this->online_.removeIf([&](auto it) {
            return quitIds.contains(it.value().id);
        });
    }

    for (const auto &user : joined)
    {
        if (user.nick.isEmpty() || user.isSystem())
        {
            continue;
        }
        this->noteUser(user);
        this->online_.insert(user.nick.toLower(), user);
    }
    this->onlineChanged.invoke();
}

void DggChannel::userJoined(const DggUser &user)
{
    this->updateOnlineUsers({user}, {});
}

void DggChannel::userQuit(const DggUser &user)
{
    this->updateOnlineUsers({}, {user});
}

void DggChannel::userUpdated(const DggUser &user)
{
    if (user.nick.isEmpty() || user.isSystem())
    {
        return;
    }

    bool present = this->online_.contains(user.nick.toLower());
    if (!present && user.id > 0)
    {
        // the entry is found by id, so a changed nick still replaces it
        present = this->online_.removeIf([&](auto it) {
            return it.value().id == user.id;
        }) > 0;
    }
    if (!present)
    {
        return;
    }

    this->noteUser(user);
    this->online_.insert(user.nick.toLower(), user);
    this->onlineChanged.invoke();
}

std::vector<DggUser> DggChannel::onlineUsers() const
{
    std::vector<DggUser> out;
    out.reserve(static_cast<size_t>(this->online_.size()));
    for (auto it = this->online_.cbegin(); it != this->online_.cend(); ++it)
    {
        // prefer what messages taught (flairs, subscription) over the bare
        // entry from NAMES
        if (const auto known = this->users_.constFind(it.key());
            known != this->users_.constEnd() && known->subTier > 0 &&
            it->subTier == 0)
        {
            auto merged = *it;
            merged.subTier = known->subTier;
            merged.features = known->features;
            out.push_back(std::move(merged));
        }
        else
        {
            out.push_back(*it);
        }
    }
    return out;
}

qsizetype DggChannel::onlineCount() const
{
    return this->online_.size();
}

const std::optional<DggPin> &DggChannel::pinnedMessage() const
{
    return this->pin_;
}

void DggChannel::setPinnedMessage(std::optional<DggPin> pin)
{
    if (!pin && !this->pin_)
    {
        return;
    }
    this->pin_ = std::move(pin);
    this->pinnedMessageChanged.invoke();
}

const std::optional<DggPoll> &DggChannel::poll() const
{
    return this->poll_;
}

void DggChannel::startPoll(DggPoll poll)
{
    this->poll_ = std::move(poll);
    this->pollChanged.invoke();
}

void DggChannel::addPollVotes(int option, qint64 quantity)
{
    if (!this->poll_ || this->poll_->ended || quantity <= 0)
    {
        return;
    }

    auto &poll = *this->poll_;
    // A hidden poll's votes arrive without the option they went to
    if (!poll.hidden && option >= 1 &&
        static_cast<size_t>(option) <= poll.totals.size())
    {
        poll.totals[static_cast<size_t>(option) - 1] += quantity;
    }
    poll.votesCast += quantity;
    this->pollChanged.invoke();
}

void DggChannel::markPollVote(int option)
{
    if (!this->poll_ || this->poll_->ended)
    {
        return;
    }

    this->poll_->canVote = false;
    this->poll_->myVote = option;
    this->pollChanged.invoke();
}

bool DggChannel::endPoll(const std::optional<DggPollEnd> &result)
{
    if (!this->poll_ || this->poll_->ended)
    {
        return false;
    }

    auto &poll = *this->poll_;
    poll.ended = true;
    if (result && result->totals.size() == poll.totals.size())
    {
        // The final numbers; they also reveal a hidden poll
        poll.totals = result->totals;
        poll.votesCast = result->votesCast;
        poll.hidden = false;
    }
    this->pollChanged.invoke();
    return true;
}

}  // namespace chatterino
