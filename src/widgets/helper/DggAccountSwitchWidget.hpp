#pragma once

#include <pajlada/signals/signalholder.hpp>
#include <QListWidget>

namespace chatterino {

/// Lists the saved destiny.gg accounts and Guest; a click makes one active
/// (the chat reconnects as that user). Copy of KickAccountSwitchWidget.
class DggAccountSwitchWidget : public QListWidget
{
public:
    explicit DggAccountSwitchWidget(QWidget *parent = nullptr);

    void refresh();

private:
    void refreshItems();

    pajlada::Signals::SignalHolder managedConnections_;
};

}  // namespace chatterino
