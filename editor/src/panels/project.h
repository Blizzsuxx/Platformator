#pragma once

#include <filesystem>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "../project.h"
#include "../editcommands.h"

namespace platformator_editor
{
class ProjectPanel
{
  public:
    ProjectPanel(ProjectContext &context, ProjectScanService &scans, SDL_Renderer *renderer);
    ~ProjectPanel();
    void draw(DocumentSession &document, EditCommandService &commands);
    void refresh();
    void set_renderer(SDL_Renderer *renderer) noexcept;

  private:
    struct DirectoryState
    {
        ProjectScanToken request;
        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> files;
        std::string error;
        bool loaded = false;
        bool truncated = false;
    };

    struct AnimationFramePreview
    {
        std::filesystem::path texturePath;
        float sourceX = 0.0f;
        float sourceY = 0.0f;
        float sourceWidth = 0.0f;
        float sourceHeight = 0.0f;
        bool hasSourceRect = false;
    };

    struct AnimationPreview
    {
        std::vector<AnimationFramePreview> frames;
        float framesPerSecond = 1.0f;
        bool loop = true;
        bool loaded = false;
        std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
    };

    void request_scan(const std::filesystem::path &directory);
    void draw_folder_tree(const std::filesystem::path &root,
                          const std::filesystem::path &directory,
                          const std::string &rootLabel, std::size_t depth);
    void draw_contents(const std::filesystem::path &root, const std::string &rootLabel,
                       bool behaviorRoot);
    void apply_ready_results();
    void draw_asset_tile(const std::filesystem::path &path, const std::filesystem::path &assetRoot,
                         bool isDirectory, bool behaviorRoot, float tileWidth, float tileHeight);
    SDL_Texture *thumbnail_for(const std::filesystem::path &path);
    const AnimationPreview &animation_for(const std::filesystem::path &path,
                                         const std::filesystem::path &assetRoot);
    void release_preview_resources() noexcept;

    ProjectContext &context_;
    ProjectScanService &scans_;
    SDL_Renderer *renderer_ = nullptr;
    std::map<std::string, SDL_Texture *> thumbnails_;
    std::set<std::string> unavailableThumbnails_;
    std::map<std::string, AnimationPreview> animations_;
    std::map<std::string, DirectoryState> directories_;
    std::map<std::uint64_t, std::string> requestPaths_;
    std::filesystem::path assetRoot_;
    std::filesystem::path projectRoot_;
    std::filesystem::path selectedDirectory_;
    std::filesystem::path selectedRoot_;
    std::string selectedRootLabel_ = "Assets";
    std::optional<std::filesystem::path> selectedEntry_;
    std::set<std::string> expandedDirectories_;
};
} // namespace platformator_editor
