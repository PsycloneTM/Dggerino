#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <memory>

namespace chatterino {

class DggWebSocket;

/// The "Dgg" tab of the login dialog: the user pastes a login key (created at
/// https://www.destiny.gg/profile/developer, "Create Login Key") once.
///
/// The key is checked by connecting to the chat with it: if the server's `ME`
/// names a user, the key works and the account is saved and made active.
/// Nothing is saved otherwise. The key never goes into a URL (Chatterino
/// logs URLs) and is never logged.
class DggLoginPage : public QWidget
{
public:
    DggLoginPage();
    ~DggLoginPage() override;

private:
    void verify();

    /// Ends the check. An empty `error` means success.
    void finish(uint64_t attempt, const QString &nick, int64_t userID,
                const QString &error);
    void setBusy(bool busy);
    void showError(const QString &text);

    QLabel *instructions_ = nullptr;
    QLineEdit *keyInput_ = nullptr;
    QPushButton *addButton_ = nullptr;
    QLabel *status_ = nullptr;

    /// The connection that checks the key, while a check is running
    std::shared_ptr<DggWebSocket> verifier_;
    QTimer timeout_;
    /// Identifies the running check, so a late callback of an older one is
    /// ignored
    uint64_t attempt_ = 0;
    /// The key being checked. Cleared as soon as the check ends.
    QString pendingKey_;
};

}  // namespace chatterino
