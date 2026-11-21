#include "providers/dgg/DggChatServer.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/dgg/DggAccount.hpp"
#include "providers/dgg/DggAccountManager.hpp"
#include "providers/dgg/DggAssets.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggCombo.hpp"
#include "providers/dgg/DggFrame.hpp"
#include "providers/dgg/DggMentions.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "providers/dgg/DggMessageBuilder.hpp"
#include "providers/dgg/DggWebSocket.hpp"
#include "singletons/Settings.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <array>
#include <tuple>

namespace chatterino {

using namespace Qt::Literals;

namespace {

/// `withKey`: send the logged-in account's login key as the `authtoken`
/// cookie, the way the chat socket takes it. The URL only holds the nick, so
/// the key never reaches a log; the `Cookie` header isn't logged either.
void fetchUserInfo(
    const QString &nick, bool withKey,
    std::function<void(const std::optional<DggProfile> &, int status)> done)
{
    auto request = NetworkRequest(dggUserInfoUrl(nick));
    if (withKey)
    {
        const auto account = getApp()->getAccounts()->dgg.current();
        if (account && !account->loginKey().isEmpty())
        {
            request = std::move(request).header(
                "Cookie", ("authtoken=" + account->loginKey()).toUtf8());
        }
        else
        {
            withKey = false;
        }
    }

    std::move(request)
        .onSuccess([done](NetworkResult result) {
            const auto profile = parseDggUserInfo(result.parseJson());
            done(profile, result.status().value_or(0));
        })
        .onError([nick, withKey, done](const NetworkResult &result) {
            const auto status = result.status().value_or(0);
            if (withKey && (status == 401 || status == 403))
            {
                // Whether the site accepts the key on this endpoint was never
                // tested (it refuses it on the whisper ones): ask again
                // without it, the way a guest does.
                fetchUserInfo(nick, false, done);
                return;
            }
            done(std::nullopt, status);
        })
        .execute();
}

/// Asks the site for the mentions at `url`. Like fetchUserInfo(): when
/// `withKey` the login key goes along as the `authtoken` cookie, and an
/// answer of 401/403 is asked for again without it. `done` gets the parsed
/// answer (nullopt if the site said no or sent junk), and `failed` is true if
/// no usable HTTP answer arrived at all.
void fetchMentionsFromSite(
    const QUrl &url, bool withKey,
    std::function<void(const std::optional<std::vector<DggMention>> &,
                       bool failed)>
        done)
{
    auto request = NetworkRequest(url);
    if (withKey)
    {
        const auto account = getApp()->getAccounts()->dgg.current();
        if (account && !account->loginKey().isEmpty())
        {
            request = std::move(request).header(
                "Cookie", ("authtoken=" + account->loginKey()).toUtf8());
        }
        else
        {
            withKey = false;
        }
    }

    std::move(request)
        .onSuccess([done](NetworkResult result) {
            done(parseDggMentions(result.parseJson()), false);
        })
        .onError([url, withKey, done](const NetworkResult &result) {
            const auto status = result.status().value_or(0);
            if (withKey && (status == 401 || status == 403))
            {
                fetchMentionsFromSite(url, false, done);
                return;
            }
            done(std::nullopt, true);
        })
        .execute();
}

}  // namespace

void dggFetchUserInfo(
    const QString &nick,
    std::function<void(const std::optional<DggProfile> &, int status)> done)
{
    fetchUserInfo(nick, getApp()->getAccounts()->dgg.isLoggedIn(),
                  std::move(done));
}

namespace {

/// The stylesheets the page links. The sheet of icons is named by the CSS
/// (`background-image`), not by the HTML, so it is looked for there.
std::vector<QUrl> stylesheetUrls(const QByteArray &page, const QUrl &pageUrl)
{
    static const QRegularExpression re(
        QStringLiteral(
            R"re((?:href|src)\s*=\s*["']([^"']+?\.css(?:\?[^"']*)?)["'])re"),
        QRegularExpression::CaseInsensitiveOption);

    auto text = QString::fromUtf8(page);
    text.replace(u"\\/"_s, u"/"_s);

    std::vector<QUrl> urls;
    auto matches = re.globalMatch(text);
    while (matches.hasNext() && urls.size() < 6)
    {
        const auto url = pageUrl.resolved(QUrl(matches.next().captured(1)));
        if (url.isValid() &&
            std::find(urls.begin(), urls.end(), url) == urls.end())
        {
            urls.push_back(url);
        }
    }
    return urls;
}

/// `emote-greyout`: the splatter behind a completed x10+ combo is red when the
/// combo ends and grey a few seconds later (chat-gui fades it between 2.6 s and
/// 3.5 s). A message can't change by itself, so the line is rebuilt once, with
/// the splatter grey. That is a single rebuild of a line that is moving up the
/// chat as new messages arrive; the fade in between is left out on purpose, as
/// every rebuild of the line is a chance for it to flash.
void fadeComboSplat(std::weak_ptr<DggChannel> weak, DggComboLine line,
                    MessagePtr shown)
{
    static constexpr int GREY_AFTER_MS = 3000;
    QTimer::singleShot(
        GREY_AFTER_MS, qApp,
        [weak = std::move(weak), line = std::move(line),
         shown = std::move(shown)]() mutable {
            auto chan = weak.lock();
            if (!chan)
            {
                return;
            }
            line.splatGrey = 100;
            const MessagePtr replacement =
                DggMessageBuilder::makeComboMessage(chan.get(), line);
            chan->replaceMessage(shown, replacement);
        });
}

/// Reads the stylesheets one after the other until the icon sheet is known,
/// then calls `done`. A stylesheet that fails to load is skipped.
void learnFromStylesheets(std::vector<QUrl> urls, size_t index,
                          std::function<void()> done)
{
    if (index >= urls.size() || !dggSiteImage(u"icons"_s).isEmpty())
    {
        done();
        return;
    }
    const auto url = urls[index];
    NetworkRequest(url)
        .onSuccess([urls, index, done, url](NetworkResult result) {
            dggLearnSiteImages(result.getData(), url);
            learnFromStylesheets(urls, index + 1, done);
        })
        .onError([urls, index, done](const NetworkResult &) {
            learnFromStylesheets(urls, index + 1, done);
        })
        .execute();
}

}  // namespace

void dggLoadSiteImages(std::function<void()> done)
{
    enum class State { Idle, Loading, Done };
    static State state = State::Idle;
    static std::vector<std::function<void()>> waiting;

    if (state == State::Done)
    {
        if (done)
        {
            done();
        }
        return;
    }
    if (done)
    {
        waiting.push_back(std::move(done));
    }
    if (state == State::Loading)
    {
        return;
    }
    state = State::Loading;

    const auto finish = [](State next) {
        state = next;
        auto callbacks = std::move(waiting);
        waiting.clear();
        for (const auto &callback : callbacks)
        {
            callback();
        }
    };

    const auto pageUrl = dggChatPageUrl();
    NetworkRequest(pageUrl)
        .onSuccess([pageUrl, finish](NetworkResult result) {
            const QByteArray page = result.getData();
            const auto count = dggLearnSiteImages(page, pageUrl);
            learnFromStylesheets(
                stylesheetUrls(page, pageUrl), 0, [count, finish] {
                    if (dggSiteImage(u"icons"_s).isEmpty())
                    {
                        qCWarning(chatterinoDgg)
                            << "The sheet of icons wasn't found in the chat "
                               "page or its stylesheets";
                    }
                    finish(count > 0 ? State::Done : State::Idle);
                });
        })
        .onError([finish](const NetworkResult &) {
            finish(State::Idle);
        })
        .execute();
}

DggChatServer::DggChatServer()
    : assets_(std::make_unique<DggAssets>())
{
    this->signalHolder_.managedConnect(this->assets_->ready, [this] {
        this->flushPendingFrames();
    });
}

void DggChatServer::flushPendingFrames()
{
    if (!this->assets_->isReady() || !this->siteImagesReady_)
    {
        return;
    }
    // Everything held back while the manifests and the site images loaded,
    // in arrival order
    auto frames = std::move(this->pendingFrames_);
    this->pendingFrames_.clear();
    for (const auto &frame : frames)
    {
        this->handleFrame(frame);
    }
}

DggChatServer::~DggChatServer() = default;

void DggChatServer::initialize()
{
    // The websocket is created when the channel is first opened.
}

std::shared_ptr<Channel> DggChatServer::getOrCreate()
{
    assertInGuiThread();

    auto chan = this->channel_.lock();
    if (!chan)
    {
        chan = std::make_shared<DggChannel>();
        this->channel_ = chan;
        this->combo_.reset();

        this->signalHolder_.managedConnect(chan->aboutToClose, [this] {
            if (this->socket_)
            {
                this->socket_->stop();
            }
        });
    }

    this->ensureConnected(chan);
    return chan;
}

std::shared_ptr<DggChannel> DggChatServer::channel() const
{
    return this->channel_.lock();
}

void DggChatServer::ensureConnected(const std::shared_ptr<DggChannel> &chan)
{
    if (!this->socket_)
    {
        this->socket_ = std::make_unique<DggWebSocket>();
        this->socket_->onOpened = [this] {
            this->onSocketOpened();
        };
        this->socket_->onClosed = [this](std::chrono::milliseconds retryIn) {
            this->onSocketClosed(retryIn);
        };
        this->socket_->onFrameReceived = [this](const DggFrame &frame) {
            this->onFrame(frame);
        };
    }

    if (!this->accountSignalConnected_)
    {
        this->accountSignalConnected_ = true;
        this->signalHolder_.managedConnect(
            getApp()->getAccounts()->dgg.currentUserChanged, [this] {
                this->onAccountChanged();
            });
    }

    if (!this->socket_->isStarted())
    {
        chan->addSystemMessage(u"Connecting to destiny.gg chat..."_s);
        this->assets_->load();
        // The icon of a death line comes from the site, so lines are held
        // back until it is known (or the site couldn't be read)
        this->siteImagesReady_ = false;
        dggLoadSiteImages([this] {
            this->siteImagesReady_ = true;
            this->flushPendingFrames();
        });
        // The login key of the active account, or none for a guest
        this->socket_->setAuthToken(
            getApp()->getAccounts()->dgg.current()->loginKey());
        this->socket_->start();
    }
}

void DggChatServer::onSocketOpened()
{
    auto chan = this->channel_.lock();
    if (!chan)
    {
        return;
    }

    // The server sends its backlog right after the handshake, so this lands
    // before the history. The user and connection counts follow with NAMES.
    this->endCombo(*chan);
    // The server sends the current pin again after the handshake. Clearing
    // the old one first means a pin that was removed while the connection was
    // down doesn't stay on screen.
    chan->setPinnedMessage(std::nullopt);
    this->authenticated_ = false;
    // The line saying who we are comes with `ME`, which the server sends
    // right after the handshake.
}

void DggChatServer::onSocketClosed(std::chrono::milliseconds retryIn)
{
    auto chan = this->channel_.lock();
    if (!chan)
    {
        return;
    }

    this->authenticated_ = false;
    this->endCombo(*chan);
    // Like chat-gui: a poll is ended on disconnect, so it isn't shown
    // forever. A poll that is still running is sent again after reconnecting.
    chan->endPoll(std::nullopt);
    if (retryIn.count() > 0)
    {
        chan->addSystemMessage(
            u"Disconnected from destiny.gg chat. Reconnecting in %1 seconds..."_s
                .arg(static_cast<double>(retryIn.count()) / 1000.0, 0, 'f',
                     1));
    }
    else
    {
        chan->addSystemMessage(u"Disconnected from destiny.gg chat."_s);
    }
}

void DggChatServer::onMe(DggChannel &chan, const QJsonObject &payload)
{
    // A guest gets `ME` without a user. A logged-in user's `ME` is their user
    // object: `nick` is the name as they spell it.
    const auto nick = payload.value("nick"_L1).toString();
    this->authenticated_ = !nick.isEmpty();

    this->endCombo(chan);
    if (this->authenticated_)
    {
        chan.addSystemMessage(
            u"Connected to destiny.gg chat as %1."_s.arg(nick));
        return;
    }

    if (!getApp()->getAccounts()->dgg.current()->isAnonymous())
    {
        // Said once per connection, and the connection isn't retried: a key
        // that was refused stays refused.
        chan.addSystemMessage(
            u"destiny.gg did not accept the saved login key, so you are "
            u"connected as Guest. Add a new key under Settings > Accounts > "
            u"Add new account > Dgg."_s);
        return;
    }

    chan.addSystemMessage(u"Connected to destiny.gg chat as Guest."_s);
}

void DggChatServer::onAccountChanged()
{
    assertInGuiThread();

    auto chan = this->channel_.lock();
    if (!chan || !this->socket_ || !this->socket_->isStarted())
    {
        // Nothing is connected: the next connection reads the account.
        return;
    }

    const auto account = getApp()->getAccounts()->dgg.current();

    // stop() doesn't report a close, so what a close would have cleaned up
    // is done here
    this->socket_->stop();
    this->authenticated_ = false;
    this->pendingFrames_.clear();
    this->endCombo(*chan);
    chan->endPoll(std::nullopt);

    if (account->isAnonymous())
    {
        chan->addSystemMessage(u"Logged out. Reconnecting as Guest..."_s);
    }
    else
    {
        chan->addSystemMessage(
            u"Reconnecting as %1..."_s.arg(account->username()));
    }

    this->socket_->setAuthToken(account->loginKey());
    this->socket_->start();
}

void DggChatServer::sendChatMessage(DggChannel &chan, const QString &rawText)
{
    assertInGuiThread();

    const auto text = rawText.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    const auto say = [&](const char *code) {
        chan.addSystemMessage(
            dggErrorText(QJsonObject{{"description"_L1, QLatin1String(code)}}));
    };

    if (!getApp()->getAccounts()->dgg.isLoggedIn())
    {
        say("needlogin");
        return;
    }

    // While a poll runs, typing an option's number votes instead of chatting
    // (chat-gui does the same)
    static const QRegularExpression pollNumber(QStringLiteral(R"(^[0-9]+$)"));
    if (const auto &poll = chan.poll();
        poll && !poll->ended && pollNumber.match(text).hasMatch())
    {
        const auto number = text.toLongLong();
        if (number >= 1 && number <= poll->options.size())
        {
            this->castPollVote(chan, static_cast<int>(number));
            return;
        }
    }

    // chat-gui's names for a whisper: /w, /whisper, /msg, /message, /tell,
    // /t and /notify. Caught before the check below so that `/w someone text`
    // can never be said in public chat.
    static const QRegularExpression whisperCommand(
        QStringLiteral(
            R"(^/(?:w|whisper|msg|message|tell|t|notify)(?:\s+(\S+))?(?:\s+(.*))?$)"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    if (const auto whisper = whisperCommand.match(text); whisper.hasMatch())
    {
        this->sendWhisper(chan, whisper.captured(1), whisper.captured(2));
        return;
    }

    // `/mentions [nick] [limit]` and its short form `/m`: answered from the
    // site's API, never said in chat.
    static const QRegularExpression mentionsCommand(
        QStringLiteral(R"(^/(?:mentions|m)(?:\s+(.*))?$)"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    if (const auto mentions = mentionsCommand.match(text);
        mentions.hasMatch())
    {
        this->fetchMentions(chan, mentions.captured(1));
        return;
    }

    // Like chat-gui, a slash command is never sent as a message (`/me` is the
    // only one the server sees).
    static const QRegularExpression slashCommand(
        QStringLiteral(R"(^/([a-z0-9]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto command = slashCommand.match(text);
    if (command.hasMatch() &&
        command.captured(1).compare(u"me"_s, Qt::CaseInsensitive) != 0)
    {
        chan.addSystemMessage(
            u"Chatterino can only send messages and /me here for now. "
            u"\"/%1\" was not sent."_s.arg(command.captured(1)));
        return;
    }

    // The server counts the same way chat-gui does; this only saves a round
    // trip, the server still has the last word.
    if (text.toUcs4().size() > 512)
    {
        say("msgtoolong");
        return;
    }

    if (!this->socket_ ||
        !this->socket_->send("MSG", QJsonObject{{"data"_L1, text}}))
    {
        say("notconnected");
        return;
    }
}

void DggChatServer::sendWhisper(DggChannel &chan, const QString &rawNick,
                                const QString &rawText)
{
    assertInGuiThread();

    const auto say = [&](const char *code) {
        chan.addSystemMessage(
            dggErrorText(QJsonObject{{"description"_L1, QLatin1String(code)}}));
    };

    const auto account = getApp()->getAccounts()->dgg.current();
    if (!account || !getApp()->getAccounts()->dgg.isLoggedIn())
    {
        say("needlogin");
        return;
    }

    // chat-gui's nickregex
    static const QRegularExpression nickRegex(QStringLiteral(R"(^\w{3,20}$)"));
    const auto nick = rawNick.trimmed();
    if (!nickRegex.match(nick).hasMatch())
    {
        chan.addSystemMessage(u"Invalid nick - /msg nick message"_s);
        return;
    }
    if (nick.compare(account->username(), Qt::CaseInsensitive) == 0)
    {
        chan.addSystemMessage(u"Cannot send a message to yourself"_s);
        return;
    }

    const auto text = rawText.trimmed();
    if (text.isEmpty())
    {
        say("msgempty");
        return;
    }
    if (text.toUcs4().size() > 512)
    {
        say("msgtoolong");
        return;
    }

    if (!this->socket_ || !this->socket_->send("PRIVMSG",
                                               QJsonObject{
                                                   {"nick"_L1, nick},
                                                   {"data"_L1, text},
                                               }))
    {
        say("notconnected");
        return;
    }

    // Show it from the local text (see the header). If the server refuses it,
    // its error line follows.
    DggChatMessage line;
    if (const auto me = chan.findUser(account->username()))
    {
        line.user = *me;
    }
    line.user.nick = account->username();
    line.rawText = text;
    line.text = text;
    line.timestampMs = QDateTime::currentMSecsSinceEpoch();
    line.hash = dggMessageHash(line.timestampMs, 0, u"pm-sent|"_s + text);
    line.isWhisper = true;
    line.whisperTarget = chan.canonicalNick(nick).value_or(nick);

    this->endCombo(chan);
    chan.addMessage(
        DggMessageBuilder::makeChatMessage(&chan, line, false, *this->assets_),
        MessageContext::Original);
}

void DggChatServer::fetchMentions(DggChannel &chan, const QString &args)
{
    assertInGuiThread();

    const auto account = getApp()->getAccounts()->dgg.current();
    const QString ownNick =
        (account && getApp()->getAccounts()->dgg.isLoggedIn())
            ? account->username()
            : QString{};

    const auto parsed = parseDggMentionsArgs(args, ownNick);
    if (!parsed.error.isEmpty())
    {
        chan.addSystemMessage(parsed.error);
        return;
    }
    if (this->mentionsBusy_)
    {
        chan.addSystemMessage(
            u"Still busy getting %1's mentions"_s.arg(parsed.nick));
        return;
    }

    this->mentionsBusy_ = true;
    chan.addSystemMessage(u"Getting mentions for %1 ..."_s.arg(parsed.nick));

    const auto nick = parsed.nick;
    const QPointer<DggChatServer> self(this);
    const std::weak_ptr<DggChannel> weakChan = this->channel_;

    fetchMentionsFromSite(
        dggMentionsUrl(nick, parsed.limit),
        getApp()->getAccounts()->dgg.isLoggedIn(),
        [self, weakChan, nick](
            const std::optional<std::vector<DggMention>> &mentions,
            bool failed) {
            if (!self)
            {
                return;
            }
            self->mentionsBusy_ = false;

            const auto chan = weakChan.lock();
            if (!chan || !self->assets_)
            {
                // The split was closed while the answer was on its way.
                return;
            }

            if (failed || !mentions)
            {
                chan->addSystemMessage(
                    u"No mentions for %1 received. Try again later."_s.arg(
                        nick));
                return;
            }
            if (mentions->empty())
            {
                chan->addSystemMessage(u"No mentions for %1."_s.arg(nick));
                return;
            }

            // Whatever was running as a combo is over now
            self->endCombo(*chan);

            // chat-gui dates the header with the last (newest) entry
            const auto last =
                QDateTime::fromMSecsSinceEpoch(mentions->back().timestampMs)
                    .toLocalTime();
            chan->addSystemMessage(u"Mentions for %1 last seen %2."_s.arg(
                nick, dggMentionsDateText(last)));

            for (const auto &mention : *mentions)
            {
                DggChatMessage line;
                if (const auto known = chan->findUser(mention.nick))
                {
                    line.user = *known;
                }
                line.user.nick =
                    chan->canonicalNick(mention.nick).value_or(mention.nick);
                line.rawText = mention.text;
                line.isAction =
                    mention.text.startsWith(u"/me "_s, Qt::CaseInsensitive);
                line.text = line.isAction ? mention.text.mid(4) : mention.text;
                line.greentext = line.text.startsWith(u'>') &&
                                 (line.user.isSubscriber() ||
                                  line.user.isPrivileged());
                line.timestampMs = mention.timestampMs;
                // Not the hash of the original line: this one must not be
                // taken for it (a spotlight finds its line by that id).
                line.hash = dggMessageHash(line.timestampMs, 0,
                                           u"mentions|"_s + mention.text);

                // `backlog`: shown as history (dimmed like the replayed
                // lines, no sound or alert, not written to the log file).
                chan->addMessage(DggMessageBuilder::makeChatMessage(
                                     chan.get(), line, true, *self->assets_),
                                 MessageContext::Original);
            }
        });
}

void DggChatServer::castPollVote(DggChannel &chan, int option)
{
    assertInGuiThread();

    const auto &poll = chan.poll();
    if (!poll || poll->ended || option < 1 || option > poll->options.size())
    {
        return;
    }

    const auto say = [&](const char *code) {
        chan.addSystemMessage(
            dggErrorText(QJsonObject{{"description"_L1, QLatin1String(code)}}));
    };

    if (!getApp()->getAccounts()->dgg.isLoggedIn())
    {
        say("needlogin");
        return;
    }
    if (!poll->canVote)
    {
        say("alreadyvoted");
        return;
    }

    // the server wants the number as a string, like chat-gui sends it
    if (!this->socket_ ||
        !this->socket_->send(
            "CASTVOTE", QJsonObject{{"vote"_L1, QString::number(option)}}))
    {
        say("notconnected");
    }
}

void DggChatServer::onFrame(const DggFrame &frame)
{
    if (!this->assets_->isReady() || !this->siteImagesReady_)
    {
        this->pendingFrames_.push_back(frame);
        return;
    }
    this->handleFrame(frame);
}

void DggChatServer::handleFrame(const DggFrame &frame)
{
    auto chan = this->channel_.lock();
    if (!chan)
    {
        return;
    }

    const auto obj = frame.payload.toObject();
    const auto nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto &name = frame.name;

    if (name == "MSG"_L1)
    {
        auto msg = parseDggChatMessage(obj, nowMs);
        if (!msg)
        {
            return;
        }
        if (!chan->markSeen(msg->hash))
        {
            // Already shown, e.g. in the backlog of a reconnect
            return;
        }

        chan->noteUser(msg->user);

        // The same emote again and again becomes one counting line
        const auto textOnly = dggStripSlashCommand(msg->rawText);
        if (this->continueCombo(*chan, *msg, textOnly, frame.backlog))
        {
            return;
        }
        // Anything else completes the combo that was running
        this->endCombo(*chan);

        HighlightAlert alert;
        const MessagePtr line = DggMessageBuilder::makeChatMessage(
            chan.get(), *msg, frame.backlog, *this->assets_, &alert);
        MessageBuilder::triggerHighlights(chan.get(), alert);
        chan->addMessage(line, MessageContext::Original);

        ComboState state;
        state.text = textOnly;
        state.shown = line;
        state.firstTime = msg->time();
        state.firstId = msg->hash;
        state.backlog = frame.backlog;
        this->combo_ = std::move(state);
    }
    else if (name == "MUTE"_L1 || name == "UNMUTE"_L1 || name == "BAN"_L1 ||
             name == "UNBAN"_L1 || name == "SUBONLY"_L1)
    {
        const auto text = name == "SUBONLY"_L1 ? dggSubOnlyText(obj)
                                               : dggModerationText(name, obj);
        if (text.isEmpty())
        {
            return;
        }

        const auto timestampMs = dggTimestampMs(obj, nowMs);
        const auto key = u"%1|%2|%3|%4"_s.arg(
            name, QString::number(timestampMs),
            obj.value("data"_L1).toString(), obj.value("nick"_L1).toString());
        if (!chan->markSeen(key))
        {
            return;
        }

        // A muted or banned user's earlier messages are greyed out, like a
        // timeout on Twitch. Only once per event: the key above is what keeps
        // a replayed backlog from doing it twice.
        if (name == "MUTE"_L1 || name == "BAN"_L1)
        {
            chan->disableMessagesFrom(obj.value("data"_L1).toString());
        }

        this->endCombo(*chan);
        const auto time = QDateTime::fromMSecsSinceEpoch(timestampMs);
        if (name == "SUBONLY"_L1)
        {
            chan->addMessage(DggMessageBuilder::makeSystemLine(
                                 chan.get(), time, text, frame.backlog, true),
                             MessageContext::Original);
        }
        else
        {
            // the target and the moderator are clickable names
            const auto moderator = DggUser::fromJson(obj);
            chan->addMessage(
                DggMessageBuilder::makeModerationLine(
                    chan.get(), time, text, obj.value("data"_L1).toString(),
                    moderator.isSystem() ? QString() : moderator.nick,
                    frame.backlog),
                MessageContext::Original);
        }
    }
    else if (name == "DEATH"_L1)
    {
        // The dead user is the event's own user (`data` is the cause), and
        // they're silenced for `duration` seconds like with a mute.
        const auto death = parseDggDeath(obj, nowMs);
        if (!death)
        {
            return;
        }

        const auto key = u"DEATH|%1|%2|%3"_s.arg(
            QString::number(death->timestampMs), death->user.nick,
            death->cause);
        if (!chan->markSeen(key))
        {
            return;
        }

        // Grey out their earlier messages, like for a mute
        chan->disableMessagesFrom(death->user.nick);

        this->endCombo(*chan);
        chan->addMessage(DggMessageBuilder::makeDeathMessage(
                             chan.get(), *death, frame.backlog, *this->assets_),
                         MessageContext::Original);
    }
    else if (name == "BROADCAST"_L1)
    {
        const auto text = obj.value("data"_L1).toString();
        if (text.isEmpty())
        {
            return;
        }

        const auto timestampMs = dggTimestampMs(obj, nowMs);
        const auto uuid = obj.value("uuid"_L1).toString();
        const auto key =
            u"BROADCAST|%1|%2"_s.arg(uuid.isEmpty() ? text : uuid,
                                      QString::number(timestampMs));
        if (!chan->markSeen(key))
        {
            return;
        }

        this->endCombo(*chan);
        auto [message, alert] = DggMessageBuilder::makeBroadcastMessage(
            chan.get(), QDateTime::fromMSecsSinceEpoch(timestampMs),
            DggUser::fromJson(obj.value("user"_L1).toObject()), text,
            frame.backlog, *this->assets_);
        if (!frame.backlog)
        {
            MessageBuilder::triggerHighlights(chan.get(), alert);
        }
        chan->addMessage(message, MessageContext::Original);
    }
    else if (name == "XPOST"_L1)
    {
        const auto post = parseDggXPost(obj, nowMs);
        if (!post)
        {
            return;
        }

        const auto id = post->uuid.isEmpty()
                            ? u"%1|%2"_s.arg(QString::number(post->timestampMs),
                                             post->url)
                            : post->uuid;
        if (!chan->markSeen(u"XPOST|%1"_s.arg(id)))
        {
            return;
        }

        this->endCombo(*chan);
        auto [message, alert] =
            DggMessageBuilder::makeXPostMessage(chan.get(), *post, frame.backlog);
        if (!frame.backlog)
        {
            MessageBuilder::triggerHighlights(chan.get(), alert);
        }
        chan->addMessage(message, MessageContext::Original);
    }
    else if (isDggVestaboardName(name))
    {
        const auto board = parseDggVestaboard(name, obj, nowMs);
        if (!board)
        {
            return;
        }

        const auto id =
            board->uuid.isEmpty()
                ? u"%1|%2|%3"_s.arg(QString::number(board->timestampMs),
                                    board->submitter,
                                    QString::number(board->totalCents))
                : board->uuid;
        if (!chan->markSeen(u"VESTABOARD|%1|%2"_s.arg(name, id)))
        {
            return;
        }

        this->endCombo(*chan);
        auto [message, alert] = DggMessageBuilder::makeVestaboardMessage(
            chan.get(), *board, frame.backlog, *this->assets_);
        if (!frame.backlog)
        {
            MessageBuilder::triggerHighlights(chan.get(), alert);
        }
        chan->addMessage(message, MessageContext::Original);
    }
    else if (isDggEventName(name))
    {
        // Subscriptions, gifts, donations and spotlights. The site also
        // replays them as `PAIDEVENTS` / `SPOTLIGHTS` for its event bar,
        // which are not handled here (they'd only repeat lines).
        const auto event = parseDggEvent(name, obj, nowMs);
        if (!event)
        {
            return;
        }

        const auto id =
            event->uuid.isEmpty()
                ? u"%1|%2|%3"_s.arg(QString::number(event->timestampMs),
                                     event->user.nick, event->text)
                : event->uuid;
        if (!chan->markSeen(u"EVENT|%1|%2"_s.arg(name, id)))
        {
            return;
        }

        this->endCombo(*chan);

        // A spotlight shows the message it quotes, so while that message is
        // still in chat the spotlight takes its place (and its time) instead
        // of the text being there twice. The line is found by its id, the
        // hash the spotlight carries as `key`. Only a line by the same author
        // counts, so a combo line, which has the id of the message it started
        // with, is never replaced. When the message is gone (older than what
        // chat holds, sent before we connected) the spotlight is a new line.
        MessagePtr original;
        auto shown = *event;
        if (event->kind == DggEvent::Kind::Spotlight &&
            !event->messageKey.isEmpty())
        {
            original = chan->findMessageByID(event->messageKey);
            if (original &&
                original->loginName != event->user.nick.toLower())
            {
                original.reset();
            }
            if (original)
            {
                shown.timestampMs = original->timestamp.toMSecsSinceEpoch();
            }
        }

        auto [message, alert] = DggMessageBuilder::makeEventMessage(
            chan.get(), shown, frame.backlog, *this->assets_);
        if (event->kind == DggEvent::Kind::Spotlight)
        {
            // so that spotlighting the message again replaces this line
            message->id = event->messageKey;
        }
        if (!frame.backlog)
        {
            // sound and window alert of the event's highlight, not for
            // lines that are only being replayed
            MessageBuilder::triggerHighlights(chan.get(), alert);
        }
        if (original)
        {
            chan->replaceMessage(original, message);
        }
        else
        {
            chan->addMessage(message, MessageContext::Original);
        }
    }
    else if (name == "PIN"_L1)
    {
        const auto pin = parseDggPin(obj, nowMs);
        const auto &current = chan->pinnedMessage();
        if (pin.isRemoval())
        {
            if (!current)
            {
                return;
            }
            chan->setPinnedMessage(std::nullopt);
            if (!frame.backlog)
            {
                this->endCombo(*chan);
                chan->addMessage(
                    DggMessageBuilder::makeSystemLine(
                        chan.get(), QDateTime::currentDateTime(),
                        pin.user.nick.isEmpty()
                            ? u"The pinned message was removed."_s
                            : u"%1 removed the pinned message."_s.arg(
                                  pin.user.nick),
                        false, false),
                    MessageContext::Original);
            }
            return;
        }

        if (current && !pin.uuid.isEmpty() && current->uuid == pin.uuid)
        {
            // The same pin again
            return;
        }
        chan->setPinnedMessage(pin);

        // A line in the chat for a pin that was just made. The one the server
        // sends after connecting (pinned an hour ago) only fills the banner.
        constexpr qint64 FRESH_PIN_MS = 5 * 60 * 1000;
        const auto age = nowMs - pin.timestampMs;
        const auto key = u"PIN|%1"_s.arg(
            pin.uuid.isEmpty() ? pin.text + QString::number(pin.timestampMs)
                               : pin.uuid);
        if (!frame.backlog && age < FRESH_PIN_MS && chan->markSeen(key))
        {
            this->endCombo(*chan);
            chan->addMessage(
                DggMessageBuilder::makePinMessage(chan.get(), pin,
                                                  frame.backlog,
                                                  *this->assets_),
                MessageContext::Original);
        }
    }
    else if (name == "POLLSTART"_L1)
    {
        // The poll is only shown in the banner under the header (see
        // DggPollBanner): it has no line in chat, neither at its start nor
        // at its end. The server sends a running poll again after a
        // reconnect; that just refreshes the numbers.
        auto poll = parseDggPollStart(obj, nowMs);
        if (!poll)
        {
            return;
        }
        chan->startPoll(*poll);
    }
    else if (name == "POLLSTOP"_L1)
    {
        chan->endPoll(parseDggPollStop(obj));
    }
    else if (name == "VOTECAST"_L1)
    {
        const auto vote = parseDggPollVote(obj);
        chan->addPollVotes(vote.option, vote.quantity);
    }
    else if (name == "VOTECOUNTED"_L1)
    {
        chan->markPollVote(parseDggPollVote(obj).option);
    }
    else if (name == "NAMES"_L1)
    {
        // The full list of who is connected (~1500 users): it replaces what
        // the viewer list knew.
        std::vector<DggUser> users;
        for (const auto &entry : obj.value("users"_L1).toArray())
        {
            users.push_back(DggUser::fromJson(entry.toObject()));
        }
        chan->setOnlineUsers(users);
        this->endCombo(*chan);
        chan->addSystemMessage(
            u"Serving %1 connections and %2 users."_s
                .arg(obj.value("connectioncount"_L1).toInt())
                .arg(obj.value("usercount"_L1).toInt()));
    }
    else if (name == "ME"_L1)
    {
        this->onMe(*chan, obj);
    }
    else if (name == "PRIVMSG"_L1)
    {
        // A whisper to the user. It is private: it only shows in this split
        // and isn't logged (see DggMessageBuilder::makeChatMessage).
        auto msg = parseDggWhisper(obj, nowMs);
        if (!msg || !chan->markSeen(msg->hash))
        {
            return;
        }
        chan->noteUser(msg->user);
        this->endCombo(*chan);
        HighlightAlert alert;
        const MessagePtr line = DggMessageBuilder::makeChatMessage(
            chan.get(), *msg, frame.backlog, *this->assets_, &alert);
        MessageBuilder::triggerHighlights(chan.get(), alert);
        chan->addMessage(line, MessageContext::Original);
    }
    else if (name == "PRIVMSGSENT"_L1)
    {
        // Has no payload; sendWhisper() already showed the line
    }
    else if (name == "ERR"_L1)
    {
        this->endCombo(*chan);
        chan->addSystemMessage(dggErrorText(obj));
    }
    else if (name == "REFRESHCHATASSETS"_L1)
    {
        // New emotes or flairs were published. Lines already shown keep the
        // old images.
        this->assets_->load();
    }
    else if (name == "UPDATEUSER"_L1)
    {
        // new flairs and the like: the viewer list moves the user
        chan->userUpdated(DggUser::fromJson(obj));
    }
    else if (name == "JOIN"_L1 || name == "QUIT"_L1 || name == "USERSDELTA"_L1)
    {
        // With ~1,500 users these would flood the split, so they aren't shown
        // as lines. They keep the viewer list current, and their names are
        // remembered so they can be clicked in messages.
        if (name == "JOIN"_L1)
        {
            chan->userJoined(DggUser::fromJson(obj));
        }
        else if (name == "QUIT"_L1)
        {
            chan->userQuit(DggUser::fromJson(obj));
        }
        else
        {
            // `users` joined, `removed` left. chat-gui finds the removed by
            // their id, so the entries are user objects; a bare nick is
            // accepted too.
            std::vector<DggUser> joined;
            for (const auto &entry : obj.value("users"_L1).toArray())
            {
                joined.push_back(DggUser::fromJson(entry.toObject()));
            }
            std::vector<DggUser> quit;
            for (const auto &entry : obj.value("removed"_L1).toArray())
            {
                if (entry.isString())
                {
                    DggUser user;
                    user.nick = entry.toString();
                    quit.push_back(std::move(user));
                }
                else
                {
                    quit.push_back(DggUser::fromJson(entry.toObject()));
                }
            }
            chan->updateOnlineUsers(joined, quit);
        }
    }
    else
    {
        qCDebug(chatterinoDgg) << "Unhandled event" << name;
    }
}

DggComboLine DggChatServer::comboLine(ComboState &state, bool complete) const
{
    DggComboLine line;
    line.emoteName = state.text;
    line.count = state.count;
    line.complete = complete;
    line.time = state.firstTime;
    line.id = state.firstId;
    line.backlog = state.backlog;

    // chat-gui sets `data-combo` and `data-combo-group` from the third line
    // on (the line that joins two messages has none) and when the combo ends
    const int attributeCount = (state.count >= 3 || complete) ? state.count : 0;
    const QString look =
        attributeCount > 0 ? dggComboGroup(attributeCount) : QString();
    if (!state.emote || state.emoteLook != look)
    {
        state.emote = this->assets_->comboEmoteFor(state.text, attributeCount);
        state.emoteLook = look;
    }
    line.emote = state.emote;
    return line;
}

bool DggChatServer::continueCombo(DggChannel &chan, const DggChatMessage &msg,
                                  const QString &textOnly, bool backlog)
{
    // chat-gui: the author must be allowed the emote, and the last message
    // must have said the same
    if (!this->combo_ || textOnly.isEmpty() || this->combo_->text != textOnly ||
        !this->assets_->isEmoteFor(textOnly, msg.user))
    {
        return false;
    }

    auto &state = *this->combo_;
    if (chan.getLastMessage() != state.shown)
    {
        // Something else was added after that line, so it is not the last
        // message anymore
        return false;
    }

    state.count++;
    state.backlog = backlog;
    const MessagePtr line = DggMessageBuilder::makeComboMessage(
        &chan, this->comboLine(state, false));
    chan.replaceMessage(state.shown, line);
    state.shown = line;
    state.isCombo = true;
    return true;
}

void DggChatServer::endCombo(DggChannel &chan)
{
    if (!this->combo_)
    {
        return;
    }
    auto state = std::move(*this->combo_);
    this->combo_.reset();

    if (!state.isCombo)
    {
        return;
    }
    auto comboLine = this->comboLine(state, true);
    // a combo from the history is over for a long time: its splatter is grey
    comboLine.splatGrey = state.backlog ? 100 : 0;
    const MessagePtr line =
        DggMessageBuilder::makeComboMessage(&chan, comboLine);
    chan.replaceMessage(state.shown, line);

    if (!state.backlog && dggComboStep(state.count) >= 10)
    {
        fadeComboSplat(this->channel_, std::move(comboLine), line);
    }
}

const DggAssets *DggChatServer::assets() const
{
    return this->assets_.get();
}

}  // namespace chatterino
