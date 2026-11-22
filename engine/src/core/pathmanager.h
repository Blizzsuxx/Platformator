#pragma once

#include <string>
#include <filesystem>

struct ResolvedAssetPath
{
    std::string canonicalPath;
    std::string absolutePath;
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
    ResolvedAssetPath resolveAssetPath(const std::string &rawPath) const;

  private:
    std::filesystem::path root, startupBase;
};
