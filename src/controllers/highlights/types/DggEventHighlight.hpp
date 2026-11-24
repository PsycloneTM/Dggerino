// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "controllers/highlights/types/AnyHighlightForward.hpp"
#include "controllers/highlights/types/Common.hpp"
#include "controllers/highlights/types/Outcome.hpp"
#include "messages/MessageFlag.hpp"
#include "util/RapidjsonHelpers.hpp"

#include <pajlada/serialize/common.hpp>
#include <pajlada/serialize/deserialize.hpp>
#include <pajlada/serialize/serialize.hpp>
#include <QColor>
#include <QDebug>
#include <QStringView>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <optional>

namespace chatterino::highlights {

// What tells the destiny.gg highlights apart. The saved `id`s are part of the
// settings file: don't change them.

struct DggSubscriptionTraits {
    static constexpr QStringView ID = u"dggsubscription";
    static constexpr QStringView TYPE_NAME = u"DGG subscriptions";
    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg subscription (<code>SUBSCRIPTION</code> event).";
    static constexpr QStringView DEFAULT_NAME = u"DGG Subscriptions";
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        QColor(196, 102, 255, 100);
    static constexpr MessageFlag FLAG = MessageFlag::DggSubscription;
};

struct DggGiftTraits {
    static constexpr QStringView ID = u"dgggift";
    static constexpr QStringView TYPE_NAME = u"DGG gifted subscriptions";
    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg gifted subscription or mass gift<br "
        u"/>(<code>GIFTSUB</code> and <code>MASSGIFT</code> events).";
    static constexpr QStringView DEFAULT_NAME = u"DGG Gifted Subscriptions";
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        QColor(255, 102, 178, 100);
    static constexpr MessageFlag FLAG = MessageFlag::DggGift;
};

struct DggDonationTraits {
    static constexpr QStringView ID = u"dggdonation";
    static constexpr QStringView TYPE_NAME = u"DGG donations";
    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg donation (<code>DONATION</code> event).";
    static constexpr QStringView DEFAULT_NAME = u"DGG Donations";
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        QColor(80, 200, 120, 100);
    static constexpr MessageFlag FLAG = MessageFlag::DggDonation;
};

struct DggSpotlightTraits {
    static constexpr QStringView ID = u"dggspotlight";
    static constexpr QStringView TYPE_NAME = u"DGG spotlights";
    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg spotlighted message (<code>SPOTLIGHT</code> event).";
    static constexpr QStringView DEFAULT_NAME = u"DGG Spotlights";
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        QColor(255, 190, 60, 100);
    static constexpr MessageFlag FLAG = MessageFlag::DggSpotlight;
};

struct DggBroadcastTraits {
    static constexpr QStringView ID = u"dggbroadcast";
    static constexpr QStringView TYPE_NAME = u"DGG broadcasts";
    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg broadcast from a moderator or a bot "
        u"(<code>BROADCAST</code> event).";
    static constexpr QStringView DEFAULT_NAME = u"DGG Broadcasts";
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        QColor(70, 160, 255, 100);
    static constexpr MessageFlag FLAG = MessageFlag::DggBroadcast;
};

struct DggXPostTraits {
    static constexpr QStringView ID = u"dggxpost";
    static constexpr QStringView TYPE_NAME = u"DGG X posts";
    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg X post (<code>XPOST</code> event).";
    static constexpr QStringView DEFAULT_NAME = u"DGG X Posts";
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        QColor(200, 200, 200, 70);
    static constexpr MessageFlag FLAG = MessageFlag::DggXPost;
};

struct DggVestaboardTraits {
    static constexpr QStringView ID = u"dggvestaboard";
    static constexpr QStringView TYPE_NAME = u"DGG Vestaboard";
    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg Vestaboard event (<code>VESTABOARD_LEAD</code>, "
        u"<code>VESTABOARD_HOURLY</code> or <code>VESTABOARD_RESET</code>).";
    static constexpr QStringView DEFAULT_NAME = u"DGG Vestaboard";
    static constexpr QColor BACKGROUND_COLOR_DEFAULT = QColor(0, 144, 255, 80);
    static constexpr MessageFlag FLAG = MessageFlag::DggVestaboard;
};

/// Matches the messages of one kind of destiny.gg event (the ones carrying
/// `Traits::FLAG`). Every kind is its own type (see the aliases in
/// AnyHighlightForward.hpp, e.g. `DggGiftHighlight`), because each is an
/// alternative of `AnyHighlight`.
template <typename Traits>
struct DggEventHighlight {
    static constexpr QStringView ID = Traits::ID;
    static constexpr QStringView TYPE_NAME = Traits::TYPE_NAME;
    static constexpr QStringView ICON_RESOURCE =
        u":/buttons/settings-darkMode.svg";

    static constexpr QStringView DESCRIPTION = Traits::DESCRIPTION;

    static constexpr QStringView DEFAULT_NAME = Traits::DEFAULT_NAME;

    static constexpr bool ENABLED_BY_DEFAULT = true;
    static constexpr bool SHOW_IN_MENTIONS_DEFAULT = false;
    static constexpr bool ALERT_DEFAULT = false;
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        Traits::BACKGROUND_COLOR_DEFAULT;

    QString name;
    std::optional<bool> enabled;

    Outcome outcome = Outcome::create<DggEventHighlight>();

    HighlightCheck buildCheck() const
    {
        using H = DggEventHighlight;
        using Params = HighlightCheckParams;

        return {
            [highlight = *this](
                const Params &p) -> std::optional<HighlightResult> {
                if (!p.messageFlags.has(Traits::FLAG))
                {
                    return std::nullopt;
                }

                return highlight.outcome.template makeSimpleResult<H>();
            },
        };
    }

    friend QDebug operator<<(QDebug dbg, const DggEventHighlight &v)
    {
        dbg.nospace() << "DggEventHighlight("        //
                      << "id:" << ID                  //
                      << ',' << "name:" << v.name     //
                      << ',' << "enabled:" << v.enabled  //
                      << ',' << "outcome:" << v.outcome  //
                      << ')';

        return dbg;
    }
};

}  // namespace chatterino::highlights

namespace pajlada {

template <typename Traits>
struct Serialize<chatterino::highlights::DggEventHighlight<Traits>> {
    using H = chatterino::highlights::DggEventHighlight<Traits>;

    static rapidjson::Value get(const H &h,
                                rapidjson::Document::AllocatorType &a)
    {
        rapidjson::Value ret(rapidjson::kObjectType);
        chatterino::rj::set(ret, "id", H::ID, a);

        chatterino::rj::setOptionally(ret, "name", h.name, a);
        chatterino::rj::setOptionally(ret, "enabled", h.enabled, a);

        h.outcome.serialize(ret, a);

        return ret;
    }
};

template <typename Traits>
struct Deserialize<chatterino::highlights::DggEventHighlight<Traits>> {
    using H = chatterino::highlights::DggEventHighlight<Traits>;

    static H get(const rapidjson::Value &value, bool *error = nullptr)
    {
        if (!value.IsObject())
        {
            PAJLADA_REPORT_ERROR(error)
            return {};
        }

        if (!chatterino::highlights::matchesID(value, H::ID))
        {
            PAJLADA_REPORT_ERROR(error)
            return {};
        }

        H h;

        chatterino::rj::getSafe(value, "name", h.name);
        chatterino::rj::getSafe(value, "enabled", h.enabled);

        h.outcome.deserialize(value);

        return h;
    }
};

}  // namespace pajlada
