#include "referencefields.h"

#include "../assetformats.h"
#include "../resourceindex.h"
#include "../discovery.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <imgui.h>

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
                         std::string_view target)
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
                                               location->second.ownerObjectId, "GameObject");
    std::string label = ownerName.empty() ? std::string(target) : ownerName + " / " + std::string(target);
    const Json &component = (*components)[location->second.componentIndex];
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

bool draw_template_reference_kind_control(int objectId, int componentId,
                                          std::size_t behaviorOrdinal,
                                          std::string_view controlId,
                                          std::optional<std::string> &declaredKind)
{
    const std::string label(controlId);
    ImGui::PushID(objectId);
    ImGui::PushID(componentId);
    ImGui::PushID(static_cast<int>(behaviorOrdinal));
    ImGui::PushID(label.c_str());
    const char *preview = declaredKind
        ? (declaredKind->compare("Component") == 0 ? "Component" : "GameObject")
        : "Not declared";
    bool changed = false;
    if (ImGui::BeginCombo(("Reference kind##" + label).c_str(), preview))
    {
        if (ImGui::Selectable("Not a reference", !declaredKind))
        {
            declaredKind.reset();
            changed = true;
        }
        if (ImGui::Selectable("GameObject", declaredKind == "GameObject"))
        {
            declaredKind = "GameObject";
            changed = true;
        }
        if (ImGui::Selectable("Component", declaredKind == "Component"))
        {
            declaredKind = "Component";
            changed = true;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Declare the meaning of this opaque integer for safe template remapping.");
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    return changed;
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

void draw_scene_reference_field(DocumentSession &document, EditCommandService &commands,
                                int objectId, int componentId, std::size_t behaviorOrdinal,
                                const std::vector<FieldPathSegment> &path,
                                std::string_view expectedTarget, bool nullable, const char *label)
{
    push_field_id(objectId, componentId, behaviorOrdinal, path);
    const Json *current = read_path(property_owner(document, objectId, componentId, behaviorOrdinal), path);
    const Json defaultNull = nullptr;
    const auto info = inspect_scene_reference(document, current == nullptr ? defaultNull : *current, expectedTarget);
    if (info.state == ReferenceState::Dangling || info.state == ReferenceState::WrongKind ||
        info.state == ReferenceState::Invalid)
        ImGui::TextWrapped("%s: %s", label, info.label.c_str());
    const std::string targetType = expectedTarget == "GameObject"
        ? "GameObject" : std::string(expectedTarget) + " component";
    const std::string identity = info.state == ReferenceState::Unset
        ? "Select " + targetType + "..."
        : info.state == ReferenceState::Resolved
            ? (info.label.empty() ? targetType + " (selected)" : targetType + ": " + info.label)
            : "Missing " + targetType;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(ImGui::GetCursorPosX() + std::min(124.0f, available * 0.42f));
    ImGui::SetNextItemWidth(std::max(50.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##reference-selector", identity.c_str()))
    {
        auto filterBuffer = load_scene_reference_search_buffer();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##reference-selector-search", "Search name, type, or ID",
                                 filterBuffer.data(), filterBuffer.size());
        save_scene_reference_search_buffer(filterBuffer);
        const std::string_view filter(filterBuffer.data());
        ImGui::Separator();
        auto assign = [&](Json value) {
            std::string error;
            if (!validate_scene_reference_assignment(document, value, expectedTarget, nullable, error))
            {
                ImGui::SetTooltip("%s", error.c_str());
                return;
            }
            commands.set_property(document,
                                  address_for_document(document, objectId, componentId,
                                                       behaviorOrdinal, path),
                                  std::move(value));
        };
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
            if (ImGui::Selectable(candidateLabel.c_str(), selected))
                assign(id);
        }
        ImGui::EndCombo();
    }
    pop_field_id(path);
}

void draw_asset_reference_field(DocumentSession &document, EditCommandService &commands,
                                int objectId, std::optional<int> componentId,
                                std::optional<std::size_t> behaviorOrdinal,
                                const std::vector<FieldPathSegment> &path,
                                std::string_view expectedAssetType, bool nullable, const char *label,
                                const AssetBrowseCallback &browse, Json clearValue,
                                ResourceIndex *resourceIndex)
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
    const std::string assetType = expectedAssetType == "AnimationClip" ? "Animation clip asset"
        : expectedAssetType.empty() || expectedAssetType == "Asset" ? "Asset"
        : std::string(expectedAssetType) + " asset";
    const std::string display = info.state == ReferenceState::Resolved
        ? assetType + ": " + std::filesystem::path(info.path).filename().string()
        : info.state == ReferenceState::Unset ? "Select " + assetType + "..."
                                              : "Missing " + assetType;
    const std::string rawPath = currentValue.is_string()
        ? currentValue.get<std::string>() : std::string{};
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(ImGui::GetCursorPosX() + std::min(124.0f, available * 0.42f));
    const float clearWidth = nullable ? 46.0f + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    const bool choose = ImGui::Button((display + "##asset-selector").c_str(),
                                      {std::max(50.0f, ImGui::GetContentRegionAvail().x - clearWidth), 0.0f});
    ImGui::SetItemTooltip("Full path: %s", rawPath.empty() ? "(unset)" : rawPath.c_str());
    if (choose && browse)
        browse(address_for_document(document, objectId, componentId, behaviorOrdinal, path),
               std::string(expectedAssetType), nullable);
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PLATFORMATOR_ASSET_PATH"))
        {
            if (payload->Data != nullptr && payload->DataSize > 0 && payload->DataSize <= 2048)
            {
                const auto *bytes = static_cast<const char *>(payload->Data);
                std::string dropped(bytes, bytes + payload->DataSize);
                if (!dropped.empty() && dropped.back() == '\0')
                    dropped.pop_back();
                std::string error;
                if (validate_asset_reference_assignment(document, dropped, expectedAssetType, nullable, error))
                {
                    std::replace(dropped.begin(), dropped.end(), '\\', '/');
                    commands.set_property(document,
                        address_for_document(document, objectId, componentId,
                                             behaviorOrdinal, path), std::move(dropped));
                }
                else
                    ImGui::SetTooltip("%s", error.c_str());
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (info.state == ReferenceState::Dangling || info.state == ReferenceState::WrongKind ||
        info.state == ReferenceState::Invalid)
        ImGui::TextWrapped("Unresolved asset: %s", info.message.c_str());
    if (ImGui::TreeNode("Edit asset path"))
    {
        ImGui::TextWrapped("Assigned path: %s", rawPath.empty() ? "(unset)" : rawPath.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(rawPath.empty());
        if (ImGui::SmallButton("Copy Path"))
            SDL_SetClipboardText(rawPath.c_str());
        ImGui::EndDisabled();
        std::array<char, 2048> buffer{};
        const auto copied = std::min(rawPath.size(), buffer.size() - 1);
        std::memcpy(buffer.data(), rawPath.data(), copied);
        const bool changed = ImGui::InputText("##asset-path", buffer.data(), buffer.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        const bool commit = changed || ImGui::IsItemDeactivatedAfterEdit();
        if (commit)
        {
            std::string candidate(buffer.data());
            Json assigned = candidate.empty() && nullable ? clearValue : Json(candidate);
            std::string error;
            if (validate_asset_reference_assignment(document, assigned, expectedAssetType, nullable, error))
            {
                if (!candidate.empty())
                    std::replace(candidate.begin(), candidate.end(), '\\', '/');
                commands.set_property(document,
                    address_for_document(document, objectId, componentId, behaviorOrdinal, path),
                    candidate.empty() && nullable ? clearValue : Json(std::move(candidate)));
            }
            else
                ImGui::TextWrapped("Path was not assigned: %s", error.c_str());
        }
        ImGui::TreePop();
    }
    if (nullable)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("Clear##") + label).c_str()))
            commands.set_property(document,
                                  address_for_document(document, objectId, componentId,
                                                       behaviorOrdinal, path),
                                  clearValue);
    }
    pop_field_id(path);
}
} // namespace platformator_editor
