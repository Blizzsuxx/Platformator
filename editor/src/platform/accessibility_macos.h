#pragma once

#include "../accessibility.h"

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace platformator_editor
{
// NSAccessibility bridge for one SDL Cocoa window. Elements resolve immutable
// semantic snapshots on every callback, and actions are queued for the editor
// owner thread.
class MacAccessibilityProvider
{
  public:
    struct Impl;
    MacAccessibilityProvider();
    ~MacAccessibilityProvider();
    MacAccessibilityProvider(const MacAccessibilityProvider &) = delete;
    MacAccessibilityProvider &operator=(const MacAccessibilityProvider &) = delete;

    bool start(AccessibilitySnapshots &snapshots, SDL_Window *window,
               HostLayoutId host, WindowHostGeneration generation,
               std::string &error);
    void stop() noexcept;
    bool running() const noexcept;

  private:
    std::unique_ptr<Impl> impl_;
};
} // namespace platformator_editor
