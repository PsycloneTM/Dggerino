// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/types/Common.hpp"
#include "controllers/highlights/types/Outcome.hpp"
#include "util/RapidjsonHelpers.hpp"

#include <pajlada/serialize/common.hpp>
#include <pajlada/serialize/deserialize.hpp>
#include <pajlada/serialize/serialize.hpp>
#include <QDebug>
#include <QStringView>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <cassert>
#include <optional>

namespace chatterino::highlights {

/// Matches a destiny.gg gifted subscription or mass gift
struct DggGiftHighlight {
    static constexpr QStringView ID = u"dgggift";
    static constexpr QStringView TYPE_NAME = u"DGG gifted subscriptions";
    static constexpr QStringView ICON_RESOURCE =
        u":/buttons/settings-darkMode.svg";

    static constexpr QStringView DESCRIPTION =
        u"A destiny.gg gifted subscription or mass gift<br "
        u"/>(<code>GIFTSUB</code> and <code>MASSGIFT</code> events).";

    static constexpr QStringView DEFAULT_NAME = u"DGG Gifted Subscriptions";

    static constexpr bool ENABLED_BY_DEFAULT = true;
    static constexpr bool SHOW_IN_MENTIONS_DEFAULT = false;
    static constexpr bool ALERT_DEFAULT = false;
    static constexpr QColor BACKGROUND_COLOR_DEFAULT =
        QColor(255, 102, 178, 100);

    QString name;
    std::optional<bool> enabled;

    Outcome outcome = Outcome::create<DggGiftHighlight>();

    HighlightCheck buildCheck() const;

    friend QDebug operator<<(QDebug dbg, const DggGiftHighlight &v);
};

}  // namespace chatterino::highlights

namespace pajlada {

template <>
struct Serialize<chatterino::highlights::DggGiftHighlight> {
    using H = chatterino::highlights::DggGiftHighlight;

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

template <>
struct Deserialize<chatterino::highlights::DggGiftHighlight> {
    using H = chatterino::highlights::DggGiftHighlight;

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
