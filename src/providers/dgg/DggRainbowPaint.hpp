#pragma once

#include <QFont>
#include <QPixmap>
#include <QSizeF>
#include <QString>

#include <memory>

namespace chatterino {

class Paint;

/// The paint for destiny.gg nicks with a rainbow flair: a left-to-right
/// rainbow gradient that scrolls sideways, drawn through the same code as 7TV
/// paints. It is `animated()`, so `TextLayoutElement` draws it in its
/// animated pass (repainted on the GIF timer) and leaves it out of the
/// message's cached picture.
///
/// All nicks share one clock (`std::chrono::steady_clock`), so every rainbow
/// nick shows the same colours at the same moment.
const std::shared_ptr<Paint> &dggRainbowPaint();

/// `text` drawn in the rainbow on a transparent pixmap of `size` (device
/// pixel ratio `dpr`). Unlike Paint::getPixmap(), which draws a trailing `:`
/// in the plain text colour (7TV nametags), the whole text, colon included,
/// gets the rainbow, and the gradient spans all of it.
QPixmap dggRainbowPixmap(const QString &text, const QFont &font, QSizeF size,
                         float dpr);

}  // namespace chatterino
