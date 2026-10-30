#include "behaviorfields.h"

#include "referencefields.h"

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

CommandResult set_value(DocumentSession &document, EditCommandService &commands, int objectId,
                        int componentId, std::size_t ordinal,
                        const std::vector<FieldPathSegment> &path, Json value)
{
    return commands.set_property(document,
                                 PropertyAddress{document.documentGeneration, objectId, componentId,
                                                 ordinal, path},
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
                      const AssetBrowseCallback &browse);

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
        const bool valueChanged = ImGui::InputFloat("##value", &value, 0.0f, 0.0f, "%.3f",
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        end_field();
        if (valueChanged && std::isfinite(value))
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
        end_field();
        if (changed)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_number_integer())
    {
        std::int64_t value = current->get<std::int64_t>();
        begin_field(label, path);
        const bool changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &value, nullptr, nullptr,
                                                "%lld", ImGuiInputTextFlags_EnterReturnsTrue);
        end_field();
        if (changed)
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        return true;
    }
    if (current->is_number_float())
    {
        double value = current->get<double>();
        begin_field(label, path);
        const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0, "%.6f",
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        end_field();
        if (changed && std::isfinite(value))
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
        end_field();
        if (changed)
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
                                            PropertyAddress{document.documentGeneration, objectId, componentId,
                                                            ordinal, nestedPath});
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
                      const AssetBrowseCallback &browse)
{
    const Json *current = read_path(behavior_owner(document, objectId, componentId, ordinal), path);
    const std::string kind = descriptor.value("kind", "opaque_json");
    if (kind == "boolean")
    {
        bool value = current != nullptr && current->is_boolean() ? current->get<bool>() : false;
        if (ImGui::Checkbox(label.c_str(), &value))
            set_value(document, commands, objectId, componentId, ordinal, path, value);
        if (current == nullptr)
            ImGui::SameLine(), ImGui::TextDisabled("draft; game default retained until edited");
        return true;
    }
    if (kind == "signed_integer" || kind == "unsigned_integer")
    {
        const bool isUnsigned = kind == "unsigned_integer";
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (isUnsigned)
        {
            std::uint64_t value = json_unsigned(current);
            auto minimum = unsigned_bound(descriptor, "minimum").value_or(0);
            auto maximum = unsigned_bound(descriptor, "maximum");
            std::uint64_t limit = maximum.value_or(std::numeric_limits<std::uint64_t>::max());
            if (ImGui::InputScalar(("##" + label).c_str(), ImGuiDataType_U64, &value, &minimum, &limit,
                                   "%llu", ImGuiInputTextFlags_EnterReturnsTrue))
                set_value(document, commands, objectId, componentId, ordinal, path, value);
        }
        else
        {
            std::int64_t value = json_signed(current);
            auto minimum = signed_bound(descriptor, "minimum").value_or(std::numeric_limits<std::int64_t>::min());
            auto maximum = signed_bound(descriptor, "maximum").value_or(std::numeric_limits<std::int64_t>::max());
            if (ImGui::InputScalar(("##" + label).c_str(), ImGuiDataType_S64, &value, &minimum, &maximum,
                                   "%lld", ImGuiInputTextFlags_EnterReturnsTrue))
                set_value(document, commands, objectId, componentId, ordinal, path, value);
        }
        if (current == nullptr)
            ImGui::SameLine(), ImGui::TextDisabled("draft; game default retained until edited");
        return true;
    }
    if (kind == "number")
    {
        double value = current != nullptr && current->is_number() ? current->get<double>() : 0.0;
        if (ImGui::InputDouble(label.c_str(), &value, 0.0, 0.0, "%.6f",
                               ImGuiInputTextFlags_EnterReturnsTrue) && std::isfinite(value))
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
        if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
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
        draw_asset_reference_field(document, commands, objectId, componentId, ordinal, path,
                                   descriptor.value("target", std::string("Asset")),
                                   descriptor.value("nullable", true), label.c_str(), browse);
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
        ImGui::Text("%s", label.c_str());
        ImGui::SameLine();
        if (ImGui::Checkbox(("Set value##" + label).c_str(), &enabled))
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
                                 path, *element, label, browse);
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
                                 child_path(path, index), *element, "Element " + std::to_string(index), browse);
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
} // namespace

void BehaviorFieldsPanel::draw(DocumentSession &document, SelectionModel &selection,
                               EditCommandService &commands, const BehaviorCatalog &catalog,
                               const AssetBrowseCallback &browse)
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
    const Json *behavior = behavior_owner(document, objectId, componentId, ordinal);
    if (behavior == nullptr || !behavior->is_object())
        return;
    const std::string behaviorName = behavior->value("type", std::string("Unknown"));
    const auto snapshot = catalog.snapshot();
    const Json *descriptor = snapshot ? find_behavior_descriptor(snapshot->document, behaviorName) : nullptr;
    if (catalog.state() == CatalogState::Stale)
        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.25f, 1.0f), "Catalog is stale; fields are for editing only.");
    else if (descriptor == nullptr)
        ImGui::TextDisabled("Scene-only or uncatalogued behavior; its saved JSON is preserved.");
    else if (!descriptor->value("field_schema_available", false))
        ImGui::TextDisabled("Registered behavior; field schema is unavailable.");
    else
        ImGui::TextDisabled("Missing fields remain drafts until explicitly edited; runtime defaults are preserved.");

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
                knownFields.insert(fieldName);
                draw_typed_value(document, commands, objectId, componentId, ordinal,
                                 {std::string(fieldName)}, field, fieldName, browse);
            }
        }
    }

    std::vector<std::string> extras;
    for (auto it = behavior->begin(); it != behavior->end(); ++it)
        if (it.key() != "type" && !knownFields.contains(it.key()))
            extras.push_back(it.key());
    if (!extras.empty() || descriptor == nullptr || !descriptor->value("field_schema_available", false))
    {
        if (ImGui::TreeNode("Observed extra fields (preserved)"))
        {
            for (const auto &key : extras)
            {
                ImGui::PushID(key.c_str());
                draw_raw_value(document, commands, objectId, componentId, ordinal,
                               {std::string(key)}, key);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
}
} // namespace platformator_editor
