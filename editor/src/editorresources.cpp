#include "editorresources.h"

#include <fstream>
#include <limits>

namespace platformator_editor
{
namespace
{
bool is_within(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}
} // namespace

std::optional<std::string> read_optional_editor_resource(
    const std::filesystem::path &executableDirectory,
    const std::filesystem::path &relativeResourcePath,
    std::size_t maximumBytes)
{
    if (executableDirectory.empty() || relativeResourcePath.empty() ||
        relativeResourcePath.is_absolute() || maximumBytes == 0)
        return std::nullopt;

    std::error_code error;
    const auto executableDirectoryCanonical = std::filesystem::canonical(executableDirectory, error);
    if (error || !std::filesystem::is_directory(executableDirectoryCanonical, error) || error)
        return std::nullopt;
    const auto installRoot = executableDirectoryCanonical.parent_path();
    if (installRoot.empty())
        return std::nullopt;

    const auto candidate = std::filesystem::canonical(
        executableDirectoryCanonical / relativeResourcePath, error);
    if (error || !is_within(installRoot, candidate) ||
        !std::filesystem::is_regular_file(candidate, error) || error)
        return std::nullopt;

    const auto size = std::filesystem::file_size(candidate, error);
    if (error || size > maximumBytes || size > std::numeric_limits<std::streamsize>::max())
        return std::nullopt;

    std::ifstream input(candidate, std::ios::binary);
    if (!input)
        return std::nullopt;
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (size > 0)
    {
        input.read(contents.data(), static_cast<std::streamsize>(size));
        if (!input || input.gcount() != static_cast<std::streamsize>(size))
            return std::nullopt;
    }
    return contents;
}

std::string read_editor_layout_or_fallback(
    const std::filesystem::path &executableDirectory,
    const std::filesystem::path &relativeLayoutPath,
    std::string_view embeddedLayout,
    std::size_t maximumBytes)
{
    if (const auto optional = read_optional_editor_resource(
            executableDirectory, relativeLayoutPath, maximumBytes);
        optional && !optional->empty())
        return *optional;
    return std::string(embeddedLayout);
}
} // namespace platformator_editor
