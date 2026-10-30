#include "referencefields.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
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
        return object->value("name", std::string("GameObject")) + " (#" + std::to_string(id) + ")";
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
    const std::string ownerName = owner->value("name", std::string("GameObject"));
    return ownerName + " / " + std::string(target) + " (#" + std::to_string(id) + ")";
}

std::string lower_extension(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

bool extension_matches(std::string_view type, std::string_view extension)
{
    if (type == "Asset" || type.empty() || type == "unknown")
        return true;
    if (type == "Texture")
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
               extension == ".bmp" || extension == ".gif" || extension == ".webp" ||
               extension == ".tga" || extension == ".qoi" || extension == ".tif" || extension == ".tiff";
    if (type == "Audio")
        return extension == ".wav" || extension == ".ogg" || extension == ".mp3" ||
               extension == ".flac" || extension == ".opus";
    if (type == "AnimationClip")
        return extension == ".animset";
    return false;
}

const Json *property_owner(const DocumentSession &document, int objectId, int componentId,
                           std::size_t behaviorOrdinal)
{
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    const auto componentLocation = document.indexes.componentsById.find(componentId);
    if (objectLocation == document.indexes.objectsById.end() || componentLocation == document.indexes.componentsById.end() ||
        componentLocation->second.ownerObjectId != objectId)
        return nullptr;
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return nullptr;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        componentLocation->second.componentIndex >= components->size())
        return nullptr;
    const Json &component = (*components)[componentLocation->second.componentIndex];
    const auto behaviors = component.find("behaviors");
    if (behaviors == component.end() || !behaviors->is_array() || behaviorOrdinal >= behaviors->size())
        return nullptr;
    return &(*behaviors)[behaviorOrdinal];
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
                result.emplace_back(objectId, object->value("name", std::string("GameObject")) +
                                                 " (#" + std::to_string(objectId) + ")");
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
            result.emplace_back(*componentId, object->value("name", std::string("GameObject")) + " / " +
                                                  std::string(target) + " (#" + std::to_string(*componentId) + ")");
        }
    }
    return result;
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
            return {ReferenceState::Dangling, id, "Unresolved object ID " + std::to_string(*id)};
        const Json *object = resolve_object(document.root, location->second);
        return {ReferenceState::Resolved, id,
                object == nullptr ? "GameObject (#" + std::to_string(*id) + ")"
                                  : object->value("name", std::string("GameObject")) + " (#" + std::to_string(*id) + ")"};
    }
    const auto location = document.indexes.componentsById.find(*id);
    if (location == document.indexes.componentsById.end())
        return {ReferenceState::Dangling, id, "Unresolved component ID " + std::to_string(*id)};
    const auto ownerLocation = document.indexes.objectsById.find(location->second.ownerObjectId);
    const Json *owner = ownerLocation == document.indexes.objectsById.end()
                            ? nullptr
                            : resolve_object(document.root, ownerLocation->second);
    if (owner == nullptr)
        return {ReferenceState::Dangling, id, "Unresolved component ID " + std::to_string(*id)};
    const auto components = owner->find("components");
    if (components == owner->end() || !components->is_array() ||
        location->second.componentIndex >= components->size())
        return {ReferenceState::Dangling, id, "Unresolved component ID " + std::to_string(*id)};
    if (!component_matches((*components)[location->second.componentIndex], expectedTarget))
        return {ReferenceState::WrongKind, id, "Wrong component kind for ID " + std::to_string(*id)};
    return {ReferenceState::Resolved, id,
            owner->value("name", std::string("GameObject")) + " / " + std::string(expectedTarget) +
                " (#" + std::to_string(*id) + ")"};
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
        error = "References must be a file ID or null.";
        return false;
    }
    error = "The reference is invalid.";
    return false;
}

AssetReferenceInfo inspect_asset_reference(const DocumentSession &document, const Json &value,
                                           std::string_view expectedAssetType)
{
    if (value.is_null())
        return {ReferenceState::Unset, {}, {}};
    if (!value.is_string())
        return {ReferenceState::Invalid, {}, "Asset references must be a path string or null."};
    std::string raw = value.get<std::string>();
    if (raw.empty())
        return {ReferenceState::Invalid, {}, "Asset paths cannot be empty."};
    std::replace(raw.begin(), raw.end(), '\\', '/');
    const std::filesystem::path path(raw);
    if (path.is_absolute() || path.begin() == path.end() || *path.begin() != "assets")
        return {ReferenceState::Invalid, raw, "Asset paths must stay inside assets/...."};
    for (const auto &part : path)
        if (part == ".." || part == ".")
            return {ReferenceState::Invalid, raw, "Asset paths cannot contain traversal segments."};
    if (document.assetRoot.empty())
        return {ReferenceState::Invalid, raw, "The project asset root is unavailable."};
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
    if (!extension_matches(expectedAssetType, lower_extension(candidate)))
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
    const Json *current = read_path(property_owner(document, objectId, componentId, behaviorOrdinal), path);
    const Json defaultNull = nullptr;
    const auto info = inspect_scene_reference(document, current == nullptr ? defaultNull : *current, expectedTarget);
    if (info.state == ReferenceState::Dangling || info.state == ReferenceState::WrongKind ||
        info.state == ReferenceState::Invalid)
        ImGui::TextWrapped("%s: %s", label, info.label.c_str());
    const auto identity = info.state == ReferenceState::Unset ? "None" : info.label;
    if (ImGui::BeginCombo(label, identity.c_str()))
    {
        auto assign = [&](Json value) {
            std::string error;
            if (!validate_scene_reference_assignment(document, value, expectedTarget, nullable, error))
            {
                ImGui::SetTooltip("%s", error.c_str());
                return;
            }
            commands.set_property(document,
                                  PropertyAddress{document.documentGeneration, objectId, componentId,
                                                  behaviorOrdinal, path},
                                  std::move(value));
        };
        if (nullable && ImGui::Selectable("None", info.state == ReferenceState::Unset))
            assign(nullptr);
        for (const auto &[id, candidateLabel] : scene_candidates(document, expectedTarget))
        {
            const bool selected = info.fileId && *info.fileId == id && info.state == ReferenceState::Resolved;
            if (ImGui::Selectable(candidateLabel.c_str(), selected))
                assign(id);
        }
        ImGui::EndCombo();
    }
}

void draw_asset_reference_field(DocumentSession &document, EditCommandService &commands,
                                int objectId, int componentId, std::size_t behaviorOrdinal,
                                const std::vector<FieldPathSegment> &path,
                                std::string_view expectedAssetType, bool nullable, const char *label,
                                const AssetBrowseCallback &browse)
{
    const Json *current = read_path(property_owner(document, objectId, componentId, behaviorOrdinal), path);
    std::string value = current != nullptr && current->is_string() ? current->get<std::string>() : std::string{};
    if (value.size() >= 2048)
    {
        ImGui::TextDisabled("%s path is too long for inline editing.", label);
        return;
    }
    std::array<char, 2048> buffer{};
    std::memcpy(buffer.data(), value.data(), value.size());
    if (ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        std::string error;
        const std::string entered(buffer.data());
        if (validate_asset_reference_assignment(document, entered, expectedAssetType, nullable, error))
        {
            auto normalized = entered;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            commands.set_property(document,
                                  PropertyAddress{document.documentGeneration, objectId, componentId,
                                                  behaviorOrdinal, path},
                                  std::move(normalized));
        }
        else
            ImGui::TextWrapped("%s", error.c_str());
    }
    ImGui::SameLine();
    if (browse && ImGui::SmallButton((std::string("Browse...") + "##" + label).c_str()))
        browse(PropertyAddress{document.documentGeneration, objectId, componentId, behaviorOrdinal, path},
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
                        PropertyAddress{document.documentGeneration, objectId, componentId,
                                        behaviorOrdinal, path}, std::move(dropped));
                }
                else
                    ImGui::SetTooltip("%s", error.c_str());
            }
        }
        ImGui::EndDragDropTarget();
    }
    const Json currentValue = current == nullptr ? Json(nullptr) : *current;
    const auto info = inspect_asset_reference(document, currentValue, expectedAssetType);
    if (info.state == ReferenceState::Dangling || info.state == ReferenceState::WrongKind ||
        info.state == ReferenceState::Invalid)
        ImGui::TextWrapped("Unresolved asset: %s", info.message.c_str());
    ImGui::SameLine();
    if (nullable && ImGui::SmallButton("Clear"))
        commands.set_property(document,
                              PropertyAddress{document.documentGeneration, objectId, componentId,
                                              behaviorOrdinal, path},
                              nullptr);
}
} // namespace platformator_editor
