#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace platformator_editor
{
struct LaunchOptions
{
    std::filesystem::path projectRoot;
    std::filesystem::path assetRoot;
    std::optional<std::filesystem::path> scenePath;
    bool projectRootProvided = false;
    bool assetRootProvided = false;
    float uiScale = 1.0f;
    bool uiScaleProvided = false;
    bool noRestore = false;
    bool help = false;
};

LaunchOptions parse_launch_options(int argc, const char *const argv[],
                                  const std::filesystem::path &invocationDirectory);
std::string launch_help();
int run_editor(int argc, char *argv[]);
} // namespace platformator_editor
