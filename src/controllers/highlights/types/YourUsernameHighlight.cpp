// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/types/YourUsernameHighlight.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "providers/dgg/DggAccount.hpp"
#include "providers/dgg/DggAccountManager.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/twitch/TwitchAccount.hpp"  // IWYU pragma: keep

namespace chatterino::highlights {

HighlightCheck YourUsernameHighlight::buildCheck() const
{
    using H = std::remove_pointer_t<decltype(this)>;
    using Params = HighlightCheckParams;

    // The pattern for the name of an account, or nothing if it's anonymous.
    auto nameRe = [](bool anonymous,
                     const QString &name) -> std::optional<QRegularExpression> {
        if (anonymous || name.isEmpty())
        {
            return std::nullopt;
        }

        return QRegularExpression{
            REGEX_START_BOUNDARY % QRegularExpression::escape(name) %
                REGEX_END_BOUNDARY,
            QRegularExpression::UseUnicodePropertiesOption |
                QRegularExpression::CaseInsensitiveOption,
        };
    };

    const auto twitchUser = getApp()->getAccounts()->twitch.getCurrent();
    const auto twitchRe =
        nameRe(twitchUser->isAnon(), twitchUser->getUserName());

    const auto kickUser = getApp()->getAccounts()->kick.current();
    const auto kickRe = nameRe(kickUser->isAnonymous(), kickUser->username());

    const auto dggUser = getApp()->getAccounts()->dgg.current();
    const auto dggRe = nameRe(dggUser->isAnonymous(), dggUser->username());

    if (!twitchRe && !kickRe && !dggRe)
    {
        return {};
    }

    return {
        [highlight = *this, twitchRe, kickRe,
         dggRe](const Params &p) -> std::optional<HighlightResult> {
            if (p.self)
            {
                return std::nullopt;
            }

            switch (p.platform)
            {
                case MessagePlatform::AnyOrTwitch: {
                    if (!twitchRe ||
                        !twitchRe->match(p.originalMessage).hasMatch())
                    {
                        return std::nullopt;
                    }
                }
                break;
                case MessagePlatform::Kick: {
                    if (!kickRe || !kickRe->match(p.originalMessage).hasMatch())
                    {
                        return std::nullopt;
                    }
                }
                break;
                case MessagePlatform::Dgg: {
                    if (!dggRe || !dggRe->match(p.originalMessage).hasMatch())
                    {
                        return std::nullopt;
                    }
                }
                break;
            }

            return highlight.outcome.makeSimpleResult<H>();
        },
    };
}

QDebug operator<<(QDebug dbg, const YourUsernameHighlight &v)
{
    dbg.nospace() << "YourUsernameHighlight("        //
                  << "name:" << v.name               //
                  << ',' << "enabled:" << v.enabled  //
                  << ',' << "outcome:" << v.outcome  //
                  << ')';

    return dbg;
}

}  // namespace chatterino::highlights
