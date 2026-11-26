#include "widgets/splits/DggPollBanner.hpp"

#include "Application.hpp"
#include "messages/Link.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggChatServer.hpp"
#include "providers/dgg/DggMessageBuilder.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "util/IncognitoBrowser.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/helper/MessageView.hpp"
#include "widgets/splits/Split.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace std::chrono_literals;

namespace chatterino {

namespace {

constexpr auto MUTED_STYLE = "color: #adadb8;";

/// How long an ended poll stays on screen (chat-gui's `POLL_END_TIME`).
constexpr auto END_VISIBLE = 7s;

/// How long a bar takes to slide to its new width.
constexpr int BAR_SLIDE_MS = 220;

/// All the votes: what the shares are a share of.
qint64 allVotes(const DggPoll &poll)
{
    qint64 sum = 0;
    for (const auto total : poll.totals)
    {
        sum += total;
    }
    return std::max(poll.votesCast, sum);
}

/// The options of a poll, one row each: a bar as wide as the share of the
/// votes, the number and text of the option on the left and the share on the
/// right. The text of an option is a MessageView (emotes, emoji, links) that
/// sits on top of the row; it wraps if it doesn't fit on a line and its row
/// grows. The views let mouse clicks through, so a click on a row is a click on
/// the bar. The option the user voted for has an outline; after the poll the
/// winner (or each option of a tie) is drawn stronger.
class PollOptionsView final : public QWidget
{
public:
    explicit PollOptionsView(QWidget *parent)
        : QWidget(parent)
        , animation_(new QVariantAnimation(this))
    {
        // The bars slide to their new width instead of jumping
        this->animation_->setStartValue(0.0);
        this->animation_->setEndValue(1.0);
        this->animation_->setDuration(BAR_SLIDE_MS);
        this->animation_->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(this->animation_, &QVariantAnimation::valueChanged,
                         this, [this] {
                             this->update();
                         });
    }

    /// Called when the rows got taller or shorter because the width changed
    /// (the rows wrap to it).
    std::function<void()> heightChanged;

    /// Called with the 1-based number of the option that was clicked
    std::function<void(int)> optionClicked;

    /// Makes the message that shows the text of an option (its number in
    /// front). Returns nullptr if there is no way to make one yet.
    std::function<MessagePtr(const QString &)> makeMessage;

    void setPoll(const DggPoll &poll, const QColor &text, const QColor &accent,
                 float scale)
    {
        // Where the bars are right now (halfway through a slide, say)
        const auto before = this->shownFractions();
        const bool samePoll = !this->poll_.id.isEmpty() &&
                              this->poll_.id == poll.id &&
                              this->poll_.options.size() == poll.options.size();

        this->poll_ = poll;
        this->text_ = text;
        this->accent_ = accent;
        this->scale_ = scale;
        this->syncViews();
        // an option can be clicked to vote for it while the user still may
        this->setCursor(poll.ended || !poll.canVote ? Qt::ArrowCursor
                                                    : Qt::PointingHandCursor);
        this->relayout();

        // Slide the bars from where they are to the new shares. A poll that
        // just appeared, or another one, starts at its shares.
        this->animation_->stop();
        this->to_ = barFractions(poll);
        this->from_ = this->to_;
        if (samePoll && before.size() == this->to_.size())
        {
            bool moves = false;
            for (size_t i = 0; i < before.size(); i++)
            {
                moves = moves || std::abs(before[i] - this->to_[i]) > 0.0005;
            }
            if (moves)
            {
                this->from_ = before;
                this->animation_->start();
            }
        }
        this->update();
    }

    /// The height of a row with a single line of text
    int rowHeight() const
    {
        return QFontMetrics(this->font()).height() + this->rowPadding();
    }

    /// The height of all the rows, with the options wrapped to the width
    int contentHeight() const
    {
        return this->contentHeight_;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !this->optionClicked)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        const int y = static_cast<int>(event->position().y());
        for (size_t i = 0; i < this->rows_.size(); i++)
        {
            const auto &row = this->rows_[i];
            if (y >= row.top && y < row.top + row.height)
            {
                this->optionClicked(static_cast<int>(i) + 1);
                return;
            }
        }
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (event->oldSize().width() == event->size().width())
        {
            return;
        }

        // Another width wraps the options differently
        const int before = this->contentHeight_;
        this->relayout();
        this->update();
        if (this->contentHeight_ != before && this->heightChanged)
        {
            this->heightChanged();
        }
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(this->font());

        const int gap = std::max(1, static_cast<int>(2 * this->scale_));
        const int pad = this->textPadding();
        const auto shown = this->shownFractions();

        qint64 best = 0;
        for (const auto total : this->poll_.totals)
        {
            best = std::max(best, total);
        }

        QColor track = this->text_;
        track.setAlpha(24);

        for (size_t index = 0; index < this->rows_.size(); index++)
        {
            const Row &row = this->rows_[index];
            const qint64 votes = index < this->poll_.totals.size()
                                     ? this->poll_.totals[index]
                                     : 0;
            const bool leader = this->poll_.ended && best > 0 && votes == best;
            const QRect box(0, row.top + gap, this->width(),
                            row.height - 2 * gap);

            painter.setPen(Qt::NoPen);
            painter.setBrush(track);
            painter.drawRoundedRect(box, 3, 3);

            // The bar. While a hidden poll runs every bar is faintly full, so
            // none reads as zero.
            QColor fill = this->accent_;
            fill.setAlpha(leader ? 200 : 110);
            if (this->poll_.hidden)
            {
                fill.setAlpha(40);
                painter.setBrush(fill);
                painter.drawRoundedRect(box, 3, 3);
            }
            else if (index < shown.size() && shown[index] > 0.0)
            {
                auto bar = box;
                bar.setWidth(std::max(
                    1, static_cast<int>(static_cast<double>(box.width()) *
                                        shown[index])));
                painter.setBrush(fill);
                painter.drawRoundedRect(bar, 3, 3);
            }

            if (static_cast<int>(index) + 1 == this->poll_.myVote)
            {
                painter.setPen(QPen(this->accent_, 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(box.adjusted(0, 0, -1, -1), 3, 3);
            }

            painter.setPen(this->text_);

            // The share on the right, in the middle of the row
            painter.drawText(box.adjusted(0, 0, -pad, 0),
                             Qt::AlignRight | Qt::AlignVCenter, row.share);

            // "1 Option" on the left is the row's MessageView, which is a
            // child widget and drawn over this
        }
    }

private:
    struct Row {
        int top = 0;
        int height = 0;
        /// The height of the wrapped text
        int textHeight = 0;
        QString share;
        /// Shows the option's text (owned by this widget)
        MessageView *view = nullptr;
    };

    /// How wide each bar is, 0 to 1: the share of the votes. A hidden poll
    /// draws its bars faintly full instead, so they are 0 here.
    static std::vector<double> barFractions(const DggPoll &poll)
    {
        std::vector<double> out(static_cast<size_t>(poll.options.size()), 0.0);
        const qint64 all = allVotes(poll);
        if (poll.hidden || all <= 0)
        {
            return out;
        }
        for (size_t i = 0; i < out.size() && i < poll.totals.size(); i++)
        {
            if (poll.totals[i] > 0)
            {
                out[i] = static_cast<double>(poll.totals[i]) /
                         static_cast<double>(all);
            }
        }
        return out;
    }

    /// The bars as they are drawn now: on their way from `from_` to `to_`.
    std::vector<double> shownFractions() const
    {
        const double t =
            this->animation_->state() == QAbstractAnimation::Running
                ? this->animation_->currentValue().toDouble()
                : 1.0;
        std::vector<double> out = this->to_;
        for (size_t i = 0; i < out.size() && i < this->from_.size(); i++)
        {
            out[i] = this->from_[i] + (this->to_[i] - this->from_[i]) * t;
        }
        return out;
    }

    int rowPadding() const
    {
        return static_cast<int>(8 * this->scale_);
    }

    int textPadding() const
    {
        return static_cast<int>(6 * this->scale_);
    }

    /// "40% (20 votes)", or "?" while a hidden poll runs
    QString shareText(size_t index, qint64 all) const
    {
        if (this->poll_.hidden)
        {
            return QStringLiteral("?");
        }
        const qint64 votes =
            index < this->poll_.totals.size() ? this->poll_.totals[index] : 0;
        const auto percent = all > 0
                                 ? qRound(100.0 * static_cast<double>(votes) /
                                          static_cast<double>(all))
                                 : 0;
        return QStringLiteral("%1% (%2 %3)")
            .arg(percent)
            .arg(votes)
            .arg(votes == 1 ? QStringLiteral("vote") : QStringLiteral("votes"));
    }

    /// Makes one view for each option, and gives a view the text of its option
    /// when that changed. A view that stays is not rebuilt, so votes coming in
    /// don't make the emotes flicker.
    void syncViews()
    {
        const auto count = static_cast<size_t>(this->poll_.options.size());

        // A poll with fewer options than the one before
        while (this->views_.size() > count)
        {
            this->views_.back()->hide();
            this->views_.back()->deleteLater();
            this->views_.pop_back();
            this->shownTexts_.pop_back();
        }

        for (size_t i = 0; i < count; i++)
        {
            // "1 Option", on one line however the option was written
            QString text = QString::number(static_cast<int>(i) + 1) +
                           QChar(u' ') +
                           this->poll_.options[static_cast<qsizetype>(i)];
            text.replace(QChar(u'\n'), QChar(u' '));
            text.replace(QChar(u'\r'), QChar(u' '));

            if (i >= this->views_.size())
            {
                auto *view = new MessageView();
                view->setParent(this);
                view->setBackground(MessageView::Background::Transparent);
                // A click on an option is a click on its bar
                view->setAttribute(Qt::WA_TransparentForMouseEvents);
                QObject::connect(view, &MessageView::heightChanged, this,
                                 [this] {
                                     this->viewHeightChanged();
                                 });
                view->show();
                this->views_.push_back(view);
                // A null string: never equal to `text`
                this->shownTexts_.push_back(QString());
            }

            if (this->shownTexts_[i] != text)
            {
                this->shownTexts_[i] = text;
                this->views_[i]->setWrappedMessage(
                    this->makeMessage ? this->makeMessage(text) : nullptr);
            }
        }
    }

    /// The text of an option changed its height (an emote loaded, say).
    void viewHeightChanged()
    {
        if (this->relayouting_)
        {
            // relayout() reads the heights itself
            return;
        }

        const int before = this->contentHeight_;
        this->relayout();
        this->update();
        if (this->contentHeight_ != before && this->heightChanged)
        {
            this->heightChanged();
        }
    }

    /// Wraps every option to the current width and works out the rows.
    void relayout()
    {
        if (this->relayouting_)
        {
            return;
        }
        this->relayouting_ = true;

        const QFontMetrics metrics(this->font());
        const int pad = this->textPadding();
        const qint64 all = allVotes(this->poll_);

        this->rows_.clear();
        int y = 0;
        for (size_t i = 0; i < static_cast<size_t>(this->poll_.options.size());
             i++)
        {
            Row row;
            row.top = y;
            row.share = this->shareText(i, all);

            // Room: a padding, the text, a padding, the share, a padding
            const int available =
                std::max(20, this->width() - 3 * pad -
                                 metrics.horizontalAdvance(row.share));

            row.view = i < this->views_.size() ? this->views_[i] : nullptr;
            row.textHeight = metrics.height();
            if (row.view != nullptr)
            {
                row.view->setWidth(available);
                row.textHeight = std::max(1, row.view->height());
            }
            row.height =
                std::max(metrics.height(), row.textHeight) + this->rowPadding();

            if (row.view != nullptr)
            {
                // In the middle of the row, after the padding
                row.view->move(pad,
                               row.top + (row.height - row.textHeight) / 2);
            }

            y += row.height;
            this->rows_.push_back(std::move(row));
        }

        this->contentHeight_ = this->rows_.empty() ? this->rowHeight() : y;
        this->setFixedHeight(this->contentHeight_);

        this->relayouting_ = false;
    }

    DggPoll poll_;
    QColor text_{Qt::white};
    QColor accent_{Qt::blue};
    float scale_ = 1.0F;
    std::vector<Row> rows_;
    int contentHeight_ = 0;

    /// One view for each option, and the text each shows
    std::vector<MessageView *> views_;
    std::vector<QString> shownTexts_;

    /// relayout() is running (a view's height changing meanwhile is
    /// read by it, not by another relayout())
    bool relayouting_ = false;

    /// Slides the bars from `from_` to `to_`
    QVariantAnimation *animation_ = nullptr;
    std::vector<double> from_;
    std::vector<double> to_;
};

QString pollTitle(const DggPoll &poll)
{
    QString kind = poll.weighted ? QStringLiteral("sub-weighted poll")
                                 : QStringLiteral("poll");
    if (poll.hidden)
    {
        kind = QStringLiteral("hidden ") + kind;
    }
    kind[0] = kind[0].toUpper();
    return QStringLiteral("%1 started by <b>%2</b>")
        .arg(kind, poll.nick.toHtmlEscaped());
}

QString votesText(qint64 votes)
{
    return votes == 1 ? QStringLiteral("1 vote")
                      : QStringLiteral("%1 votes").arg(votes);
}

}  // namespace

DggPollBanner::DggPollBanner(QWidget *parent)
    : BaseWidget(parent)
    , infoLabel_(new QLabel(this))
    , statusLabel_(new QLabel(this))
    , closeButton_(new LabelButton(QStringLiteral("\u2715"), this))
    , questionView_(new MessageView())
    , optionsScrollArea_(new QScrollArea(this))
    , optionsView_(new PollOptionsView(this))
    , refreshTimer_(new QTimer(this))
    , tickTimer_(new QTimer(this))
    , endTimer_(new QTimer(this))
    , barTimer_(new QTimer(this))
{
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto *outerBox = new QVBoxLayout(this);
    // The room at the bottom is for the countdown bar
    outerBox->setContentsMargins(8, 6, 8, 9);
    outerBox->setSpacing(3);

    // Header row: "Poll started by <user>"   "12 votes · 0:23"  [x]
    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(6);
    this->infoLabel_->setTextFormat(Qt::RichText);
    headerRow->addWidget(this->infoLabel_);
    headerRow->addStretch(1);
    this->statusLabel_->setStyleSheet(MUTED_STYLE);
    headerRow->addWidget(this->statusLabel_);
    this->closeButton_->setScaleIndependentSize(28, 28);
    this->closeButton_->setToolTip(QStringLiteral("Hide the poll"));
    headerRow->addWidget(this->closeButton_);
    outerBox->addLayout(headerRow);

    // The question, drawn like a chat line (emotes, links and names work)
    this->questionView_->setBackground(MessageView::Background::Header);
    outerBox->addWidget(this->questionView_, 0, Qt::AlignLeft);
    QObject::connect(this->questionView_, &MessageView::linkClicked, this,
                     [this](const Link &link) {
                         this->openLink(link);
                     });

    // The options
    this->optionsScrollArea_->setWidgetResizable(true);
    this->optionsScrollArea_->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
    this->optionsScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->optionsScrollArea_->setFrameShape(QFrame::NoFrame);
    this->optionsScrollArea_->setFocusPolicy(Qt::NoFocus);
    this->optionsScrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; } "
        "QScrollArea > QWidget > QWidget { background: transparent; }");
    this->optionsScrollArea_->viewport()->setAutoFillBackground(false);
    this->optionsScrollArea_->setSizePolicy(QSizePolicy::Expanding,
                                            QSizePolicy::Fixed);
    this->optionsScrollArea_->setWidget(this->optionsView_);
    outerBox->addWidget(this->optionsScrollArea_);
    static_cast<PollOptionsView *>(this->optionsView_)->heightChanged = [this] {
        this->fitOptions();
    };
    static_cast<PollOptionsView *>(this->optionsView_)->optionClicked =
        [this](int option) {
            if (auto channel = this->channel_.lock())
            {
                getApp()->getDggChatServer()->castPollVote(*channel, option);
            }
        };
    static_cast<PollOptionsView *>(this->optionsView_)->makeMessage =
        [this](const QString &text) -> MessagePtr {
        const auto channel = this->channel_.lock();
        const auto *assets = getApp()->getDggChatServer()->assets();
        if (!channel || assets == nullptr)
        {
            return nullptr;
        }
        return DggMessageBuilder::makePollTextMessage(channel.get(), text,
                                                      *assets);
    };

    this->refreshTimer_->setSingleShot(true);
    this->refreshTimer_->setInterval(100ms);
    QObject::connect(this->refreshTimer_, &QTimer::timeout, this, [this] {
        // Votes came in while it waited: show them now
        if (this->refreshPending_)
        {
            this->refreshPending_ = false;
            this->refresh();
            this->refreshTimer_->start();
        }
    });

    this->tickTimer_->setInterval(250ms);
    QObject::connect(this->tickTimer_, &QTimer::timeout, this, [this] {
        this->tick();
    });

    // The countdown bar is redrawn far more often than the text beside it, so
    // that it runs out smoothly instead of in steps
    this->barTimer_->setInterval(33ms);
    QObject::connect(this->barTimer_, &QTimer::timeout, this, [this] {
        this->updateCountdownBar();
    });

    this->endTimer_->setSingleShot(true);
    this->endTimer_->setInterval(END_VISIBLE);
    QObject::connect(this->endTimer_, &QTimer::timeout, this, [this] {
        // Remembered, so that a refresh (a zoom, say) doesn't bring the
        // finished poll back
        this->endHidden_ = true;
        this->hide();
    });

    QObject::connect(this->closeButton_, &Button::leftClicked, this, [this] {
        this->closed_ = true;
        this->stopTicking();
        this->endTimer_->stop();
        this->hide();
    });

    this->scaleChangedEvent(this->scale());
    this->hide();
}

void DggPollBanner::setChannel(const std::shared_ptr<DggChannel> &channel)
{
    this->connections_.clear();
    this->channel_ = channel;

    this->currentKey_.clear();
    this->shownQuestion_.clear();
    this->closed_ = false;
    this->sawRunning_ = false;
    this->endShown_ = false;
    this->endHidden_ = false;
    this->poll_.reset();
    this->refreshPending_ = false;
    this->refreshTimer_->stop();
    this->stopTicking();
    this->endTimer_->stop();

    if (channel)
    {
        this->connections_.managedConnect(channel->pollChanged, [this] {
            this->scheduleRefresh();
        });
    }

    this->refresh();
}

void DggPollBanner::scheduleRefresh()
{
    if (this->refreshTimer_->isActive())
    {
        // Just redrawn: the votes since then are shown when the timer ends
        this->refreshPending_ = true;
        return;
    }

    // The first vote is shown at once, not after the wait
    this->refresh();
    this->refreshTimer_->start();
}

void DggPollBanner::startTicking()
{
    if (!this->tickTimer_->isActive())
    {
        this->tickTimer_->start();
    }
    if (!this->barTimer_->isActive())
    {
        this->barTimer_->start();
    }
}

void DggPollBanner::stopTicking()
{
    this->tickTimer_->stop();
    this->barTimer_->stop();
}

void DggPollBanner::updateCountdownBar()
{
    // Only the strip of the bar, not the whole banner
    const int barHeight = std::max(2, static_cast<int>(3 * this->scale()));
    this->update(0, this->height() - 1 - barHeight, this->width(), barHeight);
}

void DggPollBanner::refresh()
{
    const auto channel = this->channel_.lock();
    if (!channel || !channel->poll())
    {
        this->poll_.reset();
        this->stopTicking();
        this->endTimer_->stop();
        this->hide();
        return;
    }

    const DggPoll &poll = *channel->poll();
    if (poll.key() != this->currentKey_)
    {
        // Another poll: nothing the user did to the last one applies
        this->currentKey_ = poll.key();
        this->closed_ = false;
        this->sawRunning_ = false;
        this->endShown_ = false;
        this->endHidden_ = false;
        this->endTimer_->stop();
    }
    if (!poll.ended)
    {
        // Running (again, after a reconnect): its end is shown anew
        this->sawRunning_ = true;
        this->endShown_ = false;
        this->endHidden_ = false;
    }
    if (this->closed_ || !this->sawRunning_ || this->endHidden_)
    {
        this->stopTicking();
        this->hide();
        return;
    }
    this->poll_ = poll;

    this->updateTexts();
    this->updateOptions();

    this->tick();

    if (poll.ended)
    {
        this->stopTicking();
        if (!this->endShown_)
        {
            this->endShown_ = true;
            this->endTimer_->start();
        }
    }
    else
    {
        this->startTicking();
    }

    this->show();
}

void DggPollBanner::updateTexts()
{
    if (!this->poll_)
    {
        return;
    }

    this->infoLabel_->setText(pollTitle(*this->poll_));

    // Only built again when the question changed: votes come in far more
    // often than that
    const auto channel = this->channel_.lock();
    const auto *assets = getApp()->getDggChatServer()->assets();
    if (channel && assets != nullptr &&
        (this->shownQuestion_ != this->poll_->question ||
         this->shownQuestion_.isNull()))
    {
        this->shownQuestion_ = this->poll_->question;
        this->questionView_->setWrappedMessage(
            DggMessageBuilder::makePollTextMessage(
                channel.get(), this->poll_->question, *assets));
    }
    this->updateQuestionWidth();
}

void DggPollBanner::updateOptions()
{
    if (!this->poll_)
    {
        return;
    }

    auto *options = static_cast<PollOptionsView *>(this->optionsView_);
    options->setPoll(*this->poll_, this->theme->splits.header.text,
                     this->theme->accent, this->scale());
    this->fitOptions();
}

void DggPollBanner::updateQuestionWidth()
{
    // The banner minus the margins of its layout
    this->questionView_->setWidth(std::max(1, this->width() - 16));
}

void DggPollBanner::openLink(const Link &link)
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

void DggPollBanner::fitOptions()
{
    const auto *options =
        static_cast<const PollOptionsView *>(this->optionsView_);

    // As tall as the options are, wrapped lines included, up to the height of
    // `maxOptionRows_` one-line rows; more than that scrolls.
    this->optionsScrollArea_->setFixedHeight(std::min(
        options->contentHeight(), this->maxOptionRows_ * options->rowHeight()));
}

double DggPollBanner::remainingFraction() const
{
    if (!this->poll_ || this->poll_->ended || this->poll_->durationMs <= 0)
    {
        return 0.0;
    }
    const auto remaining =
        this->poll_->endsAtMs - QDateTime::currentMSecsSinceEpoch();
    return std::clamp(static_cast<double>(remaining) /
                          static_cast<double>(this->poll_->durationMs),
                      0.0, 1.0);
}

void DggPollBanner::tick()
{
    if (!this->poll_)
    {
        return;
    }

    if (this->poll_->ended)
    {
        this->statusLabel_->setText(
            QStringLiteral("Poll ended \u00B7 %1")
                .arg(votesText(this->poll_->votesCast)));
    }
    else
    {
        const auto remainingMs = std::max<qint64>(
            0, this->poll_->endsAtMs - QDateTime::currentMSecsSinceEpoch());
        const auto seconds = (remainingMs + 999) / 1000;
        this->statusLabel_->setText(QStringLiteral("%1 \u00B7 %2:%3")
                                        .arg(votesText(this->poll_->votesCast))
                                        .arg(seconds / 60)
                                        .arg(seconds % 60, 2, 10, QChar(u'0')));
    }
    this->update();
}

void DggPollBanner::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);

    // Same colour as the split header above
    painter.fillRect(event->rect(), this->theme->splits.header.background);

    // 1px top and bottom border
    painter.setPen(this->theme->splits.header.border);
    painter.drawLine(0, 0, this->width() - 1, 0);
    painter.drawLine(0, this->height() - 1, this->width() - 1,
                     this->height() - 1);

    // The countdown, a thin bar above the bottom border that runs out
    const int barHeight = std::max(2, static_cast<int>(3 * this->scale()));
    const int barWidth = static_cast<int>(static_cast<double>(this->width()) *
                                          this->remainingFraction());
    if (barWidth > 0)
    {
        painter.fillRect(0, this->height() - 1 - barHeight, barWidth, barHeight,
                         this->theme->accent);
    }
}

void DggPollBanner::scaleChangedEvent(float newScale)
{
    QFont headerFont = this->infoLabel_->font();
    headerFont.setPointSizeF(9.5F * newScale);
    this->infoLabel_->setFont(headerFont);
    this->statusLabel_->setFont(headerFont);

    QFont optionFont = this->optionsView_->font();
    optionFont.setPointSizeF(10.0F * newScale);
    this->optionsView_->setFont(optionFont);

    if (this->poll_)
    {
        this->refresh();
    }
}

void DggPollBanner::themeChangedEvent()
{
    // The colours of the options come from the theme. Only those are handed
    // over again (not a whole refresh(), which shows the banner).
    if (this->poll_)
    {
        this->updateOptions();
    }
}

void DggPollBanner::resizeEvent(QResizeEvent *event)
{
    BaseWidget::resizeEvent(event);
    this->updateQuestionWidth();
}

void DggPollBanner::mousePressEvent(QMouseEvent * /*event*/)
{
    // ignore to disable the parent's right click menu
}

}  // namespace chatterino
