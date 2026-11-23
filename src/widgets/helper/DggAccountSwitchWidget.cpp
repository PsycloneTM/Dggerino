#include "widgets/helper/DggAccountSwitchWidget.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/dgg/DggAccount.hpp"
#include "singletons/Settings.hpp"

namespace chatterino {

DggAccountSwitchWidget::DggAccountSwitchWidget(QWidget *parent)
    : QListWidget(parent)
{
    this->managedConnections_.managedConnect(
        getApp()->getAccounts()->dgg.userListUpdated, [this]() {
            QSignalBlocker b(this);

            this->refreshItems();
            this->refresh();
        });

    this->refreshItems();
    this->refresh();

    QObject::connect(this, &QListWidget::clicked, [this] {
        if (this->selectedItems().isEmpty())
        {
            return;
        }

        QString newUsername = this->currentItem()->text();
        if (newUsername.compare(ANONYMOUS_USERNAME_LABEL,
                                Qt::CaseInsensitive) == 0)
        {
            getApp()->getAccounts()->dgg.currentUsername = "";
        }
        else
        {
            getApp()->getAccounts()->dgg.currentUsername =
                newUsername.toLower();
        }

        std::ignore = getSettings()->requestSave();
    });
}

void DggAccountSwitchWidget::refresh()
{
    QSignalBlocker b(this);

    if (this->count() <= 0)
    {
        return;
    }

    // Select the currently logged in user
    auto currentUser = getApp()->getAccounts()->dgg.current();

    if (currentUser->isAnonymous())
    {
        this->setCurrentRow(0);
        return;
    }

    QString currentUsername = currentUser->username();
    for (int i = 0; i < this->count(); ++i)
    {
        QString itemText = this->item(i)->text();

        if (itemText.compare(currentUsername, Qt::CaseInsensitive) == 0)
        {
            this->setCurrentRow(i);
            break;
        }
    }
}

void DggAccountSwitchWidget::refreshItems()
{
    this->clear();

    this->addItem(ANONYMOUS_USERNAME_LABEL);

    for (const auto &userName : getApp()->getAccounts()->dgg.usernames())
    {
        this->addItem(userName);
    }
}

}  // namespace chatterino
