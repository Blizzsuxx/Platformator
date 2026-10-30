#include "inspector.h"

#include "../componenttypes.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

namespace platformator_editor
{
namespace
{
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

const Json *address_owner(const DocumentSession &document, int objectId,
                          std::optional<int> componentId)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return nullptr;
    const Json *object = resolve_object(document.root, location->second);
    if (object == nullptr || !componentId)
        return object;
    const auto componentLocation = document.indexes.componentsById.find(*componentId);
    if (componentLocation == document.indexes.componentsById.end() ||
        componentLocation->second.ownerObjectId != objectId)
        return nullptr;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        componentLocation->second.componentIndex >= components->size())
        return nullptr;
    return &(*components)[componentLocation->second.componentIndex];
}

const Json *address_value(const DocumentSession &document, int objectId,
                          std::optional<int> componentId,
                          const std::vector<FieldPathSegment> &path)
{
    const Json *value = address_owner(document, objectId, componentId);
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

int json_integer(const Json *value, int fallback)
{
    if (value == nullptr || (!value->is_number_integer() && !value->is_number_unsigned()))
        return fallback;
    try
    {
        const auto parsed = value->get<std::int64_t>();
        if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
            return fallback;
        return static_cast<int>(parsed);
    }
    catch (...)
    {
        return fallback;
    }
}

std::optional<int> script_component_id(const DocumentSession &document, int objectId)
{
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (objectLocation == document.indexes.objectsById.end())
        return std::nullopt;
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return std::nullopt;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array())
        return std::nullopt;
    for (const auto &component : *components)
    {
        if (!component.is_object() || json_integer(component.contains("type") ? &component["type"] : nullptr, -1) != 5)
            continue;
        const int id = json_integer(component.contains("id") ? &component["id"] : nullptr, -1);
        if (id >= 0)
            return id;
    }
    return std::nullopt;
}

float json_float(const Json *value, float fallback)
{
    if (value == nullptr || !value->is_number())
        return fallback;
    try
    {
        return value->get<float>();
    }
    catch (...)
    {
        return fallback;
    }
}

std::string stable_field_id(const std::vector<FieldPathSegment> &path)
{
    std::string result;
    for (const auto &segment : path)
    {
        result.push_back('/');
        if (const auto *key = std::get_if<std::string>(&segment))
            result += *key;
        else
            result += std::to_string(std::get<std::size_t>(segment));
    }
    return result;
}

void begin_field(const char *label, const std::vector<FieldPathSegment> &path)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(ImGui::GetCursorPosX() + std::min(124.0f, available * 0.42f));
    ImGui::PushID(stable_field_id(path).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void end_field()
{
    ImGui::PopID();
}

} // namespace

const std::string &InspectorPanel::feedback() const noexcept
{
    return feedback_;
}

bool InspectorPanel::set(DocumentSession &document, EditCommandService &commands, int objectId,
                         std::optional<int> componentId, std::vector<FieldPathSegment> path, Json value)
{
    PropertyAddress address{document.documentGeneration, objectId, componentId, std::nullopt, std::move(path)};
    const auto result = commands.set_property(document, address, std::move(value));
    if (result.status == CommandStatus::Rejected || result.status == CommandStatus::StaleAddress)
    {
        feedback_ = result.message;
        return false;
    }
    feedback_.clear();
    return result.status == CommandStatus::Changed;
}

bool InspectorPanel::draw_boolean(DocumentSession &document, EditCommandService &commands, int objectId,
                                  std::optional<int> componentId, std::vector<FieldPathSegment> path,
                                  const char *label, bool defaultValue)
{
    const Json *stored = address_value(document, objectId, componentId, path);
    bool value = stored != nullptr && stored->is_boolean() ? stored->get<bool>() : defaultValue;
    begin_field(label, path);
    const bool changed = ImGui::Checkbox("##value", &value);
    end_field();
    if (changed)
        return set(document, commands, objectId, componentId, std::move(path), value);
    return false;
}

bool InspectorPanel::draw_number(DocumentSession &document, EditCommandService &commands, int objectId,
                                 std::optional<int> componentId, std::vector<FieldPathSegment> path,
                                 const char *label, float defaultValue,
                                 std::optional<float> minimum, std::optional<float> maximum)
{
    const Json *stored = address_value(document, objectId, componentId, path);
    float value = json_float(stored, defaultValue);
    begin_field(label, path);
    const bool changed = ImGui::InputFloat("##value", &value, 0.0f, 0.0f, "%.2f");
    end_field();
    if (!changed)
        return false;
    if (!std::isfinite(value) || (minimum && value < *minimum) || (maximum && value > *maximum))
    {
        feedback_ = std::string(label) + " is outside the supported range.";
        return false;
    }
    return set(document, commands, objectId, componentId, std::move(path), value);
}

bool InspectorPanel::draw_integer(DocumentSession &document, EditCommandService &commands, int objectId,
                                  std::optional<int> componentId, std::vector<FieldPathSegment> path,
                                  const char *label, int defaultValue,
                                  std::optional<int> minimum, std::optional<int> maximum)
{
    const Json *stored = address_value(document, objectId, componentId, path);
    int value = json_integer(stored, defaultValue);
    begin_field(label, path);
    const bool changed = ImGui::InputInt("##value", &value, 1, 100);
    end_field();
    if (!changed)
        return false;
    if ((minimum && value < *minimum) || (maximum && value > *maximum))
    {
        feedback_ = std::string(label) + " is outside the supported range.";
        return false;
    }
    return set(document, commands, objectId, componentId, std::move(path), value);
}

bool InspectorPanel::draw_string(DocumentSession &document, EditCommandService &commands, int objectId,
                                 std::optional<int> componentId, std::vector<FieldPathSegment> path,
                                 const char *label, std::string defaultValue)
{
    const Json *stored = address_value(document, objectId, componentId, path);
    std::string value = stored != nullptr && stored->is_string() ? stored->get<std::string>() : std::move(defaultValue);
    if (value.size() >= 4096)
    {
        ImGui::TextDisabled("%s is too long for the inline editor.", label);
        return false;
    }
    std::array<char, 4096> buffer{};
    std::memcpy(buffer.data(), value.data(), value.size());
    begin_field(label, path);
    const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    const bool commit = changed || ImGui::IsItemDeactivatedAfterEdit();
    end_field();
    if (!commit)
        return false;
    return set(document, commands, objectId, componentId, std::move(path), std::string(buffer.data()));
}

bool InspectorPanel::draw_enum(DocumentSession &document, EditCommandService &commands, int objectId,
                               std::optional<int> componentId, std::vector<FieldPathSegment> path,
                               const char *label, int defaultValue,
                               const std::vector<std::pair<int, const char *>> &values)
{
    const int current = json_integer(address_value(document, objectId, componentId, path), defaultValue);
    const char *preview = "Unsupported";
    for (const auto &[value, name] : values)
        if (value == current)
            preview = name;
    begin_field(label, path);
    bool changed = false;
    if (ImGui::BeginCombo("##value", preview))
    {
        for (const auto &[value, name] : values)
        {
            const bool selected = value == current;
            if (ImGui::Selectable(name, selected))
            {
                changed = true;
                set(document, commands, objectId, componentId, path, value);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    end_field();
    return changed;
}

void InspectorPanel::draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands,
                          const BehaviorCatalog &catalog, const AssetBrowseCallback &browse)
{
    selection.repair(document);
    if (!selection.current().objectId)
    {
        ImGui::TextDisabled("Select an object to edit its properties.");
        return;
    }
    const int objectId = *selection.current().objectId;
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (objectLocation == document.indexes.objectsById.end())
    {
        selection.repair(document);
        return;
    }
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return;

    const auto attach_behavior = [&](const std::string &behaviorType) {
        if (behaviorType.empty())
            return;
        const Selection before = selection.current();
        auto scriptId = script_component_id(document, objectId);
        std::size_t ordinal = 0;
        StructuralCommandResult result;
        if (scriptId)
        {
            const auto objectLocation = document.indexes.objectsById.find(objectId);
            const auto scriptLocation = document.indexes.componentsById.find(*scriptId);
            if (objectLocation == document.indexes.objectsById.end() ||
                scriptLocation == document.indexes.componentsById.end())
            {
                feedback_ = "The selected object changed before the behavior could be attached.";
                return;
            }
            const Json *currentObject = resolve_object(document.root, objectLocation->second);
            if (currentObject != nullptr)
            {
                const auto components = currentObject->find("components");
                if (components != currentObject->end() && components->is_array() &&
                    scriptLocation->second.componentIndex < components->size())
                {
                    const auto &script = (*components)[scriptLocation->second.componentIndex];
                    const auto behaviors = script.find("behaviors");
                    if (behaviors != script.end() && behaviors->is_array())
                        ordinal = behaviors->size();
                }
            }
            result = commands.add_behavior(document, objectId, *scriptId,
                                           Json{{"type", behaviorType}});
        }
        else
        {
            Json script = default_component_record(5);
            script["behaviors"].push_back(Json{{"type", behaviorType}});
            result = commands.add_component(document, objectId, std::move(script));
            if (result.status == CommandStatus::Changed && result.command)
                scriptId = result.command->payload.value("id", 0);
        }
        if (result.status == CommandStatus::Changed && scriptId)
        {
            selection.set(document, Selection{objectId, *scriptId, ordinal,
                                              FocusedPanel::Inspector});
            commands.set_last_selection_state({before, selection.current()});
            feedback_.clear();
        }
        else
        {
            feedback_ = result.message.empty() ? "Could not attach this behavior to the object."
                                               : result.message;
        }
    };

    const ImVec2 windowPosition = ImGui::GetWindowPos();
    const ImVec2 contentMinimum = ImGui::GetCursorScreenPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const ImRect dropBounds(contentMinimum,
                            ImVec2(windowPosition.x + windowSize.x - padding.x,
                                   windowPosition.y + windowSize.y - padding.y));
    if (ImGui::BeginDragDropTargetCustom(dropBounds, ImGui::GetID("##behavior-drop-target")))
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PLATFORMATOR_BEHAVIOR_TYPE"))
        {
            if (payload->IsDelivery() && payload->Data != nullptr && payload->DataSize > 1 &&
                payload->DataSize <= 256)
            {
                const auto *bytes = static_cast<const char *>(payload->Data);
                const std::size_t textSize = payload->DataSize - (bytes[payload->DataSize - 1] == '\0' ? 1U : 0U);
                std::string behaviorType(bytes, bytes + textSize);
                const bool validType = !behaviorType.empty() &&
                    (std::isalpha(static_cast<unsigned char>(behaviorType.front())) || behaviorType.front() == '_') &&
                    std::all_of(behaviorType.begin() + 1, behaviorType.end(), [](unsigned char character) {
                        return std::isalnum(character) || character == '_';
                    });
                if (validType)
                    attach_behavior(behaviorType);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Text("Object %d", objectId);
    const bool gameObjectExpanded = ImGui::CollapsingHeader(
        "GameObject", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SetItemTooltip("Drop a behavior header anywhere in the Inspector to add it to this GameObject.");
    const auto refreshedObjectLocation = document.indexes.objectsById.find(objectId);
    object = refreshedObjectLocation == document.indexes.objectsById.end()
        ? nullptr : resolve_object(document.root, refreshedObjectLocation->second);
    if (object == nullptr)
        return;
    if (gameObjectExpanded)
    {
        draw_string(document, commands, objectId, std::nullopt, {std::string("name")}, "Name");
        draw_string(document, commands, objectId, std::nullopt, {std::string("tag")}, "Tag");
        draw_boolean(document, commands, objectId, std::nullopt, {std::string("active")}, "Active", true);
        ImGui::SeparatorText("Position");
        draw_number(document, commands, objectId, std::nullopt,
                    {std::string("position"), std::string("x")}, "X");
        draw_number(document, commands, objectId, std::nullopt,
                    {std::string("position"), std::string("y")}, "Y");
        draw_number(document, commands, objectId, std::nullopt,
                    {std::string("scale"), std::string("x")}, "Scale X", 1.0f,
                    std::numeric_limits<float>::min());
        draw_number(document, commands, objectId, std::nullopt,
                    {std::string("scale"), std::string("y")}, "Scale Y", 1.0f,
                    std::numeric_limits<float>::min());
        draw_number(document, commands, objectId, std::nullopt,
                    {std::string("rotation")}, "Rotation");
    }

    const auto objectComponents = object->find("components");
    std::vector<int> visibleComponentIds;
    if (selection.current().componentId)
    {
        const auto componentLocation = document.indexes.componentsById.find(*selection.current().componentId);
        if (componentLocation != document.indexes.componentsById.end() &&
            componentLocation->second.ownerObjectId == objectId)
            visibleComponentIds.push_back(*selection.current().componentId);
    }
    else if (objectComponents != object->end() && objectComponents->is_array())
    {
        visibleComponentIds.reserve(objectComponents->size());
        for (const auto &candidate : *objectComponents)
        {
            const int candidateId = json_integer(candidate.is_object() && candidate.contains("id")
                                                     ? &candidate.at("id") : nullptr, -1);
            if (candidateId >= 0)
                visibleComponentIds.push_back(candidateId);
        }
    }

    for (const int visibleComponentId : visibleComponentIds)
    {
        const auto componentLocation = document.indexes.componentsById.find(visibleComponentId);
        if (componentLocation == document.indexes.componentsById.end() ||
            componentLocation->second.ownerObjectId != objectId)
            continue;
        const auto currentObjectLocation = document.indexes.objectsById.find(objectId);
        const Json *currentObject = currentObjectLocation == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, currentObjectLocation->second);
        if (currentObject == nullptr)
            continue;
        const auto components = currentObject->find("components");
        if (components == currentObject->end() || !components->is_array() ||
            componentLocation->second.componentIndex >= components->size())
            continue;
        const Json *component = &(*components)[componentLocation->second.componentIndex];
        const std::optional<int> componentId = visibleComponentId;
        ImGui::Separator();
        const int type = json_integer(component->contains("type") ? &component->at("type") : nullptr, -1);
        ImGui::PushID(*componentId);
        const std::string componentHeader = std::string(component_label(type)) + "###component." +
                                            std::to_string(*componentId);
        if (!ImGui::CollapsingHeader(componentHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            component = nullptr;
        if (component != nullptr)
        {
        if (type == 2)
        {
            ImGui::SeparatorText("Camera");
            const auto legacy = component->find("camera");
            const bool hasLegacy = legacy != component->end() && legacy->is_object();
            const bool hasWidth = component->contains("width");
            const bool hasHeight = component->contains("height");
            if (hasWidth || !hasLegacy)
                draw_number(document, commands, objectId, componentId, {std::string("width")}, "Width", 640.0f,
                            std::numeric_limits<float>::min());
            else
                draw_number(document, commands, objectId, componentId,
                            {std::string("camera"), std::string("w")}, "Width", 640.0f,
                            std::numeric_limits<float>::min());
            if (hasHeight || !hasLegacy)
                draw_number(document, commands, objectId, componentId, {std::string("height")}, "Height", 480.0f,
                            std::numeric_limits<float>::min());
            else
                draw_number(document, commands, objectId, componentId,
                            {std::string("camera"), std::string("h")}, "Height", 480.0f,
                            std::numeric_limits<float>::min());
            if (hasLegacy && hasWidth && legacy->contains("w"))
                draw_number(document, commands, objectId, componentId,
                            {std::string("camera"), std::string("w")}, "Legacy width", 640.0f,
                            std::numeric_limits<float>::min());
            if (hasLegacy && hasHeight && legacy->contains("h"))
                draw_number(document, commands, objectId, componentId,
                            {std::string("camera"), std::string("h")}, "Legacy height", 480.0f,
                            std::numeric_limits<float>::min());
        }
        else if (type == 3)
        {
            ImGui::SeparatorText("Collider");
            draw_enum(document, commands, objectId, componentId, {std::string("colliderType")}, "Shape", 1,
                      {{0, "Circle"}, {1, "Box"}});
            const int shape = json_integer(address_value(document, objectId, componentId,
                                                         {std::string("colliderType")}), -1);
            if (shape == 0)
                draw_number(document, commands, objectId, componentId, {std::string("radius")}, "Radius", 16.0f,
                            std::numeric_limits<float>::min());
            else if (shape == 1)
            {
                draw_number(document, commands, objectId, componentId, {std::string("width")}, "Width", 16.0f,
                            std::numeric_limits<float>::min());
                draw_number(document, commands, objectId, componentId, {std::string("height")}, "Height", 16.0f,
                            std::numeric_limits<float>::min());
            }
            draw_number(document, commands, objectId, componentId,
                        {std::string("offset"), std::string("x")}, "Offset X");
            draw_number(document, commands, objectId, componentId,
                        {std::string("offset"), std::string("y")}, "Offset Y");
            draw_boolean(document, commands, objectId, componentId, {std::string("trigger")}, "Trigger");
            draw_integer(document, commands, objectId, componentId,
                         {std::string("collisionGroup")}, "Collision Group", 1, 0);
            draw_integer(document, commands, objectId, componentId,
                         {std::string("collisionMask")}, "Collision Mask", 1, 0);
        }
        else if (type == 4)
        {
            ImGui::SeparatorText("Rigidbody");
            draw_number(document, commands, objectId, componentId,
                        {std::string("velocity"), std::string("x")}, "Velocity X");
            draw_number(document, commands, objectId, componentId,
                        {std::string("velocity"), std::string("y")}, "Velocity Y");
            draw_number(document, commands, objectId, componentId,
                        {std::string("force"), std::string("x")}, "Force X");
            draw_number(document, commands, objectId, componentId,
                        {std::string("force"), std::string("y")}, "Force Y");
            const int bodyType = json_integer(address_value(document, objectId, componentId,
                                                            {std::string("bodyType")}), 0);
            draw_number(document, commands, objectId, componentId, {std::string("mass")}, "Mass", 1.0f,
                        bodyType == 0 ? std::optional<float>(std::numeric_limits<float>::min())
                                      : std::optional<float>(0.0f));
            draw_number(document, commands, objectId, componentId,
                        {std::string("angularVelocity")}, "Angular Velocity");
            draw_number(document, commands, objectId, componentId, {std::string("torque")}, "Torque");
            draw_number(document, commands, objectId, componentId, {std::string("friction")}, "Friction", 0.5f, 0.0f);
            draw_number(document, commands, objectId, componentId, {std::string("restitution")}, "Restitution", 0.0f, 0.0f, 1.0f);
            draw_enum(document, commands, objectId, componentId, {std::string("bodyType")}, "Body Type", 0,
                      {{0, "Dynamic"}, {1, "Static"}, {2, "Kinematic"}});
            draw_boolean(document, commands, objectId, componentId, {std::string("gravity")}, "Gravity", true);
        }
        else if (type == 6)
        {
            ImGui::SeparatorText("Sprite");
            draw_asset_reference_field(document, commands, objectId, componentId, std::nullopt,
                                       {std::string("textureFilePath")}, "Texture", true,
                                       "Texture", browse, std::string{});
            draw_enum(document, commands, objectId, componentId, {std::string("flip")}, "Flip", 0,
                      {{0, "None"}, {1, "Horizontal"}, {2, "Vertical"}, {3, "Horizontal + Vertical"}});
            draw_number(document, commands, objectId, componentId, {std::string("width")}, "Width", 16.0f,
                        std::numeric_limits<float>::min());
            draw_number(document, commands, objectId, componentId, {std::string("height")}, "Height", 16.0f,
                        std::numeric_limits<float>::min());
            draw_boolean(document, commands, objectId, componentId,
                         {std::string("sourceRectEnabled")}, "Use Source Rectangle");
            const Json *rectangle = address_value(document, objectId, componentId, {std::string("sourceRect")});
            if (rectangle != nullptr && rectangle->is_object())
            {
                draw_number(document, commands, objectId, componentId,
                            {std::string("sourceRect"), std::string("x")}, "Source X");
                draw_number(document, commands, objectId, componentId,
                            {std::string("sourceRect"), std::string("y")}, "Source Y");
                draw_number(document, commands, objectId, componentId,
                            {std::string("sourceRect"), std::string("w")}, "Source Width");
                draw_number(document, commands, objectId, componentId,
                            {std::string("sourceRect"), std::string("h")}, "Source Height");
            }
            else if (ImGui::Button("Initialize Source Rectangle"))
                set(document, commands, objectId, componentId, {std::string("sourceRect")},
                    Json{{"x", 0.0}, {"y", 0.0}, {"w", 16.0}, {"h", 16.0}});
        }
        else if (type == 0)
        {
            ImGui::SeparatorText("Animator");
            draw_integer(document, commands, objectId, componentId,
                         {std::string("currentFrameIndex")}, "Current Frame", 0, 0);
            draw_number(document, commands, objectId, componentId,
                        {std::string("playbackSpeed")}, "Playback Speed", 1.0f);
            draw_boolean(document, commands, objectId, componentId, {std::string("playing")}, "Playing");
            draw_asset_reference_field(document, commands, objectId, componentId, std::nullopt,
                                       {std::string("animationClipFilePath")}, "AnimationClip", true,
                                       "Animation Clip", browse, std::string{});
        }
        else if (type == 1)
        {
            ImGui::SeparatorText("Audio");
            draw_asset_reference_field(document, commands, objectId, componentId, std::nullopt,
                                       {std::string("filePath")}, "Audio", true,
                                       "Audio File", browse, std::string{});
            draw_number(document, commands, objectId, componentId, {std::string("gain")}, "Gain", 1.0f);
            draw_integer(document, commands, objectId, componentId, {std::string("loopCount")}, "Loop Count", 0);
            draw_boolean(document, commands, objectId, componentId, {std::string("autoPlay")}, "Auto Play");
        }
        else if (type == 5)
        {
            ImGui::SeparatorText("Behavior Fields");
            if (selection.current().componentId == componentId)
                behaviorFields_.draw(document, selection, commands, catalog, browse);
            else
            {
                SelectionModel scriptSelection;
                scriptSelection.set(document, Selection{objectId, componentId, std::nullopt,
                                                        FocusedPanel::Inspector});
                behaviorFields_.draw(document, scriptSelection, commands, catalog, browse);
            }
        }
        else
        {
            ImGui::SeparatorText("Unsupported Component");
            const std::string raw = component->dump(2);
            ImGui::TextWrapped("%s", raw.c_str());
        }
        }
        ImGui::PopID();
    }

    if (ImGui::Button("Add Component"))
        ImGui::OpenPopup("Add Component Menu");
    if (ImGui::BeginPopup("Add Component Menu"))
    {
        for (const auto &[type, label] : kComponentChoices)
        {
            const auto currentObjectLocation = document.indexes.objectsById.find(objectId);
            const Json *currentObject = currentObjectLocation == document.indexes.objectsById.end()
                ? nullptr : resolve_object(document.root, currentObjectLocation->second);
            if (currentObject == nullptr || has_component_type(*currentObject, type))
                continue;
            if (ImGui::MenuItem(label))
            {
                const Selection before = selection.current();
                auto result = commands.add_component(document, objectId, default_component_record(type));
                if (result.status == CommandStatus::Changed && result.command)
                {
                    const int newComponentId = result.command->payload.value("id", 0);
                    selection.set(document, Selection{objectId, newComponentId, std::nullopt,
                                                       selection.current().focusedPanel});
                    commands.set_last_selection_state({before, selection.current()});
                }
                else
                    feedback_ = result.message;
                break;
            }
        }
        ImGui::EndPopup();
    }
    if (!feedback_.empty())
        ImGui::TextWrapped("%s", feedback_.c_str());
}
} // namespace platformator_editor
