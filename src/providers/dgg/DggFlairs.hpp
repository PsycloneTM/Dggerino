#pragma once

#include <QString>

namespace chatterino {

/// The flair that is the tier badge (and colour) of a subscription tier
/// (1 to 5), or an empty string for any other tier. The only place that
/// names these ids: the site renumbering a tier flair is one edit here.
inline QString dggTierFlairName(int tier)
{
    switch (tier)
    {
        case 1:
            return QStringLiteral("flair13");
        case 2:
            return QStringLiteral("flair1");
        case 3:
            return QStringLiteral("flair3");
        case 4:
            return QStringLiteral("flair8");
        case 5:
            return QStringLiteral("flair42");
        default:
            return {};
    }
}

}  // namespace chatterino
