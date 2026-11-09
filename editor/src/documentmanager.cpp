#include "documentmanager.h"
#include "assetformats.h"
#include "animationeditor.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace platformator_editor
{
namespace
{
bool path_is_within(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    if (relative.is_absolute())
        return false;
    return std::none_of(relative.begin(), relative.end(), [](const auto &part) {
        return part == "..";
    });
}

std::optional<std::filesystem::path> normalized_destination_path(
    const std::filesystem::path &path, std::string &error)
{
    if (path.empty())
    {
        error = "Save As destination is empty";
        return std::nullopt;
    }
    std::error_code filesystemError;
    auto normalized = std::filesystem::weakly_canonical(path, filesystemError);
    if (filesystemError)
    {
        error = "could not resolve Save As destination: " + filesystemError.message();
        return std::nullopt;
    }
    return normalized;
}

const char *document_kind_name(DocumentTabKind kind) noexcept
{
    return kind == DocumentTabKind::Scene ? "Scene" : "animation";
}
} // namespace

DocumentTab::DocumentTab(TabId id, ProjectGeneration projectGeneration,
                         const std::filesystem::path &projectRoot,
                         const std::filesystem::path &assetRoot)
    : id_(id), history_(),
      workspace_(projectRoot, assetRoot, projectGeneration.value, id)
{
    workspace_.bind_history(history_);
    history_.set_selection_provider([this] { return selection_.current(); });
}

TabId DocumentTab::id() const noexcept { return id_; }

DocumentIdentity DocumentTab::identity() const noexcept
{
    const auto &document = workspace_.document();
    return {ProjectGeneration{document.projectGeneration}, id_,
            DocumentGeneration{document.documentGeneration}};
}

DocumentTabKind DocumentTab::kind() const noexcept { return kind_; }

EditCommandService &DocumentTab::history() noexcept { return history_; }
const EditCommandService &DocumentTab::history() const noexcept { return history_; }
DocumentWorkspace &DocumentTab::workspace() noexcept { return workspace_; }
const DocumentWorkspace &DocumentTab::workspace() const noexcept { return workspace_; }
SelectionModel &DocumentTab::selection() noexcept { return selection_; }
const SelectionModel &DocumentTab::selection() const noexcept { return selection_; }
SceneViewState &DocumentTab::view_state() noexcept { return viewState_; }
const SceneViewState &DocumentTab::view_state() const noexcept { return viewState_; }
std::vector<int> &DocumentTab::expanded_hierarchy_ids() noexcept { return expandedHierarchyIds_; }
const std::vector<int> &DocumentTab::expanded_hierarchy_ids() const noexcept
{
    return expandedHierarchyIds_;
}
bool DocumentTab::has_expansion_state() const noexcept { return hasExpansionState_; }
void DocumentTab::set_expanded_hierarchy_ids(std::vector<int> ids)
{
    expandedHierarchyIds_ = std::move(ids);
    hasExpansionState_ = true;
}

void DocumentTab::acknowledge_external_change()
{
    const auto status = workspace_.source_file_status();
    externalChangeAcknowledged_ = true;
    acknowledgedExternalIdentity_ = status.diskIdentity;
}

void DocumentTab::clear_external_change_acknowledgement() noexcept
{
    externalChangeAcknowledged_ = false;
    acknowledgedExternalIdentity_.reset();
}

bool DocumentTab::external_change_acknowledged(const SourceFileStatus &status) const noexcept
{
    return externalChangeAcknowledged_ && acknowledgedExternalIdentity_ == status.diskIdentity;
}

bool DocumentTab::source_file_allows_run(const SourceFileStatus &status) const noexcept
{
    return platformator_editor::source_file_allows_run(
        status, externalChangeAcknowledged_, acknowledgedExternalIdentity_);
}

AnimationDocumentTab::AnimationDocumentTab(TabId id, ProjectGeneration projectGeneration,
                                           AssetDocument document,
                                           std::filesystem::path projectRoot,
                                           std::filesystem::path assetRoot)
    : id_(id), projectGeneration_(projectGeneration), document_(std::move(document)),
      projectRoot_(std::move(projectRoot)),
      assetRoot_(std::move(assetRoot))
{
}

void AnimationDocumentTab::acknowledge_external_change(const SourceFileStatus &status) noexcept
{
    externalChangeAcknowledged_ = true;
    acknowledgedExternalIdentity_ = status.diskIdentity;
}

void AnimationDocumentTab::clear_external_change_acknowledgement() noexcept
{
    externalChangeAcknowledged_ = false;
    acknowledgedExternalIdentity_.reset();
}

bool AnimationDocumentTab::external_change_acknowledged(
    const SourceFileStatus &status) const noexcept
{
    return externalChangeAcknowledged_ &&
           (status.state == SourceFileState::Changed || status.state == SourceFileState::Missing) &&
           acknowledgedExternalIdentity_ == status.diskIdentity;
}

DocumentManager::DocumentManager(std::filesystem::path projectRoot,
                                 std::filesystem::path assetRoot,
                                 ProjectGeneration projectGeneration)
    : projectRoot_(std::move(projectRoot)), assetRoot_(std::move(assetRoot)),
      projectGeneration_(projectGeneration)
{
    if (!projectGeneration_)
        throw std::invalid_argument("project generation must be nonzero");
    auto initial = make_tab();
    activeTab_ = initial->id();
    tabs_.push_back(std::move(initial));
}

TabId DocumentManager::active_tab_id() const noexcept { return activeTab_; }

DocumentTab *DocumentManager::active_tab() noexcept { return find(activeTab_); }
const DocumentTab *DocumentManager::active_tab() const noexcept { return find(activeTab_); }

DocumentTab *DocumentManager::find(TabId id) noexcept
{
    const auto found = std::find_if(tabs_.begin(), tabs_.end(),
                                    [id](const auto &tab) { return tab->id() == id; });
    return found == tabs_.end() ? nullptr : found->get();
}

const DocumentTab *DocumentManager::find(TabId id) const noexcept
{
    const auto found = std::find_if(tabs_.begin(), tabs_.end(),
                                    [id](const auto &tab) { return tab->id() == id; });
    return found == tabs_.end() ? nullptr : found->get();
}

std::vector<TabId> DocumentManager::tab_ids() const
{
    std::vector<TabId> result;
    result.reserve(tabs_.size());
    for (const auto &tab : tabs_)
        result.push_back(tab->id());
    return result;
}

std::vector<TabId> DocumentManager::animation_tab_ids() const
{
    std::vector<TabId> result;
    result.reserve(animationTabs_.size());
    for (const auto &tab : animationTabs_)
        result.push_back(tab->id());
    return result;
}

std::size_t DocumentManager::tab_count() const noexcept { return tabs_.size(); }
std::size_t DocumentManager::animation_tab_count() const noexcept { return animationTabs_.size(); }
TabId DocumentManager::active_animation_tab_id() const noexcept { return activeAnimationTab_; }

void DocumentManager::deactivate_active_animation_panel() noexcept
{
    if (auto *tab = find_animation_tab(activeAnimationTab_))
    {
        tab->panel().cancel_numeric_drafts();
        tab->panel().set_playing(false);
    }
}

AnimationDocumentTab *DocumentManager::active_animation_tab() noexcept
{
    return find_animation_tab(activeAnimationTab_);
}

const AnimationDocumentTab *DocumentManager::active_animation_tab() const noexcept
{
    return find_animation_tab(activeAnimationTab_);
}

AnimationDocumentTab *DocumentManager::find_animation_tab(TabId id) noexcept
{
    const auto found = std::find_if(animationTabs_.begin(), animationTabs_.end(),
        [id](const auto &tab) { return tab->id() == id; });
    return found == animationTabs_.end() ? nullptr : found->get();
}

const AnimationDocumentTab *DocumentManager::find_animation_tab(TabId id) const noexcept
{
    const auto found = std::find_if(animationTabs_.begin(), animationTabs_.end(),
        [id](const auto &tab) { return tab->id() == id; });
    return found == animationTabs_.end() ? nullptr : found->get();
}

std::vector<DocumentSourceStatus> DocumentManager::source_file_statuses() const
{
    std::vector<DocumentSourceStatus> result;
    result.reserve(tabs_.size() + animationTabs_.size());
    for (const auto &tab : tabs_)
        result.push_back({tab->id(), tab->workspace().source_file_status(),
                          tab->identity(), DocumentTabKind::Scene});
    for (const auto &tab : animationTabs_)
        result.push_back({tab->id(), tab->document().source_file_status(),
                          tab->identity(), DocumentTabKind::Animation});
    return result;
}

std::optional<DocumentSourceOwner> DocumentManager::source_owner_for_destination(
    const std::filesystem::path &path, std::optional<TabId> requestingTab,
    std::string *error) const
{
    std::string resolutionError;
    const auto destination = normalized_destination_path(path, resolutionError);
    if (!destination)
    {
        if (error != nullptr)
            *error = std::move(resolutionError);
        return std::nullopt;
    }

    const auto matches = [&](TabId id, DocumentTabKind kind,
                             const std::optional<std::filesystem::path> &source) {
        if (!source || (requestingTab && id == *requestingTab))
            return false;
        std::error_code filesystemError;
        const auto owned = std::filesystem::weakly_canonical(*source, filesystemError);
        if (filesystemError)
        {
            resolutionError = "could not resolve an open document source: " +
                              filesystemError.message();
            return false;
        }
        if (owned != *destination)
            return false;
        return true;
    };

    for (const auto &tab : tabs_)
    {
        if (matches(tab->id(), DocumentTabKind::Scene,
                    tab->workspace().document().sourcePath))
            return DocumentSourceOwner{tab->id(), DocumentTabKind::Scene};
        if (!resolutionError.empty())
            break;
    }
    if (resolutionError.empty())
    {
        for (const auto &tab : animationTabs_)
        {
            if (matches(tab->id(), DocumentTabKind::Animation,
                        tab->document().source_path()))
                return DocumentSourceOwner{tab->id(), DocumentTabKind::Animation};
            if (!resolutionError.empty())
                break;
        }
    }
    if (error != nullptr)
        *error = std::move(resolutionError);
    return std::nullopt;
}

std::optional<SceneLaunchSnapshot> DocumentManager::capture_active_scene_launch_snapshot() const
{
    const auto *tab = active_tab();
    if (tab == nullptr || tab->kind() != DocumentTabKind::Scene)
        return std::nullopt;
    const auto &document = tab->workspace().document();
    if (!document.sourcePath)
        return std::nullopt;
    std::string savedFileIdentity = document.sourcePath->lexically_normal().generic_string();
    if (document.sourceIdentity)
    {
        const auto &identity = *document.sourceIdentity;
        savedFileIdentity += ":" + std::to_string(identity.device) + ":" +
            std::to_string(identity.inode) + ":" + std::to_string(identity.size) + ":" +
            std::to_string(identity.mode) + ":" +
            std::to_string(identity.modificationTimeNanoseconds) + ":" +
            std::to_string(identity.changeTimeNanoseconds);
    }
    return SceneLaunchSnapshot{tab->identity(), *document.sourcePath, document.root,
                               tab->history().history_status().savedToken,
                               std::move(savedFileIdentity)};
}

TabId DocumentManager::create_scene_tab()
{
    auto tab = make_tab();
    const TabId id = tab->id();
    tabs_.push_back(std::move(tab));
    activeTab_ = id;
    return id;
}

std::optional<TabId> DocumentManager::open_scene(const std::filesystem::path &path,
                                                  std::string *error)
{
    const auto canonical = canonical_scene_path(path, error);
    if (!canonical)
        return std::nullopt;

    for (const auto &tab : tabs_)
    {
        const auto &source = tab->workspace().document().sourcePath;
        if (source && *source == *canonical)
        {
            activeTab_ = tab->id();
            return activeTab_;
        }
    }

    auto tab = make_tab();
    const auto opened = tab->workspace().open_document(*canonical, UnsavedDecision::Discard);
    if (!opened)
    {
        if (error != nullptr)
            *error = opened.message;
        return std::nullopt;
    }
    tab->selection().repair(tab->workspace().document());
    const TabId id = tab->id();
    tabs_.push_back(std::move(tab));
    activeTab_ = id;
    return id;
}

std::optional<TabId> DocumentManager::open_animation(const std::filesystem::path &path,
                                                     std::string *error)
{
    const auto canonical = canonical_animation_path(path, error);
    if (!canonical)
        return std::nullopt;
    std::error_code filesystemError;
    const auto canonicalAssetRoot = std::filesystem::canonical(assetRoot_, filesystemError);
    if (filesystemError || !path_is_within(canonicalAssetRoot, *canonical))
    {
        if (error != nullptr)
            *error = "animation asset must resolve inside the current project assets folder";
        return std::nullopt;
    }
    for (const auto &tab : animationTabs_)
    {
        if (tab->document().source_path() == canonical)
        {
            if (activeAnimationTab_ != tab->id())
                deactivate_active_animation_panel();
            activeAnimationTab_ = tab->id();
            return activeAnimationTab_;
        }
    }

    const TabId id = tabIds_.next();
    auto loaded = load_asset_document(*canonical, id.value);
    if (!loaded)
    {
        if (error != nullptr)
            *error = loaded.error;
        return std::nullopt;
    }
    animationTabs_.push_back(std::make_unique<AnimationDocumentTab>(id, projectGeneration_,
        std::move(*loaded.document), projectRoot_, assetRoot_));
    deactivate_active_animation_panel();
    activeAnimationTab_ = id;
    return id;
}

WorkspaceActionResult DocumentManager::save_scene_tab_as(
    const DocumentIdentity &identity, const std::filesystem::path &path, bool allowOverwrite)
{
    auto *tab = find(identity.tabId);
    if (tab == nullptr || tab->identity() != identity)
        return {WorkspaceActionStatus::Failed,
                "the scene Save As request belongs to a replaced or closed tab"};

    std::string resolutionError;
    const auto destination = normalized_destination_path(path, resolutionError);
    if (!destination)
        return {WorkspaceActionStatus::Failed, std::move(resolutionError)};
    const auto owner = source_owner_for_destination(*destination, identity.tabId, &resolutionError);
    if (!resolutionError.empty())
        return {WorkspaceActionStatus::Failed, std::move(resolutionError)};
    if (owner)
        return {WorkspaceActionStatus::Failed,
                std::string("Save As destination is already owned by ") +
                    document_kind_name(owner->kind) + " tab " + std::to_string(owner->tabId.value) +
                    "; choose another destination or activate that tab."};
    return tab->workspace().save_as(*destination, allowOverwrite);
}

bool DocumentManager::activate_animation_tab(TabId id) noexcept
{
    if (find_animation_tab(id) == nullptr)
        return false;
    if (activeAnimationTab_ != id)
        deactivate_active_animation_panel();
    activeAnimationTab_ = id;
    return true;
}

WorkspaceActionResult DocumentManager::save_animation_tab(TabId id)
{
    auto *tab = find_animation_tab(id);
    if (tab == nullptr)
        return {WorkspaceActionStatus::Failed, "the requested animation tab is no longer open"};
    return save_animation_tab(tab->identity());
}

WorkspaceActionResult DocumentManager::save_animation_tab(
    const DocumentIdentity &identity, std::optional<AssetOverwriteObservation> overwrite)
{
    auto *tab = find_animation_tab(identity.tabId);
    if (tab == nullptr || tab->identity() != identity)
        return {WorkspaceActionStatus::Failed,
                "the animation save request belongs to a replaced or closed tab"};
    if (!tab->document().source_path())
        return {WorkspaceActionStatus::Failed, "animation asset has no save destination"};
    const auto validation = validate_animation_clip_document(tab->document().root());
    if (!validation.valid())
    {
        const auto &issue = validation.issues.front();
        return {WorkspaceActionStatus::Failed,
                "animation asset is not runtime-compatible at " + issue.jsonPointer + ": " +
                    issue.message};
    }
    const auto saved = tab->document().save(*tab->document().source_path(), std::move(overwrite));
    if (saved.succeeded())
    {
        tab->clear_external_change_acknowledgement();
        return {WorkspaceActionStatus::Completed, saved.message};
    }
    return {WorkspaceActionStatus::Failed, saved.message,
            saved.status == AssetSaveStatus::Conflict};
}

WorkspaceActionResult DocumentManager::save_animation_tab_as(
    const DocumentIdentity &identity, const std::filesystem::path &path,
    std::optional<AssetOverwriteObservation> overwrite)
{
    auto *tab = find_animation_tab(identity.tabId);
    if (tab == nullptr || tab->identity() != identity)
        return {WorkspaceActionStatus::Failed,
                "the animation Save As request belongs to a replaced or closed tab"};
    std::string resolutionError;
    const auto destination = normalized_destination_path(path, resolutionError);
    if (!destination)
        return {WorkspaceActionStatus::Failed, std::move(resolutionError)};
    const auto owner = source_owner_for_destination(*destination, identity.tabId, &resolutionError);
    if (!resolutionError.empty())
        return {WorkspaceActionStatus::Failed, std::move(resolutionError)};
    if (owner)
        return {WorkspaceActionStatus::Failed,
                std::string("Save As destination is already owned by ") +
                    document_kind_name(owner->kind) + " tab " + std::to_string(owner->tabId.value) +
                    "; choose another destination or activate that tab."};
    const auto validation = validate_animation_clip_document(tab->document().root());
    if (!validation.valid())
    {
        const auto &issue = validation.issues.front();
        return {WorkspaceActionStatus::Failed,
                "animation asset is not runtime-compatible at " + issue.jsonPointer + ": " +
                    issue.message};
    }
    const auto saved = tab->document().save(*destination, std::move(overwrite));
    if (saved.succeeded())
    {
        tab->clear_external_change_acknowledgement();
        return {WorkspaceActionStatus::Completed, saved.message};
    }
    return {WorkspaceActionStatus::Failed, saved.message,
            saved.status == AssetSaveStatus::Conflict};
}

WorkspaceActionResult DocumentManager::reload_animation_tab(const DocumentIdentity &identity)
{
    auto *tab = find_animation_tab(identity.tabId);
    if (tab == nullptr || tab->identity() != identity)
        return {WorkspaceActionStatus::Failed,
                "the animation reload request belongs to a replaced or closed tab"};
    if (!tab->document().source_path())
        return {WorkspaceActionStatus::Failed, "animation asset has no source path to reload"};
    if (tab->document().generation() == std::numeric_limits<std::uint64_t>::max())
        return {WorkspaceActionStatus::Failed, "animation document generation space is exhausted"};
    auto loaded = load_asset_document(*tab->document().source_path(),
                                      tab->document().generation() + 1);
    if (!loaded)
        return {WorkspaceActionStatus::Failed, loaded.error};
    tab->panel().cancel_numeric_drafts();
    tab->panel().set_playing(false);
    tab->panel().scrub_to(loaded.document->root(), 0);
    tab->document() = std::move(*loaded.document);
    tab->clear_external_change_acknowledgement();
    return {WorkspaceActionStatus::Completed,
            "Reloaded the animation asset from disk; its edit history was reset."};
}

WorkspaceActionResult DocumentManager::close_animation_tab(TabId id, UnsavedDecision decision)
{
    const auto found = std::find_if(animationTabs_.begin(), animationTabs_.end(),
        [id](const auto &tab) { return tab->id() == id; });
    if (found == animationTabs_.end())
        return {WorkspaceActionStatus::Failed, "the requested animation tab is no longer open"};
    if ((*found)->document().dirty())
    {
        if (decision == UnsavedDecision::Cancel)
            return {WorkspaceActionStatus::Cancelled, {}};
        if (decision == UnsavedDecision::Save)
        {
            const auto saved = save_animation_tab(id);
            if (!saved)
                return saved;
        }
    }
    const bool wasActive = activeAnimationTab_ == id;
    if (wasActive)
        deactivate_active_animation_panel();
    animationTabs_.erase(found);
    if (wasActive)
        activeAnimationTab_ = animationTabs_.empty() ? TabId{} : animationTabs_.front()->id();
    return {WorkspaceActionStatus::Completed, {}};
}

bool DocumentManager::activate_tab(TabId id) noexcept
{
    if (find(id) == nullptr)
        return false;
    activeTab_ = id;
    return true;
}

SaveAllResult DocumentManager::save_all()
{
    SaveAllResult result;
    result.action = {WorkspaceActionStatus::Completed, {}};
    for (const auto &tab : tabs_)
    {
        if (!tab->workspace().dirty())
            continue;
        if (!tab->workspace().document().sourcePath)
        {
            result.failedTab = tab->id();
            result.action = {WorkspaceActionStatus::Failed,
                             "an unsaved scene needs Save As before Save All can continue"};
            return result;
        }
        const auto saved = tab->workspace().save();
        if (!saved)
        {
            result.failedTab = tab->id();
            result.action = saved;
            return result;
        }
        tab->clear_external_change_acknowledgement();
        result.savedTabs.push_back(tab->id());
    }
    for (const auto &tab : animationTabs_)
    {
        if (!tab->document().dirty())
            continue;
        const auto saved = save_animation_tab(tab->id());
        if (!saved)
        {
            result.failedTab = tab->id();
            result.action = saved;
            return result;
        }
        result.savedTabs.push_back(tab->id());
    }
    result.action.message = result.savedTabs.empty()
        ? "All document tabs are already saved."
        : "Saved " + std::to_string(result.savedTabs.size()) + " document tab(s).";
    return result;
}

WorkspaceActionResult DocumentManager::close_tab(TabId id, UnsavedDecision decision)
{
    const auto found = std::find_if(tabs_.begin(), tabs_.end(),
                                    [id](const auto &tab) { return tab->id() == id; });
    if (found == tabs_.end())
        return {WorkspaceActionStatus::Failed, "the requested scene tab is no longer open"};

    if ((*found)->workspace().dirty())
    {
        if (decision == UnsavedDecision::Cancel)
            return {WorkspaceActionStatus::Cancelled, {}};
        if (decision == UnsavedDecision::Save)
        {
            const auto saved = (*found)->workspace().save();
            if (!saved)
                return saved;
        }
    }

    const bool wasActive = activeTab_ == id;
    tabs_.erase(found);
    if (tabs_.empty())
    {
        auto replacement = make_tab();
        activeTab_ = replacement->id();
        tabs_.push_back(std::move(replacement));
    }
    else if (wasActive)
        activeTab_ = tabs_.front()->id();
    return {WorkspaceActionStatus::Completed, {}};
}

WorkspaceActionResult DocumentManager::set_project_roots(
    const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot,
    ProjectGeneration generation)
{
    if (!generation || generation <= projectGeneration_)
        return {WorkspaceActionStatus::Failed, "project generation must advance when roots change"};
    std::error_code error;
    const auto canonicalProject = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonicalProject, error) || error ||
        !std::filesystem::is_regular_file(canonicalProject / "CMakeLists.txt", error) || error)
        return {WorkspaceActionStatus::Failed, "project root must be an existing project directory"};
    const auto canonicalAssets = std::filesystem::canonical(assetRoot, error);
    if (error || !std::filesystem::is_directory(canonicalAssets, error) || error)
        return {WorkspaceActionStatus::Failed, "asset root must be an existing directory"};

    struct PreparedTabRoots
    {
        DocumentWorkspace *workspace = nullptr;
        std::filesystem::path projectRoot;
        std::filesystem::path assetRoot;
        std::filesystem::path documentProjectRoot;
        std::filesystem::path documentAssetRoot;
    };
    struct PreparedAnimationRoots
    {
        AnimationDocumentTab *tab = nullptr;
        std::filesystem::path projectRoot;
        std::filesystem::path assetRoot;
    };
    std::vector<PreparedTabRoots> prepared;
    std::vector<PreparedAnimationRoots> preparedAnimations;
    std::filesystem::path nextProjectRoot;
    std::filesystem::path nextAssetRoot;
    try
    {
        prepared.reserve(tabs_.size());
        for (const auto &tab : tabs_)
        {
            if (tab->workspace().document().projectGeneration != projectGeneration_.value)
                return {WorkspaceActionStatus::Failed, "an open document has an inconsistent project generation"};
            const DocumentIdentity oldIdentity{projectGeneration_, tab->id(),
                DocumentGeneration{tab->workspace().document().documentGeneration}};
            const DocumentIdentity newIdentity{generation, tab->id(), oldIdentity.documentGeneration};
            if (!tab->history().can_migrate_project_identity(oldIdentity, newIdentity))
                return {WorkspaceActionStatus::Failed,
                        "an open document history cannot migrate from the current project identity"};
            prepared.push_back(PreparedTabRoots{&tab->workspace(), canonicalProject, canonicalAssets,
                                                 canonicalProject, canonicalAssets});
        }
        preparedAnimations.reserve(animationTabs_.size());
        for (const auto &tab : animationTabs_)
            preparedAnimations.push_back(
                PreparedAnimationRoots{tab.get(), canonicalProject, canonicalAssets});
        nextProjectRoot = canonicalProject;
        nextAssetRoot = canonicalAssets;
    }
    catch (const std::exception &exception)
    {
        return {WorkspaceActionStatus::Failed,
                std::string("could not prepare project-root replacement: ") + exception.what()};
    }

    for (auto &roots : prepared)
        roots.workspace->commit_project_roots(std::move(roots.projectRoot), std::move(roots.assetRoot),
                                              std::move(roots.documentProjectRoot),
                                              std::move(roots.documentAssetRoot), generation.value);
    for (auto &roots : preparedAnimations)
    {
        roots.tab->panel().cancel_numeric_drafts();
        roots.tab->panel().set_playing(false);
        roots.tab->commit_project_roots(std::move(roots.projectRoot), std::move(roots.assetRoot),
                                        generation);
    }
    projectRoot_ = std::move(nextProjectRoot);
    assetRoot_ = std::move(nextAssetRoot);
    projectGeneration_ = generation;
    return {WorkspaceActionStatus::Completed, {}};
}

std::unique_ptr<DocumentTab> DocumentManager::make_tab()
{
    return std::make_unique<DocumentTab>(tabIds_.next(), projectGeneration_,
                                         projectRoot_, assetRoot_);
}

std::optional<std::filesystem::path> DocumentManager::canonical_scene_path(
    const std::filesystem::path &path, std::string *error)
{
    std::error_code filesystemError;
    const auto canonical = std::filesystem::canonical(path, filesystemError);
    if (filesystemError || !std::filesystem::is_regular_file(canonical, filesystemError) || filesystemError)
    {
        if (error != nullptr)
            *error = "scene path is not an existing file: " + path.string();
        return std::nullopt;
    }
    return canonical;
}

std::optional<std::filesystem::path> DocumentManager::canonical_animation_path(
    const std::filesystem::path &path, std::string *error)
{
    std::error_code filesystemError;
    const auto canonical = std::filesystem::canonical(path, filesystemError);
    if (filesystemError || !std::filesystem::is_regular_file(canonical, filesystemError) || filesystemError)
    {
        if (error != nullptr)
            *error = "animation path is not an existing file: " + path.string();
        return std::nullopt;
    }
    if (!editor_asset_is(canonical, EditorAssetKind::Animation))
    {
        if (error != nullptr)
            *error = "animation assets must use the .animset extension";
        return std::nullopt;
    }
    return canonical;
}
} // namespace platformator_editor
