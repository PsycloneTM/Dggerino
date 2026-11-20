#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <optional>
#include <vector>

namespace chatterino {

/// What the user typed after `/mentions` (or `/m`), read the way chat-gui's
/// `cmdMENTIONS` reads it: `[nick] [limit]`, where a lone number is the
/// limit and the nick then is the user's own.
struct DggMentionsArgs {
    QString nick;
    int limit = 3;

    /// Not empty when the arguments are no good: the line to show.
    QString error;
};

/// How many lines chat-gui asks for when no limit is given.
constexpr int DGG_MENTIONS_DEFAULT_LIMIT = 3;
/// The most this asks the site for in one go.
constexpr int DGG_MENTIONS_MAX_LIMIT = 100;

/// `args` is the text after the command name. `ownNick` is the logged-in
/// user (empty if none): the nick used when none is given. The nick must
/// match chat-gui's `^\w{3,20}$`.
DggMentionsArgs parseDggMentionsArgs(const QString &args,
                                     const QString &ownNick);

/// `/api/chat/mentions?username=<nick>&limit=<limit>`
QUrl dggMentionsUrl(const QString &nick, int limit);

/// One line of the site's answer.
struct DggMention {
    QString nick;
    QString text;

    /// Unix time in milliseconds (the site gives seconds).
    qint64 timestampMs = 0;
};

/// Reads `{success, data: [{date, nick, text}, ...]}`. Nullopt if the site
/// says no (`success` is not true) or the shape is wrong. An empty list is a
/// valid answer: nobody mentioned the user. Entries without a nick or text
/// are skipped. The order is the site's (oldest first).
std::optional<std::vector<DggMention>> parseDggMentions(
    const QJsonObject &root);

/// `1st`, `2nd`, `3rd`, `4th`, `11th`, `22nd`, ...
QString dggOrdinal(int number);

/// chat-gui's `MMMM Do YYYY, h:mm:ss a` (`November 20th 2026, 9:14:28 am`),
/// always in English and for the time as it is given (convert to local time
/// first).
QString dggMentionsDateText(const QDateTime &time);

}  // namespace chatterino
