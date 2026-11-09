#pragma once

#include <cstdint>
#include <functional>

namespace platformator_editor
{
struct NativeProcessTestHooks
{
    bool forcePosixForkFallback = false;
    bool failHandleWhitelistUpdate = false;
    bool failJobAssignment = false;
    std::function<void()> onHandleWhitelistFailure;
    std::function<void(bool)> onSuspendedChildCleanup;
};

void set_native_process_test_hooks(NativeProcessTestHooks hooks);
void clear_native_process_test_hooks();
} // namespace platformator_editor
