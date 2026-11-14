#include "documentmanager.h"
#include "assetformats.h"
#include "animationeditor.h"
#include "documentio.h"
#include "layersettings.h"
#include "templates.h"
#include "platform/nativefile.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
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
    switch (kind)
    {
    case DocumentTabKind::Scene: return "Scene";
    case DocumentTabKind::Animation: return "Animation";
    case DocumentTabKind::Template: return "Template";
    }
    return "Document";
}

Json *find_document_object_mutable(DocumentSession &document, int objectId)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return nullptr;
    Json *current = &document.root;
    const auto &indices = location->second.childIndices;
    for (std::size_t index = 0; index < indices.size(); ++index)
    {
        if (!current->is_array() || indices[index] >= current->size())
            return nullptr;
        current = &(*current)[indices[index]];
        if (index + 1 < indices.size())
        {
            auto children = current->find("children");
            if (children == current->end() || !children->is_array())
                return nullptr;
            current = &*children;
        }
    }
    return current->is_object() ? current : nullptr;
}

void reconcile_linked_templates(DocumentTab &tab)
{
    auto &document = tab.workspace().document_for_edit();
    rebuild_document_indexes(document);
    std::unordered_set<std::string> reconciled;
    const auto objectIds = document.indexes.orderedObjectIds;
    for (const int objectId : objectIds)
    {
        const Json *object = find_document_object_by_id(document, objectId);
        if (object == nullptr || !is_template_instance_root(*object) ||
            !object->at("template").is_string())
            continue;
        const std::string ref = object->at("template").get<std::string>();
        if (!reconciled.insert(ref).second)
            continue;
        const auto path = resolve_template_asset_path(document.assetRoot, ref);
        std::ifstream input(path);
        if (!input.is_open())
            continue;
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        auto asset = parse_template_envelope(contents);
        if (asset)
            (void)propagate_template_to_scene(document, tab.history(), ref, *asset.asset);
    }
}

std::size_t linked_template_instance_count(const DocumentSession &document,
                                           std::string_view targetRef)
{
    DocumentSession indexed = document;
    rebuild_document_indexes(indexed);
    std::size_t count = 0;
    for (const int objectId : indexed.indexes.orderedObjectIds)
    {
        const Json *object = find_document_object_by_id(indexed, objectId);
        if (object == nullptr || !is_template_instance_root(*object) ||
            !object->contains("template") || !object->at("template").is_string())
            continue;
        const auto path = resolve_template_asset_path(indexed.assetRoot,
                                                       object->at("template").get<std::string>());
        if (normalize_template_asset_ref(indexed.assetRoot, path) == targetRef)
            ++count;
    }
    return count;
}

struct TemplateRetargetMapping
{
    std::filesystem::path oldPath;
    std::filesystem::path newPath;
    bool directory = false;
};

std::filesystem::path template_retarget_recovery_path(const std::filesystem::path &projectRoot)
{
    return projectRoot / ".platformator" / "template-retarget-pending.json";
}

bool read_template_retarget_mappings(const std::filesystem::path &path,
                                     std::vector<TemplateRetargetMapping> &mappings,
                                     std::string &error)
{
    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError))
    {
        if (filesystemError)
        {
            error = filesystemError.message();
            return false;
        }
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        error = "could not read recovery marker";
        return false;
    }
    try
    {
        Json root;
        input >> root;
        if (!root.is_object() || !root.contains("mappings") || !root["mappings"].is_array())
            throw std::invalid_argument("invalid mappings array");
        for (const auto &entry : root["mappings"])
        {
            if (!entry.is_object() || !entry.contains("old") || !entry["old"].is_string() ||
                !entry.contains("new") || !entry["new"].is_string() ||
                !entry.contains("directory") || !entry["directory"].is_boolean())
                throw std::invalid_argument("invalid mapping entry");
            mappings.push_back({entry["old"].get<std::string>(),
                                entry["new"].get<std::string>(),
                                entry["directory"].get<bool>()});
        }
    }
    catch (const std::exception &exception)
    {
        error = std::string("invalid recovery marker: ") + exception.what();
        return false;
    }
    return true;
}

bool write_template_retarget_mappings(const std::filesystem::path &projectRoot,
                                      const std::vector<TemplateRetargetMapping> &mappings,
                                      std::string &error)
{
    const auto markerPath = template_retarget_recovery_path(projectRoot);
    if (mappings.empty())
    {
        std::error_code filesystemError;
        if (!std::filesystem::remove(markerPath, filesystemError) && filesystemError)
        {
            error = filesystemError.message();
            return false;
        }
        return true;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(markerPath.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = filesystemError.message();
        return false;
    }
    Json document{{"schema", "platformator.template-retarget-recovery.v1"},
                  {"mappings", Json::array()}};
    for (const auto &mapping : mappings)
        document["mappings"].push_back(Json{{"old", mapping.oldPath.string()},
                                             {"new", mapping.newPath.string()},
                                             {"directory", mapping.directory}});
    const auto written = write_file_atomically(markerPath, document.dump(2) + "\n", true);
    if (!written.succeeded())
    {
        error = written.message;
        return false;
    }
    return true;
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

const nlohmann::json &DocumentTab::template_reference_bindings() const noexcept
{
    static const nlohmann::json empty = nlohmann::json::array();
    const auto &root = workspace_.document().root;
    if (root.is_array() && root.size() == 1 && root[0].is_object())
    {
        const auto found = root[0].find(std::string(EditorTemplateBindingsKey));
        if (found != root[0].end() && found->is_array())
            return *found;
    }
    return empty;
}

void DocumentTab::set_template_reference_bindings(nlohmann::json bindings)
{
    auto &root = workspace_.document_for_edit().root;
    if (root.is_array() && root.size() == 1 && root[0].is_object())
        root[0][std::string(EditorTemplateBindingsKey)] =
            bindings.is_array() ? std::move(bindings) : nlohmann::json::array();
}

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

bool DocumentManager::has_unsaved_changes() const noexcept
{
    return std::any_of(tabs_.begin(), tabs_.end(), [](const auto &tab) {
               return tab && tab->workspace().dirty();
           }) ||
           std::any_of(animationTabs_.begin(), animationTabs_.end(), [](const auto &tab) {
               return tab && tab->document().dirty();
           });
}
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
                          tab->identity(), tab->kind()});
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
            return DocumentSourceOwner{tab->id(), tab->kind()};
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
    reconcile_linked_templates(*tab);
    const TabId id = tab->id();
    tabs_.push_back(std::move(tab));
    activeTab_ = id;
    return id;
}

std::optional<TabId> DocumentManager::open_scene_reusing_empty_tab(
    const std::filesystem::path &path, std::string *error)
{
    const auto canonical = canonical_scene_path(path, error);
    if (!canonical)
        return std::nullopt;

    if (tabs_.size() == 1 && tabs_.front() &&
        tabs_.front()->kind() == DocumentTabKind::Scene &&
        !tabs_.front()->workspace().document().sourcePath &&
        !tabs_.front()->workspace().dirty())
    {
        auto &tab = *tabs_.front();
        const auto opened = tab.workspace().open_document(*canonical, UnsavedDecision::Discard);
        if (!opened)
        {
            if (error != nullptr)
                *error = opened.message;
            return std::nullopt;
        }
        tab.selection().repair(tab.workspace().document());
        reconcile_linked_templates(tab);
        activeTab_ = tab.id();
        return activeTab_;
    }
    return open_scene(*canonical, error);
}

std::size_t DocumentManager::reconcile_template_asset(const std::filesystem::path &path)
{
    std::error_code filesystemError;
    const auto canonical = std::filesystem::weakly_canonical(path, filesystemError);
    if (filesystemError)
        return 0;
    std::ifstream input(canonical);
    if (!input.is_open())
        return 0;
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    auto loaded = parse_template_envelope(contents);
    if (!loaded)
        return 0;
    const std::string ref = normalize_template_asset_ref(assetRoot_, canonical);
    std::size_t updated = 0;
    for (const auto &tab : tabs_)
        if (tab && tab->kind() == DocumentTabKind::Scene)
            updated += propagate_template_to_scene(tab->workspace().document_for_edit(),
                                                   tab->history(), ref, *loaded.asset);
    return updated;
}

TemplateRetargetResult DocumentManager::retarget_template_asset_links(
    const std::filesystem::path &oldPath, const std::filesystem::path &newPath,
    bool directory)
{
    TemplateRetargetResult result;
    if (oldPath.empty() || newPath.empty())
    {
        result.failures.push_back({oldPath, "template retarget mapping has an empty source or destination"});
        return result;
    }

    const auto normalized = [](const std::filesystem::path &path) {
        std::error_code error;
        auto result = std::filesystem::weakly_canonical(path, error);
        if (error)
            result = std::filesystem::absolute(path, error);
        return result.lexically_normal();
    };
    const auto source = normalized(oldPath);
    const auto destination = normalized(newPath);
    if (source.empty() || destination.empty())
    {
        result.failures.push_back({oldPath, "template retarget paths could not be resolved"});
        return result;
    }

    std::error_code rootError;
    const auto canonicalAssetRoot = std::filesystem::weakly_canonical(assetRoot_, rootError);
    if (rootError)
    {
        result.failures.push_back({assetRoot_, "asset root could not be resolved: " + rootError.message()});
        return result;
    }

    const auto contains_path = [](const std::filesystem::path &root,
                                  const std::filesystem::path &candidate) {
        const auto relative = candidate.lexically_relative(root);
        return relative.empty() ? candidate == root
            : (!relative.is_absolute() &&
               std::none_of(relative.begin(), relative.end(), [](const auto &part) {
                   return part == "..";
               }));
    };

    for (const auto &tab : tabs_)
    {
        if (!tab || tab->kind() != DocumentTabKind::Template)
            continue;
        const auto tabPath = normalized(tab->template_path());
        std::filesystem::path relative;
        if (directory)
        {
            if (!contains_path(source, tabPath))
                continue;
            relative = tabPath.lexically_relative(source);
        }
        else if (tabPath != source)
            continue;
        const auto movedPath = normalized(directory ? destination / relative : destination);
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(movedPath, fileError) || fileError)
        {
            result.failures.push_back({movedPath, fileError ? fileError.message()
                                                            : "moved template file is unavailable"});
            continue;
        }
        tab->set_template_path(movedPath);
        auto &document = tab->workspace().document_for_edit();
        document.sourcePath = movedPath;
        std::string identityError;
        document.sourceIdentity = native_file_identity(movedPath, identityError);
        if (!identityError.empty() || !document.sourceIdentity)
            result.failures.push_back({movedPath, identityError.empty()
                ? "could not identify moved template" : identityError});
        tab->clear_external_change_acknowledgement();
    }

    for (const auto &tab : tabs_)
    {
        if (!tab)
            continue;
        auto &document = tab->workspace().document_for_edit();
        rebuild_document_indexes(document);
        std::vector<BatchOperation> operations;
        std::size_t changedInDocument = 0;
        for (const int objectId : document.indexes.orderedObjectIds)
        {
            const Json *object = find_document_object_by_id(document, objectId);
            if (object == nullptr || !is_template_instance_root(*object) ||
                !object->contains("template") || !(*object)["template"].is_string())
                continue;

            const std::string oldRef = (*object)["template"].get<std::string>();
            std::filesystem::path referencedPath(oldRef);
            if (!referencedPath.is_absolute())
            {
                std::string raw = referencedPath.generic_string();
                if (raw.starts_with("assets/"))
                    raw.erase(0, 7);
                referencedPath = assetRoot_ / raw;
            }
            const auto linkedPath = normalized(referencedPath);
            std::filesystem::path relative;
            if (directory)
            {
                if (!contains_path(source, linkedPath))
                    continue;
                relative = linkedPath.lexically_relative(source);
            }
            else
            {
                if (linkedPath != source)
                    continue;
            }

            const auto movedPath = directory ? (destination / relative).lexically_normal()
                                             : destination;
            const auto canonicalMovedPath = normalized(movedPath);
            if (canonicalMovedPath.extension() != ".pttemplate" ||
                !contains_path(canonicalAssetRoot, canonicalMovedPath))
            {
                result.failures.push_back({document.sourcePath.value_or(std::filesystem::path{}),
                    "template link resolves outside the project asset root after move"});
                continue;
            }
            std::error_code fileError;
            if (!std::filesystem::is_regular_file(canonicalMovedPath, fileError) || fileError)
            {
                result.failures.push_back({canonicalMovedPath, fileError ? fileError.message()
                    : "moved template file is unavailable"});
                continue;
            }

            const std::string newRef = normalize_template_asset_ref(assetRoot_, canonicalMovedPath);
            if (newRef == oldRef)
                continue;
            operations.emplace_back(PropertyCommand{
                address_for_document(document, objectId, std::nullopt, std::nullopt,
                                     {std::string("template")}),
                PropertyValue::of(oldRef), PropertyValue::of(newRef), {}});
            ++changedInDocument;
        }

        if (operations.empty())
            continue;
        const auto committed = tab->history().apply_batch(document, operations);
        if (committed.status == CommandStatus::Changed || committed.status == CommandStatus::NoChange)
            result.updatedCount += changedInDocument;
        else
            result.failures.push_back({document.sourcePath.value_or(std::filesystem::path{}),
                committed.message.empty() ? "could not retarget an open document" : committed.message});
    }

    std::unordered_set<std::string> openScenePaths;
    for (const auto &tab : tabs_)
        if (tab && tab->kind() == DocumentTabKind::Scene &&
            tab->workspace().document().sourcePath)
            openScenePaths.insert(normalized(*tab->workspace().document().sourcePath).generic_string());
    std::error_code scanError;
    for (std::filesystem::recursive_directory_iterator it(
             assetRoot_, std::filesystem::directory_options::skip_permission_denied, scanError), end;
         it != end && !scanError; it.increment(scanError))
    {
        const auto path = it->path();
        if (path.extension() != ".scene")
            continue;
        if (!it->is_regular_file(scanError) || scanError)
        {
            if (scanError)
            {
                result.failures.push_back({path, "could not inspect closed scene: " + scanError.message()});
                scanError.clear();
            }
            continue;
        }
        const auto canonicalPath = normalized(path);
        if (openScenePaths.contains(canonicalPath.generic_string()))
            continue;
        std::ifstream input(canonicalPath, std::ios::binary);
        if (!input.is_open())
        {
            result.failures.push_back({canonicalPath, "could not read closed scene for template retargeting"});
            continue;
        }
        Json root;
        try
        {
            root = Json::parse(input);
        }
        catch (const std::exception &error)
        {
            result.failures.push_back({canonicalPath, "could not parse closed scene: " + std::string(error.what())});
            continue;
        }
        if (!root.is_array())
        {
            result.failures.push_back({canonicalPath, "closed scene root is not an array"});
            continue;
        }
        bool changed = false;
        std::vector<Json *> pending;
        for (auto &object : root)
            pending.push_back(&object);
        while (!pending.empty())
        {
            Json *object = pending.back();
            pending.pop_back();
            if (!object->is_object())
                continue;
            auto link = object->find("template");
            if (link != object->end() && link->is_string())
            {
                const std::string oldRef = link->get<std::string>();
                std::filesystem::path referencedPath(oldRef);
                if (!referencedPath.is_absolute())
                {
                    std::string raw = referencedPath.generic_string();
                    if (raw.starts_with("assets/"))
                        raw.erase(0, 7);
                    referencedPath = assetRoot_ / raw;
                }
                const auto linkedPath = normalized(referencedPath);
                std::filesystem::path relative;
                bool matches = false;
                if (directory && contains_path(source, linkedPath))
                {
                    relative = linkedPath.lexically_relative(source);
                    matches = true;
                }
                else if (!directory && linkedPath == source)
                    matches = true;
                const auto movedPath = matches
                    ? normalized(directory ? destination / relative : destination)
                    : std::filesystem::path{};
                std::error_code movedError;
                if (matches)
                {
                    if (movedPath.extension() != ".pttemplate" ||
                        !contains_path(canonicalAssetRoot, movedPath) ||
                        !std::filesystem::is_regular_file(movedPath, movedError) || movedError)
                    {
                        result.failures.push_back({canonicalPath,
                            movedError ? "moved template target could not be inspected: " + movedError.message()
                                       : "moved template target is unavailable inside the asset root"});
                    }
                    else
                    {
                        const std::string nextRef = normalize_template_asset_ref(assetRoot_, movedPath);
                        if (nextRef != oldRef)
                        {
                            *link = nextRef;
                            changed = true;
                        }
                    }
                }
            }
            auto children = object->find("children");
            if (children != object->end() && children->is_array())
                for (auto &child : *children)
                    pending.push_back(&child);
        }
        if (changed)
        {
            const auto written = write_file_atomically(canonicalPath, root.dump(2) + "\n", true);
            if (written.succeeded())
                ++result.updatedCount;
            else
                result.failures.push_back({canonicalPath,
                    "could not persist retargeted scene links: " + written.message});
        }
    }
    if (scanError)
        result.failures.push_back({assetRoot_, "closed scene scan failed: " + scanError.message()});

    std::vector<TemplateRetargetMapping> pendingMappings;
    std::string recoveryError;
    const auto markerPath = template_retarget_recovery_path(projectRoot_);
    if (!read_template_retarget_mappings(markerPath, pendingMappings, recoveryError))
    {
        result.recoveryStored = false;
        result.failures.push_back({markerPath,
            "could not read template retarget recovery state: " + recoveryError});
        return result;
    }
    // Compose the pending destinations through this later move. A failed scene may
    // still name the original source, so its recovery rule must point at the asset's
    // current location rather than an intermediate directory that no longer exists.
    for (auto &mapping : pendingMappings)
    {
        if (mapping.newPath.lexically_normal() == source.lexically_normal())
            mapping.newPath = destination;
        else if (directory && contains_path(source, mapping.newPath))
        {
            const auto relative = mapping.newPath.lexically_relative(source);
            mapping.newPath = (destination / relative).lexically_normal();
        }
    }
    std::erase_if(pendingMappings, [&](const TemplateRetargetMapping &mapping) {
        return mapping.oldPath.lexically_normal() == source.lexically_normal() &&
               mapping.directory == directory;
    });
    if (!result.failures.empty())
        pendingMappings.push_back({source, destination, directory});
    if (!write_template_retarget_mappings(projectRoot_, pendingMappings, recoveryError))
    {
        result.recoveryStored = false;
        result.failures.push_back({markerPath,
            "could not store retryable template retarget mapping: " + recoveryError});
    }
    return result;
}

TemplateRetargetResult DocumentManager::retry_pending_template_asset_links()
{
    TemplateRetargetResult aggregate;
    const auto markerPath = template_retarget_recovery_path(projectRoot_);
    std::vector<TemplateRetargetMapping> mappings;
    std::string recoveryError;
    if (!read_template_retarget_mappings(markerPath, mappings, recoveryError))
    {
        aggregate.recoveryStored = false;
        aggregate.failures.push_back({markerPath,
            "could not read template retarget recovery state: " + recoveryError});
        return aggregate;
    }
    for (const auto &mapping : mappings)
    {
        const auto result = retarget_template_asset_links(mapping.oldPath, mapping.newPath,
                                                          mapping.directory);
        aggregate.updatedCount += result.updatedCount;
        aggregate.recoveryStored = aggregate.recoveryStored && result.recoveryStored;
        aggregate.failures.insert(aggregate.failures.end(), result.failures.begin(), result.failures.end());
    }
    return aggregate;
}

std::optional<TabId> DocumentManager::open_template(const std::filesystem::path &path,
                                                    std::string *error)
{
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::exists(canonical, ec))
    {
        if (error != nullptr)
            *error = "template file does not exist: " + path.string();
        return std::nullopt;
    }

    for (const auto &tab : tabs_)
    {
        if (tab->kind() == DocumentTabKind::Template && tab->template_path() == canonical)
        {
            activeTab_ = tab->id();
            return activeTab_;
        }
    }

    std::ifstream in(canonical);
    if (!in.is_open())
    {
        if (error != nullptr)
            *error = "unable to read template file: " + canonical.string();
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto loaded = parse_template_envelope(text);
    if (!loaded)
    {
        if (error != nullptr)
            *error = loaded.error;
        return std::nullopt;
    }

    auto tab = make_tab();
    tab->set_kind(DocumentTabKind::Template);
    tab->set_template_path(canonical);
    auto &doc = tab->workspace().document_for_edit();
    doc.root = Json::array({loaded.asset->root()});
    tab->set_template_reference_bindings(loaded.asset->reference_bindings());
    doc.sourcePath = canonical;
    std::string identityError;
    doc.sourceIdentity = native_file_identity(canonical, identityError);
    if (!identityError.empty() || !doc.sourceIdentity)
    {
        if (error != nullptr)
            *error = identityError.empty() ? "unable to identify template file: " + canonical.string()
                                           : "unable to identify template file: " + identityError;
        return std::nullopt;
    }
    ++doc.documentGeneration;
    ++doc.structureRevision;
    ++doc.valueRevision;
    tab->history().reset_history(doc.documentGeneration);
    rebuild_document_indexes(doc);
    tab->workspace().mark_saved();
    tab->selection().repair(doc);
    if (!doc.indexes.orderedObjectIds.empty())
    {
        Selection sel;
        sel.objectId = doc.indexes.orderedObjectIds.front();
        sel.selectedObjectIds = {doc.indexes.orderedObjectIds.front()};
        tab->selection().set(doc, sel);
    }

    const TabId id = tab->id();
    tabs_.push_back(std::move(tab));
    activeTab_ = id;
    return id;
}

bool DocumentManager::save_template_tab(TabId id, std::string *error,
                                        bool allowExternalOverwrite,
                                        std::optional<FileIdentity> expectedExternalIdentity)
{
    DocumentTab *tab = find(id);
    if (tab == nullptr || tab->kind() != DocumentTabKind::Template)
    {
        if (error != nullptr)
            *error = "tab is not a template editing tab";
        return false;
    }
    const auto &doc = tab->workspace().document();
    if (!doc.root.is_array() || doc.root.empty() || !doc.root[0].is_object())
    {
        if (error != nullptr)
            *error = "template document must contain a root GameObject";
        return false;
    }
    std::string identityError;
    const auto diskIdentity = native_file_identity(tab->template_path(), identityError);
    if (!identityError.empty())
    {
        if (error != nullptr)
            *error = "unable to verify template file before saving: " +
                     (identityError.empty() ? tab->template_path().string() : identityError);
        return false;
    }
    if (allowExternalOverwrite)
    {
        if (diskIdentity != expectedExternalIdentity)
        {
            if (error != nullptr)
                *error = "template changed again after the external version was reviewed";
            return false;
        }
    }
    else if (!diskIdentity || doc.sourceIdentity != diskIdentity)
    {
        if (error != nullptr)
            *error = "template changed on disk; reload or explicitly overwrite the observed version first";
        return false;
    }
    Json rootCopy = doc.root[0];
    strip_nested_template_metadata(rootCopy);
    const std::string tmplName = rootCopy.value("name", tab->template_path().stem().string());
    Json bindings = tab->template_reference_bindings();
    if (templateBindingCollector_)
    {
        auto collected = templateBindingCollector_(doc, rootCopy, bindings);
        if (!collected)
        {
            if (error != nullptr)
                *error = collected.error.empty()
                    ? "template references could not be recollected from the current source schemas"
                    : collected.error;
            return false;
        }
        bindings = std::move(collected.bindings);
        const auto same_site = [](const Json &left, const Json &right) {
            return left.value("owner_id", -1) == right.value("owner_id", -2) &&
                   left.value("component_id", -1) == right.value("component_id", -2) &&
                   left.value("path", std::string{}) == right.value("path", std::string{});
        };
        for (auto &binding : bindings)
        {
            if (!binding.is_object() || binding.value("scope", std::string{}) != "external")
                continue;
            const auto previous = std::find_if(tab->template_reference_bindings().begin(),
                tab->template_reference_bindings().end(), [&](const Json &candidate) {
                    return candidate.is_object() &&
                           candidate.value("scope", std::string{}) == "external" &&
                           same_site(binding, candidate);
                });
            if (previous != tab->template_reference_bindings().end() &&
                previous->contains("source_scene_identity"))
                binding["source_scene_identity"] = previous->at("source_scene_identity");
        }
    }
    const Json envelope = make_template_envelope(tmplName, rootCopy, bindings);
    auto loaded = load_template_envelope(envelope);
    if (!loaded)
    {
        if (error != nullptr)
            *error = loaded.error;
        return false;
    }

    const std::string normalizedRef = normalize_template_asset_ref(assetRoot_, tab->template_path());
    std::vector<std::pair<DocumentTab *, std::size_t>> scenePropagationPlan;
    for (const auto &otherTab : tabs_)
    {
        if (otherTab->kind() != DocumentTabKind::Scene)
            continue;
        const std::size_t expected = linked_template_instance_count(
            otherTab->workspace().document(), normalizedRef);
        DocumentSession staged = otherTab->workspace().document();
        EditCommandService stagedHistory;
        stagedHistory.reset_history(staged.documentGeneration);
        const std::size_t prepared = propagate_template_to_scene(
            staged, stagedHistory, normalizedRef, *loaded.asset);
        if (prepared != expected)
        {
            if (error != nullptr)
                *error = "template changes could not be prepared for every open Scene tab";
            return false;
        }
        scenePropagationPlan.emplace_back(otherTab.get(), expected);
    }

    std::string previousContents;
    bool hadPreviousFile = false;
    {
        std::ifstream input(tab->template_path(), std::ios::binary);
        if (input.is_open())
        {
            previousContents.assign(std::istreambuf_iterator<char>(input),
                                    std::istreambuf_iterator<char>());
            hadPreviousFile = true;
        }
    }
    const auto written = write_file_atomically(tab->template_path(), envelope.dump(2) + "\n", true);
    if (!written.succeeded())
    {
        if (error != nullptr)
            *error = "unable to write template file: " + written.message;
        return false;
    }

    identityError.clear();
    const auto savedIdentity = native_file_identity(tab->template_path(), identityError);
    if (!identityError.empty() || !savedIdentity)
    {
        std::error_code rollbackError;
        if (hadPreviousFile)
            (void)write_file_atomically(tab->template_path(), previousContents, true);
        else
            std::filesystem::remove(tab->template_path(), rollbackError);
        if (error != nullptr)
            *error = "template was written but its saved identity could not be refreshed: " +
                     (identityError.empty() ? tab->template_path().string() : identityError);
        return false;
    }

    Json editorRoot = doc.root[0];
    editorRoot[std::string(EditorTemplateBindingsKey)] = bindings;
    std::vector<std::pair<DocumentTab *, std::uint64_t>> synchronizedScenes;
    const auto rollback_saved_file = [&]() {
        std::error_code rollbackError;
        if (hadPreviousFile)
            return write_file_atomically(tab->template_path(), previousContents, true).succeeded();
        std::filesystem::remove(tab->template_path(), rollbackError);
        return !rollbackError;
    };
    const auto rollback_synchronized_scenes = [&]() {
        bool restored = true;
        for (auto scene = synchronizedScenes.rbegin(); scene != synchronizedScenes.rend(); ++scene)
        {
            if (scene->first->history().history_status().currentToken == scene->second)
                continue;
            const auto undone = scene->first->history().undo(
                scene->first->workspace().document_for_edit(), scene->first->selection());
            restored = undone.status == CommandStatus::Changed && restored;
        }
        return restored;
    };
    for (const auto &[sceneTab, expected] : scenePropagationPlan)
    {
        const std::uint64_t tokenBefore = sceneTab->history().history_status().currentToken;
        const std::size_t synchronized = propagate_template_to_scene(
            sceneTab->workspace().document_for_edit(), sceneTab->history(), normalizedRef,
            *loaded.asset);
        if (synchronized != expected)
        {
            const bool scenesRestored = rollback_synchronized_scenes();
            const bool fileRestored = rollback_saved_file();
            if (error != nullptr)
                *error = "template propagation failed for an open Scene tab" +
                    std::string(scenesRestored ? "" : "; earlier scene updates could not be rolled back") +
                    std::string(fileRestored ? "" : "; the previous template file could not be restored");
            return false;
        }
        synchronizedScenes.emplace_back(sceneTab, tokenBefore);
    }

    if (editorRoot != doc.root[0])
    {
        std::string historyError;
        if (!replace_template_document_root(tab->workspace().document_for_edit(), tab->history(),
                                            editorRoot, historyError))
        {
            const bool scenesRestored = rollback_synchronized_scenes();
            const bool fileRestored = rollback_saved_file();
            if (error != nullptr)
                *error = historyError +
                    std::string(scenesRestored ? "" : "; scene updates could not be rolled back") +
                    std::string(fileRestored ? "" : "; the previous template file could not be restored");
            return false;
        }
    }

    tab->workspace().mark_saved();
    tab->workspace().document_for_edit().sourceIdentity = savedIdentity;
    tab->set_template_reference_bindings(bindings);
    tab->clear_external_change_acknowledgement();

    return true;
}

bool DocumentManager::revert_template_tab(TabId id, std::string *error)
{
    DocumentTab *tab = find(id);
    if (tab == nullptr || tab->kind() != DocumentTabKind::Template)
    {
        if (error != nullptr)
            *error = "tab is not a template editing tab";
        return false;
    }
    std::ifstream in(tab->template_path(), std::ios::binary);
    if (!in.is_open())
    {
        if (error != nullptr)
            *error = "unable to read template file: " + tab->template_path().string();
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto loaded = parse_template_envelope(text);
    if (!loaded)
    {
        if (error != nullptr)
            *error = loaded.error;
        return false;
    }
    auto &doc = tab->workspace().document_for_edit();
    std::string identityError;
    Json nextRoot = loaded.asset->root();
    nextRoot[std::string(EditorTemplateBindingsKey)] = loaded.asset->reference_bindings();
    std::string replacementError;
    if (!replace_template_document_root(doc, tab->history(), nextRoot, replacementError))
    {
        if (error != nullptr)
            *error = std::move(replacementError);
        return false;
    }
    rebuild_document_indexes(doc);
    tab->workspace().mark_saved();
    identityError.clear();
    doc.sourceIdentity = native_file_identity(tab->template_path(), identityError);
    if (!identityError.empty() || !doc.sourceIdentity)
    {
        if (error != nullptr)
            *error = "template was reloaded but its saved identity could not be refreshed: " +
                     (identityError.empty() ? tab->template_path().string() : identityError);
        return false;
    }
    tab->set_template_reference_bindings(loaded.asset->reference_bindings());
    tab->selection().repair(doc);
    return true;
}

bool DocumentManager::reload_template_tab(TabId id, std::string *error)
{
    if (!revert_template_tab(id, error))
        return false;
    if (auto *tab = find(id); tab != nullptr && tab->kind() == DocumentTabKind::Template)
        tab->history().reset_history(tab->workspace().document().documentGeneration);
    return true;
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
    if (tab->kind() != DocumentTabKind::Scene)
        return {WorkspaceActionStatus::Failed,
                "Save As is available for Scene tabs; use the Template Save action for templates"};

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
        if (tab->kind() == DocumentTabKind::Template)
        {
            std::string templateError;
            if (!save_template_tab(tab->id(), &templateError))
            {
                result.failedTab = tab->id();
                result.action = {WorkspaceActionStatus::Failed, std::move(templateError)};
                return result;
            }
            result.savedTabs.push_back(tab->id());
            continue;
        }
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
            if ((*found)->kind() == DocumentTabKind::Template)
            {
                std::string error;
                if (!save_template_tab(id, &error))
                    return {WorkspaceActionStatus::Failed, std::move(error)};
            }
            else
            {
                const auto saved = (*found)->workspace().save();
                if (!saved)
                    return saved;
            }
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
    ProjectGeneration generation, bool discardDirtyDocuments)
{
    if (!generation || generation <= projectGeneration_)
        return {WorkspaceActionStatus::Failed, "project generation must advance when roots change"};
    if (has_unsaved_changes() && !discardDirtyDocuments)
        return {WorkspaceActionStatus::Failed,
                "dirty documents must be saved or explicitly discarded before replacing the project"};
    std::error_code error;
    const auto canonicalProject = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonicalProject, error) || error)
        return {WorkspaceActionStatus::Failed, "project root must be an existing project directory"};
    const auto canonicalAssets = std::filesystem::canonical(assetRoot, error);
    if (error || !std::filesystem::is_directory(canonicalAssets, error) || error)
        return {WorkspaceActionStatus::Failed, "asset root must be an existing directory"};

    std::vector<std::unique_ptr<DocumentTab>> replacementTabs;
    try
    {
        replacementTabs.reserve(1);
        replacementTabs.push_back(std::make_unique<DocumentTab>(
            tabIds_.next(), generation, canonicalProject, canonicalAssets));
    }
    catch (const std::exception &exception)
    {
        return {WorkspaceActionStatus::Failed,
                std::string("could not prepare project-root replacement: ") + exception.what()};
    }

    deactivate_active_animation_panel();
    tabs_.swap(replacementTabs);
    animationTabs_.clear();
    projectRoot_ = canonicalProject;
    assetRoot_ = canonicalAssets;
    projectGeneration_ = generation;
    activeTab_ = tabs_.front()->id();
    activeAnimationTab_ = {};
    return {WorkspaceActionStatus::Completed, {}};
}

std::vector<TabId> DocumentManager::sync_layer_settings(const LayerSettings &settings,
                                                        bool recordUndo)
{
    std::vector<TabId> changedTabs;
    for (const auto &tab : tabs_)
    {
        if (!tab)
            continue;
        auto &doc = tab->workspace().document_for_edit();
        std::vector<BatchPropertyEdit> edits;
        bool changedWithoutHistory = false;
        for (const int objectId : doc.indexes.orderedObjectIds)
        {
            Json *object = find_document_object_mutable(doc, objectId);
            if (object == nullptr || !object->contains("components") || !(*object)["components"].is_array())
                continue;
            for (auto &component : (*object)["components"])
            {
                if (!component.is_object() || component.value("type", -1) != 3 ||
                    !component.contains("id") || !component["id"].is_number_integer())
                    continue;
                std::uint64_t group = 1;
                if (component.contains("collisionGroup"))
                {
                    const auto &value = component["collisionGroup"];
                    if (value.is_number_unsigned())
                        group = value.get<std::uint64_t>();
                    else if (value.is_number_integer() && value.get<std::int64_t>() >= 0)
                        group = static_cast<std::uint64_t>(value.get<std::int64_t>());
                }
                const auto layer = collision_group_to_layer(group);
                if (!layer)
                    continue;
                const std::uint64_t desired = layer_to_collision_mask(settings, *layer);
                const std::uint64_t current = component.contains("collisionMask") &&
                        component["collisionMask"].is_number_integer() &&
                        component["collisionMask"].get<std::int64_t>() >= 0
                    ? component["collisionMask"].get<std::uint64_t>() : 0ULL;
                if (!component.contains("collisionMask") || current != desired)
                {
                    const int componentId = component["id"].get<int>();
                    if (recordUndo)
                        edits.push_back({address_for_document(doc, objectId, componentId, std::nullopt,
                                                              {std::string("collisionMask")}),
                                         PropertyValue::of(desired)});
                    else
                    {
                        component["collisionMask"] = desired;
                        changedWithoutHistory = true;
                    }
                }
            }
        }
        if (recordUndo && !edits.empty())
        {
            const Selection current = tab->selection().current();
            const auto applied = tab->history().apply_batch(doc, edits, SelectionState{current, current});
            if (applied.status == CommandStatus::Changed)
                changedTabs.push_back(tab->id());
        }
        else if (changedWithoutHistory)
        {
            ++doc.valueRevision;
            changedTabs.push_back(tab->id());
        }
    }
    return changedTabs;
}

std::uint32_t DocumentManager::collision_group_layers_in_use() const noexcept
{
    std::uint32_t used = 0;
    const auto collect = [&](const auto &self, const Json &object) -> void {
        if (!object.is_object())
            return;
        if (object.contains("components") && object["components"].is_array())
        {
            for (const auto &component : object["components"])
            {
                if (!component.is_object() || component.value("type", -1) != 3 ||
                    !component.contains("collisionGroup"))
                    continue;
                const auto &group = component["collisionGroup"];
                std::uint64_t value = 0;
                if (group.is_number_unsigned())
                    value = group.get<std::uint64_t>();
                else if (group.is_number_integer() && group.get<std::int64_t>() >= 0)
                    value = static_cast<std::uint64_t>(group.get<std::int64_t>());
                used |= static_cast<std::uint32_t>(value & 0xffffffffULL);
            }
        }
        if (object.contains("children") && object["children"].is_array())
            for (const auto &child : object["children"])
                self(self, child);
    };
    for (const auto &tab : tabs_)
    {
        if (!tab)
            continue;
        const auto &root = tab->workspace().document().root;
        if (root.is_array())
            for (const auto &object : root)
                collect(collect, object);
        else
            collect(collect, root);
    }
    return used;
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
