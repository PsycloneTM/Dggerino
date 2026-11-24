// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <variant>

namespace chatterino::highlights {

struct YourUsernameHighlight;
struct WhispersHighlight;
struct AnnouncementsHighlight;
struct SubscriptionsHighlight;
struct InvalidHighlight;
struct ChannelPointsHighlight;
struct FirstMessageHighlight;
struct SubscribedThreadHighlight;
struct AutomodCaughtHighlight;
struct WatchStreakHighlight;
struct YourMessagesHighlight;
struct MessageHighlight;
struct FilterHighlight;
struct UserHighlight;
struct BadgeHighlight;
struct LowTrustUserHighlight;
struct UncategorizedNotificationHighlight;
// The destiny.gg highlights: one template, see DggEventHighlight.hpp
template <typename Traits>
struct DggEventHighlight;
struct DggSubscriptionTraits;
struct DggGiftTraits;
struct DggDonationTraits;
struct DggSpotlightTraits;
struct DggBroadcastTraits;
struct DggXPostTraits;
struct DggVestaboardTraits;
using DggSubscriptionHighlight = DggEventHighlight<DggSubscriptionTraits>;
using DggGiftHighlight = DggEventHighlight<DggGiftTraits>;
using DggDonationHighlight = DggEventHighlight<DggDonationTraits>;
using DggSpotlightHighlight = DggEventHighlight<DggSpotlightTraits>;
using DggBroadcastHighlight = DggEventHighlight<DggBroadcastTraits>;
using DggXPostHighlight = DggEventHighlight<DggXPostTraits>;
using DggVestaboardHighlight = DggEventHighlight<DggVestaboardTraits>;

// clang-format off
/// Variant of all types of highlights.
///
/// When you add a new built-in highlight, you must:
///  - Add it to HighlightController::billTinHighlights
///  - Add it to Settings::recreateMissingHighlights
///  - Confirm the `missing-built-in.json` snapshot test is updated with your new highlight
///
/// When you add a new user-defined highlight, you must:
///  - Update the BillTinHighlights test in HighlightController
///  - Update the highlights/types/Common.cpp isUserDefined function
using AnyHighlight = std::variant<
    InvalidHighlight,
    YourUsernameHighlight,
    WhispersHighlight,
    AnnouncementsHighlight,
    SubscriptionsHighlight,
    ChannelPointsHighlight,
    FirstMessageHighlight,
    SubscribedThreadHighlight,
    AutomodCaughtHighlight,
    WatchStreakHighlight,
    YourMessagesHighlight,
    MessageHighlight,
    UserHighlight,
    BadgeHighlight,
    FilterHighlight,
    LowTrustUserHighlight,
    UncategorizedNotificationHighlight,
    DggSubscriptionHighlight,
    DggGiftHighlight,
    DggDonationHighlight,
    DggSpotlightHighlight,
    DggBroadcastHighlight,
    DggXPostHighlight,
    DggVestaboardHighlight
    >;
// clang-format on

}  // namespace chatterino::highlights
