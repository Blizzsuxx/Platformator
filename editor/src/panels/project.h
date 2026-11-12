#pragma once

#include <filesystem>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <oneapi/tbb/task_group.h>

#include <SDL3/SDL.h>

#include "../project.h"
#include "../animationeditor.h"
#include "../accessibility.h"
#include "../editcommands.h"
#include "../projectchanges.h"
#include "../thumbnailcache.h"
#include "../templates.h"
#include "animation.h"

namespace platformator_editor
{
class SelectionModel;
struct Selection;

struct ProjectBrowserViewState
{
    float folderTreeWidth = 190.0f;
    float thumbnailScale = 0.5f;
    std::filesystem::path contextTargetPath;
    char renameBuffer[256] = {0};
    bool renaming = false;
    bool renameFocusPending = false;
    std::filesystem::path renamingPath;
    bool confirmDeletePending = false;
    std::filesystem::path pendingDeletePath;
};
using ProjectViewState = ProjectBrowserViewState;

class ProjectPanel
{
    friend struct ProjectPanelTestProbe;

  public:
    static constexpr std::size_t MaxOwnerChangePathsPerFrame = 256;
    static constexpr std::chrono::milliseconds MaxOwnerChangeCommitTime{2};
    using ResourceChangeHandler = std::function<void(const ProjectChangeEvent &,
                                                     const std::filesystem::path &)>;

    ProjectPanel(ProjectContext &context, ProjectChanges &changes,
                 ProjectScanService &scans, SDL_Renderer *renderer);
    ~ProjectPanel();
    void draw(DocumentSession &document, EditCommandService &commands,
              SelectionModel *selection = nullptr);
    std::vector<SemanticNode> semantic_nodes(const DocumentSession &document,
                                             const Selection &selection,
                                             std::string_view scope);
    bool dispatch_semantic_action(DocumentSession &document, EditCommandService &commands,
                                  SelectionModel &selection, const SemanticAction &action,
                                  WindowHostGeneration hostGeneration, bool allowEdits);
    void refresh();
    std::size_t apply_changes(const ProjectChangeBatch &batch,
                              const ResourceChangeHandler &resourceChange = {});
    std::size_t pending_change_paths() const noexcept;
    void set_renderer(SDL_Renderer *renderer) noexcept;
    void set_frame_renderer(SDL_Renderer *renderer) noexcept;
    void release_renderer(SDL_Renderer *renderer) noexcept;
    // Commit at most one ready thumbnail upload on the renderer's owning thread,
    // even when the Project panel is hidden or collapsed.
    void service_owner_work(SDL_Renderer *renderer);
    void set_wake_callback(std::function<void()> callback);
    void set_open_animation_callback(std::function<void(const std::filesystem::path &)> callback);
    AnimationPanel::PreparedTextureInfo prepared_animation_texture(
        const std::filesystem::path &path);
    void set_animation_draft_preview(const std::filesystem::path &assetPath,
                                    const std::filesystem::path &assetRoot,
                                    const Json &document, std::uint64_t historyToken,
                                    std::optional<std::uint64_t> savedHistoryToken);
    void clear_animation_draft_preview();
    void end_frame() noexcept;
    std::optional<std::chrono::steady_clock::time_point> next_animation_deadline() const noexcept;
    bool take_reference_retry_request() noexcept;
    void set_reference_retry_status(std::size_t scheduled);
    const ProjectBrowserViewState &view_state() const noexcept { return viewState_; }
    ProjectBrowserViewState &view_state() noexcept { return viewState_; }
    const std::filesystem::path &selected_directory() const noexcept { return selectedDirectory_; }
    const std::optional<std::filesystem::path> &selected_entry() const noexcept { return selectedEntry_; }
    bool execute_context_create(std::string_view itemType, const std::filesystem::path &targetDir);
    bool execute_context_delete(const std::filesystem::path &targetPath);
    bool execute_context_duplicate(const std::filesystem::path &targetPath);
    bool execute_context_rename(const std::filesystem::path &targetPath, const std::string &newName);
    bool reveal_in_file_manager(const std::filesystem::path &targetPath);
    const std::string &last_revealed_url() const noexcept { return lastRevealedUrl_; }

  private:
    struct ListedEntry
    {
        std::filesystem::path path;
        bool isDirectory = false;
    };

    struct AssetAssignmentTarget
    {
        PropertyAddress address;
        std::string expectedAssetType;
        std::string label;
    };

    struct SemanticProjectTarget
    {
        enum class Operation
        {
            Tile,
            LoadTemplate,
            InstantiateTemplate,
            AssignTemplateTarget
        };

        std::filesystem::path path;
        std::filesystem::path root;
        std::string rootLabel;
        ProjectGeneration projectGeneration;
        DocumentIdentity documentIdentity;
        std::uint64_t structureRevision = 0;
        std::uint64_t valueRevision = 0;
        bool directory = false;
        bool behaviorRoot = false;
        std::optional<AssetAssignmentTarget> assignment;
        Operation operation = Operation::Tile;
        std::string externalAssignmentKey;
        std::string externalTargetKind;
        int externalTargetId = -1;
    };

    struct DirectoryState
    {
        ProjectScanToken request;
        DirectoryListingToken changeRequest;
        std::filesystem::path canonicalRoot;
        std::vector<std::filesystem::path> aliasAncestors;
        std::vector<std::filesystem::path> unresolvedTargets;
        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> files;
        std::vector<ListedEntry> entries;
        bool entriesValid = false;
        bool entriesBehaviorFilter = false;
        std::uint64_t entriesRevision = 0;
        std::uint64_t filteredEntriesRevision = 0;
        std::string filteredQuery;
        std::vector<std::size_t> filteredEntryIndexes;
        std::string error;
        bool loaded = false;
        bool hasCompleteSnapshot = false;
        bool truncated = false;
        RecoveryRetrySchedule retry;
    };

    struct AnimationFramePreview
    {
        std::filesystem::path texturePath;
        float sourceX = 0.0f;
        float sourceY = 0.0f;
        float sourceWidth = 0.0f;
        float sourceHeight = 0.0f;
        bool hasSourceRect = false;
        double durationSeconds = 0.0;
    };

    struct AnimationPreview
    {
        std::vector<AnimationFramePreview> frames;
        double totalDuration = 0.0;
        std::vector<double> frameEndTimes;
        bool loop = true;
        bool loaded = false;
        std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
    };

    struct PreparedAnimation
    {
        std::string key;
        ProjectGeneration projectGeneration;
        std::uint64_t cacheGeneration = 0;
        std::uint64_t revision = 0;
        AnimationPreview preview;
        bool success = false;
        std::string error;
    };

    struct PresentationDependency
    {
        std::filesystem::path canonicalPath;
        std::vector<std::filesystem::path> aliasAncestors;
        std::vector<std::filesystem::path> unresolvedTargets;
    };

    struct PendingProjectChange
    {
        ProjectChangeEvent event;
        std::size_t nextPath = 0;
    };

    void request_scan(const std::filesystem::path &directory);
    void draw_folder_tree(const std::filesystem::path &root,
                          const std::filesystem::path &directory,
                          const std::string &rootLabel, std::size_t depth);
    void draw_contents(const std::filesystem::path &root, const std::string &rootLabel,
                       bool behaviorRoot);
    void apply_ready_results();
    void apply_animation_results();
    void remember_directory_dependency(DirectoryState &state,
                                       const std::filesystem::path &logicalPath,
                                       const std::filesystem::path &assetRoot);
    void remember_presentation_dependency(const std::filesystem::path &logicalPath,
                                         const std::filesystem::path &assetRoot);
    void draw_asset_tile(const std::filesystem::path &path, const std::filesystem::path &assetRoot,
                         bool isDirectory, bool behaviorRoot, float tileWidth, float tileHeight);
    void draw_asset_list_row(const std::filesystem::path &path, const std::filesystem::path &assetRoot,
                             bool isDirectory, bool behaviorRoot, float rowHeight);
    std::vector<AssetAssignmentTarget> asset_assignment_targets(
        const DocumentSession &document, const SelectionModel &selection,
        const std::filesystem::path &assetPath) const;
    bool assign_asset_target(DocumentSession &document, EditCommandService &commands,
                             const AssetAssignmentTarget &target,
                             const std::filesystem::path &assetPath, std::string &error);
    void draw_template_controls(DocumentSession &document, EditCommandService &commands,
                                SelectionModel *selection);
    void load_selected_template(const DocumentSession &document);
    void draw_sprite_region_editor(DocumentSession &document, EditCommandService &commands,
                                   const AssetAssignmentTarget &target,
                                   const std::filesystem::path &assetPath);
    void service_template_state(DocumentSession &document, EditCommandService &commands,
                                 SelectionModel *selection);
    void draw_assignment_controls(DocumentSession &document, EditCommandService &commands,
                                  SelectionModel *selection);
    SDL_Texture *thumbnail_for(const std::filesystem::path &path);
    AnimationPreview convert_animation_preview(const AnimationPreviewDocument &source) const;
    const AnimationPreview &animation_for(const std::filesystem::path &path,
                                         const std::filesystem::path &assetRoot);
    void release_preview_resources() noexcept;
    void request_animation_preparation(const std::filesystem::path &path,
                                       const std::filesystem::path &assetRoot,
                                       const std::string &key);

    ProjectContext &context_;
    ProjectChanges &changes_;
    ProjectScanService &scans_;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Renderer *frameRenderer_ = nullptr;
    ThumbnailCache thumbnails_;
    std::map<std::string, AnimationPreview> animations_;
    std::set<std::string> staleAnimations_;
    std::set<std::string> failedAnimationRefreshes_;
    std::map<std::string, std::uint64_t> animationRevisions_;
    std::set<std::string> animationsInFlight_;
    std::deque<PreparedAnimation> preparedAnimations_;
    oneapi::tbb::task_group animationTasks_;
    mutable std::mutex animationMutex_;
    std::uint64_t animationCacheGeneration_ = 1;
    std::optional<std::string> animationDraftKey_;
    std::uint64_t animationDraftHistoryToken_ = 0;
    std::optional<std::uint64_t> animationDraftSavedToken_;
    std::optional<std::chrono::steady_clock::time_point> nextAnimationDeadline_;
    bool ownerUploadPollDoneThisFrame_ = false;
    std::function<void()> wakeCallback_;
    std::map<std::string, PresentationDependency> presentationDependencies_;
    bool presentationDependenciesSaturated_ = false;
    std::map<std::string, DirectoryState> directories_;
    std::map<std::uint64_t, std::string> requestPaths_;
    std::filesystem::path assetRoot_;
    std::filesystem::path projectRoot_;
    std::filesystem::path selectedDirectory_;
    std::filesystem::path selectedRoot_;
    std::string selectedRootLabel_ = "Assets";
    std::optional<std::filesystem::path> selectedEntry_;
    std::set<std::string> expandedDirectories_;
    std::deque<PendingProjectChange> pendingChanges_;
    std::uint64_t lastProjectGeneration_ = 0;
    std::size_t pendingChangePathCount_ = 0;
    bool rootRecoveryActive_ = false;
    bool referenceRetryRequested_ = false;
    std::string coverageMessage_ = "Monitoring asset changes.";
    std::string referenceRetryMessage_;
    std::string searchQuery_;
    std::string selectedAssignmentTarget_;
    std::string assignmentFeedback_;
    bool sourceRegionDragging_ = false;
    float sourceRegionDragStartX_ = 0.0f;
    float sourceRegionDragStartY_ = 0.0f;
    DocumentIdentity sourceRegionDragIdentity_;
    std::filesystem::path sourceRegionDragAsset_;
    std::optional<TemplateAssetV1> selectedTemplate_;
    std::filesystem::path selectedTemplatePath_;
    std::string templateFeedback_;
    std::map<std::string, int> externalTemplateTargets_;
    std::map<std::string, std::vector<std::pair<int, std::string>>> templateTargetCandidates_;
    std::uint64_t templateTargetsStructureRevision_ = 0;
    std::uint64_t templateTargetsValueRevision_ = 0;
    bool templateCanInstantiate_ = false;
    bool pendingTemplateInstantiate_ = false;
    DocumentIdentity templateAssignmentIdentity_;
    bool assetDoubleClickPending_ = false;
    std::function<void(const std::filesystem::path &)> openAnimationCallback_;
    std::vector<SemanticNode> pendingSemanticNodes_;
    std::map<std::string, SemanticProjectTarget> pendingTileTargets_;
    std::map<std::string, SemanticProjectTarget> pendingSemanticTargets_;
    std::map<std::string, SemanticProjectTarget> semanticTargets_;
    std::optional<std::string> semanticFocusNode_;
    ProjectBrowserViewState viewState_{};
    std::string lastRevealedUrl_;
};
} // namespace platformator_editor
