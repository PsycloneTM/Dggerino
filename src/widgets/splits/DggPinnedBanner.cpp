#include "widgets/splits/DggPinnedBanner.hpp"

#include "common/ChatterinoSetting.hpp"
#include "common/LinkParser.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "widgets/buttons/LabelButton.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

namespace chatterino {

namespace {

constexpr auto MUTED_STYLE = "color: #adadb8;";

/// The uuid of the pin the user closed last, so it stays closed after a
/// restart.
QStringSetting &dismissedPinSetting()
{
    static QStringSetting setting("/dgg/dismissedPin", QString());
    return setting;
}

/// The pinned text as the label shows it: escaped, with the links clickable.
QString pinnedRichText(const QString &text, const QColor &linkColor)
{
    QStringList out;
    const QStringList words =
        text.simplified().split(QChar(u' '), Qt::SkipEmptyParts);
    for (const auto &word : words)
    {
        const auto link = linkparser::parse(word);
        if (!link)
        {
            out << word.toHtmlEscaped();
            continue;
        }

        QString url = link->link.toString();
        if (link->protocol.isEmpty())
        {
            url = QStringLiteral("https://") + url;
        }
        out << QStringLiteral("%1<a href=\"%2\" style=\"color: %3\">%4</a>%5")
                   .arg(link->prefix(word).toString().toHtmlEscaped(),
                        url.toHtmlEscaped(), linkColor.name(),
                        link->link.toString().toHtmlEscaped(),
                        link->suffix(word).toString().toHtmlEscaped());
    }
    return out.join(QChar(u' '));
}

}  // namespace

DggPinnedBanner::DggPinnedBanner(QWidget *parent)
    : BaseWidget(parent)
    , pinnedByLabel_(new QLabel(this))
    , closeButton_(new LabelButton(QStringLiteral("\u2715"), this))
    , messageScrollArea_(new QScrollArea(this))
    , messageLabel_(new QLabel(this))
    , footerLabel_(new QLabel(this))
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

    // Message body
    this->messageLabel_->setWordWrap(true);
    this->messageLabel_->setTextFormat(Qt::RichText);
    this->messageLabel_->setOpenExternalLinks(true);
    this->messageLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    this->messageLabel_->setStyleSheet("background: transparent;");
    this->messageLabel_->setSizePolicy(QSizePolicy::Expanding,
                                       QSizePolicy::Preferred);
    this->messageLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                 Qt::LinksAccessibleByMouse);

    this->messageScrollArea_->setWidgetResizable(true);
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
    this->messageScrollArea_->setWidget(this->messageLabel_);
    contentBox->addWidget(this->messageScrollArea_);

    // Footer: when it was pinned
    auto *footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 2, 0, 0);
    footerRow->setSpacing(4);
    this->footerLabel_->setStyleSheet(MUTED_STYLE);
    footerRow->addWidget(this->footerLabel_);
    footerRow->addStretch(1);
    contentBox->addLayout(footerRow);

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
    this->messageLabel_->setText(
        pinnedRichText(pin.text, this->theme->messages.textColors.link));
    this->updateMessageHeight();
    this->footerLabel_->setText(
        QStringLiteral("Pinned %1")
            .arg(pin.time().toString(getSettings()->timestampFormat)));

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
    // Wrapped height of the label at the current viewport width.
    this->lastViewportWidth_ = this->messageScrollArea_->viewport()->width();
    int contentHeight =
        this->messageLabel_->heightForWidth(this->lastViewportWidth_);
    if (contentHeight <= 0)
    {
        contentHeight = this->messageLabel_->sizeHint().height();
    }

    // Size to content, but never taller than the cap.
    this->messageScrollArea_->setFixedHeight(
        qBound(1, contentHeight, this->messageMaxHeight_));
}

void DggPinnedBanner::updateMessageHeightIfNeeded()
{
    if (this->lastViewportWidth_ !=
        this->messageScrollArea_->viewport()->width())
    {
        this->updateMessageHeight();
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

    QFont bodyFont = this->messageLabel_->font();
    bodyFont.setPointSizeF(11.0F * newScale);
    this->messageLabel_->setFont(bodyFont);
    this->messageMaxHeight_ = int(110 * newScale);
    this->updateMessageHeight();

    QFont footerFont = this->footerLabel_->font();
    footerFont.setPointSizeF(9.0F * newScale);
    this->footerLabel_->setFont(footerFont);
}

void DggPinnedBanner::mousePressEvent(QMouseEvent * /*event*/)
{
    // ignore to disable the parent's right click menu
}

}  // namespace chatterino
