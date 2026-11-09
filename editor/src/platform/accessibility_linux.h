#pragma once

#include "../accessibility.h"

#include <memory>
#include <string>

namespace platformator_editor
{
// Linux-only AT-SPI D-Bus provider. Its dispatch thread reads immutable semantic
// snapshots and queues actions; it never calls SDL, ImGui, or Runtime APIs.
class LinuxAccessibilityProvider
{
  public:
    struct Impl;
    LinuxAccessibilityProvider();
    ~LinuxAccessibilityProvider();
    LinuxAccessibilityProvider(const LinuxAccessibilityProvider &) = delete;
    LinuxAccessibilityProvider &operator=(const LinuxAccessibilityProvider &) = delete;

    bool start(AccessibilitySnapshots &snapshots, HostLayoutId host,
               WindowHostGeneration generation, std::string &error);
    void stop() noexcept;
    bool running() const noexcept;

  private:
    std::unique_ptr<Impl> impl_;
};
} // namespace platformator_editor
