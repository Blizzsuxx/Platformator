#include "pathmanager.h"
#include "runtimeaccess.h"
#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace fs = std::filesystem;
namespace
{
std::string separators(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}
bool logical(const fs::path &path)
{
    return !path.empty() && *path.begin() == "assets";
}
bool beneath(const fs::path &root, const fs::path &path)
{
    return std::mismatch(root.begin(), root.end(), path.begin(), path.end()).first == root.end();
}
} // namespace

PathManager::PathManager(const std::string &configuredRoot) : startupBase(fs::current_path())
{
    if (!configuredRoot.empty())
        root = fs::absolute(fs::path(configuredRoot));
    else
    {
        const char *base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("Cannot determine the executable directory");
        fs::path executable(base);
        if (executable.filename().empty())
            executable = executable.parent_path();
        root = fs::is_directory(executable / "assets") ? executable / "assets" : executable.parent_path() / "assets";
    }
    root = fs::weakly_canonical(root);
}

PathManager &PathManager::getInstance()
{
    return platformator_detail::RuntimeAccess::pathManager();
}
std::string PathManager::getAssetsRootAbsolutePath() const
{
    return root.generic_string();
}

std::string PathManager::canonicalizeAssetPath(const std::string &raw) const
{
    if (raw.empty())
        return {};
    const fs::path path(separators(raw));
    if (path.is_absolute() || !logical(path) || path == "assets" || path.filename().empty() || path.filename() == ".")
        throw std::invalid_argument("Asset paths must use assets/...: " + raw);
    for (const auto &component : path)
        if (component == "..")
            throw std::invalid_argument("Asset paths cannot contain '..': " + raw);
    const auto candidate = fs::weakly_canonical(root / path.lexically_relative("assets"));
    if (!beneath(root, candidate))
        throw std::invalid_argument("Asset path escapes configured root: " + raw);
    return (fs::path("assets") / candidate.lexically_relative(root)).generic_string();
}

fs::path PathManager::resolveScenePath(const std::string &raw) const
{
    if (raw.empty())
        throw std::invalid_argument("Scene path is empty");
    const fs::path path(separators(raw));
    if (path.is_absolute())
        return path.lexically_normal();
    if (logical(path))
    {
        const auto canonical = fs::path(canonicalizeAssetPath(raw));
        return root / canonical.lexically_relative("assets");
    }
    return (startupBase / path).lexically_normal();
}

std::string PathManager::getFallbackAssetPath(AssetPathType type) const
{
    switch (type)
    {
    case AssetPathType::Texture:
        return "assets/textures/missing.png";
    case AssetPathType::Audio:
        return "assets/audio/missing.wav";
    case AssetPathType::AnimationClip:
        return "assets/animations/missing.animset";
    default:
        return {};
    }
}

ResolvedAssetPath PathManager::resolveAssetPath(const std::string &raw, AssetPathType type, bool required) const
{
    auto canonical = canonicalizeAssetPath(raw);
    auto absolute = root / fs::path(canonical).lexically_relative("assets");
    if (!canonical.empty() && fs::is_regular_file(absolute))
        return {canonical, absolute.string(), false};
    if (required)
        throw std::runtime_error("Required asset not found: '" + raw + "' beneath '" + root.string() + "'");
    canonical = getFallbackAssetPath(type);
    absolute = root / fs::path(canonical).lexically_relative("assets");
    if (canonical.empty() || !fs::is_regular_file(absolute))
        throw std::runtime_error("Fallback asset is missing for '" + raw + "': " + absolute.string());
    const auto key = std::to_string(static_cast<int>(type)) + ':' + raw;
    if (warnedFallbacks.insert(key).second)
        std::fprintf(stderr, "Optional asset '%s' uses fallback '%s'\n", raw.c_str(), canonical.c_str());
    return {canonical, absolute.string(), true};
}
