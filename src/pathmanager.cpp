#include "pathmanager.h"

#include <SDL3/SDL_filesystem.h>

#include "buildconfig.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{
    bool isValidAssetsDirectory(const std::filesystem::path &assetsDirectory)
    {
        static const std::filesystem::path requiredAssetPaths[] = {
            std::filesystem::path("textures") / "missing.png",
            std::filesystem::path("audio") / "missing.wav",
            std::filesystem::path("animations") / "missing.animset",
            std::filesystem::path("models") / "default_cube.obj",
        };

        if (!std::filesystem::is_directory(assetsDirectory))
        {
            return false;
        }

        for (const std::filesystem::path &requiredAssetPath : requiredAssetPaths)
        {
            if (!std::filesystem::is_regular_file(assetsDirectory / requiredAssetPath))
            {
                return false;
            }
        }

        return true;
    }

    std::filesystem::path findAssetsDirectory(std::filesystem::path startDirectory)
    {
        if (startDirectory.empty())
        {
            return {};
        }

        startDirectory = std::filesystem::absolute(startDirectory).lexically_normal();

        while (true)
        {
            const std::filesystem::path assetsDirectory = startDirectory / "assets";
            if (isValidAssetsDirectory(assetsDirectory))
            {
                return assetsDirectory.lexically_normal();
            }

            const std::filesystem::path parentDirectory = startDirectory.parent_path();
            if (parentDirectory.empty() || parentDirectory == startDirectory)
            {
                break;
            }

            startDirectory = parentDirectory;
        }

        return {};
    }

    const std::filesystem::path &getExecutableRuntimeRootDirectory()
    {
        static const std::filesystem::path executableRuntimeRootDirectory = []()
        {
            const char *basePath = SDL_GetBasePath();
            if (basePath == nullptr)
            {
                return std::filesystem::path();
            }

            const std::filesystem::path assetsDirectory = findAssetsDirectory(std::filesystem::path(basePath));
            if (assetsDirectory.empty())
            {
                return std::filesystem::path();
            }

            return assetsDirectory.parent_path().lexically_normal();
        }();

        return executableRuntimeRootDirectory;
    }

    std::filesystem::path getRuntimeRootDirectory()
    {
        const std::filesystem::path &executableRuntimeRootDirectory = getExecutableRuntimeRootDirectory();
        if (!executableRuntimeRootDirectory.empty())
        {
            return executableRuntimeRootDirectory;
        }

        const std::filesystem::path assetsDirectory = findAssetsDirectory(std::filesystem::current_path());
        if (!assetsDirectory.empty())
        {
            return assetsDirectory.parent_path().lexically_normal();
        }

        return std::filesystem::current_path().lexically_normal();
    }

    std::vector<std::string> splitPathComponents(const std::string &path)
    {
        std::vector<std::string> components;
        std::string currentComponent;

        for (char character : path)
        {
            if (character == '/')
            {
                if (!currentComponent.empty())
                {
                    components.push_back(currentComponent);
                    currentComponent.clear();
                }
                continue;
            }

            currentComponent.push_back(character);
        }

        if (!currentComponent.empty())
        {
            components.push_back(currentComponent);
        }

        return components;
    }

    std::string joinPathComponents(const std::vector<std::string> &components, size_t startIndex)
    {
        std::string joinedPath;
        for (size_t index = startIndex; index < components.size(); ++index)
        {
            if (!joinedPath.empty())
            {
                joinedPath += '/';
            }

            joinedPath += components[index];
        }

        return joinedPath;
    }
} // namespace

PathManager &PathManager::getInstance()
{
    static PathManager instance;
    return instance;
}

std::string PathManager::canonicalizePath(const std::string &rawPath) const
{
    if (rawPath.empty())
    {
        return "";
    }

    std::string normalizedPath = rawPath;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(), [](unsigned char character)
                   { return static_cast<char>(std::tolower(character)); });

    normalizedPath = std::filesystem::path(normalizedPath).lexically_normal().generic_string();
    if (normalizedPath == ".")
    {
        return "";
    }

    while (normalizedPath.rfind("./", 0) == 0)
    {
        normalizedPath.erase(0, 2);
    }

    return normalizedPath;
}

std::string PathManager::canonicalizeAssetPath(const std::string &rawPath) const
{
    const std::string canonicalPath = canonicalizePath(rawPath);
    if (canonicalPath.empty())
    {
        return "";
    }

    std::string assetRelativePath = extractAssetsRelativePath(canonicalPath);
    if (assetRelativePath.empty())
    {
        if (isAbsolutePath(canonicalPath))
        {
            return "";
        }

        assetRelativePath = canonicalizePath(std::string("assets/") + canonicalPath);
    }

    if (!isAssetsRelativePath(assetRelativePath))
    {
        return "";
    }

    return assetRelativePath;
}

std::string PathManager::getAssetsRootAbsolutePath() const
{
    return (getRuntimeRootDirectory() / "assets").lexically_normal().generic_string();
}

std::string PathManager::getFallbackAssetPath(AssetPathType assetPathType) const
{
    switch (assetPathType)
    {
    case AssetPathType::Texture:
        return "assets/textures/missing.png";
    case AssetPathType::Audio:
        return "assets/audio/missing.wav";
    case AssetPathType::AnimationClip:
        return "assets/animations/missing.animset";
    case AssetPathType::Model:
        return "assets/models/default_cube.obj";
    case AssetPathType::Script:
        return "assets/scripts/missing.lua";
    case AssetPathType::Generic:
        return "assets/scripts/missing.lua";
    }

    return "assets/scripts/missing.lua";
}

ResolvedAssetPath PathManager::resolveAssetPath(const std::string &rawPath, AssetPathType assetPathType) const
{
    const std::string canonicalPath = canonicalizeAssetPath(rawPath);
    if (!canonicalPath.empty())
    {
        ResolvedAssetPath resolvedPath = makeResolvedPath(rawPath, canonicalPath, false);
        if (std::filesystem::is_regular_file(resolvedPath.absolutePath))
        {
            return resolvedPath;
        }

        const std::string fallbackPath = getFallbackAssetPath(assetPathType);
        logFallbackWarning(rawPath, "asset-not-found", fallbackPath);
        return makeResolvedPath(rawPath, fallbackPath, true);
    }

    const std::string fallbackPath = getFallbackAssetPath(assetPathType);
    logFallbackWarning(rawPath, "rejected-non-assets-path", fallbackPath);
    return makeResolvedPath(rawPath, fallbackPath, true);
}

bool PathManager::isAbsolutePath(const std::string &path)
{
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' && path[2] == '/')
    {
        return true;
    }

    return std::filesystem::path(path).is_absolute();
}

std::string PathManager::extractAssetsRelativePath(const std::string &canonicalPath)
{
    const std::vector<std::string> components = splitPathComponents(canonicalPath);
    for (size_t index = 0; index < components.size(); ++index)
    {
        if (components[index] == "assets")
        {
            return joinPathComponents(components, index);
        }
    }

    return "";
}

bool PathManager::isAssetsRelativePath(const std::string &canonicalPath)
{
    return canonicalPath == "assets" || canonicalPath.rfind("assets/", 0) == 0;
}

ResolvedAssetPath PathManager::makeResolvedPath(const std::string &requestedPath,
                                                const std::string &canonicalPath,
                                                bool usedFallback) const
{
    return ResolvedAssetPath{requestedPath,
                             canonicalPath,
                             (getRuntimeRootDirectory() / canonicalPath).lexically_normal().generic_string(),
                             usedFallback};
}

void PathManager::logFallbackWarning(const std::string &requestedPath,
                                     const std::string &reason,
                                     const std::string &fallbackPath) const
{
    PLATFORMATOR_LOG("[PathManager][Warning] requested='%s' reason='%s' fallback='%s'\n",
                     requestedPath.c_str(),
                     reason.c_str(),
                     fallbackPath.c_str());
}