#pragma once

#include "editcommands.h"
#include "assetdocument.h"
#include "panels/animation.h"
#include "sessionids.h"
#include "templates.h"
#include "workspace.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace platformator_editor
{
struct LayerSettings;
struct SceneViewState
{
    float centerX = 0.0f;
    float centerY = 0.0f;
    float zoom = 1.0f;
};

enum class DocumentTabKind
{
    Scene,
    Animation,
    Template
};

struct DocumentSourceStatus
{
    TabId tabId;
    SourceFileStatus file;
    DocumentIdentity identity;
    DocumentTabKind kind = DocumentTabKind::Scene;
};

struct DocumentSourceOwner
{
    TabId tabId;
    DocumentTabKind kind = DocumentTabKind::Scene;
};

struct SceneLaunchSnapshot
{
    DocumentIdentity identity;
    std::filesystem::path sourcePath;
    Json scene = Json::array();
    std::uint64_t savedHistoryToken = 0;
    std::string savedFileIdentity;
};

struct SaveAllResult
{
    WorkspaceActionResult action;
    std::vector<TabId> savedTabs;
    std::optional<TabId> failedTab;
};

struct TemplateRetargetFailure
{
    std::filesystem::path path;
    std::string message;
};

struct TemplateRetargetResult
{
    std::size_t updatedCount = 0;
    std::vector<TemplateRetargetFailure> failures;
    bool recoveryStored = true;

    bool succeeded() const noexcept { return failures.empty() && recoveryStored; }
    operator std::size_t() const noexcept { return updatedCount; }
};

class DocumentTab
{
  public:
    DocumentTab(TabId id, ProjectGeneration projectGeneration,
                const std::filesystem::path &projectRoot,
                const std::filesystem::path &assetRoot);

    TabId id() const noexcept;
    DocumentIdentity identity() const noexcept;
    DocumentTabKind kind() const noexcept;
    void set_kind(DocumentTabKind kind) noexcept { kind_ = kind; }
    const std::filesystem::path &template_path() const noexcept { return templatePath_; }
    void set_template_path(std::filesystem::path path) { templatePath_ = std::move(path); }
    const nlohmann::json &template_reference_bindings() const noexcept;
    void set_template_reference_bindings(nlohmann::json bindings);
    EditCommandService &history() noexcept;
    const EditCommandService &history() const noexcept;
    DocumentWorkspace &workspace() noexcept;
    const DocumentWorkspace &workspace() const noexcept;
    SelectionModel &selection() noexcept;
    const SelectionModel &selection() const noexcept;
    SceneViewState &view_state() noexcept;
    const SceneViewState &view_state() const noexcept;
    std::vector<int> &expanded_hierarchy_ids() noexcept;
    const std::vector<int> &expanded_hierarchy_ids() const noexcept;
    bool has_expansion_state() const noexcept;
    void set_expanded_hierarchy_ids(std::vector<int> ids);
    void acknowledge_external_change();
    void clear_external_change_acknowledgement() noexcept;
    bool external_change_acknowledged(const SourceFileStatus &status) const noexcept;
    bool source_file_allows_run(const SourceFileStatus &status) const noexcept;

  private:
    TabId id_;
    DocumentTabKind kind_ = DocumentTabKind::Scene;
    // Workspace borrows history. Declaration order makes it destruct first.
    EditCommandService history_;
    DocumentWorkspace workspace_;
    SelectionModel selection_;
    SceneViewState viewState_;
    std::vector<int> expandedHierarchyIds_;
    std::filesystem::path templatePath_;
    bool hasExpansionState_ = false;
    bool externalChangeAcknowledged_ = false;
    std::optional<FileIdentity> acknowledgedExternalIdentity_;
};

class AnimationDocumentTab
{
  public:
    AnimationDocumentTab(TabId id, ProjectGeneration projectGeneration,
                         AssetDocument document,
                         std::filesystem::path projectRoot,
                         std::filesystem::path assetRoot);
    TabId id() const noexcept { return id_; }
    DocumentTabKind kind() const noexcept { return DocumentTabKind::Animation; }
    DocumentIdentity identity() const noexcept
    {
        return {projectGeneration_, id_, DocumentGeneration{document_.generation()}};
    }
    AssetDocument &document() noexcept { return document_; }
    const AssetDocument &document() const noexcept { return document_; }
    AnimationPanel &panel() noexcept { return panel_; }
    const AnimationPanel &panel() const noexcept { return panel_; }
    const std::filesystem::path &project_root() const noexcept { return projectRoot_; }
    const std::filesystem::path &asset_root() const noexcept { return assetRoot_; }
    void acknowledge_external_change(const SourceFileStatus &status) noexcept;
    void clear_external_change_acknowledgement() noexcept;
    bool external_change_acknowledged(const SourceFileStatus &status) const noexcept;
    void commit_project_roots(std::filesystem::path projectRoot,
                              std::filesystem::path assetRoot,
                              ProjectGeneration projectGeneration) noexcept
    {
        projectRoot_.swap(projectRoot);
        assetRoot_.swap(assetRoot);
        projectGeneration_ = projectGeneration;
    }

  private:
    TabId id_;
    ProjectGeneration projectGeneration_;
    AssetDocument document_;
    AnimationPanel panel_;
    std::filesystem::path projectRoot_;
    std::filesystem::path assetRoot_;
    bool externalChangeAcknowledged_ = false;
    std::optional<FileIdentity> acknowledgedExternalIdentity_;
};

class DocumentManager
{
  public:
    using TemplateBindingCollector = std::function<TemplateBindingCollection(
        const DocumentSession &, const Json &, const Json &)>;

    DocumentManager(std::filesystem::path projectRoot,
                    std::filesystem::path assetRoot,
                    ProjectGeneration projectGeneration = ProjectGeneration{1});

    TabId active_tab_id() const noexcept;
    DocumentTab *active_tab() noexcept;
    const DocumentTab *active_tab() const noexcept;
    DocumentTab *find(TabId id) noexcept;
    const DocumentTab *find(TabId id) const noexcept;
    std::vector<TabId> tab_ids() const;
    std::vector<TabId> animation_tab_ids() const;
    std::size_t tab_count() const noexcept;
    std::size_t animation_tab_count() const noexcept;
    bool has_unsaved_changes() const noexcept;
    TabId active_animation_tab_id() const noexcept;
    AnimationDocumentTab *active_animation_tab() noexcept;
    const AnimationDocumentTab *active_animation_tab() const noexcept;
    AnimationDocumentTab *find_animation_tab(TabId id) noexcept;
    const AnimationDocumentTab *find_animation_tab(TabId id) const noexcept;
    std::vector<DocumentSourceStatus> source_file_statuses() const;
    std::optional<DocumentSourceOwner> source_owner_for_destination(
        const std::filesystem::path &path, std::optional<TabId> requestingTab = std::nullopt,
        std::string *error = nullptr) const;
    std::optional<SceneLaunchSnapshot> capture_active_scene_launch_snapshot() const;
    TabId create_scene_tab();
    std::optional<TabId> open_scene(const std::filesystem::path &path,
                                   std::string *error = nullptr);
    std::optional<TabId> open_scene_reusing_empty_tab(const std::filesystem::path &path,
                                                      std::string *error = nullptr);
    std::optional<TabId> open_template(const std::filesystem::path &path,
                                      std::string *error = nullptr);
    bool save_template_tab(TabId id, std::string *error = nullptr,
                           bool allowExternalOverwrite = false,
                           std::optional<FileIdentity> expectedExternalIdentity = std::nullopt);
    bool revert_template_tab(TabId id, std::string *error = nullptr);
    bool reload_template_tab(TabId id, std::string *error = nullptr);
    std::optional<TabId> open_animation(const std::filesystem::path &path,
                                        std::string *error = nullptr);
    WorkspaceActionResult save_scene_tab_as(const DocumentIdentity &identity,
                                            const std::filesystem::path &path,
                                            bool allowOverwrite = false);
    bool activate_animation_tab(TabId id) noexcept;
    WorkspaceActionResult save_animation_tab(TabId id);
    WorkspaceActionResult save_animation_tab(
        const DocumentIdentity &identity,
        std::optional<AssetOverwriteObservation> overwrite = std::nullopt);
    WorkspaceActionResult save_animation_tab_as(
        const DocumentIdentity &identity, const std::filesystem::path &path,
        std::optional<AssetOverwriteObservation> overwrite = std::nullopt);
    WorkspaceActionResult reload_animation_tab(const DocumentIdentity &identity);
    WorkspaceActionResult close_animation_tab(TabId id, UnsavedDecision decision);
    bool activate_tab(TabId id) noexcept;
    SaveAllResult save_all();
    WorkspaceActionResult close_tab(TabId id, UnsavedDecision decision);
    WorkspaceActionResult set_project_roots(const std::filesystem::path &projectRoot,
                                            const std::filesystem::path &assetRoot,
                                            ProjectGeneration generation,
                                            bool discardDirtyDocuments = false);
    std::vector<TabId> sync_layer_settings(const LayerSettings &settings,
                                           bool recordUndo = true);
    std::uint32_t collision_group_layers_in_use() const noexcept;
    std::size_t reconcile_template_asset(const std::filesystem::path &path);
    TemplateRetargetResult retarget_template_asset_links(const std::filesystem::path &oldPath,
                                                         const std::filesystem::path &newPath,
                                                         bool directory);
    TemplateRetargetResult retry_pending_template_asset_links();
    void set_template_binding_collector(TemplateBindingCollector collector)
    {
        templateBindingCollector_ = std::move(collector);
    }

  private:
    std::filesystem::path projectRoot_;
    std::filesystem::path assetRoot_;
    ProjectGeneration projectGeneration_;
    MonotonicIdentitySource<TabId> tabIds_;
    std::vector<std::unique_ptr<DocumentTab>> tabs_;
    std::vector<std::unique_ptr<AnimationDocumentTab>> animationTabs_;
    TabId activeTab_;
    TabId activeAnimationTab_;
    TemplateBindingCollector templateBindingCollector_;

    std::unique_ptr<DocumentTab> make_tab();
    void deactivate_active_animation_panel() noexcept;
    static std::optional<std::filesystem::path> canonical_scene_path(
        const std::filesystem::path &path, std::string *error);
    static std::optional<std::filesystem::path> canonical_animation_path(
        const std::filesystem::path &path, std::string *error);
};
} // namespace platformator_editor
