#include "providers/dgg/DggRainbowPaint.hpp"

#include "providers/seventv/paints/Paint.hpp"

#include <QBrush>
#include <QColor>
#include <QGradient>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QTextOption>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace chatterino {

using namespace Qt::StringLiterals;

namespace {

/// `animation: move 3s linear infinite` of `.user.flair42`; `move` ends at
/// `background-position-x: -100%`. With a background twice as wide as the nick
/// that moves the background right by one nick width, which is exactly one
/// period of the gradient.
constexpr std::int64_t CYCLE_MS = 3000;

/// The site's `--rainbow-saturation` and `--rainbow-lightness`
constexpr double SATURATION = 1.0;
constexpr double LIGHTNESS = 0.65;

class DggAnimatedRainbowPaint : public Paint
{
public:
    DggAnimatedRainbowPaint()
        : Paint(QStringLiteral("dgg-rainbow"))
    {
    }

    /// The site's rule: `background: var(--rainbow-gradient)` with
    /// `background-size: 200% 100%`, where the gradient is
    /// `repeating-linear-gradient(90deg, hsl(0), hsl(45), ... hsl(315),
    /// hsl(360) 50%)`. The last stop sits at 50% of a line that is twice as
    /// wide as the nick, so the colours go once around the hue wheel per nick
    /// width, and the animation slides that background right by one period per cycle.
    QBrush asBrush(QColor /*userColor*/, QRectF drawingRect) const override
    {
        using namespace std::chrono;
        const auto now =
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
                .count();
        const double phase =
            static_cast<double>(now % CYCLE_MS) / static_cast<double>(CYCLE_MS);

        const double period = std::max(1.0, drawingRect.width());
        const double startX = drawingRect.left() + (phase * period);

        QLinearGradient gradient(QPointF(startX, 0),
                                 QPointF(startX + period, 0));
        gradient.setSpread(QGradient::RepeatSpread);
        for (int i = 0; i <= 8; i++)
        {
            // hsl(0), hsl(45), ..., hsl(360): the last is the first again
            gradient.setColorAt(i / 8.0,
                                QColor::fromHslF(std::min(i * 45, 359) / 360.0F,
                                                 SATURATION, LIGHTNESS));
        }
        gradient.setColorAt(1.0, QColor::fromHslF(0.0F, SATURATION, LIGHTNESS));
        return QBrush(gradient);
    }

    const std::vector<PaintDropShadow> &getDropShadows() const override
    {
        return this->noShadows_;
    }

    bool animated() const override
    {
        return true;
    }

private:
    std::vector<PaintDropShadow> noShadows_;
};

}  // namespace

const std::shared_ptr<Paint> &dggRainbowPaint()
{
    static const std::shared_ptr<Paint> PAINT =
        std::make_shared<DggAnimatedRainbowPaint>();
    return PAINT;
}

namespace {

/// The shape of a nick's text, drawn once: white on transparent. The text
/// doesn't change while the rainbow scrolls, so only the colour is redone on
/// every tick.
struct RainbowMask {
    QPixmap pixmap;
    QRectF period;
};

RainbowMask makeRainbowMask(const QString &text, const QFont &font, QSizeF size,
                            float dpr)
{
    RainbowMask mask;
    mask.pixmap = QPixmap((size * dpr).toSize());
    mask.pixmap.setDevicePixelRatio(dpr);
    mask.pixmap.fill(Qt::transparent);

    QPainter painter(&mask.pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // The text goes into a transparent picture that is drawn over the line
    // later. Sub-pixel (LCD) anti-aliasing has no background to blend with
    // there, so it leaves dark and coloured fringes around the letters. Plain
    // grayscale anti-aliasing doesn't.
    QFont drawFont(font);
    drawFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(
        QFont::PreferAntialias | QFont::NoSubpixelAntialias));
    painter.setFont(drawFont);

    const QTextOption option(Qt::AlignLeft | Qt::AlignTop);
    const QRectF bounds =
        painter.boundingRect(QRectF(0, 0, 10000, 10000), text, option);

    // One rainbow period is the width of the nick itself. The colon of a chat
    // line ("nick:") must not stretch it, or the same nick would have wider
    // colours there than in an event line; the colon just continues the
    // pattern.
    mask.period = bounds;
    if (text.size() > 1 && text.endsWith(QChar(u':')))
    {
        mask.period.setWidth(painter
                                 .boundingRect(QRectF(0, 0, 10000, 10000),
                                               text.left(text.size() - 1),
                                               option)
                                 .width());
    }

    painter.setPen(Qt::white);
    painter.drawText(bounds, text, option);
    painter.end();

    return mask;
}

}  // namespace

QPixmap dggRainbowPixmap(const QString &text, const QFont &font, QSizeF size,
                         float dpr)
{
    // Called for every rainbow nick on screen on every repaint, which is
    // every scroll step and every tick of the GIF timer, so the text itself
    // is only drawn the first time.
    // (never destroyed: pixmaps must not outlive the GUI application)
    static auto &cache = *new QHash<QString, RainbowMask>;

    const auto key = u"%1|%2|%3x%4|%5"_s.arg(
        font.key(), text, QString::number(size.width(), 'f', 2),
        QString::number(size.height(), 'f', 2), QString::number(dpr, 'f', 2));
    auto it = cache.find(key);
    if (it == cache.end())
    {
        if (cache.size() > 512)
        {
            cache.clear();
        }
        it = cache.insert(key, makeRainbowMask(text, font, size, dpr));
    }
    const auto &mask = it.value();

    QPixmap pixmap(mask.pixmap.size());
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.fillRect(
        QRectF(QPointF(), QSizeF(pixmap.size()) / pixmap.devicePixelRatio()),
        dggRainbowPaint()->asBrush(QColor(), mask.period));
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawPixmap(QPointF(), mask.pixmap);
    painter.end();

    return pixmap;
}

}  // namespace chatterino
