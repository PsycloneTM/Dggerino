#pragma once

#include "providers/dgg/DggMessage.hpp"
#include "widgets/BaseWidget.hpp"

#include <memory>
#include <optional>

class QLabel;
class QScrollArea;
class QTimer;

namespace chatterino {

class DggChannel;
class LabelButton;
class MessageView;
struct Link;

/// The banner under the split header that shows the poll that is running in
/// destiny.gg chat: who started it, the question, a bar for each option with
/// its share of the votes (an option longer than a line wraps instead of
/// being cut off), the number of votes and a countdown. When the poll
/// ends the winner is highlighted and the banner goes away after a few
/// seconds, like the poll frame of the site. The close button hides it for
/// the rest of the poll.
///
/// Clicking an option votes for it (like chat-gui), while the poll runs and the
/// user is logged in and hasn't voted yet.
///
/// The question and the options are drawn like chat lines (MessageView), so
/// they show emotes, emoji, links and names. The option views let mouse clicks
/// through to the bars below them.
class DggPollBanner final : public BaseWidget
{
    Q_OBJECT

public:
    explicit DggPollBanner(QWidget *parent = nullptr);

    /// Pass nullptr to detach from any channel.
    void setChannel(const std::shared_ptr<DggChannel> &channel);

protected:
    void scaleChangedEvent(float newScale) override;
    void themeChangedEvent() override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void paintEvent(QPaintEvent *event) override;

    /// Redraws the banner from the channel's poll: at once, then at most every
    /// 100 ms however many votes come in.
    void scheduleRefresh();
    void refresh();

    /// Shows the header and the question of the poll. The question is only
    /// built again when it changed.
    void updateTexts();

    /// Hands the poll and the colours to the options view.
    void updateOptions();

    /// Wraps the question to the width of the banner.
    void updateQuestionWidth();

    /// A link or name in the question was clicked.
    void openLink(const Link &link);

    /// Sizes the scroll area to the options: as tall as they are (an option
    /// that is wider than a line wraps and takes more), up to
    /// `maxOptionRows_` rows.
    void fitOptions();

    /// Updates the vote count, countdown and timer bar.
    void tick();

    /// Starts and stops `tickTimer_` and `barTimer_` together.
    void startTicking();
    void stopTicking();

    /// Redraws the strip of the countdown bar.
    void updateCountdownBar();

    /// How much of the poll's time is left, 0 to 1.
    double remainingFraction() const;

    std::weak_ptr<DggChannel> channel_;
    pajlada::Signals::SignalHolder connections_;

    QLabel *infoLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    LabelButton *closeButton_ = nullptr;
    MessageView *questionView_ = nullptr;
    QScrollArea *optionsScrollArea_ = nullptr;
    /// Draws the options (defined in the .cpp)
    QWidget *optionsView_ = nullptr;

    QTimer *refreshTimer_ = nullptr;
    QTimer *tickTimer_ = nullptr;
    QTimer *endTimer_ = nullptr;
    /// Redraws the countdown bar about 30 times a second while a poll runs.
    QTimer *barTimer_ = nullptr;

    /// Votes came in while `refreshTimer_` was running.
    bool refreshPending_ = false;

    /// The poll that is shown
    std::optional<DggPoll> poll_;

    /// DggPoll::key() of the poll that is being handled
    QString currentKey_;

    /// The question `questionView_` shows
    QString shownQuestion_;

    /// The user closed the banner for this poll.
    bool closed_ = false;

    /// The poll was seen running. A poll that is already over when the banner
    /// first looks (a split opened afterwards) is not shown.
    bool sawRunning_ = false;

    /// The hiding of the ended poll was scheduled.
    bool endShown_ = false;

    /// The ended poll was shown for its few seconds and hidden again. It
    /// stays hidden however often the banner is refreshed (zooming
    /// refreshes it) until another poll, or the same one running again.
    bool endHidden_ = false;

    /// Cap for the list of options, in one-line rows (it scrolls past that).
    int maxOptionRows_ = 8;
};

}  // namespace chatterino
