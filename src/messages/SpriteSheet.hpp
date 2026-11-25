#pragma once

#include <QImage>
#include <QMargins>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace chatterino {

/// One function of a CSS `filter` chain. Only the functions that are plain
/// colour transforms are implemented (the ones of the Filter Effects spec
/// that don't need neighbouring pixels): `drop-shadow()` and `blur()` have no
/// equivalent here and are left out by whoever builds the chain.
struct SpriteColorFilter {
    enum class Kind {
        Brightness,
        Contrast,
        Saturate,
        HueRotate,
        Grayscale,
        Sepia,
        Invert,
        Opacity,
        /// `drop-shadow(x y blur color)`: needs the neighbouring pixels and
        /// room around the frame (see SpriteSheetSpec::glowMargin). Appended
        /// after the colour functions: anim::FilterKind has the same order.
        DropShadow,
    };

    Kind kind = Kind::Brightness;

    /// The CSS argument: a factor (1 = unchanged for brightness, contrast,
    /// saturate and opacity; 0 = unchanged for grayscale, sepia and invert)
    /// or, for hue-rotate, an angle in degrees.
    double amount = 1.0;

    /// Only for `DropShadow`. x/y/blur in px; the colour is r, g, b in 0..255
    /// and alpha in 0..1. `blur` is a "box-shadow" blur: the gaussian's
    /// standard deviation is half of it.
    double shadowX = 0;
    double shadowY = 0;
    double shadowBlur = 0;
    std::array<double, 4> shadowRgba{0, 0, 0, 1};

    bool operator==(const SpriteColorFilter &) const = default;
};

/// How one frame is drawn on top of what the sheet shows: what a CSS
/// animation of `transform`, `opacity`, `filter` and `clip-path` makes of the
/// emote at one moment (see providers/dgg/DggCssAnimation.hpp, which "bakes"
/// such animations into a list of these).
///
/// Applied in the order of the CSS rendering model: filters, clip, opacity,
/// transform. The frame is not made bigger, so what a transform moves out of
/// the frame's box is cut off (chat-gui lets it overflow into its
/// neighbours).
struct SpritePose {
    /// CSS `matrix(a, b, c, d, e, f)` in frame pixels, with the transform
    /// origin already folded in: x' = a x + c y + e, y' = b x + d y + f.
    std::array<double, 6> matrix{1, 0, 0, 1, 0, 0};

    double opacity = 1.0;

    /// The colour filters of this moment (the element's own plus whatever
    /// the animation does to them).
    std::vector<SpriteColorFilter> filters;

    /// `clip-path: polygon()` in frame pixels. Empty if there is none.
    std::vector<std::pair<double, double>> clip;

    /// Nothing but (maybe) filters: no transform, opacity or clip.
    bool isIdentity() const;

    bool operator==(const SpritePose &) const = default;
};

/// One `box-shadow` of an overlay.
struct SpriteShadow {
    bool inset = false;
    double x = 0;
    double y = 0;
    double blur = 0;
    double spread = 0;
    /// r, g, b in 0..255, alpha in 0..1
    std::array<double, 4> rgba{0, 0, 0, 1};

    bool operator==(const SpriteShadow &) const = default;
};

/// What the CSS `::before` or `::after` of an emote looks like at one moment:
/// an absolutely positioned box with some text in it (an emoji, a line of
/// green digits) and maybe shadows, drawn on top of the emote and cut off at
/// the edge of the emote's box (chat-gui's `.emote` has `overflow: hidden`).
/// All lengths are in px and already resolved against the emote's box, see
/// providers/dgg/DggEmoteCss.cpp. Drawn by `sliceSpriteSheet()`.
struct SpriteOverlay {
    /// Empty if the box only has shadows.
    QString text;
    double fontPx = 16;
    /// A CSS `font-family` list; empty for the default font.
    QString fontFamily;
    /// r, g, b in 0..255, alpha in 0..1
    std::array<double, 4> color{255, 255, 255, 1};
    double letterSpacingPx = 0;
    /// 0 is `normal`
    double lineHeightPx = 0;

    /// Distances from the edges of the emote's box; the missing ones are
    /// `auto`.
    std::optional<double> left;
    std::optional<double> top;
    std::optional<double> right;
    std::optional<double> bottom;
    /// The size of the box; text without a width is as wide as it is long.
    std::optional<double> width;
    std::optional<double> height;

    /// CSS `matrix(a, b, c, d, e, f)` of the box's transform, *without* the
    /// transform origin.
    std::array<double, 6> matrix{1, 0, 0, 1, 0, 0};
    double originX = 50;
    bool originXPercent = true;
    double originY = 50;
    bool originYPercent = true;

    double opacity = 1;
    std::vector<SpriteShadow> shadows;
    /// The box's CSS `filter` (without drop-shadow()), run over the box and
    /// its shadows before they are transformed.
    std::vector<SpriteColorFilter> filters;

    bool operator==(const SpriteOverlay &) const = default;
};

/// One CSS background layer of an emote's box: the emote itself, or a
/// `::before` / `::after` that has `background: inherit` (ARRIVE's combo
/// lines). The same image is drawn into `box` at `imageSize`, `imagePos`
/// from the box's top-left, repeated if asked, coloured by `filters`, and cut
/// off at the box (and at the emote's edge).
struct SpriteImageLayer {
    /// In the emote's box; may reach outside it (the part outside is cut off)
    QRectF box;
    /// `background-size`. Empty: the image at its natural size.
    QSizeF imageSize;
    /// `background-position`, from the box's top-left
    QPointF imagePos;
    bool tileX = false;
    bool tileY = false;
    double opacity = 1;
    std::vector<SpriteColorFilter> filters;

    bool operator==(const SpriteImageLayer &) const = default;
};

/// `mask-image: radial-gradient(black A%, transparent B%)` on the emote: the
/// default ellipse that reaches the farthest corner, opaque up to `innerPct`
/// and clear from `outerPct` of its radius.
struct SpriteRadialMask {
    double innerPct = 0;
    double outerPct = 100;

    bool operator==(const SpriteRadialMask &) const = default;
};

/// Describes which parts of a (sprite) sheet are shown, in which order and
/// for how long. This is what a CSS rule like
///
///     width: 56px; background-position: 0; animation: x 3.5s steps(99)
///
/// boils down to once the stylesheet has been evaluated.
struct SpriteSheetSpec {
    /// The size of one frame in image pixels.
    QSize frameSize;

    /// The top-left corner of each frame in the sheet, in play order. A frame
    /// may reach outside of the sheet; the part that does is transparent
    /// (that is what a CSS background does, too).
    std::vector<QPoint> offsets;

    /// How long each frame is shown, in milliseconds. One entry per entry of
    /// `offsets`. Unused if there is only a single frame.
    std::vector<int> durationsMs;

    /// The sheet repeats endlessly in this direction (CSS `background-repeat`,
    /// which is `repeat` unless the stylesheet says otherwise). A frame that
    /// reaches beyond the sheet then shows the other end of it instead of
    /// being transparent. CuckCrab starts its animation 22 frames "before"
    /// the sheet and SCHEMING ends one sheet width past it; both rely on this.
    bool tileX = false;
    bool tileY = false;

    /// Applied to every frame, in order.
    std::vector<SpriteColorFilter> filters;

    /// How often the frames are played. 0 means forever. For a number above
    /// 0 the animation plays that many times and then stays on one frame
    /// (`restOffset`): the first frame, or the last one if the CSS has
    /// `animation-fill-mode: forwards`.
    int loopCount = 0;

    /// Where the frame the animation ends on is. Only used if `loopCount`
    /// is above 0.
    QPoint restOffset;

    /// Optional, for emotes whose CSS animates `transform`, `opacity`, `filter`
    /// or `clip-path`: one pose per entry of `offsets` (so, as many as the
    /// frames), drawn on top of that frame. Empty for the ordinary case. If
    /// set, it replaces `filters` (the poses carry the element's own filters
    /// as well).
    std::vector<SpritePose> poses;

    /// What the animation rests on after `loopCount` plays, like
    /// `restOffset` for the sheet. Only used if `poses` is set.
    SpritePose restPose;

    /// Things drawn on top of each frame (the emote's `::before` and
    /// `::after`): one list for every entry of `offsets`, or none at all.
    /// Together with `offsets` the frames are the same animation as
    /// `poses`, only with more to draw.
    std::vector<std::vector<SpriteOverlay>> overlays;

    /// What is drawn on top of the frame the animation rests on. Only used
    /// if `overlays` is set and the animation `playsOnce()`.
    std::vector<SpriteOverlay> restOverlays;

    /// Room on every side of the frame for a `drop-shadow()` glow (0 if the
    /// emote has none). The frames are this much bigger on each side; the
    /// layout gives the emote its own size and lets the glow reach out
    /// (canvasMargins()). Only used if `canvasSize` is empty.
    int glowMargin = 0;

    /// A canvas that is bigger than the emote. chat-gui widens the box of an
    /// emote while its `::before` flies around (NoTears), and that box is
    /// as far as the overlay reaches. Empty: the canvas is the frame.
    QSize canvasSize;

    /// Where the (posed) frame sits on the canvas.
    QPoint canvasImagePos;

    /// The emote is mirrored as a whole (`transform: scale(-1, 1)`): the
    /// overlays are laid out unmirrored and then flipped over the canvas.
    bool canvasMirror = false;

    /// The emote's box is drawn from several copies of the image (see
    /// SpriteImageLayer), the first being the emote's own background. Each
    /// frame of the (possibly animated) image is drawn into all of them. Not
    /// combined with offsets, poses or overlays.
    std::vector<SpriteImageLayer> imageLayers;
    std::optional<SpriteRadialMask> mask;

    bool hasImageLayers() const
    {
        return !this->imageLayers.empty();
    }

    /// Whether anything is drawn on top of the frames. A frame whose overlay
    /// list is empty (the banana of a still NoTears parks outside of the box
    /// and is dropped) draws nothing, so it doesn't count.
    bool hasOverlays() const
    {
        return !this->restOverlays.empty() ||
               std::any_of(this->overlays.begin(), this->overlays.end(),
                           [](const auto &frame) {
                               return !frame.empty();
                           });
    }

    /// How far the canvas reaches beyond the frame on each side (all zero
    /// if there is no canvas). The layout only gives the frame room.
    QMargins canvasMargins() const;

    /// Only used to keep specs apart that are otherwise equal. An animation
    /// that plays once starts when its image is loaded, so every message
    /// that should start it again needs an image of its own.
    quint64 instanceId = 0;

    /// Plays a limited number of times and then stops.
    bool playsOnce() const
    {
        return this->loopCount > 0 && this->offsets.size() > 1;
    }

    bool hasPoses() const
    {
        return !this->poses.empty();
    }

    bool isValid() const;
    bool animated() const
    {
        return this->offsets.size() > 1;
    }

    /// A short string that is equal for equal specs, and only for those. Used
    /// to share one `Image` between everything that wants the same frames.
    QString cacheKey() const;

    bool operator==(const SpriteSheetSpec &) const = default;
};

struct SpriteFrame {
    QImage image;
    int durationMs = 0;
};

/// Cuts the frames of `spec` out of `sheet` and applies its filters.
///
/// Returns nothing if the spec is invalid or the sheet is null. If all the
/// frames together would need more than `maxBytes` of memory, only the first
/// frame is returned (a static image is better than none).
///
/// For a spec that `playsOnce()`, one more frame is returned after the frames
/// of `offsets`: the one the animation rests on (duration 0). It is missing if
/// only the first frame was returned because of `maxBytes`.
///
/// Frames are returned as `QImage::Format_ARGB32`. Every frame takes at least
/// 20 ms: that is the resolution of the animation clock, and anything faster
/// would just be dropped frames.
std::vector<SpriteFrame> sliceSpriteSheet(const QImage &sheet,
                                          const SpriteSheetSpec &spec,
                                          qint64 maxBytes);

/// Draws `pose` on `frame`: filters, clip, opacity, transform. The result
/// has the size of `frame` and is ARGB32.
///
/// `pad`: `frame` has this many transparent pixels on every side (the glow
/// margin); the pose is still laid out against the unpadded frame.
QImage applySpritePose(const QImage &frame, const SpritePose &pose,
                       int pad = 0);

/// `frame` on a transparent image that is `margin` pixels bigger on every
/// side (ARGB32). Returns the frame as ARGB32 if `margin` is 0.
QImage padSpriteFrame(const QImage &frame, int margin);

/// How far the `drop-shadow()` functions of these filters reach out of the
/// frame, in px (0 if there are none).
int dropShadowReach(const std::vector<SpriteColorFilter> &filters);

/// Draws `image` (one frame of the emote's image) into every layer of
/// `spec.imageLayers`, in order, and applies `spec.mask`. The result is
/// `spec.frameSize` big.
QImage composeImageLayers(const QImage &image, const SpriteSheetSpec &spec);

/// Applies a filter chain to an image (any format; the result is ARGB32).
QImage applySpriteFilters(const QImage &image,
                          const std::vector<SpriteColorFilter> &filters);

}  // namespace chatterino
