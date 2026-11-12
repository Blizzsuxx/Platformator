#include "inspector.h"

#include "../camerainterpretation.h"

#include "../componenttypes.h"
#include "../componentoperations.h"
#include "../documentio.h"
#include "../discovery.h"
#include "../style.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
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

Json *resolve_mutable_object(Json &root, const ObjectLocation &location)
{
    if (!root.is_array() || location.childIndices.empty())
        return nullptr;
    Json *object = nullptr;
    Json *siblings = &root;
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

Json *mutable_address_value(DocumentSession &document, int objectId,
                            std::optional<int> componentId,
                            const std::vector<FieldPathSegment> &path)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return nullptr;
    Json *object = resolve_mutable_object(document.root, location->second);
    if (object == nullptr)
        return nullptr;
    Json *value = object;
    if (componentId)
    {
        const auto componentLocation = document.indexes.componentsById.find(*componentId);
        if (componentLocation == document.indexes.componentsById.end() ||
            componentLocation->second.ownerObjectId != objectId)
            return nullptr;
        const auto components = object->find("components");
        if (components == object->end() || !components->is_array() ||
            componentLocation->second.componentIndex >= components->size())
            return nullptr;
        value = &(*components)[componentLocation->second.componentIndex];
    }
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

std::string diagnostic_field_id(const std::string &path)
{
    std::string result = "/";
    for (const char value : path)
        result.push_back(value == '.' ? '/' : value);
    return result;
}

std::string semantic_path_token(const std::vector<FieldPathSegment> &path)
{
    const std::string stable = stable_field_id(path);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(stable.size() * 2);
    for (const unsigned char value : stable)
    {
        result.push_back(hex[value >> 4]);
        result.push_back(hex[value & 0x0f]);
    }
    return result;
}

std::string semantic_value_text(const Json *value, std::string_view fallback = "Not set")
{
    if (value == nullptr)
        return std::string(fallback);
    try
    {
        if (value->is_string())
        {
            std::string text = value->get<std::string>();
            if (text.size() > 512)
            {
                text.resize(509);
                text += "...";
            }
            return text;
        }
        if (value->is_boolean() || value->is_number())
            return value->dump();
        if (value->is_array())
            return "Array with " + std::to_string(value->size()) + " items";
        if (value->is_object())
            return "Object with " + std::to_string(value->size()) + " fields";
        return "null";
    }
    catch (...)
    {
        return "Value unavailable";
    }
}

std::string semantic_field_path(const std::vector<FieldPathSegment> &path)
{
    std::string result;
    for (const auto &segment : path)
    {
        if (!result.empty())
            result.push_back('.');
        if (const auto *key = std::get_if<std::string>(&segment))
            result += *key;
        else
            result += std::to_string(std::get<std::size_t>(segment));
    }
    return result;
}

std::string template_file_stem(std::string_view name)
{
    std::string stem;
    stem.reserve(std::min<std::size_t>(name.size(), 96));
    bool lastWasSeparator = false;
    for (const unsigned char character : name)
    {
        if (stem.size() >= 96)
            break;
        if (std::isalnum(character))
        {
            stem.push_back(static_cast<char>(character));
            lastWasSeparator = false;
        }
        else if ((character == '-' || character == '_' || character == ' ') && !lastWasSeparator)
        {
            stem.push_back(character == ' ' ? '-' : static_cast<char>(character));
            lastWasSeparator = true;
        }
    }
    while (!stem.empty() && (stem.back() == '-' || stem.back() == '_'))
        stem.pop_back();
    return stem.empty() ? "object-template" : stem;
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

class InspectorAddressScope
{
  public:
    InspectorAddressScope(const DocumentSession &document, int objectId,
                          std::uint64_t hostGeneration)
        : hostGeneration_(std::to_string(hostGeneration)),
          projectGeneration_(std::to_string(document.projectGeneration)),
          tabId_(std::to_string(document.tabId.value)),
          documentGeneration_(std::to_string(document.documentGeneration)),
          objectId_(std::to_string(objectId))
    {
        ImGui::PushID(hostGeneration_.c_str());
        ImGui::PushID(projectGeneration_.c_str());
        ImGui::PushID(tabId_.c_str());
        ImGui::PushID(documentGeneration_.c_str());
        ImGui::PushID(objectId_.c_str());
    }

    ~InspectorAddressScope()
    {
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }

  private:
    std::string hostGeneration_;
    std::string projectGeneration_;
    std::string tabId_;
    std::string documentGeneration_;
    std::string objectId_;
};

} // namespace

const std::string &InspectorPanel::feedback() const noexcept
{
    return feedback_;
}

void InspectorPanel::save_selected_template(DocumentSession &document, int objectId,
                                             const BehaviorCatalog &catalog)
{
    if (templateName_.empty() || document.assetRoot.empty())
    {
        feedback_ = "A template name and project asset root are required.";
        return;
    }
    const auto location = document.indexes.objectsById.find(objectId);
    const Json *object = location == document.indexes.objectsById.end()
        ? nullptr : resolve_object(document.root, location->second);
    if (object == nullptr)
    {
        feedback_ = "The selected GameObject changed before the template could be saved.";
        return;
    }
    const auto catalogSnapshot = catalog.snapshot();
    const Json catalogDocument = catalogSnapshot ? catalogSnapshot->document : Json::object();
    const auto bindings = behaviorFields_.template_author_bindings(document);
    const auto authorSchemas = behaviorFields_.template_author_schemas(document);
    const auto collected = collect_template_bindings(document, *object,
        template_scene_identity(document), catalogDocument, bindings, authorSchemas);
    if (!collected)
    {
        feedback_ = "Template references could not be validated: " + collected.error;
        return;
    }
    if (!collected.assured)
    {
        feedback_ = "Template save is blocked until every behavior schema has complete reference declarations.";
        return;
    }
    try
    {
        const auto templateDirectory = document.assetRoot / "templates";
        std::error_code directoryError;
        std::filesystem::create_directories(templateDirectory, directoryError);
        if (directoryError)
            throw std::runtime_error("could not create assets/templates: " + directoryError.message());
        const auto destination = templateDirectory / (template_file_stem(templateName_) + ".pttemplate");
        const std::string contents = make_template_envelope(
            templateName_, *object, collected.bindings).dump(2) + "\n";
        const auto written = write_file_atomically(destination, contents, false);
        if (!written.succeeded())
            feedback_ = written.status == AtomicWriteStatus::Conflict
                ? "A template with that file name already exists. Choose a different name."
                : "Template could not be saved: " + written.message;
        else
            feedback_ = "Template saved to assets/templates/" + destination.filename().string() + ".";
    }
    catch (const std::exception &error)
    {
        feedback_ = std::string("Template could not be saved: ") + error.what();
    }
}

std::vector<Json> InspectorPanel::template_author_bindings(const DocumentSession &document) const
{
    return behaviorFields_.template_author_bindings(document);
}

std::vector<TemplateAuthorSchemaDeclaration> InspectorPanel::template_author_schemas(
    const DocumentSession &document) const
{
    return behaviorFields_.template_author_schemas(document);
}

std::vector<SemanticNode> InspectorPanel::semantic_nodes(const DocumentSession &document,
                                                          const Selection &selection,
                                                          std::string_view scope,
                                                          const BehaviorCatalog *catalog)
{
    std::vector<SemanticNode> nodes;
    if (scope.empty() || !selection.objectId || selection.selectedObjectIds.size() > 1)
    {
        semanticTargets_.clear();
        semanticFocusNode_.reset();
        return nodes;
    }

    const int objectId = *selection.objectId;
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    const Json *object = objectLocation == document.indexes.objectsById.end()
        ? nullptr : resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
    {
        semanticTargets_.clear();
        semanticFocusNode_.reset();
        return nodes;
    }

    const std::string scopeString(scope);
    const std::string selectionNodeId = scopeString + "/selection";
    const std::string selectionKey = std::to_string(document.projectGeneration) + ":" +
        std::to_string(document.tabId.value) + ":" +
        std::to_string(document.documentGeneration) + ":" + std::to_string(objectId) + ":" +
        (selection.componentId ? std::to_string(*selection.componentId) : std::string("-"));
    if (templateNameSelectionKey_ != selectionKey)
    {
        templateNameSelectionKey_ = selectionKey;
        templateName_ = object->value("name", std::string("Object Template"));
    }
    const auto currentFocus = [&](const SemanticTarget &target) {
        return target.objectId == objectId && target.componentId == selection.componentId &&
               target.behaviorOrdinal == selection.behaviorOrdinal;
    };
    if (semanticFocusNode_)
    {
        const auto previous = semanticTargets_.find(*semanticFocusNode_);
        if (previous != semanticTargets_.end() && !currentFocus(previous->second))
            semanticFocusNode_.reset();
    }
    semanticTargets_.clear();

    constexpr std::size_t maximumNodes = 256;
    const auto catalogSnapshot = catalog != nullptr ? catalog->snapshot() : nullptr;
    const Json *catalogDocument = catalogSnapshot ? &catalogSnapshot->document : nullptr;
    nodes.reserve(64);
    const auto append_action = [&](std::string id, std::string name, std::string parentId,
                                   int ownerObjectId, std::optional<int> componentId,
                                   SemanticOperation operation, bool enabled = true) {
        if (nodes.size() >= maximumNodes)
            return;
        SemanticNode node;
        node.id = id;
        node.parentId = std::move(parentId);
        node.role = SemanticRole::Button;
        node.name = std::move(name);
        node.visible = true;
        node.enabled = enabled;
        node.selected = semanticFocusNode_ && *semanticFocusNode_ == id;
        node.focused = node.selected && selection.focusedPanel == FocusedPanel::Inspector;
        node.actions = {"activate"};
        nodes.push_back(std::move(node));
        semanticTargets_.emplace(std::move(id), SemanticTarget{
            ownerObjectId, componentId, std::nullopt, {}, operation});
    };
    const auto append_field = [&](std::string label, std::vector<FieldPathSegment> path,
                                  std::optional<int> componentId, std::string parentId,
                                  std::string_view fallback = "Not set",
                                  std::string_view expectedAssetType = {}) {
        if (nodes.size() >= maximumNodes)
            return;
        const Json *value = address_value(document, objectId, componentId, path);
        const std::string id = scopeString + "/field/" + std::to_string(objectId) + "/" +
            (componentId ? std::to_string(*componentId) : std::string("object")) + "/" +
            semantic_path_token(path);
        SemanticNode node;
        node.id = id;
        node.parentId = std::move(parentId);
        node.name = std::move(label);
        node.value = semantic_value_text(value, fallback);
        if (!expectedAssetType.empty())
        {
            const Json emptyValue;
            const auto reference = inspect_asset_reference(document,
                value != nullptr ? *value : emptyValue, expectedAssetType, resourceIndex_);
            switch (reference.state)
            {
            case ReferenceState::Unset:
                node.value += "; asset not assigned";
                break;
            case ReferenceState::Resolved:
                node.value += "; asset resolved";
                break;
            case ReferenceState::Dangling:
                node.value += "; asset missing";
                node.error = true;
                break;
            case ReferenceState::WrongKind:
                node.value += "; wrong asset type";
                node.error = true;
                break;
            case ReferenceState::Invalid:
                node.value += "; invalid asset reference";
                node.error = true;
                break;
            }
            node.help = reference.message;
        }
        node.visible = true;
        node.selected = semanticFocusNode_ && *semanticFocusNode_ == id;
        node.focused = node.selected && selection.focusedPanel == FocusedPanel::Inspector;
        if (value != nullptr && value->is_boolean())
            node.role = SemanticRole::CheckBox;
        else if (value != nullptr && (value->is_number() || value->is_string()))
            node.role = SemanticRole::TextField;
        else
            node.role = SemanticRole::Group;
        node.actions = {"reveal"};
        nodes.push_back(std::move(node));
        semanticTargets_.emplace(id, SemanticTarget{objectId, componentId, std::nullopt,
                                                     semantic_field_path(path)});
    };
    const auto append_group = [&](std::string label, std::optional<int> componentId,
                                  std::string parentId, std::vector<FieldPathSegment> path,
                                  std::string value = {}) -> std::string {
        if (nodes.size() >= maximumNodes)
            return {};
        SemanticNode node;
        node.id = scopeString + "/group/" + std::to_string(objectId) + "/" +
            (componentId ? std::to_string(*componentId) : std::string("object")) + "/" +
            semantic_path_token(path);
        node.parentId = std::move(parentId);
        node.role = SemanticRole::Group;
        node.name = std::move(label);
        node.value = std::move(value);
        node.visible = true;
        node.selected = semanticFocusNode_ && *semanticFocusNode_ == node.id;
        node.focused = node.selected && selection.focusedPanel == FocusedPanel::Inspector;
        node.actions = {"reveal"};
        const std::string groupId = node.id;
        nodes.push_back(std::move(node));
        semanticTargets_.emplace(groupId, SemanticTarget{objectId, componentId, std::nullopt,
                                                          semantic_field_path(path)});
        return groupId;
    };
    const auto append_point = [&](std::string label, std::string key,
                                  std::optional<int> componentId, std::string parentId,
                                  double defaultX, double defaultY) {
        const std::string groupId = append_group(std::move(label), componentId, std::move(parentId),
            {key}, "X and Y values");
        append_field("X", {key, std::string("x")}, componentId, groupId,
                     Json(defaultX).dump());
        append_field("Y", {key, std::string("y")}, componentId, groupId,
                     Json(defaultY).dump());
    };

    if (selection.componentId)
    {
        const auto componentLocation = document.indexes.componentsById.find(*selection.componentId);
        if (componentLocation == document.indexes.componentsById.end() ||
            componentLocation->second.ownerObjectId != objectId)
            return nodes;
    }
    else
    {
        append_field("Name", {std::string("name")}, std::nullopt, selectionNodeId, "GameObject");
        append_field("Tag", {std::string("tag")}, std::nullopt, selectionNodeId, "");
        append_field("Active", {std::string("active")}, std::nullopt, selectionNodeId, "true");
        append_point("Position", "position", std::nullopt, selectionNodeId, 0.0, 0.0);
        append_point("Scale", "scale", std::nullopt, selectionNodeId, 1.0, 1.0);
        append_field("Rotation", {std::string("rotation")}, std::nullopt,
                     selectionNodeId, "0");
    }

    append_action(scopeString + "/template/save/" + std::to_string(objectId),
        "Save selected GameObject as template", selectionNodeId, objectId, std::nullopt,
        SemanticOperation::SaveTemplate, !templateName_.empty() && !document.assetRoot.empty());

    const auto components = object->find("components");
    if (components == object->end() || !components->is_array())
        return nodes;

    for (const auto &component : *components)
    {
        if (nodes.size() >= maximumNodes || !component.is_object())
            break;
        const auto idField = component.find("id");
        const auto typeField = component.find("type");
        if (idField == component.end() || typeField == component.end() ||
            (!idField->is_number_integer() && !idField->is_number_unsigned()) ||
            (!typeField->is_number_integer() && !typeField->is_number_unsigned()))
            continue;
        const int componentId = json_integer(&*idField, -1);
        const int type = json_integer(&*typeField, -1);
        if (componentId < 0)
            continue;
        const std::string componentIdText = std::to_string(componentId);
        const std::string componentNodeId = scopeString + "/component/" +
            std::to_string(objectId) + "/" + componentIdText;
        SemanticNode componentNode;
        componentNode.id = componentNodeId;
        componentNode.parentId = selectionNodeId;
        componentNode.role = SemanticRole::Group;
        componentNode.name = std::string(component_label(type)) + " component";
        componentNode.value = "Component " + componentIdText;
        componentNode.visible = true;
        componentNode.selected = selection.componentId == componentId;
        componentNode.focused = componentNode.selected && !selection.behaviorOrdinal &&
            selection.focusedPanel == FocusedPanel::Inspector;
        componentNode.actions = {"activate"};
        nodes.push_back(std::move(componentNode));
        semanticTargets_.emplace(componentNodeId, SemanticTarget{objectId, componentId,
            std::nullopt, {}, SemanticOperation::SelectComponent});

        const bool clipboardMatches = componentSettingsClipboard_ &&
            componentSettingsClipboard_->value("componentType", -1) == type;
        append_action(componentNodeId + "/copy-settings", "Copy component settings",
            componentNodeId, objectId, componentId, SemanticOperation::CopySettings,
            catalog != nullptr);
        append_action(componentNodeId + "/paste-settings", "Paste component settings",
            componentNodeId, objectId, componentId, SemanticOperation::PasteSettings,
            clipboardMatches && catalog != nullptr);
        append_action(componentNodeId + "/reset-settings", "Reset component settings",
            componentNodeId, objectId, componentId, SemanticOperation::ResetSettings,
            catalog != nullptr);
        append_action(componentNodeId + "/remove-component", "Remove component",
            componentNodeId, objectId, componentId, SemanticOperation::RemoveComponent,
            true);

        const auto add = [&](std::string_view key, std::string label,
                             std::string_view fallback = "Not set",
                             std::string_view expectedAssetType = {}) {
            append_field(std::move(label), {std::string(key)}, componentId, componentNodeId,
                         fallback, expectedAssetType);
        };
        switch (type)
        {
        case 0: // Animator
            add("currentFrameIndex", "Current frame", "0");
            add("playbackSpeed", "Playback speed", "1");
            add("playing", "Playing", "false");
            add("animationClipFilePath", "Animation Clip", "", "AnimationClip");
            break;
        case 1: // Audio
            add("filePath", "Audio file", "", "Audio");
            add("gain", "Gain", "1");
            add("loopCount", "Loop count", "0");
            add("autoPlay", "Auto Play", "false");
            break;
        case 2: // Camera
            add("width", "Width", "640");
            add("height", "Height", "480");
            if (component.contains("camera"))
                append_point("Legacy camera size", "camera", componentId, componentNodeId,
                             640.0, 480.0);
            break;
        case 3: // Collider
        {
            add("colliderType", "Shape", "1");
            const int shape = json_integer(component.contains("colliderType")
                ? &component.at("colliderType") : nullptr, 1);
            if (shape == 0)
                add("radius", "Radius", "16");
            else if (shape == 1)
            {
                add("width", "Width", "16");
                add("height", "Height", "16");
            }
            if (component.contains("offset"))
                append_point("Offset", "offset", componentId, componentNodeId, 0.0, 0.0);
            add("trigger", "Trigger", "false");
            add("collisionGroup", "Collision group", "1");
            add("collisionMask", "Collision mask", "1");
            break;
        }
        case 4: // Rigidbody
            append_point("Velocity", "velocity", componentId, componentNodeId, 0.0, 0.0);
            append_point("Force", "force", componentId, componentNodeId, 0.0, 0.0);
            add("mass", "Mass", "1");
            add("angularVelocity", "Angular velocity", "0");
            add("torque", "Torque", "0");
            add("friction", "Friction", "0.5");
            add("restitution", "Restitution", "0");
            add("bodyType", "Body type", "0");
            add("gravity", "Gravity", "true");
            break;
        case 5: // Script behavior list
        {
            const auto behaviors = component.find("behaviors");
            if (behaviors == component.end() || !behaviors->is_array())
                break;
            const std::optional<std::size_t> visibleBehaviorOrdinal = behaviors->empty()
                ? std::nullopt
                : (selection.componentId == componentId && selection.behaviorOrdinal &&
                   *selection.behaviorOrdinal < behaviors->size()
                    ? selection.behaviorOrdinal : std::optional<std::size_t>{0});
            for (std::size_t ordinal = 0; ordinal < behaviors->size() &&
                 nodes.size() < maximumNodes; ++ordinal)
            {
                const auto &behavior = (*behaviors)[ordinal];
                const std::string nodeId = componentNodeId + "/behavior/" +
                    std::to_string(ordinal);
                SemanticNode behaviorNode;
                behaviorNode.id = nodeId;
                behaviorNode.parentId = componentNodeId;
                behaviorNode.role = SemanticRole::ListItem;
                behaviorNode.name = behavior.is_object()
                    ? behavior.value("type", std::string("Behavior"))
                    : std::string("Behavior ") + std::to_string(ordinal + 1);
                behaviorNode.value = "Behavior " + std::to_string(ordinal + 1);
                behaviorNode.visible = true;
                behaviorNode.selected = selection.behaviorOrdinal == ordinal;
                behaviorNode.focused = behaviorNode.selected && selection.componentId == componentId &&
                    selection.focusedPanel == FocusedPanel::Behaviors;
                behaviorNode.actions = {"activate"};
                nodes.push_back(std::move(behaviorNode));
                semanticTargets_.emplace(nodeId, SemanticTarget{objectId, componentId,
                    ordinal, {}, SemanticOperation::SelectBehavior});

                const std::string behaviorName = behavior.is_object()
                    ? behavior.value("type", std::string{}) : std::string{};
                const Json *descriptor = nullptr;
                if (catalogDocument != nullptr && !behaviorName.empty())
                {
                    const auto catalogBehaviors = catalogDocument->find("behaviors");
                    if (catalogBehaviors != catalogDocument->end() && catalogBehaviors->is_array())
                        for (const auto &candidate : *catalogBehaviors)
                            if (candidate.is_object() &&
                                candidate.value("name", std::string{}) == behaviorName)
                            {
                                descriptor = &candidate;
                                break;
                            }
                }
                const auto schemaFields = descriptor != nullptr &&
                    descriptor->value("field_schema_available", false)
                    ? descriptor->find("fields") : Json::const_iterator{};
                if (descriptor == nullptr || !descriptor->value("field_schema_available", false) ||
                    schemaFields == descriptor->end() || !schemaFields->is_array())
                    continue;

                for (const auto &field : *schemaFields)
                {
                    if (nodes.size() >= maximumNodes || !field.is_object())
                        break;
                    const std::string fieldName = field.value("name", std::string{});
                    if (fieldName.empty())
                        continue;
                    const auto valueIt = behavior.find(fieldName);
                    const Json *value = valueIt == behavior.end() ? nullptr : &*valueIt;
                    const std::string kind = field.value("kind", std::string("opaque_json"));
                    const std::string fieldId = nodeId + "/field/" +
                        semantic_path_token({std::string(fieldName)});
                    SemanticNode fieldNode;
                    fieldNode.id = fieldId;
                    fieldNode.parentId = nodeId;
                    fieldNode.name = field.value("label", fieldName);
                    fieldNode.value = semantic_value_text(value);
                    fieldNode.visible = visibleBehaviorOrdinal == ordinal;
                    fieldNode.selected = semanticFocusNode_ && *semanticFocusNode_ == fieldId;
                    fieldNode.focused = fieldNode.selected &&
                        selection.componentId == componentId &&
                        selection.behaviorOrdinal == ordinal &&
                        selection.focusedPanel == FocusedPanel::Behaviors;
                    if (kind == "boolean")
                        fieldNode.role = SemanticRole::CheckBox;
                    else if (kind == "vector2" || kind == "rectangle" || kind == "array" ||
                             kind == "optional" || kind == "opaque_json")
                        fieldNode.role = SemanticRole::Group;
                    else
                        fieldNode.role = SemanticRole::TextField;

                    if (kind == "asset_reference")
                    {
                        const Json emptyValue;
                        const auto reference = inspect_asset_reference(document,
                            value != nullptr ? *value : emptyValue,
                            field.value("target", std::string("Asset")), resourceIndex_);
                        switch (reference.state)
                        {
                        case ReferenceState::Unset:
                            fieldNode.value += "; asset not assigned";
                            break;
                        case ReferenceState::Resolved:
                            fieldNode.value += "; asset resolved";
                            break;
                        case ReferenceState::Dangling:
                            fieldNode.value += "; asset missing";
                            fieldNode.error = true;
                            break;
                        case ReferenceState::WrongKind:
                            fieldNode.value += "; wrong asset type";
                            fieldNode.error = true;
                            break;
                        case ReferenceState::Invalid:
                            fieldNode.value += "; invalid asset reference";
                            fieldNode.error = true;
                            break;
                        }
                        fieldNode.help = reference.message;
                    }
                    else if (kind == "object_reference")
                    {
                        const Json emptyValue;
                        const auto reference = inspect_scene_reference(document,
                            value != nullptr ? *value : emptyValue,
                            field.value("target", std::string("GameObject")));
                        switch (reference.state)
                        {
                        case ReferenceState::Unset:
                            fieldNode.value += "; target not assigned";
                            break;
                        case ReferenceState::Resolved:
                            fieldNode.value += "; " + reference.label;
                            break;
                        case ReferenceState::Dangling:
                            fieldNode.value += "; target missing";
                            fieldNode.error = true;
                            break;
                        case ReferenceState::WrongKind:
                            fieldNode.value += "; target has wrong type";
                            fieldNode.error = true;
                            break;
                        case ReferenceState::Invalid:
                            fieldNode.value += "; invalid target";
                            fieldNode.error = true;
                            break;
                        }
                        fieldNode.help = reference.label;
                    }
                    fieldNode.actions = {"reveal"};
                    nodes.push_back(std::move(fieldNode));
                    semanticTargets_.emplace(fieldId, SemanticTarget{objectId, componentId,
                                                                       ordinal, fieldName});
                }
            }
            break;
        }
        case 6: // Sprite
            add("textureFilePath", "Texture", "", "Texture");
            add("flip", "Flip", "0");
            add("width", "Width", "16");
            add("height", "Height", "16");
            add("sourceRectEnabled", "Use source rectangle", "false");
            if (component.contains("sourceRect"))
            {
                const std::string groupId = append_group("Source rectangle", componentId,
                    componentNodeId, {std::string("sourceRect")}, "X, Y, width, height");
                for (const auto &[key, label] : std::array{
                         std::pair{"x", "Source X"}, std::pair{"y", "Source Y"},
                         std::pair{"w", "Source width"}, std::pair{"h", "Source height"}})
                    append_field(label, {std::string("sourceRect"), std::string(key)},
                                 componentId, groupId, "0");
            }
            break;
        default:
            break;
        }
    }
    if (semanticFocusNode_ && !semanticTargets_.contains(*semanticFocusNode_))
        semanticFocusNode_.reset();
    return nodes;
}

bool InspectorPanel::dispatch_semantic_action(DocumentSession &document,
                                              SelectionModel &selection,
                                              const SemanticAction &action,
                                              std::string_view scope,
                                              const BehaviorCatalog *catalog,
                                              EditCommandService *commands)
{
    if (scope.empty() || (action.action != "activate" && action.action != "reveal"))
        return false;
    (void)semantic_nodes(document, selection.current(), scope, catalog);
    const auto target = semanticTargets_.find(action.nodeId);
    if (target == semanticTargets_.end())
        return false;
    const auto &semanticTarget = target->second;
    const bool reveal = semanticTarget.operation == SemanticOperation::Reveal;
    if ((reveal && action.action != "reveal") || (!reveal && action.action != "activate"))
        return false;
    if (!document.indexes.objectsById.contains(semanticTarget.objectId))
        return false;
    if (semanticTarget.componentId)
    {
        const auto component = document.indexes.componentsById.find(*semanticTarget.componentId);
        if (component == document.indexes.componentsById.end() ||
            component->second.ownerObjectId != semanticTarget.objectId)
            return false;
    }
    const auto panel = semanticTarget.behaviorOrdinal
        ? FocusedPanel::Behaviors : FocusedPanel::Inspector;
    Selection before = selection.current();
    Selection next{semanticTarget.objectId, semanticTarget.componentId,
                   semanticTarget.behaviorOrdinal, panel,
                   WindowHostGeneration{hostGeneration_}};

    switch (semanticTarget.operation)
    {
    case SemanticOperation::Reveal:
        selection.set(document, next);
        if (semanticTarget.behaviorOrdinal)
            searchQuery_ = semanticTarget.fieldPath;
        else
            reveal_field(semanticTarget.objectId, semanticTarget.componentId,
                         semanticTarget.fieldPath);
        break;
    case SemanticOperation::SelectComponent:
    case SemanticOperation::SelectBehavior:
        selection.set(document, std::move(next));
        break;
    case SemanticOperation::CopySettings:
    case SemanticOperation::PasteSettings:
    case SemanticOperation::ResetSettings:
    {
        if (commands == nullptr || catalog == nullptr || !semanticTarget.componentId)
            return false;
        const Json *component = address_owner(document, semanticTarget.objectId,
                                               semanticTarget.componentId);
        if (component == nullptr)
            return false;
        const auto current = selection.current();
        const SelectionState selectionState{current, next};
        if (semanticTarget.operation == SemanticOperation::CopySettings)
        {
            std::string error;
            auto copied = copy_component_settings(*component, *catalog, error);
            if (!copied)
            {
                feedback_ = std::move(error);
                return true;
            }
            componentSettingsClipboard_ = std::move(*copied);
            feedback_.clear();
            selection.set(document, std::move(next));
        }
        else
        {
            CommandResult result;
            if (semanticTarget.operation == SemanticOperation::PasteSettings)
            {
                if (!componentSettingsClipboard_ ||
                    componentSettingsClipboard_->value("componentType", -1) !=
                        component->value("type", -2))
                    return false;
                result = paste_component_settings(document, *commands,
                    semanticTarget.objectId, *semanticTarget.componentId,
                    *componentSettingsClipboard_, *catalog, selectionState);
            }
            else
                result = reset_component_settings(document, *commands,
                    semanticTarget.objectId, *semanticTarget.componentId, *catalog, selectionState);
            if (result.status == CommandStatus::Rejected ||
                result.status == CommandStatus::StaleAddress)
            {
                feedback_ = result.message;
                return true;
            }
            feedback_.clear();
            selection.set(document, next);
            if (result.status == CommandStatus::Changed)
                commands->set_last_selection_state({before, selection.current()});
        }
        break;
    }
    case SemanticOperation::RemoveComponent:
        if (commands == nullptr || !semanticTarget.componentId)
            return false;
        {
            const Selection current = selection.current();
            const auto removed = commands->remove_component(document, semanticTarget.objectId,
                                                            *semanticTarget.componentId);
            if (removed.status == CommandStatus::Changed)
            {
                selection.repair(document);
                commands->set_last_selection_state({current, selection.current()});
                feedback_.clear();
            }
            else if (removed.status == CommandStatus::Rejected)
                feedback_ = removed.message;
        }
        break;
    case SemanticOperation::SaveTemplate:
        if (catalog == nullptr)
            return false;
        save_selected_template(document, semanticTarget.objectId, *catalog);
        selection.set(document, std::move(next));
        break;
    }
    semanticFocusNode_ = action.nodeId;
    return true;
}

void InspectorPanel::reveal_field(int objectId, std::optional<int> componentId,
                                 std::string fieldPath)
{
    revealObjectId_ = objectId;
    revealComponentId_ = componentId;
    revealPath_ = diagnostic_field_id(fieldPath);
}

void InspectorPanel::set_host_generation(std::uint64_t generation) noexcept
{
    if (hostGeneration_ == generation)
        return;
    hostGeneration_ = generation;
    integerDrafts_.clear();
}

void InspectorPanel::draw_search_bar()
{
    std::array<char, 256> buffer{};
    const std::size_t length = std::min(searchQuery_.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), searchQuery_.data(), length);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##inspector-search", "Search fields, types, path, or ID",
                                 buffer.data(), buffer.size()))
        searchQuery_ = buffer.data();
}

bool InspectorPanel::field_matches_search(const DocumentSession &document, int objectId,
                                          std::optional<int> componentId,
                                          const std::vector<FieldPathSegment> &path,
                                          std::string_view label) const
{
    if (searchQuery_.empty())
        return true;
    std::string candidate(label);
    candidate += stable_field_id(path);
    candidate += " #" + std::to_string(objectId);
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (objectLocation != document.indexes.objectsById.end())
    {
        const Json *object = resolve_object(document.root, objectLocation->second);
        if (object != nullptr)
        {
            const auto name = object->find("name");
            const auto tag = object->find("tag");
            if (name != object->end() && name->is_string())
                candidate += " " + name->get<std::string>();
            if (tag != object->end() && tag->is_string())
                candidate += " " + tag->get<std::string>();
            int currentId = objectId;
            while (true)
            {
                const auto parent = document.indexes.parentById.find(currentId);
                if (parent == document.indexes.parentById.end() || !parent->second)
                    break;
                currentId = *parent->second;
                const auto ancestorLocation = document.indexes.objectsById.find(currentId);
                const Json *ancestor = ancestorLocation == document.indexes.objectsById.end()
                    ? nullptr : resolve_object(document.root, ancestorLocation->second);
                if (ancestor != nullptr && ancestor->contains("name") &&
                    ancestor->at("name").is_string())
                    candidate += " " + ancestor->at("name").get<std::string>();
            }
        }
    }
    if (componentId)
    {
        const auto componentLocation = document.indexes.componentsById.find(*componentId);
        if (componentLocation != document.indexes.componentsById.end())
        {
            const auto ownerLocation = document.indexes.objectsById.find(
                componentLocation->second.ownerObjectId);
            const Json *owner = ownerLocation == document.indexes.objectsById.end()
                ? nullptr : resolve_object(document.root, ownerLocation->second);
            if (owner != nullptr)
            {
                const auto components = owner->find("components");
                if (components != owner->end() && components->is_array() &&
                    componentLocation->second.componentIndex < components->size())
                {
                    const auto &component = (*components)[componentLocation->second.componentIndex];
                    const auto type = component.find("type");
                    candidate += " " + std::string(component_label(
                        json_integer(type == component.end() ? nullptr : &*type, -1)));
                }
            }
        }
    }
    return discovery_query_matches(searchQuery_, candidate);
}

bool InspectorPanel::consume_reveal_target(int objectId, std::optional<int> componentId,
                                           const std::vector<FieldPathSegment> &path)
{
    if (!revealObjectId_ || *revealObjectId_ != objectId ||
        revealComponentId_ != componentId || revealPath_.empty())
        return false;
    const std::string currentPath = stable_field_id(path);
    const bool related = currentPath == revealPath_ ||
                         currentPath.starts_with(revealPath_ + "/") ||
                         revealPath_.starts_with(currentPath + "/");
    if (!related)
        return false;
    revealObjectId_.reset();
    revealComponentId_.reset();
    revealPath_.clear();
    return true;
}

bool InspectorPanel::set(DocumentSession &document, EditCommandService &commands, int objectId,
                         std::optional<int> componentId, std::vector<FieldPathSegment> path, Json value)
{
    PropertyAddress address = address_for_document(document, objectId, componentId,
                                                   std::nullopt, std::move(path));
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
    if (!field_matches_search(document, objectId, componentId, path, label))
        return false;
    const Json *stored = address_value(document, objectId, componentId, path);
    bool value = stored != nullptr && stored->is_boolean() ? stored->get<bool>() : defaultValue;
    const bool reveal = consume_reveal_target(objectId, componentId, path);
    begin_field(label, path);
    if (reveal)
        ImGui::SetKeyboardFocusHere();
    const bool changed = ImGui::Checkbox("##value", &value);
    if (reveal)
        ImGui::SetScrollHereY(0.5f);
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
    if (!field_matches_search(document, objectId, componentId, path, label))
        return false;
    const Json *stored = address_value(document, objectId, componentId, path);
    float value = json_float(stored, defaultValue);
    const bool reveal = consume_reveal_target(objectId, componentId, path);
    begin_field(label, path);
    if (reveal)
        ImGui::SetKeyboardFocusHere();
    const bool changed = ImGui::InputFloat("##value", &value, 0.0f, 0.0f, "%.9g",
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    if (reveal)
        ImGui::SetScrollHereY(0.5f);
    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
    end_field();
    if (!commit)
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
    if (!field_matches_search(document, objectId, componentId, path, label))
        return false;
    const Json *stored = address_value(document, objectId, componentId, path);
    const int storedValue = json_integer(stored, defaultValue);
    const bool reveal = consume_reveal_target(objectId, componentId, path);
    begin_field(label, path);
    if (reveal)
        ImGui::SetKeyboardFocusHere();
    const auto widgetId = static_cast<std::uint32_t>(ImGui::GetID("##value"));
    auto [draftIt, inserted] = integerDrafts_.try_emplace(widgetId, IntegerDraft{storedValue, false});
    if (!inserted && !draftIt->second.wasActive && ImGui::GetActiveID() != widgetId)
        draftIt->second.value = storedValue;
    const bool changed = ImGui::InputInt("##value", &draftIt->second.value, 1, 100,
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (reveal)
        ImGui::SetScrollHereY(0.5f);
    const bool itemActive = ImGui::IsItemActive();
    const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                              ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    const bool cancel = itemActive && ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool commit = !cancel && (ImGui::IsItemDeactivatedAfterEdit() ||
                                    (changed && (enterPressed || !itemActive)));
    const int value = draftIt->second.value;
    draftIt->second.wasActive = itemActive;
    if (cancel)
    {
        integerDrafts_.erase(draftIt);
    }
    end_field();
    if (!commit)
        return false;
    if ((minimum && value < *minimum) || (maximum && value > *maximum))
    {
        feedback_ = std::string(label) + " is outside the supported range.";
        integerDrafts_.erase(widgetId);
        return false;
    }
    integerDrafts_.erase(widgetId);
    return set(document, commands, objectId, componentId, std::move(path), value);
}

bool InspectorPanel::draw_string(DocumentSession &document, EditCommandService &commands, int objectId,
                                 std::optional<int> componentId, std::vector<FieldPathSegment> path,
                                 const char *label, std::string defaultValue)
{
    if (!field_matches_search(document, objectId, componentId, path, label))
        return false;
    const Json *stored = address_value(document, objectId, componentId, path);
    std::string value = stored != nullptr && stored->is_string() ? stored->get<std::string>() : std::move(defaultValue);
    if (value.size() >= 4096)
    {
        ImGui::TextDisabled("%s is too long for the inline editor.", label);
        return false;
    }
    std::array<char, 4096> buffer{};
    std::memcpy(buffer.data(), value.data(), value.size());
    const bool reveal = consume_reveal_target(objectId, componentId, path);
    begin_field(label, path);
    if (reveal)
        ImGui::SetKeyboardFocusHere();
    const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    if (reveal)
        ImGui::SetScrollHereY(0.5f);
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
    if (!field_matches_search(document, objectId, componentId, path, label))
        return false;
    const int current = json_integer(address_value(document, objectId, componentId, path), defaultValue);
    const char *preview = "Unsupported";
    for (const auto &[value, name] : values)
        if (value == current)
            preview = name;
    const bool reveal = consume_reveal_target(objectId, componentId, path);
    begin_field(label, path);
    if (reveal)
        ImGui::SetKeyboardFocusHere();
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
    if (reveal)
        ImGui::SetScrollHereY(0.5f);
    end_field();
    return changed;
}

bool InspectorPanel::draw_compact_vector2(
    DocumentSession &document, EditCommandService &commands, int objectId,
    std::optional<int> componentId, std::vector<FieldPathSegment> basePath, const char *label,
    float defaultX, float defaultY, std::optional<float> minimum, std::optional<float> maximum,
    float baseStep, float snapIncrement)
{
    if (!field_matches_search(document, objectId, componentId, basePath, label))
        return false;

    auto pathX = basePath;
    pathX.emplace_back(std::string("x"));
    auto pathY = basePath;
    pathY.emplace_back(std::string("y"));

    const Json *storedX = address_value(document, objectId, componentId, pathX);
    const Json *storedY = address_value(document, objectId, componentId, pathY);
    float valX = json_float(storedX, defaultX);
    float valY = json_float(storedY, defaultY);

    const float labelWidth = 64.0f;
    const float axisTagWidth = 14.0f;
    const float totalAvail = ImGui::GetContentRegionAvail().x;
    const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    const float fieldsWidth = std::max(60.0f, totalAvail - labelWidth - itemSpacingX);
    const float subColWidth = (fieldsWidth - itemSpacingX) * 0.5f;
    const float inputWidth = std::max(20.0f, subColWidth - axisTagWidth - 2.0f);

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(labelWidth + itemSpacingX);

    bool anyCommitted = false;

    auto draw_axis = [&](const char *axisName, const char *btnId, const ImVec4 &axisColor,
                         float &axisVal, const std::vector<FieldPathSegment> &path, float defVal) {
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const float frameHeight = ImGui::GetFrameHeight();

        ImGui::InvisibleButton(btnId, ImVec2(axisTagWidth, frameHeight));
        const bool hovered = ImGui::IsItemHovered();
        const bool activated = ImGui::IsItemActivated();
        const bool active = ImGui::IsItemActive();
        const bool deactivated = ImGui::IsItemDeactivated();

        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddText(ImVec2(cursorPos.x + 2.0f, cursorPos.y + (frameHeight - ImGui::GetFontSize()) * 0.5f),
                          ImGui::GetColorU32(axisColor), axisName);

        if (activated)
        {
            scrubState_.active = true;
            scrubState_.startValue = axisVal;
            scrubState_.currentValue = axisVal;
            scrubState_.objectId = objectId;
            scrubState_.componentId = componentId;
            scrubState_.path = path;
            scrubState_.baseStep = baseStep;
            scrubState_.snapIncrement = snapIncrement;
            scrubState_.minimum = minimum;
            scrubState_.maximum = maximum;
        }
        else if (active && scrubState_.active)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                Json *mut = mutable_address_value(document, objectId, componentId, path);
                if (mut)
                {
                    *mut = scrubState_.startValue;
                    ++document.valueRevision;
                }
                scrubState_.active = false;
            }
            else
            {
                const ImGuiIO &io = ImGui::GetIO();
                const float deltaX = io.MouseDelta.x;
                if (deltaX != 0.0f)
                {
                    float mult = io.KeyShift ? 10.0f : (io.KeyAlt ? 0.1f : 1.0f);
                    scrubState_.currentValue += deltaX * baseStep * mult;
                    if (io.KeyCtrl && snapIncrement > 0.0f)
                        scrubState_.currentValue = std::round(scrubState_.currentValue / snapIncrement) * snapIncrement;
                    if (minimum && scrubState_.currentValue < *minimum)
                        scrubState_.currentValue = *minimum;
                    if (maximum && scrubState_.currentValue > *maximum)
                        scrubState_.currentValue = *maximum;

                    axisVal = scrubState_.currentValue;
                    Json *mut = mutable_address_value(document, objectId, componentId, path);
                    if (mut)
                    {
                        *mut = scrubState_.currentValue;
                        ++document.valueRevision;
                    }
                }
            }
        }
        else if (deactivated && scrubState_.active)
        {
            Json *mut = mutable_address_value(document, objectId, componentId, path);
            if (mut)
                *mut = scrubState_.startValue;
            if (scrubState_.currentValue != scrubState_.startValue)
            {
                set(document, commands, objectId, componentId, path, scrubState_.currentValue);
                anyCommitted = true;
            }
            scrubState_.active = false;
        }

        ImGui::SameLine(0.0f, 2.0f);
        ImGui::PushID(stable_field_id(path).c_str());
        ImGui::SetNextItemWidth(inputWidth);
        const bool reveal = consume_reveal_target(objectId, componentId, path);
        if (reveal)
            ImGui::SetKeyboardFocusHere();
        const bool changed = ImGui::InputFloat("##value", &axisVal, 0.0f, 0.0f, "%.2f",
                                               ImGuiInputTextFlags_EnterReturnsTrue);
        if (reveal)
            ImGui::SetScrollHereY(0.5f);
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
        ImGui::PopID();

        if (commit)
        {
            if (!std::isfinite(axisVal) || (minimum && axisVal < *minimum) || (maximum && axisVal > *maximum))
            {
                feedback_ = std::string(label) + " " + axisName + " is outside the supported range.";
            }
            else
            {
                set(document, commands, objectId, componentId, path, axisVal);
                anyCommitted = true;
            }
        }
    };

    draw_axis("X", "##scrub_x", ImVec4(0.9f, 0.35f, 0.35f, 1.0f), valX, pathX, defaultX);
    ImGui::SameLine(0.0f, itemSpacingX);
    draw_axis("Y", "##scrub_y", ImVec4(0.4f, 0.85f, 0.4f, 1.0f), valY, pathY, defaultY);

    return anyCommitted;
}

bool InspectorPanel::draw_compact_rotation(
    DocumentSession &document, EditCommandService &commands, int objectId,
    std::optional<int> componentId, std::vector<FieldPathSegment> path, const char *label,
    float defaultValue, float baseStep, float snapIncrement)
{
    if (!field_matches_search(document, objectId, componentId, path, label))
        return false;

    const Json *stored = address_value(document, objectId, componentId, path);
    float val = json_float(stored, defaultValue);

    const float labelWidth = 64.0f;
    const float totalAvail = ImGui::GetContentRegionAvail().x;
    const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    const float inputWidth = std::max(60.0f, totalAvail - labelWidth - itemSpacingX);

    ImGui::AlignTextToFramePadding();
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const float frameHeight = ImGui::GetFrameHeight();
    ImGui::InvisibleButton("##scrub_rot", ImVec2(labelWidth, frameHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool activated = ImGui::IsItemActivated();
    const bool active = ImGui::IsItemActive();
    const bool deactivated = ImGui::IsItemDeactivated();

    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddText(ImVec2(cursorPos.x, cursorPos.y + (frameHeight - ImGui::GetFontSize()) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), label);

    if (activated)
    {
        scrubState_.active = true;
        scrubState_.startValue = val;
        scrubState_.currentValue = val;
        scrubState_.objectId = objectId;
        scrubState_.componentId = componentId;
        scrubState_.path = path;
        scrubState_.baseStep = baseStep;
        scrubState_.snapIncrement = snapIncrement;
        scrubState_.minimum = std::nullopt;
        scrubState_.maximum = std::nullopt;
    }
    else if (active && scrubState_.active)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            Json *mut = mutable_address_value(document, objectId, componentId, path);
            if (mut)
            {
                *mut = scrubState_.startValue;
                ++document.valueRevision;
            }
            scrubState_.active = false;
        }
        else
        {
            const ImGuiIO &io = ImGui::GetIO();
            const float deltaX = io.MouseDelta.x;
            if (deltaX != 0.0f)
            {
                float mult = io.KeyShift ? 10.0f : (io.KeyAlt ? 0.1f : 1.0f);
                scrubState_.currentValue += deltaX * baseStep * mult;
                if (io.KeyCtrl && snapIncrement > 0.0f)
                    scrubState_.currentValue = std::round(scrubState_.currentValue / snapIncrement) * snapIncrement;
                val = scrubState_.currentValue;
                Json *mut = mutable_address_value(document, objectId, componentId, path);
                if (mut)
                {
                    *mut = scrubState_.currentValue;
                    ++document.valueRevision;
                }
            }
        }
    }
    else if (deactivated && scrubState_.active)
    {
        Json *mut = mutable_address_value(document, objectId, componentId, path);
        if (mut)
            *mut = scrubState_.startValue;
        if (scrubState_.currentValue != scrubState_.startValue)
        {
            set(document, commands, objectId, componentId, path, scrubState_.currentValue);
        }
        scrubState_.active = false;
        return true;
    }

    ImGui::SameLine(labelWidth + itemSpacingX);
    ImGui::PushID(stable_field_id(path).c_str());
    ImGui::SetNextItemWidth(inputWidth);
    const bool reveal = consume_reveal_target(objectId, componentId, path);
    if (reveal)
        ImGui::SetKeyboardFocusHere();
    const bool changed = ImGui::InputFloat("##value", &val, 0.0f, 0.0f, "%.2f deg",
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    if (reveal)
        ImGui::SetScrollHereY(0.5f);
    const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
    ImGui::PopID();

    if (commit)
    {
        if (!std::isfinite(val))
        {
            feedback_ = std::string(label) + " is outside the supported range.";
            return false;
        }
        return set(document, commands, objectId, componentId, path, val);
    }
    return false;
}

void InspectorPanel::draw_common_compact_transform(
    DocumentSession &document, SelectionModel &selection, EditCommandService &commands,
    const std::vector<int> &objectIds)
{
    const auto common_values = [&](const std::vector<FieldPathSegment> &path)
        -> std::optional<std::vector<float>> {
        std::vector<float> values;
        values.reserve(objectIds.size());
        for (const int objectId : objectIds)
        {
            const Json *value = address_value(document, objectId, std::nullopt, path);
            if (value == nullptr || !value->is_number() || !std::isfinite(value->get<double>()))
                return std::nullopt;
            values.push_back(json_float(value, 0.0f));
        }
        return values;
    };

    const auto commit_common = [&](const std::vector<FieldPathSegment> &path, float value) {
        std::vector<BatchPropertyEdit> edits;
        edits.reserve(objectIds.size());
        for (const int objectId : objectIds)
            edits.push_back({address_for_document(document, objectId, std::nullopt, std::nullopt, path),
                             PropertyValue::of(Json(value))});
        const Selection current = selection.current();
        const auto result = commands.apply_batch(document, edits, {current, current});
        if (result.status == CommandStatus::Rejected || result.status == CommandStatus::StaleAddress)
            feedback_ = result.message;
        else
            feedback_.clear();
    };

    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        return;

    const float labelWidth = 64.0f;
    const float axisTagWidth = 14.0f;
    const float totalAvail = ImGui::GetContentRegionAvail().x;
    const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    const float fieldsWidth = std::max(60.0f, totalAvail - labelWidth - itemSpacingX);
    const float subColWidth = (fieldsWidth - itemSpacingX) * 0.5f;
    const float inputWidth = std::max(20.0f, subColWidth - axisTagWidth - 2.0f);

    auto draw_common_vec2 = [&](const char *label, const std::string &baseProp,
                                float defX, float defY, std::optional<float> minimum,
                                float baseStep, float snapIncrement) {
        std::vector<FieldPathSegment> pathX = {baseProp, std::string("x")};
        std::vector<FieldPathSegment> pathY = {baseProp, std::string("y")};
        auto valsX = common_values(pathX);
        auto valsY = common_values(pathY);
        if (!valsX || !valsY)
            return;

        const bool mixedX = std::any_of(valsX->begin() + 1, valsX->end(), [&](float v) {
            return std::abs(v - valsX->front()) > 1e-5f;
        });
        const bool mixedY = std::any_of(valsY->begin() + 1, valsY->end(), [&](float v) {
            return std::abs(v - valsY->front()) > 1e-5f;
        });

        float valX = valsX->front();
        float valY = valsY->front();

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(labelWidth + itemSpacingX);

        auto draw_common_axis = [&](const char *axisName, const char *btnId, const ImVec4 &axisColor,
                                    float &val, bool mixed,
                                    const std::vector<float> &allVals,
                                    const std::vector<FieldPathSegment> &path) {
            const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            const float frameHeight = ImGui::GetFrameHeight();
            ImGui::InvisibleButton(btnId, ImVec2(axisTagWidth, frameHeight));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->AddText(ImVec2(cursorPos.x + 2.0f, cursorPos.y + (frameHeight - ImGui::GetFontSize()) * 0.5f),
                              ImGui::GetColorU32(axisColor), axisName);

            if (ImGui::IsItemActivated())
            {
                scrubState_.active = true;
                scrubState_.startValue = val;
                scrubState_.currentValue = val;
                scrubState_.targetObjectIds = objectIds;
                scrubState_.multiStartValues = allVals;
                scrubState_.path = path;
                scrubState_.baseStep = baseStep;
                scrubState_.snapIncrement = snapIncrement;
                scrubState_.minimum = minimum;
            }
            else if (ImGui::IsItemActive() && scrubState_.active)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    for (std::size_t i = 0; i < objectIds.size(); ++i)
                    {
                        Json *mut = mutable_address_value(document, objectIds[i], std::nullopt, path);
                        if (mut)
                            *mut = scrubState_.multiStartValues[i];
                    }
                    ++document.valueRevision;
                    scrubState_.active = false;
                }
                else
                {
                    const ImGuiIO &io = ImGui::GetIO();
                    const float deltaX = io.MouseDelta.x;
                    if (deltaX != 0.0f)
                    {
                        float mult = io.KeyShift ? 10.0f : (io.KeyAlt ? 0.1f : 1.0f);
                        scrubState_.currentValue += deltaX * baseStep * mult;
                        if (io.KeyCtrl && snapIncrement > 0.0f)
                            scrubState_.currentValue = std::round(scrubState_.currentValue / snapIncrement) * snapIncrement;
                        if (minimum && scrubState_.currentValue < *minimum)
                            scrubState_.currentValue = *minimum;
                        val = scrubState_.currentValue;
                        for (std::size_t i = 0; i < objectIds.size(); ++i)
                        {
                            Json *mut = mutable_address_value(document, objectIds[i], std::nullopt, path);
                            if (mut)
                                *mut = scrubState_.multiStartValues[i] + (scrubState_.currentValue - scrubState_.startValue);
                        }
                        ++document.valueRevision;
                    }
                }
            }
            else if (ImGui::IsItemDeactivated() && scrubState_.active)
            {
                for (std::size_t i = 0; i < objectIds.size(); ++i)
                {
                    Json *mut = mutable_address_value(document, objectIds[i], std::nullopt, path);
                    if (mut)
                        *mut = scrubState_.multiStartValues[i];
                }
                const float totalDelta = scrubState_.currentValue - scrubState_.startValue;
                if (std::abs(totalDelta) > 1e-5f)
                {
                    std::vector<BatchPropertyEdit> edits;
                    edits.reserve(objectIds.size());
                    for (std::size_t i = 0; i < objectIds.size(); ++i)
                    {
                        float finalVal = scrubState_.multiStartValues[i] + totalDelta;
                        if (minimum && finalVal < *minimum)
                            finalVal = *minimum;
                        edits.push_back({address_for_document(document, objectIds[i], std::nullopt, std::nullopt, path),
                                         PropertyValue::of(Json(finalVal))});
                    }
                    const Selection current = selection.current();
                    commands.apply_batch(document, edits, {current, current});
                }
                scrubState_.active = false;
            }

            ImGui::SameLine(0.0f, 2.0f);
            ImGui::PushID(stable_field_id(path).c_str());
            ImGui::SetNextItemWidth(inputWidth);
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
            const bool changed = ImGui::InputFloat("##value", &val, 0.0f, 0.0f, mixed ? " — " : "%.2f",
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopItemFlag();
            const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
            ImGui::PopID();

            if (commit)
            {
                if (!std::isfinite(val) || (minimum && val < *minimum))
                    feedback_ = std::string(label) + " " + axisName + " is outside the supported range.";
                else
                    commit_common(path, val);
            }
        };

        draw_common_axis("X", "##common_scrub_x", ImVec4(0.9f, 0.35f, 0.35f, 1.0f), valX, mixedX, *valsX, pathX);
        ImGui::SameLine(0.0f, itemSpacingX);
        draw_common_axis("Y", "##common_scrub_y", ImVec4(0.4f, 0.85f, 0.4f, 1.0f), valY, mixedY, *valsY, pathY);
    };

    draw_common_vec2("Position", "position", 0.0f, 0.0f, std::nullopt, 0.1f, 0.5f);

    std::vector<FieldPathSegment> rotPath = {std::string("rotation")};
    auto valsRot = common_values(rotPath);
    if (valsRot)
    {
        const bool mixedRot = std::any_of(valsRot->begin() + 1, valsRot->end(), [&](float v) {
            return std::abs(v - valsRot->front()) > 1e-5f;
        });
        float valRot = valsRot->front();
        ImGui::AlignTextToFramePadding();
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const float frameHeight = ImGui::GetFrameHeight();
        ImGui::InvisibleButton("##scrub_rot", ImVec2(labelWidth, frameHeight));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->AddText(ImVec2(cursorPos.x, cursorPos.y + (frameHeight - ImGui::GetFontSize()) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), "Rotation");

        if (ImGui::IsItemActivated())
        {
            scrubState_.active = true;
            scrubState_.startValue = valRot;
            scrubState_.currentValue = valRot;
            scrubState_.targetObjectIds = objectIds;
            scrubState_.multiStartValues = *valsRot;
            scrubState_.path = rotPath;
            scrubState_.baseStep = 1.0f;
            scrubState_.snapIncrement = 15.0f;
        }
        else if (ImGui::IsItemActive() && scrubState_.active)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                for (std::size_t i = 0; i < objectIds.size(); ++i)
                {
                    Json *mut = mutable_address_value(document, objectIds[i], std::nullopt, rotPath);
                    if (mut)
                        *mut = scrubState_.multiStartValues[i];
                }
                ++document.valueRevision;
                scrubState_.active = false;
            }
            else
            {
                const ImGuiIO &io = ImGui::GetIO();
                const float deltaX = io.MouseDelta.x;
                if (deltaX != 0.0f)
                {
                    float mult = io.KeyShift ? 10.0f : (io.KeyAlt ? 0.1f : 1.0f);
                    scrubState_.currentValue += deltaX * 1.0f * mult;
                    if (io.KeyCtrl)
                        scrubState_.currentValue = std::round(scrubState_.currentValue / 15.0f) * 15.0f;
                    valRot = scrubState_.currentValue;
                    for (std::size_t i = 0; i < objectIds.size(); ++i)
                    {
                        Json *mut = mutable_address_value(document, objectIds[i], std::nullopt, rotPath);
                        if (mut)
                            *mut = scrubState_.multiStartValues[i] + (scrubState_.currentValue - scrubState_.startValue);
                    }
                    ++document.valueRevision;
                }
            }
        }
        else if (ImGui::IsItemDeactivated() && scrubState_.active)
        {
            for (std::size_t i = 0; i < objectIds.size(); ++i)
            {
                Json *mut = mutable_address_value(document, objectIds[i], std::nullopt, rotPath);
                if (mut)
                    *mut = scrubState_.multiStartValues[i];
            }
            const float totalDelta = scrubState_.currentValue - scrubState_.startValue;
            if (std::abs(totalDelta) > 1e-5f)
            {
                std::vector<BatchPropertyEdit> edits;
                edits.reserve(objectIds.size());
                for (std::size_t i = 0; i < objectIds.size(); ++i)
                {
                    edits.push_back({address_for_document(document, objectIds[i], std::nullopt, std::nullopt, rotPath),
                                     PropertyValue::of(Json(scrubState_.multiStartValues[i] + totalDelta))});
                }
                const Selection current = selection.current();
                commands.apply_batch(document, edits, {current, current});
            }
            scrubState_.active = false;
        }

        ImGui::SameLine(labelWidth + itemSpacingX);
        ImGui::PushID(stable_field_id(rotPath).c_str());
        ImGui::SetNextItemWidth(std::max(60.0f, totalAvail - labelWidth - itemSpacingX));
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedRot);
        const bool changed = ImGui::InputFloat("##value", &valRot, 0.0f, 0.0f, mixedRot ? " — " : "%.2f deg",
                                               ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemFlag();
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
        ImGui::PopID();
        if (commit)
        {
            if (!std::isfinite(valRot))
                feedback_ = "Rotation is outside the supported range.";
            else
                commit_common(rotPath, valRot);
        }
    }

    draw_common_vec2("Scale", "scale", 1.0f, 1.0f, 0.0001f, 0.05f, 0.1f);
}

void InspectorPanel::draw(DocumentSession &document, SelectionModel &selection, EditCommandService &commands,
                          const BehaviorCatalog &catalog, const AssetBrowseCallback &browse,
                          ResourceIndex *resourceIndex)
{
    resourceIndex_ = resourceIndex;
    selection.repair(document);
    if (selection.current().selectedObjectIds.size() > 1)
    {
        integerDrafts_.clear();
        lastSelectionKey_.clear();
        lastObjectSelectionKey_.clear();
        draw_multi_object_fields(document, selection, commands, catalog,
                                 selection.current().selectedObjectIds);
        behaviorFields_.draw_multi_object_fields(document, selection.current(), commands, catalog,
                                                 selection.current().selectedObjectIds,
                                                 resourceIndex_, searchQuery_);
        return;
    }
    if (!selection.current().objectId)
    {
        integerDrafts_.clear();
        lastSelectionKey_.clear();
        lastObjectSelectionKey_.clear();
        ImGui::TextDisabled("Select an object to edit its properties.");
        return;
    }
    const int objectId = *selection.current().objectId;
    const std::string objectSelectionKey = std::to_string(document.projectGeneration) + ":" +
        std::to_string(document.tabId.value) + ":" + std::to_string(document.documentGeneration) + ":" +
        std::to_string(objectId);
    if (lastObjectSelectionKey_ != objectSelectionKey)
    {
        lastObjectSelectionKey_ = objectSelectionKey;
        ImGui::SetScrollY(0.0f);
    }
    if (revealObjectId_ && *revealObjectId_ != objectId)
    {
        revealObjectId_.reset();
        revealComponentId_.reset();
        revealPath_.clear();
    }
    const std::string selectionKey = std::to_string(document.projectGeneration) + ":" +
        std::to_string(document.tabId.value) + ":" + std::to_string(document.documentGeneration) + ":" +
        std::to_string(objectId) + ":" +
        (selection.current().componentId ? std::to_string(*selection.current().componentId) : std::string("-"));
    if (!lastSelectionKey_.empty() && lastSelectionKey_ != selectionKey)
        integerDrafts_.clear();
    lastSelectionKey_ = selectionKey;
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (objectLocation == document.indexes.objectsById.end())
    {
        selection.repair(document);
        return;
    }
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return;
    if (templateNameSelectionKey_ != selectionKey)
    {
        templateNameSelectionKey_ = selectionKey;
        templateName_ = object->value("name", std::string("Object Template"));
    }
    const InspectorAddressScope addressScope(document, objectId, hostGeneration_);

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

    draw_search_bar();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Object Template"))
    {
        std::array<char, 256> templateNameBuffer{};
        const std::size_t templateNameLength = std::min(templateName_.size(), templateNameBuffer.size() - 1);
        std::memcpy(templateNameBuffer.data(), templateName_.data(), templateNameLength);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##object-template-name", "Template name",
                                     templateNameBuffer.data(), templateNameBuffer.size()))
            templateName_ = templateNameBuffer.data();
        const bool canSaveTemplate = !templateName_.empty() && !document.assetRoot.empty();
        ImGui::BeginDisabled(!canSaveTemplate);
        if (ImGui::Button("Save as Template"))
            save_selected_template(document, objectId, catalog);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && !canSaveTemplate)
            ImGui::SetTooltip("A template name and project asset root are required.");
    }
    const auto refreshedObjectLocation = document.indexes.objectsById.find(objectId);
    object = refreshedObjectLocation == document.indexes.objectsById.end()
        ? nullptr : resolve_object(document.root, refreshedObjectLocation->second);
    if (object == nullptr)
        return;

    // Top GameObject Header
    // Row 1: [☑ Active] [Icon] [Name InputText]
    const bool revealName = consume_reveal_target(objectId, std::nullopt, {std::string("name")});
    const bool revealTag = consume_reveal_target(objectId, std::nullopt, {std::string("tag")});
    const bool revealActive = consume_reveal_target(objectId, std::nullopt, {std::string("active")});

    bool activeValue = object->value("active", true);
    ImGui::PushID(stable_field_id({std::string("active")}).c_str());
    if (revealActive)
        ImGui::SetKeyboardFocusHere();
    if (ImGui::Checkbox("##value", &activeValue))
    {
        set(document, commands, objectId, std::nullopt, {std::string("active")}, activeValue);
    }
    ImGui::PopID();
    if (revealActive)
        ImGui::SetScrollHereY(0.5f);

    ImGui::SameLine();
    const float iconSize = 14.0f;
    const ImVec2 iconPos = ImGui::GetCursorScreenPos();
    const float frameHeight = ImGui::GetFrameHeight();
    draw_editor_icon(EditorIcon::GameObject, ImGui::GetWindowDrawList(),
                     {iconPos.x, iconPos.y + (frameHeight - iconSize) * 0.5f}, iconSize,
                     ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::Dummy(ImVec2(iconSize + 2.0f, frameHeight));
    ImGui::SameLine();

    std::string nameValue = object->value("name", std::string("GameObject"));
    std::array<char, 256> nameBuffer{};
    std::memcpy(nameBuffer.data(), nameValue.data(), std::min(nameValue.size(), nameBuffer.size() - 1));
    ImGui::PushID(stable_field_id({std::string("name")}).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (revealName)
        ImGui::SetKeyboardFocusHere();
    const bool nameChanged = ImGui::InputText("##value", nameBuffer.data(), nameBuffer.size(),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
    if (revealName)
        ImGui::SetScrollHereY(0.5f);
    if (nameChanged || ImGui::IsItemDeactivatedAfterEdit())
    {
        set(document, commands, objectId, std::nullopt, {std::string("name")}, std::string(nameBuffer.data()));
    }
    ImGui::PopID();

    // Row 2: Tag: [Tag InputText]
    std::string tagValue = object->value("tag", std::string{});
    std::array<char, 256> tagBuffer{};
    std::memcpy(tagBuffer.data(), tagValue.data(), std::min(tagValue.size(), tagBuffer.size() - 1));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Tag");
    ImGui::SameLine(64.0f + ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushID(stable_field_id({std::string("tag")}).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (revealTag)
        ImGui::SetKeyboardFocusHere();
    const bool tagChanged = ImGui::InputText("##value", tagBuffer.data(), tagBuffer.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    if (revealTag)
        ImGui::SetScrollHereY(0.5f);
    if (tagChanged || ImGui::IsItemDeactivatedAfterEdit())
    {
        set(document, commands, objectId, std::nullopt, {std::string("tag")}, std::string(tagBuffer.data()));
    }
    ImGui::PopID();

    ImGui::Spacing();
    if (revealObjectId_ && *revealObjectId_ == objectId && !revealComponentId_)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    const bool transformExpanded = ImGui::CollapsingHeader(
        "Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SetItemTooltip("Drop a behavior header anywhere in the Inspector to add it to this GameObject.");
    if (transformExpanded)
    {
        draw_compact_vector2(document, commands, objectId, std::nullopt, {std::string("position")}, "Position",
                             0.0f, 0.0f, std::nullopt, std::nullopt, 0.1f, 0.5f);
        draw_compact_rotation(document, commands, objectId, std::nullopt, {std::string("rotation")}, "Rotation",
                              0.0f, 1.0f, 15.0f);
        draw_compact_vector2(document, commands, objectId, std::nullopt, {std::string("scale")}, "Scale",
                             1.0f, 1.0f, 0.0001f, std::nullopt, 0.05f, 0.1f);
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
        if (revealObjectId_ && *revealObjectId_ == objectId &&
            revealComponentId_ == componentId)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const float headerFrameHeight = ImGui::GetFrameHeight();
        const float buttonWidth = 24.0f;
        const bool headerOpen = ImGui::CollapsingHeader(componentHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();

        ImGui::SetCursorScreenPos({headerMax.x - buttonWidth - 4.0f, headerMin.y + (headerFrameHeight - 18.0f) * 0.5f});
        if (ImGui::SmallButton("⋮##menu"))
            ImGui::OpenPopup("ComponentHeaderContextMenu");

        bool componentRemoved = false;
        if (ImGui::BeginPopupContextItem("ComponentHeaderContextMenu"))
        {
            if (ImGui::MenuItem("Reset"))
            {
                const Selection current = selection.current();
                const auto reset = reset_component_settings(document, commands, objectId, visibleComponentId,
                    catalog, SelectionState{current, current});
                if (reset.status == CommandStatus::Rejected || reset.status == CommandStatus::StaleAddress)
                    feedback_ = reset.message;
                else
                    feedback_.clear();
            }
            if (ImGui::MenuItem("Copy Settings"))
            {
                std::string error;
                auto copied = copy_component_settings(*component, catalog, error);
                if (copied)
                {
                    componentSettingsClipboard_ = std::move(*copied);
                    feedback_.clear();
                }
                else
                    feedback_ = std::move(error);
            }
            const bool clipboardMatches = componentSettingsClipboard_ &&
                componentSettingsClipboard_->value("componentType", -1) == type;
            if (ImGui::MenuItem("Paste Settings", nullptr, false, clipboardMatches))
            {
                const Selection current = selection.current();
                const auto pasted = paste_component_settings(document, commands, objectId, visibleComponentId,
                    *componentSettingsClipboard_, catalog, SelectionState{current, current});
                if (pasted.status == CommandStatus::Rejected || pasted.status == CommandStatus::StaleAddress)
                    feedback_ = pasted.message;
                else
                    feedback_.clear();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component"))
            {
                const Selection current = selection.current();
                const auto removed = commands.remove_component(document, objectId, *componentId);
                if (removed.status == CommandStatus::Changed)
                {
                    selection.repair(document);
                    commands.set_last_selection_state({current, selection.current()});
                    feedback_.clear();
                    componentRemoved = true;
                }
                else if (removed.status == CommandStatus::Rejected)
                {
                    feedback_ = removed.message;
                }
            }
            ImGui::EndPopup();
        }

        if (!headerOpen || componentRemoved)
            component = nullptr;
        if (component != nullptr)
        {
        if (type == 2)
        {
            ImGui::SeparatorText("Camera");
            const auto dimensions = interpret_camera_dimensions(*component);
            if (dimensions.widthConflict || dimensions.heightConflict)
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                                   "Canonical and legacy camera dimensions differ.");
            const auto legacy = component->find("camera");
            const bool hasLegacy = legacy != component->end() && legacy->is_object();
            const bool hasWidth = component->contains("width");
            const bool hasHeight = component->contains("height");
            if (hasWidth || !hasLegacy || !dimensions.widthUsesLegacy)
                draw_number(document, commands, objectId, componentId, {std::string("width")}, "Width", 640.0f,
                            std::numeric_limits<float>::min());
            else
                draw_number(document, commands, objectId, componentId,
                            {std::string("camera"), std::string("w")}, "Width", 640.0f,
                            std::numeric_limits<float>::min());
            if (hasHeight || !hasLegacy || !dimensions.heightUsesLegacy)
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
            if (field_matches_search(document, objectId, componentId,
                                     {std::string("textureFilePath")}, "Texture"))
                draw_asset_reference_field(document, commands, objectId, componentId, std::nullopt,
                                           {std::string("textureFilePath")}, "Texture", true,
                                           "Texture", browse, std::string{}, resourceIndex_);
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
            else if (field_matches_search(document, objectId, componentId,
                                          {std::string("sourceRect")}, "Initialize Source Rectangle") &&
                     ImGui::Button("Initialize Source Rectangle"))
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
            if (field_matches_search(document, objectId, componentId,
                                     {std::string("animationClipFilePath")}, "Animation Clip"))
                draw_asset_reference_field(document, commands, objectId, componentId, std::nullopt,
                                           {std::string("animationClipFilePath")}, "AnimationClip", true,
                                           "Animation Clip", browse, std::string{}, resourceIndex_);
        }
        else if (type == 1)
        {
            ImGui::SeparatorText("Audio");
            if (field_matches_search(document, objectId, componentId,
                                     {std::string("filePath")}, "Audio File"))
                draw_asset_reference_field(document, commands, objectId, componentId, std::nullopt,
                                           {std::string("filePath")}, "Audio", true,
                                           "Audio File", browse, std::string{}, resourceIndex_);
            draw_number(document, commands, objectId, componentId, {std::string("gain")}, "Gain", 1.0f);
            draw_integer(document, commands, objectId, componentId, {std::string("loopCount")}, "Loop Count", 0);
            draw_boolean(document, commands, objectId, componentId, {std::string("autoPlay")}, "Auto Play");
        }
        else if (type == 5)
        {
            ImGui::SeparatorText("Behavior Fields");
            if (selection.current().componentId == componentId)
                behaviorFields_.draw(document, selection, commands, catalog, browse, resourceIndex_,
                                     searchQuery_);
            else
            {
                SelectionModel scriptSelection;
                scriptSelection.set(document, Selection{objectId, componentId, std::nullopt,
                                                        FocusedPanel::Inspector});
                behaviorFields_.draw(document, scriptSelection, commands, catalog, browse, resourceIndex_,
                                     searchQuery_);
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

    if (ImGui::Button("Add Component...", ImVec2(-FLT_MIN, 0.0f)))
        ImGui::OpenPopup("Add Component or Behavior");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add a built-in component, or attach a script behavior to this GameObject.");
    if (ImGui::BeginPopup("Add Component or Behavior"))
    {
        std::array<char, 192> addBuffer{};
        const std::size_t addLength = std::min(addPickerQuery_.size(), addBuffer.size() - 1);
        std::memcpy(addBuffer.data(), addPickerQuery_.data(), addLength);
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::InputTextWithHint("##add-picker-search", "Search components or behaviors",
                                     addBuffer.data(), addBuffer.size()))
            addPickerQuery_ = addBuffer.data();
        ImGui::SeparatorText("Components");
        bool anyComponent = false;
        const auto currentObjectLocation = document.indexes.objectsById.find(objectId);
        const Json *currentObject = currentObjectLocation == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, currentObjectLocation->second);
        for (const auto &[type, label] : kComponentChoices)
        {
            const std::string searchText = std::string(label) + " component " + std::to_string(type);
            if (!discovery_query_matches(addPickerQuery_, searchText))
                continue;
            if (currentObject == nullptr || has_component_type(*currentObject, type))
                continue;
            anyComponent = true;
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
                    feedback_.clear();
                    ImGui::CloseCurrentPopup();
                }
                else
                    feedback_ = result.message;
                break;
            }
        }
        if (!anyComponent)
            ImGui::TextDisabled("No compatible components match this search.");
        ImGui::SeparatorText("Behaviors");
        bool anyBehavior = false;
        const auto behaviorCatalog = catalog.snapshot();
        if (!behaviorCatalog)
            ImGui::TextDisabled("Build the project to load the behavior catalog.");
        else
        {
            const auto behaviors = behaviorCatalog->document.find("behaviors");
            if (behaviors != behaviorCatalog->document.end() && behaviors->is_array())
            {
                for (const auto &behavior : *behaviors)
                {
                    if (!behavior.is_object() || !behavior.contains("name") ||
                        !behavior["name"].is_string())
                        continue;
                    const std::string behaviorType = behavior["name"].get<std::string>();
                    if (!discovery_query_matches(addPickerQuery_, behaviorType + " behavior"))
                        continue;
                    anyBehavior = true;
                    if (ImGui::MenuItem(behaviorType.c_str()))
                    {
                        attach_behavior(behaviorType);
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                }
            }
            if (!anyBehavior)
                ImGui::TextDisabled("No behaviors match this search.");
        }
        ImGui::EndPopup();
    }
    if (!feedback_.empty())
        ImGui::TextWrapped("%s", feedback_.c_str());
}

void InspectorPanel::draw_multi_object_fields(DocumentSession &document, SelectionModel &selection,
                                               EditCommandService &commands, const BehaviorCatalog &catalog,
                                               const std::vector<int> &objectIds)
{
    draw_search_bar();
    ImGui::Separator();
    std::string selectionScope("multi");
    for (const int objectId : objectIds)
    {
        selectionScope.push_back(':');
        selectionScope += std::to_string(objectId);
    }
    ImGui::PushID(selectionScope.c_str());
    ImGui::Text("%zu objects selected", objectIds.size());

    const auto common_values = [&](const std::vector<FieldPathSegment> &path, auto compatible)
        -> std::optional<std::vector<Json>> {
        std::vector<Json> values;
        values.reserve(objectIds.size());
        for (const int objectId : objectIds)
        {
            const Json *value = address_value(document, objectId, std::nullopt, path);
            if (value == nullptr || !compatible(*value))
                return std::nullopt;
            values.push_back(*value);
        }
        return values;
    };
    const auto commit_common = [&](const std::vector<FieldPathSegment> &path, Json value) {
        std::vector<BatchPropertyEdit> edits;
        edits.reserve(objectIds.size());
        for (const int objectId : objectIds)
            edits.push_back({address_for_document(document, objectId, std::nullopt, std::nullopt, path),
                             PropertyValue::of(value)});
        const Selection current = selection.current();
        const auto result = commands.apply_batch(document, edits, {current, current});
        if (result.status == CommandStatus::Rejected || result.status == CommandStatus::StaleAddress)
            feedback_ = result.message;
        else
            feedback_.clear();
    };
    const auto draw_common_boolean = [&](std::vector<FieldPathSegment> path, const char *label) {
        if (!field_matches_search(document, selection.current().objectId.value_or(-1),
                                  std::nullopt, path, label))
            return;
        auto values = common_values(path, [](const Json &value) { return value.is_boolean(); });
        if (!values)
            return;
        bool mixed = std::any_of(values->begin() + 1, values->end(), [&](const Json &value) {
            return value != values->front();
        });
        bool value = values->front().get<bool>();
        begin_field(label, path);
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
        const bool changed = ImGui::Checkbox("##value", &value);
        ImGui::PopItemFlag();
        end_field();
        if (changed)
            commit_common(path, Json(value));
    };
    const auto draw_common_number = [&](std::vector<FieldPathSegment> path, const char *label,
                                        std::optional<float> minimum = std::nullopt,
                                        std::optional<float> maximum = std::nullopt) {
        if (!field_matches_search(document, selection.current().objectId.value_or(-1),
                                  std::nullopt, path, label))
            return;
        auto values = common_values(path, [](const Json &value) {
            return value.is_number() && std::isfinite(value.get<double>());
        });
        if (!values)
            return;
        const bool mixed = std::any_of(values->begin() + 1, values->end(), [&](const Json &value) {
            return value != values->front();
        });
        float value = json_float(&values->front(), 0.0f);
        const bool reveal = consume_reveal_target(selection.current().objectId.value_or(-1),
                                                  std::nullopt, path);
        begin_field(label, path);
        if (reveal)
            ImGui::SetKeyboardFocusHere();
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
        const bool changed = ImGui::InputFloat("##value", &value, 0.0f, 0.0f, "%.9g",
                                               ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemFlag();
        if (reveal)
            ImGui::SetScrollHereY(0.5f);
        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool commit = !cancel && (changed || ImGui::IsItemDeactivatedAfterEdit());
        end_field();
        if (!commit)
            return;
        if (!std::isfinite(value) || (minimum && value < *minimum) || (maximum && value > *maximum))
        {
            feedback_ = std::string(label) + " is outside the supported range.";
            return;
        }
        commit_common(path, Json(value));
    };

    draw_common_boolean({std::string("active")}, "Active");
    draw_common_compact_transform(document, selection, commands, objectIds);

    const auto component_at_occurrence = [&](int objectId, int type, std::size_t occurrence)
        -> std::optional<std::pair<int, const Json *>> {
        const auto objectLocation = document.indexes.objectsById.find(objectId);
        if (objectLocation == document.indexes.objectsById.end())
            return std::nullopt;
        const Json *object = resolve_object(document.root, objectLocation->second);
        if (object == nullptr)
            return std::nullopt;
        const auto components = object->find("components");
        if (components == object->end() || !components->is_array())
            return std::nullopt;
        std::size_t currentOccurrence = 0;
        for (const auto &component : *components)
        {
            if (!component.is_object() ||
                json_integer(component.contains("type") ? &component["type"] : nullptr, -1) != type)
                continue;
            if (currentOccurrence++ != occurrence)
                continue;
            const int componentId = json_integer(component.contains("id") ? &component["id"] : nullptr, -1);
            return componentId >= 0 ? std::optional<std::pair<int, const Json *>>(
                                         std::pair<int, const Json *>{componentId, &component})
                                   : std::nullopt;
        }
        return std::nullopt;
    };
    const auto category = [](const Json &value) {
        if (value.is_boolean()) return 1;
        if (value.is_number_float()) return 2;
        if (value.is_number_integer() && !value.is_number_unsigned()) return 3;
        if (value.is_number_unsigned()) return 4;
        if (value.is_string()) return 5;
        return 0;
    };
    const auto primaryLocation = document.indexes.objectsById.find(selection.current().objectId.value_or(-1));
    if (primaryLocation != document.indexes.objectsById.end())
    {
        const Json *primaryObject = resolve_object(document.root, primaryLocation->second);
        const auto primaryComponents = primaryObject != nullptr
                                           ? primaryObject->find("components")
                                           : Json::const_iterator{};
        bool componentSectionShown = false;
        if (primaryObject != nullptr && primaryComponents != primaryObject->end() &&
            primaryComponents->is_array())
        {
            std::unordered_map<int, std::size_t> occurrences;
            for (const auto &primaryComponent : *primaryComponents)
            {
                if (!primaryComponent.is_object())
                    continue;
                const int type = json_integer(primaryComponent.contains("type")
                                                  ? &primaryComponent["type"] : nullptr, -1);
                if (type < 0)
                    continue;
                const std::size_t occurrence = occurrences[type]++;
                std::vector<std::pair<int, const Json *>> matching;
                matching.reserve(objectIds.size());
                for (const int objectId : objectIds)
                {
                    const auto component = component_at_occurrence(objectId, type, occurrence);
                    if (!component)
                        break;
                    matching.push_back(*component);
                }
                if (matching.size() != objectIds.size())
                    continue;

                if (!componentSectionShown)
                {
                    ImGui::SeparatorText("Common component fields");
                    componentSectionShown = true;
                }
                const std::string componentScope = "component:" + std::to_string(type) + ":" +
                                                   std::to_string(occurrence);
                ImGui::PushID(componentScope.c_str());
                ImGui::Text("%s", component_label(type));
                std::vector<ComponentSettingsTarget> groupTargets;
                groupTargets.reserve(matching.size());
                for (std::size_t index = 0; index < matching.size(); ++index)
                    groupTargets.emplace_back(objectIds[index], matching[index].first);
                if (ImGui::SmallButton("Copy Settings"))
                {
                    std::string error;
                    auto copied = copy_component_settings(*matching.front().second, catalog, error);
                    if (copied)
                    {
                        componentSettingsClipboard_ = std::move(*copied);
                        feedback_.clear();
                    }
                    else
                        feedback_ = std::move(error);
                }
                ImGui::SameLine();
                const bool clipboardMatches = componentSettingsClipboard_ &&
                    componentSettingsClipboard_->value("componentType", -1) == type;
                ImGui::BeginDisabled(!clipboardMatches);
                if (ImGui::SmallButton("Paste Settings to Selection"))
                {
                    const Selection current = selection.current();
                    const auto pasted = paste_component_settings_group(document, commands, groupTargets,
                        *componentSettingsClipboard_, catalog, SelectionState{current, current});
                    if (pasted.status == CommandStatus::Rejected || pasted.status == CommandStatus::StaleAddress)
                        feedback_ = pasted.message;
                    else
                        feedback_.clear();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Settings for Selection"))
                {
                    const Selection current = selection.current();
                    const auto reset = reset_component_settings_group(document, commands, groupTargets,
                        catalog, SelectionState{current, current});
                    if (reset.status == CommandStatus::Rejected || reset.status == CommandStatus::StaleAddress)
                        feedback_ = reset.message;
                    else
                        feedback_.clear();
                }
                for (auto it = matching.front().second->begin(); it != matching.front().second->end(); ++it)
                {
                    const std::string key = it.key();
                    if (key == "id" || key == "type" || key == "behaviors")
                        continue;
                    std::vector<Json> values;
                    values.reserve(matching.size());
                    const int valueCategory = category(it.value());
                    if (valueCategory == 0)
                        continue;
                    bool compatible = true;
                    for (const auto &component : matching)
                    {
                        const auto found = component.second->find(key);
                        if (found == component.second->end() || category(*found) != valueCategory)
                        {
                            compatible = false;
                            break;
                        }
                        values.push_back(*found);
                    }
                    if (!compatible)
                        continue;
                    const bool mixed = std::any_of(values.begin() + 1, values.end(),
                                                   [&](const Json &value) { return value != values.front(); });
                    const std::vector<FieldPathSegment> path{key};
                    if (!field_matches_search(document, objectIds.front(), matching.front().first,
                                              path, key))
                        continue;
                    const auto commit_component = [&](Json value) {
                        std::vector<BatchPropertyEdit> edits;
                        edits.reserve(matching.size());
                        for (std::size_t index = 0; index < matching.size(); ++index)
                            edits.push_back({address_for_document(document, objectIds[index],
                                                                  matching[index].first, std::nullopt,
                                                                  path),
                                             PropertyValue::of(value)});
                        const Selection current = selection.current();
                        const auto result = commands.apply_batch(document, edits, {current, current});
                        if (result.status == CommandStatus::Rejected ||
                            result.status == CommandStatus::StaleAddress)
                            feedback_ = result.message;
                        else
                            feedback_.clear();
                    };
                    begin_field(key.c_str(), path);
                    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
                    if (valueCategory == 1)
                    {
                        bool value = values.front().get<bool>();
                        const bool changed = ImGui::Checkbox("##value", &value);
                        ImGui::PopItemFlag();
                        end_field();
                        if (changed)
                            commit_component(value);
                    }
                    else if (valueCategory == 2)
                    {
                        double value = values.front().get<double>();
                        const bool changed = ImGui::InputDouble("##value", &value, 0.0, 0.0,
                                                                "%.17g", ImGuiInputTextFlags_EnterReturnsTrue);
                        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                        const bool shouldCommit = !cancel &&
                            (changed || ImGui::IsItemDeactivatedAfterEdit());
                        ImGui::PopItemFlag();
                        end_field();
                        if (shouldCommit && std::isfinite(value))
                            commit_component(value);
                    }
                    else if (valueCategory == 3)
                    {
                        std::int64_t value = values.front().get<std::int64_t>();
                        const bool changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &value,
                            nullptr, nullptr, "%lld", ImGuiInputTextFlags_EnterReturnsTrue);
                        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                        const bool shouldCommit = !cancel &&
                            (changed || ImGui::IsItemDeactivatedAfterEdit());
                        ImGui::PopItemFlag();
                        end_field();
                        if (shouldCommit)
                            commit_component(value);
                    }
                    else if (valueCategory == 4)
                    {
                        std::uint64_t value = values.front().get<std::uint64_t>();
                        const bool changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &value,
                            nullptr, nullptr, "%llu", ImGuiInputTextFlags_EnterReturnsTrue);
                        const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                        const bool shouldCommit = !cancel &&
                            (changed || ImGui::IsItemDeactivatedAfterEdit());
                        ImGui::PopItemFlag();
                        end_field();
                        if (shouldCommit)
                            commit_component(value);
                    }
                    else
                    {
                        const std::string &stored = values.front().get_ref<const std::string &>();
                        if (stored.size() >= 2048)
                        {
                            ImGui::PopItemFlag();
                            end_field();
                            ImGui::TextDisabled("%s is too long for inline editing.", key.c_str());
                            continue;
                        }
                        std::array<char, 2048> buffer{};
                        std::memcpy(buffer.data(), stored.data(), stored.size());
                        const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size(),
                                                              ImGuiInputTextFlags_EnterReturnsTrue);
                        const bool shouldCommit = changed || ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::PopItemFlag();
                        end_field();
                        if (shouldCommit)
                            commit_component(std::string(buffer.data()));
                    }
                }
                ImGui::PopID();
            }
        }
    }
    if (!feedback_.empty())
        ImGui::TextWrapped("%s", feedback_.c_str());
    ImGui::PopID();
}
} // namespace platformator_editor
