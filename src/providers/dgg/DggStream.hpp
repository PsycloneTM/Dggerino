#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <cstdint>
#include <optional>

namespace chatterino {

/// What the site says about the broadcast, boiled down from
/// `/api/info/stream` (see dggStreamInfoUrl()).
///
/// The site lists one entry per platform (`youtube`, `kick`, `twitch`, ...;
/// null when the platform has nothing). Destiny is often live on several at
/// once, so this folds them together: he is live if any of them is, the
/// viewers are added up, and the title, game and thumbnail come from the
/// first live platform that has one.
struct DggStreamInfo {
    bool isLive = false;

    /// Names of the platforms that are live (`youtube`, `kick`), sorted.
    QStringList platforms;

    QString title;
    QString game;

    /// A picture of the stream. A format Qt can read everywhere (jpg) is
    /// preferred over `.webp`, which needs an image plugin.
    QString thumbnailUrl;

    /// When the earliest live platform went live. Invalid if not told.
    QDateTime startedAt;

    /// Viewers on all live platforms together.
    uint64_t viewers = 0;

    bool operator==(const DggStreamInfo &other) const = default;

    /// How long the stream has been live, like `1h 23m` or `43m`. Empty if
    /// the start isn't known.
    QString uptime(
        const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
};

/// `/api/info/stream`
QUrl dggStreamInfoUrl();

/// Reads the answer of the site's stream info:
/// `{"success": true, "data": {"streams": {"youtube": {...}|null, ...}}}`.
///
/// Nullopt if the site said no or sent something else, so that the caller
/// keeps what it knew instead of calling a failed request "offline". An
/// answer where every platform is null is a real answer: not live.
std::optional<DggStreamInfo> parseDggStreamInfo(const QJsonObject &root);

/// `seconds` as `1h 23m`, `43m` or `<1m`. Empty if negative.
QString dggFormatUptime(qint64 seconds);

}  // namespace chatterino
