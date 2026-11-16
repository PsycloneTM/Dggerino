// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/types/DggGiftHighlight.hpp"

#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "messages/MessageFlag.hpp"

namespace chatterino::highlights {

HighlightCheck DggGiftHighlight::buildCheck() const
{
    using H = std::remove_pointer_t<decltype(this)>;
    using Params = HighlightCheckParams;

    return {
        [highlight = *this](const Params &p) -> std::optional<HighlightResult> {
            if (!p.messageFlags.has(MessageFlag::DggGift))
            {
                return std::nullopt;
            }

            return highlight.outcome.makeSimpleResult<H>();
        },
    };
}

QDebug operator<<(QDebug dbg, const DggGiftHighlight &v)
{
    dbg.nospace() << "DggGiftHighlight("       //
                  << "name:" << v.name               //
                  << ',' << "enabled:" << v.enabled  //
                  << ',' << "outcome:" << v.outcome  //
                  << ')';

    return dbg;
}

}  // namespace chatterino::highlights
