// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/signal.hpp>
#include <QElapsedTimer>
#include <QTimer>

namespace chatterino {

constexpr long unsigned GIF_FRAME_LENGTH = 20;

class GIFTimer
{
public:
    void initialize();

    pajlada::Signals::NoArgSignal signal;
    long unsigned position()
    {
        return this->position_;
    }

    /// Moves the shared animation clock forward by `ms` milliseconds. Called by
    /// the timer itself on every tick; tests use it to step animations without
    /// waiting for real time to pass.
    void advance(long unsigned ms)
    {
        this->position_ += ms;
    }

    void registerOpenOverlayWindow()
    {
        this->openOverlayWindows_++;
    }

    void unregisterOpenOverlayWindow()
    {
        assert(this->openOverlayWindows_ >= 1);
        this->openOverlayWindows_--;
    }

private:
    QTimer timer;
    QElapsedTimer wall_;
    qint64 lastWall_ = 0;
    long unsigned position_{};
    size_t openOverlayWindows_ = 0;
};

}  // namespace chatterino
