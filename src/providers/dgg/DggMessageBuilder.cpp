#include "providers/dgg/DggMessageBuilder.hpp"

#include "Application.hpp"
#include "common/LinkParser.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "controllers/highlights/HighlightController.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "messages/Image.hpp"
#include "messages/MessageElement.hpp"
#include "messages/SpriteSheet.hpp"
#include "providers/dgg/DggAccount.hpp"
#include "providers/dgg/DggAccountManager.hpp"
#include "providers/dgg/DggFlairs.hpp"
#include "providers/dgg/DggAssets.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggCombo.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "providers/emoji/Emojis.hpp"
#include "providers/twitch/TwitchBadge.hpp"
#include "singletons/Settings.hpp"
#include "util/Variant.hpp"

#include <QFont>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace {

using namespace chatterino;
using namespace Qt::StringLiterals;

/// chat-gui's `$color-green-text`.
const QColor GREENTEXT_COLOR{0x6c, 0xa5, 0x28};

/// The warning sign in front of a death line: `.msg-death .user::before` is a
/// 16x16 box showing the sheet at `background-position: -32px 0`.
ImagePtr deathIcon()
{
    // the sheet's URL (`img/icons.<hash>.png`) is read from the site; until
    // then there is no icon
    const auto sheet = dggSiteImage(QStringLiteral("icons"));
    if (sheet.isEmpty())
    {
        return nullptr;
    }
    SpriteSheetSpec spec;
    spec.frameSize = QSize(16, 16);
    spec.offsets = {QPoint(32, 0)};
    spec.durationsMs = {100};
    return Image::fromSpriteSheet(Url{sheet.toString()}, spec);
}

/// The blood splatter behind a completed x10+ combo (`.chat-combo.combo-
/// complete.x10 { background: url(img/emote-splat.png) }`), as it is.
ImagePtr comboSplatRed()
{
    const auto sheet = dggSiteImage(QStringLiteral("emote-splat"));
    if (sheet.isEmpty())
    {
        return nullptr;
    }
    return Image::fromUrl(Url{sheet.toString()});
}

/// The same splatter at `grayscale(100%)`, where `emote-greyout` ends. The
/// fade from red to grey is not made of images in between: the red splatter is
/// drawn over this one with less and less opacity (see makeComboMessage()),
/// so the two images never change and are loaded long before the fade.
ImagePtr comboSplatGrey()
{
    const auto sheet = dggSiteImage(QStringLiteral("emote-splat"));
    if (sheet.isEmpty())
    {
        return nullptr;
    }
    // the image is 178x178 px
    SpriteSheetSpec spec;
    spec.frameSize = QSize(178, 178);
    spec.offsets = {QPoint(0, 0)};
    spec.durationsMs = {100};
    SpriteColorFilter grey;
    grey.kind = SpriteColorFilter::Kind::Grayscale;
    grey.amount = 1.0;
    spec.filters = {grey};
    return Image::fromSpriteSheet(Url{sheet.toString()}, spec);
}

/// A transparent, one pixel high box `width` pixels wide: chat-gui spaces the
/// parts of a combo line with margins and letter spacing that Chatterino has
/// no equivalent for.
ImagePtr comboSpacer(int width)
{
    // `Image::fromResourcePixmap` keys on the pixmap's address, so the pixmaps
    // have to outlive every message
    static std::map<int, QPixmap> pixmaps;
    auto it = pixmaps.find(width);
    if (it == pixmaps.end())
    {
        QPixmap pixmap(width, 1);
        pixmap.fill(Qt::transparent);
        it = pixmaps.emplace(width, std::move(pixmap)).first;
    }
    return Image::fromResourcePixmap(it->second);
}

/// Adds one word of user text: an emote if the word is one the author may
/// use, otherwise a link or plain text.
/// `@nick`, or a plain word that is the nick of someone seen in chat (DGG
/// users mention each other without the `@`): the length of the name part
/// of `word` (with the `@`) and the nick as it is spelled in chat.
///
/// Punctuation after the name stays out of it (`nick,` `nick:` `nick?`).
struct NickWord {
    qsizetype length = 0;
    QString nick;
};

std::optional<NickWord> asNickWord(QStringView word, const DggChannel *channel)
{
    const bool at = word.startsWith(u'@');
    auto name = at ? word.sliced(1) : word;
    while (!name.isEmpty() && QStringView(u".?!,:;'\")").contains(name.back()))
    {
        name.chop(1);
    }
    if (name.size() < 3 || name.size() > 20 ||
        !std::all_of(name.begin(), name.end(), [](QChar c) {
            return c.isLetterOrNumber() || c == u'_';
        }))
    {
        return std::nullopt;
    }
    const qsizetype length = name.size() + (at ? 1 : 0);
    if (channel != nullptr)
    {
        if (auto nick = channel->canonicalNick(name))
        {
            // The viewer list holds ~1500 nicks, many of them ordinary words
            // ("bro", "chat"). Without the `@`, only the exact spelling of the
            // nick counts, so a lower-case "bro" in a sentence stays text.
            if (at || name == QStringView(*nick))
            {
                return NickWord{length, *nick};
            }
        }
    }
    if (at)
    {
        // somebody who didn't speak yet (or left before the backlog)
        return NickWord{length, name.toString()};
    }
    return std::nullopt;
}

/// A word that is not a DGG emote and has no emoji in it: a clickable name, a
/// link or plain text. A name is drawn in the colour of that user's own nick
/// (their flair colour, or the scrolling rainbow), if the user is known.
void appendPlainWord(DggMessageBuilder &builder, QStringView word,
                     bool greentext, const DggChannel *channel,
                     const DggAssets &assets)
{
    const MessageColor textColor =
        greentext ? MessageColor(GREENTEXT_COLOR) : MessageColor::Text;

    // A name that can be clicked: it opens the user card
    if (!linkparser::parse(word))
    {
        if (const auto nickWord = asNickWord(word, channel))
        {
            const QString text = word.left(nickWord->length).toString();
            const QString trailing = word.mid(nickWord->length).toString();
            // A MentionElement, not a plain TextElement with a link:
            // MessageBuilder::appendOrEmplaceText() merges the following
            // plain words into a trailing TextElement, which would make the
            // whole rest of the sentence part of the clickable name. It
            // never merges into a MentionElement, and its login name gives
            // the same UserInfo link.
            //
            // The mention gets the colour the nick has in chat. It is shown
            // while "Color @usernames" is on (the same setting Twitch
            // mentions follow); with it off the mention keeps the text colour.
            MessageColor userColor = textColor;
            MessageElementFlags flags{MessageElementFlag::Text,
                                      MessageElementFlag::Mention};
            if (channel != nullptr)
            {
                if (const auto user = channel->findUser(nickWord->nick))
                {
                    const auto style = assets.nickStyleFor(*user);
                    if (style.color)
                    {
                        userColor = MessageColor(*style.color);
                    }
                    if (style.rainbow)
                    {
                        // drawn like the rainbow nick of a chat line
                        // (TextLayoutElement::paintAnimated)
                        flags.set(MessageElementFlag::DggRainbowUsername);
                    }
                }
            }
            auto *element = builder.emplace<MentionElement>(
                text, nickWord->nick, textColor, userColor, flags);
            if (!trailing.isEmpty())
            {
                // the punctuation sits right behind the name
                element->setTrailingSpace(false);
                builder.emplace<TextElement>(trailing, MessageElementFlag::Text,
                                             textColor);
            }
            return;
        }
    }

    if (!greentext)
    {
        builder.addWordFromUserMessage(word);
        return;
    }

    // addWordFromUserMessage() always uses the default text colour, so
    // greentext words are emitted here. Links keep their link colour.
    if (auto link = linkparser::parse(word))
    {
        builder.addLink(*link, word);
        return;
    }
    builder.emplace<TextElement>(word.toString(), MessageElementFlag::Text,
                                 textColor);
}

void appendWord(DggMessageBuilder &builder, QStringView word,
                const DggUser &user, const DggEmoteContext &emoteContext,
                bool greentext, const DggAssets &assets,
                const DggChannel *channel)
{
    const MessageColor textColor =
        greentext ? MessageColor(GREENTEXT_COLOR) : MessageColor::Text;

    if (auto emote = assets.emoteFor(word, user, emoteContext))
    {
        // textColor is only used if the image can't be shown
        builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote,
                                      textColor);
        return;
    }

    // Emoji become images of the emoji set the user picked, like they do for
    // Twitch and Kick. A link is left whole: emoji inside a URL are part of it.
    if (linkparser::parse(word))
    {
        appendPlainWord(builder, word, greentext, channel, assets);
        return;
    }
    for (const auto &item : getApp()->getEmotes()->getEmojis()->parse(word))
    {
        std::visit(variant::Overloaded{
                       [&](const EmotePtr &emoji) {
                           builder.emplace<EmoteElement>(
                               emoji, MessageElementFlag::EmojiAll, textColor);
                       },
                       [&](QStringView text) {
                           appendPlainWord(builder, text, greentext, channel,
                                           assets);
                       },
                   },
                   item);
    }
}

/// The elements chat-gui would put directly inside the message's text, in
/// order: the emote's name for an emote, `DGG_CHAT_USER_ELEMENT` for a name
/// (`@mention` or a nick), an empty string for anything else that is an
/// element (a link). Plain words are text nodes and don't count. The emote CSS
/// looks at these to tell which emotes sit next to each other.
struct TextElements {
    QStringList names;
    /// For every word, the index into `names` of the element it became, or -1
    /// for a plain word.
    std::vector<int> indexOfWord;
};

/// `\w` of chat-gui's regular expressions (they have no `u` flag): ASCII only.
bool isSiteWordChar(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'z') ||
           (u >= u'A' && u <= u'Z') || u == u'_';
}

/// How many `<span class="chat-user">` chat-gui makes out of one word of the
/// text (almost always 0 or 1). Emote rules look at these (`.chat-user +
/// .emote.NoTears`), so this follows the site and not Chatterino's own idea
/// of a clickable name:
///
/// - `@nick` (3 to 20 word characters) is one whether or not `nick` is in
///   chat (`MentionedUserFormatter`'s first expression);
/// - a bare word is one if it is the nick of somebody in chat, in any
///   spelling (`chat.users.has(nick.toLowerCase())`);
/// - both only at the start of the word or right behind a `>`, and only if
///   the name ends the word or a `.`, `?`, `!`, `,` or `'` follows it.
///   Anything else (`nick:`, `nick)`, `nick-nick`) is plain text.
int countChatUserSpans(QStringView word, const DggChannel *channel)
{
    int spans = 0;
    const qsizetype size = word.size();
    qsizetype i = 0;
    while (i < size)
    {
        if (i != 0 && word[i - 1] != u'>')
        {
            i++;
            continue;
        }

        const bool at = word[i] == u'@';
        const qsizetype start = at ? i + 1 : i;
        qsizetype end = start;
        while (end < size && isSiteWordChar(word[end]))
        {
            end++;
        }
        const qsizetype length = end - start;
        const bool endsName =
            end == size || QStringView(u".?!,'").contains(word[end]);

        if (length >= 3 && length <= 20 && endsName &&
            (at || (channel != nullptr &&
                    channel->canonicalNick(word.mid(start, length)))))
        {
            spans++;
            i = end;
            continue;
        }
        i++;
    }
    return spans;
}

TextElements collectElements(const QStringList &words, const DggUser &user,
                             const DggAssets &assets, const DggChannel *channel)
{
    TextElements out;
    for (const auto &word : words)
    {
        if (assets.isEmoteFor(word, user))
        {
            out.indexOfWord.push_back(static_cast<int>(out.names.size()));
            out.names.append(word);
        }
        else if (linkparser::parse(word))
        {
            out.indexOfWord.push_back(static_cast<int>(out.names.size()));
            out.names.append(QString());
        }
        else if (const int spans = countChatUserSpans(word, channel); spans > 0)
        {
            // `@nick` and the nick of somebody in chat are a
            // `<span class="chat-user">` on the site, which emote rules look
            // at (NoTears doesn't throw its banana next to one)
            out.indexOfWord.push_back(static_cast<int>(out.names.size()));
            for (int k = 0; k < spans; k++)
            {
                out.names.append(DGG_CHAT_USER_ELEMENT);
            }
        }
        else
        {
            out.indexOfWord.push_back(-1);
        }
    }
    return out;
}

QString displayedNick(const QString &nick);

/// Where the elements of an event's header sit. With a message that came with
/// the event the header is its own line above that message, and it is only
/// shown while "Show subscription header" is on, like the header of Twitch's
/// sub notices. Without one the header is the whole line: a system message.
struct EventHeader {
    bool hasContent = false;
};

/// One piece of an event's header, in the plain (non-bold) style of Twitch's
/// system lines. `base` are the flags the piece has on its own.
TextElement *emplaceHeaderText(DggMessageBuilder &builder, const QString &text,
                               MessageElementFlags base, MessageColor color,
                               bool rainbow, EventHeader header)
{
    MessageElementFlags flags = base;
    bool exhaustive = header.hasContent;
    if (header.hasContent)
    {
        flags.set(MessageElementFlag::SubscriptionHeader);
    }
    if (rainbow)
    {
        // The layout only looks for the rainbow flag on the element, and the
        // view never has it, so such an element can't need all of its flags:
        // with a message it sits on the header flag alone.
        flags =
            header.hasContent
                ? MessageElementFlags{MessageElementFlag::SubscriptionHeader,
                                      MessageElementFlag::DggRainbowUsername}
                : base | MessageElementFlag::DggRainbowUsername;
        exhaustive = false;
    }

    auto *element =
        builder.emplace<TextElement>(text, flags, color, FontStyle::ChatMedium);
    element->exhaustiveFlags = exhaustive;
    return element;
}

/// A name in an event line, shown the way Chatterino shows a mentioned user
/// (a MentionElement, like the names in Twitch's and Kick's event lines): in
/// the user's flair colour while "Color @usernames" is on, bold with "Bold
/// @usernames", and a click opens the user card. Returns the text that was
/// shown.
QString appendEventName(DggMessageBuilder &builder, const DggUser &user,
                        const DggAssets &assets, EventHeader header,
                        bool trailingSpace = true)
{
    if (user.nick.isEmpty())
    {
        const QString anonymous = QStringLiteral("Anonymous");
        emplaceHeaderText(builder, anonymous, MessageElementFlag::Text,
                          MessageColor::System, false, header)
            ->setTrailingSpace(trailingSpace);
        return anonymous;
    }

    const auto style = assets.nickStyleFor(user);
    const QString nick = displayedNick(user.nick);

    // the same flags emplaceHeaderText() gives its pieces
    MessageElementFlags flags{MessageElementFlag::Text,
                              MessageElementFlag::Mention};
    bool exhaustive = header.hasContent;
    if (header.hasContent)
    {
        flags.set(MessageElementFlag::SubscriptionHeader);
    }
    if (style.rainbow)
    {
        // see emplaceHeaderText(): the rainbow flag is only on the element
        flags =
            header.hasContent
                ? MessageElementFlags{MessageElementFlag::SubscriptionHeader,
                                      MessageElementFlag::DggRainbowUsername}
                : flags | MessageElementFlag::DggRainbowUsername;
        exhaustive = false;
    }

    const MessageColor userColor =
        style.color ? MessageColor(*style.color) : MessageColor::System;
    auto *element = builder.emplace<MentionElement>(
        nick, user.nick, MessageColor::System, userColor, flags);
    element->exhaustiveFlags = exhaustive;
    element->setTrailingSpace(trailingSpace);
    return nick;
}

/// The label of a tier, in the colour of the tier's flair.
QString appendEventTier(DggMessageBuilder &builder, const DggEvent &event,
                        const DggAssets &assets, EventHeader header)
{
    DggUser tierUser;
    if (const auto flair = dggTierFlairName(event.tier); !flair.isEmpty())
    {
        tierUser.features = QStringList{flair};
    }
    const auto style = assets.nickStyleFor(tierUser);
    const QString label =
        event.tierLabel.isEmpty() ? QStringLiteral("Tier") : event.tierLabel;
    emplaceHeaderText(
        builder, label, MessageElementFlag::Text,
        style.color ? MessageColor(*style.color) : MessageColor::System,
        style.rainbow, header);
    return label;
}

/// Plain event wording ("is now a", "gifted", ...), in the system colour.
QString appendEventWords(DggMessageBuilder &builder, const QString &words,
                         EventHeader header)
{
    emplaceHeaderText(builder, words, MessageElementFlag::Text,
                      MessageColor::System, false, header);
    return words;
}

/// The message that came with an event: emotes, emoji, links and names like
/// in a chat line. Returns the text that was shown.
QString appendEventMessageText(DggMessageBuilder &builder, const QString &text,
                               const DggUser &user, const DggAssets &assets,
                               const DggChannel *channel, bool backlog)
{
    auto emoteContext = assets.emoteContextFor(user, false, backlog);
    const QStringList words =
        text.simplified().split(QChar(u' '), Qt::SkipEmptyParts);
    const auto elements = collectElements(words, user, assets, channel);
    emoteContext.elements = elements.names;

    QString shown;
    for (qsizetype i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];
        if (!shown.isEmpty())
        {
            shown.append(u' ');
        }
        shown.append(word);

        emoteContext.elementIndex =
            elements.indexOfWord[static_cast<size_t>(i)];
        appendWord(builder, word, user, emoteContext, false, assets, channel);
    }
    return shown;
}

/// The line of the message that came with an event, as chat shows it: time,
/// flairs, nick and the text. Returns the text that was shown.
QString appendEventBody(DggMessageBuilder &builder, const DggEvent &event,
                        const DggAssets &assets, const DggChannel *channel,
                        bool backlog)
{
    builder.emplace<TimestampElement>(event.time().time());

    for (const auto &badge : assets.badgesFor(event.user))
    {
        builder.emplace<BadgeElement>(badge, MessageElementFlag::BadgeVanity);
    }

    if (!event.user.nick.isEmpty())
    {
        const auto style = assets.nickStyleFor(event.user);
        if (style.color)
        {
            builder->usernameColor = *style.color;
        }
        auto *nick = builder.emplace<TextElement>(
            displayedNick(event.user.nick) + QChar(u':'),
            MessageElementFlag::Username,
            style.color ? MessageColor(*style.color) : MessageColor::Text,
            FontStyle::ChatMediumBold);
        if (style.rainbow)
        {
            nick->addFlags(MessageElementFlag::DggRainbowUsername);
        }
        nick->setLink(Link(Link::UserInfo, event.user.nick));
    }

    return appendEventMessageText(builder, event.text, event.user, assets,
                                  channel, backlog);
}

/// A name that may be in the viewer list: its flair is used if it is.
DggUser userNamed(const DggChannel *channel, const QString &nick)
{
    if (channel != nullptr)
    {
        if (const auto user = channel->findUser(nick))
        {
            return *user;
        }
    }
    DggUser user;
    user.nick = nick;
    return user;
}

/// A clickable piece of text that opens `url`, in the colour of links.
TextElement *emplaceLinkText(DggMessageBuilder &builder, const QString &text,
                             const QString &url)
{
    auto *element = builder.emplace<TextElement>(text, MessageElementFlag::Text,
                                                 MessageColor::Link);
    element->setLink(Link(Link::Url, url));
    return element;
}

QString dollars(qint64 cents)
{
    return QLocale(QLocale::English, QLocale::UnitedStates)
        .toCurrencyString(static_cast<double>(cents) / 100.0,
                          QStringLiteral("$"));
}

/// The board of a Vestaboard event as a picture, like chat-gui's digital
/// board: a dark bezel, 6x22 tiles, letters and the spot colours.
QPixmap vestaboardPixmap(const std::vector<std::vector<int>> &rows)
{
    // spot colours, like `_vestaboard.scss`
    static const std::map<int, QColor> COLORS{
        {63, QColor(QRgb(0xe5484d))}, {64, QColor(QRgb(0xf2851f))},
        {65, QColor(QRgb(0xf5c518))}, {66, QColor(QRgb(0x1fbf75))},
        {67, QColor(QRgb(0x0090ff))}, {68, QColor(QRgb(0x6e6ade))},
        {69, QColor(QRgb(0xedeef0))}, {70, QColor(QRgb(0x0a0b0d))},
        {71, QColor(QRgb(0x777b84))},
    };
    static const QString PUNCTUATION = [] {
        // codes 37 to 60; the gaps are tiles the board doesn't print
        QString chars(24, QChar(u' '));
        const std::map<int, QChar> known{
            {37, u'!'}, {38, u'@'}, {39, u'#'},  {40, u'$'}, {41, u'('},
            {42, u')'}, {44, u'-'}, {46, u'+'},  {47, u'&'}, {48, u'='},
            {49, u';'}, {50, u':'}, {52, u'\''}, {53, u'"'}, {54, u'%'},
            {55, u','}, {56, u'.'}, {59, u'/'},  {60, u'?'},
        };
        for (const auto &[code, ch] : known)
        {
            chars[code - 37] = ch;
        }
        return chars;
    }();

    constexpr int TILE_W = 11;
    constexpr int TILE_H = 14;
    constexpr int GAP = 1;
    constexpr int SCALE = 2;  // drawn at twice the size, shown at half

    int columns = 0;
    for (const auto &row : rows)
    {
        columns = std::max(columns, static_cast<int>(row.size()));
    }
    const int rowCount = static_cast<int>(rows.size());
    if (columns == 0 || rowCount == 0)
    {
        return {};
    }

    const int width = GAP + columns * (TILE_W + GAP);
    const int height = GAP + rowCount * (TILE_H + GAP);
    QPixmap pixmap(width * SCALE, height * SCALE);
    pixmap.fill(QColor(QRgb(0x0a0b0d)));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(SCALE, SCALE);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(10);
    painter.setFont(font);

    for (int r = 0; r < rowCount; r++)
    {
        for (int c = 0; c < static_cast<int>(rows[r].size()); c++)
        {
            const int code = rows[r][c];
            const QRectF tile(GAP + c * (TILE_W + GAP),
                              GAP + r * (TILE_H + GAP), TILE_W, TILE_H);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QRgb(0x1a1b1f)));
            painter.drawRoundedRect(tile, 1.5, 1.5);

            if (const auto color = COLORS.find(code); color != COLORS.end())
            {
                painter.setBrush(color->second);
                painter.drawRoundedRect(tile.adjusted(1, 1, -1, -1), 1.5, 1.5);
                continue;
            }

            QChar ch;
            if (code >= 1 && code <= 26)
            {
                ch = QChar(u'A' + (code - 1));
            }
            else if (code >= 27 && code <= 35)
            {
                ch = QChar(u'1' + (code - 27));
            }
            else if (code == 36)
            {
                ch = u'0';
            }
            else if (code >= 37 && code <= 60)
            {
                ch = PUNCTUATION[code - 37];
            }
            if (ch.isNull() || ch == u' ')
            {
                continue;
            }
            painter.setPen(QColor(QRgb(0xedeef0)));
            painter.drawText(tile, Qt::AlignCenter, QString(ch));
        }
    }
    painter.end();
    return pixmap;
}

/// How big a board is shown, as a factor of the picture. The picture is drawn
/// at twice the size (see SCALE in vestaboardPixmap) so it stays sharp, which
/// makes 0.5 the size of chat-gui's digital board: 265x91 px at 100% scale.
/// Make this bigger for a bigger board in chat (0.75 is about 400 px wide).
constexpr qreal VESTABOARD_DISPLAY_SCALE = 0.5;

/// The picture of a board as an image, nullptr for an empty board.
///
/// `Image::fromResourcePixmap` finds images by the *address* of the pixmap, so
/// every board needs a pixmap of its own that outlives all messages (a local
/// one would share its address with the next board, which would then show
/// the previous board's picture). The same board again reuses its pixmap and
/// image. Boards are small and rare (a few per hour), so they are kept.
ImagePtr vestaboardImage(const std::vector<std::vector<int>> &rows)
{
    static std::map<std::vector<std::vector<int>>, QPixmap> pixmaps;
    auto it = pixmaps.find(rows);
    if (it == pixmaps.end())
    {
        auto pixmap = vestaboardPixmap(rows);
        if (pixmap.isNull())
        {
            return nullptr;
        }
        it = pixmaps.emplace(rows, std::move(pixmap)).first;
    }
    return Image::fromResourcePixmap(it->second, VESTABOARD_DISPLAY_SCALE);
}

/// Runs the user's highlights over an event, like Twitch does for its sub
/// notices: the line gets the highlight colour, and the alert says what to
/// play or flash. Nothing is triggered here.
HighlightAlert processHighlights(DggMessageBuilder &builder,
                                 const MessageParseArgs &args,
                                 bool self = false)
{
    if (getSettings()->isBlacklistedUser(builder->loginName))
    {
        return {};
    }

    auto [highlighted, highlightResult] = getApp()->getHighlights()->check({
        .args = args,
        .twitchBadges = {},
        .senderName = builder->loginName,
        .originalMessage = builder->messageText,
        .messageFlags = builder->flags,
        .self = self,
        .runContext =
            filters::RunContext{
                .message = builder.message(),
                .channel = builder.channel(),
            },
        .platform = builder->platform,
    });

    if (!highlighted)
    {
        return {};
    }

    builder->flags.set(MessageFlag::Highlighted);
    builder->highlightColor = highlightResult.color;
    if (highlightResult.showInMentions)
    {
        builder->flags.set(MessageFlag::ShowInMentions);
    }

    return {
        .sound = highlightResult.sound,
        .windowAlert = highlightResult.alert,
    };
}

QString displayedNick(const QString &nick)
{
    if (auto nickname = getSettings()->matchNickname(nick))
    {
        return *nickname;
    }
    return nick;
}

}  // namespace

namespace chatterino {

using namespace Qt::StringLiterals;

DggMessageBuilder::DggMessageBuilder(SystemMessageTag /*tag*/,
                                     DggChannel *channel, const QDateTime &time)
    : channel_(channel)
{
    this->emplace<TimestampElement>(time.time());
    this->message().flags.set(MessageFlag::System);
    this->message().platform = MessagePlatform::Dgg;
    this->message().timestamp = time;
}

DggMessageBuilder::DggMessageBuilder(DggChannel *channel)
    : channel_(channel)
{
    this->message().platform = MessagePlatform::Dgg;
}

DggMessageBuilder::DggMessageBuilder(DggChannel *channel, const QDateTime &time)
    : channel_(channel)
{
    this->emplace<TimestampElement>(time.time());
    this->message().platform = MessagePlatform::Dgg;
    this->message().timestamp = time;
}

void DggMessageBuilder::markBacklog()
{
    this->message().flags.set(MessageFlag::RecentMessage,
                              MessageFlag::DoNotLog);
}

MessagePtrMut DggMessageBuilder::makeChatMessage(DggChannel *channel,
                                                 const DggChatMessage &msg,
                                                 bool backlog,
                                                 const DggAssets &assets,
                                                 HighlightAlert *alert)
{
    DggMessageBuilder builder(channel, msg.time());
    builder->channelName = channel->getName();
    builder->id = msg.hash;
    builder->displayName = msg.user.nick;
    builder->loginName = msg.user.nick.toLower();
    builder->userID = QString::number(msg.user.id);

    if (backlog)
    {
        builder.markBacklog();
    }
    if (msg.isAction)
    {
        builder->flags.set(MessageFlag::Action);
    }
    if (msg.isWhisper)
    {
        // Private: never write it to the channel's log file
        builder->flags.set(MessageFlag::Whisper, MessageFlag::DoNotLog);
    }

    // Flairs first, like chat-gui.
    for (const auto &badge : assets.badgesFor(msg.user))
    {
        builder.emplace<BadgeElement>(badge, MessageElementFlag::BadgeVanity);
    }

    const auto nickStyle = assets.nickStyleFor(msg.user);
    const auto &nickColor = nickStyle.color;
    if (nickColor)
    {
        builder->usernameColor = *nickColor;
    }

    const QString nick = displayedNick(msg.user.nick);
    QString usernameText = nick;
    if (!msg.isAction && !msg.isWhisper)
    {
        usernameText.append(u':');
    }
    auto *nickElement = builder.emplace<TextElement>(
        usernameText, MessageElementFlag::Username,
        nickColor ? MessageColor(*nickColor) : MessageColor::Text,
        FontStyle::ChatMediumBold);
    if (nickStyle.rainbow)
    {
        nickElement->addFlags(MessageElementFlag::DggRainbowUsername);
    }
    // a click opens the user card (ChannelView::showUserInfoPopup)
    nickElement->setLink(Link(Link::UserInfo, msg.user.nick));
    if (msg.isWhisper)
    {
        // Like Twitch whispers in Chatterino: `sender -> receiver:`, the
        // sender first (a received one is `nick -> you:`, one you sent is
        // `you -> target:`).
        builder.emplace<TextElement>(u"->"_s, MessageElementFlag::Username,
                                     MessageColor::System,
                                     FontStyle::ChatMedium);

        const bool received = msg.whisperTarget.isEmpty();
        QString otherNick;
        if (received)
        {
            const auto account = getApp()->getAccounts()->dgg.current();
            otherNick =
                account->isAnonymous() ? QString{} : account->username();
        }
        else
        {
            otherNick = msg.whisperTarget;
        }

        // the other one's name in their flair colour if it is known
        MessageColor otherColor = MessageColor::System;
        if (const auto other = channel->findUser(otherNick))
        {
            if (const auto color = assets.nickStyleFor(*other).color)
            {
                otherColor = MessageColor(*color);
            }
        }
        builder
            .emplace<TextElement>(displayedNick(otherNick) + u':',
                                  MessageElementFlag::Username, otherColor,
                                  FontStyle::ChatMediumBold)
            ->setLink(Link(Link::UserInfo, otherNick));
    }

    // Emotes look different depending on who says them and on what sits next
    // to them
    auto emoteContext = assets.emoteContextFor(msg.user, msg.isAction, backlog);
    emoteContext.greentext = msg.greentext;

    const QString simplified = msg.text.simplified();
    const QStringList words = simplified.split(QChar(u' '), Qt::SkipEmptyParts);
    const auto elements = collectElements(words, msg.user, assets, channel);
    emoteContext.elements = elements.names;

    QString messageText;
    for (qsizetype i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];
        if (!messageText.isEmpty())
        {
            messageText.append(u' ');
        }
        messageText.append(word);

        emoteContext.elementIndex =
            elements.indexOfWord[static_cast<size_t>(i)];
        appendWord(builder, word, msg.user, emoteContext, msg.greentext, assets,
                   channel);
    }

    builder->messageText = messageText;
    builder->searchText = builder->loginName + QChar(u' ') + nick +
                          QStringLiteral(": ") + messageText;

    // A whisper you sent is not checked; one you received runs the whisper
    // highlight (and the others, like Twitch does)
    const bool receivedWhisper = msg.isWhisper && msg.whisperTarget.isEmpty();
    if (!msg.isWhisper || receivedWhisper)
    {
        // Your own lines never highlight (they come back from the server
        // like everyone else's)
        const auto account = getApp()->getAccounts()->dgg.current();
        const bool self = !account->isAnonymous() &&
                          msg.user.nick.compare(account->username(),
                                                Qt::CaseInsensitive) == 0;

        MessageParseArgs args;
        args.isReceivedWhisper = receivedWhisper;
        const auto result = processHighlights(builder, args, self);
        if (alert != nullptr && !backlog)
        {
            // History is coloured but must not beep
            *alert = result;
        }
    }

    return builder.release();
}

MessagePtrMut DggMessageBuilder::makeComboMessage(DggChannel *channel,
                                                  const DggComboLine &combo)
{
    DggMessageBuilder builder(channel, combo.time);
    builder->channelName = channel->getName();
    builder->id = combo.id;
    if (combo.backlog)
    {
        builder.markBacklog();
    }

    // the combo's text is centred on the line against the emote
    const MessageElementFlags textFlags{MessageElementFlag::Text,
                                        MessageElementFlag::DggComboText};

    // the count, `X` and `C-C-C-COMBO`: what the splatter is drawn behind
    const MessageElementFlags bodyFlags{MessageElementFlag::Text,
                                        MessageElementFlag::DggComboText,
                                        MessageElementFlag::DggComboBody};

    // behind the body of the line, not the emote: it takes no room. Grey at the
    // bottom, red over it, fading out: `grayscale(g%)` is `(1 - g%)` red and
    // `g%` grey. While it is still red the grey one is hidden, but it is asked
    // for, so it is loaded by the time it shows.
    if (combo.complete && dggComboStep(combo.count) >= 10)
    {
        const MessageElementFlags splatFlags{MessageElementFlag::Text,
                                             MessageElementFlag::DggComboSplat};
        const int grey = std::clamp(combo.splatGrey, 0, 100);
        if (const auto greySplat = comboSplatGrey())
        {
            builder.emplace<ImageElement>(greySplat, splatFlags);
        }
        if (grey < 100)
        {
            if (const auto redSplat = comboSplatRed())
            {
                builder.emplace<ImageElement>(redSplat, splatFlags)
                    ->setOpacity(1.0 - grey / 100.0);
            }
        }
    }

    if (combo.emote)
    {
        builder.emplace<EmoteElement>(combo.emote, MessageElementFlag::Emote,
                                      MessageColor::Text);
    }
    else
    {
        builder.emplace<TextElement>(combo.emoteName, textFlags,
                                     MessageColor::Text);
    }
    // the website leaves about 4 px more between the emote and the count
    builder.emplace<ImageElement>(comboSpacer(4), MessageElementFlag::Text)
        ->setTrailingSpace(false);

    // chat-gui grows the number with the size of the combo, but Chatterino
    // has no bold large font: the count stays bold at every size
    const int step = dggComboStep(combo.count);
    const auto countFont = FontStyle::ChatMediumBold;
    // the count sits about 2 px closer to the X (`letter-spacing: -0.05em`)
    builder
        .emplace<TextElement>(QString::number(combo.count), bodyFlags,
                              MessageColor::Text, countFont)
        ->setTrailingSpace(false);
    builder.emplace<ImageElement>(comboSpacer(2), MessageElementFlag::Text)
        ->setTrailingSpace(false);
    builder.emplace<TextElement>(QStringLiteral("X"), bodyFlags,
                                 MessageColor::Text, FontStyle::ChatMediumBold);
    if (combo.complete)
    {
        // `.hit` is hidden and `.combo` shown once the combo is over
        builder.emplace<TextElement>(
            QStringLiteral("C-C-C-COMBO"), bodyFlags, MessageColor::Text,
            step >= 10 ? FontStyle::ChatMediumBold : FontStyle::ChatMedium);
    }
    else
    {
        builder.emplace<TextElement>(QStringLiteral("HITS"), bodyFlags,
                                     MessageColor::System,
                                     FontStyle::ChatMediumItalic);
    }

    const QString text =
        QStringLiteral("%1 x%2").arg(combo.emoteName).arg(combo.count);
    builder->messageText = text;
    builder->searchText = text;

    return builder.release();
}

MessagePtrMut DggMessageBuilder::makeSystemLine(DggChannel *channel,
                                                const QDateTime &time,
                                                const QString &text,
                                                bool backlog, bool moderation)
{
    DggMessageBuilder builder(systemMessage, channel, time);
    if (backlog)
    {
        builder.markBacklog();
    }
    if (moderation)
    {
        builder->flags.set(MessageFlag::ModerationAction);
    }

    builder.emplace<TextElement>(text, MessageElementFlag::Text,
                                 MessageColor::System);
    builder->messageText = text;
    builder->searchText = text;

    return builder.release();
}

MessagePtrMut DggMessageBuilder::makeReconnectLine(DggChannel *channel,
                                                   const QDateTime &time,
                                                   const QString &text,
                                                   const QString &prompt)
{
    DggMessageBuilder builder(systemMessage, channel, time);

    // Like a stock system line: a word that is a link stays a link
    for (const auto &word : text.split(u' ', Qt::SkipEmptyParts))
    {
        if (auto link = linkparser::parse(word))
        {
            builder.addLink(*link, word);
            continue;
        }
        builder.emplace<TextElement>(word, MessageElementFlag::Text,
                                     MessageColor::System);
    }

    builder
        .emplace<TextElement>(prompt, MessageElementFlag::Text,
                              MessageColor::Link)
        ->setLink(Link(Link::Reconnect, QString()));

    const auto full = text + u' ' + prompt;
    builder->messageText = full;
    builder->searchText = full;

    return builder.release();
}

MessagePtrMut DggMessageBuilder::makeModerationLine(
    DggChannel *channel, const QDateTime &time, const QString &text,
    const QString &target, const QString &moderator, bool backlog)
{
    DggMessageBuilder builder(systemMessage, channel, time);
    if (backlog)
    {
        builder.markBacklog();
    }
    builder->flags.set(MessageFlag::ModerationAction);

    // A name in the line: the same plain system text as the rest of it, but a
    // click opens the user card
    const auto emplaceName = [&](const QString &nick) {
        auto *element = builder.emplace<TextElement>(
            nick, MessageElementFlag::Text, MessageColor::System);
        element->setLink(Link(Link::UserInfo, nick));
        return element;
    };
    const auto emplaceWords = [&](const QString &words) {
        const auto trimmed = words.trimmed();
        if (!trimmed.isEmpty())
        {
            builder.emplace<TextElement>(trimmed, MessageElementFlag::Text,
                                         MessageColor::System);
        }
    };

    // The text is "<target> <words>[ by <moderator>][ <words>]", see
    // dggModerationText()
    const QString byMarker = QStringLiteral(" by ") + moderator;
    if (target.isEmpty() || !text.startsWith(target))
    {
        emplaceWords(text);
    }
    else
    {
        QString rest = text.mid(target.size());
        const auto byIndex = moderator.isEmpty() ? -1 : rest.indexOf(byMarker);
        emplaceName(target);
        if (byIndex < 0)
        {
            emplaceWords(rest);
        }
        else
        {
            emplaceWords(rest.left(byIndex) + QStringLiteral(" by"));
            rest = rest.mid(byIndex + byMarker.size());
            auto *mod = emplaceName(moderator);
            if (rest.startsWith(u' '))
            {
                emplaceWords(rest);
            }
            else if (!rest.isEmpty())
            {
                // punctuation sits right behind the name
                mod->setTrailingSpace(false);
                emplaceWords(rest);
            }
        }
    }

    builder->messageText = text;
    builder->searchText = text;

    return builder.release();
}

MessagePtrMut DggMessageBuilder::makeDeathMessage(DggChannel *channel,
                                                  const DggDeath &death,
                                                  bool backlog,
                                                  const DggAssets &assets)
{
    DggMessageBuilder builder(systemMessage, channel, death.time());
    if (backlog)
    {
        builder.markBacklog();
    }
    builder->flags.set(MessageFlag::ModerationAction);

    if (auto icon = deathIcon())
    {
        builder.emplace<ImageElement>(icon, MessageElementFlag::Text);
    }

    const auto &user = death.user;
    const auto nickStyle = assets.nickStyleFor(user);
    const auto &nickColor = nickStyle.color;
    const QString nick = displayedNick(user.nick);
    auto *nickElement = builder.emplace<TextElement>(
        nick, MessageElementFlag::Username,
        nickColor ? MessageColor(*nickColor) : MessageColor::Text,
        FontStyle::ChatMediumBold);
    if (nickStyle.rainbow)
    {
        nickElement->addFlags(MessageElementFlag::DggRainbowUsername);
    }

    // The cause is in italics and can contain emotes
    auto emoteContext = assets.emoteContextFor(user, false, backlog);
    const QStringList words =
        death.cause.simplified().split(QChar(u' '), Qt::SkipEmptyParts);
    const auto elements = collectElements(words, user, assets, channel);
    emoteContext.elements = elements.names;

    QString causeText;
    for (qsizetype i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];
        if (!causeText.isEmpty())
        {
            causeText.append(u' ');
        }
        causeText.append(word);

        emoteContext.elementIndex =
            elements.indexOfWord[static_cast<size_t>(i)];
        if (auto emote = assets.emoteFor(word, user, emoteContext))
        {
            builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote,
                                          MessageColor::Text);
        }
        else
        {
            for (const auto &item :
                 getApp()->getEmotes()->getEmojis()->parse(word))
            {
                std::visit(variant::Overloaded{
                               [&](const EmotePtr &emoji) {
                                   builder.emplace<EmoteElement>(
                                       emoji, MessageElementFlag::EmojiAll,
                                       MessageColor::Text);
                               },
                               [&](QStringView text) {
                                   builder.emplace<TextElement>(
                                       text.toString(),
                                       MessageElementFlag::Text,
                                       MessageColor::Text,
                                       FontStyle::ChatMediumItalic);
                               },
                           },
                           item);
            }
        }
    }

    const QString full =
        causeText.isEmpty() ? nick : nick + QChar(u' ') + causeText;
    builder->messageText = full;
    builder->searchText = full;

    return builder.release();
}

std::pair<MessagePtrMut, HighlightAlert>
    DggMessageBuilder::makeBroadcastMessage(DggChannel *channel,
                                            const QDateTime &time,
                                            const DggUser &user,
                                            const QString &text, bool backlog,
                                            const DggAssets &assets)
{
    // Built like a Twitch announcement: a "Broadcast" header on its own line
    // (shown while "Show announcement header" is on), then the message. The
    // header has to come before the message's own timestamp, so this does not
    // use the system message constructor, which adds that first.
    DggMessageBuilder builder(channel);
    builder->timestamp = time;
    builder->channelName = channel->getName();
    builder->flags.set(MessageFlag::System, MessageFlag::DggBroadcast);
    if (backlog)
    {
        builder.markBacklog();
    }

    builder
        .emplace<TimestampElement>(time.time(),
                                   MessageElementFlags{
                                       MessageElementFlag::HeaderTimestamp,
                                       MessageElementFlag::AnnouncementHeader,
                                   })
        ->exhaustiveFlags = true;
    builder
        .emplace<TextElement>("Broadcast",
                              MessageElementFlags{
                                  MessageElementFlag::Text,
                                  MessageElementFlag::AnnouncementHeader,
                              },
                              MessageColor::System, FontStyle::ChatMediumBold)
        ->exhaustiveFlags = true;
    builder
        .emplace<LinebreakElement>(MessageElementFlags{
            MessageElementFlag::AnnouncementHeader,
        })
        ->exhaustiveFlags = true;
    builder.emplace<TimestampElement>(time.time());

    const bool named = !user.isSystem() && !user.nick.isEmpty();
    QString prefix;
    if (named)
    {
        builder->loginName = user.nick.toLower();
        builder->displayName = user.nick;

        // The sender in their flair colour, a click opens the user card
        const auto style = assets.nickStyleFor(user);
        prefix = displayedNick(user.nick) + QChar(u':');
        emplaceHeaderText(
            builder, prefix, MessageElementFlag::Username,
            style.color ? MessageColor(*style.color) : MessageColor::System,
            style.rainbow, EventHeader{.hasContent = false})
            ->setLink(Link(Link::UserInfo, user.nick));
    }

    const QString shown =
        appendEventMessageText(builder, text, user, assets, channel, backlog);
    builder->messageText = shown;
    builder->searchText = named ? prefix + QChar(u' ') + shown : shown;

    const auto alert = processHighlights(builder, MessageParseArgs{});
    return {builder.release(), alert};
}

std::pair<MessagePtrMut, HighlightAlert> DggMessageBuilder::makeEventMessage(
    DggChannel *channel, const DggEvent &event, bool backlog,
    const DggAssets &assets)
{
    const QDateTime time = event.time();
    const bool hasContent = !event.text.isEmpty();
    const EventHeader header{.hasContent = hasContent};

    DggMessageBuilder builder(channel);
    builder->timestamp = time;
    builder->channelName = channel->getName();
    // Each kind has its own built-in highlight, which looks for its flag
    switch (event.kind)
    {
        case DggEvent::Kind::Subscription:
            builder->flags.set(MessageFlag::DggSubscription);
            break;
        case DggEvent::Kind::GiftSub:
        case DggEvent::Kind::MassGift:
            builder->flags.set(MessageFlag::DggGift);
            break;
        case DggEvent::Kind::Donation:
            builder->flags.set(MessageFlag::DggDonation);
            break;
        case DggEvent::Kind::Spotlight:
            builder->flags.set(MessageFlag::DggSpotlight);
            break;
    }
    if (backlog)
    {
        builder.markBacklog();
    }
    if (!event.user.nick.isEmpty())
    {
        builder->loginName = event.user.nick.toLower();
        builder->displayName = event.user.nick;
    }

    if (hasContent)
    {
        builder
            .emplace<TimestampElement>(
                time.time(),
                MessageElementFlags{MessageElementFlag::HeaderTimestamp,
                                    MessageElementFlag::SubscriptionHeader})
            ->exhaustiveFlags = true;
    }
    else
    {
        builder.emplace<TimestampElement>(time.time());
    }

    QStringList shown;
    shown << appendEventName(builder, event.user, assets, header);

    switch (event.kind)
    {
        case DggEvent::Kind::Subscription:
            shown << appendEventWords(builder, QStringLiteral("is now a"),
                                      header);
            shown << appendEventTier(builder, event, assets, header);
            shown << appendEventWords(builder, QStringLiteral("subscriber"),
                                      header);
            if (event.streak > 0)
            {
                shown << appendEventWords(
                    builder,
                    QStringLiteral("(%1 month streak)").arg(event.streak),
                    header);
            }
            break;
        case DggEvent::Kind::GiftSub:
            shown << appendEventWords(builder, QStringLiteral("gifted"),
                                      header);
            shown << appendEventName(builder, event.recipient, assets, header);
            shown << appendEventWords(builder, QStringLiteral("a"), header);
            shown << appendEventTier(builder, event, assets, header);
            shown << appendEventWords(builder, QStringLiteral("subscription"),
                                      header);
            break;
        case DggEvent::Kind::MassGift:
            shown << appendEventWords(builder, QStringLiteral("gifted"),
                                      header);
            shown << appendEventWords(builder, QString::number(event.quantity),
                                      header);
            shown << appendEventTier(builder, event, assets, header);
            shown << appendEventWords(
                builder,
                event.quantity > 1 ? QStringLiteral("subs to the community")
                                   : QStringLiteral("sub to the community"),
                header);
            break;
        case DggEvent::Kind::Donation:
            shown << appendEventWords(builder, QStringLiteral("donated"),
                                      header);
            shown << appendEventWords(
                builder,
                QLocale(QLocale::English, QLocale::UnitedStates)
                    .toCurrencyString(
                        static_cast<double>(event.amountCents) / 100.0,
                        QStringLiteral("$")),
                header);
            break;
        case DggEvent::Kind::Spotlight:
            shown << appendEventWords(builder, QStringLiteral("spotlighted by"),
                                      header);
            shown << appendEventWords(builder, event.spotlightedBy, header);
            break;
    }

    const QString headerText = shown.join(QChar(u' '));

    MessageParseArgs args;
    if (hasContent)
    {
        // The header is its own line above the message, like on Twitch
        builder
            .emplace<LinebreakElement>(MessageElementFlags{
                MessageElementFlag::SubscriptionHeader,
            })
            ->exhaustiveFlags = true;

        const QString text =
            appendEventBody(builder, event, assets, channel, backlog);
        builder->messageText = text;
        builder->searchText = headerText + QChar(u' ') + builder->loginName +
                              QStringLiteral(": ") + text;
        args.isSubscriptionMessage = true;
    }
    else
    {
        // Nothing the user typed: the whole line is a system message
        builder->flags.set(MessageFlag::System);
        builder->messageText = headerText;
        builder->searchText = headerText;
    }

    const auto alert = processHighlights(builder, args);
    return {builder.release(), alert};
}

std::pair<MessagePtrMut, HighlightAlert> DggMessageBuilder::makeXPostMessage(
    DggChannel *channel, const DggXPost &post, bool backlog)
{
    const QDateTime time = post.time();
    const bool hasContent = !post.text.isEmpty();
    const EventHeader header{.hasContent = hasContent};

    DggMessageBuilder builder(channel);
    builder->timestamp = time;
    builder->channelName = channel->getName();
    builder->flags.set(MessageFlag::DggXPost);
    if (backlog)
    {
        builder.markBacklog();
    }

    if (hasContent)
    {
        builder
            .emplace<TimestampElement>(
                time.time(),
                MessageElementFlags{MessageElementFlag::HeaderTimestamp,
                                    MessageElementFlag::SubscriptionHeader})
            ->exhaustiveFlags = true;
    }
    else
    {
        builder.emplace<TimestampElement>(time.time());
    }

    // The handle is not a chat user: it opens the profile on X
    const QString handle = QChar(u'@') + post.handle;
    emplaceHeaderText(builder, handle, MessageElementFlag::Text,
                      MessageColor::Link, false, header)
        ->setLink(Link(Link::Url, u"https://x.com/"_s + post.handle));
    appendEventWords(builder, QStringLiteral("posted on X"), header);
    const QString headerText = handle + QStringLiteral(" posted on X");

    MessageParseArgs args;
    if (hasContent)
    {
        builder
            .emplace<LinebreakElement>(MessageElementFlags{
                MessageElementFlag::SubscriptionHeader,
            })
            ->exhaustiveFlags = true;

        builder.emplace<TimestampElement>(time.time());
        const QStringList words =
            post.text.simplified().split(QChar(u' '), Qt::SkipEmptyParts);
        for (const auto &word : words)
        {
            builder.addWordFromUserMessage(word);
        }
        if (!post.url.isEmpty())
        {
            emplaceLinkText(builder, QStringLiteral("View Post on X"),
                            post.url);
        }

        builder->messageText = words.join(QChar(u' '));
        builder->searchText = headerText + QChar(u' ') + builder->messageText;
        args.isSubscriptionMessage = true;
    }
    else
    {
        if (!post.url.isEmpty())
        {
            emplaceLinkText(builder, QStringLiteral("View Post on X"),
                            post.url);
        }
        builder->flags.set(MessageFlag::System);
        builder->messageText = headerText;
        builder->searchText = headerText;
    }

    const auto alert = processHighlights(builder, args);
    return {builder.release(), alert};
}

MessagePtrMut DggMessageBuilder::makePinMessage(DggChannel *channel,
                                                const DggPin &pin, bool backlog,
                                                const DggAssets &assets)
{
    DggMessageBuilder builder(systemMessage, channel, pin.time());
    builder->channelName = channel->getName();
    if (backlog)
    {
        builder.markBacklog();
    }

    // The flairs the event carries, else what the viewer list knows
    const DggUser user = pin.user.features.isEmpty()
                             ? userNamed(channel, pin.user.nick)
                             : pin.user;
    if (!user.nick.isEmpty())
    {
        builder->loginName = user.nick.toLower();
        builder->displayName = user.nick;
    }

    const EventHeader header{.hasContent = false};
    QStringList shown;
    shown << appendEventName(builder, user, assets, header);
    shown << appendEventWords(builder, QStringLiteral("pinned a message:"),
                              header);
    const QString text = appendEventMessageText(builder, pin.text, user, assets,
                                                channel, backlog);

    builder->messageText = text;
    builder->searchText = shown.join(QChar(u' ')) + QChar(u' ') + text;
    return builder.release();
}

std::pair<MessagePtrMut, HighlightAlert>
    DggMessageBuilder::makeVestaboardMessage(DggChannel *channel,
                                             const DggVestaboard &board,
                                             bool backlog,
                                             const DggAssets &assets)
{
    const QDateTime time = board.time();
    const EventHeader header{.hasContent = true};
    const bool openHourly =
        board.kind == DggVestaboard::Kind::Hourly && board.submitter.isEmpty();

    DggMessageBuilder builder(channel);
    builder->timestamp = time;
    builder->channelName = channel->getName();
    builder->flags.set(MessageFlag::DggVestaboard);
    if (backlog)
    {
        builder.markBacklog();
    }
    if (!board.submitter.isEmpty())
    {
        builder->loginName = board.submitter.toLower();
        builder->displayName = board.submitter;
    }

    // No timestamp anywhere in this message: the site's card has none, and
    // the board is the content. (`timestamp` above is still the message's
    // time, for sorting and search.)

    // The header, worded like chat-gui
    const QString amount = dollars(board.totalCents);
    QStringList shown;
    if (openHourly)
    {
        shown << appendEventWords(
            builder, QStringLiteral("No designs on the Vestaboard yet"),
            header);
    }
    else if (board.kind == DggVestaboard::Kind::Lead && !board.isNewDesign)
    {
        // a contribution pushed an existing design to the lead
        shown << appendEventName(builder, userNamed(channel, board.submitter),
                                 assets, header);
        shown << appendEventWords(builder, QStringLiteral("funded"), header);
        shown << appendEventName(builder, userNamed(channel, board.artist),
                                 assets, header, false);
        shown << appendEventWords(
            builder, QStringLiteral("'s design into the lead · %1").arg(amount),
            header);
    }
    else
    {
        QString verb;
        switch (board.kind)
        {
            case DggVestaboard::Kind::Lead:
                verb = QStringLiteral("took the lead with a new design");
                break;
            case DggVestaboard::Kind::Hourly:
                verb = QStringLiteral("is leading the Vestaboard");
                break;
            case DggVestaboard::Kind::Reset:
                verb = QStringLiteral("won the Vestaboard auction");
                break;
        }
        shown << appendEventName(builder, userNamed(channel, board.submitter),
                                 assets, header);
        shown << appendEventWords(
            builder, QStringLiteral("%1 · %2").arg(verb, amount), header);
    }
    const QString headerText = shown.join(QChar(u' '));

    // The body: the board, the top contributors and the call to action
    builder
        .emplace<LinebreakElement>(MessageElementFlags{
            MessageElementFlag::SubscriptionHeader,
        })
        ->exhaustiveFlags = true;

    if (const auto image = vestaboardImage(board.characters))
    {
        builder.emplace<LinebreakElement>(MessageElementFlags{
            MessageElementFlag::Text,
        });
        builder.emplace<ImageElement>(image, MessageElementFlag::Text);
    }

    if (!board.contributors.empty())
    {
        builder.emplace<LinebreakElement>(MessageElementFlags{
            MessageElementFlag::Text,
        });
        builder.emplace<TextElement>(
            QStringLiteral("Top contributors:"), MessageElementFlag::Text,
            MessageColor::System, FontStyle::ChatMediumBold);
        for (size_t i = 0; i < board.contributors.size(); i++)
        {
            const auto &contributor = board.contributors[i];
            if (i > 0)
            {
                builder.emplace<TextElement>(QStringLiteral("·"),
                                             MessageElementFlag::Text,
                                             MessageColor::System);
            }
            appendEventName(builder, userNamed(channel, contributor.name),
                            assets, EventHeader{.hasContent = false});
            builder.emplace<TextElement>(dollars(contributor.amountCents),
                                         MessageElementFlag::Text,
                                         MessageColor::System);
        }
    }

    QString prompt;
    if (openHourly)
    {
        prompt =
            QStringLiteral("Be the first. Submit a design to claim the board.");
    }
    else
    {
        switch (board.kind)
        {
            case DggVestaboard::Kind::Lead:
                prompt = QStringLiteral(
                    "Think you can top it? Fund a challenger or submit your "
                    "own.");
                break;
            case DggVestaboard::Kind::Hourly:
                prompt = QStringLiteral(
                    "The auction's live. Fund a design to take the lead.");
                break;
            case DggVestaboard::Kind::Reset:
                prompt = QStringLiteral(
                    "A new auction begins now! Submit a design to claim the "
                    "board.");
                break;
        }
    }
    builder.emplace<LinebreakElement>(MessageElementFlags{
        MessageElementFlag::Text,
    });
    builder.emplace<TextElement>(prompt, MessageElementFlag::Text,
                                 MessageColor::System);
    emplaceLinkText(builder, QStringLiteral("Visit the Auction"),
                    dggSiteUrl(QStringLiteral("/vestaboard")).toString());

    builder->messageText = headerText;
    builder->searchText = headerText + QChar(u' ') + prompt;

    MessageParseArgs args;
    args.isSubscriptionMessage = true;
    const auto alert = processHighlights(builder, args);
    return {builder.release(), alert};
}

}  // namespace chatterino
