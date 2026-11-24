#include "providers/dgg/DggMentions.hpp"

#include "providers/dgg/DggMessage.hpp"

#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>

#include <algorithm>

namespace chatterino {

using namespace Qt::Literals;

namespace {

bool isDigits(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\d+$)"));
    return re.match(text).hasMatch();
}

}  // namespace

DggMentionsArgs parseDggMentionsArgs(const QString &args,
                                     const QString &ownNick)
{
    DggMentionsArgs out;
    out.nick = ownNick;

    const auto parts = args.split(QRegularExpression(QStringLiteral(R"(\s+)")),
                                  Qt::SkipEmptyParts);

    QString limitText;
    if (!parts.isEmpty())
    {
        if (isDigits(parts[0]))
        {
            // `/mentions 5`: the user's own, five of them
            limitText = parts[0];
        }
        else
        {
            out.nick = parts[0];
            if (parts.size() > 1)
            {
                limitText = parts[1];
            }
        }
    }

    // chat-gui's nickregex
    static const QRegularExpression nickRegex(QStringLiteral(R"(^\w{3,20}$)"));
    if (!nickRegex.match(out.nick).hasMatch())
    {
        out.error = u"Invalid nick - /mentions <nick> <limit>"_s;
        return out;
    }

    if (!limitText.isEmpty())
    {
        if (!isDigits(limitText))
        {
            out.error = u"Invalid limit - /mentions <nick> <limit>"_s;
            return out;
        }
        bool ok = false;
        const auto value = limitText.toLongLong(&ok);
        // a number too long for a qlonglong is also "too many"
        out.limit = ok ? static_cast<int>(std::clamp<qint64>(
                             value, 1, DGG_MENTIONS_MAX_LIMIT))
                       : DGG_MENTIONS_MAX_LIMIT;
    }
    return out;
}

QUrl dggMentionsUrl(const QString &nick, int limit)
{
    auto url = dggSiteUrl(u"/api/chat/mentions"_s);
    QUrlQuery query;
    query.addQueryItem(u"username"_s, nick);
    query.addQueryItem(u"limit"_s, QString::number(limit));
    url.setQuery(query);
    return url;
}

std::optional<std::vector<DggMention>> parseDggMentions(const QJsonObject &root)
{
    if (!root.value("success"_L1).toBool(false))
    {
        return std::nullopt;
    }

    const auto data = root.value("data"_L1);
    if (data.isNull() || data.isUndefined())
    {
        // `{"success": true, "data": null}` is how the site says "none"
        return std::vector<DggMention>{};
    }
    if (!data.isArray())
    {
        return std::nullopt;
    }

    std::vector<DggMention> out;
    for (const auto &value : data.toArray())
    {
        const auto obj = value.toObject();
        DggMention mention;
        mention.nick = obj.value("nick"_L1).toString();
        mention.text = obj.value("text"_L1).toString();
        // seconds; the limit keeps `* 1000` inside qint64
        mention.timestampMs =
            dggClampedInt64(obj.value("date"_L1).toDouble(),
                            9'000'000'000'000) *
            1000;
        if (mention.nick.isEmpty() || mention.text.isEmpty())
        {
            continue;
        }
        out.push_back(std::move(mention));
    }
    return out;
}

QString dggOrdinal(int number)
{
    const auto lastTwo = number % 100;
    QString suffix = u"th"_s;
    if (lastTwo < 11 || lastTwo > 13)
    {
        switch (number % 10)
        {
            case 1:
                suffix = u"st"_s;
                break;
            case 2:
                suffix = u"nd"_s;
                break;
            case 3:
                suffix = u"rd"_s;
                break;
            default:
                break;
        }
    }
    return QString::number(number) + suffix;
}

QString dggMentionsDateText(const QDateTime &time)
{
    const QLocale english = QLocale::c();
    const auto date = time.date();
    return u"%1 %2 %3, %4"_s.arg(
        english.monthName(date.month(), QLocale::LongFormat),
        dggOrdinal(date.day()), QString::number(date.year()),
        english.toString(time.time(), u"h:mm:ss ap"_s));
}

}  // namespace chatterino
