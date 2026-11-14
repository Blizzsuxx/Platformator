#pragma once

#include <chrono>
#include <string>

namespace platformator_editor
{
using StatusToastClock = std::chrono::steady_clock;

inline void update_status_toast(std::string &message, std::string &lastMessage,
                                StatusToastClock::time_point &deadline,
                                StatusToastClock::time_point now)
{
    if (message != lastMessage)
    {
        lastMessage = message;
        deadline = message.empty() ? StatusToastClock::time_point{}
                                  : now + std::chrono::seconds(5);
    }
    if (!message.empty() && deadline != StatusToastClock::time_point{} && now >= deadline)
    {
        message.clear();
        lastMessage.clear();
        deadline = {};
    }
}

inline bool status_toast_visible(const std::string &message,
                                 StatusToastClock::time_point deadline,
                                 StatusToastClock::time_point now) noexcept
{
    return !message.empty() && deadline != StatusToastClock::time_point{} && now < deadline;
}
} // namespace platformator_editor
