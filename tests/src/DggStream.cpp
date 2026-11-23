#include "providers/dgg/DggStream.hpp"

#include "Test.hpp"

#include <QJsonDocument>

using namespace chatterino;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

// What www.destiny.gg/api/info/stream answers while live on two platforms
const char *LIVE_ON_TWO = R"({
  "success": true, "message": null, "error": null,
  "data": {"streams": {
    "twitch": null,
    "youtube": {
      "live": true, "game": null,
      "preview": "https://i.ytimg.com/vi/axV3C-IjQuo/hqdefault.jpg?v=6b033101",
      "status_text": "chillin' like a villain",
      "started_at": "2026-11-22T18:15:56+0000", "ended_at": null,
      "duration": 2610, "viewers": 4709, "id": "axV3C-IjQuo",
      "platform": "youtube", "type": "livestream"
    },
    "facebook": null, "rumble": null,
    "kick": {
      "live": true, "game": "Just Chatting",
      "preview": "https://images.kick.com/video_thumbnails/0PMYoN0I2p4i/fkv4GIpaP0TI/480.webp",
      "status_text": "chillin' like a villain",
      "started_at": "2026-11-22T18:15:56+0000", "ended_at": null,
      "duration": 2610, "viewers": 3399, "id": "destiny",
      "platform": "kick", "type": null
    }
  }}
})";

}  // namespace

TEST(DggStream, LiveOnTwoPlatformsIsFoldedTogether)
{
    const auto info = parseDggStreamInfo(json(LIVE_ON_TWO));
    ASSERT_TRUE(info.has_value());

    EXPECT_TRUE(info->isLive);
    EXPECT_EQ(info->platforms, (QStringList{"kick", "youtube"}));
    EXPECT_EQ(info->viewers, 4709u + 3399u);
    EXPECT_EQ(info->title, QString("chillin' like a villain"));
    EXPECT_EQ(info->game, QString("Just Chatting"));
    EXPECT_EQ(info->startedAt,
              QDateTime(QDate(2026, 11, 22), QTime(18, 15, 56), Qt::UTC));
}

TEST(DggStream, PrefersJpgThumbnailOverWebp)
{
    const auto info = parseDggStreamInfo(json(LIVE_ON_TWO));
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->thumbnailUrl.contains("ytimg.com"));
}

TEST(DggStream, WebpThumbnailIsUsedWhenItIsAllThereIs)
{
    const auto info = parseDggStreamInfo(json(R"({
      "success": true,
      "data": {"streams": {"kick": {
        "live": true, "viewers": 5,
        "preview": "https://images.kick.com/x/480.webp"
      }}}
    })"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->thumbnailUrl,
              QString("https://images.kick.com/x/480.webp"));
}

TEST(DggStream, OnlyOneLivePlatformCounts)
{
    const auto info = parseDggStreamInfo(json(R"({
      "success": true,
      "data": {"streams": {
        "youtube": {"live": false, "viewers": 99, "status_text": "old"},
        "kick": {"live": true, "viewers": 12, "status_text": "now"}
      }}
    })"));
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isLive);
    EXPECT_EQ(info->platforms, QStringList{"kick"});
    EXPECT_EQ(info->viewers, 12u);
    EXPECT_EQ(info->title, QString("now"));
}

TEST(DggStream, EveryPlatformNullIsOffline)
{
    const auto info = parseDggStreamInfo(json(R"({
      "success": true,
      "data": {"streams": {"twitch": null, "youtube": null, "kick": null}}
    })"));
    ASSERT_TRUE(info.has_value());
    EXPECT_FALSE(info->isLive);
    EXPECT_TRUE(info->platforms.isEmpty());
    EXPECT_EQ(info->viewers, 0u);
}

TEST(DggStream, PlatformsMarkedNotLiveAreOffline)
{
    const auto info = parseDggStreamInfo(json(R"({
      "success": true,
      "data": {"streams": {"youtube": {"live": false, "viewers": 0}}}
    })"));
    ASSERT_TRUE(info.has_value());
    EXPECT_FALSE(info->isLive);
}

TEST(DggStream, FailedOrOddAnswersAreIgnored)
{
    // so a hiccup of the site never turns into "offline"
    EXPECT_FALSE(parseDggStreamInfo(json("{}")).has_value());
    EXPECT_FALSE(parseDggStreamInfo(json(R"({"success": false, "data": null})"))
                     .has_value());
    EXPECT_FALSE(parseDggStreamInfo(json(R"({"success": true, "data": null})"))
                     .has_value());
    EXPECT_FALSE(parseDggStreamInfo(json(R"({"success": true, "data": {}})"))
                     .has_value());
    EXPECT_FALSE(parseDggStreamInfo(
                     json(R"({"success": true, "data": {"streams": []}})"))
                     .has_value());
}

TEST(DggStream, EarliestStartWins)
{
    const auto info = parseDggStreamInfo(json(R"({
      "success": true,
      "data": {"streams": {
        "kick": {"live": true, "started_at": "2026-11-22T18:30:00+0000"},
        "youtube": {"live": true, "started_at": "2026-11-22T18:15:56+0000"}
      }}
    })"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->startedAt,
              QDateTime(QDate(2026, 11, 22), QTime(18, 15, 56), Qt::UTC));
}

TEST(DggStream, UptimeText)
{
    EXPECT_EQ(dggFormatUptime(-5), QString());
    EXPECT_EQ(dggFormatUptime(0), QString("<1m"));
    EXPECT_EQ(dggFormatUptime(59), QString("<1m"));
    EXPECT_EQ(dggFormatUptime(60), QString("1m"));
    EXPECT_EQ(dggFormatUptime(2540), QString("42m"));
    EXPECT_EQ(dggFormatUptime(3600), QString("1h 0m"));
    EXPECT_EQ(dggFormatUptime(3600 * 5 + 60 * 7 + 9), QString("5h 7m"));

    DggStreamInfo info;
    EXPECT_EQ(info.uptime(), QString());  // start unknown
    info.startedAt = QDateTime(QDate(2026, 11, 22), QTime(18, 15, 56), Qt::UTC);
    EXPECT_EQ(
        info.uptime(QDateTime(QDate(2026, 11, 22), QTime(18, 58, 16), Qt::UTC)),
        QString("42m"));
}

TEST(DggStream, UrlPointsAtTheStreamInfoEndpoint)
{
    EXPECT_EQ(dggStreamInfoUrl().path(), QString("/api/info/stream"));
}
