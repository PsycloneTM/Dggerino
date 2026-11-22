#include "providers/dgg/DggEmoteCss.hpp"

#include "Test.hpp"

#include <algorithm>

using namespace chatterino;

namespace {

/// The rules of DGG's real `emotes.css` (as of 2026-11-08) for the emotes the
/// tests below look at, with comments left out. The real file is 250 kB.
const char *const FIXTURE_CSS = R"CSS(
.emote.Askers {
    background-image: url("66596cb5db931.png");
    height: 34px;
    width: 5544px;
}
.msg-chat .emote.Askers {
    margin-top: -34px;
    top: 8.5px;
}
.emote.Aware {
    background-image: url("66596d4136715.png");
    height: 32px;
    width: 1152px;
}
.msg-chat .emote.Aware {
    margin-top: -32px;
    top: 8px;
}
.emote.BINGQILIN {
    background-image: url("691926a52c4e6.avif");
    height: 32px;
    width: 6390px;
}
.msg-chat .emote.BINGQILIN {
    margin-top: -32px;
    top: 8px;
}
.emote.dggL {
    background-image: url("6a4e93d269835.png");
    height: 35px;
    width: 825px;
}
.msg-chat .emote.dggL {
    margin-top: -35px;
    top: 8.75px;
}
.emote.glorp {
    background-image: url("691a4c80becd1.png");
    height: 28px;
    width: 308px;
}
.msg-chat .emote.glorp {
    margin-top: -28px;
    top: 7px;
}
.emote.WWWWaiting {
    background-image: url("6918d770809b0.png");
    height: 32px;
    width: 77px;
}
.msg-chat .emote.WWWWaiting {
    margin-top: -32px;
    top: 8px;
}

.emote.mhm {
    background-image: url("690f52d51011d.gif");
    height: 36px;
    width: 44px;
}
.emote.NativeSquare {
    background-image: url("native-square.png");
    height: 34px;
    width: 34px;
}
.emote.NativeWide {
    background-image: url("native-wide.png");
    height: 30px;
    width: 36px;
}
.emote.NativeAnimatedImage {
    background-image: url("native-animated.png");
    height: 28px;
    width: 28px;
}
.emote.mhm {
  transform-origin: bottom;
  transform: scale(calc(28/36));
  filter: hue-rotate(-30deg);
  margin-left: -5px;
  margin-right: -5px;
}
@keyframes Askers-anim {
  100% { background-position: -5544px; }
}
@keyframes Askers-hover {
  100% { background-position: -5544px; }
}
.emote.Askers {
  width: 56px;
  margin-left: -4px;
  margin-right: -4px;
  background-position: 0px;
  background-size: 5544px 34px;
  animation: Askers-anim calc(3.5s / var(--animation-speed, 1)) steps(99) 4;
}
.emote.Askers:hover {
  animation: Askers-hover calc(3.5s / var(--animation-speed, 1)) steps(99) infinite;
}
.emote.Aware {
  height: 32px;
  width: 36px;
  animation: Aware-anim 1.3s steps(31) forwards;
}
.emote.Aware:hover {
  height: 32px;
  width: 36px;
  animation: Aware-hover 1.3s steps(31) forwards 1;
}
@keyframes Aware-anim {
  to { background-position: -1116px; }
}
@keyframes Aware-hover {
  to { background-position: -1116px; }
}
@keyframes BINGQILIN {
  0% { background-position: var(--start); }
  100% { background-position: var(--end); }
}
@keyframes BINGQILIN-hover {
  0% { background-position: var(--start); }
  100% { background-position: var(--end); }
}
.emote.BINGQILIN {
  --start: 0px;
  --end: -2880px;
  width: 32px;
  background-position: -2848px; 
  animation: BINGQILIN steps(90) 3s 1;
}
.emote.BINGQILIN:hover {
  animation: BINGQILIN-hover steps(90) 3s infinite;
}
.msg-chat.msg-user[data-username="igor"] .emote.BINGQILIN {
  --start: -2880px;
  --end: -6390px;
  width: 39px;
  background-position: -6351px;
}
.emote.dggL {
  height: 35px;
  width: 33px;
  background-position: 0px;
  transition: all 0.5s;
}
.msg-chat.msg-user[data-username="peachachoo"] .emote.dggL {
  background-position: -693px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair13 ~ .text .emote.dggL {
  
  background-position: -33px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair1 ~ .text .emote.dggL {
  
  background-position: -66px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair3 ~ .text .emote.dggL {
  
  background-position: -99px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair8 ~ .text .emote.dggL {
  
  background-position: -132px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair42 ~ .text .emote.dggL {
  
  background-position: -165px;
  animation: dggL-T5 3s steps(19) infinite;
  transition: all 0.5s;
}
@keyframes dggL-T5 {
  0% {
    background-position: -165px;
  }
  100% {
    background-position: -792px;
  }
}
.emote.dggL:hover {
  
  background-position: 0px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 3px #ffffff);
}
.msg-chat.msg-user .user.flair13 ~ .text .emote.dggL:hover {
  
  background-position: -33px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #00a6ff);
}
.msg-chat.msg-user .user.flair1 ~ .text .emote.dggL:hover {
  
  background-position: -66px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #00ffe2);
}
.msg-chat.msg-user .user.flair3 ~ .text .emote.dggL:hover {
  
  background-position: -99px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #49ff00);
}
.msg-chat.msg-user .user.flair8 ~ .text .emote.dggL:hover {
  
  background-position: -132px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #ff00ef);
}
.msg-chat.msg-user .user.flair42 ~ .text .emote.dggL:hover {
  
  background-position: -165px;
  animation: dggL-T5 1.9s steps(19) infinite;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 1px #ff0000) drop-shadow(0px 0px 1px #ff7f00)
    drop-shadow(0px 0px 1px #ffff00) drop-shadow(0px 0px 1px #00ff00)
    drop-shadow(0px 0px 1px #8b00ff) drop-shadow(0px 0px 2px #0000ff);
}
.emote.glorp {
    height: 28px;
    width: 28px;
    background-position: 0px;
    background-repeat: no-repeat;
}
[data-username="prsek"] .emote.glorp {
    height: 28px;
    width: 28px;
    background-position: -110px;
    background-repeat: no-repeat;
    filter: hue-rotate(10deg) brightness(0.9) saturate(1.1);
}
[data-username="destiny"] .emote.glorp {
    height: 28px;
    width: 28px;
    background-position: -110px;
    background-repeat: no-repeat;
    filter: hue-rotate(10deg) brightness(0.9) saturate(1.1);
}
.emote.glorp:hover {
    filter: brightness(1.1) drop-shadow(0px 0px 3px #f7f7d7);
    transition: ease-out .4s;
}
.emote.WWWWaiting {
  width: 36px;
}
.msg-chat.msg-user[data-username="mrmouton"] .emote.WWWWaiting {
  width: 41px;
  background-position: -36px;
}
.msg-chat.msg-user[data-username="jaydrvernanda"] .emote.WWWWaiting {
  width: 36px;
  background-position: -0px;
}
)CSS";

const DggEmoteCss &css()
{
    static const DggEmoteCss parsed =
        DggEmoteCss::parse(QString::fromUtf8(FIXTURE_CSS));
    return parsed;
}

DggEmoteContext userContext(const char *nick)
{
    DggEmoteContext context;
    context.username = QString::fromUtf8(nick);
    return context;
}

int totalMs(const DggEmoteStyle &style)
{
    int total = 0;
    for (const int duration : style.sprite.durationsMs)
    {
        total += duration;
    }
    return total;
}

}  // namespace

TEST(DggEmoteCss, ParsesTheFixture)
{
    EXPECT_FALSE(css().isEmpty());
    EXPECT_EQ(css().stats().emotes, 9U);
    EXPECT_EQ(css().stats().unsupportedRules, 0U);
}

TEST(DggEmoteCss, MhmKeepsFullGifFrameAndItsCssFilter)
{
    const auto style = css().resolve(QStringLiteral("mhm"),
                                     userContext("someone"), QSize(44, 36));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.frameSize, QSize(44, 36));
    ASSERT_EQ(style.sprite.offsets.size(), 1U);
    EXPECT_EQ(style.sprite.offsets.front(), QPoint(0, 0));
    EXPECT_FALSE(style.animated());
    ASSERT_EQ(style.sprite.filters.size(), 1U);
    EXPECT_EQ(style.sprite.filters.front().kind, SpriteColorFilter::Kind::HueRotate);
    EXPECT_EQ(style.sprite.filters.front().amount, -30.0);
    // A filter makes it a processed whole-image animation, not a sprite sheet.
    EXPECT_FALSE(style.isWholeImage(QSize(44, 36)));
}

TEST(DggEmoteCss, FullImageAssetsRemainOnNativeImagePath)
{
    // Full-image assets are not sprite sheets. Their file decoder determines
    // whether they contain multiple frames; playback is handled generically by
    // the native image path, without emote-name or neighboring-emote rules.
    const auto square = css().resolve(QStringLiteral("NativeSquare"),
                                      userContext("someone"), QSize(34, 34));
    ASSERT_TRUE(square.valid);
    EXPECT_EQ(square.frameSize, QSize(34, 34));
    EXPECT_TRUE(square.sprite.filters.empty());
    EXPECT_TRUE(square.isWholeImage(QSize(34, 34)));

    const auto wide = css().resolve(QStringLiteral("NativeWide"),
                                    userContext("someone"), QSize(36, 30));
    ASSERT_TRUE(wide.valid);
    EXPECT_EQ(wide.frameSize, QSize(36, 30));
    EXPECT_TRUE(wide.sprite.filters.empty());
    EXPECT_TRUE(wide.isWholeImage(QSize(36, 30)));

    const auto animated = css().resolve(QStringLiteral("NativeAnimatedImage"),
                                        userContext("someone"), QSize(28, 28));
    ASSERT_TRUE(animated.valid);
    EXPECT_EQ(animated.frameSize, QSize(28, 28));
    EXPECT_TRUE(animated.sprite.filters.empty());
    EXPECT_TRUE(animated.isWholeImage(QSize(28, 28)));
}

TEST(DggEmoteCss, UnknownEmoteHasNoStyle)
{
    EXPECT_FALSE(css().hasEmote(QStringLiteral("NotAnEmote")));
    EXPECT_FALSE(css()
                     .resolve(QStringLiteral("NotAnEmote"), userContext("x"),
                              QSize(10, 10))
                     .valid);
}

TEST(DggEmoteCss, EmptyStylesheetResolvesNothing)
{
    const DggEmoteCss empty = DggEmoteCss::parse(QString());
    EXPECT_TRUE(empty.isEmpty());
    EXPECT_FALSE(empty.resolve(QStringLiteral("Askers"), userContext("x"),
                               QSize(5544, 34))
                     .valid);
}

TEST(DggEmoteCss, AskersShowsOneFrameOfNinetyNine)
{
    // The manifest says 5544x34. That's the whole sheet, not the emote.
    const auto style = css().resolve(QStringLiteral("Askers"),
                                     userContext("someone"), QSize(5544, 34));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.frameSize.width(), 56);
    EXPECT_EQ(style.frameSize.height(), 34);
    ASSERT_EQ(style.sprite.offsets.size(), 99U);
    EXPECT_EQ(style.sprite.offsets.front().x(), 0);
    EXPECT_EQ(style.sprite.offsets.back().x(), 98 * 56);
    EXPECT_EQ(style.sprite.durationsMs.size(), 99U);
    EXPECT_EQ(totalMs(style), 3500);
    EXPECT_TRUE(style.animated());
    EXPECT_FALSE(style.isWholeImage(QSize(5544, 34)));
    EXPECT_FALSE(css().dependsOnContext(QStringLiteral("Askers")));
}

TEST(DggEmoteCss, AwareHasThirtyOneFrames)
{
    const auto style = css().resolve(QStringLiteral("Aware"),
                                     userContext("someone"), QSize(1152, 32));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.frameSize.width(), 36);
    EXPECT_EQ(style.frameSize.height(), 32);
    ASSERT_EQ(style.sprite.offsets.size(), 31U);
    EXPECT_EQ(style.sprite.offsets.back().x(), 30 * 36);
    EXPECT_EQ(totalMs(style), 1300);
}

TEST(DggEmoteCss, BingqilinUsesIgorsRangeForIgor)
{
    const auto normal = css().resolve(QStringLiteral("BINGQILIN"),
                                      userContext("someone"), QSize(6390, 32));
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 32);
    ASSERT_EQ(normal.sprite.offsets.size(), 90U);
    EXPECT_EQ(normal.sprite.offsets.front().x(), 0);
    EXPECT_EQ(normal.sprite.offsets.back().x(), 2848);
    EXPECT_EQ(totalMs(normal), 3000);

    const auto igor = css().resolve(QStringLiteral("BINGQILIN"),
                                    userContext("igor"), QSize(6390, 32));
    ASSERT_TRUE(igor.valid);
    EXPECT_EQ(igor.frameSize.width(), 39);
    EXPECT_EQ(igor.frameSize.height(), 32);
    ASSERT_EQ(igor.sprite.offsets.size(), 90U);
    EXPECT_EQ(igor.sprite.offsets.front().x(), 2880);
    EXPECT_EQ(igor.sprite.offsets.back().x(), 6351);

    EXPECT_TRUE(css().dependsOnContext(QStringLiteral("BINGQILIN")));
}

TEST(DggEmoteCss, DggLPicksTheVariantOfTheUserAndTheTier)
{
    const QSize sheet(825, 35);

    const auto normal = css().resolve(QStringLiteral("dggL"),
                                      userContext("someone"), sheet);
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 33);
    EXPECT_EQ(normal.frameSize.height(), 35);
    ASSERT_EQ(normal.sprite.offsets.size(), 1U);
    EXPECT_EQ(normal.sprite.offsets[0].x(), 0);
    EXPECT_FALSE(normal.animated());

    // a username selector
    const auto peach = css().resolve(QStringLiteral("dggL"),
                                     userContext("peachachoo"), sheet);
    ASSERT_EQ(peach.sprite.offsets.size(), 1U);
    EXPECT_EQ(peach.sprite.offsets[0].x(), 693);

    // `.user.flair13 ~ .text`: the flair that colours the nick
    auto tier = userContext("someone");
    tier.messageClasses.append(QStringLiteral("flair13"));
    tier.nickColorFlair = QStringLiteral("flair13");
    const auto t1 = css().resolve(QStringLiteral("dggL"), tier, sheet);
    ASSERT_EQ(t1.sprite.offsets.size(), 1U);
    EXPECT_EQ(t1.sprite.offsets[0].x(), 33);

    // a flair the user has but that doesn't colour the nick doesn't count
    auto other = userContext("someone");
    other.messageClasses.append(QStringLiteral("flair13"));
    const auto none = css().resolve(QStringLiteral("dggL"), other, sheet);
    ASSERT_EQ(none.sprite.offsets.size(), 1U);
    EXPECT_EQ(none.sprite.offsets[0].x(), 0);

    EXPECT_TRUE(css().dependsOnContext(QStringLiteral("dggL")));
}

TEST(DggEmoteCss, GlorpIsTintedForSomeUsers)
{
    const QSize sheet(308, 28);

    const auto normal = css().resolve(QStringLiteral("glorp"),
                                      userContext("someone"), sheet);
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 28);
    EXPECT_EQ(normal.sprite.offsets[0].x(), 0);
    EXPECT_TRUE(normal.sprite.filters.empty());

    for (const char *nick : {"prsek", "destiny"})
    {
        const auto tinted =
            css().resolve(QStringLiteral("glorp"), userContext(nick), sheet);
        ASSERT_TRUE(tinted.valid);
        EXPECT_EQ(tinted.sprite.offsets[0].x(), 110);
        ASSERT_EQ(tinted.sprite.filters.size(), 3U);
        using Kind = SpriteColorFilter::Kind;
        EXPECT_TRUE(tinted.sprite.filters[0].kind == Kind::HueRotate);
        EXPECT_NEAR(tinted.sprite.filters[0].amount, 10.0, 1e-9);
        EXPECT_TRUE(tinted.sprite.filters[1].kind == Kind::Brightness);
        EXPECT_NEAR(tinted.sprite.filters[1].amount, 0.9, 1e-9);
        EXPECT_TRUE(tinted.sprite.filters[2].kind == Kind::Saturate);
        EXPECT_NEAR(tinted.sprite.filters[2].amount, 1.1, 1e-9);
    }
}

TEST(DggEmoteCss, WWWWaitingIsWiderForMrmouton)
{
    const QSize sheet(77, 32);

    const auto normal = css().resolve(QStringLiteral("WWWWaiting"),
                                      userContext("someone"), sheet);
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 36);
    EXPECT_EQ(normal.frameSize.height(), 32);
    EXPECT_EQ(normal.sprite.offsets[0].x(), 0);

    const auto mouton = css().resolve(QStringLiteral("WWWWaiting"),
                                      userContext("mrmouton"), sheet);
    ASSERT_TRUE(mouton.valid);
    EXPECT_EQ(mouton.frameSize.width(), 41);
    EXPECT_EQ(mouton.frameSize.height(), 32);
    EXPECT_EQ(mouton.sprite.offsets[0].x(), 36);
}

TEST(DggEmoteCss, WholeImageIsRecognised)
{
    DggEmoteStyle style;
    style.valid = true;
    style.frameSize = QSize(32, 32);
    style.sprite.frameSize = QSize(32, 32);
    style.sprite.offsets = {QPoint(0, 0)};
    style.sprite.durationsMs = {0};

    EXPECT_TRUE(style.isWholeImage(QSize(32, 32)));
    EXPECT_FALSE(style.isWholeImage(QSize(64, 32)));
    EXPECT_FALSE(style.isWholeImage(QSize()));

    style.sprite.filters.push_back({SpriteColorFilter::Kind::Invert, 1.0});
    EXPECT_FALSE(style.isWholeImage(QSize(32, 32)));
}

TEST(DggEmoteCss, StylesThatDrawTheSameShareAKey)
{
    const QSize sheet(825, 35);
    const auto a = css().resolve(QStringLiteral("dggL"),
                                 userContext("someone"), sheet);
    const auto b = css().resolve(QStringLiteral("dggL"),
                                 userContext("somebody"), sheet);
    const auto c = css().resolve(QStringLiteral("dggL"),
                                 userContext("peachachoo"), sheet);
    EXPECT_TRUE(a.cacheKey() == b.cacheKey());
    EXPECT_TRUE(a.cacheKey() != c.cacheKey());
}

TEST(DggEmoteCss, AskersPlaysFourTimesAndRestsOnTheFirstFrame)
{
    const auto style = css().resolve(QStringLiteral("Askers"),
                                     userContext("someone"), QSize(5544, 34));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.sprite.loopCount, 4);
    EXPECT_TRUE(style.sprite.playsOnce());
    // no `forwards`: it goes back to the element's own position
    EXPECT_EQ(style.sprite.restOffset, QPoint(0, 0));
}

TEST(DggEmoteCss, AwarePlaysOnceAndStaysOnTheLastFrame)
{
    const auto style = css().resolve(QStringLiteral("Aware"),
                                     userContext("someone"), QSize(1152, 32));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.sprite.loopCount, 1);
    // `forwards` keeps the end of the keyframes: the 32nd frame of the sheet,
    // which is one past the 31 that steps(31) shows
    EXPECT_EQ(style.sprite.restOffset, QPoint(31 * 36, 0));
}

TEST(DggEmoteCss, BingqilinPlaysOnceAndGoesBackToItsOwnFrame)
{
    const auto style = css().resolve(QStringLiteral("BINGQILIN"),
                                     userContext("someone"), QSize(6390, 32));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.sprite.loopCount, 1);
    EXPECT_EQ(style.sprite.restOffset, QPoint(2848, 0));
}

TEST(DggEmoteCss, InfiniteAnimationsKeepLooping)
{
    const auto style = css().resolve(QStringLiteral("dggL"),
                                     userContext("someone"), QSize(825, 35));
    ASSERT_TRUE(style.valid);
    if (style.animated())
    {
        EXPECT_EQ(style.sprite.loopCount, 0);
        EXPECT_FALSE(style.sprite.playsOnce());
    }
}

namespace {

const char *const GLOW_CSS = R"CSS(
.emote.Glowy {
  width: 30px;
  height: 30px;
  filter: brightness(1.1) drop-shadow(0px 0px 3px #001eff);
}
.emote.Star {
  width: 30px;
  height: 30px;
  animation: Star-anim 100ms 4;
}
@keyframes Star-anim {
  0%, 100% { filter: contrast(1.2); }
  50% { filter: contrast(1); }
}
.emote.Star::before {
  content: "*";
  font-size: 10px;
  color: white;
  position: absolute;
  top: 5px;
  left: 12px;
  opacity: 0;
  animation: Star-before 300ms 1 linear;
}
@keyframes Star-before {
  0% { opacity: 1; filter: drop-shadow(0 0 3px white); }
  100% { opacity: 0; }
}
)CSS";

const DggEmoteCss &glowCss()
{
    static const DggEmoteCss parsed =
        DggEmoteCss::parse(QString::fromUtf8(GLOW_CSS));
    return parsed;
}

}  // namespace

TEST(DggEmoteCss, DropShadowOfTheEmoteBecomesAFilterWithRoomAroundIt)
{
    const auto style = glowCss().resolve(QStringLiteral("Glowy"),
                                         userContext("someone"), QSize(30, 30));
    ASSERT_TRUE(style.valid);
    ASSERT_EQ(style.sprite.filters.size(), 2U);
    const auto &shadow = style.sprite.filters.back();
    EXPECT_EQ(shadow.kind, SpriteColorFilter::Kind::DropShadow);
    EXPECT_EQ(shadow.shadowBlur, 3.0);
    EXPECT_EQ(shadow.shadowRgba[2], 255.0);
    // 1.5 * blur, rounded up
    EXPECT_EQ(style.sprite.glowMargin, 5);
}

TEST(DggEmoteCss, PseudoElementIsPlayedWithTheEmotesOwnPoseAnimation)
{
    const auto style = glowCss().resolve(QStringLiteral("Star"),
                                         userContext("someone"), QSize(30, 30));
    ASSERT_TRUE(style.valid);
    ASSERT_TRUE(style.sprite.hasOverlays());
    EXPECT_TRUE(style.sprite.isValid());
    EXPECT_GT(style.sprite.offsets.size(), 1U);
    EXPECT_EQ(style.sprite.overlays.size(), style.sprite.offsets.size());
    EXPECT_EQ(style.sprite.poses.size(), style.sprite.offsets.size());
    EXPECT_TRUE(style.sprite.playsOnce());
    // the star fades out after 300 ms and the shake runs 4 * 100 ms
    EXPECT_EQ(totalMs(style), 400);
}

namespace {

// COGGERS as on the site: the spin is a transform animation, and a second
// rule reverses it for every even emote of a message.
const char *const COGGERS_CSS = R"CSS(
.emote.COGGERS {
    background-image: url("65f6c4a6484eb.png");
    height: 32px;
    width: 32px;
}
.msg-chat .emote.COGGERS {
    margin-top: -32px;
    top: 8px;
}
@keyframes COGGERS-anim {
    0% { transform: rotate(0deg); }
    100% { transform: rotate(1080deg); }
}
@keyframes COGGERS-anim-hover {
    0% { transform: rotate(0deg); }
    100% { transform: rotate(360deg); }
}
.emote.COGGERS {
    animation: COGGERS-anim 2.4s 1 cubic-bezier(0.15, 0, 0.85, 1);
}
.emote.COGGERS:hover {
    animation: COGGERS-anim-hover 0.8s infinite linear;
}
.emote.COGGERS:nth-child(even) {
    animation-direction: reverse;
}
)CSS";

const DggEmoteCss &coggersCss()
{
    static const DggEmoteCss parsed =
        DggEmoteCss::parse(QString::fromUtf8(COGGERS_CSS));
    return parsed;
}

}  // namespace

TEST(DggEmoteCss, CoggersSpinsInEveryPositionOfAMessage)
{
    // first of the message: odd, spins forwards
    auto first = userContext("someone");
    first.elements = {QStringLiteral("COGGERS"), QStringLiteral("COGGERS")};
    first.elementIndex = 0;
    const auto odd = coggersCss().resolve(QStringLiteral("COGGERS"), first,
                                          QSize(32, 32));
    ASSERT_TRUE(odd.valid);
    EXPECT_TRUE(odd.animated());
    EXPECT_EQ(odd.sprite.loopCount, 1);

    // second: even, `animation-direction: reverse` used to throw the whole
    // animation away
    auto second = first;
    second.elementIndex = 1;
    const auto even = coggersCss().resolve(QStringLiteral("COGGERS"), second,
                                           QSize(32, 32));
    ASSERT_TRUE(even.valid);
    EXPECT_TRUE(even.animated());
    EXPECT_EQ(even.sprite.loopCount, 1);
    EXPECT_EQ(even.sprite.poses.size(), odd.sprite.poses.size());
    // reversed: a quarter of the way in, the two aren't turned the same way
    // (frame 0 of both is upright, as 1080 degrees is three whole turns)
    ASSERT_GT(even.sprite.poses.size(), 8U);
    const size_t quarter = even.sprite.poses.size() / 4;
    EXPECT_NE(even.sprite.poses[quarter].matrix,
              odd.sprite.poses[quarter].matrix);
}

namespace {

// OverRustle as on the site: it shakes by moving itself 3 px with `left`.
// Slider comes in from the left like GIGASIP: a box that moves, not a shake.
const char *const NUDGE_CSS = R"CSS(
.emote.OverRustle {
    background-image: url("653c2e9e780ae.png");
    height: 30px;
    width: 33px;
}
.msg-chat .emote.OverRustle {
    margin-top: -30px;
    top: 7.5px;
}
.emote.Slider {
    background-image: url("65f6c4a6484ee.png");
    height: 30px;
    width: 29px;
}
.msg-chat .emote.Slider {
    margin-top: -30px;
    top: 7.5px;
}
@keyframes OverRustle-anim {
    from { left: 0; }
    to { left: 3px; }
}
.emote.OverRustle {
    animation: OverRustle-anim 100ms 7;
}
@keyframes Slider-anim {
  0% { left: -29px; }
  100% { left: 0px; }
}
.emote.Slider {
  animation: Slider-anim 1.5s 1 ease-out;
}
)CSS";

const DggEmoteCss &nudgeCss()
{
    static const DggEmoteCss parsed =
        DggEmoteCss::parse(QString::fromUtf8(NUDGE_CSS));
    return parsed;
}

}  // namespace

TEST(DggEmoteCss, LeftNudgeOfTheEmoteIsPlayedAsAShift)
{
    const auto style = nudgeCss().resolve(QStringLiteral("OverRustle"),
                                          userContext("someone"), QSize(33, 30));
    ASSERT_TRUE(style.valid);
    EXPECT_TRUE(style.animated());
    EXPECT_EQ(style.sprite.loopCount, 7);
    ASSERT_GT(style.sprite.poses.size(), 2U);
    EXPECT_EQ(style.sprite.poses.size(), style.sprite.offsets.size());

    // the picture moves right by up to 3 px and is back where it was at the end
    double most = 0.0;
    for (const auto &pose : style.sprite.poses)
    {
        EXPECT_GE(pose.matrix[4], -0.01);
        EXPECT_LE(pose.matrix[4], 3.01);
        most = std::max(most, pose.matrix[4]);
    }
    EXPECT_GT(most, 1.0);
}

TEST(DggEmoteCss, SlideInOfTheEmoteIsNotAShake)
{
    const auto style = nudgeCss().resolve(QStringLiteral("Slider"),
                                          userContext("someone"), QSize(29, 30));
    ASSERT_TRUE(style.valid);
    EXPECT_FALSE(style.animated());
}

namespace {

// NoTears exactly as on the site (copied from emotes.css, lines 1801-1809 and
// 6409-6544, including the mirrored rules): it throws a banana, its `::before`,
// unless it is alone, last after an emote, or a name (`.chat-user`) sits right
// in front of it.
const char *const CHAT_USER_CSS = R"CSS(
.emote.NoTears {
    background-image: url("5c2bbd3411c78.png");
    height: 30px;
    width: 31px;
}
.msg-chat .emote.NoTears {
    margin-top: -30px;
    top: 7.5px;
}
@keyframes NoTears-z {
  0%, 100% { z-index: 2; }
}
@keyframes NoTears-z-hover {  
  0%, 100% { z-index: 2; }
}
@keyframes NoTears-size {
  0%, 100% { 
  width: 69px;
  background-position: 6px;
  margin-left: -6px;
  margin-right: -20px; }
}

@keyframes NoTears-size-hover {
  0%, 100% {
  width: 69px;
  background-position: 6px;
  margin-left: -6px;
  margin-right: -20px; }
}

.emote.NoTears {
  --t-NoTears: 0.8s;
  margin-right: 12px;
  background-repeat: no-repeat;
  animation: NoTears-z var(--t-NoTears) 1 -.15s, NoTears-size var(--t-NoTears) 3 -.15s;
}

.emote.NoTears:hover {
  animation: NoTears-z-hover var(--t-NoTears) 1, NoTears-size-hover var(--t-NoTears) 3;
}

@keyframes NoTears-anim {
  0% { transform: rotate(-60deg) scaleX(.4); right: 47px; top: 12px; }
  15% { transform: rotate(-120deg) scaleX(.4); right: 57px; top: 12px; animation-timing-function: linear; }
  48% { transform: rotate(150deg) scaleX(.4); right: 17px; top: 1px; animation-timing-function: linear; }
  80% {transform: rotate(500deg) scaleX(.4); right: -21px; top: 9px; animation-timing-function: linear; }
}
@keyframes NoTears-anim-hover {
  0% { transform: rotate(-60deg) scaleX(.4); right: 47px; top: 12px; }
  20% { transform: rotate(-120deg) scaleX(.4); right: 57px; top: 12px; animation-timing-function: linear; }
  48% { transform: rotate(150deg) scaleX(.4); right: 17px; top: 1px; animation-timing-function: linear; }
  80% {transform: rotate(500deg) scaleX(.4); right: -21px; top: 9px; animation-timing-function: linear; }
}
.emote.NoTears::before{
    content: "🍌";
    text-indent: 0;
    color: white;
    font-size: 17px;
    line-height: initial;
    position: absolute;
    opacity: 1;
    top: -34px;
    text-shadow: none;
    width: 17px;
    transform: scaleX(.4) rotate(90deg);
    transform-origin: 55% 55%;
    animation: NoTears-anim var(--t-NoTears) 3 ease -.15s;
}
.emote.NoTears:hover::before {
    animation: NoTears-anim-hover var(--t-NoTears) infinite ease;
}

.emote.NoTears:only-child,
.emote + .emote.NoTears:last-child,
.chat-user + .emote.NoTears,
.emote.NoTears:has(+ .emote.Clap),
.emote.NoTears:has(+ .emote.RapThis),
.emote.NoTears:has(+ .emote.Sippy),
.emote.NoTears:has(+ .emote.ZoomerSippy),
.emote.NoTears:has(+ .emote.BoomerSippy),
.emote.NoTears:has(+ .emote.NoTears) {
  width: 31px;
  margin-right: 0 !important;
  animation: none;
}
.emote.NoTears:only-child::before,
.emote + .emote.NoTears:last-child::before,
.chat-user + .emote.NoTears::before,
.emote.NoTears:has(+ .emote.Clap)::before,
.emote.NoTears:has(+ .emote.RapThis)::before,
.emote.NoTears:has(+ .emote.Sippy)::before,
.emote.NoTears:has(+ .emote.ZoomerSippy)::before,
.emote.NoTears:has(+ .emote.BoomerSippy)::before,
.emote.NoTears:has(+ .emote.NoTears)::before {
  animation: none;
}

.emote.NoTears + .chat-user + .emote.NoTears,
.emote.NoTears + .emote:not(.NoTears) + .emote.NoTears {
  transform: scale(-1, 1);
  margin-left: 12px;
  margin-right: 0px;
  animation: NoTears-z var(--t-NoTears) 1 -.15s, NoTears-size-flipped var(--t-NoTears) 3 -.15s;
}

.emote.NoTears + .chat-user + .emote.NoTears:hover,
.emote.NoTears + .emote:not(.NoTears) + .emote.NoTears:hover {
  margin-left: 0px;
  animation: NoTears-z-hover var(--t-NoTears) 1, NoTears-size-flipped-hover var(--t-NoTears) 3;
}

.emote.NoTears + .chat-user + .emote.NoTears::before,
.emote.NoTears + .emote:not(.NoTears) + .emote.NoTears::before {
    content: "🍌";
    text-indent: 0;
    color: white;
    font-size: 17px;
    line-height: initial;
    position: absolute;
    opacity: 1;
    top: -34px;
    text-shadow: none;
    width: 17px;
    transform: scaleX(.4) rotate(90deg);
    transform-origin: 55% 55%;
    animation: NoTears-anim var(--t-NoTears) 3 ease -.15s;
}

.emote.NoTears + .chat-user + .emote.NoTears:hover::before,
.emote.NoTears + .emote:not(.NoTears) + .emote.NoTears:hover::before {
    animation: NoTears-anim-hover var(--t-NoTears) infinite ease;
}

@keyframes NoTears-size-flipped {
  0%, 100% { 
  width: 69px;
  margin-left: -26px; }
}

@keyframes NoTears-size-flipped-hover {
  0%, 100% {
  width: 69px;
  margin-left: -26px; }
}
)CSS";

const DggEmoteCss &chatUserCss()
{
    static const DggEmoteCss parsed =
        DggEmoteCss::parse(QString::fromUtf8(CHAT_USER_CSS));
    return parsed;
}

}  // namespace

TEST(DggEmoteCss, ChatUserInFrontOfTheEmoteCountsAsAChatUser)
{
    const QString name = QStringLiteral("NoTears");

    // `Destiny NoTears text`: a name, then the emote: no animation
    auto afterName = userContext("someone");
    afterName.elements = {DGG_CHAT_USER_ELEMENT, name, QString()};
    afterName.elementIndex = 1;
    const auto still =
        chatUserCss().resolve(name, afterName, QSize(31, 30));
    ASSERT_TRUE(still.valid);
    EXPECT_FALSE(still.sprite.hasOverlays());
    EXPECT_FALSE(still.animated());

    // a link in front of it is not a name: the rule doesn't apply
    auto afterLink = afterName;
    afterLink.elements = {QString(), name, QString()};
    const auto moving =
        chatUserCss().resolve(name, afterLink, QSize(31, 30));
    ASSERT_TRUE(moving.valid);
    // the banana
    EXPECT_TRUE(moving.sprite.hasOverlays());
}

namespace {

enum class NoTearsLook {
    Still,    // 31 px, no banana
    Wide,     // 69 px, throws the banana
    Flipped,  // mirrored, throws the banana
};

/// `seq` is the elements of the text, in order (plain words are not
/// elements): N NoTears, L another emote, C Clap, R RapThis, S Sippy,
/// U a name (`.chat-user`), K a link.
DggEmoteContext noTearsContext(const char *seq, int index)
{
    auto context = userContext("someone");
    for (const char *c = seq; *c != 0; c++)
    {
        switch (*c)
        {
            case 'N':
                context.elements.append(QStringLiteral("NoTears"));
                break;
            case 'L':
                context.elements.append(QStringLiteral("LUL"));
                break;
            case 'C':
                context.elements.append(QStringLiteral("Clap"));
                break;
            case 'R':
                context.elements.append(QStringLiteral("RapThis"));
                break;
            case 'S':
                context.elements.append(QStringLiteral("Sippy"));
                break;
            case 'U':
                context.elements.append(DGG_CHAT_USER_ELEMENT);
                break;
            default:
                context.elements.append(QString());
                break;
        }
    }
    context.elementIndex = index;
    return context;
}

void expectNoTears(const char *seq, int index, NoTearsLook want)
{
    SCOPED_TRACE(std::string(seq) + " #" + std::to_string(index));
    const auto style = chatUserCss().resolve(
        QStringLiteral("NoTears"), noTearsContext(seq, index), QSize(31, 30));
    ASSERT_TRUE(style.valid);
    switch (want)
    {
        case NoTearsLook::Still:
            EXPECT_FALSE(style.sprite.hasOverlays());
            EXPECT_FALSE(style.animated());
            break;
        case NoTearsLook::Wide:
            EXPECT_TRUE(style.sprite.hasOverlays());
            EXPECT_FALSE(style.sprite.canvasMirror);
            break;
        case NoTearsLook::Flipped:
            EXPECT_TRUE(style.sprite.hasOverlays());
            EXPECT_TRUE(style.sprite.canvasMirror);
            break;
    }
}

}  // namespace

// Every expectation below was produced by running the site's own selectors
// (emotes.css) through a CSS engine on the DOM chat-gui builds, and the
// selector matching of this file was checked against that engine over
// 10 000 element sequences.
TEST(DggEmoteCss, NoTearsFollowsTheSitesSelectors)
{
    using L = NoTearsLook;
    // alone, or the only element (`words NoTears words`)
    expectNoTears("N", 0, L::Still);
    // last after any emote (`LUL NoTears`, `NoTears words NoTears`)
    expectNoTears("LN", 1, L::Still);
    expectNoTears("NN", 0, L::Still);
    expectNoTears("NN", 1, L::Still);
    expectNoTears("NNN", 1, L::Still);
    // after a name, whatever follows
    expectNoTears("UN", 1, L::Still);
    expectNoTears("UNL", 1, L::Still);
    // followed by Clap, RapThis or Sippy
    expectNoTears("NC", 0, L::Still);
    expectNoTears("NR", 0, L::Still);
    expectNoTears("NS", 0, L::Still);
    // followed by something else (a name after it doesn't matter)
    expectNoTears("NL", 0, L::Wide);
    expectNoTears("NU", 0, L::Wide);
    expectNoTears("NK", 0, L::Wide);
    expectNoTears("KN", 1, L::Wide);
    expectNoTears("LNL", 1, L::Wide);
    // `NoTears <other emote or name> NoTears`: the last one is mirrored
    expectNoTears("NLN", 0, L::Wide);
    expectNoTears("NLN", 2, L::Flipped);
    expectNoTears("NUN", 2, L::Flipped);
    expectNoTears("NLNN", 2, L::Flipped);
    expectNoTears("NLNN", 3, L::Still);
    // a link in between is neither an emote nor a name
    expectNoTears("NKN", 2, L::Wide);
    // two names in a row: the second is not right behind a NoTears
    expectNoTears("NUUN", 3, L::Still);
    expectNoTears("NLUN", 3, L::Still);
}

// BONK hits (`BONK-anim` steps through the two frames of its sheet) and
// shakes (`YUMP-anim` animates `transform`) at the same time. Only the hit
// used to be played: an emote with a sprite animation got no poses.
TEST(DggEmoteCss, BonkShakesWhileItStepsThroughItsSheet)
{
    const char *const css = R"CSS(
.emote.BONK {
    background-image: url("66828047c48cd.png");
    height: 32px;
    width: 110px;
}
@keyframes BONK-anim {
  100% { background-position: -110px; }
}
.emote.BONK {
  width: 55px;
  height: 32px;
  animation: BONK-anim .15s steps(2) 9 alternate-reverse , YUMP-anim .3s 5 .1s reverse;
}
@keyframes YUMP-anim {
    0% { transform: translate(0px, 0px); }
    25% { transform: translate(0px, -7.5px) rotate(-3deg); animation-timing-function: ease }
    37% { transform: translate(0px, 0px) rotate(-3deg); animation-timing-function: ease-in }
    55% { transform: translate(0px, -3px) rotate(4deg); animation-timing-function: ease}
    60% { transform: translate(0px, 0px) rotate(4deg); animation-timing-function: ease-in}
    75% { transform: translate(0px, -1.5px); animation-timing-function: ease }
    100% { transform: translate(0px, 0px); animation-timing-function: ease-in}
}
)CSS";
    const auto parsed = DggEmoteCss::parse(QString::fromUtf8(css));
    const auto style = parsed.resolve(QStringLiteral("BONK"),
                                      userContext("someone"), QSize(110, 32));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.frameSize, QSize(55, 32));

    // one pose for every frame, over the 0.1 s delay and five 0.3 s shakes
    ASSERT_TRUE(style.sprite.hasPoses());
    EXPECT_EQ(style.sprite.poses.size(), style.sprite.offsets.size());
    EXPECT_EQ(style.sprite.durationsMs.size(), style.sprite.offsets.size());
    EXPECT_EQ(totalMs(style), 1600);
    EXPECT_EQ(style.sprite.loopCount, 1);

    // both frames of the sheet are still shown, and the shake lifts it up
    bool first = false;
    bool second = false;
    for (const auto &offset : style.sprite.offsets)
    {
        first = first || offset == QPoint(0, 0);
        second = second || offset == QPoint(55, 0);
    }
    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
    const auto lifted = std::any_of(
        style.sprite.poses.begin(), style.sprite.poses.end(),
        [](const SpritePose &pose) { return pose.matrix[5] < -2.0; });
    EXPECT_TRUE(lifted);

    // and it comes to rest on the first frame, not moved
    EXPECT_EQ(style.sprite.restOffset, QPoint(0, 0));
    EXPECT_TRUE(style.sprite.restPose.isIdentity());
}
