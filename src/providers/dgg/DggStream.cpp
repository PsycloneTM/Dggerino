#include "providers/dgg/DggStream.hpp"

#include "providers/dgg/DggMessage.hpp"

#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>

namespace chatterino {

using namespace Qt::Literals;

QUrl dggStreamInfoUrl()
{
    return dggSiteUrl(u"/api/info/stream"_s);
}

QString dggFormatUptime(qint64 seconds)
{
    if (seconds < 0)
    {
        return {};
    }

    const auto minutes = seconds / 60;
    if (minutes < 1)
    {
        return u"<1m"_s;
    }

    const auto hours = minutes / 60;
    if (hours < 1)
    {
        return QString::number(minutes) + u"m"_s;
    }
    return QString::number(hours) + u"h "_s + QString::number(minutes % 60) +
           u"m"_s;
}

namespace {

/// `2026-11-22T18:15:56+0000`, which the site sends, with the offset as
/// `+00:00`, a form every Qt version reads.
QDateTime parseSiteTime(QString text)
{
    static const QRegularExpression offset(u"([+-]\\d{2})(\\d{2})$"_s);
    text.replace(offset, u"\\1:\\2"_s);
    return QDateTime::fromString(text, Qt::ISODate);
}

}  // namespace

QString DggStreamInfo::uptime(const QDateTime &now) const
{
    if (!this->startedAt.isValid())
    {
        return {};
    }
    return dggFormatUptime(this->startedAt.secsTo(now));
}

std::optional<DggStreamInfo> parseDggStreamInfo(const QJsonObject &root)
{
    if (!root.value("success"_L1).toBool(false))
    {
        return std::nullopt;
    }

    const auto data = root.value("data"_L1);
    if (!data.isObject())
    {
        return std::nullopt;
    }

    const auto streamsValue = data.toObject().value("streams"_L1);
    if (!streamsValue.isObject())
    {
        return std::nullopt;
    }
    const auto streams = streamsValue.toObject();

    DggStreamInfo out;
    bool haveThumbnailJpg = false;

    // QJsonObject iterates in key order, which keeps the result stable
    for (auto it = streams.begin(); it != streams.end(); ++it)
    {
        // `null` is how the site says "nothing on this platform"
        if (!it.value().isObject())
        {
            continue;
        }
        const auto stream = it.value().toObject();
        if (!stream.value("live"_L1).toBool(false))
        {
            continue;
        }

        out.isLive = true;
        out.platforms.push_back(it.key());
        out.viewers += static_cast<uint64_t>(
            std::max(0.0, stream.value("viewers"_L1).toDouble()));

        if (out.title.isEmpty())
        {
            out.title = stream.value("status_text"_L1).toString();
        }
        if (out.game.isEmpty())
        {
            out.game = stream.value("game"_L1).toString();
        }

        const auto preview = stream.value("preview"_L1).toString();
        if (!preview.isEmpty())
        {
            const bool isWebp = QUrl(preview).path().endsWith(
                u".webp"_s, Qt::CaseInsensitive);
            if (out.thumbnailUrl.isEmpty() || (!haveThumbnailJpg && !isWebp))
            {
                out.thumbnailUrl = preview;
                haveThumbnailJpg = !isWebp;
            }
        }

        // `2026-11-22T18:15:56+0000`
        auto startedAt =
            parseSiteTime(stream.value("started_at"_L1).toString());
        if (startedAt.isValid() &&
            (!out.startedAt.isValid() || startedAt < out.startedAt))
        {
            out.startedAt = startedAt.toUTC();
        }
    }

    return out;
}

}  // namespace chatterino
