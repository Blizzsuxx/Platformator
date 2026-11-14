#include "referencefields.h"

#include "../assetformats.h"
#include "../resourceindex.h"
#include "../discovery.h"
#include "../style.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace platformator_editor
{
namespace
{
ImGuiID scene_reference_search_storage_key(int word)
{
    ImGui::PushID("##scene-reference-search-storage");
    ImGui::PushID(word);
    const ImGuiID id = ImGui::GetID("word");
    ImGui::PopID();
    ImGui::PopID();
    return id;
}

std::array<char, 128> load_scene_reference_search_buffer()
{
    static_assert(sizeof(int) == 4);
    std::array<char, 128> buffer{};
    ImGuiStorage *storage = ImGui::GetStateStorage();
    for (int word = 0; word < static_cast<int>(buffer.size() / sizeof(int)); ++word)
    {
        const int packed = storage->GetInt(scene_reference_search_storage_key(word));
        std::memcpy(buffer.data() + static_cast<std::size_t>(word) * sizeof(packed),
                    &packed, sizeof(packed));
    }
    return buffer;
}

void save_scene_reference_search_buffer(const std::array<char, 128> &buffer)
{
    ImGuiStorage *storage = ImGui::GetStateStorage();
    for (int word = 0; word < static_cast<int>(buffer.size() / sizeof(int)); ++word)
    {
        int packed = 0;
        std::memcpy(&packed, buffer.data() + static_cast<std::size_t>(word) * sizeof(packed),
                    sizeof(packed));
        storage->SetInt(scene_reference_search_storage_key(word), packed);
    }
}

std::optional<int> json_id(const Json &value)
{
    if (!value.is_number_integer() && !value.is_number_unsigned())
        return std::nullopt;
    try
    {
        const auto id = value.get<std::int64_t>();
        if (id < 0 || id > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(id);
    }
    catch (...)
    {
        return std::nullopt;
    }
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
        const auto index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}

bool component_matches(const Json &component, std::string_view expectedTarget)
{
    if (!component.is_object())
        return false;
    const auto type = component.find("type");
    if (type == component.end())
        return false;
    const auto parsed = json_id(*type);
    if (!parsed)
        return false;
    if (expectedTarget == "Component")
        return true;
    const int wanted = expectedTarget == "Animator"          ? 0
                       : expectedTarget == "Audio"            ? 1
                       : expectedTarget == "Camera"           ? 2
                       : expectedTarget == "Collider"         ? 3
                       : expectedTarget == "Rigidbody"        ? 4
                       : expectedTarget == "ScriptComponent"  ? 5
                       : expectedTarget == "Sprite"           ? 6
                                                               : -1;
    return wanted >= 0 && *parsed == wanted;
}

std::string target_label(const Json &documentRoot, const DocumentIndexes &indexes, int id,
                         std::string_view target, bool includeIds = true)
{
    if (target == "GameObject")
    {
        const auto location = indexes.objectsById.find(id);
        if (location == indexes.objectsById.end())
            return {};
        const Json *object = resolve_object(documentRoot, location->second);
        if (object == nullptr)
            return {};
        std::vector<std::string> names;
        int currentId = id;
        while (true)
        {
            const auto current = indexes.objectsById.find(currentId);
            if (current == indexes.objectsById.end())
                break;
            const Json *record = resolve_object(documentRoot, current->second);
            if (record == nullptr)
                break;
            names.push_back(record->value("name", std::string("GameObject")));
            const auto parent = indexes.parentById.find(currentId);
            if (parent == indexes.parentById.end() || !parent->second)
                break;
            currentId = *parent->second;
        }
        std::string label;
        for (auto name = names.rbegin(); name != names.rend(); ++name)
        {
            if (!label.empty())
                label += " / ";
            label += *name;
        }
        if (includeIds)
            label += " (#" + std::to_string(id) + ")";
        return label;
    }
    const auto location = indexes.componentsById.find(id);
    if (location == indexes.componentsById.end())
        return {};
    const auto ownerLocation = indexes.objectsById.find(location->second.ownerObjectId);
    if (ownerLocation == indexes.objectsById.end())
        return {};
    const Json *owner = resolve_object(documentRoot, ownerLocation->second);
    if (owner == nullptr)
        return {};
    const auto components = owner->find("components");
    if (components == owner->end() || !components->is_array() ||
        location->second.componentIndex >= components->size())
        return {};
    const std::string ownerName = target_label(documentRoot, indexes,
                                               location->second.ownerObjectId, "GameObject",
                                               includeIds);
    std::string label = ownerName.empty() ? std::string(target) : ownerName + " / " + std::string(target);
    const Json &component = (*components)[location->second.componentIndex];
    if (includeIds)
        label += " (#" + std::to_string(id) + ")";
    const char *assetField = target == "Audio" ? "filePath"
                             : target == "Animator" ? "animationClipFilePath"
                             : target == "Sprite" ? "textureFilePath"
                                                   : nullptr;
    if (assetField != nullptr)
    {
        const auto asset = component.find(assetField);
        if (asset != component.end() && asset->is_string() && !asset->get_ref<const std::string &>().empty())
            label += " — " + std::filesystem::path(asset->get<std::string>()).filename().string();
    }
    return label;
}

const Json *property_owner(const DocumentSession &document, int objectId,
                           std::optional<int> componentId,
                           std::optional<std::size_t> behaviorOrdinal)
{
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (objectLocation == document.indexes.objectsById.end())
        return nullptr;
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return nullptr;
    if (!componentId)
        return behaviorOrdinal ? nullptr : object;
    const auto componentLocation = document.indexes.componentsById.find(*componentId);
    if (componentLocation == document.indexes.componentsById.end() ||
        componentLocation->second.ownerObjectId != objectId)
        return nullptr;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        componentLocation->second.componentIndex >= components->size())
        return nullptr;
    const Json &component = (*components)[componentLocation->second.componentIndex];
    if (!behaviorOrdinal)
        return &component;
    const auto behaviors = component.find("behaviors");
    if (behaviors == component.end() || !behaviors->is_array() || *behaviorOrdinal >= behaviors->size())
        return nullptr;
    return &(*behaviors)[*behaviorOrdinal];
}

const Json *read_path(const Json *root, const std::vector<FieldPathSegment> &path)
{
    const Json *value = root;
    for (const auto &segment : path)
    {
        if (value == nullptr)
            return nullptr;
        if (const auto *key = std::get_if<std::string>(&segment))
        {
            if (!value->is_object())
                return nullptr;
            const auto found = value->find(*key);
            value = found == value->end() ? nullptr : &*found;
        }
        else
        {
            const auto index = std::get<std::size_t>(segment);
            if (!value->is_array() || index >= value->size())
                return nullptr;
            value = &(*value)[index];
        }
    }
    return value;
}

void push_field_id(int objectId, std::optional<int> componentId,
                   std::optional<std::size_t> behaviorOrdinal,
                   const std::vector<FieldPathSegment> &path)
{
    ImGui::PushID(objectId);
    ImGui::PushID(componentId.value_or(-1));
    ImGui::PushID(behaviorOrdinal ? static_cast<int>(*behaviorOrdinal) : -1);
    for (const auto &segment : path)
    {
        if (const auto *key = std::get_if<std::string>(&segment))
            ImGui::PushID(key->c_str());
        else
            ImGui::PushID(static_cast<int>(std::get<std::size_t>(segment)));
    }
}

void pop_field_id(const std::vector<FieldPathSegment> &path)
{
    for (std::size_t index = 0; index < path.size() + 3; ++index)
        ImGui::PopID();
}

std::vector<std::pair<int, std::string>> scene_candidates(const DocumentSession &document,
                                                          std::string_view target)
{
    std::vector<std::pair<int, std::string>> result;
    for (const int objectId : document.indexes.orderedObjectIds)
    {
        if (target == "GameObject")
        {
            const auto location = document.indexes.objectsById.find(objectId);
            if (location == document.indexes.objectsById.end())
                continue;
            const Json *object = resolve_object(document.root, location->second);
            if (object != nullptr)
                result.emplace_back(objectId, target_label(document.root, document.indexes,
                                                           objectId, "GameObject"));
            continue;
        }
        const auto location = document.indexes.objectsById.find(objectId);
        if (location == document.indexes.objectsById.end())
            continue;
        const Json *object = resolve_object(document.root, location->second);
        if (object == nullptr)
            continue;
        const auto components = object->find("components");
        if (components == object->end() || !components->is_array())
            continue;
        for (const auto &component : *components)
        {
            if (!component_matches(component, target))
                continue;
            const auto id = component.find("id");
            if (id == component.end())
                continue;
            const auto componentId = json_id(*id);
            if (!componentId)
                continue;
            result.emplace_back(*componentId, target_label(document.root, document.indexes,
                                                           *componentId, target));
        }
    }
    return result;
}

std::string escape_json_pointer_segment(std::string_view value)
{
    std::string escaped;
    for (const char character : value)
    {
        if (character == '~') escaped += "~0";
        else if (character == '/') escaped += "~1";
        else escaped.push_back(character);
    }
    return escaped;
}
} // namespace

SceneReferenceInfo inspect_scene_reference(const DocumentSession &document, const Json &value,
                                           std::string_view expectedTarget)
{
    if (value.is_null())
        return {ReferenceState::Unset, std::nullopt, "None"};
    const auto id = json_id(value);
    if (!id)
        return {ReferenceState::Invalid, std::nullopt, "Invalid reference value"};
    if (expectedTarget == "GameObject")
    {
        const auto location = document.indexes.objectsById.find(*id);
        if (location == document.indexes.objectsById.end())
            return {ReferenceState::Dangling, id, "Missing GameObject reference"};
        const Json *object = resolve_object(document.root, location->second);
        return {ReferenceState::Resolved, id,
                object == nullptr ? "GameObject" : target_label(document.root, document.indexes, *id, "GameObject")};
    }
    const auto location = document.indexes.componentsById.find(*id);
    if (location == document.indexes.componentsById.end())
        return {ReferenceState::Dangling, id, "Missing component reference"};
    const auto ownerLocation = document.indexes.objectsById.find(location->second.ownerObjectId);
    const Json *owner = ownerLocation == document.indexes.objectsById.end()
                            ? nullptr
                            : resolve_object(document.root, ownerLocation->second);
    if (owner == nullptr)
        return {ReferenceState::Dangling, id, "Missing component reference"};
    const auto components = owner->find("components");
    if (components == owner->end() || !components->is_array() ||
        location->second.componentIndex >= components->size())
        return {ReferenceState::Dangling, id, "Missing component reference"};
    if (!component_matches((*components)[location->second.componentIndex], expectedTarget))
        return {ReferenceState::WrongKind, id, "Reference target has the wrong component type"};
    return {ReferenceState::Resolved, id,
            target_label(document.root, document.indexes, *id, expectedTarget)};
}

bool validate_scene_reference_assignment(const DocumentSession &document, const Json &value,
                                         std::string_view expectedTarget, bool nullable,
                                         std::string &error)
{
    const auto info = inspect_scene_reference(document, value, expectedTarget);
    switch (info.state)
    {
    case ReferenceState::Unset:
        if (nullable)
            return true;
        error = "This reference cannot be empty.";
        return false;
    case ReferenceState::Resolved:
        return true;
    case ReferenceState::Dangling:
        error = "The selected reference target does not exist in this scene.";
        return false;
    case ReferenceState::WrongKind:
        error = "The selected target has the wrong component kind.";
        return false;
    case ReferenceState::Invalid:
        error = "Select a valid scene object or component.";
        return false;
    }
    error = "The reference is invalid.";
    return false;
}

Json make_template_reference_binding_declaration(int ownerId, int componentId,
                                                  std::size_t behaviorOrdinal,
                                                  std::string_view fieldName,
                                                  std::string_view targetKind)
{
    if (fieldName.empty()) return Json::object();
    const std::string relativePointer = "/" + escape_json_pointer_segment(fieldName);
    return make_template_reference_path_declaration(ownerId, componentId, behaviorOrdinal,
                                                    relativePointer, targetKind);
}

Json make_template_reference_path_declaration(int ownerId, int componentId,
                                               std::size_t behaviorOrdinal,
                                               std::string_view relativePointer,
                                               std::string_view targetKind)
{
    if (ownerId < 0 || componentId < 0 || relativePointer.empty() ||
        relativePointer.front() != '/' ||
        (targetKind != "GameObject" && targetKind != "Component"))
        return Json::object();
    for (std::size_t index = 0; index < relativePointer.size(); ++index)
    {
        if (relativePointer[index] != '~') continue;
        if (index + 1 >= relativePointer.size() ||
            (relativePointer[index + 1] != '0' && relativePointer[index + 1] != '1'))
            return Json::object();
        ++index;
    }
    if (relativePointer.back() == '/' || relativePointer.find("//") != std::string_view::npos)
        return Json::object();
    return Json{{"owner_id", ownerId}, {"component_id", componentId},
                {"path", "/behaviors/" + std::to_string(behaviorOrdinal) +
                         std::string(relativePointer)},
                {"target_kind", targetKind}};
}

AssetReferenceInfo inspect_asset_reference(const DocumentSession &document, const Json &value,
                                           std::string_view expectedAssetType,
                                           ResourceIndex *resourceIndex)
{
    if (value.is_null())
        return {ReferenceState::Unset, {}, {}};
    if (!value.is_string())
        return {ReferenceState::Invalid, {}, "Asset references must be a path string or null."};
    std::string raw = value.get<std::string>();
    if (raw.empty())
        return {ReferenceState::Unset, {}, {}};
    std::replace(raw.begin(), raw.end(), '\\', '/');
    const std::filesystem::path path(raw);
    if (path.is_absolute() || path.begin() == path.end() || *path.begin() != "assets")
        return {ReferenceState::Invalid, raw, "Asset paths must stay inside assets/...."};
    for (const auto &part : path)
        if (part == ".." || part == ".")
            return {ReferenceState::Invalid, raw, "Asset paths cannot contain traversal segments."};
    if (document.assetRoot.empty())
        return {ReferenceState::Invalid, raw, "The project asset root is unavailable."};

    if (resourceIndex != nullptr)
    {
        const ResourceKind kind = expectedAssetType == "Texture" ? ResourceKind::Texture
            : expectedAssetType == "AnimationClip" ? ResourceKind::Animation
            : expectedAssetType == "Audio" ? ResourceKind::Audio
            : expectedAssetType == "Script" ? ResourceKind::Script
            : expectedAssetType == "Scene" ? ResourceKind::Scene
            : ResourceKind::Unknown;
        const auto resolution = resourceIndex->resolve(path, kind);
        if (resolution.status == ResourceStatus::Resolved)
        {
            if (!asset_reference_extension_matches(expectedAssetType, resolution.canonicalPath))
                return {ReferenceState::WrongKind, raw, "The asset file type does not match this reference."};
            return {ReferenceState::Resolved, raw, {}};
        }
        if (resolution.status == ResourceStatus::Missing ||
            resolution.status == ResourceStatus::BrokenSymlink)
            return {ReferenceState::Dangling, raw, "The referenced asset is missing or is not a file."};
        if (resolution.status == ResourceStatus::OutsideRoot)
            return {ReferenceState::Invalid, raw, "The asset path resolves outside the project asset root."};
        if (resolution.status == ResourceStatus::SymlinkCycle)
            return {ReferenceState::Invalid, raw, "The asset path contains a symbolic link cycle."};
        return {ReferenceState::Invalid, raw, "The referenced asset cannot be read."};
    }

    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(document.assetRoot, error);
    if (error)
        return {ReferenceState::Invalid, raw, "The project asset root cannot be resolved."};
    const auto relative = path.lexically_relative("assets");
    const auto candidate = std::filesystem::weakly_canonical(canonicalRoot / relative, error);
    if (error)
        return {ReferenceState::Invalid, raw, "The asset path cannot be resolved."};
    auto rootPart = canonicalRoot.begin();
    auto candidatePart = candidate.begin();
    for (; rootPart != canonicalRoot.end(); ++rootPart, ++candidatePart)
        if (candidatePart == candidate.end() || *rootPart != *candidatePart)
            return {ReferenceState::Invalid, raw, "The asset path resolves outside the project asset root."};
    if (!std::filesystem::is_regular_file(candidate, error) || error)
        return {ReferenceState::Dangling, raw, "The referenced asset is missing or is not a file."};
    if (!asset_reference_extension_matches(expectedAssetType, candidate))
        return {ReferenceState::WrongKind, raw, "The asset file type does not match this reference."};
    return {ReferenceState::Resolved, raw, {}};
}

bool validate_asset_reference_assignment(const DocumentSession &document, const Json &value,
                                         std::string_view expectedAssetType, bool nullable,
                                         std::string &error)
{
    const auto info = inspect_asset_reference(document, value, expectedAssetType);
    if (info.state == ReferenceState::Unset)
    {
        if (nullable)
            return true;
        error = "This asset reference cannot be empty.";
        return false;
    }
    if (info.state == ReferenceState::Resolved)
        return true;
    error = info.message.empty() ? "The asset reference is invalid." : info.message;
    return false;
}

namespace
{
std::string asset_reference_key(const std::vector<PropertyAddress> &targets);
void remember_asset_validation_error(const std::string &key, std::uint64_t valueRevision,
                                     std::string message);
void clear_asset_validation_error(const std::string &key);
void show_asset_validation_error(const std::string &key, std::uint64_t valueRevision);
} // namespace

void draw_scene_reference_field(DocumentSession &document, EditCommandService &commands,
                                int objectId, int componentId, std::size_t behaviorOrdinal,
                                const std::vector<FieldPathSegment> &path,
                                std::string_view expectedTarget, bool nullable, const char *label,
                                ReferenceFieldOverride overrideInfo)
{
    push_field_id(objectId, componentId, behaviorOrdinal, path);
    const Json *current = read_path(property_owner(document, objectId, componentId, behaviorOrdinal), path);
    const Json defaultNull = nullptr;
    const auto info = inspect_scene_reference(document, current == nullptr ? defaultNull : *current, expectedTarget);
    const std::string targetType = expectedTarget == "GameObject"
        ? "GameObject" : std::string(expectedTarget) + " component";
    const std::string cleanInfoLabel = (info.state == ReferenceState::Resolved && info.fileId.has_value())
        ? target_label(document.root, document.indexes, *info.fileId, expectedTarget, false)
        : std::string(strip_identity_suffix(info.label));
    const std::string identity = info.state == ReferenceState::Unset
        ? "Select " + targetType + "..."
        : info.state == ReferenceState::Resolved
            ? (cleanInfoLabel.empty() ? targetType + " (selected)" : targetType + ": " + cleanInfoLabel)
            : "Missing " + targetType;
    const std::string validationKey = asset_reference_key({
        address_for_document(document, objectId, componentId, behaviorOrdinal, path)});
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    ImGui::AlignTextToFramePadding();
    const float available = ImGui::GetContentRegionAvail().x;
    const float labelWidth = std::max(1.0f, std::min(124.0f * uiScale, available * 0.42f));
    const float frameHeight = ImGui::GetFrameHeight();
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##scene-ref-label", ImVec2(labelWidth, frameHeight));
    draw_clipped_property_label(ImGui::GetWindowDrawList(), cursorPos.x, cursorPos.y,
                                labelWidth, frameHeight, label,
                                ImGui::GetColorU32(overrideInfo.overridden
                                    ? ImGuiCol_NavHighlight : ImGuiCol_TextDisabled),
                                ImGui::IsItemHovered());
    if (overrideInfo.overridden && ImGui::IsItemHovered())
        ImGui::SetTooltip("This field overrides its linked template value.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x));
    auto assign = [&](Json value) {
        std::string error;
        if (!validate_scene_reference_assignment(document, value, expectedTarget, nullable, error))
        {
            remember_asset_validation_error(validationKey, document.valueRevision, std::move(error));
            return;
        }
        clear_asset_validation_error(validationKey);
        commands.set_property(document,
                              address_for_document(document, objectId, componentId,
                                                   behaviorOrdinal, path),
                              std::move(value));
    };
    if (overrideInfo.overridden)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.27f, 0.24f, 0.16f, 1.0f));
    const bool comboOpen = ImGui::BeginCombo("##reference-selector", identity.c_str());
    if (overrideInfo.overridden)
        ImGui::PopStyleColor();
    if (info.state == ReferenceState::Dangling || info.state == ReferenceState::WrongKind ||
        info.state == ReferenceState::Invalid)
        ImGui::SetItemTooltip("%s", info.label.c_str());
    else if (overrideInfo.overridden)
        ImGui::SetItemTooltip("This field overrides its linked template value.");
    show_asset_validation_error(validationKey, document.valueRevision);
    if (comboOpen)
    {
        auto filterBuffer = load_scene_reference_search_buffer();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##reference-selector-search", "Search name or type",
                                 filterBuffer.data(), filterBuffer.size());
        save_scene_reference_search_buffer(filterBuffer);
        const std::string_view filter(filterBuffer.data());
        ImGui::Separator();
        if (nullable && discovery_query_matches(filter, "None") &&
            ImGui::Selectable("None", info.state == ReferenceState::Unset))
            assign(nullptr);
        const auto candidates = scene_candidates(document, expectedTarget);
        if (candidates.empty())
            ImGui::TextDisabled("No matching reference targets.");
        for (const auto &[id, candidateLabel] : candidates)
        {
            const std::string searchText = candidateLabel + " " + std::to_string(id) + " " +
                                           std::string(expectedTarget);
            if (!discovery_query_matches(filter, searchText))
                continue;
            const bool selected = info.fileId && *info.fileId == id && info.state == ReferenceState::Resolved;
            ImGui::PushID(id);
            const std::string displayLabel = target_label(document.root, document.indexes, id, expectedTarget, false);
            if (ImGui::Selectable(displayLabel.c_str(), selected))
                assign(id);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (overrideInfo.overridden && ImGui::BeginPopupContextItem("SceneReferenceContextMenu"))
    {
        if (ImGui::MenuItem("Revert Property") && overrideInfo.revert)
            overrideInfo.revert();
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PLATFORMATOR_HIERARCHY_OBJECT"))
        {
            if (payload->DataSize == sizeof(int))
            {
                const int droppedObject = *static_cast<const int *>(payload->Data);
                std::optional<int> target;
                if (expectedTarget == "GameObject")
                    target = droppedObject;
                else
                {
                    const auto location = document.indexes.objectsById.find(droppedObject);
                    const Json *object = location == document.indexes.objectsById.end()
                        ? nullptr : resolve_object(document.root, location->second);
                    if (object != nullptr && object->contains("components") && (*object)["components"].is_array())
                        for (const auto &component : (*object)["components"])
                            if (component_matches(component, expectedTarget) && component.contains("id"))
                            {
                                // Ambiguous objects require choosing the component in the picker.
                                if (target) { target.reset(); break; }
                                target = json_id(component["id"]);
                            }
                }
                if (target)
                    assign(*target);
            }
        }
        ImGui::EndDragDropTarget();
    }
    pop_field_id(path);
}

namespace
{
std::string asset_reference_tooltip(const std::string &rawPath, const AssetReferenceInfo &info)
{
    std::string tooltip = "Full path: " + (rawPath.empty() ? std::string("(unset)") : rawPath);
    if ((info.state == ReferenceState::Dangling || info.state == ReferenceState::WrongKind ||
         info.state == ReferenceState::Invalid) && !info.message.empty())
        tooltip += "\n" + info.message;
    return tooltip;
}

struct AssetValidationFeedback
{
    std::uint64_t valueRevision = 0;
    std::string message;
};

std::unordered_map<std::string, AssetValidationFeedback> &asset_validation_feedback()
{
    static std::unordered_map<std::string, AssetValidationFeedback> feedback;
    return feedback;
}

std::string asset_reference_key(const std::vector<PropertyAddress> &targets)
{
    std::string key;
    for (const auto &target : targets)
    {
        key += std::to_string(target.identity.projectGeneration.value) + ":" +
            std::to_string(target.identity.tabId.value) + ":" +
            std::to_string(target.identity.documentGeneration.value) + ":" +
            std::to_string(target.objectId) + ":" +
            std::to_string(target.componentId.value_or(-1)) + ":" +
            std::to_string(target.behaviorOrdinal ? *target.behaviorOrdinal :
                std::numeric_limits<std::size_t>::max()) + ";";
        for (const auto &segment : target.fieldPath)
        {
            if (const auto *name = std::get_if<std::string>(&segment))
                key += "s" + std::to_string(name->size()) + ":" + *name + ";";
            else
                key += "i" + std::to_string(std::get<std::size_t>(segment)) + ";";
        }
        key.push_back('|');
    }
    return key;
}

void remember_asset_validation_error(const std::string &key, std::uint64_t valueRevision,
                                     std::string message)
{
    auto &feedback = asset_validation_feedback();
    if (feedback.size() >= 128 && !feedback.contains(key))
        feedback.erase(feedback.begin());
    feedback[key] = {valueRevision, std::move(message)};
}

void clear_asset_validation_error(const std::string &key)
{
    asset_validation_feedback().erase(key);
}

void show_asset_validation_error(const std::string &key, std::uint64_t valueRevision)
{
    auto &feedback = asset_validation_feedback();
    const auto found = feedback.find(key);
    if (found == feedback.end())
        return;
    if (found->second.valueRevision != valueRevision)
    {
        feedback.erase(found);
        return;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetItemTooltip("%s", found->second.message.c_str());
}
} // namespace

void draw_asset_reference_field(DocumentSession &document, EditCommandService &commands,
                                int objectId, std::optional<int> componentId,
                                std::optional<std::size_t> behaviorOrdinal,
                                const std::vector<FieldPathSegment> &path,
                                std::string_view expectedAssetType, bool nullable, const char *label,
                                const AssetBrowseCallback &browse, Json clearValue,
                                ResourceIndex *resourceIndex,
                                ReferenceFieldOverride overrideInfo)
{
    push_field_id(objectId, componentId, behaviorOrdinal, path);
    const Json *current = read_path(property_owner(document, objectId, componentId, behaviorOrdinal), path);
    const Json currentValue = current == nullptr ? Json(nullptr) : *current;
    const auto info = inspect_asset_reference(document, currentValue, expectedAssetType, resourceIndex);
    if (resourceIndex != nullptr && info.state != ReferenceState::Invalid && !info.path.empty())
    {
        const ResourceKind kind = expectedAssetType == "Texture" ? ResourceKind::Texture
            : expectedAssetType == "AnimationClip" ? ResourceKind::Animation
            : expectedAssetType == "Audio" ? ResourceKind::Audio
            : expectedAssetType == "Script" ? ResourceKind::Script
            : expectedAssetType == "Scene" ? ResourceKind::Scene
            : ResourceKind::Unknown;
        std::string role = "inspector-asset:" + std::string(expectedAssetType) + ":";
        if (behaviorOrdinal)
            role += "behavior:" + std::to_string(*behaviorOrdinal) + ":";
        for (const auto &segment : path)
        {
            if (const auto *key = std::get_if<std::string>(&segment))
                role += "s" + std::to_string(key->size()) + ":" + *key + ";";
            else
                role += "i" + std::to_string(std::get<std::size_t>(segment)) + ";";
        }
        resourceIndex->register_visible_reference(info.path, kind,
            ResourceConsumer{DocumentIdentity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                              DocumentGeneration{document.documentGeneration}},
                             objectId, componentId, std::move(role)});
    }
    const std::string display = !info.path.empty()
        ? std::filesystem::path(info.path).filename().string()
        : "None";
    const std::string rawPath = currentValue.is_string()
        ? currentValue.get<std::string>() : std::string{};
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    ImGui::AlignTextToFramePadding();
    const float available = ImGui::GetContentRegionAvail().x;
    const bool hasLabel = label != nullptr && label[0] != '\0';
    const float labelWidth = hasLabel
        ? std::max(1.0f, std::min(124.0f * uiScale, available * 0.42f)) : 0.0f;
    const float frameHeight = ImGui::GetFrameHeight();
    if (hasLabel)
    {
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##asset-ref-label", ImVec2(labelWidth, frameHeight));
        draw_clipped_property_label(ImGui::GetWindowDrawList(), cursorPos.x, cursorPos.y,
                                    labelWidth, frameHeight, label,
                                    ImGui::GetColorU32(overrideInfo.overridden
                                        ? ImGuiCol_NavHighlight : ImGuiCol_TextDisabled),
                                    ImGui::IsItemHovered());
        if (overrideInfo.overridden && ImGui::IsItemHovered())
            ImGui::SetTooltip("This field overrides its linked template value.");
        ImGui::SameLine();
    }
    const std::string validationKey = asset_reference_key({
        address_for_document(document, objectId, componentId, behaviorOrdinal, path)});
    if (overrideInfo.overridden)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.24f, 0.16f, 1.0f));
    const bool choose = ImGui::Button((display + "##asset-selector").c_str(),
                                      {std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f});
    if (overrideInfo.overridden)
        ImGui::PopStyleColor();
    const auto referenceTooltip = asset_reference_tooltip(rawPath, info);
    if (overrideInfo.overridden)
        ImGui::SetItemTooltip("%s\nThis field overrides its linked template value.", referenceTooltip.c_str());
    else
        ImGui::SetItemTooltip("%s", referenceTooltip.c_str());
    show_asset_validation_error(validationKey, document.valueRevision);
    if (ImGui::BeginPopupContextItem("AssetReferenceContextMenu"))
    {
        if (ImGui::MenuItem("Copy Path", nullptr, false, !rawPath.empty()))
        {
            SDL_SetClipboardText(rawPath.c_str());
        }
        if (browse && ImGui::MenuItem("Browse..."))
        {
            browse(address_for_document(document, objectId, componentId, behaviorOrdinal, path),
                   std::string(expectedAssetType), nullable);
        }
        if (nullable && ImGui::MenuItem("Clear", nullptr, false, !rawPath.empty()))
        {
            clear_asset_validation_error(validationKey);
            commands.set_property(document,
                                  address_for_document(document, objectId, componentId,
                                                       behaviorOrdinal, path),
                                  clearValue);
        }
        if (overrideInfo.overridden && ImGui::MenuItem("Revert Property") && overrideInfo.revert)
            overrideInfo.revert();
        ImGui::EndPopup();
    }
    if (choose && browse)
        browse(address_for_document(document, objectId, componentId, behaviorOrdinal, path),
               std::string(expectedAssetType), nullable);
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PLATFORMATOR_ASSET_PATH"))
        {
            if (payload->Data != nullptr && payload->DataSize > 0)
            {
                std::string dropped;
                if (payload->DataSize == sizeof(AssetDragPayload))
                {
                    const auto *drag = static_cast<const AssetDragPayload *>(payload->Data);
                    dropped = drag->relativeAssetPath;
                }
                else
                {
                    const auto *bytes = static_cast<const char *>(payload->Data);
                    dropped = bytes;
                }
                while (!dropped.empty() && dropped.back() == '\0')
                    dropped.pop_back();
                std::string error;
                if (validate_asset_reference_assignment(document, dropped, expectedAssetType, nullable, error))
                {
                    std::replace(dropped.begin(), dropped.end(), '\\', '/');
                    clear_asset_validation_error(validationKey);
                    commands.set_property(document,
                        address_for_document(document, objectId, componentId,
                                             behaviorOrdinal, path), std::move(dropped));
                }
                else
                {
                    remember_asset_validation_error(validationKey, document.valueRevision, error);
                    ImGui::SetTooltip("%s", error.c_str());
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    pop_field_id(path);
}

namespace
{
struct MultiAssetPickerCache
{
    std::uint64_t projectGeneration = 0;
    std::string rootKey;
    std::string expectedAssetType;
    std::uint64_t uncertaintyEpoch = 0;
    std::vector<std::pair<std::filesystem::path, std::uint64_t>> scannedDirRevisions;
    bool initialized = false;
    bool truncated = false;
    std::string scanError;
    std::vector<std::string> candidates;
};

MultiAssetPickerCache &picker_cache()
{
    static MultiAssetPickerCache cache;
    return cache;
}

std::size_t &picker_scan_counter() noexcept
{
    static std::size_t scans = 0;
    return scans;
}
} // namespace

std::size_t multi_asset_picker_scan_count_for_test() noexcept
{
    return picker_scan_counter();
}

void draw_clipped_property_label(void *drawListPtr, float x, float y,
                                 float labelWidth, float frameHeight,
                                 const char *label, std::uint32_t color,
                                 bool hovered)
{
    if (drawListPtr == nullptr || label == nullptr || label[0] == '\0' || labelWidth <= 1.0f)
        return;
    ImDrawList *drawList = static_cast<ImDrawList *>(drawListPtr);
    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    const float maxTextWidth = std::max(1.0f, labelWidth - 4.0f * uiScale);
    const float textY = y + (frameHeight - ImGui::GetFontSize()) * 0.5f;
    const float fullWidth = ImGui::CalcTextSize(label).x;
    if (fullWidth <= maxTextWidth)
    {
        drawList->AddText(ImVec2(x, textY), color, label);
        return;
    }

    const float ellipsisWidth = ImGui::CalcTextSize("...").x;
    const std::string_view fullView(label);
    std::size_t bestLen = 0;
    if (maxTextWidth > ellipsisWidth)
    {
        const float targetWidth = maxTextWidth - ellipsisWidth;
        for (std::size_t i = 0; i < fullView.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(fullView[i]);
            if ((c & 0xC0) == 0x80)
                continue;
            const std::size_t candidateLen = i + 1;
            std::size_t utf8End = candidateLen;
            while (utf8End < fullView.size() && (static_cast<unsigned char>(fullView[utf8End]) & 0xC0) == 0x80)
                ++utf8End;
            const float w = ImGui::CalcTextSize(fullView.data(), fullView.data() + utf8End).x;
            if (w <= targetWidth)
                bestLen = utf8End;
            else
                break;
        }
    }
    const std::string elided = bestLen > 0
        ? std::string(fullView.substr(0, bestLen)) + "..."
        : std::string("...");
    const ImVec4 clipRect(x, y, x + labelWidth, y + frameHeight);
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, textY), color,
                      elided.c_str(), elided.c_str() + elided.size(), 0.0f, &clipRect);
    if (hovered)
        ImGui::SetTooltip("%s", label);
}

AssetCompletionResult apply_asset_browse_completion(
    DocumentSession &document, EditCommandService &commands,
    const std::vector<PropertyAddress> &targets,
    const Selection &originSelection,
    const DocumentIdentity &dialogIdentity,
    const std::filesystem::path &activeAssetRoot,
    const std::filesystem::path &selectedPath,
    std::string_view expectedAssetType,
    bool nullable)
{
    if (targets.empty())
        return {false, "No target property addresses were provided for asset assignment."};

    const DocumentIdentity currentDocIdentity{
        ProjectGeneration{document.projectGeneration}, document.tabId,
        DocumentGeneration{document.documentGeneration}};
    if (currentDocIdentity != dialogIdentity)
        return {false, "Asset assignment expired because its originating document changed."};

    for (const auto &target : targets)
    {
        if (target.identity != dialogIdentity)
            return {false, "Asset assignment expired because its originating document changed."};
    }

    std::error_code rootError;
    const auto assetRoot = std::filesystem::canonical(activeAssetRoot, rootError);
    std::error_code assetError;
    const auto selected = std::filesystem::canonical(selectedPath, assetError);
    const auto relative = selected.lexically_relative(assetRoot);
    const bool contained = !rootError && !assetError && !relative.empty() && !relative.is_absolute() &&
        *relative.begin() != "..";
    if (!contained || !std::filesystem::is_regular_file(selected, assetError) || assetError)
        return {false, "Selected file is outside the project asset root or is not a regular file."};

    const auto reference = "assets/" + relative.generic_string();
    std::string validationError;
    if (!validate_asset_reference_assignment(document, reference, expectedAssetType, nullable, validationError))
        return {false, validationError};

    if (targets.size() == 1)
    {
        const auto changed = commands.set_property(document, targets.front(), reference);
        return {static_cast<bool>(changed), changed ? "Asset reference assigned." : changed.message};
    }

    std::vector<BatchPropertyEdit> edits;
    edits.reserve(targets.size());
    for (const auto &target : targets)
        edits.push_back({target, PropertyValue::of(reference)});
    const auto changed = commands.apply_batch(document, edits, {originSelection, originSelection});
    return {static_cast<bool>(changed), changed ? "Asset reference assigned." : changed.message};
}

void draw_multi_asset_reference_field(DocumentSession &document, EditCommandService &commands,
                                      const std::vector<PropertyAddress> &targets,
                                      const std::vector<Json> &values,
                                      const Selection &currentSelection,
                                      std::string_view expectedAssetType, bool nullable,
                                      const char *label,
                                      const AssetBrowseCallback &browse,
                                      Json clearValue,
                                      ResourceIndex *resourceIndex,
                                      const MultiAssetBrowseCallback &multiBrowse,
                                      ReferenceFieldOverride overrideInfo)
{
    if (targets.empty() || values.empty())
        return;

    const bool mixed = std::any_of(values.begin() + 1, values.end(),
                                   [&](const Json &value) { return value != values.front(); });
    const Json &frontValue = values.front();
    const auto info = inspect_asset_reference(document, frontValue, expectedAssetType, resourceIndex);
    const std::string assetType = expectedAssetType == "AnimationClip" || expectedAssetType == "Animation Clip"
        ? "Animation clip asset"
        : expectedAssetType.empty() || expectedAssetType == "Asset" ? "Asset"
        : std::string(expectedAssetType) + " asset";
    const std::string display = mixed
        ? "Multiple values"
        : (!info.path.empty() ? std::filesystem::path(info.path).filename().string() : "None");
    const std::string rawPath = !mixed && frontValue.is_string()
        ? frontValue.get<std::string>() : std::string{};
    const std::string validationKey = asset_reference_key(targets);
    ImGui::PushID(validationKey.c_str());

    auto commit_all = [&](const Json &next) {
        clear_asset_validation_error(validationKey);
        std::vector<BatchPropertyEdit> edits;
        edits.reserve(targets.size());
        for (const auto &target : targets)
            edits.push_back({target, PropertyValue::of(next)});
        commands.apply_batch(document, edits, {currentSelection, currentSelection});
    };

    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    ImGui::AlignTextToFramePadding();
    const float available = ImGui::GetContentRegionAvail().x;
    const bool hasLabel = label != nullptr && label[0] != '\0';
    const float labelWidth = hasLabel
        ? std::max(1.0f, std::min(124.0f * uiScale, available * 0.42f)) : 0.0f;
    const float frameHeight = ImGui::GetFrameHeight();
    if (hasLabel)
    {
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##multi-asset-ref-label", ImVec2(labelWidth, frameHeight));
        draw_clipped_property_label(ImGui::GetWindowDrawList(), cursorPos.x, cursorPos.y,
                                    labelWidth, frameHeight, label,
                                    ImGui::GetColorU32(overrideInfo.overridden
                                        ? ImGuiCol_NavHighlight : ImGuiCol_TextDisabled),
                                    ImGui::IsItemHovered());
        if (overrideInfo.overridden && ImGui::IsItemHovered())
            ImGui::SetTooltip("This field overrides its linked template value.");
        ImGui::SameLine();
    }
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
    if (overrideInfo.overridden)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.24f, 0.16f, 1.0f));
    const bool choose = ImGui::Button((display + "##asset-selector").c_str(),
                                      ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), 0.0f));
    if (overrideInfo.overridden)
        ImGui::PopStyleColor();
    ImGui::PopItemFlag();
    const auto referenceTooltip = mixed
        ? std::string("Multiple asset values.")
        : asset_reference_tooltip(rawPath, info);
    if (overrideInfo.overridden)
        ImGui::SetItemTooltip("%s\nThis field overrides its linked template value.", referenceTooltip.c_str());
    else
        ImGui::SetItemTooltip("%s", referenceTooltip.c_str());
    show_asset_validation_error(validationKey, document.valueRevision);

    if (ImGui::BeginPopupContextItem("MultiAssetReferenceContextMenu"))
    {
        if (ImGui::MenuItem("Copy Path", nullptr, false, !mixed && !rawPath.empty()))
            SDL_SetClipboardText(rawPath.c_str());
        if ((multiBrowse || browse) && ImGui::MenuItem("Browse..."))
        {
            if (multiBrowse)
                multiBrowse(targets, currentSelection, std::string(expectedAssetType), nullable);
            else
                browse(targets.front(), std::string(expectedAssetType), nullable);
        }
        if (nullable && ImGui::MenuItem("Clear", nullptr, false, mixed || !rawPath.empty()))
            commit_all(clearValue);
        if (overrideInfo.overridden && ImGui::MenuItem("Revert Property") && overrideInfo.revert)
            overrideInfo.revert();
        ImGui::EndPopup();
    }

    if (choose)
    {
        if (multiBrowse)
            multiBrowse(targets, currentSelection, std::string(expectedAssetType), nullable);
        else if (browse)
            browse(targets.front(), std::string(expectedAssetType), nullable);
        else if (resourceIndex != nullptr && !resourceIndex->asset_root().empty())
            ImGui::OpenPopup("MultiAssetPickerPopup");
    }

    if (ImGui::BeginPopup("MultiAssetPickerPopup"))
    {
        const auto &root = resourceIndex != nullptr ? resourceIndex->asset_root() : document.assetRoot;
        const std::string rootKey = root.generic_string();
        const std::uint64_t projectGen = resourceIndex != nullptr
            ? resourceIndex->project_generation().value
            : document.projectGeneration;
        const std::uint64_t epoch = resourceIndex != nullptr ? resourceIndex->uncertainty_epoch() : 0;
        auto &cache = picker_cache();
        bool dirRevisionsChanged = false;
        if (resourceIndex != nullptr)
        {
            for (const auto &[dir, rev] : cache.scannedDirRevisions)
            {
                if (resourceIndex->directory_revision(dir) != rev)
                {
                    dirRevisionsChanged = true;
                    break;
                }
            }
        }
        const bool needsScan = !cache.initialized ||
            cache.projectGeneration != projectGen ||
            cache.rootKey != rootKey ||
            cache.expectedAssetType != expectedAssetType ||
            cache.uncertaintyEpoch != epoch ||
            dirRevisionsChanged;
        if (needsScan)
        {
            ++picker_scan_counter();
            cache.projectGeneration = projectGen;
            cache.rootKey = rootKey;
            cache.expectedAssetType = std::string(expectedAssetType);
            cache.uncertaintyEpoch = epoch;
            cache.scannedDirRevisions.clear();
            cache.initialized = true;
            cache.truncated = false;
            cache.scanError.clear();
            cache.candidates.clear();
            if (resourceIndex != nullptr && !root.empty())
                cache.scannedDirRevisions.emplace_back(root, resourceIndex->directory_revision(root));

            constexpr std::size_t kMaxVisitedEntries = 2048;
            constexpr std::size_t kMaxCandidates = 256;
            std::error_code ec;
            if (!root.empty() && std::filesystem::exists(root, ec) && !ec)
            {
                std::filesystem::recursive_directory_iterator it(
                    root, std::filesystem::directory_options::skip_permission_denied, ec);
                std::filesystem::recursive_directory_iterator end;
                if (ec)
                {
                    cache.scanError = ec.message();
                }
                else
                {
                    std::size_t visited = 0;
                    while (it != end)
                    {
                        if (visited >= kMaxVisitedEntries || cache.candidates.size() >= kMaxCandidates)
                        {
                            cache.truncated = true;
                            break;
                        }
                        ++visited;
                        std::error_code entryEc;
                        if (it->is_directory(entryEc) && !entryEc)
                        {
                            if (resourceIndex != nullptr)
                                cache.scannedDirRevisions.emplace_back(
                                    it->path(), resourceIndex->directory_revision(it->path()));
                        }
                        else if (it->is_regular_file(entryEc) && !entryEc)
                        {
                            const auto rel = it->path().lexically_relative(root).generic_string();
                            const std::string candidate = "assets/" + rel;
                            std::string error;
                            if (validate_asset_reference_assignment(document, candidate, expectedAssetType, nullable, error))
                                cache.candidates.push_back(candidate);
                        }
                        it.increment(ec);
                        if (ec)
                        {
                            cache.scanError = ec.message();
                            break;
                        }
                    }
                    std::sort(cache.candidates.begin(), cache.candidates.end());
                }
            }
            else if (ec)
            {
                cache.scanError = ec.message();
            }
        }

        if (!cache.scanError.empty())
            ImGui::TextDisabled("Asset scan warning: %s", cache.scanError.c_str());
        if (cache.candidates.empty())
        {
            ImGui::TextDisabled("No matching %s candidates found.", assetType.c_str());
        }
        else
        {
            for (const auto &candidate : cache.candidates)
            {
                if (ImGui::Selectable(candidate.c_str(), !mixed && rawPath == candidate))
                {
                    std::string error;
                    const bool valid = validate_asset_reference_assignment(
                        document, candidate, expectedAssetType, nullable, error);
                    std::error_code existEc;
                    const bool existsOnDisk = root.empty() ||
                        (std::filesystem::is_regular_file(
                             root / std::filesystem::path(candidate).lexically_relative("assets"), existEc) &&
                         !existEc);
                    if (valid && existsOnDisk)
                    {
                        commit_all(candidate);
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        cache.scanError = !valid ? error
                            : ("Selected asset is no longer available: " + candidate);
                        cache.initialized = false;
                    }
                }
            }
            if (cache.truncated)
                ImGui::TextDisabled("Listing truncated at bounded scan limit; use Browse... for more.");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PLATFORMATOR_ASSET_PATH"))
        {
            if (payload->Data != nullptr && payload->DataSize > 0)
            {
                std::string dropped;
                if (payload->DataSize == sizeof(AssetDragPayload))
                {
                    const auto *drag = static_cast<const AssetDragPayload *>(payload->Data);
                    dropped = drag->relativeAssetPath;
                }
                else
                {
                    const auto *bytes = static_cast<const char *>(payload->Data);
                    dropped = bytes;
                }
                while (!dropped.empty() && dropped.back() == '\0')
                    dropped.pop_back();
                std::string error;
                if (validate_asset_reference_assignment(document, dropped, expectedAssetType, nullable, error))
                {
                    std::replace(dropped.begin(), dropped.end(), '\\', '/');
                    commit_all(dropped);
                }
                else
                {
                    remember_asset_validation_error(validationKey, document.valueRevision, error);
                    ImGui::SetTooltip("%s", error.c_str());
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PopID();
}
} // namespace platformator_editor
