#include "providers/dgg/DggMessage.hpp"

#include "providers/dgg/DggCombo.hpp"
#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

using namespace chatterino;
using namespace Qt::StringLiterals;

namespace {

QJsonObject obj(const char *json)
{
    return QJsonDocument::fromJson(json).object();
}

constexpr qint64 NOW = 1000;

}  // namespace

TEST(DggMessage, HashMatchesChatGuiRecipe)
{
    // md5("<timestamp ms><user id><text>")
    EXPECT_EQ(dggMessageHash(1794165468400, 167779, QStringLiteral("OOOO")),
              QStringLiteral("12b94495cda14e88bc2760a8caa82b45"));

    // an unknown user id counts as empty
    EXPECT_EQ(dggMessageHash(1794165468400, 0, QStringLiteral("hello")),
              QStringLiteral("8e390221629e2a9cec1f1c502244a3bd"));
}

TEST(DggMessage, ParsesCapturedMsg)
{
    auto msg = parseDggChatMessage(
        obj(R"({"id":167779,"nick":"frozenuser14","roles":[],"features":["flair1","subscriber"],"createdDate":"2022-10-02T22:11:55Z","watching":{"platform":"angelthump","id":"kiloss"},"subscription":{"tier":2,"source":"destiny.gg"},"patreonMember":false,"photo":"alien","timestamp":1794165468400,"data":"OOOO"})"),
        NOW);
    ASSERT_TRUE(msg.has_value());

    EXPECT_EQ(msg->user.id, 167779);
    EXPECT_EQ(msg->user.nick, QStringLiteral("frozenuser14"));
    EXPECT_EQ(msg->user.subTier, 2);
    EXPECT_TRUE(msg->user.isSubscriber());
    EXPECT_FALSE(msg->user.isPrivileged());
    EXPECT_EQ(msg->user.features, (QStringList{QStringLiteral("flair1"),
                                               QStringLiteral("subscriber")}));
    EXPECT_EQ(msg->timestampMs, 1794165468400);
    EXPECT_EQ(msg->time().toMSecsSinceEpoch(), 1794165468400);
    EXPECT_EQ(msg->text, QStringLiteral("OOOO"));
    EXPECT_EQ(msg->hash, QStringLiteral("12b94495cda14e88bc2760a8caa82b45"));
    EXPECT_FALSE(msg->isAction);
    EXPECT_FALSE(msg->greentext);
}

TEST(DggMessage, NullSubscriptionIsTierZero)
{
    auto msg = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"features":[],"subscription":null,"timestamp":1,"data":"x"})"),
        NOW);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->user.subTier, 0);
    EXPECT_FALSE(msg->user.isSubscriber());
}

TEST(DggMessage, RejectsMissingNickOrText)
{
    EXPECT_FALSE(
        parseDggChatMessage(obj(R"({"id":5,"data":"x"})"), NOW).has_value());
    EXPECT_FALSE(
        parseDggChatMessage(obj(R"({"id":5,"nick":"a"})"), NOW).has_value());
    EXPECT_FALSE(
        parseDggChatMessage(obj(R"({"id":5,"nick":"a","data":""})"), NOW)
            .has_value());
}

TEST(DggMessage, MissingTimestampUsesNow)
{
    auto msg =
        parseDggChatMessage(obj(R"({"id":5,"nick":"a","data":"x"})"), 4242);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->timestampMs, 4242);
}

TEST(DggMessage, SlashMeIsAnActionInAnyCase)
{
    auto lower = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","timestamp":1,"data":"/me waves"})"), NOW);
    ASSERT_TRUE(lower.has_value());
    EXPECT_TRUE(lower->isAction);
    EXPECT_EQ(lower->text, QStringLiteral("waves"));
    EXPECT_EQ(lower->rawText, QStringLiteral("/me waves"));

    auto upper = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","timestamp":1,"data":"/ME waves"})"), NOW);
    ASSERT_TRUE(upper.has_value());
    EXPECT_TRUE(upper->isAction);
    EXPECT_EQ(upper->text, QStringLiteral("waves"));

    // needs the space, like chat-gui
    auto notAction = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","timestamp":1,"data":"/mex"})"), NOW);
    ASSERT_TRUE(notAction.has_value());
    EXPECT_FALSE(notAction->isAction);
    EXPECT_EQ(notAction->text, QStringLiteral("/mex"));
}

TEST(DggMessage, GreentextNeedsSubscriberOrPrivilegedRole)
{
    auto plain = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"subscription":null,"timestamp":1,"data":">be me"})"),
        NOW);
    ASSERT_TRUE(plain.has_value());
    EXPECT_FALSE(plain->greentext);

    auto sub = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"subscription":{"tier":1},"timestamp":1,"data":">be me"})"),
        NOW);
    ASSERT_TRUE(sub.has_value());
    EXPECT_TRUE(sub->greentext);

    for (const char *role : {"MODERATOR", "PROTECTED", "ADMIN"})
    {
        auto json =
            QStringLiteral(
                R"({"id":5,"nick":"a","roles":["%1"],"subscription":null,"timestamp":1,"data":">be me"})")
                .arg(QString::fromLatin1(role))
                .toUtf8();
        auto msg =
            parseDggChatMessage(QJsonDocument::fromJson(json).object(), NOW);
        ASSERT_TRUE(msg.has_value());
        EXPECT_TRUE(msg->user.isPrivileged()) << role;
        EXPECT_TRUE(msg->greentext) << role;
    }

    // roles that aren't privileged don't count (BOT, HOST, POLLS)
    auto bot = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":["BOT"],"subscription":null,"timestamp":1,"data":">be me"})"),
        NOW);
    ASSERT_TRUE(bot.has_value());
    EXPECT_FALSE(bot->greentext);

    // `>` has to be the first character
    auto later = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":[],"subscription":{"tier":1},"timestamp":1,"data":"hi >be me"})"),
        NOW);
    ASSERT_TRUE(later.has_value());
    EXPECT_FALSE(later->greentext);
}

TEST(DggMessage, GreentextAfterSlashMe)
{
    auto msg = parseDggChatMessage(
        obj(R"({"id":5,"nick":"a","roles":["ADMIN"],"timestamp":1,"data":"/me >be me"})"),
        NOW);
    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(msg->isAction);
    EXPECT_TRUE(msg->greentext);
}

TEST(DggMessage, ApplyChatTextReadsMeAndGreentext)
{
    // the mentions API has no flags of its own: the same rules as for `MSG`
    DggChatMessage msg;
    msg.user.subTier = 1;
    msg.rawText = u"/ME >be me"_s;
    dggApplyChatText(msg);
    EXPECT_TRUE(msg.isAction);
    EXPECT_EQ(msg.text, u">be me"_s);
    EXPECT_TRUE(msg.greentext);

    DggChatMessage plain;
    plain.rawText = u">be me"_s;
    dggApplyChatText(plain);
    EXPECT_FALSE(plain.isAction);
    EXPECT_EQ(plain.text, u">be me"_s);
    EXPECT_FALSE(plain.greentext);
}

TEST(DggMessage, ModerationText)
{
    auto mute = obj(
        R"({"id":9,"nick":"modguy","roles":["MODERATOR"],"features":[],"timestamp":1,"data":"spammer","duration":600})");
    EXPECT_EQ(dggModerationText(u"MUTE", mute),
              QStringLiteral("spammer muted by modguy for 10m."));

    auto muteNoDuration =
        obj(R"({"id":9,"nick":"modguy","timestamp":1,"data":"spammer"})");
    EXPECT_EQ(dggModerationText(u"MUTE", muteNoDuration),
              QStringLiteral("spammer muted by modguy."));

    auto ban = obj(R"({"id":9,"nick":"modguy","timestamp":1,"data":"troll"})");
    EXPECT_EQ(dggModerationText(u"BAN", ban),
              QStringLiteral("troll banned by modguy."));
    EXPECT_EQ(dggModerationText(u"UNBAN", ban),
              QStringLiteral("troll unbanned by modguy."));
    EXPECT_EQ(dggModerationText(u"UNMUTE", ban),
              QStringLiteral("troll unmuted by modguy."));

    // the website lifting a mute is sent as user -1 and has no moderator
    auto system =
        obj(R"({"id":-1,"nick":"System","timestamp":1,"data":"subscriber"})");
    EXPECT_EQ(dggModerationText(u"UNMUTE", system),
              QStringLiteral("subscriber has been unmuted."));
}

TEST(DggMessage, ModerationTextIgnoresOtherEventsAndEmptyTargets)
{
    EXPECT_TRUE(
        dggModerationText(u"MSG", obj(R"({"nick":"a","data":"b"})")).isEmpty());
    EXPECT_TRUE(dggModerationText(u"BAN", obj(R"({"nick":"a"})")).isEmpty());
    EXPECT_TRUE(
        dggModerationText(u"BAN", obj(R"({"nick":"a","data":""})")).isEmpty());
}

TEST(DggMessage, ParsesCapturedDeath)
{
    // the dead user is `nick`, `data` is the cause
    auto death = parseDggDeath(
        obj(R"({"id":134457,"nick":"Cyclone","roles":[],"features":["flair1","subscriber"],"subscription":{"tier":2,"source":"destiny.gg"},"timestamp":1794438748002,"duration":600,"data":"  believed his LIES  "})"),
        NOW);
    ASSERT_TRUE(death.has_value());
    EXPECT_EQ(death->user.nick, QStringLiteral("Cyclone"));
    EXPECT_EQ(death->user.id, 134457);
    EXPECT_EQ(death->user.subTier, 2);
    EXPECT_EQ(death->cause, QStringLiteral("believed his LIES"));
    EXPECT_EQ(death->durationSeconds, 600);
    EXPECT_EQ(death->timestampMs, 1794438748002);
}

TEST(DggMessage, DeathWithoutCauseDurationOrTimestamp)
{
    auto death = parseDggDeath(obj(R"({"nick":"Cyclone"})"), NOW);
    ASSERT_TRUE(death.has_value());
    EXPECT_TRUE(death->cause.isEmpty());
    EXPECT_EQ(death->durationSeconds, 0);
    EXPECT_EQ(death->timestampMs, NOW);
}

TEST(DggMessage, DeathNeedsANick)
{
    EXPECT_FALSE(
        parseDggDeath(obj(R"({"data":"x","duration":60})"), NOW).has_value());
    EXPECT_FALSE(
        parseDggDeath(obj(R"({"nick":"","data":"x"})"), NOW).has_value());
}

TEST(DggMessage, SubOnlyText)
{
    EXPECT_EQ(dggSubOnlyText(obj(
                  R"({"data":"on","tier":1,"nick":"modguy","timestamp":1})")),
              QStringLiteral("Subscriber only mode enabled by modguy."));
    EXPECT_EQ(
        dggSubOnlyText(
            obj(R"({"data":"on","tier":3,"nick":"modguy","timestamp":1})")),
        QStringLiteral("Subscriber only mode (Tier 3+) enabled by modguy."));
    EXPECT_EQ(dggSubOnlyText(obj(R"({"data":"off","timestamp":1})")),
              QStringLiteral("Subscriber only mode disabled."));
    // no tier means tier 1
    EXPECT_EQ(dggSubOnlyText(obj(R"({"data":"on","nick":"m"})")),
              QStringLiteral("Subscriber only mode enabled by m."));
}

TEST(DggMessage, ErrorText)
{
    EXPECT_TRUE(dggErrorText(obj(R"({"description":"banned"})"))
                    .startsWith(QStringLiteral("You have been banned!")));

    EXPECT_EQ(dggErrorText(obj(R"({"description":"toomanyconnections"})")),
              QStringLiteral("Only 5 concurrent connections allowed"));

    EXPECT_EQ(
        dggErrorText(obj(R"({"description":"submode","tier":2})")),
        QStringLiteral("The channel is currently in subscriber only mode (Tier "
                       "2+)"));

    EXPECT_EQ(
        dggErrorText(obj(R"({"description":"bannedphrase","filtered":"x"})")),
        QStringLiteral("Your message was blocked because it contained this "
                       "banned phrase: \"x\"."));

    EXPECT_TRUE(
        dggErrorText(obj(R"({"description":"muted","muteTimeLeft":90})"))
            .contains(QStringLiteral("1m 30s")));

    // unknown codes are shown as they are
    EXPECT_EQ(dggErrorText(obj(R"({"description":"somethingnew"})")),
              QStringLiteral("somethingnew"));
    EXPECT_EQ(dggErrorText(obj(R"({})")), QStringLiteral("Unknown error."));
}

TEST(DggMessage, ReadsPhotoCreatedDateAndWatching)
{
    const auto doc = QJsonDocument::fromJson(
        R"({"id":210984,"nick":"Kaimee","roles":[],"features":[],"createdDate":"2024-11-20T00:59:06Z","watching":{"platform":"kick","id":"drt0"},"subscription":null,"patreonMember":false,"photo":"buddy","timestamp":1794684753397,"data":"drt0 NOPERS"})");
    const auto user = DggUser::fromJson(doc.object());
    EXPECT_EQ(user.photo, u"buddy"_s);
    EXPECT_TRUE(user.createdDate.isValid());
    EXPECT_EQ(user.createdDate.date(), QDate(2024, 11, 20));
    EXPECT_EQ(user.watchingPlatform, u"kick"_s);
    EXPECT_EQ(user.watchingId, u"drt0"_s);
    EXPECT_TRUE(dggPhotoUrl({}).isEmpty());
}

TEST(DggMessage, NothingAboutPhotosIsBuiltIn)
{
    EXPECT_TRUE(dggPhotoUrl(u"neverlearned"_s).isEmpty());
    // a full URL is the picture itself
    EXPECT_EQ(dggPhotoUrl(u"https://example.org/a.png"_s).toString(),
              u"https://example.org/a.png"_s);
}

TEST(DggMessage, LearnsImagesWithPathsFromAPage)
{
    const QByteArray page =
        R"(<link href="/static/css/app.css">
           <img src="https://cdn.example.org/img/profile-photo-aaa.0123456789abcdef.png">
           <img src="/img/icons.fedcba9876543210.png">)";
    EXPECT_EQ(
        dggLearnSiteImages(page, QUrl(u"https://www.example.org/embed/chat"_s)),
        2);
    EXPECT_EQ(
        dggPhotoUrl(u"aaa"_s).toString(),
        u"https://cdn.example.org/img/profile-photo-aaa.0123456789abcdef.png"_s);
    EXPECT_EQ(dggSiteImage(u"icons"_s).toString(),
              u"https://www.example.org/img/icons.fedcba9876543210.png"_s);
}

TEST(DggMessage, RelativeUrlsInStylesheetsKeepTheirDirectory)
{
    // `.msg-death .user::before` on the site: the directory is relative to
    // the stylesheet, and must not be dropped
    const QByteArray css =
        R"(.msg-death .user::before {
             background-image: url(img/icons.b81b90059d7691309000.png);
             background-position: -32px 0px;
           })";
    EXPECT_EQ(
        dggLearnSiteImages(
            css, QUrl(u"https://cdn.destiny.gg/style.0123456789ab.css"_s)),
        1);
    EXPECT_EQ(dggSiteImage(u"icons"_s).toString(),
              u"https://cdn.destiny.gg/img/icons.b81b90059d7691309000.png"_s);
}

TEST(DggMessage, BareFileNamesTakeThePathOfAnotherImage)
{
    const QByteArray page =
        R"({"bbb":"profile-photo-bbb.aaaabbbbccccdddd.png"}
           <img src="https://cdn.example.org/img/profile-photo-ccc.1111222233334444.png">)";
    EXPECT_EQ(
        dggLearnSiteImages(page, QUrl(u"https://www.example.org/embed/chat"_s)),
        2);
    EXPECT_EQ(
        dggPhotoUrl(u"bbb"_s).toString(),
        u"https://cdn.example.org/img/profile-photo-bbb.aaaabbbbccccdddd.png"_s);
}

TEST(DggMessage, RehashedImagesReplaceOldOnes)
{
    const QUrl page(u"https://www.example.org/embed/chat"_s);
    dggLearnSiteImages(R"(<img src="/i/profile-photo-ddd.00000000.png">)",
                       page);
    dggLearnSiteImages(R"(<img src="/i/profile-photo-ddd.11111111.png">)",
                       page);
    EXPECT_EQ(dggPhotoUrl(u"ddd"_s).toString(),
              u"https://www.example.org/i/profile-photo-ddd.11111111.png"_s);
    EXPECT_EQ(dggLearnSiteImages("nothing here", page), 0);
}

TEST(DggMessage, SiteUrlsComeFromOnePlace)
{
    EXPECT_EQ(dggProfileUrl(u"A B"_s).path(), u"/u/A B"_s);
    EXPECT_EQ(dggUserInfoUrl(u"Just4Universe_"_s).query(),
              u"username=Just4Universe_"_s);
    EXPECT_EQ(dggBigscreenUrl(u"kick"_s, u"yunidev"_s).fragment(),
              u"kick/yunidev"_s);
    EXPECT_EQ(dggLogsUrl(u"MiXeD"_s).query(), u"username=mixed"_s);
}

TEST(DggMessage, ParsesUserInfo)
{
    const auto doc = QJsonDocument::fromJson(
        R"({"success":true,"message":null,"data":{"id":187353,"nick":"Just4Universe_","roles":[],"features":["flair127","subscriber"],"createdDate":"2023-06-16T17:37:46+00:00","photo":"buddy","bio":null,"gender":"male","age":"25-34","watching":{"platform":"kick","id":"yunidev"}}})");
    const auto profile = parseDggUserInfo(doc.object());
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->user.id, 187353);
    EXPECT_EQ(profile->user.nick, u"Just4Universe_"_s);
    EXPECT_EQ(profile->user.photo, u"buddy"_s);
    EXPECT_EQ(profile->user.watchingPlatform, u"kick"_s);
    EXPECT_EQ(profile->user.watchingId, u"yunidev"_s);
    EXPECT_TRUE(profile->user.features.contains(u"subscriber"_s));
    EXPECT_EQ(profile->age, u"25-34"_s);
    EXPECT_EQ(profile->gender, u"male"_s);
    EXPECT_TRUE(profile->bio.isEmpty());
}

TEST(DggMessage, UserInfoRefusalsAreNotProfiles)
{
    EXPECT_FALSE(parseDggUserInfo(
        QJsonDocument::fromJson(
            R"({"success":false,"message":"nope","data":null})")
            .object()));
    EXPECT_FALSE(parseDggUserInfo(
        QJsonDocument::fromJson(R"({"success":true,"data":{}})").object()));
    EXPECT_FALSE(parseDggUserInfo({}));
}

TEST(DggMessage, LearnsImagesFromEscapedJson)
{
    const QUrl page(u"https://www.example.org/embed/chat"_s);
    const QByteArray json =
        R"({"profilePhotos":{"buddy":"https:\/\/cdn.example.org\/img\/profile-photo-buddy.0123456789abcdef.png"}})";
    EXPECT_EQ(dggLearnSiteImages(json, page), 1);
    EXPECT_EQ(
        dggPhotoUrl(u"buddy"_s),
        QUrl(
            u"https://cdn.example.org/img/profile-photo-buddy.0123456789abcdef.png"_s));
}

TEST(DggMessage, ComboSplatterStaysRedThenFadesToGrey)
{
    // `emote-greyout`, 3500 ms: colour for 75 %, then linear to grey
    EXPECT_EQ(dggComboGreyPercent(0), 0);
    EXPECT_EQ(dggComboGreyPercent(2625), 0);
    EXPECT_EQ(dggComboGreyPercent(3062), 49);
    EXPECT_EQ(dggComboGreyPercent(3500), 100);
    EXPECT_EQ(dggComboGreyPercent(60000), 100);
}

TEST(DggMessage, ParsesASubscription)
{
    const auto event = parseDggEvent(
        u"SUBSCRIPTION",
        obj(R"({"user":{"id":7,"nick":"Subber","features":["flair3"]},
                "tier":3,"tierLabel":"Tier 3","amount":1000,"streak":4,
                "data":"hello PepoG","timestamp":5000,"uuid":"u1"})"),
        NOW);
    ASSERT_TRUE(event);
    EXPECT_EQ(event->kind, DggEvent::Kind::Subscription);
    EXPECT_EQ(event->user.nick, QStringLiteral("Subber"));
    EXPECT_EQ(event->tier, 3);
    EXPECT_EQ(event->tierLabel, QStringLiteral("Tier 3"));
    EXPECT_EQ(event->streak, 4);
    EXPECT_EQ(event->text, QStringLiteral("hello PepoG"));
    EXPECT_EQ(event->timestampMs, 5000);
    EXPECT_EQ(event->uuid, QStringLiteral("u1"));
}

TEST(DggMessage, TierLabelFallsBackToTheNumber)
{
    const auto event = parseDggEvent(
        u"SUBSCRIPTION", obj(R"({"user":{"nick":"Subber"},"tier":2})"), NOW);
    ASSERT_TRUE(event);
    EXPECT_EQ(event->tierLabel, QStringLiteral("Tier 2"));
    EXPECT_EQ(event->timestampMs, NOW);
}

TEST(DggMessage, ParsesAGiftSubWithItsRecipient)
{
    const auto event = parseDggEvent(
        u"GIFTSUB",
        obj(R"({"user":{"nick":"Giver"},"recipient":{"nick":"Lucky"},
                "tier":1,"fromMassGift":true})"),
        NOW);
    ASSERT_TRUE(event);
    EXPECT_EQ(event->kind, DggEvent::Kind::GiftSub);
    EXPECT_EQ(event->recipient.nick, QStringLiteral("Lucky"));

    // without a recipient there's nothing to say
    EXPECT_FALSE(parseDggEvent(
        u"GIFTSUB", obj(R"({"user":{"nick":"Giver"},"tier":1})"), NOW));
}

TEST(DggMessage, ParsesAMassGiftAndADonation)
{
    const auto mass = parseDggEvent(
        u"MASSGIFT", obj(R"({"user":{"nick":"Giver"},"tier":1,"quantity":5})"),
        NOW);
    ASSERT_TRUE(mass);
    EXPECT_EQ(mass->kind, DggEvent::Kind::MassGift);
    EXPECT_EQ(mass->quantity, 5);

    // an anonymous donation has no nick
    const auto donation = parseDggEvent(
        u"DONATION", obj(R"({"user":{},"amount":2500,"data":"gg"})"), NOW);
    ASSERT_TRUE(donation);
    EXPECT_EQ(donation->kind, DggEvent::Kind::Donation);
    EXPECT_EQ(donation->amountCents, 2500);
    EXPECT_TRUE(donation->user.nick.isEmpty());
}

TEST(DggMessage, ParsesASpotlight)
{
    const auto event = parseDggEvent(
        u"SPOTLIGHT", obj(R"({"user":{"id":3,"nick":"Author"},"nick":"Author",
                "data":"a quote","spotlightedBy":"Mod","key":"abc"})"),
        NOW);
    ASSERT_TRUE(event);
    EXPECT_EQ(event->kind, DggEvent::Kind::Spotlight);
    EXPECT_EQ(event->user.nick, QStringLiteral("Author"));
    EXPECT_EQ(event->spotlightedBy, QStringLiteral("Mod"));
    EXPECT_EQ(event->text, QStringLiteral("a quote"));
    // names the chat line that was spotlighted
    EXPECT_EQ(event->messageKey, QStringLiteral("abc"));

    EXPECT_FALSE(parseDggEvent(u"SPOTLIGHT", obj(R"({"nick":"Author"})"), NOW));
}

TEST(DggMessage, OnlyTheEventNamesAreEvents)
{
    EXPECT_TRUE(isDggEventName(u"DONATION"));
    EXPECT_FALSE(isDggEventName(u"MSG"));
    EXPECT_FALSE(isDggEventName(u"PAIDEVENTS"));
    EXPECT_FALSE(parseDggEvent(u"MSG", obj(R"({"nick":"x"})"), NOW));
}

TEST(DggMessage, ParsesAPin)
{
    const auto pin = parseDggPin(
        obj(R"({"nick":"Mod","features":["flair12"],"data":"  Welcome home  ",
                "uuid":"u-1","timestamp":123456})"),
        NOW);
    EXPECT_EQ(pin.user.nick, QStringLiteral("Mod"));
    EXPECT_EQ(pin.user.features, QStringList{QStringLiteral("flair12")});
    EXPECT_EQ(pin.text, QStringLiteral("Welcome home"));
    EXPECT_EQ(pin.uuid, QStringLiteral("u-1"));
    EXPECT_EQ(pin.timestampMs, 123456);
    EXPECT_FALSE(pin.isRemoval());
}

TEST(DggMessage, AnEmptyPinIsARemoval)
{
    const auto empty =
        parseDggPin(obj(R"({"nick":"Mod","data":"","uuid":"u-1"})"), NOW);
    EXPECT_TRUE(empty.isRemoval());
    // no timestamp: now
    EXPECT_EQ(empty.timestampMs, NOW);

    // the site sends no text at all for it as well
    EXPECT_TRUE(parseDggPin(obj(R"({"nick":"Mod"})"), NOW).isRemoval());
    EXPECT_TRUE(parseDggPin(obj(R"({"data":"   "})"), NOW).isRemoval());
}

TEST(DggMessage, ParsesAPollStart)
{
    // The server's clock is 995000 ms behind ours: the poll started 5 s ago
    // by the server's clock and has 25 s left
    const auto poll = parseDggPollStart(
        obj(R"({"canvote":true,"myvote":-1,"weighted":false,"hidden":false,
                "start":1000000,"now":1005000,"time":30000,
                "question":"Is it Friday?","options":["Yes","No"],
                "totals":[3,1],"totalvotes":4,"nick":"Mod"})"),
        2000000);
    ASSERT_TRUE(poll);
    EXPECT_EQ(poll->nick, QStringLiteral("Mod"));
    EXPECT_EQ(poll->question, QStringLiteral("Is it Friday?"));
    EXPECT_EQ(poll->options,
              (QStringList{QStringLiteral("Yes"), QStringLiteral("No")}));
    EXPECT_EQ(poll->totals, (std::vector<qint64>{3, 1}));
    EXPECT_EQ(poll->votesCast, 4);
    EXPECT_TRUE(poll->canVote);
    // -1 means "not voted"
    EXPECT_EQ(poll->myVote, 0);
    EXPECT_FALSE(poll->weighted);
    EXPECT_FALSE(poll->hidden);
    EXPECT_FALSE(poll->ended);
    EXPECT_EQ(poll->durationMs, 30000);
    EXPECT_EQ(poll->startedAtMs, 1995000);
    EXPECT_EQ(poll->endsAtMs, 2025000);
}

TEST(DggMessage, PollStartFallsBackToDefaults)
{
    // an empty hidden poll: no totals, no `time`, dates as ISO strings
    const auto poll = parseDggPollStart(obj(R"({"hidden":true,"weighted":true,
                "start":"2026-11-15T20:00:00.000Z",
                "now":"2026-11-15T20:00:00.000Z",
                "question":"Pick one","options":["A","B","C"],"nick":"Mod"})"),
                                        1700000000000);
    ASSERT_TRUE(poll);
    EXPECT_TRUE(poll->hidden);
    EXPECT_TRUE(poll->weighted);
    EXPECT_EQ(poll->durationMs, 30000);
    // one zero for each option, nothing voted
    EXPECT_EQ(poll->totals, (std::vector<qint64>{0, 0, 0}));
    EXPECT_EQ(poll->votesCast, 0);
    EXPECT_EQ(poll->endsAtMs - poll->startedAtMs, 30000);
}

TEST(DggMessage, PollTotalsAreMadeToFitTheOptions)
{
    // too few totals are padded, too many cut, strings are numbers too
    const auto fewer = parseDggPollStart(
        obj(R"({"question":"Q","options":["A","B","C"],"totals":[2,"5"],
                "nick":"Mod"})"),
        NOW);
    ASSERT_TRUE(fewer);
    EXPECT_EQ(fewer->totals, (std::vector<qint64>{2, 5, 0}));
    // without `totalvotes` the votes cast are the sum of the totals
    EXPECT_EQ(fewer->votesCast, 7);

    const auto more = parseDggPollStart(
        obj(R"({"question":"Q","options":["A"],"totals":[2,5,9],
                "nick":"Mod"})"),
        NOW);
    ASSERT_TRUE(more);
    EXPECT_EQ(more->totals, (std::vector<qint64>{2}));
}

TEST(DggMessage, PollStartNeedsAQuestionAndOptions)
{
    EXPECT_FALSE(parseDggPollStart(
        obj(R"({"question":"","options":["A","B"],"nick":"Mod"})"), NOW));
    EXPECT_FALSE(parseDggPollStart(
        obj(R"({"question":"Q","options":[],"nick":"Mod"})"), NOW));
    EXPECT_FALSE(parseDggPollStart(obj(R"({"nick":"Mod"})"), NOW));
}

TEST(DggMessage, APollIsTheSamePollAfterAReconnect)
{
    // The same poll, sent again after a reconnect: the server's `now` and
    // our own clock moved, `start` did not
    const auto first =
        parseDggPollStart(obj(R"({"start":1000000,"now":1005000,"question":"Q",
                "options":["A","B"],"nick":"Mod"})"),
                          2000000);
    const auto again =
        parseDggPollStart(obj(R"({"start":1000000,"now":1012000,"question":"Q",
                "options":["A","B"],"nick":"Mod"})"),
                          2009000);
    ASSERT_TRUE(first);
    ASSERT_TRUE(again);
    EXPECT_EQ(first->key(), again->key());

    // another poll by the same user has another key
    const auto other =
        parseDggPollStart(obj(R"({"start":1050000,"now":1055000,"question":"Q",
                "options":["A","B"],"nick":"Mod"})"),
                          2000000);
    ASSERT_TRUE(other);
    EXPECT_NE(first->key(), other->key());
}

TEST(DggMessage, ParsesAPollStop)
{
    const auto end =
        parseDggPollStop(obj(R"({"totals":[10,5],"totalvotes":15})"));
    ASSERT_TRUE(end);
    EXPECT_EQ(end->totals, (std::vector<qint64>{10, 5}));
    EXPECT_EQ(end->votesCast, 15);

    // a hidden poll's stop has the numbers; one without them has no result
    EXPECT_FALSE(parseDggPollStop(obj(R"({"totalvotes":15})")));
    EXPECT_FALSE(parseDggPollStop(obj(R"({})")));
}

TEST(DggMessage, ParsesAVote)
{
    const auto vote = parseDggPollVote(obj(R"({"vote":2,"quantity":3})"));
    EXPECT_EQ(vote.option, 2);
    EXPECT_EQ(vote.quantity, 3);

    // a hidden poll's votes come without the option, one vote each
    const auto hidden = parseDggPollVote(obj(R"({})"));
    EXPECT_EQ(hidden.option, 0);
    EXPECT_EQ(hidden.quantity, 1);

    // nonsense doesn't subtract votes
    const auto negative = parseDggPollVote(obj(R"({"vote":-4,"quantity":-2})"));
    EXPECT_EQ(negative.option, 0);
    EXPECT_EQ(negative.quantity, 0);
}

TEST(DggMessage, AVestaboardIsKeptToTheSizeOfTheBoard)
{
    // 100 rows of 1000 tiles would make a picture of hundreds of MB
    QString rows;
    for (int r = 0; r < 100; r++)
    {
        if (r > 0)
        {
            rows += u',';
        }
        rows += u'[';
        for (int c = 0; c < 1000; c++)
        {
            rows += (c > 0 ? u",1" : u"1");
        }
        rows += u']';
    }
    const QString json =
        uR"({"submitter":"Bob","total":500,"isNewDesign":true,"characters":[)"_s +
        rows + u"]}"_s;
    const auto board = parseDggVestaboard(
        u"VESTABOARD_LEAD", QJsonDocument::fromJson(json.toUtf8()).object(),
        NOW);

    ASSERT_TRUE(board);
    ASSERT_EQ(board->characters.size(), DGG_VESTABOARD_MAX_ROWS);
    for (const auto &row : board->characters)
    {
        EXPECT_EQ(row.size(), DGG_VESTABOARD_MAX_COLUMNS);
    }
}

TEST(DggMessage, AVestaboardTileCodeStaysSmall)
{
    const auto board = parseDggVestaboard(
        u"VESTABOARD_LEAD",
        obj(R"({"submitter":"Bob","characters":[[1,99999999999,-99999999999]]})"),
        NOW);

    ASSERT_TRUE(board);
    ASSERT_EQ(board->characters.size(), 1U);
    ASSERT_EQ(board->characters[0].size(), 3U);
    EXPECT_EQ(board->characters[0][0], 1);
    EXPECT_EQ(board->characters[0][1], 1000);
    EXPECT_EQ(board->characters[0][2], -1000);
}
