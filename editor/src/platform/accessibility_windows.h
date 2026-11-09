#pragma once

#include "../accessibility.h"

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace platformator_editor
{
// UI Automation bridge for one SDL Win32 window. Provider callbacks read
// immutable host-incarnation snapshots and queue actions for the editor thread.
class WindowsAccessibilityProvider
{
  public:
    struct Impl;
    WindowsAccessibilityProvider();
    ~WindowsAccessibilityProvider();
    WindowsAccessibilityProvider(const WindowsAccessibilityProvider &) = delete;
    WindowsAccessibilityProvider &operator=(const WindowsAccessibilityProvider &) = delete;

    bool start(AccessibilitySnapshots &snapshots, SDL_Window *window,
               HostLayoutId host, WindowHostGeneration generation,
               std::string &error);
    void stop() noexcept;
    bool running() const noexcept;

  private:
    std::unique_ptr<Impl> impl_;
};
} // namespace platformator_editor
