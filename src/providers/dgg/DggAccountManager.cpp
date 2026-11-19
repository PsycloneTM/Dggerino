#include "providers/dgg/DggAccountManager.hpp"

#include "common/QLogging.hpp"
#include "providers/dgg/DggAccount.hpp"
#include "util/RapidJsonSerializeQString.hpp"  // IWYU pragma: keep
#include "util/SharedPtrElementLess.hpp"

#include <pajlada/settings/setting.hpp>
#include <pajlada/settings/settingmanager.hpp>

namespace chatterino {

DggAccountManager::DggAccountManager()
    : accounts(SharedPtrElementLess<DggAccount>{})
    , anonymousUser_(std::make_shared<DggAccount>(DggAccountData{}))
{
    std::ignore = this->accounts.itemRemoved.connect([this](const auto &acc) {
        this->removeAccount(acc.item.get());
    });
}

std::shared_ptr<DggAccount> DggAccountManager::current()
{
    if (!this->currentUser_)
    {
        return this->anonymousUser_;
    }
    return this->currentUser_;
}

std::vector<QString> DggAccountManager::usernames() const
{
    std::vector<QString> names;
    for (const auto &acc : this->accounts.raw())
    {
        names.emplace_back(acc->username());
    }
    return names;
}

std::shared_ptr<DggAccount> DggAccountManager::findUserByUsername(
    const QString &username) const
{
    for (const auto &acc : this->accounts.raw())
    {
        if (QString::compare(acc->username(), username, Qt::CaseInsensitive) ==
            0)
        {
            return acc;
        }
    }
    return nullptr;
}

bool DggAccountManager::userExists(const QString &username) const
{
    return this->findUserByUsername(username) != nullptr;
}

bool DggAccountManager::isLoggedIn() const
{
    return this->currentUser_ && !this->currentUser_->isAnonymous();
}

void DggAccountManager::reloadUsers()
{
    auto keys =
        pajlada::Settings::SettingManager::getObjectKeys("/dggAccounts");

    bool listUpdated = false;

    for (const auto &uid : keys)
    {
        if (uid == "current")
        {
            continue;
        }

        auto data = DggAccountData::loadRaw(uid);
        if (!data)
        {
            continue;
        }

        // Never log data->loginKey.
        switch (this->addAccount(*data))
        {
            case AddUserResponse::UserAlreadyExists: {
                qCDebug(chatterinoDgg)
                    << "Account" << data->username << "already exists";
            }
            break;
            case AddUserResponse::UserUpdated: {
                qCDebug(chatterinoDgg) << "Account" << data->username
                                       << "updated";
                if (data->username == this->current()->username())
                {
                    this->currentUserChanged.invoke();
                }
            }
            break;
            case AddUserResponse::UserAdded: {
                qCDebug(chatterinoDgg) << "Added account" << data->username;
                listUpdated = true;
            }
            break;
        }
    }

    if (listUpdated)
    {
        this->userListUpdated.invoke();
    }
}

void DggAccountManager::load()
{
    this->reloadUsers();

    this->currentUsername.connect([this](const QString &newUsername) {
        auto user = this->findUserByUsername(newUsername);
        if (user)
        {
            qCDebug(chatterinoDgg) << "Account switched to" << newUsername;
            this->currentUser_ = user;
        }
        else
        {
            qCDebug(chatterinoDgg) << "Account switched to guest";
            this->currentUser_ = this->anonymousUser_;
        }

        this->currentUserChanged.invoke();
    });
}

void DggAccountManager::addAndActivate(const DggAccountData &data)
{
    if (data.username.isEmpty() || data.userID == 0 ||
        data.loginKey.trimmed().isEmpty())
    {
        return;
    }

    data.save();
    this->addAccount(data);
    this->userListUpdated.invoke();
    // Setting the name is what switches the current account (see load()).
    this->currentUsername = data.username.toLower();
}

DggAccountManager::AddUserResponse DggAccountManager::addAccount(
    const DggAccountData &data)
{
    auto previousUser = this->findUserByUsername(data.username);
    if (previousUser)
    {
        bool userUpdated = previousUser->update(data);
        if (userUpdated)
        {
            return AddUserResponse::UserUpdated;
        }

        return AddUserResponse::UserAlreadyExists;
    }

    auto account = std::make_shared<DggAccount>(data);
    this->accounts.insert(account);

    return AddUserResponse::UserAdded;
}

bool DggAccountManager::removeAccount(DggAccount *account)
{
    if (account->isAnonymous())
    {
        return false;
    }

    auto accountPath = "/dggAccounts/uid" + std::to_string(account->userID());
    pajlada::Settings::SettingManager::gRemoveSetting(accountPath);

    if (account->username() == this->currentUsername)
    {
        // The removed user was the current one: fall back to guest.
        this->currentUsername = "";
    }

    this->userListUpdated.invoke();
    return true;
}

}  // namespace chatterino
