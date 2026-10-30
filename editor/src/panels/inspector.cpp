#include "inspector.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <imgui.h>

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

const char *component_label(int type)
{
    switch (type)
    {
    case 0:
        return "Animator";
    case 1:
        return "Audio";
    case 2:
        return "Camera";
    case 3:
        return "Collider";
    case 4:
        return "Rigidbody";
    case 5:
        return "Script";
    case 6:
        return "Sprite";
    default:
        return "Unknown Component";
    }
}

bool has_component_type(const Json &object, int type)
{
    const auto components = object.find("components");
    if (components == object.end() || !components->is_array())
        return false;
    return std::any_of(components->begin(), components->end(), [type](const Json &component) {
        return component.is_object() && json_integer(component.contains("type") ? &component["type"] : nullptr, -1) == type;
    });
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

Json default_component(int type)
{
    switch (type)
    {
    case 0:
        return Json{{"type", 0}, {"currentFrameIndex", 0}, {"playbackSpeed", 1.0},
                    {"playing", false}, {"animationClipFilePath", ""}};
    case 1:
        return Json{{"type", 1}, {"filePath", ""}, {"gain", 1.0}, {"loopCount", 0}, {"autoPlay", false}};
    case 2:
        return Json{{"type", 2}, {"width", 640.0}, {"height", 480.0}};
    case 3:
        return Json{{"type", 3}, {"colliderType", 1}, {"width", 16.0}, {"height", 16.0},
                    {"offset", {{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
                    {"collisionGroup", 1}, {"collisionMask", 1}};
    case 4:
        return Json{{"type", 4}, {"velocity", {{"x", 0.0}, {"y", 0.0}}},
                    {"force", {{"x", 0.0}, {"y", 0.0}}}, {"mass", 1.0},
                    {"angularVelocity", 0.0}, {"torque", 0.0}, {"friction", 0.5},
                    {"restitution", 0.0}, {"bodyType", 0}, {"gravity", true}};
    case 5:
        return Json{{"type", 5}, {"behaviors", Json::array()}};
    case 6:
        return Json{{"type", 6}, {"textureFilePath", ""}, {"flip", 0},
                    {"width", 16.0}, {"height", 16.0}, {"sourceRectEnabled", false}};
    default:
        return Json::object();
    }
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
    const bool changed = ImGui::InputFloat("##value", &value, 0.0f, 0.0f, "%.3f",
                                           ImGuiInputTextFlags_EnterReturnsTrue);
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
    const bool changed = ImGui::InputInt("##value", &value, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue);
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
    const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    end_field();
    if (!changed)
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

void InspectorPanel::draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands)
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

    ImGui::Text("Object %d", objectId);
    if (ImGui::CollapsingHeader("GameObject", ImGuiTreeNodeFlags_DefaultOpen))
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

    const Json *component = nullptr;
    std::optional<int> componentId = selection.current().componentId;
    if (componentId)
    {
        const auto componentLocation = document.indexes.componentsById.find(*componentId);
        if (componentLocation != document.indexes.componentsById.end() &&
            componentLocation->second.ownerObjectId == objectId)
        {
            const auto components = object->find("components");
            if (components != object->end() && components->is_array() &&
                componentLocation->second.componentIndex < components->size())
                component = &(*components)[componentLocation->second.componentIndex];
        }
    }
    if (component != nullptr)
    {
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
            draw_string(document, commands, objectId, componentId,
                        {std::string("textureFilePath")}, "Texture");
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
            draw_string(document, commands, objectId, componentId,
                        {std::string("animationClipFilePath")}, "Animation Clip");
        }
        else if (type == 1)
        {
            ImGui::SeparatorText("Audio");
            draw_string(document, commands, objectId, componentId, {std::string("filePath")}, "Audio File");
            draw_number(document, commands, objectId, componentId, {std::string("gain")}, "Gain", 1.0f);
            draw_integer(document, commands, objectId, componentId, {std::string("loopCount")}, "Loop Count", 0);
            draw_boolean(document, commands, objectId, componentId, {std::string("autoPlay")}, "Auto Play");
        }
        else if (type == 5)
        {
            ImGui::SeparatorText("Behaviors");
            const auto behaviors = component->find("behaviors");
            if (behaviors != component->end() && behaviors->is_array())
            {
                for (std::size_t index = 0; index < behaviors->size(); ++index)
                {
                    const auto &behavior = (*behaviors)[index];
                    ImGui::BulletText("%zu: %s", index,
                                      behavior.is_object() ? behavior.value("type", "Unknown").c_str() : "Malformed");
                }
            }
            if (ImGui::Button("Open Behavior Fields"))
                selection.set(document, Selection{objectId, componentId,
                                                  selection.current().behaviorOrdinal,
                                                  FocusedPanel::Behaviors});
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

    if (ImGui::BeginCombo("Add Component", "Choose a component..."))
    {
        const std::array<std::pair<int, const char *>, 7> choices{{
            {0, "Animator"}, {1, "Audio"}, {2, "Camera"}, {3, "Collider"},
            {4, "Rigidbody"}, {5, "Script"}, {6, "Sprite"}}};
        for (const auto &[type, label] : choices)
        {
            if (has_component_type(*object, type))
                continue;
            if (ImGui::Selectable(label))
            {
                const Selection before = selection.current();
                auto result = commands.add_component(document, objectId, default_component(type));
                if (result.status == CommandStatus::Changed && result.command)
                {
                    const int newComponentId = result.command->payload.value("id", 0);
                    selection.set(document, Selection{objectId, newComponentId, std::nullopt,
                                                       selection.current().focusedPanel});
                    commands.set_last_selection_state({before, selection.current()});
                }
                else
                    feedback_ = result.message;
            }
        }
        ImGui::EndCombo();
    }
    if (!feedback_.empty())
        ImGui::TextWrapped("%s", feedback_.c_str());
}
} // namespace platformator_editor
