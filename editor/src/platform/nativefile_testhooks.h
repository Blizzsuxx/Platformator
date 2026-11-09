#pragma once

#include <filesystem>
#include <functional>

namespace platformator_editor
{
struct NativeFileTestHooks
{
    bool failTemporaryCreate = false;
    bool failFileFlush = false;
    bool failReplace = false;
    bool failDirectoryFlush = false;
    std::function<void(const std::filesystem::path &, const std::filesystem::path &)>
        afterStagingAndFlush;
    std::function<void(const std::filesystem::path &)> beforeCommit;
};

void set_native_file_test_hooks(NativeFileTestHooks hooks);
void clear_native_file_test_hooks();
} // namespace platformator_editor
