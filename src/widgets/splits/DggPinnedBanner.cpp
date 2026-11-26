#include "widgets/splits/DggPinnedBanner.hpp"

#include "Application.hpp"
#include "common/ChatterinoSetting.hpp"
#include "messages/Link.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggChatServer.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "providers/dgg/DggMessageBuilder.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "util/IncognitoBrowser.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/helper/MessageView.hpp"
#include "widgets/splits/Split.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace chatterino {

namespace {

/// The uuid of the pin the user closed last, so it stays closed after a
/// restart.
QStringSetting &dismissedPinSetting()
{
    static QStringSetting setting("/dgg/dismissedPin", QString());
    return setting;
}

}  // namespace

DggPinnedBanner::DggPinnedBanner(QWidget *parent)
    : BaseWidget(parent)
    , pinnedByLabel_(new QLabel(this))
    , closeButton_(new LabelButton(QStringLiteral("\u2715"), this))
    , messageScrollArea_(new QScrollArea(this))
    , messageView_(new MessageView())
{
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto *outerBox = new QVBoxLayout(this);
    outerBox->setContentsMargins(0, 0, 0, 0);
    outerBox->setSpacing(0);

    auto *contentBox = new QVBoxLayout();
    contentBox->setContentsMargins(8, 6, 8, 6);
    contentBox->setSpacing(3);

    // Header row: "Pinned by <user>"  [x]
    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(4);
    headerRow->addWidget(this->pinnedByLabel_);
    headerRow->addStretch(1);
    this->closeButton_->setScaleIndependentSize(28, 28);
    this->closeButton_->setToolTip(QStringLiteral("Hide the pinned message"));
    headerRow->addWidget(this->closeButton_);
    contentBox->addLayout(headerRow);

    // Message body: drawn like a chat line, so it has the emotes
    this->messageView_->setBackground(MessageView::Background::Header);

    // The view has the size of its wrapped text, the area scrolls it
    this->messageScrollArea_->setWidgetResizable(false);
    this->messageScrollArea_->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
    this->messageScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->messageScrollArea_->setFrameShape(QFrame::NoFrame);
    this->messageScrollArea_->setFocusPolicy(Qt::NoFocus);
    this->messageScrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; } "
        "QScrollArea > QWidget > QWidget { background: transparent; }");
    this->messageScrollArea_->viewport()->setAutoFillBackground(false);
    this->messageScrollArea_->setSizePolicy(QSizePolicy::Expanding,
                                            QSizePolicy::Fixed);
    this->messageScrollArea_->setWidget(this->messageView_);
    contentBox->addWidget(this->messageScrollArea_);

    // An emote that loads, or a setting that changes, can change the height
    QObject::connect(this->messageView_, &MessageView::heightChanged, this,
                     [this] {
                         this->updateMessageHeight();
                     });
    QObject::connect(this->messageView_, &MessageView::linkClicked, this,
                     [this](const Link &link) {
                         this->openLink(link);
                     });

    outerBox->addLayout(contentBox);

    // 1px bottom border, separates the banner from the chat view below
    auto *bottomBorder = new QWidget(this);
    bottomBorder->setFixedHeight(1);
    bottomBorder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bottomBorder->setAutoFillBackground(true);
    {
        QPalette pal = bottomBorder->palette();
        pal.setColor(QPalette::Window, pal.color(QPalette::Mid));
        bottomBorder->setPalette(pal);
    }
    outerBox->addWidget(bottomBorder);

    QObject::connect(this->closeButton_, &Button::leftClicked, this, [this] {
        this->dismiss();
    });

    this->scaleChangedEvent(this->scale());
    this->hide();
}

void DggPinnedBanner::setChannel(const std::shared_ptr<DggChannel> &channel)
{
    this->connections_.clear();
    this->channel_ = channel;
    this->messageView_->setChannel(channel);

    if (channel)
    {
        this->connections_.managedConnect(channel->pinnedMessageChanged,
                                          [this] {
                                              this->refresh();
                                          });
    }

    this->refresh();
}

bool DggPinnedBanner::hasPinnedMessage() const
{
    const auto channel = this->channel_.lock();
    return channel && channel->pinnedMessage().has_value();
}

void DggPinnedBanner::refresh()
{
    const auto channel = this->channel_.lock();
    if (!channel || !channel->pinnedMessage())
    {
        this->shownId_.clear();
        this->userToggled_ = std::nullopt;
        this->hide();
        return;
    }

    const DggPin &pin = *channel->pinnedMessage();

    // Another pin: whatever the user did with the last one doesn't apply
    const QString id = pin.uuid.isEmpty() ? pin.text : pin.uuid;
    if (id != this->shownId_)
    {
        this->shownId_ = id;
        this->userToggled_ = std::nullopt;
    }

    this->pinnedByLabel_->setText(
        pin.user.nick.isEmpty() ? QStringLiteral("Pinned message")
                                : QStringLiteral("Pinned by <b>%1</b>")
                                      .arg(pin.user.nick.toHtmlEscaped()));

    // The text as a chat line, with the pin's time as a normal timestamp in
    // front: it shows (or not) and is formatted like the chat's timestamps
    if (const auto *assets = getApp()->getDggChatServer()->assets())
    {
        this->messageView_->setWrappedMessage(
            DggMessageBuilder::makePinBannerMessage(channel.get(), pin,
                                                    *assets));
    }
    this->lastAreaWidth_ = -1;
    this->updateMessageHeight();

    const bool dismissed =
        !pin.uuid.isEmpty() && dismissedPinSetting().getValue() == pin.uuid;
    if (this->userToggled_.value_or(!dismissed))
    {
        this->show();
        this->updateMessageHeightIfNeeded();
    }
    else
    {
        this->hide();
    }
}

void DggPinnedBanner::dismiss()
{
    this->userToggled_ = false;
    if (const auto channel = this->channel_.lock();
        channel && channel->pinnedMessage())
    {
        dismissedPinSetting().setValue(channel->pinnedMessage()->uuid);
    }
    this->hide();
}

void DggPinnedBanner::toggleUserPinned()
{
    if (!this->hasPinnedMessage())
    {
        return;
    }

    if (this->isVisible())
    {
        this->dismiss();
        return;
    }

    this->userToggled_ = true;
    if (const auto channel = this->channel_.lock();
        channel && channel->pinnedMessage() &&
        dismissedPinSetting().getValue() == channel->pinnedMessage()->uuid)
    {
        // Opened again: it stays open after a restart too
        dismissedPinSetting().setValue(QString());
    }
    this->show();
    this->updateMessageHeightIfNeeded();
}

void DggPinnedBanner::updateMessageHeight()
{
    // Wrap the message to the width of the area. If it is taller than the cap
    // a scrollbar takes some of that width, so it is wrapped again to what is
    // left.
    int width = std::max(1, this->messageScrollArea_->width());
    this->lastAreaWidth_ = width;
    this->messageView_->setWidth(width);

    int contentHeight = this->messageView_->height();
    if (contentHeight > this->messageMaxHeight_)
    {
        width = std::max(
            1,
            width - this->messageScrollArea_->verticalScrollBar()->sizeHint()
                        .width());
        this->messageView_->setWidth(width);
        contentHeight = this->messageView_->height();
    }

    // Size to content, but never taller than the cap.
    this->messageScrollArea_->setFixedHeight(
        qBound(1, contentHeight, this->messageMaxHeight_));
}

void DggPinnedBanner::updateMessageHeightIfNeeded()
{
    if (this->lastAreaWidth_ != this->messageScrollArea_->width())
    {
        this->updateMessageHeight();
    }
}

void DggPinnedBanner::openLink(const Link &link)
{
    switch (link.type)
    {
        case Link::Url: {
            if (getSettings()->openLinksIncognito && supportsIncognitoLinks())
            {
                openLinkIncognito(link.value);
            }
            else
            {
                QDesktopServices::openUrl(QUrl(link.value));
            }
        }
        break;

        case Link::UserInfo: {
            // The user card belongs to the split's chat view
            auto *split = dynamic_cast<Split *>(this->parentWidget());
            const auto channel = this->channel_.lock();
            if (split != nullptr && channel)
            {
                split->getChannelView().showUserInfoPopup(
                    link.value, MessagePlatform::Dgg, channel->getName());
            }
        }
        break;

        default:
            break;
    }
}

void DggPinnedBanner::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);

    // Same colour as the split header above
    painter.fillRect(event->rect(), this->theme->splits.header.background);

    // 1px top border
    painter.setPen(this->theme->splits.header.border);
    painter.drawLine(0, 0, this->width() - 1, 0);
}

void DggPinnedBanner::resizeEvent(QResizeEvent *event)
{
    BaseWidget::resizeEvent(event);
    this->updateMessageHeight();
}

void DggPinnedBanner::showEvent(QShowEvent *event)
{
    BaseWidget::showEvent(event);
    this->visibilityChanged.invoke();
}

void DggPinnedBanner::hideEvent(QHideEvent *event)
{
    BaseWidget::hideEvent(event);
    this->visibilityChanged.invoke();
}

void DggPinnedBanner::scaleChangedEvent(float newScale)
{
    QFont headerFont = this->pinnedByLabel_->font();
    headerFont.setPointSizeF(9.5F * newScale);
    this->pinnedByLabel_->setFont(headerFont);

    // The message view scales itself (the chat's fonts and emote sizes)
    this->messageMaxHeight_ = int(110 * newScale);
    this->updateMessageHeight();
}

void DggPinnedBanner::mousePressEvent(QMouseEvent * /*event*/)
{
    // ignore to disable the parent's right click menu
}

}  // namespace chatterino
