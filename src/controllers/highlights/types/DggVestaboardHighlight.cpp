// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/types/DggVestaboardHighlight.hpp"

#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "messages/MessageFlag.hpp"

namespace chatterino::highlights {

HighlightCheck DggVestaboardHighlight::buildCheck() const
{
    using H = std::remove_pointer_t<decltype(this)>;
    using Params = HighlightCheckParams;

    return {
        [highlight = *this](const Params &p) -> std::optional<HighlightResult> {
            if (!p.messageFlags.has(MessageFlag::DggVestaboard))
            {
                return std::nullopt;
            }

            return highlight.outcome.makeSimpleResult<H>();
        },
    };
}

QDebug operator<<(QDebug dbg, const DggVestaboardHighlight &v)
{
    dbg.nospace() << "DggVestaboardHighlight("       //
                  << "name:" << v.name               //
                  << ',' << "enabled:" << v.enabled  //
                  << ',' << "outcome:" << v.outcome  //
                  << ')';

    return dbg;
}

}  // namespace chatterino::highlights
