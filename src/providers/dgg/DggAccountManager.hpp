#pragma once

#include "common/SignalVector.hpp"

#include <pajlada/settings/setting.hpp>
#include <pajlada/signals/signal.hpp>
#include <QString>

#include <memory>
#include <vector>

namespace chatterino {

class DggAccount;
struct DggAccountData;

class DggAccountManager
{
public:
    DggAccountManager();

    /// The active account, or an anonymous (guest) account. Never null.
    std::shared_ptr<DggAccount> current();

    std::vector<QString> usernames() const;

    std::shared_ptr<DggAccount> findUserByUsername(
        const QString &username) const;
    bool userExists(const QString &username) const;

    void reloadUsers();
    void load();

    bool isLoggedIn() const;

    /// Adds the account (or updates its key if it exists), saves it, and makes
    /// it the current account. Called by the login page after the key has
    /// been verified.
    void addAndActivate(const DggAccountData &data);

    pajlada::Settings::Setting<QString> currentUsername{"/dggAccounts/current",
                                                        ""};

    pajlada::Signals::NoArgSignal currentUserChanged;
    pajlada::Signals::NoArgSignal userListUpdated;

    SignalVector<std::shared_ptr<DggAccount>> accounts;

private:
    enum class AddUserResponse : uint8_t {
        UserAlreadyExists,
        UserUpdated,
        UserAdded,
    };
    AddUserResponse addAccount(const DggAccountData &data);
    bool removeAccount(DggAccount *account);

    std::shared_ptr<DggAccount> currentUser_;
    std::shared_ptr<DggAccount> anonymousUser_;
};

}  // namespace chatterino
