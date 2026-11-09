#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace platformator_editor
{
// Reads an optional resource below the install prefix containing the executable.
// The file is canonicalized and size-bounded before it is returned.
std::optional<std::string> read_optional_editor_resource(
    const std::filesystem::path &executableDirectory,
    const std::filesystem::path &relativeResourcePath,
    std::size_t maximumBytes);

std::string read_editor_layout_or_fallback(
    const std::filesystem::path &executableDirectory,
    const std::filesystem::path &relativeLayoutPath,
    std::string_view embeddedLayout,
    std::size_t maximumBytes);
} // namespace platformator_editor
