// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"
#include "messages/layouts/MessageLayoutContext.hpp"
#include "messages/Link.hpp"
#include "messages/Message.hpp"
#include "widgets/BaseWidget.hpp"

#include <QWidget>

#include <memory>

namespace chatterino {

class MessageLayout;

/// MessageView is a fixed-width widget that displays a single message.
/// For the message to be rendered, you must call setWidth.
///
/// It has two modes:
///
/// - `setMessage()` shows the text of the message on a single line, cut off
///   at the width (the preview of the message you reply to).
/// - `setWrappedMessage()` shows the message itself, wrapped to the width and
///   with everything a chat line has: emotes, emoji, links and names. It
///   follows the chat settings (timestamps, emote animation, ...), and
///   gets laid out again when an emote has loaded. A click on a link or name
///   is reported with `linkClicked`.
class MessageView : public BaseWidget
{
    Q_OBJECT

public:
    /// What is behind the message
    enum class Background {
        /// The background of the split input
        Input,
        /// The background of the split header (the banners under it)
        Header,
        /// Nothing: the widget behind shows through
        Transparent,
    };

    MessageView();
    ~MessageView() override;
    MessageView(const MessageView &) = delete;
    MessageView(MessageView &&) = delete;
    MessageView &operator=(const MessageView &) = delete;
    MessageView &operator=(MessageView &&) = delete;

    /// Shows the text of `message` on a single line.
    void setMessage(const MessagePtr &message);

    /// Shows `message` itself, wrapped to the width. Pass nullptr to clear it.
    void setWrappedMessage(const MessagePtr &message);

    void clearMessage();

    void setWidth(int width);

    void setBackground(Background background);

    /// The channel the message belongs to. The wrapped message hides its
    /// timestamp while the channel is live if the user turned that on.
    void setChannel(const ChannelPtr &channel);

Q_SIGNALS:
    /// A link or name of the wrapped message was clicked.
    void linkClicked(const Link &link);

    /// The wrapped message got taller or shorter (an emote loaded, a setting
    /// changed), so whoever sizes itself to this widget has to do it again.
    void heightChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void themeChangedEvent() override;
    void scaleChangedEvent(float newScale) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void createMessageLayout();
    void layoutMessage();

    /// What the layout is allowed to show
    MessageElementFlags currentFlags() const;

    void applyColors();

    /// The link under `pos`, an invalid one if there is none
    Link linkAt(const QPoint &pos) const;

    MessagePtr message_;
    std::unique_ptr<MessageLayout> messageLayout_;

    MessageColors messageColors_;
    MessagePreferences messagePreferences_;

    std::weak_ptr<Channel> channel_;

    Background background_ = Background::Input;

    /// The message is shown wrapped, with everything chat shows
    bool wrapped_ = false;

    /// The last paint had animated emotes in it
    bool hasAnimatedElements_ = false;

    /// The link the mouse was pressed on
    Link pressedLink_;

    int width_{};
};

}  // namespace chatterino
