#pragma once

#include <string>
#include <filesystem>
#include <unordered_set>

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
    explicit PathManager(const std::string &root);
    static PathManager &getInstance();
    const std::filesystem::path &getRoot() const
    {
        return root;
    }
    std::filesystem::path resolveScenePath(const std::string &path) const;

    std::string canonicalizeAssetPath(const std::string &rawPath) const;
    std::string getAssetsRootAbsolutePath() const;
    std::string getFallbackAssetPath(AssetPathType assetPathType) const;
    ResolvedAssetPath resolveAssetPath(const std::string &rawPath, AssetPathType assetPathType,
                                       bool required = false) const;

  private:
    std::filesystem::path root, startupBase;
    mutable std::unordered_set<std::string> warnedFallbacks;
};
