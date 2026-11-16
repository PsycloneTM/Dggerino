#include "widgets/splits/DggPollBanner.hpp"

#include "providers/dgg/DggChannel.hpp"
#include "singletons/Theme.hpp"
#include "widgets/buttons/LabelButton.hpp"

#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
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

/// Lays `layout` out in lines no wider than `width`, breaking at spaces (or
/// inside a word that is wider than a line) and returns its height.
int layoutText(QTextLayout &layout, int width)
{
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);

    layout.beginLayout();
    qreal y = 0;
    for (;;)
    {
        QTextLine line = layout.createLine();
        if (!line.isValid())
        {
            break;
        }
        line.setLineWidth(width);
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout.endLayout();
    return static_cast<int>(std::ceil(y));
}

/// The options of a poll, one row each: a bar as wide as the share of the
/// votes, the number and text of the option on the left and the share on the
/// right. An option that doesn't fit on a line wraps and its row grows. The
/// option the user voted for has an outline; after the poll the winner (or
/// each option of a tie) is drawn stronger.
class PollOptionsView final : public QWidget
{
public:
    explicit PollOptionsView(QWidget *parent)
        : QWidget(parent)
    {
    }

    /// Called when the rows got taller or shorter because the width changed
    /// (the rows wrap to it).
    std::function<void()> heightChanged;

    void setPoll(const DggPoll &poll, const QColor &text, const QColor &accent,
                 float scale)
    {
        this->poll_ = poll;
        this->text_ = text;
        this->accent_ = accent;
        this->scale_ = scale;
        this->relayout();
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
        const qint64 all = allVotes(this->poll_);

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
            const bool leader =
                this->poll_.ended && best > 0 && votes == best;
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
            else if (all > 0 && votes > 0)
            {
                auto bar = box;
                bar.setWidth(std::max(
                    1, static_cast<int>(static_cast<double>(box.width()) *
                                        static_cast<double>(votes) /
                                        static_cast<double>(all))));
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

            // "1  Option" on the left, wrapped before it reaches the share
            row.layout->draw(
                &painter,
                QPointF(pad, row.top + (row.height - row.textHeight) / 2.0));
        }
    }

private:
    struct Row {
        int top = 0;
        int height = 0;
        /// The height of the wrapped text
        int textHeight = 0;
        QString share;
        std::unique_ptr<QTextLayout> layout;
    };

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
        const auto percent =
            all > 0 ? qRound(100.0 * static_cast<double>(votes) /
                             static_cast<double>(all))
                    : 0;
        return QStringLiteral("%1% (%2 %3)")
            .arg(percent)
            .arg(votes)
            .arg(votes == 1 ? QStringLiteral("vote")
                            : QStringLiteral("votes"));
    }

    /// Wraps every option to the current width and works out the rows.
    void relayout()
    {
        const QFontMetrics metrics(this->font());
        const int pad = this->textPadding();
        const qint64 all = allVotes(this->poll_);

        this->rows_.clear();
        int y = 0;
        for (qsizetype i = 0; i < this->poll_.options.size(); i++)
        {
            Row row;
            row.top = y;
            row.share = this->shareText(static_cast<size_t>(i), all);

            QString label = QString::number(i + 1) + QStringLiteral("  ") +
                            this->poll_.options[i];
            label.replace(QChar(u'\n'), QChar(u' '));
            label.replace(QChar(u'\r'), QChar(u' '));
            row.layout =
                std::make_unique<QTextLayout>(label, this->font(), this);

            // Room: a padding, the text, a padding, the share, a padding
            const int available = std::max(
                20, this->width() - 3 * pad -
                        metrics.horizontalAdvance(row.share));
            row.textHeight = layoutText(*row.layout, available);
            row.height = std::max(metrics.height(), row.textHeight) +
                         this->rowPadding();

            y += row.height;
            this->rows_.push_back(std::move(row));
        }

        this->contentHeight_ = this->rows_.empty() ? this->rowHeight() : y;
        this->setFixedHeight(this->contentHeight_);
    }

    DggPoll poll_;
    QColor text_{Qt::white};
    QColor accent_{Qt::blue};
    float scale_ = 1.0F;
    std::vector<Row> rows_;
    int contentHeight_ = 0;
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
    , questionLabel_(new QLabel(this))
    , optionsScrollArea_(new QScrollArea(this))
    , optionsView_(new PollOptionsView(this))
    , refreshTimer_(new QTimer(this))
    , tickTimer_(new QTimer(this))
    , endTimer_(new QTimer(this))
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

    // The question
    this->questionLabel_->setWordWrap(true);
    this->questionLabel_->setTextFormat(Qt::PlainText);
    this->questionLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    this->questionLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outerBox->addWidget(this->questionLabel_);

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
    static_cast<PollOptionsView *>(this->optionsView_)->heightChanged =
        [this] {
            this->fitOptions();
        };

    this->refreshTimer_->setSingleShot(true);
    this->refreshTimer_->setInterval(100ms);
    QObject::connect(this->refreshTimer_, &QTimer::timeout, this, [this] {
        this->refresh();
    });

    this->tickTimer_->setInterval(250ms);
    QObject::connect(this->tickTimer_, &QTimer::timeout, this, [this] {
        this->tick();
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
        this->tickTimer_->stop();
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
    this->closed_ = false;
    this->sawRunning_ = false;
    this->endShown_ = false;
    this->endHidden_ = false;
    this->poll_.reset();
    this->tickTimer_->stop();
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
    if (!this->refreshTimer_->isActive())
    {
        this->refreshTimer_->start();
    }
}

void DggPollBanner::refresh()
{
    const auto channel = this->channel_.lock();
    if (!channel || !channel->poll())
    {
        this->poll_.reset();
        this->tickTimer_->stop();
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
        this->tickTimer_->stop();
        this->hide();
        return;
    }
    this->poll_ = poll;

    this->infoLabel_->setText(pollTitle(poll));
    this->questionLabel_->setText(poll.question);

    auto *options = static_cast<PollOptionsView *>(this->optionsView_);
    options->setPoll(poll, this->theme->splits.header.text,
                     this->theme->accent, this->scale());
    this->fitOptions();

    this->tick();

    if (poll.ended)
    {
        this->tickTimer_->stop();
        if (!this->endShown_)
        {
            this->endShown_ = true;
            this->endTimer_->start();
        }
    }
    else if (!this->tickTimer_->isActive())
    {
        this->tickTimer_->start();
    }

    this->show();
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
        this->statusLabel_->setText(
            QStringLiteral("%1 \u00B7 %2:%3")
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
    const int barWidth = static_cast<int>(
        static_cast<double>(this->width()) * this->remainingFraction());
    if (barWidth > 0)
    {
        painter.fillRect(0, this->height() - 1 - barHeight, barWidth,
                         barHeight, this->theme->accent);
    }
}

void DggPollBanner::scaleChangedEvent(float newScale)
{
    QFont headerFont = this->infoLabel_->font();
    headerFont.setPointSizeF(9.5F * newScale);
    this->infoLabel_->setFont(headerFont);
    this->statusLabel_->setFont(headerFont);

    QFont questionFont = this->questionLabel_->font();
    questionFont.setPointSizeF(11.0F * newScale);
    questionFont.setBold(true);
    this->questionLabel_->setFont(questionFont);

    QFont optionFont = this->optionsView_->font();
    optionFont.setPointSizeF(10.0F * newScale);
    this->optionsView_->setFont(optionFont);

    if (this->poll_)
    {
        this->refresh();
    }
}

void DggPollBanner::mousePressEvent(QMouseEvent * /*event*/)
{
    // ignore to disable the parent's right click menu
}

}  // namespace chatterino
