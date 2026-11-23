#include "providers/dgg/DggMentions.hpp"

#include "Test.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

using namespace chatterino;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

}  // namespace

TEST(DggMentions, ArgsDefaultToOwnNickAndLimit3)
{
    const auto args = parseDggMentionsArgs("", "cyclone");
    EXPECT_TRUE(args.error.isEmpty());
    EXPECT_EQ(args.nick, QString("cyclone"));
    EXPECT_EQ(args.limit, 3);
}

TEST(DggMentions, ArgsLoneNumberIsTheLimit)
{
    const auto args = parseDggMentionsArgs("  7 ", "cyclone");
    EXPECT_TRUE(args.error.isEmpty());
    EXPECT_EQ(args.nick, QString("cyclone"));
    EXPECT_EQ(args.limit, 7);
}

TEST(DggMentions, ArgsNickAndLimit)
{
    const auto args = parseDggMentionsArgs("Destiny 10", "cyclone");
    EXPECT_TRUE(args.error.isEmpty());
    EXPECT_EQ(args.nick, QString("Destiny"));
    EXPECT_EQ(args.limit, 10);
}

TEST(DggMentions, ArgsLimitIsClamped)
{
    EXPECT_EQ(parseDggMentionsArgs("0", "cyclone").limit, 1);
    EXPECT_EQ(parseDggMentionsArgs("5000", "cyclone").limit,
              DGG_MENTIONS_MAX_LIMIT);
    EXPECT_EQ(parseDggMentionsArgs("99999999999999999999999", "cyclone").limit,
              DGG_MENTIONS_MAX_LIMIT);
}

TEST(DggMentions, ArgsRejectBadNickAndLimit)
{
    // no own nick and none given
    EXPECT_FALSE(parseDggMentionsArgs("", "").error.isEmpty());
    // too short, too long, not a word
    EXPECT_FALSE(parseDggMentionsArgs("ab", "cyclone").error.isEmpty());
    EXPECT_FALSE(parseDggMentionsArgs("abcdefghijklmnopqrstu", "cyclone")
                     .error.isEmpty());
    EXPECT_FALSE(parseDggMentionsArgs("no-way", "cyclone").error.isEmpty());
    // limit that isn't a number
    EXPECT_FALSE(
        parseDggMentionsArgs("Destiny many", "cyclone").error.isEmpty());
}

TEST(DggMentions, UrlHasNickAndLimit)
{
    const auto url = dggMentionsUrl("cyclone", 3);
    EXPECT_EQ(url.path(), QString("/api/chat/mentions"));
    const QUrlQuery query(url);
    EXPECT_EQ(query.queryItemValue("username"), QString("cyclone"));
    EXPECT_EQ(query.queryItemValue("limit"), QString("3"));
}

TEST(DggMentions, ParsesTheSitesAnswer)
{
    const auto mentions = parseDggMentions(json(R"({
        "success": true,
        "message": null,
        "data": [
            {"date": 1795187569, "nick": "RiN_LuX", "text": "Cyclone it works"},
            {"date": 1795189700, "nick": "BarnDogg", "text": "Cyclone GotSnipes SALUTE"},
            {"date": 1795190000, "nick": "", "text": "no nick"}
        ],
        "error": null
    })"));

    ASSERT_TRUE(mentions.has_value());
    ASSERT_EQ(mentions->size(), 2U);
    EXPECT_EQ((*mentions)[0].nick, QString("RiN_LuX"));
    EXPECT_EQ((*mentions)[0].text, QString("Cyclone it works"));
    EXPECT_EQ((*mentions)[0].timestampMs, 1795187569000);
    EXPECT_EQ((*mentions)[1].nick, QString("BarnDogg"));
}

TEST(DggMentions, NoDataAndEmptyDataMeanNone)
{
    const auto none =
        parseDggMentions(json(R"({"success": true, "data": null})"));
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->empty());

    const auto empty =
        parseDggMentions(json(R"({"success": true, "data": []})"));
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->empty());
}

TEST(DggMentions, FailureIsNullopt)
{
    EXPECT_FALSE(parseDggMentions(json(R"({"success": false, "message": "x"})"))
                     .has_value());
    EXPECT_FALSE(parseDggMentions(json(R"({"data": []})")).has_value());
    EXPECT_FALSE(
        parseDggMentions(json(R"({"success": true, "data": 5})")).has_value());
    EXPECT_FALSE(parseDggMentions(QJsonObject{}).has_value());
}

TEST(DggMentions, Ordinals)
{
    EXPECT_EQ(dggOrdinal(1), QString("1st"));
    EXPECT_EQ(dggOrdinal(2), QString("2nd"));
    EXPECT_EQ(dggOrdinal(3), QString("3rd"));
    EXPECT_EQ(dggOrdinal(4), QString("4th"));
    EXPECT_EQ(dggOrdinal(11), QString("11th"));
    EXPECT_EQ(dggOrdinal(12), QString("12th"));
    EXPECT_EQ(dggOrdinal(13), QString("13th"));
    EXPECT_EQ(dggOrdinal(21), QString("21st"));
    EXPECT_EQ(dggOrdinal(22), QString("22nd"));
    EXPECT_EQ(dggOrdinal(23), QString("23rd"));
    EXPECT_EQ(dggOrdinal(30), QString("30th"));
}

TEST(DggMentions, DateTextLikeChatGui)
{
    const QDateTime morning(QDate(2026, 11, 20), QTime(9, 14, 28));
    EXPECT_EQ(dggMentionsDateText(morning),
              QString("November 20th 2026, 9:14:28 am"));

    const QDateTime evening(QDate(2026, 3, 1), QTime(21, 5, 9));
    EXPECT_EQ(dggMentionsDateText(evening),
              QString("March 1st 2026, 9:05:09 pm"));
}
