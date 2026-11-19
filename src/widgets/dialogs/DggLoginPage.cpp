#include "widgets/dialogs/DggLoginPage.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/dgg/DggAccount.hpp"
#include "providers/dgg/DggAccountManager.hpp"
#include "providers/dgg/DggFrame.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "providers/dgg/DggWebSocket.hpp"

#include <QJsonObject>
#include <QVBoxLayout>

namespace chatterino {

using namespace Qt::Literals;

namespace {

/// How long the server gets to say who the key belongs to
constexpr int VERIFY_TIMEOUT_MS = 15000;

}  // namespace

DggLoginPage::DggLoginPage()
{
    auto *layout = new QVBoxLayout(this);

    this->instructions_ = new QLabel(this);
    this->instructions_->setTextFormat(Qt::RichText);
    this->instructions_->setOpenExternalLinks(true);
    this->instructions_->setWordWrap(true);
    this->instructions_->setText(
        u"1. Open <a "
        u"href=\"https://www.destiny.gg/profile/developer\">destiny.gg/"
        u"profile/developer</a> and log in"
        u"<br>2. Under \"Dgg App Connections\", press Create Login Key"
        u"<br>3. Show the key, copy it and paste it below"
        u"<br>4. Press Add user. You only have to do this once."
        u"<br><br>The key is saved in Chatterino's settings file, like the "
        u"tokens of other accounts. Anyone who has it can chat as you; if it "
        u"leaks, remove it on the developer page."_s);
    layout->addWidget(this->instructions_);

    this->keyInput_ = new QLineEdit(this);
    this->keyInput_->setEchoMode(QLineEdit::Password);
    this->keyInput_->setPlaceholderText(u"Login key"_s);
    layout->addWidget(this->keyInput_);

    this->addButton_ = new QPushButton(u"Add user"_s, this);
    this->addButton_->setEnabled(false);
    layout->addWidget(this->addButton_);

    this->status_ = new QLabel(this);
    this->status_->setWordWrap(true);
    layout->addWidget(this->status_);

    layout->addStretch(1);

    QObject::connect(this->keyInput_, &QLineEdit::textChanged, this, [this] {
        this->addButton_->setEnabled(
            !this->keyInput_->text().trimmed().isEmpty());
    });
    QObject::connect(this->keyInput_, &QLineEdit::returnPressed, this, [this] {
        this->verify();
    });
    QObject::connect(this->addButton_, &QPushButton::clicked, this, [this] {
        this->verify();
    });

    this->timeout_.setSingleShot(true);
    QObject::connect(&this->timeout_, &QTimer::timeout, this, [this] {
        this->finish(this->attempt_, {}, 0,
                     u"destiny.gg did not answer. Check your connection and "
                     u"try again."_s);
    });
}

DggLoginPage::~DggLoginPage()
{
    this->timeout_.stop();
    this->pendingKey_.clear();
    if (this->verifier_)
    {
        this->verifier_->stop();
    }
}

void DggLoginPage::setBusy(bool busy)
{
    this->keyInput_->setEnabled(!busy);
    this->addButton_->setEnabled(!busy &&
                                 !this->keyInput_->text().trimmed().isEmpty());
}

void DggLoginPage::showError(const QString &text)
{
    this->status_->setStyleSheet(u"color: #d9534f;"_s);
    this->status_->setText(text);
}

void DggLoginPage::verify()
{
    if (this->verifier_)
    {
        // a check is already running
        return;
    }

    const auto key = this->keyInput_->text().trimmed();
    if (key.isEmpty())
    {
        return;
    }
    if (!dggIsPlausibleLoginKey(key))
    {
        this->showError(
            u"That doesn't look like a login key. Copy the whole key from the "
            u"developer page, without spaces."_s);
        return;
    }

    this->status_->setStyleSheet({});
    this->status_->setText(u"Checking the key..."_s);
    this->setBusy(true);

    const auto attempt = ++this->attempt_;
    this->pendingKey_ = key;

    this->verifier_ = std::make_shared<DggWebSocket>();
    this->verifier_->setAuthToken(key);
    this->verifier_->onFrameReceived = [this, attempt](const DggFrame &frame) {
        if (attempt != this->attempt_)
        {
            return;
        }

        if (frame.name == "ME"_L1)
        {
            // A guest's `ME` has no user. A logged-in user's is their user
            // object (`nick`, `id`, ...).
            const auto obj = frame.payload.toObject();
            const auto nick = obj.value("nick"_L1).toString();
            const auto id = obj.value("id"_L1).toVariant().toLongLong();
            if (nick.isEmpty())
            {
                this->finish(attempt, {}, 0,
                             u"destiny.gg did not accept that key. Make sure "
                             u"it is a login key that is still listed on the "
                             u"developer page."_s);
            }
            else if (id <= 0)
            {
                this->finish(attempt, {}, 0,
                             u"destiny.gg did not say which user the key "
                             u"belongs to."_s);
            }
            else
            {
                this->finish(attempt, nick, id, {});
            }
        }
        else if (frame.name == "ERR"_L1)
        {
            // banned, too many connections, ...
            this->finish(attempt, {}, 0,
                         dggErrorText(frame.payload.toObject()));
        }
    };
    this->timeout_.start(VERIFY_TIMEOUT_MS);
    this->verifier_->start();
}

void DggLoginPage::finish(uint64_t attempt, const QString &nick, int64_t userID,
                          const QString &error)
{
    if (attempt != this->attempt_ || !this->verifier_)
    {
        return;
    }

    this->timeout_.stop();

    // finish() runs inside the connection's own callback, so the connection
    // is stopped now and only freed after the callback has returned.
    auto old = std::move(this->verifier_);
    old->stop();
    QTimer::singleShot(0, this, [old] {});

    const auto key = std::move(this->pendingKey_);
    this->pendingKey_.clear();

    this->setBusy(false);

    if (!error.isEmpty())
    {
        this->showError(error);
        return;
    }

    DggAccountData data;
    data.username = nick;
    data.userID = static_cast<uint64_t>(userID);
    data.loginKey = key;
    // Saves the account and makes it the active one; the chat reconnects as
    // the new user.
    getApp()->getAccounts()->dgg.addAndActivate(data);

    this->keyInput_->clear();
    this->status_->setStyleSheet({});
    this->status_->setText(u"Logged in as %1."_s.arg(nick));
    this->window()->close();
}

}  // namespace chatterino
