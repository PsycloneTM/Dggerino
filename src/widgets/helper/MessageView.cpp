// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/MessageView.hpp"

#include "Application.hpp"
#include "messages/layouts/MessageLayout.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"
#include "messages/MessageElement.hpp"
#include "messages/Selection.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>

#include <utility>

namespace {

using namespace chatterino;

const Selection EMPTY_SELECTION;

const MessageElementFlags MESSAGE_FLAGS{
    MessageElementFlag::Text,
    MessageElementFlag::EmojiAll,
    MessageElementFlag::EmoteText,
};

}  // namespace

namespace chatterino {

MessageView::MessageView()
{
    // The wrapped message follows the chat views: laid out again when an
    // emote loaded or a setting changed, drawn again for animated emotes
    auto *windows = getApp()->getWindows();
    this->signalHolder_.managedConnect(windows->layoutRequested,
                                       [this](Channel * /*channel*/) {
                                           if (this->wrapped_)
                                           {
                                               this->layoutMessage();
                                           }
                                       });
    this->signalHolder_.managedConnect(windows->wordFlagsChanged, [this] {
        if (this->wrapped_)
        {
            this->layoutMessage();
        }
    });
    this->signalHolder_.managedConnect(windows->gifRepaintRequested, [this] {
        if (this->wrapped_ && this->hasAnimatedElements_)
        {
            this->update();
        }
    });
}

MessageView::~MessageView() = default;

MessageElementFlags MessageView::currentFlags() const
{
    if (!this->wrapped_)
    {
        return MESSAGE_FLAGS;
    }

    // What the chat views show
    auto flags = getApp()->getWindows()->getWordFlags();

    if (getSettings()->hideMessageTimestampsWhenLive)
    {
        if (const auto channel = this->channel_.lock();
            channel && channel->isLive())
        {
            flags.unset(MessageElementFlag::Timestamp);
        }
    }
    return flags;
}

void MessageView::createMessageLayout()
{
    if (this->message_ == nullptr)
    {
        this->messageLayout_.reset();
        return;
    }

    this->messageLayout_ = std::make_unique<MessageLayout>(this->message_);
}

void MessageView::setMessage(const MessagePtr &message)
{
    if (!message)
    {
        return;
    }

    this->wrapped_ = false;
    this->setMouseTracking(false);
    this->unsetCursor();

    auto singleLineMessage = std::make_shared<Message>();
    singleLineMessage->elements.emplace_back(
        std::make_unique<SingleLineTextElement>(
            message->messageText, MESSAGE_FLAGS, MessageColor::Type::System,
            FontStyle::ChatMediumSmall));
    this->message_ = std::move(singleLineMessage);
    this->createMessageLayout();
    this->layoutMessage();
}

void MessageView::setWrappedMessage(const MessagePtr &message)
{
    this->wrapped_ = true;
    this->hasAnimatedElements_ = false;
    this->pressedLink_ = Link();
    this->setMouseTracking(true);

    this->message_ = message;
    this->createMessageLayout();
    this->applyColors();
    this->layoutMessage();
}

void MessageView::clearMessage()
{
    this->setMessage(nullptr);
}

void MessageView::setWidth(int width)
{
    if (this->width_ != width)
    {
        this->width_ = width;
        this->layoutMessage();
    }
}

void MessageView::setBackground(Background background)
{
    this->background_ = background;
    this->applyColors();
    if (this->messageLayout_)
    {
        this->messageLayout_->invalidateBuffer();
    }
    this->update();
}

void MessageView::setChannel(const ChannelPtr &channel)
{
    this->channel_ = channel;
    if (this->wrapped_)
    {
        this->layoutMessage();
    }
}

void MessageView::applyColors()
{
    auto *theme = getTheme();
    this->messageColors_.applyTheme(theme, false, 255);

    switch (this->background_)
    {
        case Background::Input:
            this->messageColors_.regularBg = theme->splits.input.background;
            break;

        case Background::Header:
            this->messageColors_.regularBg = theme->splits.header.background;
            // The text sits on the header, not on the chat background
            this->messageColors_.regularText = theme->splits.header.text;
            this->messageColors_.systemText = theme->splits.header.text;
            break;

        case Background::Transparent:
            this->messageColors_.regularBg = Qt::transparent;
            this->messageColors_.regularText = theme->splits.header.text;
            this->messageColors_.systemText = theme->splits.header.text;
            break;
    }

    this->messageColors_.hasTransparency =
        this->messageColors_.regularBg.alpha() != 255;
}

void MessageView::paintEvent(QPaintEvent * /*event*/)
{
    if (this->messageLayout_ == nullptr)
    {
        return;
    }

    QPainter painter(this);

    auto ctx = MessagePaintContext{
        .painter = painter,
        .selection = EMPTY_SELECTION,
        .colorProvider = ColorProvider::instance(),
        .messageColors = this->messageColors_,
        .preferences = this->messagePreferences_,

        .canvasWidth = this->width_,
        .isWindowFocused = this->window() == QApplication::activeWindow(),
        .isMentions = false,

        .y = 0,
        .messageIndex = 0,
        .isLastReadMessage = false,
    };

    const auto result = this->messageLayout_->paint(ctx);
    this->hasAnimatedElements_ = result.hasAnimatedElements;
}

void MessageView::themeChangedEvent()
{
    this->applyColors();
    if (this->messageLayout_)
    {
        this->messageLayout_->invalidateBuffer();
    }
}

void MessageView::scaleChangedEvent(float newScale)
{
    (void)newScale;

    this->layoutMessage();
}

Link MessageView::linkAt(const QPoint &pos) const
{
    if (!this->wrapped_ || this->messageLayout_ == nullptr)
    {
        return {};
    }

    const auto *element = this->messageLayout_->getElementAt(QPointF(pos));
    if (element == nullptr)
    {
        return {};
    }

    // Only what chat-gui's lines have: names and web links
    const Link link = element->getLink();
    if (link.type == Link::Url || link.type == Link::UserInfo)
    {
        return link;
    }
    return {};
}

void MessageView::mousePressEvent(QMouseEvent *event)
{
    if (!this->wrapped_ || event->button() != Qt::LeftButton)
    {
        BaseWidget::mousePressEvent(event);
        return;
    }

    this->pressedLink_ = this->linkAt(event->position().toPoint());
    if (!this->pressedLink_.isValid())
    {
        // Not ours: the banner's own handling (it ignores it)
        BaseWidget::mousePressEvent(event);
    }
}

void MessageView::mouseMoveEvent(QMouseEvent *event)
{
    if (!this->wrapped_)
    {
        BaseWidget::mouseMoveEvent(event);
        return;
    }

    if (this->linkAt(event->position().toPoint()).isValid())
    {
        this->setCursor(Qt::PointingHandCursor);
    }
    else
    {
        this->unsetCursor();
    }
}

void MessageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!this->wrapped_ || event->button() != Qt::LeftButton)
    {
        BaseWidget::mouseReleaseEvent(event);
        return;
    }

    // A click: pressed and released on the same link
    const Link pressed = std::exchange(this->pressedLink_, Link());
    if (!pressed.isValid())
    {
        BaseWidget::mouseReleaseEvent(event);
        return;
    }

    const Link released = this->linkAt(event->position().toPoint());
    if (released.isValid() && released.type == pressed.type &&
        released.value == pressed.value)
    {
        this->linkClicked(pressed);
    }
}

void MessageView::leaveEvent(QEvent *event)
{
    this->unsetCursor();
    BaseWidget::leaveEvent(event);
}

void MessageView::layoutMessage()
{
    if (this->messageLayout_ == nullptr ||
        (this->wrapped_ && this->width_ <= 0))
    {
        // Nothing to lay out, or no width to wrap to yet
        return;
    }

    const int heightBefore = this->messageLayout_->getHeight();

    bool updateRequired = this->messageLayout_->layout(
        {
            .messageColors = this->messageColors_,
            .flags = this->currentFlags(),
            .width = this->width_,
            .scale = this->scale(),
            .imageScale =
                this->scale() * static_cast<float>(this->devicePixelRatio()),
            .selectedChannel = nullptr,
            .message = *this->message_,
        },
        false);

    if (updateRequired)
    {
        const int height = this->messageLayout_->getHeight();
        this->setFixedSize(this->width_, height);
        this->update();
        if (this->wrapped_ && height != heightBefore)
        {
            this->heightChanged();
        }
    }
}

}  // namespace chatterino
