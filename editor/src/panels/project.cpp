#include "project.h"
#include "imagepreflight.h"

#include "../assetformats.h"
#include "../editorwork.h"
#include "../componenttypes.h"
#include "../style.h"
#include "../discovery.h"
#include "referencefields.h"
#include "selection.h"
#include "spriteeditor.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <imgui.h>
#include <platformator/gameobject.h>

namespace platformator_editor
{
namespace
{
struct SymlinkTrace
{
    std::vector<std::filesystem::path> ancestors;
    std::vector<std::filesystem::path> unresolvedTargets;
    bool allLinksResolve = true;
};

bool path_is_within(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    if (relative.is_absolute())
        return false;
    for (const auto &part : relative)
        if (part == "..")
            return false;
    return true;
}

const Json *resolve_object(const Json &root, const ObjectLocation &location)
{
    if (!root.is_array() || location.childIndices.empty())
        return nullptr;
    const Json *object = nullptr;
    const Json *siblings = &root;
    for (std::size_t depth = 0; depth < location.childIndices.size(); ++depth)
    {
        if (depth > 0)
        {
            if (object == nullptr)
                return nullptr;
            const auto children = object->find("children");
            if (children == object->end())
                return nullptr;
            siblings = &*children;
        }
        const std::size_t index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}

SymlinkTrace resolved_symlink_ancestors(
    const std::filesystem::path &logicalPath, const std::filesystem::path &assetRoot)
{
    const auto relative = logicalPath.lexically_relative(assetRoot);
    if (relative.empty() || relative.is_absolute() ||
        (relative.begin() != relative.end() && *relative.begin() == ".."))
        return {.allLinksResolve = false};

    std::deque<std::filesystem::path> pending;
    for (const auto &part : relative)
        pending.push_back(part);

    SymlinkTrace result;
    auto current = assetRoot;
    std::size_t followedLinks = 0;
    std::size_t visitedComponents = 0;
    bool followingLink = false;
    constexpr std::size_t MaxSymlinkHops = 64;
    constexpr std::size_t MaxPathComponents = 512;
    while (!pending.empty() && followedLinks < MaxSymlinkHops &&
           visitedComponents < MaxPathComponents)
    {
        ++visitedComponents;
        const auto part = std::move(pending.front());
        pending.pop_front();
        if (part.empty() || part == ".")
            continue;
        if (part == "..")
        {
            current = current.parent_path();
            continue;
        }

        const auto candidate = (current / part).lexically_normal();
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
        {
            if (followingLink && path_is_within(assetRoot, candidate) &&
                std::find(result.unresolvedTargets.begin(), result.unresolvedTargets.end(), candidate) ==
                    result.unresolvedTargets.end())
                result.unresolvedTargets.push_back(candidate);
            if (followingLink)
                break;
            current = candidate;
            continue;
        }
        if (error || !std::filesystem::is_symlink(status))
        {
            current = candidate;
            continue;
        }

        followingLink = true;
        if (std::find(result.ancestors.begin(), result.ancestors.end(), candidate) ==
            result.ancestors.end())
            result.ancestors.push_back(candidate);
        std::error_code canonicalError;
        (void)std::filesystem::canonical(candidate, canonicalError);
        if (canonicalError)
            result.allLinksResolve = false;
        if (++followedLinks >= MaxSymlinkHops)
        {
            result.allLinksResolve = false;
            break;
        }
        error.clear();
        const auto target = std::filesystem::read_symlink(candidate, error);
        if (error)
        {
            result.allLinksResolve = false;
            break;
        }

        if (target.is_absolute())
            current = target.root_path();
        else
            current = candidate.parent_path();
        const auto targetParts = target.is_absolute() ? target.relative_path() : target;
        std::vector<std::filesystem::path> components;
        for (const auto &targetPart : targetParts)
            components.push_back(targetPart);
        for (auto component = components.rbegin(); component != components.rend(); ++component)
            pending.push_front(*component);
    }
    if (!pending.empty())
        result.allLinksResolve = false;
    return result;
}

std::string folder_label(const std::filesystem::path &root,
                         const std::filesystem::path &directory, const std::string &rootLabel)
{
    return directory == root ? rootLabel : directory.filename().string();
}

std::string lowercase_text(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string semantic_text_token(std::string_view value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string token;
    token.reserve(value.size() * 2);
    for (const unsigned char byte : value)
    {
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 0x0f]);
    }
    return token.empty() ? "root" : token;
}

std::string semantic_path_token(const std::filesystem::path &path)
{
    return semantic_text_token(path.generic_string());
}

std::string bounded_semantic_text(std::string value, std::size_t maximum = 512)
{
    if (value.size() > maximum)
    {
        value.resize(maximum > 3 ? maximum - 3 : 0);
        value += "...";
    }
    return value;
}

struct AssetPresentation
{
    EditorIcon icon = EditorIcon::NewDocument;
    std::string_view searchType = "asset file";
};

AssetPresentation classify_asset_presentation(const std::filesystem::path &path,
                                               bool directory = false)
{
    if (directory)
        return {EditorIcon::Folder, "folder directory"};
    const auto format = editor_asset_format(path);
    if (format.kind == EditorAssetKind::Animation)
        return {EditorIcon::Animator, "animation clip"};
    if (format.kind == EditorAssetKind::Template)
        return {EditorIcon::NewDocument, "object template"};
    if (format.kind == EditorAssetKind::Image)
        return {EditorIcon::Sprite, "image texture"};
    if (format.kind == EditorAssetKind::Audio)
        return {EditorIcon::Audio, "audio"};
    if (format.kind == EditorAssetKind::Script)
        return {EditorIcon::Script, format.behaviorHeader ? "behavior source code" : "asset file"};
    return {};
}

std::string asset_search_type(const std::filesystem::path &path, bool directory)
{
    return std::string(classify_asset_presentation(path, directory).searchType);
}

EditorIcon asset_icon(const std::filesystem::path &path)
{
    return classify_asset_presentation(path).icon;
}

std::optional<std::string> behavior_type_from_source(const std::filesystem::path &sourcePath)
{
    std::vector<std::filesystem::path> candidates;
    auto header = sourcePath;
    header.replace_extension(".h");
    candidates.push_back(header);
    header.replace_extension(".hpp");
    candidates.push_back(header);
    candidates.push_back(sourcePath);

    for (const auto &candidate : candidates)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(candidate, error);
        if (error || size == 0 || size > 1024U * 1024U)
            continue;
        std::ifstream input(candidate, std::ios::binary);
        if (!input)
            continue;
        std::string source(static_cast<std::size_t>(size), '\0');
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (!input && !input.eof())
            continue;

        std::size_t position = 0;
        while ((position = source.find("class ", position)) != std::string::npos)
        {
            const std::size_t nameStart = position + 6;
            std::size_t nameEnd = nameStart;
            while (nameEnd < source.size() &&
                   (std::isalnum(static_cast<unsigned char>(source[nameEnd])) || source[nameEnd] == '_'))
                ++nameEnd;
            if (nameEnd == nameStart)
            {
                position = nameStart;
                continue;
            }
            const std::string name = source.substr(nameStart, nameEnd - nameStart);
            const auto openingBrace = source.find('{', nameEnd);
            const auto semicolon = source.find(';', nameEnd);
            if (openingBrace != std::string::npos &&
                (semicolon == std::string::npos || openingBrace < semicolon))
            {
                const std::string_view declaration(source.data() + nameEnd, openingBrace - nameEnd);
                const auto publicBase = declaration.find("public");
                const auto baseName = publicBase == std::string_view::npos
                    ? std::string_view::npos : declaration.find("Behavior", publicBase + 6);
                if (baseName != std::string_view::npos)
                {
                    const bool startsToken = baseName == 0 ||
                        !(std::isalnum(static_cast<unsigned char>(declaration[baseName - 1])) ||
                          declaration[baseName - 1] == '_');
                    const auto afterBase = baseName + std::string_view("Behavior").size();
                    const bool endsToken = afterBase == declaration.size() ||
                        !(std::isalnum(static_cast<unsigned char>(declaration[afterBase])) ||
                          declaration[afterBase] == '_');
                    if (startsToken && endsToken)
                        return name;
                }
            }
            position = nameEnd;
        }
    }
    return std::nullopt;
}
} // namespace

ProjectPanel::ProjectPanel(ProjectContext &context, ProjectChanges &changes,
                           ProjectScanService &scans, SDL_Renderer *renderer)
    : context_(context), changes_(changes), scans_(scans), renderer_(renderer),
      frameRenderer_(renderer)
{
}

ProjectPanel::~ProjectPanel()
{
    release_preview_resources();
    animationTasks_.wait();
}

void ProjectPanel::set_renderer(SDL_Renderer *renderer) noexcept
{
    if (renderer_ == renderer)
        return;
    release_preview_resources();
    renderer_ = renderer;
    frameRenderer_ = renderer;
}

void ProjectPanel::set_frame_renderer(SDL_Renderer *renderer) noexcept
{
    frameRenderer_ = renderer != nullptr ? renderer : renderer_;
}

void ProjectPanel::release_renderer(SDL_Renderer *renderer) noexcept
{
    if (renderer == nullptr)
        return;
    thumbnails_.release_renderer(renderer);
    if (frameRenderer_ == renderer)
        frameRenderer_ = renderer_;
    if (renderer_ == renderer)
    {
        renderer_ = nullptr;
        frameRenderer_ = nullptr;
    }
}

void ProjectPanel::service_owner_work(SDL_Renderer *renderer)
{
    if (renderer == nullptr)
        renderer = renderer_;
    if (renderer == nullptr || ownerUploadPollDoneThisFrame_)
        return;
    thumbnails_.poll_uploads(renderer);
    apply_animation_results();
    ownerUploadPollDoneThisFrame_ = true;
}

void ProjectPanel::set_wake_callback(std::function<void()> callback)
{
    wakeCallback_ = std::move(callback);
    thumbnails_.set_wake_callback(wakeCallback_);
}

void ProjectPanel::set_open_animation_callback(
    std::function<void(const std::filesystem::path &)> callback)
{
    openAnimationCallback_ = std::move(callback);
}

std::vector<SemanticNode> ProjectPanel::semantic_nodes(const DocumentSession &document,
                                                       const Selection &selection,
                                                       std::string_view scope)
{
    std::vector<SemanticNode> nodes;
    if (scope.empty())
    {
        semanticTargets_.clear();
        return nodes;
    }
    const std::string prefix(scope);
    const auto &semanticAssetRoot = document.assetRoot;
    const std::string contentsId = prefix + "/contents";
    SemanticNode contents;
    contents.id = contentsId;
    contents.parentId = prefix;
    contents.role = SemanticRole::Group;
    contents.name = "Visible project assets";
    contents.value = selectedDirectory_.lexically_relative(semanticAssetRoot).generic_string();
    contents.visible = true;
    contents.expanded = true;
    nodes.push_back(std::move(contents));

    const std::string refreshId = prefix + "/refresh";
    SemanticNode refresh;
    refresh.id = refreshId;
    refresh.parentId = prefix;
    refresh.role = SemanticRole::Button;
    refresh.name = "Refresh project assets";
    refresh.value = bounded_semantic_text(coverageMessage_);
    refresh.help = "Refresh the selected folder and recheck referenced resources.";
    refresh.visible = true;
    refresh.actions = {"activate"};
    nodes.push_back(std::move(refresh));

    SemanticNode location;
    location.id = prefix + "/location";
    location.parentId = prefix;
    location.role = SemanticRole::Text;
    location.name = "Current project folder";
    location.value = bounded_semantic_text(selectedDirectory_.generic_string());
    location.visible = true;
    nodes.push_back(std::move(location));

    for (auto node : pendingSemanticNodes_)
    {
        node.parentId = contentsId;
        const auto target = pendingTileTargets_.find(node.id);
        node.selected = target != pendingTileTargets_.end() &&
            (target->second.directory
                ? selectedDirectory_ == target->second.path
                : selectedEntry_ && *selectedEntry_ == target->second.path);
        node.focused = semanticFocusNode_ && *semanticFocusNode_ == node.id;
        nodes.push_back(std::move(node));
    }

    pendingSemanticTargets_.clear();
    semanticTargets_.clear();
    pendingSemanticTargets_.emplace(refreshId, SemanticProjectTarget{});
    for (const auto &[id, target] : pendingTileTargets_)
        pendingSemanticTargets_.emplace(id, target);

    const DocumentIdentity currentIdentity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                           DocumentGeneration{document.documentGeneration}};
    std::error_code selectedEntryError;
    const bool selectedEntryIsDirectory = selectedEntry_ &&
        std::filesystem::is_directory(*selectedEntry_, selectedEntryError);
    if (selectedEntry_ && !selectedEntryError && !selectedEntryIsDirectory &&
        path_is_within(semanticAssetRoot, *selectedEntry_) &&
        editor_asset_is(*selectedEntry_, EditorAssetKind::Template))
    {
        const auto add_template_action = [&](std::string suffix, std::string name,
                                             bool enabled,
                                             SemanticProjectTarget::Operation operation) {
            const std::string id = prefix + "/template/" + suffix;
            SemanticNode node;
            node.id = id;
            node.parentId = prefix;
            node.role = SemanticRole::Button;
            node.name = std::move(name);
            node.value = selectedEntry_->filename().string();
            node.visible = true;
            node.enabled = enabled;
            node.focused = semanticFocusNode_ && *semanticFocusNode_ == id;
            node.actions = {"activate"};
            nodes.push_back(std::move(node));
            SemanticProjectTarget target;
            target.path = *selectedEntry_;
            target.root = semanticAssetRoot;
            target.projectGeneration = ProjectGeneration{document.projectGeneration};
            target.documentIdentity = currentIdentity;
            target.structureRevision = document.structureRevision;
            target.valueRevision = document.valueRevision;
            target.operation = operation;
            pendingSemanticTargets_.emplace(id, std::move(target));
        };

        add_template_action("load", "Load selected object template", true,
                            SemanticProjectTarget::Operation::LoadTemplate);
        if (selectedTemplate_ && selectedTemplatePath_ == *selectedEntry_)
        {
            add_template_action("instantiate", "Instantiate selected object template",
                                templateCanInstantiate_,
                                SemanticProjectTarget::Operation::InstantiateTemplate);
            if (templateTargetsStructureRevision_ == document.structureRevision &&
                templateTargetsValueRevision_ == document.valueRevision)
            {
                const Json bindings = selectedTemplate_->reference_bindings();
                const std::string destinationIdentity = template_scene_identity(document);
                std::size_t exposedCandidates = 0;
                if (bindings.is_array())
                {
                    for (const auto &binding : bindings)
                    {
                        if (!binding.is_object() || binding.value("scope", std::string{}) != "external" ||
                            nodes.size() >= 256 || exposedCandidates >= 64)
                            continue;
                        const std::string kind = binding.value("target_kind", std::string{});
                        const int originalId = binding.value("original_target_id", -1);
                        const std::string key = std::to_string(binding.value("owner_id", -1)) + ":" +
                            std::to_string(binding.value("component_id", -1)) + ":" +
                            binding.value("path", std::string{});
                        const auto candidates = templateTargetCandidates_.find(kind);
                        const bool originalStillValid = candidates != templateTargetCandidates_.end() &&
                            std::any_of(candidates->second.begin(), candidates->second.end(),
                                [originalId](const auto &candidate) {
                                    return candidate.first == originalId;
                                });
                        if (binding.value("source_scene_identity", std::string{}) == destinationIdentity &&
                            originalStillValid)
                            continue;
                        if (candidates == templateTargetCandidates_.end())
                            continue;
                        const auto selectedTarget = externalTemplateTargets_.find(key);
                        for (const auto &[targetId, label] : candidates->second)
                        {
                            if (nodes.size() >= 256 || exposedCandidates >= 64)
                                break;
                            const std::string id = prefix + "/template/assign/" +
                                semantic_text_token(key) + "/" + std::to_string(targetId);
                            SemanticNode node;
                            node.id = id;
                            node.parentId = prefix;
                            node.role = SemanticRole::Button;
                            node.name = bounded_semantic_text(
                                "Assign external " + kind + " reference to " + label);
                            node.visible = true;
                            node.selected = selectedTarget != externalTemplateTargets_.end() &&
                                selectedTarget->second == targetId;
                            node.focused = semanticFocusNode_ && *semanticFocusNode_ == id;
                            node.actions = {"activate"};
                            nodes.push_back(std::move(node));
                            SemanticProjectTarget target;
                            target.path = *selectedEntry_;
                            target.root = semanticAssetRoot;
                            target.projectGeneration = ProjectGeneration{document.projectGeneration};
                            target.documentIdentity = currentIdentity;
                            target.structureRevision = document.structureRevision;
                            target.valueRevision = document.valueRevision;
                            target.operation = SemanticProjectTarget::Operation::AssignTemplateTarget;
                            target.externalAssignmentKey = key;
                            target.externalTargetKind = kind;
                            target.externalTargetId = targetId;
                            pendingSemanticTargets_.emplace(id, std::move(target));
                            ++exposedCandidates;
                        }
                    }
                }
            }
        }
    }

    if (selectedEntry_ && !selectedEntryError && selection.objectId &&
        path_is_within(semanticAssetRoot, *selectedEntry_) && !selectedEntryIsDirectory)
    {
        SelectionModel currentSelection;
        currentSelection.set(document, selection);
        const auto assignments = asset_assignment_targets(document, currentSelection,
                                                          *selectedEntry_);
        std::size_t count = 0;
        for (const auto &assignment : assignments)
        {
            if (count++ >= 32)
                break;
            const std::string id = prefix + "/assign/" +
                semantic_path_token(selectedEntry_->lexically_relative(semanticAssetRoot)) + "/" +
                semantic_text_token(assignment.label + "/" +
                    std::to_string(assignment.address.objectId) + "/" +
                    std::to_string(assignment.address.componentId.value_or(-1)));
            SemanticNode node;
            node.id = id;
            node.parentId = prefix;
            node.role = SemanticRole::Button;
            node.name = bounded_semantic_text("Assign to " + assignment.label);
            node.value = bounded_semantic_text("assets/" +
                selectedEntry_->lexically_relative(semanticAssetRoot).generic_string());
            node.visible = true;
            node.enabled = true;
            node.focused = semanticFocusNode_ && *semanticFocusNode_ == id;
            node.actions = {"activate"};
            nodes.push_back(std::move(node));
            SemanticProjectTarget target;
            target.path = *selectedEntry_;
            target.projectGeneration = ProjectGeneration{document.projectGeneration};
            target.assignment = assignment;
            pendingSemanticTargets_.emplace(id, std::move(target));
        }
    }

    if (semanticFocusNode_ && !pendingSemanticTargets_.contains(*semanticFocusNode_))
        semanticFocusNode_.reset();
    for (const auto &[id, target] : pendingSemanticTargets_)
    {
        (void)target;
        const auto found = std::find_if(nodes.begin(), nodes.end(),
            [&](const SemanticNode &node) { return node.id == id; });
        if (found == nodes.end())
            continue;
        if (semanticFocusNode_ && *semanticFocusNode_ == id)
            found->focused = true;
    }
    semanticTargets_ = pendingSemanticTargets_;
    return nodes;
}

bool ProjectPanel::dispatch_semantic_action(DocumentSession &document,
                                            EditCommandService &commands,
                                            SelectionModel &selection,
                                            const SemanticAction &action,
                                            WindowHostGeneration hostGeneration,
                                            bool allowEdits)
{
    constexpr std::string_view refreshId = "main/panel/project/refresh";
    if (action.action != "activate")
        return false;
    if (action.nodeId == refreshId)
    {
        refresh();
        referenceRetryRequested_ = true;
        return true;
    }
    const auto found = semanticTargets_.find(action.nodeId);
    if (found == semanticTargets_.end())
        return false;
    const auto &target = found->second;
    if (target.projectGeneration &&
        target.projectGeneration != ProjectGeneration{context_.snapshot().generation})
        return false;
    const DocumentIdentity currentIdentity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                           DocumentGeneration{document.documentGeneration}};
    if (target.operation != SemanticProjectTarget::Operation::Tile &&
        (target.documentIdentity != currentIdentity ||
         target.structureRevision != document.structureRevision ||
         target.valueRevision != document.valueRevision))
        return false;
    if (target.operation == SemanticProjectTarget::Operation::LoadTemplate)
    {
        if (!allowEdits || !selectedEntry_ || *selectedEntry_ != target.path ||
            target.root != document.assetRoot || target.projectGeneration !=
                ProjectGeneration{document.projectGeneration} ||
            !editor_asset_is(target.path, EditorAssetKind::Template))
            return false;
        load_selected_template(document);
        semanticFocusNode_ = action.nodeId;
        Selection focused = selection.current();
        focused.focusedPanel = FocusedPanel::Project;
        focused.focusedHost = hostGeneration;
        selection.set(document, std::move(focused));
        return true;
    }
    if (target.operation == SemanticProjectTarget::Operation::AssignTemplateTarget)
    {
        if (!allowEdits || !selectedEntry_ || *selectedEntry_ != target.path ||
            target.root != document.assetRoot || !selectedTemplate_ ||
            selectedTemplatePath_ != target.path || target.externalAssignmentKey.empty() ||
            target.externalTargetKind.empty() || target.externalTargetId < 0)
            return false;
        const auto candidates = templateTargetCandidates_.find(target.externalTargetKind);
        if (candidates == templateTargetCandidates_.end() ||
            std::none_of(candidates->second.begin(), candidates->second.end(),
                [&](const auto &candidate) { return candidate.first == target.externalTargetId; }))
            return false;
        externalTemplateTargets_[target.externalAssignmentKey] = target.externalTargetId;
        templateFeedback_ = "External template reference assigned to the selected destination.";
        semanticFocusNode_ = action.nodeId;
        Selection focused = selection.current();
        focused.focusedPanel = FocusedPanel::Project;
        focused.focusedHost = hostGeneration;
        selection.set(document, std::move(focused));
        return true;
    }
    if (target.operation == SemanticProjectTarget::Operation::InstantiateTemplate)
    {
        if (!allowEdits || !selectedEntry_ || *selectedEntry_ != target.path ||
            target.root != document.assetRoot || !selectedTemplate_ ||
            selectedTemplatePath_ != target.path || !templateCanInstantiate_)
            return false;
        pendingTemplateInstantiate_ = true;
        semanticFocusNode_ = action.nodeId;
        return true;
    }
    if (target.assignment)
    {
        if (!allowEdits || !selectedEntry_ || *selectedEntry_ != target.path ||
            target.projectGeneration != ProjectGeneration{document.projectGeneration})
            return false;
        const Selection before = selection.current();
        const auto candidates = asset_assignment_targets(document, selection, target.path);
        const auto current = std::find_if(candidates.begin(), candidates.end(),
            [&](const AssetAssignmentTarget &candidate) {
                return candidate.label == target.assignment->label &&
                       candidate.address.identity == target.assignment->address.identity &&
                       candidate.address.objectId == target.assignment->address.objectId &&
                       candidate.address.componentId == target.assignment->address.componentId &&
                       candidate.address.fieldPath == target.assignment->address.fieldPath;
            });
        if (current == candidates.end())
            return false;
        std::string error;
        if (!assign_asset_target(document, commands, *current, target.path, error))
        {
            assignmentFeedback_ = std::move(error);
            return false;
        }
        assignmentFeedback_ = "Assigned " + target.path.filename().string() + " to " + current->label + ".";
        semanticFocusNode_ = action.nodeId;
        Selection focused = selection.current();
        focused.focusedPanel = FocusedPanel::Project;
        focused.focusedHost = hostGeneration;
        selection.set(document, std::move(focused));
        commands.set_last_selection_state({before, selection.current()});
        return true;
    }

    if (target.path.empty() || !path_is_within(document.assetRoot, target.path) ||
        !path_is_within(target.root, target.path))
        return false;
    const auto listing = directories_.find(target.path.parent_path().string());
    if (listing == directories_.end() || !listing->second.entriesValid ||
        std::none_of(listing->second.entries.begin(), listing->second.entries.end(),
            [&](const ListedEntry &entry) {
                return entry.path == target.path && entry.isDirectory == target.directory;
            }))
        return false;
    selectedRoot_ = target.root;
    selectedRootLabel_ = target.rootLabel;
    if (target.directory)
    {
        selectedDirectory_ = target.path;
        selectedEntry_.reset();
        expandedDirectories_.insert(target.path.string());
        request_scan(target.path);
    }
    else
        selectedEntry_ = target.path;
    semanticFocusNode_ = action.nodeId;
    Selection focused = selection.current();
    focused.focusedPanel = FocusedPanel::Project;
    focused.focusedHost = hostGeneration;
    selection.set(document, std::move(focused));
    return true;
}

AnimationPanel::PreparedTextureInfo ProjectPanel::prepared_animation_texture(
    const std::filesystem::path &path)
{
    SDL_Texture *texture = thumbnail_for(path);
    const auto [sourceWidth, sourceHeight] = thumbnails_.source_dimensions(path);
    return {texture, sourceWidth, sourceHeight};
}

void ProjectPanel::set_animation_draft_preview(const std::filesystem::path &assetPath,
                                               const std::filesystem::path &assetRoot,
                                               const Json &document,
                                               std::uint64_t historyToken,
                                               std::optional<std::uint64_t> savedHistoryToken)
{
    if (assetPath.empty())
    {
        clear_animation_draft_preview();
        return;
    }
    const std::string key = assetPath.lexically_normal().string();
    if (animationDraftKey_ == key && animationDraftHistoryToken_ == historyToken &&
        animationDraftSavedToken_ == savedHistoryToken && !staleAnimations_.contains(key))
        return;

    if (animationDraftKey_ && *animationDraftKey_ != key)
    {
        const auto oldKey = *animationDraftKey_;
        animations_.erase(oldKey);
        staleAnimations_.insert(oldKey);
        failedAnimationRefreshes_.erase(oldKey);
        auto &oldRevision = animationRevisions_[oldKey];
        if (oldRevision != std::numeric_limits<std::uint64_t>::max())
            ++oldRevision;
    }
    animationDraftKey_ = key;
    animationDraftHistoryToken_ = historyToken;
    animationDraftSavedToken_ = savedHistoryToken;
    auto &revision = animationRevisions_[key];
    if (revision != std::numeric_limits<std::uint64_t>::max())
        ++revision;
    staleAnimations_.erase(key);
    failedAnimationRefreshes_.erase(key);

    const auto prepared = prepare_animation_preview(document, assetRoot);
    if (!prepared.valid)
    {
        failedAnimationRefreshes_.insert(key);
        coverageMessage_ = prepared.error;
        return;
    }
    animations_[key] = convert_animation_preview(prepared);
    for (const auto &frame : animations_[key].frames)
        remember_presentation_dependency(frame.texturePath, assetRoot);
}

ProjectPanel::AnimationPreview ProjectPanel::convert_animation_preview(
    const AnimationPreviewDocument &source) const
{
    AnimationPreview preview;
    if (!source.valid)
        return preview;
    preview.framesPerSecond = std::isfinite(source.framesPerSecond)
        ? static_cast<float>(source.framesPerSecond) : 0.0f;
    preview.totalDuration = source.totalDuration;
    preview.frameEndTimes = source.frameEndTimes;
    preview.loop = source.loop;
    preview.loaded = true;
    preview.startedAt = std::chrono::steady_clock::now();
    preview.frames.reserve(source.frames.size());
    for (const auto &sourceFrame : source.frames)
    {
        AnimationFramePreview frame;
        frame.texturePath = sourceFrame.texturePath;
        frame.sourceX = static_cast<float>(sourceFrame.sourceX);
        frame.sourceY = static_cast<float>(sourceFrame.sourceY);
        frame.sourceWidth = static_cast<float>(sourceFrame.sourceWidth);
        frame.sourceHeight = static_cast<float>(sourceFrame.sourceHeight);
        frame.hasSourceRect = sourceFrame.hasSourceRect;
        frame.durationSeconds = sourceFrame.durationSeconds;
        preview.frames.push_back(std::move(frame));
    }
    return preview;
}

void ProjectPanel::clear_animation_draft_preview()
{
    if (!animationDraftKey_)
        return;
    const auto key = *animationDraftKey_;
    animationDraftKey_.reset();
    animationDraftHistoryToken_ = 0;
    animationDraftSavedToken_.reset();
    animations_.erase(key);
    staleAnimations_.insert(key);
    failedAnimationRefreshes_.erase(key);
    auto &revision = animationRevisions_[key];
    if (revision != std::numeric_limits<std::uint64_t>::max())
        ++revision;
}

void ProjectPanel::end_frame() noexcept
{
    thumbnails_.end_frame();
    ownerUploadPollDoneThisFrame_ = false;
}

std::optional<std::chrono::steady_clock::time_point>
ProjectPanel::next_animation_deadline() const noexcept
{
    if (nextAnimationDeadline_ && *nextAnimationDeadline_ <= std::chrono::steady_clock::now())
        return std::nullopt;
    return nextAnimationDeadline_;
}

void ProjectPanel::release_preview_resources() noexcept
{
    thumbnails_.clear(renderer_);
    animations_.clear();
    staleAnimations_.clear();
    failedAnimationRefreshes_.clear();
    animationRevisions_.clear();
    animationDraftKey_.reset();
    animationDraftHistoryToken_ = 0;
    animationDraftSavedToken_.reset();
    {
        std::lock_guard lock(animationMutex_);
        ++animationCacheGeneration_;
        if (animationCacheGeneration_ == 0)
            ++animationCacheGeneration_;
        preparedAnimations_.clear();
    }
    presentationDependencies_.clear();
    presentationDependenciesSaturated_ = false;
}

void ProjectPanel::refresh()
{
    const auto retryAt = std::chrono::steady_clock::now();
    for (auto &[path, state] : directories_)
    {
        (void)path;
        state.request.cancel();
        changes_.cancel_directory_listing(state.changeRequest);
        state.request = {};
        state.changeRequest = {};
        state.loaded = false;
        state.error.clear();
        state.retry.request_now(retryAt);
    }
    requestPaths_.clear();
    release_preview_resources();
    coverageMessage_ = "Asset folder refresh requested.";
}

bool ProjectPanel::take_reference_retry_request() noexcept
{
    return std::exchange(referenceRetryRequested_, false);
}

void ProjectPanel::set_reference_retry_status(std::size_t scheduled)
{
    referenceRetryMessage_ = scheduled == 0
        ? "No failed referenced Scene assets needed retry."
        : "Retrying " + std::to_string(scheduled) + " failed referenced Scene asset(s).";
}

std::size_t ProjectPanel::apply_changes(const ProjectChangeBatch &batch,
                                        const ResourceChangeHandler &resourceChange)
{
    if (batch.projectGeneration.value != context_.snapshot().generation)
        return 0;
    work::record(work::Counter::AssetChanges, batch.events.size());
    if (lastProjectGeneration_ != batch.projectGeneration.value)
    {
        lastProjectGeneration_ = batch.projectGeneration.value;
        rootRecoveryActive_ = false;
        pendingChanges_.clear();
        pendingChangePathCount_ = 0;
    }
    for (const auto &event : batch.events)
    {
        const std::size_t pathCount = event.affectedPaths.empty()
            ? 1U + (event.kind == ProjectChangeKind::Renamed && !event.oldPath.empty() ? 1U : 0U)
            : event.affectedPaths.size();
        pendingChanges_.push_back(PendingProjectChange{event, 0});
        pendingChangePathCount_ += pathCount;
    }
    const auto &assetRoot = context_.snapshot().assetRoot;
    if (batch.rootDirty)
    {
        coverageMessage_ = batch.coverage == ProjectCoverageState::RootUnavailable
            ? "Asset folder is unavailable. Reopen or refresh the project folder."
            : "Some asset changes may be missing. Refresh to reconcile the folder.";
        if (!rootRecoveryActive_)
        {
            rootRecoveryActive_ = true;
            if (batch.coverage == ProjectCoverageState::RootUnavailable)
            {
                for (auto &[key, state] : directories_)
                {
                    (void)key;
                    if (state.request)
                    {
                        requestPaths_.erase(state.request.request_id());
                        state.request.cancel();
                        state.request = {};
                        changes_.cancel_directory_listing(state.changeRequest);
                        state.changeRequest = {};
                    }
                    state.loaded = false;
                    state.error = coverageMessage_;
                }
            }
            else
            {
                for (auto &[key, state] : directories_)
                {
                    (void)key;
                    if (state.request)
                    {
                        requestPaths_.erase(state.request.request_id());
                        state.request.cancel();
                        state.request = {};
                        changes_.cancel_directory_listing(state.changeRequest);
                        state.changeRequest = {};
                    }
                    state.loaded = false;
                    state.error.clear();
                    state.truncated = false;
                    state.retry.request_now(std::chrono::steady_clock::now());
                }
            }
        }
    }
    else
    {
        rootRecoveryActive_ = false;
        if (batch.coverage == ProjectCoverageState::Current)
            coverageMessage_.clear();
    }
    if (!batch.rootDirty && batch.coverage == ProjectCoverageState::Degraded)
        coverageMessage_ = "Asset monitoring is recovering; visible folders can still be refreshed.";

    const auto invalidate_listing = [&](const std::filesystem::path &directory, bool subtree) {
        for (auto &[key, state] : directories_)
        {
            const std::filesystem::path cachedDirectory(key);
            if (cachedDirectory != directory && (!subtree || !path_is_within(directory, cachedDirectory)))
                continue;
            if (state.request)
            {
                requestPaths_.erase(state.request.request_id());
                state.request.cancel();
                state.request = {};
                changes_.cancel_directory_listing(state.changeRequest);
                state.changeRequest = {};
            }
            state.loaded = false;
            state.error.clear();
            state.truncated = false;
            state.retry.request_now(std::chrono::steady_clock::now());
        }
    };
    const auto invalidate_preview_path = [&](const std::filesystem::path &path, bool subtree,
                                             std::uint64_t eventSequence) {
        const auto affects = [&](const std::filesystem::path &candidate) {
            return candidate == path || (subtree && path_is_within(path, candidate));
        };
        thumbnails_.invalidate(path, subtree, eventSequence);
        for (const auto &[key, animation] : animations_)
        {
            bool affected = affects(std::filesystem::path(key));
            if (!affected)
                affected = std::any_of(animation.frames.begin(), animation.frames.end(),
                    [&](const AnimationFramePreview &frame) {
                        return affects(frame.texturePath);
                    });
            if (affected)
            {
                staleAnimations_.insert(key);
                failedAnimationRefreshes_.erase(key);
                auto &revision = animationRevisions_[key];
                ++revision;
                if (revision == 0)
                    ++revision;
            }
        }
    };
    const auto canonical_event_path = [&](const std::filesystem::path &path) {
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            error.clear();
            canonical = std::filesystem::absolute(path, error);
            if (error)
                canonical = path;
        }
        return canonical.lexically_normal();
    };
    const auto apply_path = [&](const std::filesystem::path &path, bool directory,
                                std::uint64_t eventSequence) {
        if (path.empty() || !path_is_within(assetRoot, path))
            return;
        const auto alias_is_affected = [&](const auto &ancestors) {
            return std::any_of(ancestors.begin(), ancestors.end(), [&](const auto &ancestor) {
                const auto aliasPath = std::filesystem::path(ancestor).lexically_normal();
                return aliasPath == path.lexically_normal() ||
                       (directory && path_is_within(path, aliasPath));
            });
        };
        const auto unresolved_is_affected = [&](const auto &targets) {
            return std::any_of(targets.begin(), targets.end(), [&](const auto &target) {
                const auto targetPath = std::filesystem::path(target).lexically_normal();
                return targetPath == path.lexically_normal() ||
                       (directory && path_is_within(path, targetPath));
            });
        };
        // Native backends may report a replaced directory symlink as an ordinary
        // file event. Cached logical directory/listing and presentation descendants
        // provide stronger evidence than that event hint.
        const bool cachedLogicalDirectory = std::any_of(directories_.begin(), directories_.end(),
            [&](const auto &entry) {
                const std::filesystem::path cachedDirectory(entry.first);
                return cachedDirectory == path || path_is_within(path, cachedDirectory);
            });
        const bool hasCachedPresentationDescendant =
            std::any_of(presentationDependencies_.begin(), presentationDependencies_.end(),
                [&](const auto &entry) {
                    const std::filesystem::path logicalPath(entry.first);
                    const bool logicalDescendant = logicalPath != path && path_is_within(path, logicalPath);
                    const bool resolvedThroughChangedAlias = alias_is_affected(
                        entry.second.aliasAncestors);
                    const bool unresolvedTargetChanged = unresolved_is_affected(
                        entry.second.unresolvedTargets);
                    return logicalDescendant || resolvedThroughChangedAlias || unresolvedTargetChanged;
                });
        const bool hasCachedDirectoryAlias = std::any_of(directories_.begin(), directories_.end(),
            [&](const auto &entry) {
                return alias_is_affected(entry.second.aliasAncestors) ||
                       unresolved_is_affected(entry.second.unresolvedTargets);
            });
        const bool cachedLogicalSubtree = directory || cachedLogicalDirectory ||
                                          hasCachedPresentationDescendant || hasCachedDirectoryAlias;
        invalidate_listing(path.parent_path(), false);
        invalidate_preview_path(path, cachedLogicalSubtree, eventSequence);
        // A symlink retarget changes the logical directory's listing even when the
        // notification describes the link itself rather than its former target.
        invalidate_listing(path, true);
        changes_.invalidate_directory_revision(path, true);

        const auto canonicalChanged = canonical_event_path(path);
        std::vector<std::filesystem::path> aliases;
        for (const auto &[cachedKey, state] : directories_)
        {
            const std::filesystem::path cachedDirectory(cachedKey);
            if (state.canonicalRoot.empty() || cachedDirectory == state.canonicalRoot ||
                (!path_is_within(state.canonicalRoot, canonicalChanged) &&
                 (!cachedLogicalSubtree || !path_is_within(canonicalChanged, state.canonicalRoot))))
                continue;
            aliases.push_back(cachedDirectory);
        }
        for (const auto &alias : aliases)
        {
            changes_.invalidate_directory_revision(alias, true);
            invalidate_listing(alias, true);
        }

        std::vector<std::filesystem::path> aliasedDirectories;
        for (const auto &[cachedKey, state] : directories_)
        {
            if (!alias_is_affected(state.aliasAncestors) &&
                !unresolved_is_affected(state.unresolvedTargets))
                continue;
            const std::filesystem::path cachedDirectory(cachedKey);
            aliasedDirectories.push_back(cachedDirectory);
        }
        for (const auto &alias : aliasedDirectories)
        {
            changes_.invalidate_directory_revision(alias, true);
            invalidate_listing(alias, true);
        }

        for (auto &[cachedKey, state] : directories_)
        {
            const std::filesystem::path cachedDirectory(cachedKey);
            const bool logicalDescendant = cachedDirectory == path ||
                (cachedLogicalSubtree && path_is_within(path, cachedDirectory));
            const bool resolvedThroughChangedAlias = alias_is_affected(state.aliasAncestors);
            const bool unresolvedTargetChanged = unresolved_is_affected(state.unresolvedTargets);
            if (!logicalDescendant && !resolvedThroughChangedAlias && !unresolvedTargetChanged)
                continue;
            remember_directory_dependency(state, cachedDirectory, assetRoot);
        }

        std::vector<std::filesystem::path> affectedPresentationPaths;
        for (const auto &[logicalKey, dependency] : presentationDependencies_)
        {
            const auto logicalPath = std::filesystem::path(logicalKey);
            const bool overlapsCanonicalPath = dependency.canonicalPath == canonicalChanged ||
                (cachedLogicalSubtree && (path_is_within(dependency.canonicalPath, canonicalChanged) ||
                                          path_is_within(canonicalChanged, dependency.canonicalPath)));
            const bool resolvedThroughChangedAlias = alias_is_affected(
                dependency.aliasAncestors);
            const bool unresolvedTargetChanged = unresolved_is_affected(dependency.unresolvedTargets);
            if (logicalPath == path || (cachedLogicalSubtree && path_is_within(path, logicalPath)) ||
                overlapsCanonicalPath || resolvedThroughChangedAlias || unresolvedTargetChanged)
                affectedPresentationPaths.emplace_back(logicalKey);
        }
        for (const auto &logicalPath : affectedPresentationPaths)
            invalidate_preview_path(logicalPath, false, eventSequence);
        if (presentationDependenciesSaturated_)
            invalidate_preview_path(assetRoot, true, eventSequence);

        // A link replacement can redirect every cached presentation below its logical path.
        // Refresh those mappings only when the new targets resolve; missing targets keep their
        // last-known canonical identities so a later delete/recreate still invalidates them.
        std::vector<std::filesystem::path> rebindPresentationPaths;
        for (const auto &[logicalKey, dependency] : presentationDependencies_)
        {
            const auto logicalPath = std::filesystem::path(logicalKey);
            const bool resolvedThroughChangedAlias = alias_is_affected(
                dependency.aliasAncestors);
            const bool unresolvedTargetChanged = unresolved_is_affected(dependency.unresolvedTargets);
            if (logicalPath == path || (cachedLogicalSubtree && path_is_within(path, logicalPath)) ||
                resolvedThroughChangedAlias || unresolvedTargetChanged)
                rebindPresentationPaths.push_back(logicalPath);
        }
        for (const auto &logicalPath : rebindPresentationPaths)
            remember_presentation_dependency(logicalPath, assetRoot);
    };
    const auto commitStarted = std::chrono::steady_clock::now();
    std::size_t committedPaths = 0;
    while (!pendingChanges_.empty() &&
           committedPaths < MaxOwnerChangePathsPerFrame &&
           (committedPaths == 0 || std::chrono::steady_clock::now() - commitStarted <
                                       MaxOwnerChangeCommitTime))
    {
        PendingProjectChange &pending = pendingChanges_.front();
        const ProjectChangeEvent &event = pending.event;
        const std::size_t pathCount = event.affectedPaths.empty()
            ? 1U + (event.kind == ProjectChangeKind::Renamed && !event.oldPath.empty() ? 1U : 0U)
            : event.affectedPaths.size();
        if (pending.nextPath < pathCount)
        {
            const std::filesystem::path &path = event.affectedPaths.empty()
                ? (pending.nextPath == 0 ? event.path : event.oldPath)
                : event.affectedPaths[pending.nextPath];
            apply_path(path, event.directory, event.sequence);
            if (resourceChange && !path.empty())
                resourceChange(event, path);
            if (event.kind == ProjectChangeKind::Removed)
            {
                if (selectedEntry_ && path_is_within(path, *selectedEntry_))
                    selectedEntry_.reset();
                if (event.directory && path_is_within(path, selectedDirectory_))
                    selectedDirectory_ = path.parent_path();
                if (event.directory && path_is_within(path, selectedRoot_))
                    selectedRoot_ = assetRoot;
            }
            ++pending.nextPath;
            ++committedPaths;
            --pendingChangePathCount_;
            work::record(work::Counter::OwnerAssetChangePaths);
        }
        if (pending.nextPath >= pathCount)
        {
            if (event.kind == ProjectChangeKind::Renamed)
            {
                const auto rebase = [&](std::filesystem::path &selected) {
                    if (!selected.empty() && path_is_within(event.oldPath, selected))
                    {
                        const auto relative = selected.lexically_relative(event.oldPath);
                        selected = (event.path / relative).lexically_normal();
                    }
                };
                rebase(selectedDirectory_);
                rebase(selectedRoot_);
                if (selectedEntry_)
                    rebase(*selectedEntry_);
            }
            pendingChanges_.pop_front();
        }
        else if (committedPaths >= MaxOwnerChangePathsPerFrame ||
                 std::chrono::steady_clock::now() - commitStarted >= MaxOwnerChangeCommitTime)
        {
            break;
        }
    }
    return committedPaths;
}

std::size_t ProjectPanel::pending_change_paths() const noexcept
{
    return pendingChangePathCount_;
}

void ProjectPanel::remember_presentation_dependency(
    const std::filesystem::path &logicalPath, const std::filesystem::path &assetRoot)
{
    if (logicalPath.empty() || assetRoot.empty() || !path_is_within(assetRoot, logicalPath))
        return;
    const std::string key = logicalPath.string();
    auto trace = resolved_symlink_ancestors(logicalPath, assetRoot);
    std::error_code error;
    auto canonicalPath = std::filesystem::canonical(logicalPath, error);
    const bool canonicalValid = !error && path_is_within(assetRoot, canonicalPath);
    const bool linkChainResolved = trace.allLinksResolve;
    auto existing = presentationDependencies_.find(key);
    if (existing != presentationDependencies_.end())
    {
        if (linkChainResolved)
        {
            if (canonicalValid)
                existing->second.canonicalPath = std::move(canonicalPath);
            existing->second.aliasAncestors = std::move(trace.ancestors);
            existing->second.unresolvedTargets = std::move(trace.unresolvedTargets);
            return;
        }

        // Keep last-good canonical and alias identities through a broken retarget,
        // while replacing the unresolved target with the path currently demanded.
        constexpr std::size_t MaxRetainedAliasAncestors = 128;
        for (const auto &ancestor : trace.ancestors)
            if (path_is_within(assetRoot, ancestor) &&
                existing->second.aliasAncestors.size() < MaxRetainedAliasAncestors &&
                std::find(existing->second.aliasAncestors.begin(), existing->second.aliasAncestors.end(), ancestor) ==
                    existing->second.aliasAncestors.end())
                existing->second.aliasAncestors.push_back(ancestor);
        existing->second.unresolvedTargets = std::move(trace.unresolvedTargets);
        return;
    }

    if (linkChainResolved && canonicalValid)
    {
        // Track only cached presentation paths and bound the dependency walk on each event.
        // Once saturated, canonical changes conservatively invalidate this panel's previews.
        constexpr std::size_t MaxPresentationDependencies = 20000;
        if (presentationDependencies_.size() >= MaxPresentationDependencies)
        {
            presentationDependenciesSaturated_ = true;
            return;
        }
        presentationDependencies_.emplace(key, PresentationDependency{
            .canonicalPath = std::move(canonicalPath),
            .aliasAncestors = std::move(trace.ancestors)});
        return;
    }

    // A newly demanded broken alias still needs a repair dependency even though
    // no canonical presentation identity exists yet.
    if (trace.ancestors.empty() && trace.unresolvedTargets.empty())
        return;
    constexpr std::size_t MaxPresentationDependencies = 20000;
    if (presentationDependencies_.size() >= MaxPresentationDependencies)
    {
        presentationDependenciesSaturated_ = true;
        return;
    }
    presentationDependencies_.emplace(key, PresentationDependency{
        .canonicalPath = canonicalValid ? std::move(canonicalPath) : std::filesystem::path{},
        .aliasAncestors = std::move(trace.ancestors),
        .unresolvedTargets = std::move(trace.unresolvedTargets)});
}

void ProjectPanel::remember_directory_dependency(DirectoryState &state,
                                                 const std::filesystem::path &logicalPath,
                                                 const std::filesystem::path &assetRoot)
{
    if (logicalPath.empty() || assetRoot.empty() || !path_is_within(assetRoot, logicalPath))
        return;
    auto trace = resolved_symlink_ancestors(logicalPath, assetRoot);
    std::error_code error;
    const auto canonical = std::filesystem::canonical(logicalPath, error);
    const bool canonicalValid = !error && path_is_within(assetRoot, canonical) &&
                                std::filesystem::is_directory(canonical, error) && !error;
    if (trace.allLinksResolve)
    {
        state.aliasAncestors = std::move(trace.ancestors);
        state.unresolvedTargets = std::move(trace.unresolvedTargets);
        if (canonicalValid)
            state.canonicalRoot = canonical;
        return;
    }

    constexpr std::size_t MaxRetainedAliasAncestors = 128;
    for (const auto &ancestor : trace.ancestors)
        if (path_is_within(assetRoot, ancestor) && state.aliasAncestors.size() < MaxRetainedAliasAncestors &&
            std::find(state.aliasAncestors.begin(), state.aliasAncestors.end(), ancestor) ==
                state.aliasAncestors.end())
            state.aliasAncestors.push_back(ancestor);
    state.unresolvedTargets = std::move(trace.unresolvedTargets);
}

void ProjectPanel::apply_ready_results()
{
    if (!ownerUploadPollDoneThisFrame_)
        thumbnails_.poll_uploads(frameRenderer_ != nullptr ? frameRenderer_ : renderer_);
    apply_animation_results();
    for (auto &result : scans_.take_ready(context_.snapshot().generation))
    {
        const auto found = requestPaths_.find(result.requestId);
        if (found == requestPaths_.end())
            continue;
        auto state = directories_.find(found->second);
        requestPaths_.erase(found);
        if (state == directories_.end())
            continue;
        remember_directory_dependency(state->second, result.root, context_.snapshot().assetRoot);
        const DirectoryListingToken listing = state->second.changeRequest;
        state->second.request = {};
        state->second.changeRequest = {};
        if (result.cancelled)
        {
            changes_.cancel_directory_listing(listing);
            continue;
        }
        std::error_code rootError;
        const bool rootUnavailable = !std::filesystem::is_directory(result.root, rootError) ||
                                     static_cast<bool>(rootError);
        const bool complete = !result.truncated && result.error.empty();
        const bool current = listing.requestId == 0 || changes_.directory_listing_current(listing);
        if (listing.requestId != 0 && current)
            changes_.complete_directory_listing(listing, complete, rootUnavailable);
        if (!current)
        {
            state->second.error = "Folder changed while scanning; refreshing the latest listing.";
            state->second.loaded = false;
            state->second.retry.request_now(std::chrono::steady_clock::now());
            continue;
        }
        if (!complete)
        {
            state->second.error = result.error.empty()
                ? "Folder listing was truncated; the last complete listing was retained."
                : "Folder listing failed: " + result.error;
            state->second.loaded = false;
            state->second.truncated = result.truncated;
            state->second.retry.retry_after_failure(std::chrono::steady_clock::now());
            continue;
        }
        state->second.loaded = true;
        state->second.hasCompleteSnapshot = true;
        state->second.canonicalRoot = result.canonicalRoot;
        remember_directory_dependency(state->second, result.root, context_.snapshot().assetRoot);
        state->second.error.clear();
        state->second.truncated = false;
        state->second.retry.schedule_success(std::chrono::steady_clock::now(),
                                             RecoveryRetrySchedule::Clock::duration::zero());
        state->second.files = std::move(result.files);
        state->second.directories = std::move(result.directories);
        state->second.entriesValid = false;
    }
}

void ProjectPanel::apply_animation_results()
{
    std::vector<PreparedAnimation> ready;
    {
        std::lock_guard lock(animationMutex_);
        constexpr std::size_t MaxAnimationResultsPerFrame = 8;
        while (!preparedAnimations_.empty() && ready.size() < MaxAnimationResultsPerFrame)
        {
            ready.push_back(std::move(preparedAnimations_.front()));
            preparedAnimations_.pop_front();
            animationsInFlight_.erase(ready.back().key);
        }
    }

    const auto generation = context_.snapshot().generation;
    for (auto &result : ready)
    {
        if (result.projectGeneration.value != generation ||
            result.cacheGeneration != animationCacheGeneration_ ||
            result.revision != animationRevisions_[result.key])
            continue;
        if (result.success)
        {
            for (const auto &frame : result.preview.frames)
                remember_presentation_dependency(frame.texturePath,
                                                   context_.snapshot().assetRoot);
            animations_[result.key] = std::move(result.preview);
            staleAnimations_.erase(result.key);
            failedAnimationRefreshes_.erase(result.key);
        }
        else
        {
            auto [entry, inserted] = animations_.try_emplace(result.key);
            (void)inserted;
            if (!entry->second.loaded)
                entry->second.loaded = true;
            failedAnimationRefreshes_.insert(result.key);
            staleAnimations_.erase(result.key);
            coverageMessage_ = result.error.empty()
                ? "An animation preview is unavailable; click Refresh Assets to retry."
                : result.error;
        }
    }
}

void ProjectPanel::request_scan(const std::filesystem::path &directory)
{
    const auto key = directory.string();
    auto &state = directories_[key];
    if (state.loaded || state.request || !state.retry.due(std::chrono::steady_clock::now()))
        return;

    remember_directory_dependency(state, directory, context_.snapshot().assetRoot);
    state.error.clear();
    state.changeRequest = changes_.begin_directory_listing(directory);
    try
    {
        state.request = scans_.request_scan(context_.snapshot(), directory,
            ProjectScanOptions{.recursive = false, .maxFiles = 10000,
                               .includeDirectories = true, .maxDirectories = 2000,
                               .maxEntries = 20000});
        if (state.request)
            requestPaths_[state.request.request_id()] = key;
        else
        {
            changes_.cancel_directory_listing(state.changeRequest);
            state.changeRequest = {};
            state.error = "Asset scan service reached its outstanding-scan limit.";
            state.loaded = true;
        }
    }
    catch (const std::exception &error)
    {
        changes_.cancel_directory_listing(state.changeRequest);
        state.changeRequest = {};
        state.error = error.what();
        state.loaded = true;
    }
}

void ProjectPanel::draw_folder_tree(const std::filesystem::path &root,
                                    const std::filesystem::path &directory,
                                    const std::string &rootLabel, std::size_t depth)
{
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    const auto key = directory.string();
    auto &state = directories_[key];
    const auto label = folder_label(root, directory, rootLabel);
    const bool leaf = (state.loaded || state.hasCompleteSnapshot) && state.directories.empty();
    const bool wasExpanded = expandedDirectories_.contains(key);

    ImGui::PushID(key.c_str());
    if (depth > 0)
        ImGui::Indent(16.0f * uiScale);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selectedDirectory_ == directory)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (leaf)
        flags |= ImGuiTreeNodeFlags_Leaf;
    ImGui::SetNextItemOpen(wasExpanded, ImGuiCond_Always);
    const bool open = ImGui::TreeNodeEx("##asset-folder", flags, "");
    if (open != wasExpanded)
    {
        if (open)
            expandedDirectories_.insert(key);
        else
            expandedDirectories_.erase(key);
    }

    const ImVec2 rowMinimum = ImGui::GetItemRectMin();
    const ImVec2 rowMaximum = ImGui::GetItemRectMax();
    const float iconSize = 14.0f * uiScale;
    const float iconX = rowMinimum.x + ImGui::GetTreeNodeToLabelSpacing() + 3.0f * uiScale;
    const float iconY = rowMinimum.y + (rowMaximum.y - rowMinimum.y - iconSize) * 0.5f;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    draw_editor_icon(EditorIcon::Folder, drawList, {iconX, iconY}, iconSize,
                     ImGui::GetColorU32(ImGuiCol_Text));
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const float textX = iconX + iconSize + 5.0f * uiScale;
    drawList->PushClipRect({textX, rowMinimum.y}, rowMaximum, true);
    drawList->AddText({textX, rowMinimum.y + (rowMaximum.y - rowMinimum.y - textSize.y) * 0.5f},
                      ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    drawList->PopClipRect();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", label.c_str());
    if (ImGui::IsItemClicked())
    {
        selectedDirectory_ = directory;
        selectedRoot_ = root;
        selectedRootLabel_ = rootLabel;
        selectedEntry_.reset();
    }

    if (open)
    {
        request_scan(directory);
        if (!state.error.empty())
            ImGui::TextWrapped("%s", state.error.c_str());
        if (!state.loaded && !state.hasCompleteSnapshot)
            ImGui::TextDisabled("Scanning...");
        else if (state.loaded || state.hasCompleteSnapshot)
        {
            for (const auto &child : state.directories)
                draw_folder_tree(root, child, rootLabel, depth + 1);
            if (state.truncated)
                ImGui::TextDisabled("Folder listing reached its scan limit.");
        }
    }
    if (depth > 0)
        ImGui::Unindent(16.0f * uiScale);
    ImGui::PopID();
}

SDL_Texture *ProjectPanel::thumbnail_for(const std::filesystem::path &path)
{
    SDL_Renderer *renderer = frameRenderer_ != nullptr ? frameRenderer_ : renderer_;
    if (renderer == nullptr)
        return nullptr;
    remember_presentation_dependency(path, context_.snapshot().assetRoot);
    SDL_Texture *texture = thumbnails_.request(path, renderer);
    if (texture == nullptr)
    {
        const std::string failure = thumbnails_.failure_message(path);
        if (!failure.empty())
            coverageMessage_ = failure;
    }
    return texture;
}

const ProjectPanel::AnimationPreview &ProjectPanel::animation_for(
    const std::filesystem::path &path, const std::filesystem::path &assetRoot)
{
    remember_presentation_dependency(path, assetRoot);
    const std::string key = path.string();
    auto [existing, inserted] = animations_.try_emplace(key);
    (void)inserted;
    if (existing->second.loaded && !staleAnimations_.contains(key))
        return existing->second;
    if (!staleAnimations_.contains(key) && failedAnimationRefreshes_.contains(key))
        return existing->second;
    request_animation_preparation(path, assetRoot, key);
    return existing->second;
}

void ProjectPanel::request_animation_preparation(const std::filesystem::path &path,
                                                  const std::filesystem::path &assetRoot,
                                                  const std::string &key)
{
    constexpr std::size_t MaxAnimationPreparationPaths = 64;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    const auto projectGeneration = context_.snapshot().generation;
    const auto wakeCallback = wakeCallback_;
    {
        std::lock_guard lock(animationMutex_);
        if (animationsInFlight_.contains(key) ||
            animationsInFlight_.size() >= MaxAnimationPreparationPaths)
            return;
        animationsInFlight_.insert(key);
        generation = animationCacheGeneration_;
        revision = animationRevisions_[key];
    }

    animationTasks_.run([this, path, assetRoot, key, generation, revision, projectGeneration,
                         wakeCallback] {
        PreparedAnimation result;
        result.key = key;
        result.projectGeneration = ProjectGeneration{projectGeneration};
        result.cacheGeneration = generation;
        result.revision = revision;
        try
        {
            std::error_code fileError;
            const auto size = std::filesystem::file_size(path, fileError);
            if (fileError || size == 0)
                throw std::runtime_error("Animation preview file is unavailable.");
            if (size > 1024U * 1024U)
                throw std::runtime_error("Animation preview description exceeds the 1 MiB limit.");
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Could not open the animation preview.");
            std::string source(static_cast<std::size_t>(size), '\0');
            input.read(source.data(), static_cast<std::streamsize>(source.size()));
            if (!input || input.peek() != std::char_traits<char>::eof())
                throw std::runtime_error("Animation preview changed while it was being prepared.");
            const Json description = Json::parse(source);
            const auto prepared = prepare_animation_preview(description, assetRoot);
            if (!prepared.valid)
                throw std::runtime_error(prepared.error);
            result.preview = convert_animation_preview(prepared);
            result.success = true;
        }
        catch (const std::exception &error)
        {
            result.error = error.what();
        }
        catch (...)
        {
            result.error = "Unknown animation preview preparation failure.";
        }

        {
            std::lock_guard lock(animationMutex_);
            if (generation != animationCacheGeneration_ ||
                preparedAnimations_.size() >= MaxAnimationPreparationPaths)
            {
                animationsInFlight_.erase(key);
                return;
            }
            preparedAnimations_.push_back(std::move(result));
        }
        if (wakeCallback)
        {
            try { wakeCallback(); }
            catch (...) {}
        }
    });
}

void ProjectPanel::draw_asset_tile(const std::filesystem::path &path,
                                   const std::filesystem::path &assetRoot,
                                   bool isDirectory, bool behaviorRoot,
                                   float tileWidth, float tileHeight)
{
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    const std::string entryKey = path.string();
    const std::string label = path.filename().string();
    const bool selected = selectedEntry_ && *selectedEntry_ == path;
    ImGui::PushID(entryKey.c_str());
    const bool clicked = ImGui::InvisibleButton("##asset-tile", {tileWidth, tileHeight});
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    const bool behaviorSource = behaviorRoot && editor_asset_format(path).behaviorHeader;
    if (!isDirectory && (!behaviorRoot || behaviorSource) && ImGui::BeginDragDropSource())
    {
        if (behaviorSource)
        {
            const auto behaviorType = behavior_type_from_source(path);
            if (behaviorType)
            {
                ImGui::SetDragDropPayload("PLATFORMATOR_BEHAVIOR_TYPE", behaviorType->c_str(),
                                          behaviorType->size() + 1);
                ImGui::Text("Add %s", behaviorType->c_str());
            }
            else
                ImGui::TextUnformatted("Could not find a registered behavior class in this source file.");
        }
        else
        {
            const auto relativeAsset = path.lexically_relative(assetRoot).generic_string();
            const std::string reference = "assets/" + relativeAsset;
            AssetDragPayload payload{};
            std::snprintf(payload.relativeAssetPath, sizeof(payload.relativeAssetPath), "%s", reference.c_str());
            payload.kind = editor_asset_format(path).kind;
            ImGui::SetDragDropPayload("PLATFORMATOR_ASSET_PATH", &payload, sizeof(payload));
            ImGui::TextUnformatted(path.filename().string().c_str());
        }
        ImGui::EndDragDropSource();
    }
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImU32 background = selected ? ImGui::GetColorU32(ImGuiCol_Header)
                            : hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered)
                                      : ImGui::GetColorU32(ImGuiCol_FrameBg);
    drawList->AddRectFilled(minimum, maximum, background, 4.0f * uiScale);
    drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border), 4.0f * uiScale);

    SDL_Texture *texture = nullptr;
    AnimationFramePreview frame;
    bool useFrameRect = false;
    std::string previewState;
    std::string previewDetail;
    if (!isDirectory && editor_asset_format(path).kind == EditorAssetKind::Animation)
    {
        const auto &animation = animation_for(path, assetRoot);
        const std::string animationKey = path.string();
        if (staleAnimations_.contains(animationKey))
        {
            previewState = animation.frames.empty() ? "Preparing preview" : "Updating preview";
            previewDetail = "A changed animation preview is being prepared; the last complete frames remain available.";
        }
        else if (failedAnimationRefreshes_.contains(animationKey))
        {
            previewState = "Preview unavailable";
            previewDetail = coverageMessage_;
        }
        else if (!animation.loaded)
            previewState = "Preparing preview";
        else if (animation.frames.empty())
            previewState = "No preview frames";
        if (!animation.frames.empty())
        {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - animation.startedAt).count();
            const double previewPosition = animation.loop && animation.totalDuration > 0.0
                ? std::fmod(std::max(0.0, elapsed), animation.totalDuration)
                : std::min(std::max(0.0, elapsed), animation.totalDuration);
            const std::size_t currentFrame = animation_preview_frame_at_time(
                animation.frameEndTimes, animation.totalDuration, animation.loop, elapsed);
            if (animation.frames.size() > 1 &&
                (animation.loop || currentFrame + 1 < animation.frames.size()))
            {
                const double nextBoundary = currentFrame + 1 < animation.frameEndTimes.size()
                    ? animation.frameEndTimes[currentFrame]
                    : animation.totalDuration;
                const double delay = std::max(0.001, nextBoundary - previewPosition);
                auto deadline = now + std::chrono::duration_cast<
                    std::chrono::steady_clock::duration>(std::chrono::duration<double>(delay));
                if (deadline <= now)
                    deadline = now + std::chrono::milliseconds(1);
                if (!nextAnimationDeadline_ || deadline < *nextAnimationDeadline_)
                    nextAnimationDeadline_ = deadline;
            }
            const std::size_t frameIndex = currentFrame;
            frame = animation.frames[frameIndex];
            if (!frame.texturePath.empty())
                texture = thumbnail_for(frame.texturePath);
            else if (previewState.empty())
                previewState = "Frame has no texture";
            useFrameRect = frame.hasSourceRect;
            if (texture == nullptr && previewState.empty())
            {
                previewDetail = thumbnails_.failure_message(frame.texturePath);
                previewState = previewDetail.empty() ? "Preparing preview" : "Preview unavailable";
            }
            else if (texture != nullptr && thumbnails_.stale(frame.texturePath) && previewState.empty())
                previewState = "Updating preview";
        }
    }
    else if (!isDirectory && asset_icon(path) == EditorIcon::Sprite)
    {
        texture = thumbnail_for(path);
        if (texture == nullptr)
        {
            previewDetail = thumbnails_.failure_message(path);
            previewState = previewDetail.empty() ? "Preparing preview" : "Preview unavailable";
        }
        else if (thumbnails_.stale(path))
            previewState = "Updating preview";
    }

    const float previewInset = 8.0f * uiScale;
    const ImVec2 previewMinimum{minimum.x + previewInset, minimum.y + previewInset};
    const ImVec2 previewMaximum{maximum.x - previewInset, minimum.y + tileHeight - 32.0f * uiScale};
    if (isDirectory)
        draw_editor_icon(EditorIcon::Folder, drawList,
                         {minimum.x + (tileWidth - 40.0f * uiScale) * 0.5f,
                          minimum.y + 18.0f * uiScale}, 40.0f * uiScale,
                         ImGui::GetColorU32(ImGuiCol_Text));
    else if (texture != nullptr)
    {
        float textureWidth = 0.0f;
        float textureHeight = 0.0f;
        if (SDL_GetTextureSize(texture, &textureWidth, &textureHeight) &&
            textureWidth > 0.0f && textureHeight > 0.0f)
        {
            float sourceX = 0.0f;
            float sourceY = 0.0f;
            float sourceWidth = textureWidth;
            float sourceHeight = textureHeight;
            if (useFrameRect)
            {
                const auto [sourceScaleX, sourceScaleY] =
                    thumbnails_.source_to_thumbnail_scale(
                        frame.texturePath, frameRenderer_ != nullptr ? frameRenderer_ : renderer_);
                sourceX = std::clamp(frame.sourceX * sourceScaleX, 0.0f, textureWidth);
                sourceY = std::clamp(frame.sourceY * sourceScaleY, 0.0f, textureHeight);
                sourceWidth = std::clamp(frame.sourceWidth * sourceScaleX, 0.0f,
                                         textureWidth - sourceX);
                sourceHeight = std::clamp(frame.sourceHeight * sourceScaleY, 0.0f,
                                          textureHeight - sourceY);
                if (sourceWidth <= 0.0f || sourceHeight <= 0.0f)
                {
                    sourceX = 0.0f;
                    sourceY = 0.0f;
                    sourceWidth = textureWidth;
                    sourceHeight = textureHeight;
                }
            }
            const float scale = std::min((previewMaximum.x - previewMinimum.x) / sourceWidth,
                                         (previewMaximum.y - previewMinimum.y) / sourceHeight);
            const float displayWidth = sourceWidth * scale;
            const float displayHeight = sourceHeight * scale;
            const ImVec2 displayMinimum{minimum.x + (tileWidth - displayWidth) * 0.5f,
                                        previewMinimum.y + (previewMaximum.y - previewMinimum.y - displayHeight) * 0.5f};
            const ImVec2 displayMaximum{displayMinimum.x + displayWidth, displayMinimum.y + displayHeight};
            const auto textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
            drawList->AddImage(textureId, displayMinimum, displayMaximum,
                               {sourceX / textureWidth, sourceY / textureHeight},
                               {(sourceX + sourceWidth) / textureWidth,
                                (sourceY + sourceHeight) / textureHeight});
        }
    }
    else
    {
        const EditorIcon icon = isDirectory ? EditorIcon::Folder : asset_icon(path);
        const float iconSize = std::min(40.0f * uiScale, tileWidth * 0.42f);
        draw_editor_icon(icon, drawList,
                         {minimum.x + (tileWidth - iconSize) * 0.5f,
                          minimum.y + 18.0f * uiScale}, iconSize,
                         ImGui::GetColorU32(ImGuiCol_Text));
    }

    if (!previewState.empty())
    {
        const ImU32 stateColor = previewState == "Preview unavailable"
            ? ImGui::GetColorU32(ImGuiCol_Text)
            : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        drawList->AddText({minimum.x + 5.0f * uiScale, minimum.y + 5.0f * uiScale}, stateColor,
                          previewState.c_str());
    }

    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const float textX = minimum.x + 5.0f * uiScale;
    const float textY = maximum.y - textSize.y - 6.0f * uiScale;
    drawList->PushClipRect({minimum.x + 3.0f * uiScale, textY},
                           {maximum.x - 3.0f * uiScale, maximum.y - 3.0f * uiScale}, true);
    drawList->AddText({textX, textY}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    drawList->PopClipRect();

    if (hovered)
    {
        if (!previewDetail.empty())
            ImGui::SetTooltip("%s\n%s", path.string().c_str(), previewDetail.c_str());
        else if (!previewState.empty())
            ImGui::SetTooltip("%s\n%s", path.string().c_str(), previewState.c_str());
        else
            ImGui::SetTooltip("%s", path.string().c_str());
    }
    const bool doubleClicked = hovered &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (clicked || doubleClicked)
    {
        if (!selectedEntry_ || *selectedEntry_ != path)
        {
            assignmentFeedback_.clear();
            selectedAssignmentTarget_.clear();
        }
        selectedEntry_ = path;
    }
    if (doubleClicked)
    {
        if (isDirectory)
        {
            selectedDirectory_ = path;
            selectedEntry_.reset();
            auto parent = path;
            while (path_is_within(assetRoot, parent))
            {
                expandedDirectories_.insert(parent.string());
                if (parent == assetRoot || parent == parent.parent_path())
                    break;
                parent = parent.parent_path();
            }
        }
        else
        {
            if (editor_asset_format(path).kind == EditorAssetKind::Animation &&
                openAnimationCallback_)
            {
                assetDoubleClickPending_ = false;
                openAnimationCallback_(path);
            }
            else
                assetDoubleClickPending_ = true;
        }
    }

    if (pendingSemanticNodes_.size() < 128 && path_is_within(assetRoot, path))
    {
        const auto relative = path.lexically_relative(assetRoot);
        const std::string nodeId = "main/panel/project/asset/" + semantic_path_token(relative);
        SemanticNode node;
        node.id = nodeId;
        node.parentId = "main/panel/project/contents";
        node.role = SemanticRole::ListItem;
        node.name = bounded_semantic_text(label);
        node.value = isDirectory ? "Folder" : asset_search_type(path, false);
        node.help = bounded_semantic_text(path.generic_string());
        node.visible = true;
        node.selected = selectedEntry_ && *selectedEntry_ == path;
        node.bounds = SemanticBounds{minimum.x, minimum.y,
                                     maximum.x - minimum.x, maximum.y - minimum.y};
        node.actions = {"activate"};
        pendingSemanticNodes_.push_back(std::move(node));

        SemanticProjectTarget target;
        target.path = path;
        target.root = assetRoot;
        target.rootLabel = selectedRootLabel_;
        target.projectGeneration = ProjectGeneration{context_.snapshot().generation};
        target.directory = isDirectory;
        target.behaviorRoot = behaviorRoot;
        pendingTileTargets_.emplace(nodeId, std::move(target));
    }
    if (ImGui::BeginPopupContextItem("##asset-item-context"))
    {
        viewState_.contextTargetPath = path;
        selectedEntry_ = path;
        if (ImGui::MenuItem("Open"))
        {
            if (isDirectory)
            {
                selectedDirectory_ = path;
                selectedEntry_.reset();
            }
            else if (openAnimationCallback_ && editor_asset_format(path).kind == EditorAssetKind::Animation)
                openAnimationCallback_(path);
            else
                assetDoubleClickPending_ = true;
        }
        if (ImGui::MenuItem("Reveal in File Manager"))
        {
            reveal_in_file_manager(path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2"))
        {
            viewState_.renaming = true;
            viewState_.renamingPath = path;
            std::snprintf(viewState_.renameBuffer, sizeof(viewState_.renameBuffer), "%s",
                          path.filename().string().c_str());
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            execute_context_duplicate(path);
        }
        if (ImGui::MenuItem("Delete", "Delete"))
        {
            execute_context_delete(path);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Create"))
        {
            const auto targetDir = isDirectory ? path : path.parent_path();
            if (ImGui::MenuItem("Folder"))
                execute_context_create("Folder", targetDir);
            if (ImGui::MenuItem("Behavior Script"))
                execute_context_create("Behavior Script", targetDir);
            if (ImGui::MenuItem("Scene"))
                execute_context_create("Scene", targetDir);
            if (ImGui::MenuItem("Object Template"))
                execute_context_create("Object Template", targetDir);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void ProjectPanel::draw_asset_list_row(const std::filesystem::path &path,
                                       const std::filesystem::path &assetRoot,
                                       bool isDirectory, bool behaviorRoot,
                                       float rowHeight)
{
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    const std::string entryKey = path.string();
    const std::string label = path.filename().string();
    const bool selected = selectedEntry_ && *selectedEntry_ == path;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(entryKey.c_str());

    const bool clicked = ImGui::Selectable("##asset-row", selected, ImGuiSelectableFlags_None,
                                          ImVec2(availableWidth, rowHeight));
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();

    const bool behaviorSource = behaviorRoot && editor_asset_format(path).behaviorHeader;
    if (!isDirectory && (!behaviorRoot || behaviorSource) && ImGui::BeginDragDropSource())
    {
        if (behaviorSource)
        {
            const auto behaviorType = behavior_type_from_source(path);
            if (behaviorType)
            {
                ImGui::SetDragDropPayload("PLATFORMATOR_BEHAVIOR_TYPE", behaviorType->c_str(),
                                          behaviorType->size() + 1);
                ImGui::Text("Add %s", behaviorType->c_str());
            }
            else
                ImGui::TextUnformatted("Could not find a registered behavior class in this source file.");
        }
        else
        {
            const auto relativeAsset = path.lexically_relative(assetRoot).generic_string();
            const std::string reference = "assets/" + relativeAsset;
            AssetDragPayload payload{};
            std::snprintf(payload.relativeAssetPath, sizeof(payload.relativeAssetPath), "%s", reference.c_str());
            payload.kind = editor_asset_format(path).kind;
            ImGui::SetDragDropPayload("PLATFORMATOR_ASSET_PATH", &payload, sizeof(payload));
            ImGui::TextUnformatted(path.filename().string().c_str());
        }
        ImGui::EndDragDropSource();
    }

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const float iconSize = 14.0f * uiScale;
    const float iconY = minimum.y + (rowHeight - iconSize) * 0.5f;
    const auto icon = isDirectory ? EditorIcon::Folder : asset_icon(path);
    draw_editor_icon(icon, drawList, {minimum.x + 4.0f * uiScale, iconY}, iconSize,
                     ImGui::GetColorU32(ImGuiCol_Text));

    const float textY = minimum.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
    drawList->AddText({minimum.x + 22.0f * uiScale, textY},
                      ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

    const std::string typeText = isDirectory ? "Folder" : asset_search_type(path, false);
    const float typeWidth = ImGui::CalcTextSize(typeText.c_str()).x;
    drawList->AddText({maximum.x - typeWidth - 8.0f * uiScale, textY},
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), typeText.c_str());

    if (clicked)
    {
        if (!selectedEntry_ || *selectedEntry_ != path)
        {
            assignmentFeedback_.clear();
            selectedAssignmentTarget_.clear();
        }
        selectedEntry_ = path;
    }
    const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (doubleClicked)
    {
        if (isDirectory)
        {
            selectedDirectory_ = path;
            selectedEntry_.reset();
            auto parent = path;
            while (path_is_within(assetRoot, parent))
            {
                expandedDirectories_.insert(parent.string());
                if (parent == assetRoot || parent == parent.parent_path())
                    break;
                parent = parent.parent_path();
            }
        }
        else
        {
            if (editor_asset_format(path).kind == EditorAssetKind::Animation && openAnimationCallback_)
            {
                assetDoubleClickPending_ = false;
                openAnimationCallback_(path);
            }
            else
                assetDoubleClickPending_ = true;
        }
    }

    if (ImGui::BeginPopupContextItem("##asset-item-context"))
    {
        viewState_.contextTargetPath = path;
        selectedEntry_ = path;
        if (ImGui::MenuItem("Open"))
        {
            if (isDirectory)
            {
                selectedDirectory_ = path;
                selectedEntry_.reset();
            }
            else if (openAnimationCallback_ && editor_asset_format(path).kind == EditorAssetKind::Animation)
                openAnimationCallback_(path);
            else
                assetDoubleClickPending_ = true;
        }
        if (ImGui::MenuItem("Reveal in File Manager"))
        {
            reveal_in_file_manager(path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2"))
        {
            viewState_.renaming = true;
            viewState_.renamingPath = path;
            std::snprintf(viewState_.renameBuffer, sizeof(viewState_.renameBuffer), "%s",
                          path.filename().string().c_str());
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            execute_context_duplicate(path);
        }
        if (ImGui::MenuItem("Delete", "Delete"))
        {
            execute_context_delete(path);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Create"))
        {
            const auto targetDir = isDirectory ? path : path.parent_path();
            if (ImGui::MenuItem("Folder"))
                execute_context_create("Folder", targetDir);
            if (ImGui::MenuItem("Behavior Script"))
                execute_context_create("Behavior Script", targetDir);
            if (ImGui::MenuItem("Scene"))
                execute_context_create("Scene", targetDir);
            if (ImGui::MenuItem("Object Template"))
                execute_context_create("Object Template", targetDir);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

bool ProjectPanel::execute_context_create(std::string_view itemType, const std::filesystem::path &targetDir)
{
    std::error_code ec;
    if (itemType == "Folder")
    {
        std::filesystem::path candidate = targetDir / "New Folder";
        int counter = 1;
        while (std::filesystem::exists(candidate, ec))
        {
            candidate = targetDir / ("New Folder (" + std::to_string(counter++) + ")");
        }
        std::filesystem::create_directories(candidate, ec);
        refresh();
        return !ec;
    }
    if (itemType == "Behavior Script")
    {
        std::filesystem::path candidate = targetDir / "NewBehavior.cpp";
        int counter = 1;
        while (std::filesystem::exists(candidate, ec))
        {
            candidate = targetDir / ("NewBehavior" + std::to_string(counter++) + ".cpp");
        }
        const std::string stem = candidate.stem().string();
        std::string content = "#include <platformator/behavior.h>\n\nclass " + stem + " : public platformator::Behavior\n{\npublic:\n    void update(float deltaTime) override {}\n};\n";
        std::ofstream out(candidate, std::ios::binary);
        if (out.is_open())
        {
            out << content;
            out.close();
            refresh();
            return true;
        }
        return false;
    }
    if (itemType == "Scene")
    {
        std::filesystem::path candidate = targetDir / "New Scene.scene";
        int counter = 1;
        while (std::filesystem::exists(candidate, ec))
        {
            candidate = targetDir / ("New Scene (" + std::to_string(counter++) + ").scene");
        }
        std::ofstream out(candidate, std::ios::binary);
        if (out.is_open())
        {
            out << "{\n    \"schema\": 1,\n    \"root\": []\n}\n";
            out.close();
            refresh();
            return true;
        }
        return false;
    }
    if (itemType == "Object Template")
    {
        std::filesystem::path candidate = targetDir / "New Template.pttemplate";
        int counter = 1;
        while (std::filesystem::exists(candidate, ec))
        {
            candidate = targetDir / ("New Template (" + std::to_string(counter++) + ").pttemplate");
        }
        std::ofstream out(candidate, std::ios::binary);
        if (out.is_open())
        {
            out << "{\n    \"schema\": \"platformator/template/v1\",\n    \"root\": {\n        \"id\": 1,\n        \"name\": \"New Template\",\n        \"components\": []\n    },\n    \"reference_bindings\": []\n}\n";
            out.close();
            refresh();
            return true;
        }
        return false;
    }
    return false;
}

bool ProjectPanel::execute_context_delete(const std::filesystem::path &targetPath)
{
    std::error_code ec;
    const bool removed = std::filesystem::remove_all(targetPath, ec) > 0;
    if (selectedEntry_ && *selectedEntry_ == targetPath)
        selectedEntry_.reset();
    refresh();
    return removed && !ec;
}

bool ProjectPanel::execute_context_duplicate(const std::filesystem::path &targetPath)
{
    std::error_code ec;
    if (!std::filesystem::exists(targetPath, ec))
        return false;
    const auto parent = targetPath.parent_path();
    const auto stem = targetPath.stem().string();
    const auto ext = targetPath.extension().string();
    std::filesystem::path candidate = parent / (stem + " Copy" + ext);
    int counter = 1;
    while (std::filesystem::exists(candidate, ec))
    {
        candidate = parent / (stem + " Copy (" + std::to_string(counter++) + ")" + ext);
    }
    std::filesystem::copy(targetPath, candidate, std::filesystem::copy_options::recursive, ec);
    refresh();
    return !ec;
}

bool ProjectPanel::execute_context_rename(const std::filesystem::path &targetPath, const std::string &newName)
{
    if (newName.empty() || newName == targetPath.filename().string())
        return false;
    std::error_code ec;
    const auto dest = targetPath.parent_path() / newName;
    std::filesystem::rename(targetPath, dest, ec);
    if (!ec && selectedEntry_ && *selectedEntry_ == targetPath)
        selectedEntry_ = dest;
    refresh();
    return !ec;
}

bool ProjectPanel::reveal_in_file_manager(const std::filesystem::path &targetPath)
{
    std::error_code ec;
    std::filesystem::path dir = targetPath;
    if (dir.empty())
        dir = selectedDirectory_.empty() ? assetRoot_ : selectedDirectory_;
    if (!std::filesystem::is_directory(dir, ec))
        dir = dir.parent_path();
    if (dir.empty())
        dir = ".";
    std::filesystem::path absoluteDir = std::filesystem::absolute(dir, ec);
    std::string url = "file://" + absoluteDir.generic_string();
    lastRevealedUrl_ = url;
    return SDL_OpenURL(url.c_str());
}

void ProjectPanel::draw_contents(const std::filesystem::path &root, const std::string &rootLabel,
                                 bool behaviorRoot)
{
    request_scan(selectedDirectory_);
    auto &state = directories_[selectedDirectory_.string()];
    const auto relative = selectedDirectory_.lexically_relative(root);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::SmallButton(rootLabel.c_str()))
    {
        selectedDirectory_ = root;
        selectedEntry_.reset();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", root.string().c_str());

    if (!relative.empty() && relative != ".")
    {
        std::filesystem::path currentPath = root;
        for (const auto &part : relative)
        {
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0.0f, 2.0f);
            currentPath /= part;
            if (ImGui::SmallButton(part.string().c_str()))
            {
                selectedDirectory_ = currentPath;
                selectedEntry_.reset();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", currentPath.string().c_str());
        }
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (!state.error.empty())
        ImGui::TextWrapped("%s", state.error.c_str());
    if (!state.loaded && !state.hasCompleteSnapshot)
    {
        ImGui::TextDisabled("Scanning...");
        return;
    }
    if (!state.loaded && state.hasCompleteSnapshot)
        ImGui::TextDisabled("Showing the last complete listing while refresh is pending.");

    std::array<char, 256> searchBuffer{};
    const std::size_t searchLength = std::min(searchQuery_.size(), searchBuffer.size() - 1);
    std::memcpy(searchBuffer.data(), searchQuery_.data(), searchLength);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##project-search", "Search name, type, or path",
                                 searchBuffer.data(), searchBuffer.size()))
        searchQuery_ = searchBuffer.data();
    ImGui::Separator();

    if (!state.entriesValid || state.entriesBehaviorFilter != behaviorRoot)
    {
        state.entries.clear();
        state.entries.reserve(state.directories.size() + state.files.size());
        for (const auto &directory : state.directories)
            state.entries.push_back({directory, true});
        for (const auto &file : state.files)
            if (!behaviorRoot || editor_asset_format(file).behaviorHeader)
                state.entries.push_back({file, false});
        state.entriesBehaviorFilter = behaviorRoot;
        state.entriesValid = true;
        if (++state.entriesRevision == 0)
            ++state.entriesRevision;
    }
    const auto &entries = state.entries;
    if (state.filteredQuery != searchQuery_ ||
        state.filteredEntriesRevision != state.entriesRevision)
    {
        state.filteredQuery = searchQuery_;
        state.filteredEntriesRevision = state.entriesRevision;
        state.filteredEntryIndexes.clear();
        const std::string query = lowercase_text(searchQuery_);
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const auto &entry = entries[index];
            const auto relativePath = entry.path.lexically_relative(assetRoot_).generic_string();
            const std::string searchText = relativePath + " " + entry.path.filename().string() + " " +
                                           lowercase_asset_extension(entry.path) + " " +
                                           asset_search_type(entry.path, entry.isDirectory);
            if (discovery_query_matches(query, searchText))
                state.filteredEntryIndexes.push_back(index);
        }
    }
    const auto &filteredIndexes = state.filteredEntryIndexes;
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 6.0f;
    ImGui::BeginChild("##contents-scroll", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None);

    if (entries.empty())
    {
        ImGui::TextDisabled("This folder is empty.");
    }
    else if (filteredIndexes.empty())
    {
        ImGui::TextDisabled("No assets match this search.");
    }
    else if (viewState_.thumbnailScale <= 0.0f)
    {
        const float rowHeight = ImGui::GetFrameHeight();
        for (const std::size_t index : filteredIndexes)
        {
            const auto &entry = entries[index];
            draw_asset_list_row(entry.path, root, entry.isDirectory, behaviorRoot, rowHeight);
            work::record(work::Counter::ProjectTiles);
        }
    }
    else
    {
        const float preferredTileWidth = (64.0f + viewState_.thumbnailScale * 128.0f) * uiScale;
        const float tileHeight = (56.0f + viewState_.thumbnailScale * 112.0f) * uiScale;
        const float spacingX = ImGui::GetStyle().ItemSpacing.x;
        const float spacingY = ImGui::GetStyle().ItemSpacing.y;
        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const int columns = std::max(1, static_cast<int>((availableWidth + spacingX) /
                                                         (preferredTileWidth + spacingX)));
        const float tileWidth = std::max(56.0f * uiScale,
            (availableWidth - spacingX * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        const int rowCount = static_cast<int>((filteredIndexes.size() + static_cast<std::size_t>(columns) - 1) /
                                              static_cast<std::size_t>(columns));
        const float startX = ImGui::GetCursorPosX();
        const float startY = ImGui::GetCursorPosY();
        const float rowStride = tileHeight + spacingY;
        const float scrollY = std::max(0.0f, ImGui::GetScrollY() - startY);
        const float visibleHeight = ImGui::GetWindowSize().y;
        const int firstVisibleRow = std::max(0, static_cast<int>(std::floor(scrollY / rowStride)) - 1);
        const int lastVisibleRow = std::min(rowCount, static_cast<int>(std::ceil(
            (scrollY + visibleHeight) / rowStride)) + 1);
        for (int row = firstVisibleRow; row < lastVisibleRow; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const std::size_t entryIndex = static_cast<std::size_t>(row) *
                                               static_cast<std::size_t>(columns) +
                                               static_cast<std::size_t>(column);
                if (entryIndex >= filteredIndexes.size())
                    break;
                ImGui::SetCursorPos({startX + static_cast<float>(column) * (tileWidth + spacingX),
                                     startY + static_cast<float>(row) * rowStride});
                const auto &entry = entries[filteredIndexes[entryIndex]];
                draw_asset_tile(entry.path, root, entry.isDirectory, behaviorRoot,
                                tileWidth, tileHeight);
                work::record(work::Counter::ProjectTiles);
            }
        }
        ImGui::SetCursorPos({startX, startY + static_cast<float>(rowCount) * rowStride});
        ImGui::Dummy({1.0f, 1.0f});
    }

    if (state.truncated)
        ImGui::TextDisabled("Folder listing reached its scan limit.");

    if (ImGui::BeginPopupContextWindow("##folder-empty-context-menu",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        viewState_.contextTargetPath = selectedDirectory_;
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Folder"))
                execute_context_create("Folder", selectedDirectory_);
            if (ImGui::MenuItem("Behavior Script"))
                execute_context_create("Behavior Script", selectedDirectory_);
            if (ImGui::MenuItem("Scene"))
                execute_context_create("Scene", selectedDirectory_);
            if (ImGui::MenuItem("Object Template"))
                execute_context_create("Object Template", selectedDirectory_);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reveal in File Manager"))
        {
            reveal_in_file_manager(selectedDirectory_);
        }
        ImGui::EndPopup();
    }

    if (selectedEntry_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F2))
        {
            viewState_.renaming = true;
            viewState_.renamingPath = *selectedEntry_;
            std::snprintf(viewState_.renameBuffer, sizeof(viewState_.renameBuffer), "%s",
                          selectedEntry_->filename().string().c_str());
        }
        else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
        {
            execute_context_duplicate(*selectedEntry_);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            execute_context_delete(*selectedEntry_);
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextDisabled("%zu items", filteredIndexes.size());
    ImGui::SameLine();
    const float sliderWidth = 140.0f * uiScale;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > sliderWidth)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - sliderWidth);
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderFloat("##thumbnail-scale", &viewState_.thumbnailScale, 0.0f, 1.0f,
                       viewState_.thumbnailScale <= 0.0f ? "List" : "Grid");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Thumbnail scale (0 = compact list, >0 = scalable grid)");

    if (viewState_.renaming)
    {
        ImGui::OpenPopup("Rename Asset##modal");
    }
    if (ImGui::BeginPopupModal("Rename Asset##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Rename: %s", viewState_.renamingPath.filename().string().c_str());
        ImGui::SetNextItemWidth(250.0f * uiScale);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##rename-input", viewState_.renameBuffer,
                                            sizeof(viewState_.renameBuffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("OK") || enter)
        {
            execute_context_rename(viewState_.renamingPath, viewState_.renameBuffer);
            viewState_.renaming = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            viewState_.renaming = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

std::vector<ProjectPanel::AssetAssignmentTarget> ProjectPanel::asset_assignment_targets(
    const DocumentSession &document, const SelectionModel &selection,
    const std::filesystem::path &assetPath) const
{
    std::vector<AssetAssignmentTarget> result;
    std::vector<int> objectIds = selection.current().selectedObjectIds;
    if (objectIds.empty() && selection.current().objectId)
        objectIds.push_back(*selection.current().objectId);
    if (objectIds.empty())
        return result;
    const auto relativePath = assetPath.lexically_relative(document.assetRoot);
    if (relativePath.empty() || relativePath.is_absolute() ||
        std::any_of(relativePath.begin(), relativePath.end(),
                    [](const std::filesystem::path &part) { return part == ".."; }))
        return result;
    const std::string reference = "assets/" + relativePath.generic_string();

    for (const int objectId : objectIds)
    {
        const auto objectLocation = document.indexes.objectsById.find(objectId);
        const Json *object = objectLocation == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, objectLocation->second);
        if (object == nullptr)
            continue;
        const auto components = object->find("components");
        if (components == object->end() || !components->is_array())
            continue;
        const std::string objectName = object->value("name", std::string("GameObject"));
        for (const auto &component : *components)
        {
            if (!component.is_object())
                continue;
            const int componentId = component.value("id", -1);
            const int type = component.value("type", -1);
            if (componentId < 0)
                continue;
            if (selection.current().componentId && selection.current().objectId == objectId &&
                *selection.current().componentId != componentId)
                continue;

            std::string fieldName;
            std::string fieldLabel;
            std::string expectedAssetType;
            switch (type)
            {
            case ANIMATOR:
                fieldName = "animationClipFilePath";
                fieldLabel = "Animation Clip";
                expectedAssetType = "AnimationClip";
                break;
            case AUDIO:
                fieldName = "filePath";
                fieldLabel = "Audio Clip";
                expectedAssetType = "Audio";
                break;
            case SPRITE:
                fieldName = "textureFilePath";
                fieldLabel = "Texture";
                expectedAssetType = "Texture";
                break;
            default:
                continue;
            }
            std::string error;
            if (!validate_asset_reference_assignment(document, reference, expectedAssetType,
                                                     false, error))
                continue;
            const std::string targetLabel = objectName + " (#" + std::to_string(objectId) + ") / " +
                component_label(type) + " (#" + std::to_string(componentId) + ") / " + fieldLabel;
            result.push_back({address_for_document(document, objectId, componentId, std::nullopt,
                                                    {fieldName}),
                              std::move(expectedAssetType), targetLabel});
        }
    }
    return result;
}

bool ProjectPanel::assign_asset_target(DocumentSession &document, EditCommandService &commands,
                                       const AssetAssignmentTarget &target,
                                       const std::filesystem::path &assetPath,
                                       std::string &error)
{
    const auto relativePath = assetPath.lexically_relative(document.assetRoot);
    if (relativePath.empty() || relativePath.is_absolute() ||
        std::any_of(relativePath.begin(), relativePath.end(),
                    [](const std::filesystem::path &part) { return part == ".."; }))
    {
        error = "Selected file is outside the project asset root.";
        return false;
    }
    const std::string reference = "assets/" + relativePath.generic_string();
    if (!validate_asset_reference_assignment(document, reference,
                                             target.expectedAssetType, false, error))
        return false;
    const auto result = commands.set_property(document, target.address, reference);
    if (result.status == CommandStatus::Changed)
        return true;
    error = result.message.empty() ? "Asset reference was unchanged." : result.message;
    return false;
}

void ProjectPanel::draw_template_controls(DocumentSession &document, EditCommandService &commands,
                                          SelectionModel *selection)
{
    templateCanInstantiate_ = false;
    if (!selectedEntry_)
        return;
    ImGui::SeparatorText("Object Template");
    ImGui::TextWrapped("%s", selectedEntry_->filename().string().c_str());
    if (ImGui::Button("Load Template"))
        load_selected_template(document);
    if (selectedTemplate_ && selectedTemplatePath_ != *selectedEntry_)
    {
        selectedTemplate_.reset();
        selectedTemplatePath_.clear();
        externalTemplateTargets_.clear();
        pendingTemplateInstantiate_ = false;
    }
    const DocumentIdentity currentIdentity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                           DocumentGeneration{document.documentGeneration}};
    if (selectedTemplate_ && templateAssignmentIdentity_ != currentIdentity)
    {
        externalTemplateTargets_.clear();
        templateAssignmentIdentity_ = currentIdentity;
    }

    if (selectedTemplate_)
    {
        if (templateTargetsStructureRevision_ != document.structureRevision ||
            templateTargetsValueRevision_ != document.valueRevision)
        {
            templateTargetCandidates_.clear();
            for (const int objectId : document.indexes.orderedObjectIds)
            {
                const auto objectLocation = document.indexes.objectsById.find(objectId);
                const Json *object = objectLocation == document.indexes.objectsById.end()
                    ? nullptr : resolve_object(document.root, objectLocation->second);
                if (object == nullptr)
                    continue;
                const std::string objectLabel = object->value("name", std::string("GameObject")) +
                    " (#" + std::to_string(objectId) + ")";
                templateTargetCandidates_["GameObject"].emplace_back(objectId, objectLabel);
                templateTargetCandidates_["Object"].emplace_back(objectId, objectLabel);
                const auto components = object->find("components");
                if (components == object->end() || !components->is_array())
                    continue;
                for (const auto &component : *components)
                {
                    if (!component.is_object())
                        continue;
                    const int componentId = component.value("id", -1);
                    if (componentId < 0)
                        continue;
                    const int type = component.value("type", -1);
                    const std::string label = objectLabel + " / " + component_label(type) +
                        " (#" + std::to_string(componentId) + ")";
                    templateTargetCandidates_["Component"].emplace_back(componentId, label);
                }
            }
            templateTargetsStructureRevision_ = document.structureRevision;
            templateTargetsValueRevision_ = document.valueRevision;
        }

        const Json bindings = selectedTemplate_->reference_bindings();
        const std::string destinationIdentity = template_scene_identity(document);
        std::vector<TemplateExternalAssignment> assignments;
        bool allExternalTargetsReady = true;
        if (bindings.is_array())
        {
            for (std::size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
            {
                const auto &binding = bindings[bindingIndex];
                if (!binding.is_object() || binding.value("scope", std::string{}) != "external")
                    continue;
                const std::string kind = binding.value("target_kind", std::string{});
                const int originalId = binding.value("original_target_id", -1);
                const std::string key = std::to_string(binding.value("owner_id", -1)) + ":" +
                    std::to_string(binding.value("component_id", -1)) + ":" +
                    binding.value("path", std::string{});
                const auto candidates = templateTargetCandidates_.find(kind);
                const bool originalStillValid = candidates != templateTargetCandidates_.end() &&
                    std::any_of(candidates->second.begin(), candidates->second.end(),
                        [originalId](const auto &candidate) { return candidate.first == originalId; });
                const bool sameSceneAndValid =
                    binding.value("source_scene_identity", std::string{}) == destinationIdentity &&
                    originalStillValid;
                if (sameSceneAndValid)
                {
                    ImGui::TextDisabled("External reference %zu keeps its original target in this scene.",
                                        bindingIndex + 1);
                    continue;
                }

                ImGui::PushID(static_cast<int>(bindingIndex));
                ImGui::Text("External %s reference", kind.c_str());
                std::string preview = "Choose a destination target...";
                const auto selectedTarget = externalTemplateTargets_.find(key);
                if (selectedTarget != externalTemplateTargets_.end() && candidates != templateTargetCandidates_.end())
                {
                    const auto chosen = std::find_if(candidates->second.begin(), candidates->second.end(),
                        [&](const auto &candidate) { return candidate.first == selectedTarget->second; });
                    if (chosen != candidates->second.end())
                        preview = chosen->second;
                }
                if (ImGui::BeginCombo("##external-template-target", preview.c_str()))
                {
                    if (candidates != templateTargetCandidates_.end())
                        for (const auto &[targetId, label] : candidates->second)
                            if (ImGui::Selectable(label.c_str(),
                                selectedTarget != externalTemplateTargets_.end() &&
                                selectedTarget->second == targetId))
                                externalTemplateTargets_[key] = targetId;
                    ImGui::EndCombo();
                }
                const auto assigned = externalTemplateTargets_.find(key);
                const bool assignedValid = assigned != externalTemplateTargets_.end() &&
                    candidates != templateTargetCandidates_.end() &&
                    std::any_of(candidates->second.begin(), candidates->second.end(),
                        [&](const auto &candidate) { return candidate.first == assigned->second; });
                if (!assignedValid)
                    allExternalTargetsReady = false;
                else
                    assignments.push_back({binding.value("owner_id", -1),
                        binding.contains("component_id")
                            ? std::optional<int>(binding.value("component_id", -1)) : std::nullopt,
                        binding.value("path", std::string{}), assigned->second});
                ImGui::PopID();
            }
        }

        if (selection == nullptr)
            ImGui::TextDisabled("Select a Hierarchy destination before instantiating.");
        const bool canInstantiate = selection != nullptr && allExternalTargetsReady;
        templateCanInstantiate_ = canInstantiate;
        ImGui::BeginDisabled(!canInstantiate);
        const bool instantiateClicked = ImGui::Button("Instantiate Template");
        const bool semanticInstantiate = std::exchange(pendingTemplateInstantiate_, false);
        if (instantiateClicked || (semanticInstantiate && canInstantiate))
        {
            const Selection before = selection->current();
            std::optional<int> parentId;
            std::size_t insertionIndex = std::numeric_limits<std::size_t>::max();
            if (before.objectId)
            {
                const auto parent = document.indexes.parentById.find(*before.objectId);
                if (parent != document.indexes.parentById.end())
                    parentId = parent->second;
                const auto location = document.indexes.objectsById.find(*before.objectId);
                if (location != document.indexes.objectsById.end() &&
                    !location->second.childIndices.empty())
                    insertionIndex = location->second.childIndices.back() + 1;
            }
            const auto coverage = template_asset_binding_coverage(*selectedTemplate_);
            const auto inserted = insert_template(commands, document, *selectedTemplate_,
                destinationIdentity, coverage, parentId, insertionIndex, assignments,
                SelectionState{before, before});
            if (!inserted)
                templateFeedback_ = inserted.error.empty()
                    ? "Template could not be instantiated." : inserted.error;
            else if (!inserted.insertedObjectIds.empty())
            {
                const Selection after{inserted.insertedObjectIds.front(), std::nullopt,
                                      std::nullopt, FocusedPanel::Hierarchy};
                selection->set(document, after);
                commands.set_last_selection_state({before, after});
                templateFeedback_ = "Template instantiated as one undoable operation.";
            }
        }
        ImGui::EndDisabled();
    }
    if (!templateFeedback_.empty())
        ImGui::TextWrapped("%s", templateFeedback_.c_str());
}

void ProjectPanel::load_selected_template(const DocumentSession &document)
{
    selectedTemplate_.reset();
    selectedTemplatePath_.clear();
    externalTemplateTargets_.clear();
    templateCanInstantiate_ = false;
    pendingTemplateInstantiate_ = false;
    templateFeedback_.clear();
    if (!selectedEntry_)
    {
        templateFeedback_ = "Select a template asset first.";
        return;
    }
    std::error_code pathError;
    const auto canonicalRoot = std::filesystem::canonical(document.assetRoot, pathError);
    if (pathError)
        templateFeedback_ = "Project asset root is unavailable.";
    const auto canonicalFile = pathError ? std::filesystem::path{}
                                         : std::filesystem::canonical(*selectedEntry_, pathError);
    if (templateFeedback_.empty() && pathError)
        templateFeedback_ = "Selected template file is unavailable: " + pathError.message();
    if (templateFeedback_.empty() &&
        (!path_is_within(canonicalRoot, canonicalFile) ||
         !std::filesystem::is_regular_file(canonicalFile, pathError) || pathError))
        templateFeedback_ = "Templates must be regular files contained inside the project asset root.";
    if (templateFeedback_.empty())
    {
        const auto size = std::filesystem::file_size(canonicalFile, pathError);
        if (pathError || size > 16U * 1024U * 1024U)
            templateFeedback_ = "Template could not be read or exceeds the 16 MiB editor limit.";
        else
        {
            std::ifstream input(canonicalFile, std::ios::binary);
            const std::string contents((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());
            if (!input.eof() && input.fail())
                templateFeedback_ = "Template could not be read completely.";
            else
            {
                auto loaded = parse_template_envelope(contents);
                if (!loaded)
                    templateFeedback_ = loaded.error;
                else
                {
                    selectedTemplate_ = std::move(*loaded.asset);
                    selectedTemplatePath_ = *selectedEntry_;
                    templateAssignmentIdentity_ = {
                        ProjectGeneration{document.projectGeneration}, document.tabId,
                        DocumentGeneration{document.documentGeneration}};
                    templateFeedback_ = "Template validated. Search and select destination references below when needed.";
                }
            }
        }
    }
}

void ProjectPanel::draw_sprite_region_editor(DocumentSession &document,
                                             EditCommandService &commands,
                                             const AssetAssignmentTarget &target,
                                             const std::filesystem::path &assetPath)
{
    if (target.expectedAssetType != "Texture" || !target.address.componentId ||
        asset_icon(assetPath) != EditorIcon::Sprite)
    {
        sourceRegionDragging_ = false;
        return;
    }
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    if (sourceRegionDragging_ &&
        (sourceRegionDragIdentity_ != identity || sourceRegionDragAsset_ != assetPath))
        sourceRegionDragging_ = false;

    std::error_code rootError;
    const auto canonicalRoot = std::filesystem::canonical(document.assetRoot, rootError);
    std::error_code assetError;
    const auto canonicalAsset = rootError ? std::filesystem::path{}
                                          : std::filesystem::canonical(assetPath, assetError);
    if (rootError || assetError || !path_is_within(canonicalRoot, canonicalAsset) ||
        !std::filesystem::is_regular_file(canonicalAsset, assetError) || assetError)
    {
        ImGui::TextDisabled("Source-region editing requires an image inside the current asset root.");
        sourceRegionDragging_ = false;
        return;
    }
    const std::string textureReference =
        "assets/" + canonicalAsset.lexically_relative(canonicalRoot).generic_string();
    SDL_Texture *texture = thumbnail_for(canonicalAsset);
    if (texture == nullptr)
    {
        const auto failure = thumbnails_.failure_message(canonicalAsset);
        ImGui::TextDisabled("Preparing the source image preview...");
        if (!failure.empty())
            ImGui::TextWrapped("%s", failure.c_str());
        return;
    }
    float thumbnailWidth = 0.0f;
    float thumbnailHeight = 0.0f;
    SDL_GetTextureSize(texture, &thumbnailWidth, &thumbnailHeight);
    const auto [sourceWidth, sourceHeight] = thumbnails_.source_dimensions(canonicalAsset);
    if (thumbnailWidth <= 0.0f || thumbnailHeight <= 0.0f || sourceWidth <= 0 || sourceHeight <= 0)
    {
        ImGui::TextDisabled("Original source image dimensions are unavailable.");
        sourceRegionDragging_ = false;
        return;
    }

    ImGui::SeparatorText("Sprite Source Region");
    ImGui::Text("Original image: %d × %d pixels", sourceWidth, sourceHeight);
    ImGui::TextDisabled("Drag over the image and release to commit one Undo step.");
    const float maximumWidth = std::min(std::max(1.0f, ImGui::GetContentRegionAvail().x), 360.0f);
    const float maximumHeight = ImGui::GetTextLineHeight() * 14.0f;
    const float scale = std::min(maximumWidth / thumbnailWidth, maximumHeight / thumbnailHeight);
    const ImVec2 displaySize{std::max(1.0f, thumbnailWidth * scale),
                             std::max(1.0f, thumbnailHeight * scale)};
    ImGui::InvisibleButton("##sprite-source-region-canvas", displaySize);
    const ImVec2 canvasMinimum = ImGui::GetItemRectMin();
    const ImVec2 canvasMaximum = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const auto textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
    drawList->AddImage(textureId, canvasMinimum, canvasMaximum);
    drawList->AddRect(canvasMinimum, canvasMaximum, ImGui::GetColorU32(ImGuiCol_Border));

    SpriteSourceRegion visibleRegion{0.0, 0.0, static_cast<double>(sourceWidth),
                                     static_cast<double>(sourceHeight)};
    const auto componentLocation = document.indexes.componentsById.find(*target.address.componentId);
    if (componentLocation != document.indexes.componentsById.end() &&
        componentLocation->second.ownerObjectId == target.address.objectId)
    {
        const auto objectLocation = document.indexes.objectsById.find(target.address.objectId);
        const Json *object = objectLocation == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, objectLocation->second);
        if (object != nullptr)
        {
            const auto components = object->find("components");
            if (components != object->end() && components->is_array() &&
                componentLocation->second.componentIndex < components->size())
            {
                const auto &component = (*components)[componentLocation->second.componentIndex];
                if (component.is_object())
                {
                    const auto textureField = component.find("textureFilePath");
                    const auto enabledField = component.find("sourceRectEnabled");
                    const auto rect = component.find("sourceRect");
                    const bool sameTexture = textureField != component.end() && textureField->is_string() &&
                        textureField->get<std::string>() == textureReference;
                    const bool enabled = enabledField != component.end() && enabledField->is_boolean() &&
                        enabledField->get<bool>();
                    if (sameTexture && enabled && rect != component.end() && rect->is_object())
                    {
                        const auto read_number = [&](const char *key, double &value) {
                            const auto found = rect->find(key);
                            if (found == rect->end() || !found->is_number())
                                return false;
                            value = found->get<double>();
                            return std::isfinite(value);
                        };
                        SpriteSourceRegion current;
                        std::string ignored;
                        if (read_number("x", current.x) && read_number("y", current.y) &&
                            read_number("w", current.width) && read_number("h", current.height) &&
                            validate_sprite_source_region(current, sourceWidth, sourceHeight, ignored))
                            visibleRegion = current;
                    }
                }
            }
        }
    }

    const auto point_to_source = [&](ImVec2 point) {
        const float localX = std::clamp(point.x - canvasMinimum.x, 0.0f, displaySize.x);
        const float localY = std::clamp(point.y - canvasMinimum.y, 0.0f, displaySize.y);
        return std::pair<double, double>{
            std::floor(static_cast<double>(localX / displaySize.x) * sourceWidth),
            std::floor(static_cast<double>(localY / displaySize.y) * sourceHeight)};
    };
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto [x, y] = point_to_source(ImGui::GetIO().MousePos);
        sourceRegionDragging_ = true;
        sourceRegionDragStartX_ = static_cast<float>(x);
        sourceRegionDragStartY_ = static_cast<float>(y);
        sourceRegionDragIdentity_ = identity;
        sourceRegionDragAsset_ = assetPath;
    }
    if (sourceRegionDragging_)
    {
        const auto [currentX, currentY] = point_to_source(ImGui::GetIO().MousePos);
        const SpriteSourceRegion dragged{std::min<double>(sourceRegionDragStartX_, currentX),
            std::min<double>(sourceRegionDragStartY_, currentY),
            std::abs(currentX - sourceRegionDragStartX_),
            std::abs(currentY - sourceRegionDragStartY_)};
        if (dragged.width > 0.0 && dragged.height > 0.0)
        {
            visibleRegion = dragged;
            const ImVec2 minimum{canvasMinimum.x + static_cast<float>(dragged.x) * displaySize.x / sourceWidth,
                                 canvasMinimum.y + static_cast<float>(dragged.y) * displaySize.y / sourceHeight};
            const ImVec2 maximum{minimum.x + static_cast<float>(dragged.width) * displaySize.x / sourceWidth,
                                 minimum.y + static_cast<float>(dragged.height) * displaySize.y / sourceHeight};
            drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 2.0f);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            sourceRegionDragging_ = false;
            std::string validationError;
            if (!validate_sprite_source_region(dragged, sourceWidth, sourceHeight, validationError))
                assignmentFeedback_ = std::move(validationError);
            else
            {
                const auto committed = commit_sprite_source_region(commands, document,
                    target.address.objectId, *target.address.componentId, textureReference,
                    dragged, sourceWidth, sourceHeight);
                assignmentFeedback_ = committed.status == CommandStatus::Changed
                    ? "Sprite texture and source region committed as one Undo step."
                    : committed.status == CommandStatus::NoChange
                        ? "Sprite source region is unchanged." : committed.message;
            }
        }
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            sourceRegionDragging_ = false;
    }
    if (!sourceRegionDragging_ || ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        const ImVec2 minimum{canvasMinimum.x + static_cast<float>(visibleRegion.x) * displaySize.x / sourceWidth,
                             canvasMinimum.y + static_cast<float>(visibleRegion.y) * displaySize.y / sourceHeight};
        const ImVec2 maximum{minimum.x + static_cast<float>(visibleRegion.width) * displaySize.x / sourceWidth,
                             minimum.y + static_cast<float>(visibleRegion.height) * displaySize.y / sourceHeight};
        drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 2.0f);
    }

    bool submitNumericRegion = false;
    ImGui::SetNextItemWidth(90.0f);
    submitNumericRegion |= ImGui::InputDouble("X (px)", &visibleRegion.x, 1.0, 8.0, "%.0f",
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    submitNumericRegion |= ImGui::InputDouble("Y (px)", &visibleRegion.y, 1.0, 8.0, "%.0f",
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetNextItemWidth(90.0f);
    submitNumericRegion |= ImGui::InputDouble("Width", &visibleRegion.width, 1.0, 8.0, "%.0f",
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    submitNumericRegion |= ImGui::InputDouble("Height", &visibleRegion.height, 1.0, 8.0, "%.0f",
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (submitNumericRegion)
    {
        std::string error;
        if (!validate_sprite_source_region(visibleRegion, sourceWidth, sourceHeight, error))
            assignmentFeedback_ = std::move(error);
        else
        {
            const auto committed = commit_sprite_source_region(commands, document,
                target.address.objectId, *target.address.componentId, textureReference,
                visibleRegion, sourceWidth, sourceHeight);
            assignmentFeedback_ = committed.status == CommandStatus::Changed
                ? "Sprite texture and source region committed as one Undo step."
                : committed.status == CommandStatus::NoChange
                    ? "Sprite source region is unchanged." : committed.message;
        }
    }
}

void ProjectPanel::draw_assignment_controls(DocumentSession &document,
                                            EditCommandService &commands,
                                            SelectionModel *selection)
{
    if (!selectedEntry_)
    {
        assetDoubleClickPending_ = false;
        return;
    }
    std::error_code selectedEntryError;
    if (std::filesystem::is_directory(*selectedEntry_, selectedEntryError) || selectedEntryError)
    {
        assetDoubleClickPending_ = false;
        return;
    }
    if (editor_asset_is(*selectedEntry_, EditorAssetKind::Template))
    {
        draw_template_controls(document, commands, selection);
        return;
    }
    if (editor_asset_format(*selectedEntry_).kind == EditorAssetKind::Animation && assetDoubleClickPending_ &&
        openAnimationCallback_)
    {
        assetDoubleClickPending_ = false;
        openAnimationCallback_(*selectedEntry_);
        return;
    }
    ImGui::SeparatorText("Assign Selected Asset");
    if (selection == nullptr)
    {
        ImGui::TextDisabled("Select a compatible asset field in the Inspector first.");
        assetDoubleClickPending_ = false;
        return;
    }
    const auto targets = asset_assignment_targets(document, *selection, *selectedEntry_);
    if (targets.empty())
    {
        ImGui::TextDisabled("No selected asset field accepts this file type and project path.");
        if (assetDoubleClickPending_)
            assignmentFeedback_ = "Select a compatible texture, animation, or audio reference field.";
        assetDoubleClickPending_ = false;
    }
    std::optional<std::size_t> selectedIndex;
    if (targets.size() == 1)
        selectedIndex = 0;
    else if (!selectedAssignmentTarget_.empty())
    {
        for (std::size_t index = 0; index < targets.size(); ++index)
            if (targets[index].label == selectedAssignmentTarget_)
            {
                selectedIndex = index;
                break;
            }
    }
    if (targets.size() > 1)
    {
        const char *preview = selectedIndex ? targets[*selectedIndex].label.c_str()
                                            : "Choose a target field...";
        if (ImGui::BeginCombo("##asset-assignment-target", preview))
        {
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                const bool selected = selectedIndex == index;
                if (ImGui::Selectable(targets[index].label.c_str(), selected))
                {
                    selectedAssignmentTarget_ = targets[index].label;
                    selectedIndex = index;
                    assignmentFeedback_.clear();
                }
            }
            ImGui::EndCombo();
        }
        if (assetDoubleClickPending_)
            assignmentFeedback_ = "This asset has multiple compatible targets. Choose one, then click Assign.";
    }
    const bool canAssign = selectedIndex.has_value() && !targets.empty();
    ImGui::BeginDisabled(!canAssign);
    const bool assignClicked = ImGui::Button("Assign");
    ImGui::EndDisabled();
    const bool singleTargetDoubleClick = assetDoubleClickPending_ && targets.size() == 1;
    assetDoubleClickPending_ = false;
    if (assignClicked || singleTargetDoubleClick)
    {
        const auto &target = targets[*selectedIndex];
        std::string error;
        assignmentFeedback_ = assign_asset_target(document, commands, target,
                                                  *selectedEntry_, error)
            ? "Asset reference assigned to " + target.label + "."
            : error;
    }
    if (selectedIndex && *selectedIndex < targets.size())
        draw_sprite_region_editor(document, commands, targets[*selectedIndex], *selectedEntry_);
    else
        sourceRegionDragging_ = false;
    if (!assignmentFeedback_.empty())
        ImGui::TextWrapped("%s", assignmentFeedback_.c_str());
}

void ProjectPanel::draw(DocumentSession &document, EditCommandService &commands,
                        SelectionModel *selection)
{
    pendingSemanticNodes_.clear();
    pendingTileTargets_.clear();
    nextAnimationDeadline_.reset();
    apply_ready_results();
    const auto &snapshot = context_.snapshot();
    const auto &assetRoot = snapshot.assetRoot;
    const auto &projectRoot = snapshot.projectRoot;
    if (assetRoot.empty() || document.assetRoot != assetRoot)
    {
        ImGui::TextDisabled("Project asset root is unavailable.");
        return;
    }

    if (ImGui::Button("Refresh Assets"))
    {
        refresh();
        referenceRetryRequested_ = true;
    }
    if (!coverageMessage_.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", coverageMessage_.c_str());
    }
    if (!referenceRetryMessage_.empty())
        ImGui::TextDisabled("%s", referenceRetryMessage_.c_str());

    const auto behaviorRoot = assetRoot / "behaviors";
    std::error_code filesystemError;
    const bool hasBehaviorRoot = std::filesystem::is_directory(behaviorRoot, filesystemError) &&
                                 !filesystemError;
    if (assetRoot_ != assetRoot || projectRoot_ != projectRoot)
    {
        assetRoot_ = assetRoot;
        projectRoot_ = projectRoot;
        selectedDirectory_ = assetRoot;
        selectedRoot_ = assetRoot;
        selectedRootLabel_ = "Assets";
        selectedEntry_.reset();
        expandedDirectories_.clear();
        expandedDirectories_.insert(assetRoot.string());
        if (hasBehaviorRoot)
            expandedDirectories_.insert(behaviorRoot.string());
    }
    const bool selectedIsAsset = path_is_within(assetRoot, selectedDirectory_);
    bool selectedIsBehavior = hasBehaviorRoot && path_is_within(behaviorRoot, selectedDirectory_);
    if (!selectedIsAsset && !selectedIsBehavior)
    {
        selectedDirectory_ = assetRoot;
        selectedRoot_ = assetRoot;
        selectedRootLabel_ = "Assets";
        selectedEntry_.reset();
        expandedDirectories_.insert(assetRoot.string());
        selectedIsBehavior = false;
    }

    draw_assignment_controls(document, commands, selection);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    const float leftWidth = std::clamp(viewState_.folderTreeWidth, 90.0f * uiScale, std::max(120.0f * uiScale, available.x - 120.0f * uiScale));
    ImGui::BeginChild("Asset folders", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    viewState_.folderTreeWidth = ImGui::GetWindowWidth();
    draw_folder_tree(assetRoot, assetRoot, "Assets", 0);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Asset contents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    draw_contents(selectedRoot_, selectedRootLabel_, selectedIsBehavior);
    ImGui::EndChild();
}
} // namespace platformator_editor
