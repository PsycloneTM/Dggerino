#pragma once

#include <QString>
#include <QtGlobal>
#include <QStringView>

namespace chatterino {

/// Emote combos, like chat-gui's `ChatEmoteMessage`: while consecutive chat
/// lines are the same single emote, they collapse into one line that counts
/// them ("5 X HITS"), and when something else arrives the line is completed
/// ("5 X C-C-C-COMBO").
///
/// This file only holds the rules that don't need Chatterino: how the text of
/// a line is compared and which size step a count falls into.

/// chat-gui's `removeSlashCommand()`: drops a leading `/command ` (`/me `,
/// but any word after a slash counts) and trims. Two lines combine if this is
/// the same for both, so `Pog` and `/me Pog` are one combo.
QString dggStripSlashCommand(QStringView text);

/// The step class chat-gui gives a combo of `count` lines (the `x5` in
/// `data-combo-group="x5"` and `class="chat-combo x5"`): `x2` for 2 to 4,
/// then `x5`, `x10`, `x20`, `x30` and `x50`.
QString dggComboGroup(int count);

/// The size step as a number: 2, 5, 10, 20, 30 or 50 (the number in
/// dggComboGroup()).
int dggComboStep(int count);

/// How far the blood splatter behind a completed x10+ combo has faded to
/// grey, 0 to 100 (percent), `elapsedMs` after the combo was completed.
/// chat-gui's `emote-greyout` runs 3500 ms: the colour stays for the first
/// 75 % of it, then goes linearly to `grayscale(100%)`, where it stays.
int dggComboGreyPercent(qint64 elapsedMs);

}  // namespace chatterino
