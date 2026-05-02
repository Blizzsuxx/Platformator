#pragma once

#include <string>

enum class AssetPathType
{
    Texture,
    Audio,
    AnimationClip,
    Model,
    Script,
    Generic,
};

struct ResolvedAssetPath
{
    std::string requestedPath;
    std::string canonicalPath;
    std::string absolutePath;
    bool usedFallback;
};

class PathManager
{
public:
    static PathManager &getInstance();

    std::string canonicalizePath(const std::string &rawPath) const;
    std::string canonicalizeAssetPath(const std::string &rawPath) const;
    std::string getAssetsRootAbsolutePath() const;
    std::string getFallbackAssetPath(AssetPathType assetPathType) const;
    ResolvedAssetPath resolveAssetPath(const std::string &rawPath, AssetPathType assetPathType) const;

private:
    PathManager() = default;

    static bool isAbsolutePath(const std::string &path);
    static std::string extractAssetsRelativePath(const std::string &canonicalPath);
    static bool isAssetsRelativePath(const std::string &canonicalPath);

    ResolvedAssetPath makeResolvedPath(const std::string &requestedPath,
                                       const std::string &canonicalPath,
                                       bool usedFallback) const;
    void logFallbackWarning(const std::string &requestedPath,
                            const std::string &reason,
                            const std::string &fallbackPath) const;
};