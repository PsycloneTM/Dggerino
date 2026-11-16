// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWindow.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QString>

#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace chatterino {

class DggChannel;

/// The people connected to destiny.gg chat right now, in the same list as
/// ChatterListWidget shows for Twitch, grouped by the site's own sections
/// (privilege and subscription). It follows `NAMES`, `JOIN`, `QUIT` and `USERSDELTA` live.
class DggViewerListWidget : public BaseWindow
{
    Q_OBJECT

public:
    DggViewerListWidget(const std::shared_ptr<DggChannel> &channel,
                        QWidget *parent);

    /// A nick was double-clicked.
    Q_SIGNAL void userClicked(QString nick);

private:
    /// Rebuilds the list from the channel, keeping the scroll positions, and
    /// applies the search again.
    void refresh();

    /// The search, as in ChatterListWidget: with text in the search bar the
    /// matching names are shown in a second, flat list.
    void performListSearch();

    std::weak_ptr<DggChannel> channel_;
    pajlada::Signals::SignalHolder signalHolder_;

    QLineEdit *searchBar_ = nullptr;
    QLabel *loadingLabel_ = nullptr;
    QListWidget *chattersList_ = nullptr;
    QListWidget *resultList_ = nullptr;

    /// Joins and quits arrive in bursts: rebuilding the ~1500 rows at most
    /// every 500 ms is plenty.
    QTimer *refreshTimer_ = nullptr;
};

}  // namespace chatterino
