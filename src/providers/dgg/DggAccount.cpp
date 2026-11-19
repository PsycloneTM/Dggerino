#include "providers/dgg/DggAccount.hpp"

#include "common/ChatterinoSetting.hpp"
#include "singletons/Settings.hpp"

#include <pajlada/settings/setting.hpp>

namespace chatterino {

std::optional<DggAccountData> DggAccountData::loadRaw(const std::string &key)
{
    auto username = QStringSetting::get("/dggAccounts/" + key + "/username");
    auto userID = UInt64Setting::get("/dggAccounts/" + key + "/userID");
    auto loginKey = QStringSetting::get("/dggAccounts/" + key + "/loginKey");

    if (username.isEmpty() || userID == 0 || loginKey.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    return DggAccountData{
        .username = username.trimmed(),
        .userID = userID,
        .loginKey = loginKey.trimmed(),
    };
}

void DggAccountData::save() const
{
    auto basePath = "/dggAccounts/uid" + std::to_string(this->userID);
    QStringSetting::set(basePath + "/username", this->username.toLower());
    UInt64Setting::set(basePath + "/userID", this->userID);
    QStringSetting::set(basePath + "/loginKey", this->loginKey);
    std::ignore = getSettings()->requestSave();
}

DggAccount::DggAccount(const DggAccountData &args)
    : Account(ProviderId::Dgg)
    , username_(args.username.toLower())
    , userID_(args.userID)
    , loginKey_(args.loginKey)
{
}

DggAccount::~DggAccount() = default;

void DggAccount::save() const
{
    DggAccountData{
        .username = this->username_,
        .userID = this->userID_,
        .loginKey = this->loginKey_,
    }
        .save();
}

bool DggAccount::update(const DggAccountData &data)
{
    bool changed = false;

    if (QString::compare(this->username_, data.username,
                         Qt::CaseInsensitive) != 0)
    {
        changed = true;
        this->username_ = data.username.toLower();
    }
    if (this->userID_ != data.userID)
    {
        changed = true;
        this->userID_ = data.userID;
    }
    if (this->loginKey_ != data.loginKey)
    {
        changed = true;
        this->loginKey_ = data.loginKey;
    }

    if (changed)
    {
        this->save();
    }
    return changed;
}

QString DggAccount::toString() const
{
    return this->username_;
}

}  // namespace chatterino
