#pragma once

#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signal.hpp>

#include <memory>
#include <optional>

class QLabel;
class QScrollArea;

namespace chatterino {

class DggChannel;
class LabelButton;

/// The banner between the split header and the chat that shows the message
/// pinned in destiny.gg chat. It is the DGG counterpart of
/// PinnedMessageWidget (which belongs to Twitch channels): the same place and
/// look, but there is no expiry and no moderator menu, and it is hidden with
/// its own close button or the pin button of the split header.
///
/// The text is plain (emote names are shown as words, links can be clicked):
/// a Qt label can't draw chat emotes. The line "Nick pinned a message: ..."
/// that appears in the chat when a message is pinned has the emotes.
class DggPinnedBanner final : public BaseWidget
{
    Q_OBJECT

public:
    explicit DggPinnedBanner(QWidget *parent = nullptr);

    /// Pass nullptr to detach from any channel.
    void setChannel(const std::shared_ptr<DggChannel> &channel);

    /// Called by the header pin button: hides the banner if it is shown, shows
    /// it if it is hidden.
    void toggleUserPinned();

    /// There is a pinned message (whether the banner shows it or not).
    bool hasPinnedMessage() const;

    /// Emitted whenever this widget becomes shown or hidden.
    pajlada::Signals::NoArgSignal visibilityChanged;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scaleChangedEvent(float newScale) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void paintEvent(QPaintEvent *event) override;

    /// Shows the channel's pinned message, or hides the banner if there is
    /// none (or the user closed this one).
    void refresh();

    /// Closes the banner for the current pin; it stays closed after a restart
    /// (like on the site) until another message is pinned.
    void dismiss();

    /// Sizes the message area to its wrapped text, up to a cap. A scrollbar
    /// appears past the cap.
    void updateMessageHeight();
    void updateMessageHeightIfNeeded();

    std::weak_ptr<DggChannel> channel_;
    pajlada::Signals::SignalHolder connections_;

    QLabel *pinnedByLabel_ = nullptr;
    LabelButton *closeButton_ = nullptr;
    QScrollArea *messageScrollArea_ = nullptr;
    QLabel *messageLabel_ = nullptr;
    QLabel *footerLabel_ = nullptr;

    /// The pin that is shown (its uuid, or its text without one).
    QString shownId_;

    /// - `std::nullopt`: the user didn't toggle the banner for this pin
    /// - `false`: the user closed it
    /// - `true`: the user opened it again
    std::optional<bool> userToggled_ = std::nullopt;

    /// Scaled cap for the message area.
    int messageMaxHeight_ = 110;
    int lastViewportWidth_ = -1;
};

}  // namespace chatterino
