#include "behaviorfields.h"

#include "referencefields.h"
#include "../discovery.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <string_view>
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

const Json *behavior_owner(const DocumentSession &document, int objectId, int componentId,
                           std::size_t ordinal)
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
    if (components == object->end() || !components->is_array() || componentLocation->second.componentIndex >= components->size())
        return nullptr;
    const auto &component = (*components)[componentLocation->second.componentIndex];
    const auto behaviors = component.find("behaviors");
    return behaviors != component.end() && behaviors->is_array() && ordinal < behaviors->size()
               ? &(*behaviors)[ordinal]
               : nullptr;
}

const Json *read_path(const Json *value, const std::vector<FieldPathSegment> &path)
{
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

std::vector<FieldPathSegment> child_path(const std::vector<FieldPathSegment> &path,
                                         std::string key)
{
    auto result = path;
    result.emplace_back(std::move(key));
    return result;
}

std::vector<FieldPathSegment> child_path(const std::vector<FieldPathSegment> &path,
                                         std::size_t index)
{
    auto result = path;
    result.emplace_back(index);
    return result;
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

void begin_field(const std::string &label, const std::vector<FieldPathSegment> &path)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label.c_str());
    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(ImGui::GetCursorPosX() + std::min(124.0f, available * 0.42f));
    ImGui::PushID(stable_field_id(path).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void end_field()
{
    ImGui::PopID();
}

class BehaviorAddressScope
{
  public:
    BehaviorAddressScope(const DocumentSession &document, int objectId, int componentId,
                         std::size_t behaviorOrdinal)
        : projectGeneration_(std::to_string(document.projectGeneration)),
          tabId_(std::to_string(document.tabId.value)),
          documentGeneration_(std::to_string(document.documentGeneration)),
          objectId_(std::to_string(objectId)), componentId_(std::to_string(componentId)),
          behaviorOrdinal_(std::to_string(behaviorOrdinal))
    {
        ImGui::PushID(projectGeneration_.c_str());
        ImGui::PushID(tabId_.c_str());
        ImGui::PushID(documentGeneration_.c_str());
        ImGui::PushID(objectId_.c_str());
        ImGui::PushID(componentId_.c_str());
        ImGui::PushID(behaviorOrdinal_.c_str());
    }

    ~BehaviorAddressScope()
    {
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }

  private:
    std::string projectGeneration_;
    std::string tabId_;
    std::string documentGeneration_;
    std::string objectId_;
    std::string componentId_;
    std::string behaviorOrdinal_;
};

CommandResult set_value(DocumentSession &document, EditCommandService &commands, int objectId,
                        int componentId, std::size_t ordinal,
                        const std::vector<FieldPathSegment> &path, Json value)
{
    return commands.set_property(document,
                                 address_for_document(document, objectId, componentId, ordinal, path),
                                 std::move(value));
}

std::optional<std::int64_t> signed_bound(const Json &descriptor, const char *key)
{
    const auto found = descriptor.find(key);
    if (found == descriptor.end() || !found->is_string())
        return std::nullopt;
    std::int64_t value = 0;
    const auto &text = found->get_ref<const std::string &>();
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? std::optional<std::int64_t>(value)
               : std::nullopt;
}

std::optional<std::uint64_t> unsigned_bound(const Json &descriptor, const char *key)
{
    const auto found = descriptor.find(key);
    if (found == descriptor.end() || !found->is_string())
        return std::nullopt;
    std::uint64_t value = 0;
    const auto &text = found->get_ref<const std::string &>();
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? std::optional<std::uint64_t>(value)
               : std::nullopt;
}

std::int64_t json_signed(const Json *value, std::int64_t fallback = 0)
{
    if (value == nullptr || !value->is_number_integer())
        return fallback;
    try
    {
        return value->get<std::int64_t>();
    }
    catch (...)
    {
        return fallback;
    }
}

std::uint64_t json_unsigned(const Json *value, std::uint64_t fallback = 0)
{
    if (value == nullptr || !value->is_number_integer())
        return fallback;
    try
    {
        if (value->is_number_unsigned())
            return value->get<std::uint64_t>();
        const auto parsed = value->get<std::int64_t>();
        return parsed >= 0 ? static_cast<std::uint64_t>(parsed) : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

int json_enum(const Json *value)
{
    const auto parsed = json_signed(value, std::numeric_limits<std::int64_t>::min());
    return parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()
               ? 0
               : static_cast<int>(parsed);
}

Json default_value(const Json &descriptor)
{
    const std::string kind = descriptor.value("kind", "opaque_json");
    if (kind == "boolean")
        return false;
    if (kind == "signed_integer")
        return 0;
    if (kind == "unsigned_integer")
        return 0U;
    if (kind == "number")
        return 0.0;
    if (kind == "string")
        return "";
    if (kind == "enum")
    {
        const auto values = descriptor.find("enum_values");
        const int value = values != descriptor.end() && values->is_array() && !values->empty()
                              ? values->front().value("value", 0)
                              : 0;
        return Json(value);
    }
    if (kind == "vector2")
        return Json{{"x", 0.0}, {"y", 0.0}};
    if (kind == "rectangle")
        return Json{{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}};
    if (kind == "array")
        return Json::array();
    if (kind == "optional" || kind == "asset_reference" || kind == "object_reference")
        return nullptr;
    return Json::object();
}

bool draw_raw_value(DocumentSession &document, EditCommandService &commands, int objectId,
                    int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                    const std::string &label);

bool draw_typed_value(DocumentSession &document, EditCommandService &commands, int objectId,
                      int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                      const Json &descriptor, const std::string &label,
                      const AssetBrowseCallback &browse, ResourceIndex *resourceIndex);

bool draw_vector(DocumentSession &document, EditCommandService &commands, int objectId,
                 int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                 const Json *current, const std::vector<std::string> &names, const std::string &label)
{
    bool changed = false;
    Json draft = current != nullptr && current->is_object() ? *current : Json::object();
    for (const auto &name : names)
    {
        float value = 0.0f;
        const auto old = draft.find(name);
        if (old != draft.end() && old->is_number())
        {
            try
            {
                const float parsed = old->get<float>();
                value = std::isfinite(parsed) ? parsed : 0.0f;
            }
            catch (...)
            {
                value = 0.0f;
            }
        }
        ImGui::PushID(name.c_str());
        const auto valuePath = child_path(path, name);
        begin_field(label + " " + name, valuePath);
        const bool valueChanged = ImGui::InputFloat("##value", &value, 0.0f, 0.0f, "%.9g",
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (valueChanged || ImGui::IsItemDeactivatedAfterEdit());
        end_field();
        if (commit && std::isfinite(value))
        {
            draft[name] = value;
            if (current == nullptr || !current->is_object())
                set_value(document, commands, objectId, componentId, ordinal, path, draft);
            else
                set_value(document, commands, objectId, componentId, ordinal, valuePath, value);
            changed = true;
        }
        ImGui::PopID();
    }
    return changed;
}

bool draw_raw_value(DocumentSession &document, EditCommandService &commands, int objectId,
                    int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                    const std::string &label)
{
    const Json *current = read_path(behavior_owner(document, objectId, componentId, ordinal), path);
    if (current == nullptr)
    {
        ImGui::TextDisabled("%s is missing.", label.c_str());
        return false;
    }
    if (current->is_boolean())
    {
        bool value = current->get<bool>();
        begin_field(label, path);
        const bool changed = ImGui::Checkbox("##value", &value);
        end_field();
        if (changed)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_number_unsigned())
    {
        std::uint64_t value = current->get<std::uint64_t>();
        begin_field(label, path);
        const bool changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &value, nullptr, nullptr,
                                                "%llu", ImGuiInputTextFlags_EnterReturnsTrue);
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
        end_field();
        if (commit)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_number_integer())
    {
        std::int64_t value = current->get<std::int64_t>();
        begin_field(label, path);
        const bool changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &value, nullptr, nullptr,
                                                "%lld", ImGuiInputTextFlags_EnterReturnsTrue);
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
        end_field();
        if (commit)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_number_float())
    {
        double value = current->get<double>();
        begin_field(label, path);
        const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0, "%.17g",
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
        end_field();
        if (commit && std::isfinite(value))
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_string())
    {
        const std::string value = current->get<std::string>();
        if (value.size() >= 2048)
        {
            ImGui::TextDisabled("%s is too long for inline editing.", label.c_str());
            return true;
        }
        std::array<char, 2048> buffer{};
        std::memcpy(buffer.data(), value.data(), value.size());
        begin_field(label, path);
        const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        const bool commit = changed || ImGui::IsItemDeactivatedAfterEdit();
        end_field();
        if (commit)
            set_value(document, commands, objectId, componentId, ordinal, path, std::string(buffer.data()));
        return true;
    }
    if (current->is_null())
    {
        ImGui::Text("%s: null", label.c_str());
        return true;
    }
    if (current->is_object())
    {
        Json objectCopy = *current;
        const std::string treeId = label + "###object";
        if (ImGui::TreeNode(treeId.c_str(), "%s", label.c_str()))
        {
            for (auto it = objectCopy.begin(); it != objectCopy.end(); ++it)
            {
                ImGui::PushID(it.key().c_str());
                const auto nestedPath = child_path(path, it.key());
                draw_raw_value(document, commands, objectId, componentId, ordinal, nestedPath, it.key());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                    commands.erase_property(document,
                                            address_for_document(document, objectId, componentId,
                                                                 ordinal, nestedPath));
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        return true;
    }
    if (current->is_array())
    {
        Json arrayCopy = *current;
        ImGui::Text("%s (%zu items)", label.c_str(), arrayCopy.size());
        for (std::size_t index = 0; index < arrayCopy.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            draw_raw_value(document, commands, objectId, componentId, ordinal,
                           child_path(path, index), "Item " + std::to_string(index));
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                arrayCopy.erase(arrayCopy.begin() + static_cast<Json::difference_type>(index));
                set_value(document, commands, objectId, componentId, ordinal, path, std::move(arrayCopy));
                ImGui::PopID();
                return true;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton(("Add item##" + label).c_str()))
        {
            arrayCopy.push_back(nullptr);
            set_value(document, commands, objectId, componentId, ordinal, path, std::move(arrayCopy));
        }
        return true;
    }
    return false;
}

bool draw_typed_value(DocumentSession &document, EditCommandService &commands, int objectId,
                      int componentId, std::size_t ordinal, const std::vector<FieldPathSegment> &path,
                      const Json &descriptor, const std::string &label,
                      const AssetBrowseCallback &browse, ResourceIndex *resourceIndex)
{
    const Json *current = read_path(behavior_owner(document, objectId, componentId, ordinal), path);
    const std::string kind = descriptor.value("kind", "opaque_json");
    if (kind == "boolean")
    {
        bool value = current != nullptr && current->is_boolean() ? current->get<bool>() : false;
        begin_field(label, path);
        const bool changed = ImGui::Checkbox("##value", &value);
        end_field();
        if (changed)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        if (current == nullptr)
            ImGui::SameLine(), ImGui::TextDisabled("draft; game default retained until edited");
        return true;
    }
    if (kind == "signed_integer" || kind == "unsigned_integer")
    {
        const bool isUnsigned = kind == "unsigned_integer";
        begin_field(label, path);
        if (isUnsigned)
        {
            std::uint64_t value = json_unsigned(current);
            auto minimum = unsigned_bound(descriptor, "minimum").value_or(0);
            auto maximum = unsigned_bound(descriptor, "maximum");
            std::uint64_t limit = maximum.value_or(std::numeric_limits<std::uint64_t>::max());
            const bool changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &value, &minimum, &limit,
                "%llu", ImGuiInputTextFlags_EnterReturnsTrue);
            const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (!cancel && (changed || ImGui::IsItemDeactivatedAfterEdit()))
                set_value(document, commands, objectId, componentId, ordinal, path, value);
        }
        else
        {
            std::int64_t value = json_signed(current);
            auto minimum = signed_bound(descriptor, "minimum").value_or(std::numeric_limits<std::int64_t>::min());
            auto maximum = signed_bound(descriptor, "maximum").value_or(std::numeric_limits<std::int64_t>::max());
            const bool changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &value, &minimum, &maximum,
                "%lld", ImGuiInputTextFlags_EnterReturnsTrue);
            const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (!cancel && (changed || ImGui::IsItemDeactivatedAfterEdit()))
                set_value(document, commands, objectId, componentId, ordinal, path, value);
        }
        end_field();
        if (current == nullptr)
            ImGui::SameLine(), ImGui::TextDisabled("draft; game default retained until edited");
        return true;
    }
    if (kind == "number")
    {
        double value = current != nullptr && current->is_number() ? current->get<double>() : 0.0;
        begin_field(label, path);
        const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0, "%.17g",
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
        end_field();
        if (commit && std::isfinite(value))
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        if (current == nullptr)
            ImGui::SameLine(), ImGui::TextDisabled("draft; game default retained until edited");
        return true;
    }
    if (kind == "string")
    {
        const std::string old = current != nullptr && current->is_string() ? current->get<std::string>() : std::string{};
        if (old.size() >= 2048)
        {
            ImGui::TextDisabled("%s is too long for inline editing.", label.c_str());
            return true;
        }
        std::array<char, 2048> buffer{};
        std::memcpy(buffer.data(), old.data(), old.size());
        begin_field(label, path);
        const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        const bool commit = changed || ImGui::IsItemDeactivatedAfterEdit();
        end_field();
        if (commit)
            set_value(document, commands, objectId, componentId, ordinal, path, std::string(buffer.data()));
        if (current == nullptr)
            ImGui::SameLine(), ImGui::TextDisabled("draft; game default retained until edited");
        return true;
    }
    if (kind == "enum")
    {
        const auto enumValues = descriptor.find("enum_values");
        const int currentValue = json_enum(current);
        std::string preview = "Unknown value";
        if (enumValues != descriptor.end() && enumValues->is_array())
            for (const auto &enumValue : *enumValues)
                if (enumValue.value("value", 0) == currentValue)
                    preview = enumValue.value("name", std::string("Unknown value"));
        begin_field(label, path);
        if (ImGui::BeginCombo("##value", preview.c_str()))
        {
            if (enumValues != descriptor.end() && enumValues->is_array())
            {
                for (const auto &enumValue : *enumValues)
                {
                    const int value = enumValue.value("value", 0);
                    const auto name = enumValue.value("name", std::string("Unknown"));
                    if (ImGui::Selectable(name.c_str(), currentValue == value))
                        set_value(document, commands, objectId, componentId, ordinal, path, value);
                }
            }
            ImGui::EndCombo();
        }
        end_field();
        return true;
    }
    if (kind == "vector2")
        return draw_vector(document, commands, objectId, componentId, ordinal, path, current,
                           {"x", "y"}, label);
    if (kind == "rectangle")
        return draw_vector(document, commands, objectId, componentId, ordinal, path, current,
                           {"x", "y", "w", "h"}, label);
    if (kind == "asset_reference")
    {
        draw_asset_reference_field(document, commands, objectId, std::optional<int>(componentId),
                                   std::optional<std::size_t>(ordinal), path,
                                   descriptor.value("target", std::string("Asset")),
                                   descriptor.value("nullable", true), label.c_str(), browse,
                                   nullptr, resourceIndex);
        return true;
    }
    if (kind == "object_reference")
    {
        draw_scene_reference_field(document, commands, objectId, componentId, ordinal, path,
                                   descriptor.value("target", std::string("GameObject")),
                                   descriptor.value("nullable", true), label.c_str());
        return true;
    }
    if (kind == "optional")
    {
        const bool hasValue = current != nullptr && !current->is_null();
        bool enabled = hasValue;
        begin_field(label, path);
        const bool changed = ImGui::Checkbox("##enabled", &enabled);
        end_field();
        if (changed)
        {
            if (!enabled)
                set_value(document, commands, objectId, componentId, ordinal, path, nullptr);
            else if (!hasValue)
            {
                const auto element = descriptor.find("element");
                set_value(document, commands, objectId, componentId, ordinal, path,
                          element != descriptor.end() ? default_value(*element) : Json(nullptr));
            }
        }
        if (enabled)
        {
            const auto element = descriptor.find("element");
            if (element != descriptor.end())
                draw_typed_value(document, commands, objectId, componentId, ordinal,
                                 path, *element, label, browse, resourceIndex);
        }
        else
            ImGui::SameLine(), ImGui::TextDisabled(current != nullptr ? "explicit null" : "missing");
        return true;
    }
    if (kind == "array")
    {
        Json array = current != nullptr && current->is_array() ? *current : Json::array();
        ImGui::Text("%s (%zu items)", label.c_str(), array.size());
        const auto element = descriptor.find("element");
        if (element != descriptor.end())
        {
            for (std::size_t index = 0; index < array.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                draw_typed_value(document, commands, objectId, componentId, ordinal,
                                 child_path(path, index), *element, "Element " + std::to_string(index), browse,
                                 resourceIndex);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    array.erase(array.begin() + static_cast<Json::difference_type>(index));
                    set_value(document, commands, objectId, componentId, ordinal, path, std::move(array));
                    ImGui::PopID();
                    return true;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton(("Add element##" + label).c_str()))
            {
                array.push_back(default_value(*element));
                set_value(document, commands, objectId, componentId, ordinal, path, std::move(array));
            }
        }
        return true;
    }
    if (kind == "opaque_json")
    {
        if (current == nullptr)
        {
            if (ImGui::SmallButton(("Create JSON object##" + label).c_str()))
                set_value(document, commands, objectId, componentId, ordinal, path, Json::object());
            ImGui::SameLine();
            ImGui::TextDisabled("sample/schema unavailable; no value is assumed");
            return true;
        }
        return draw_raw_value(document, commands, objectId, componentId, ordinal, path, label);
    }
    return false;
}

const Json *find_behavior_descriptor(const Json &catalog, std::string_view name)
{
    const auto behaviors = catalog.find("behaviors");
    if (behaviors == catalog.end() || !behaviors->is_array())
        return nullptr;
    for (const auto &behavior : *behaviors)
        if (behavior.is_object() && behavior.value("name", "") == name)
            return &behavior;
    return nullptr;
}

std::optional<int> script_component_at_occurrence(const DocumentSession &document, int objectId,
                                                  std::size_t occurrence)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return std::nullopt;
    const Json *object = resolve_object(document.root, location->second);
    if (object == nullptr)
        return std::nullopt;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array())
        return std::nullopt;
    std::size_t current = 0;
    for (const auto &component : *components)
    {
        if (!component.is_object() || component.value("type", -1) != 5)
            continue;
        if (current++ != occurrence)
            continue;
        const int componentId = json_signed(component.contains("id") ? &component["id"] : nullptr, -1);
        return componentId >= 0 ? std::optional<int>(componentId) : std::nullopt;
    }
    return std::nullopt;
}

bool compatible_behavior_value(const Json *value, std::string_view kind)
{
    if (value == nullptr)
        return false;
    if (kind == "boolean")
        return value->is_boolean();
    if (kind == "number")
    {
        if (!value->is_number())
            return false;
        try
        {
            return std::isfinite(value->get<double>());
        }
        catch (...)
        {
            return false;
        }
    }
    if (kind == "signed_integer")
        return value->is_number_integer() && !value->is_number_unsigned();
    if (kind == "unsigned_integer")
    {
        if (value->is_number_unsigned())
            return true;
        return value->is_number_integer() && json_signed(value, -1) >= 0;
    }
    if (kind == "enum")
        return value->is_number_integer();
    return false;
}

std::string object_search_context(const DocumentSession &document, int objectId)
{
    std::string context = " #" + std::to_string(objectId);
    int currentId = objectId;
    while (true)
    {
        const auto location = document.indexes.objectsById.find(currentId);
        if (location == document.indexes.objectsById.end())
            break;
        const Json *object = resolve_object(document.root, location->second);
        if (object == nullptr)
            break;
        for (const char *key : {"name", "tag"})
        {
            const auto value = object->find(key);
            if (value != object->end() && value->is_string())
                context += " " + value->get<std::string>();
        }
        const auto parent = document.indexes.parentById.find(currentId);
        if (parent == document.indexes.parentById.end() || !parent->second)
            break;
        currentId = *parent->second;
    }
    return context;
}

struct MultiBehaviorAddress
{
    int objectId = 0;
    int componentId = 0;
    std::size_t ordinal = 0;
};

DocumentIdentity author_declaration_identity(const DocumentSession &document)
{
    return {ProjectGeneration{document.projectGeneration}, document.tabId,
            DocumentGeneration{document.documentGeneration}};
}

std::string author_declaration_key(const DocumentSession &document, int objectId,
                                   int componentId, std::size_t behaviorOrdinal)
{
    return std::to_string(document.projectGeneration) + ":" +
        std::to_string(document.tabId.value) + ":" +
        std::to_string(document.documentGeneration) + ":" +
        std::to_string(objectId) + ":" + std::to_string(componentId) + ":" +
        std::to_string(behaviorOrdinal);
}

std::string escape_pointer_segment(std::string_view segment)
{
    std::string escaped;
    for (const char character : segment)
    {
        if (character == '~') escaped += "~0";
        else if (character == '/') escaped += "~1";
        else escaped.push_back(character);
    }
    return escaped;
}

const Json *resolve_relative_pointer(const Json &root, std::string_view pointer)
{
    if (pointer.empty() || pointer.front() != '/')
        return nullptr;
    const Json *value = &root;
    std::size_t start = 1;
    while (start <= pointer.size())
    {
        const auto end = pointer.find('/', start);
        const auto encoded = pointer.substr(start,
            end == std::string_view::npos ? pointer.size() - start : end - start);
        if (encoded.empty())
            return nullptr;
        std::string segment;
        segment.reserve(encoded.size());
        for (std::size_t index = 0; index < encoded.size(); ++index)
        {
            if (encoded[index] != '~')
            {
                segment.push_back(encoded[index]);
                continue;
            }
            if (index + 1 >= encoded.size() ||
                (encoded[index + 1] != '0' && encoded[index + 1] != '1'))
                return nullptr;
            segment.push_back(encoded[++index] == '0' ? '~' : '/');
        }
        if (value->is_object())
        {
            const auto child = value->find(segment);
            if (child == value->end())
                return nullptr;
            value = &*child;
        }
        else if (value->is_array())
        {
            if (segment.size() > 1 && segment.front() == '0')
                return nullptr;
            std::size_t arrayIndex = 0;
            const auto parsed = std::from_chars(segment.data(),
                segment.data() + segment.size(), arrayIndex);
            if (parsed.ec != std::errc{} || parsed.ptr != segment.data() + segment.size() ||
                arrayIndex >= value->size())
                return nullptr;
            value = &(*value)[arrayIndex];
        }
        else
            return nullptr;
        if (end == std::string_view::npos)
            return value;
        start = end + 1;
    }
    return nullptr;
}

bool is_reference_integer(const Json *value)
{
    if (value == nullptr || (!value->is_number_integer() && !value->is_number_unsigned()))
        return false;
    if (value->is_number_unsigned())
        return value->get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    const auto number = value->get<std::int64_t>();
    return number >= 0 && number <= std::numeric_limits<int>::max();
}
} // namespace

std::vector<Json> BehaviorFieldsPanel::template_author_bindings(
    const DocumentSession &document) const
{
    const auto identity = author_declaration_identity(document);
    std::vector<Json> bindings;
    for (const auto &[key, declaration] : authorReferenceDeclarations_)
    {
        (void)key;
        if (declaration.identity != identity)
            continue;
        for (const auto &[relativePointer, targetKind] : declaration.fieldKinds)
            bindings.push_back(make_template_reference_path_declaration(
                declaration.ownerId, declaration.componentId,
                declaration.behaviorOrdinal, relativePointer, targetKind));
    }
    return bindings;
}

std::vector<TemplateAuthorSchemaDeclaration> BehaviorFieldsPanel::template_author_schemas(
    const DocumentSession &document) const
{
    const auto identity = author_declaration_identity(document);
    std::vector<TemplateAuthorSchemaDeclaration> declarations;
    for (const auto &[key, declaration] : authorReferenceDeclarations_)
    {
        (void)key;
        if (declaration.identity == identity && declaration.schemaCoverageComplete)
            declarations.push_back({declaration.ownerId, declaration.componentId,
                                    declaration.behaviorOrdinal, declaration.behaviorType});
    }
    return declarations;
}

void BehaviorFieldsPanel::draw(DocumentSession &document, SelectionModel &selection,
                               EditCommandService &commands, const BehaviorCatalog &catalog,
                               const AssetBrowseCallback &browse, ResourceIndex *resourceIndex,
                               std::string_view searchQuery)
{
    selection.repair(document);
    if (!selection.current().objectId || !selection.current().componentId)
    {
        ImGui::TextDisabled("Select a Script component to inspect behavior fields.");
        return;
    }
    const int objectId = *selection.current().objectId;
    const int componentId = *selection.current().componentId;
    const auto componentLocation = document.indexes.componentsById.find(componentId);
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (componentLocation == document.indexes.componentsById.end() || objectLocation == document.indexes.objectsById.end() ||
        componentLocation->second.ownerObjectId != objectId)
        return;
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        componentLocation->second.componentIndex >= components->size())
        return;
    const Json &component = (*components)[componentLocation->second.componentIndex];
    if (component.value("type", -1) != 5)
    {
        ImGui::TextDisabled("Behavior fields are available when a Script component is selected.");
        return;
    }
    const auto behaviorList = component.find("behaviors");
    if (behaviorList == component.end() || !behaviorList->is_array())
        return;
    if (!behaviorList->empty() &&
        (!selection.current().behaviorOrdinal ||
         *selection.current().behaviorOrdinal >= behaviorList->size()))
        selection.set(document, Selection{objectId, componentId, 0, FocusedPanel::Inspector});
    ImGui::TextUnformatted("Behaviors");
    for (std::size_t index = 0; index < behaviorList->size(); ++index)
    {
        const auto &behavior = (*behaviorList)[index];
        const std::string name = behavior.is_object() ? behavior.value("type", std::string("Unknown")) : "Malformed";
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable((std::to_string(index) + ": " + name).c_str(),
                              selection.current().behaviorOrdinal == index))
            selection.set(document, Selection{objectId, componentId, index, FocusedPanel::Behaviors});
        ImGui::PopID();
    }
    if (!selection.current().behaviorOrdinal || *selection.current().behaviorOrdinal >= behaviorList->size())
    {
        ImGui::TextDisabled("Select a behavior to edit its saved fields.");
        return;
    }
    const auto ordinal = *selection.current().behaviorOrdinal;
    const BehaviorAddressScope addressScope(document, objectId, componentId, ordinal);
    const Json *behavior = behavior_owner(document, objectId, componentId, ordinal);
    if (behavior == nullptr || !behavior->is_object())
        return;
    const std::string behaviorName = behavior->value("type", std::string("Unknown"));
    const auto snapshot = catalog.snapshot();
    const Json *descriptor = snapshot ? find_behavior_descriptor(snapshot->document, behaviorName) : nullptr;
    const bool schemaUnavailable = descriptor == nullptr ||
        !descriptor->value("field_schema_available", false);
    if (schemaUnavailable)
        ImGui::TextDisabled("Registered behavior; field schema is unavailable.");

    std::set<std::string> knownFields;
    if (descriptor != nullptr && descriptor->value("field_schema_available", false))
    {
        const auto fields = descriptor->find("fields");
        if (fields != descriptor->end() && fields->is_array())
        {
            for (const auto &field : *fields)
            {
                const std::string fieldName = field.value("name", std::string{});
                if (fieldName.empty())
                    continue;
                const std::string searchable = behaviorName + " " + fieldName + " " +
                    field.value("label", fieldName) + " " +
                    field.value("kind", std::string("opaque_json")) + " #" +
                    std::to_string(objectId) + object_search_context(document, objectId);
                if (!discovery_query_matches(searchQuery, searchable))
                    continue;
                knownFields.insert(fieldName);
                draw_typed_value(document, commands, objectId, componentId, ordinal,
                                 {std::string(fieldName)}, field, fieldName, browse, resourceIndex);
            }
        }
    }

    std::vector<std::string> extras;
    for (auto it = behavior->begin(); it != behavior->end(); ++it)
        if (it.key() != "type" && !knownFields.contains(it.key()) &&
            discovery_query_matches(searchQuery, behaviorName + " " + it.key() +
                object_search_context(document, objectId)))
            extras.push_back(it.key());
    AuthorReferenceDeclaration *authorDeclaration = nullptr;
    if (schemaUnavailable || !extras.empty())
    {
        const auto key = author_declaration_key(document, objectId, componentId, ordinal);
        auto &state = authorReferenceDeclarations_[key];
        state.identity = author_declaration_identity(document);
        state.ownerId = objectId;
        state.componentId = componentId;
        state.behaviorOrdinal = ordinal;
        state.behaviorType = behaviorName;
        authorDeclaration = &state;
    }
    if (!extras.empty() || schemaUnavailable)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNode("Observed extra fields (preserved)"))
        {
            for (const auto &key : extras)
            {
                ImGui::PushID(key.c_str());
                Json value;
                const auto current = behavior->find(key);
                if (current != behavior->end())
                    value = *current;
                const std::string fieldPointer = "/" + escape_pointer_segment(key);
                if (is_reference_integer(&value))
                {
                    ImGui::TextUnformatted(key.c_str());
                    ImGui::SameLine();
                    std::optional<std::string> declaredKind;
                    if (authorDeclaration != nullptr)
                        if (const auto declared = authorDeclaration->fieldKinds.find(fieldPointer);
                            declared != authorDeclaration->fieldKinds.end())
                            declaredKind = declared->second;
                    if (draw_template_reference_kind_control(objectId, componentId, ordinal,
                                                             key, declaredKind) &&
                        authorDeclaration != nullptr)
                    {
                        if (declaredKind)
                            authorDeclaration->fieldKinds[fieldPointer] = *declaredKind;
                        else
                            authorDeclaration->fieldKinds.erase(fieldPointer);
                    }
                }
                else if ((value.is_object() || value.is_array()) && authorDeclaration != nullptr)
                {
                    auto &draft = authorDeclaration->nestedPathDrafts[key];
                    ImGui::TextDisabled("Declare nested reference site under %s", key.c_str());
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint("##nested-reference-pointer", "/target or /items/0/id",
                                             draft.pointer.data(), draft.pointer.size());
                    std::optional<std::string> targetKind = draft.targetKind;
                    draw_template_reference_kind_control(objectId, componentId, ordinal,
                        "nested-reference-kind", targetKind);
                    draft.targetKind = targetKind;
                    const std::string nestedPointer(draft.pointer.data());
                    const Json *nestedValue = resolve_relative_pointer(value, nestedPointer);
                    const std::string relativePointer = fieldPointer + nestedPointer;
                    const bool declarationValid = draft.targetKind &&
                        is_reference_integer(nestedValue) &&
                        !make_template_reference_path_declaration(objectId, componentId, ordinal,
                            relativePointer, *draft.targetKind).empty();
                    if (!declarationValid && !nestedPointer.empty())
                        ImGui::TextDisabled("Path must resolve to an integer reference in this field.");
                    if (ImGui::Button("Add nested reference declaration") && declarationValid)
                    {
                        authorDeclaration->fieldKinds[relativePointer] = *draft.targetKind;
                        draft.pointer.fill('\0');
                        draft.targetKind.reset();
                    }

                    std::vector<std::string> declaredPaths;
                    for (const auto &[path, kind] : authorDeclaration->fieldKinds)
                    {
                        (void)kind;
                        if (path.size() > fieldPointer.size() &&
                            path.compare(0, fieldPointer.size(), fieldPointer) == 0 &&
                            path[fieldPointer.size()] == '/')
                            declaredPaths.push_back(path);
                    }
                    for (const auto &path : declaredPaths)
                    {
                        ImGui::PushID(path.c_str());
                        ImGui::Text("%s", path.c_str());
                        ImGui::SameLine();
                        std::optional<std::string> declaredKind = authorDeclaration->fieldKinds[path];
                        if (draw_template_reference_kind_control(objectId, componentId, ordinal,
                                                                 path, declaredKind))
                        {
                            if (declaredKind)
                                authorDeclaration->fieldKinds[path] = *declaredKind;
                            else
                                authorDeclaration->fieldKinds.erase(path);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove"))
                            authorDeclaration->fieldKinds.erase(path);
                        ImGui::PopID();
                    }
                }
                draw_raw_value(document, commands, objectId, componentId, ordinal,
                               {std::string(key)}, key);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
    if (schemaUnavailable && authorDeclaration != nullptr)
    {
        ImGui::TextWrapped("Declare integer references in opaque fields, including nested JSON pointers. Confirm only after reviewing every field that can hold an object or component reference.");
        bool complete = authorDeclaration->schemaCoverageComplete;
        if (ImGui::Checkbox("Reference declarations cover this behavior", &complete))
            authorDeclaration->schemaCoverageComplete = complete;
    }
}

void BehaviorFieldsPanel::draw_multi_object_fields(DocumentSession &document,
                                                   const Selection &selection,
                                                   EditCommandService &commands,
                                                   const BehaviorCatalog &catalog,
                                                   const std::vector<int> &objectIds,
                                                   ResourceIndex *resourceIndex,
                                                   std::string_view searchQuery)
{
    if (objectIds.size() < 2)
        return;
    const auto catalogSnapshot = catalog.snapshot();
    if (!catalogSnapshot)
        return;

    std::string selectionScope("multi-behaviors");
    for (const int objectId : objectIds)
    {
        selectionScope.push_back(':');
        selectionScope += std::to_string(objectId);
    }
    ImGui::PushID(selectionScope.c_str());

    std::size_t primaryScriptCount = 0;
    const auto primaryLocation = document.indexes.objectsById.find(selection.objectId.value_or(-1));
    if (primaryLocation != document.indexes.objectsById.end())
    {
        const Json *primary = resolve_object(document.root, primaryLocation->second);
        const auto components = primary != nullptr ? primary->find("components") : Json::const_iterator{};
        if (primary != nullptr && components != primary->end() && components->is_array())
            for (const auto &component : *components)
                if (component.is_object() && component.value("type", -1) == 5)
                    ++primaryScriptCount;
    }
    if (primaryScriptCount == 0)
    {
        ImGui::PopID();
        return;
    }

    std::vector<std::vector<MultiBehaviorAddress>> scriptComponents(primaryScriptCount);
    bool hasAnyCommonBehavior = false;
    for (std::size_t scriptOccurrence = 0; scriptOccurrence < primaryScriptCount; ++scriptOccurrence)
    {
        auto &participants = scriptComponents[scriptOccurrence];
        participants.reserve(objectIds.size());
        bool complete = true;
        for (const int objectId : objectIds)
        {
            const auto componentId = script_component_at_occurrence(document, objectId, scriptOccurrence);
            if (!componentId)
            {
                complete = false;
                break;
            }
            participants.push_back({objectId, *componentId, 0});
        }
        if (!complete)
            continue;

        std::size_t behaviorCount = std::numeric_limits<std::size_t>::max();
        for (const auto &participant : participants)
        {
            const auto componentLocation = document.indexes.componentsById.find(participant.componentId);
            const auto objectLocation = document.indexes.objectsById.find(participant.objectId);
            if (componentLocation == document.indexes.componentsById.end() ||
                objectLocation == document.indexes.objectsById.end())
            {
                behaviorCount = 0;
                break;
            }
            const Json *object = resolve_object(document.root, objectLocation->second);
            if (object == nullptr)
            {
                behaviorCount = 0;
                break;
            }
            const auto components = object->find("components");
            if (components == object->end() || !components->is_array() ||
                componentLocation->second.componentIndex >= components->size())
            {
                behaviorCount = 0;
                break;
            }
            const auto behaviors = (*components)[componentLocation->second.componentIndex].find("behaviors");
            const std::size_t count = behaviors != (*components)[componentLocation->second.componentIndex].end() &&
                                      behaviors->is_array() ? behaviors->size() : 0;
            behaviorCount = std::min(behaviorCount, count);
        }

        for (std::size_t behaviorOrdinal = 0; behaviorOrdinal < behaviorCount; ++behaviorOrdinal)
        {
            const Json *primaryBehavior = behavior_owner(document, participants.front().objectId,
                                                         participants.front().componentId,
                                                         behaviorOrdinal);
            if (primaryBehavior == nullptr || !primaryBehavior->is_object())
                continue;
            const auto behaviorName = primaryBehavior->value("type", std::string{});
            if (behaviorName.empty())
                continue;
            bool sameBehavior = true;
            for (const auto &participant : participants)
            {
                const Json *behavior = behavior_owner(document, participant.objectId,
                                                      participant.componentId, behaviorOrdinal);
                if (behavior == nullptr || !behavior->is_object() ||
                    behavior->value("type", std::string{}) != behaviorName)
                {
                    sameBehavior = false;
                    break;
                }
            }
            if (!sameBehavior)
                continue;
            const Json *descriptor = find_behavior_descriptor(catalogSnapshot->document, behaviorName);
            if (descriptor == nullptr || !descriptor->value("field_schema_available", false))
                continue;
            const auto fields = descriptor->find("fields");
            if (fields == descriptor->end() || !fields->is_array())
                continue;

            if (!hasAnyCommonBehavior)
            {
                ImGui::SeparatorText("Common Script behavior fields");
                hasAnyCommonBehavior = true;
            }
            ImGui::PushID(static_cast<int>(scriptOccurrence));
            ImGui::PushID(static_cast<int>(behaviorOrdinal));
            ImGui::Text("Script %zu · %s", scriptOccurrence + 1, behaviorName.c_str());

            for (const auto &field : *fields)
            {
                const std::string fieldName = field.value("name", std::string{});
                const std::string kind = field.value("kind", std::string("opaque_json"));
                if (fieldName.empty() || (kind != "boolean" && kind != "number" &&
                                          kind != "signed_integer" && kind != "unsigned_integer" &&
                                          kind != "enum"))
                    continue;
                std::string objectContext;
                for (const auto &participant : participants)
                    objectContext += object_search_context(document, participant.objectId);
                const std::string searchable = behaviorName + " " + fieldName + " " +
                    field.value("label", fieldName) + " " + kind;
                if (!discovery_query_matches(searchQuery, searchable + objectContext))
                    continue;
                const std::vector<FieldPathSegment> path{fieldName};
                std::vector<Json> values;
                values.reserve(participants.size());
                bool compatible = true;
                for (const auto &participant : participants)
                {
                    const Json *currentBehavior = behavior_owner(document, participant.objectId,
                                                                 participant.componentId,
                                                                 behaviorOrdinal);
                    const Json *value = read_path(currentBehavior, path);
                    if (!compatible_behavior_value(value, kind))
                    {
                        compatible = false;
                        break;
                    }
                    values.push_back(*value);
                }
                if (!compatible)
                    continue;

                const bool mixed = std::any_of(values.begin() + 1, values.end(),
                                               [&](const Json &value) { return value != values.front(); });
                const auto commit = [&](Json next) {
                    std::vector<BatchPropertyEdit> edits;
                    edits.reserve(participants.size());
                    for (const auto &participant : participants)
                        edits.push_back({address_for_document(document, participant.objectId,
                                                              participant.componentId,
                                                              behaviorOrdinal, path),
                                         PropertyValue::of(next)});
                    commands.apply_batch(document, edits, {selection, selection});
                };

                begin_field(field.value("label", fieldName), path);
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
                if (kind == "boolean")
                {
                    bool value = values.front().get<bool>();
                    const bool changed = ImGui::Checkbox("##value", &value);
                    ImGui::PopItemFlag();
                    end_field();
                    if (changed)
                        commit(value);
                }
                else if (kind == "number")
                {
                    double value = values.front().get<double>();
                    const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0, "%.17g",
                                                            ImGuiInputTextFlags_EnterReturnsTrue);
                    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool shouldCommit = !cancel &&
                        (changed || ImGui::IsItemDeactivatedAfterEdit());
                    ImGui::PopItemFlag();
                    end_field();
                    if (shouldCommit && std::isfinite(value))
                        commit(value);
                }
                else if (kind == "signed_integer")
                {
                    std::int64_t value = json_signed(&values.front());
                    const bool changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &value,
                        nullptr, nullptr, "%lld", ImGuiInputTextFlags_EnterReturnsTrue);
                    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool shouldCommit = !cancel &&
                        (changed || ImGui::IsItemDeactivatedAfterEdit());
                    ImGui::PopItemFlag();
                    end_field();
                    if (shouldCommit)
                        commit(value);
                }
                else if (kind == "unsigned_integer")
                {
                    std::uint64_t value = json_unsigned(&values.front());
                    const bool changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &value,
                        nullptr, nullptr, "%llu", ImGuiInputTextFlags_EnterReturnsTrue);
                    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool shouldCommit = !cancel &&
                        (changed || ImGui::IsItemDeactivatedAfterEdit());
                    ImGui::PopItemFlag();
                    end_field();
                    if (shouldCommit)
                        commit(value);
                }
                else
                {
                    const auto enumValues = field.find("enum_values");
                    const int current = json_enum(&values.front());
                    std::string preview = mixed ? "Multiple values" : "Unknown value";
                    if (!mixed && enumValues != field.end() && enumValues->is_array())
                        for (const auto &entry : *enumValues)
                            if (entry.value("value", 0) == current)
                                preview = entry.value("name", std::string("Unknown value"));
                    if (ImGui::BeginCombo("##value", preview.c_str()))
                    {
                        if (enumValues != field.end() && enumValues->is_array())
                            for (const auto &entry : *enumValues)
                            {
                                const int value = entry.value("value", 0);
                                const auto name = entry.value("name", std::string("Unknown"));
                                if (ImGui::Selectable(name.c_str(), !mixed && current == value))
                                    commit(value);
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemFlag();
                    end_field();
                }
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    ImGui::PopID();
    static_cast<void>(resourceIndex);
}
} // namespace platformator_editor
