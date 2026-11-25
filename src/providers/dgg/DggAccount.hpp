#pragma once

#include "controllers/accounts/Account.hpp"

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace chatterino {

/// What is persisted for a destiny.gg account.
///
/// `loginKey` is the key created under "Dgg App Connections" on
/// https://www.destiny.gg/profile/developer. It is stored in plain text in
/// the settings file, like the Twitch and Kick tokens. It authenticates the
/// chat socket (as the `authtoken` cookie) and nothing else: it is never sent
/// to a site URL. Never log it.
struct DggAccountData {
    QString username;
    uint64_t userID = 0;
    QString loginKey;

    void save() const;
    static std::optional<DggAccountData> loadRaw(const std::string &key);
};

class DggAccount : public Account,
                   public std::enable_shared_from_this<DggAccount>
{
public:
    DggAccount(const DggAccountData &args);
    ~DggAccount() override;

    Q_DISABLE_COPY_MOVE(DggAccount);

    void save() const;

    /// Updates the stored data. Returns true if anything changed.
    bool update(const DggAccountData &data);

    QString toString() const override;

    bool isAnonymous() const
    {
        return this->userID_ == 0;
    }

    QString username() const
    {
        return this->username_;
    }
    uint64_t userID() const
    {
        return this->userID_;
    }
    QString loginKey() const
    {
        return this->loginKey_;
    }

private:
    QString username_;
    uint64_t userID_ = 0;
    QString loginKey_;
};

}  // namespace chatterino
