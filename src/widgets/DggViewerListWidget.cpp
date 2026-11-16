// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/DggViewerListWidget.hpp"

#include "Application.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "util/Helpers.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace chatterino {

using namespace Qt::StringLiterals;

namespace {

/// The sections of the user list, in the order of chat-gui's
/// `UserMenuSections` (ChatUserMenu.js), which is hardcoded there: flairs.json
/// knows the flairs, not the sections. A user goes to the section of the
/// first flair of this table it has, except that Bot always wins ("force"
/// in chat-gui), because a bot can have a subscriber flair too.
struct Section {
    /// The heading, as in the stock chatter list ("Moderators (5)")
    QString title;
    QStringList features;
};

const std::vector<Section> &sections()
{
    static const std::vector<Section> list{
        {u"Admins"_s, {u"admin"_s}},
        {u"Moderators"_s, {u"moderator"_s}},
        {u"Broadcasters"_s, {u"flair12"_s}},
        {u"VIPs"_s, {u"vip"_s}},
        {u"Trusted Users"_s, {u"flair4"_s}},
        {u"Contributors"_s, {u"flair5"_s, u"flair16"_s}},
        {u"Subscriber Tier 5s"_s, {u"flair42"_s}},
        {u"Subscriber Tier 4s"_s, {u"flair8"_s}},
        {u"Subscriber Tier 3s"_s, {u"flair3"_s}},
        {u"Subscriber Tier 2s"_s, {u"flair1"_s}},
        {u"Subscriber Tier 1s"_s, {u"flair13"_s}},
        {u"Users"_s, {}},
        {u"Bots"_s, {u"bot"_s, u"flair11"_s}},
    };
    return list;
}

constexpr size_t USER_SECTION = 11;
constexpr size_t BOT_SECTION = 12;

size_t sectionOf(const DggUser &user)
{
    const auto has = [&](const QStringList &features) {
        return std::any_of(features.begin(), features.end(),
                           [&](const QString &f) {
                               return user.features.contains(f);
                           });
    };

    if (has(sections()[BOT_SECTION].features))
    {
        return BOT_SECTION;
    }
    for (size_t i = 0; i < USER_SECTION; i++)
    {
        if (has(sections()[i].features))
        {
            return i;
        }
    }
    return USER_SECTION;
}

}  // namespace

DggViewerListWidget::DggViewerListWidget(
    const std::shared_ptr<DggChannel> &channel, QWidget *parent)
    : BaseWindow({}, parent)
    , channel_(channel)
{
    assert(channel != nullptr);

    // from here on this is ChatterListWidget's code
    this->setWindowTitle(u"Chatter List - destiny.gg"_s);
    this->setAttribute(Qt::WA_DeleteOnClose);

    auto *dockVbox = new QVBoxLayout();
    this->searchBar_ = new QLineEdit(this);

    this->chattersList_ = new QListWidget();
    this->resultList_ = new QListWidget();

    this->loadingLabel_ = new QLabel(u"Loading..."_s);
    this->searchBar_->setPlaceholderText(u"Search User..."_s);

    QObject::connect(this->searchBar_, &QLineEdit::textEdited, this,
                     [this] {
                         this->performListSearch();
                     });

    this->setMinimumWidth(300);

    auto listDoubleClick = [this](const QModelIndex &index) {
        const auto itemText = index.data().toString();

        if (!itemText.isEmpty())
        {
            this->userClicked(itemText);
        }
    };

    QObject::connect(this->chattersList_, &QListWidget::doubleClicked, this,
                     listDoubleClick);

    QObject::connect(this->resultList_, &QListWidget::doubleClicked, this,
                     listDoubleClick);

    // Joins and quits arrive in bursts: rebuilding the ~1500 rows at most
    // every 500 ms is plenty. The signal fires on the GUI thread.
    this->refreshTimer_ = new QTimer(this);
    this->refreshTimer_->setSingleShot(true);
    this->refreshTimer_->setInterval(500);
    QObject::connect(this->refreshTimer_, &QTimer::timeout, this, [this] {
        this->refresh();
    });
    this->signalHolder_.managedConnect(channel->onlineChanged, [this] {
        if (!this->refreshTimer_->isActive())
        {
            this->refreshTimer_->start();
        }
    });

    HotkeyController::HotkeyMap actions{
        {"delete",
         [this](const std::vector<QString> &) -> QString {
             this->close();
             return "";
         }},
        {"accept", nullptr},
        {"reject", nullptr},
        {"scrollPage", nullptr},
        {"openTab", nullptr},
        {"search",
         [this](const std::vector<QString> &) -> QString {
             this->searchBar_->setFocus();
             this->searchBar_->selectAll();
             return "";
         }},
    };
    getApp()->getHotkeys()->shortcutsForCategory(HotkeyCategory::PopupWindow,
                                                 actions, this);

    dockVbox->addWidget(this->searchBar_);
    dockVbox->addWidget(this->loadingLabel_);
    dockVbox->addWidget(this->chattersList_);
    dockVbox->addWidget(this->resultList_);
    this->resultList_->hide();

    this->setStyleSheet(this->theme->splits.input.styleSheet);
    this->setLayout(dockVbox);

    this->refresh();
}

void DggViewerListWidget::performListSearch()
{
    auto query = this->searchBar_->text();
    if (query.isEmpty())
    {
        this->resultList_->hide();
        this->chattersList_->show();
        return;
    }

    auto results = this->chattersList_->findItems(query, Qt::MatchContains);
    this->chattersList_->hide();
    this->resultList_->clear();
    for (auto &item : results)
    {
        if (!item->text().contains("("))
        {
            auto *result = new QListWidgetItem();
            result->setText(item->text());
            result->setFont(
                getApp()->getFonts()->getFont(FontStyle::ChatMedium, 1.0));
            this->resultList_->addItem(result);
        }
    }
    this->resultList_->show();
}

void DggViewerListWidget::refresh()
{
    const auto channel = this->channel_.lock();
    if (!channel)
    {
        // the chat was closed under the window
        this->close();
        return;
    }

    const auto chattersScroll =
        this->chattersList_->verticalScrollBar()->value();
    const auto resultScroll = this->resultList_->verticalScrollBar()->value();

    this->chattersList_->setUpdatesEnabled(false);
    this->chattersList_->clear();

    auto formatListItemText = [](const QString &text) {
        auto *item = new QListWidgetItem();
        item->setText(text);
        item->setFont(
            getApp()->getFonts()->getFont(FontStyle::ChatMedium, 1.0));
        return item;
    };

    auto addLabel = [this, formatListItemText](const QString &label) {
        auto *formattedLabel = formatListItemText(label);
        formattedLabel->setFlags(Qt::NoItemFlags);
        formattedLabel->setForeground(this->theme->accent);
        this->chattersList_->addItem(formattedLabel);
    };

    auto addUserList = [=, this](const QStringList &users, QString label) {
        if (users.isEmpty())
        {
            return;
        }

        addLabel(QString("%1 (%2)").arg(label, localizeNumbers(users.size())));

        for (const auto &user : users)
        {
            this->chattersList_->addItem(formatListItemText(user));
        }
        this->chattersList_->addItem(new QListWidgetItem());
    };

    // the only part that isn't the stock list: the site's hardcoded sections
    const auto users = channel->onlineUsers();
    std::array<QStringList, 13> groups;
    for (const auto &user : users)
    {
        groups[sectionOf(user)].append(user.nick);
    }
    for (size_t i = 0; i < groups.size(); i++)
    {
        groups[i].sort(Qt::CaseInsensitive);
        addUserList(groups[i], sections()[i].title);
    }

    this->chattersList_->setUpdatesEnabled(true);

    // like the stock list, "Loading..." only shows until the names are there
    this->loadingLabel_->setVisible(users.empty());
    this->performListSearch();

    this->chattersList_->verticalScrollBar()->setValue(chattersScroll);
    this->resultList_->verticalScrollBar()->setValue(resultScroll);
}

}  // namespace chatterino
