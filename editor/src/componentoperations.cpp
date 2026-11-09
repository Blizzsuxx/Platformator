#include "componentoperations.h"

#include "componenttypes.h"

#include <exception>
#include <vector>

namespace platformator_editor
{
namespace
{
CommandResult rejected(std::string message)
{
    CommandResult result;
    result.status = CommandStatus::Rejected;
    result.message = std::move(message);
    return result;
}

std::optional<int> component_type(const Json &component)
{
    if (!component.is_object())
        return std::nullopt;
    const auto found = component.find("type");
    if (found == component.end() || (!found->is_number_integer() && !found->is_number_unsigned()))
        return std::nullopt;
    try
    {
        return found->get<int>();
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
            if (object == nullptr || !object->is_object())
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

const Json *find_component(DocumentSession &document, int objectId, int componentId,
                           std::string &error)
{
    if (!document.indexes.current_for(document.documentGeneration, document.structureRevision))
    {
        try
        {
            rebuild_document_indexes(document);
        }
        catch (const std::exception &exception)
        {
            error = std::string("cannot rebuild component identity indexes: ") + exception.what();
            return nullptr;
        }
    }
    const auto location = document.indexes.componentsById.find(componentId);
    if (location == document.indexes.componentsById.end() || location->second.ownerObjectId != objectId)
    {
        error = "the selected component is no longer present on this object";
        return nullptr;
    }
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    if (objectLocation == document.indexes.objectsById.end())
    {
        error = "the selected object is no longer present";
        return nullptr;
    }
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
    {
        error = "the selected object location is no longer valid";
        return nullptr;
    }
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        location->second.componentIndex >= components->size())
    {
        error = "the selected component location is no longer valid";
        return nullptr;
    }
    return &(*components)[location->second.componentIndex];
}

const Json *behavior_descriptor(const BehaviorCatalog &catalog, const std::string &name)
{
    const auto snapshot = catalog.snapshot();
    if (!snapshot || !snapshot->document.is_object())
        return nullptr;
    const auto behaviors = snapshot->document.find("behaviors");
    if (behaviors == snapshot->document.end() || !behaviors->is_array())
        return nullptr;
    for (const auto &behavior : *behaviors)
        if (behavior.is_object() && behavior.value("name", std::string{}) == name)
            return &behavior;
    return nullptr;
}

bool behavior_fields(const Json &descriptor, const Json *&fields)
{
    if (!descriptor.is_object() || !descriptor.value("field_schema_available", false))
        return false;
    const auto found = descriptor.find("fields");
    if (found == descriptor.end() || !found->is_array())
        return false;
    fields = &*found;
    return true;
}

Json sanitized_value(const Json &schema, const Json *value)
{
    if (schema.is_object())
    {
        Json result = Json::object();
        for (auto it = schema.begin(); it != schema.end(); ++it)
        {
            const Json *candidate = nullptr;
            if (value != nullptr && value->is_object())
            {
                const auto found = value->find(it.key());
                if (found != value->end())
                    candidate = &*found;
            }
            result[it.key()] = sanitized_value(it.value(), candidate);
        }
        return result;
    }
    if (schema.is_boolean())
        return value != nullptr && value->is_boolean() ? *value : schema;
    if (schema.is_number_integer())
        return value != nullptr && (value->is_number_integer() || value->is_number_unsigned())
            ? *value : schema;
    if (schema.is_number())
        return value != nullptr && value->is_number() ? *value : schema;
    if (schema.is_string())
        return value != nullptr && value->is_string() ? *value : schema;
    return schema;
}

Json sprite_source_rect_schema()
{
    return Json{{"x", 0.0}, {"y", 0.0}, {"w", 16.0}, {"h", 16.0}};
}

void append_static_edits(DocumentSession &document, int objectId, int componentId,
                         const Json &schema, const Json *current, const Json &source,
                         std::vector<FieldPathSegment> path,
                         std::vector<BatchPropertyEdit> &edits)
{
    if (schema.is_object() && current != nullptr && current->is_object() && source.is_object())
    {
        for (auto it = schema.begin(); it != schema.end(); ++it)
        {
            const Json *currentChild = nullptr;
            const auto foundCurrent = current->find(it.key());
            if (foundCurrent != current->end())
                currentChild = &*foundCurrent;
            const auto foundSource = source.find(it.key());
            const Json &sourceChild = foundSource != source.end() ? *foundSource : it.value();
            auto childPath = path;
            childPath.emplace_back(it.key());
            append_static_edits(document, objectId, componentId, it.value(), currentChild,
                                sourceChild, std::move(childPath), edits);
        }
        return;
    }

    const Json cleanValue = sanitized_value(schema, &source);
    if (path.empty())
        return;
    edits.push_back({address_for_document(document, objectId, componentId, std::nullopt,
                                          std::move(path)), PropertyValue::of(cleanValue)});
}

int settings_component_type(const Json &settings)
{
    if (!settings.is_object())
        return -1;
    const auto found = settings.find("componentType");
    if (found == settings.end() || (!found->is_number_integer() && !found->is_number_unsigned()))
        return -1;
    try { return found->get<int>(); }
    catch (...) { return -1; }
}

void append_behavior_edits(DocumentSession &document, int objectId, int componentId,
                           const Json &targetComponent, const Json *copiedSettings,
                           const BehaviorCatalog &catalog, bool reset,
                           std::vector<BatchPropertyEdit> &edits, std::string &error)
{
    const auto targetBehaviors = targetComponent.find("behaviors");
    if (targetBehaviors == targetComponent.end() || !targetBehaviors->is_array())
    {
        error = "the selected Script component has no behavior list";
        return;
    }
    const Json *copiedBehaviors = nullptr;
    if (!reset)
    {
        if (copiedSettings == nullptr || !copiedSettings->is_object())
        {
            error = "copied Script settings are invalid";
            return;
        }
        const auto found = copiedSettings->find("behaviors");
        if (found == copiedSettings->end() || !found->is_array() || found->size() != targetBehaviors->size())
        {
            error = "Script settings can only be pasted when behavior types and order match";
            return;
        }
        copiedBehaviors = &*found;
    }

    for (std::size_t ordinal = 0; ordinal < targetBehaviors->size(); ++ordinal)
    {
        const Json &targetBehavior = (*targetBehaviors)[ordinal];
        if (!targetBehavior.is_object() || !targetBehavior.contains("type") ||
            !targetBehavior["type"].is_string())
        {
            error = "a behavior has no declared type and cannot be safely edited";
            return;
        }
        const std::string name = targetBehavior["type"].get<std::string>();
        if (copiedBehaviors != nullptr)
        {
            const Json &copiedBehavior = (*copiedBehaviors)[ordinal];
            if (!copiedBehavior.is_object() || copiedBehavior.value("type", std::string{}) != name)
            {
                error = "Script settings can only be pasted when behavior types and order match";
                return;
            }
        }
        const Json *descriptor = behavior_descriptor(catalog, name);
        const Json *fields = nullptr;
        if (descriptor == nullptr || !behavior_fields(*descriptor, fields))
        {
            error = "field schema is unavailable for behavior '" + name + "'";
            return;
        }

        const Json *copiedFields = nullptr;
        if (copiedBehaviors != nullptr)
        {
            const auto found = (*copiedBehaviors)[ordinal].find("fields");
            if (found == (*copiedBehaviors)[ordinal].end() || !found->is_object())
            {
                error = "copied behavior fields are invalid";
                return;
            }
            copiedFields = &*found;
        }
        for (const auto &field : *fields)
        {
            if (!field.is_object())
                continue;
            const std::string fieldName = field.value("name", std::string{});
            if (fieldName.empty())
                continue;
            PropertyValue replacement = PropertyValue::missing();
            if (reset)
            {
                const auto defaultValue = field.find("default");
                if (defaultValue != field.end())
                    replacement = PropertyValue::of(*defaultValue);
            }
            else
            {
                const auto copiedValue = copiedFields->find(fieldName);
                if (copiedValue != copiedFields->end())
                    replacement = PropertyValue::of(*copiedValue);
            }
            edits.push_back({address_for_document(document, objectId, componentId, ordinal,
                                                   {std::string(fieldName)}),
                             std::move(replacement)});
        }
    }
}

CommandResult apply_settings(DocumentSession &document, EditCommandService &commands,
                             const std::vector<ComponentSettingsTarget> &targets,
                             const Json *settings, const BehaviorCatalog &catalog,
                             bool reset, SelectionState selection)
{
    if (targets.empty())
        return rejected("no compatible components are selected");
    std::string error;
    std::vector<const Json *> components;
    components.reserve(targets.size());
    std::optional<int> type;
    for (const auto &[objectId, componentId] : targets)
    {
        const Json *component = find_component(document, objectId, componentId, error);
        if (component == nullptr)
            return rejected(std::move(error));
        const auto currentType = component_type(*component);
        if (!currentType || (type && *type != *currentType))
            return rejected("group settings require components of the same supported type");
        type = currentType;
        components.push_back(component);
    }

    std::vector<BatchPropertyEdit> edits;
    if (*type == 5)
    {
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            const auto &[objectId, componentId] = targets[index];
            append_behavior_edits(document, objectId, componentId, *components[index], settings,
                                  catalog, reset, edits, error);
            if (!error.empty())
                return rejected(std::move(error));
        }
    }
    else
    {
        const Json defaults = default_component_record(*type);
        if (defaults.empty() || *type < 0)
            return rejected("settings operations are unavailable for this component type");
        if (!reset && (settings == nullptr || !settings->is_object()))
            return rejected("copied component settings are unavailable");
        const Json *sourceFields = &defaults;
        if (!reset)
        {
            const auto found = settings->find("fields");
            sourceFields = found == settings->end() ? nullptr : &*found;
        }
        if (sourceFields == nullptr || !sourceFields->is_object())
            return rejected("copied component settings are invalid");
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            const auto &[objectId, componentId] = targets[index];
            const Json &target = *components[index];
            for (auto it = defaults.begin(); it != defaults.end(); ++it)
            {
                if (it.key() == "type" || it.value().is_array())
                    continue;
                const Json *currentField = nullptr;
                const auto targetField = target.find(it.key());
                if (targetField != target.end())
                    currentField = &*targetField;
                const auto sourceField = sourceFields->find(it.key());
                const Json &value = sourceField != sourceFields->end() ? *sourceField : it.value();
                append_static_edits(document, objectId, componentId, it.value(), currentField, value,
                                    {std::string(it.key())}, edits);
            }
            if (*type == 6)
            {
                const auto currentRect = target.find("sourceRect");
                const auto sourceRect = sourceFields->find("sourceRect");
                if (sourceRect != sourceFields->end())
                    append_static_edits(document, objectId, componentId, sprite_source_rect_schema(),
                        currentRect == target.end() ? nullptr : &*currentRect, *sourceRect,
                        {std::string("sourceRect")}, edits);
                else if (currentRect != target.end())
                    edits.push_back({address_for_document(document, objectId, componentId,
                        std::nullopt, {std::string("sourceRect")}), PropertyValue::missing()});
            }
        }
    }
    return commands.apply_batch(document, edits, std::move(selection));
}
} // namespace

std::optional<Json> copy_component_settings(const Json &component,
                                            const BehaviorCatalog &catalog,
                                            std::string &error)
{
    const auto type = component_type(component);
    if (!type)
    {
        error = "the selected component has no supported type identity";
        return std::nullopt;
    }
    Json settings{{"componentType", *type}};
    if (*type == 5)
    {
        const auto behaviors = component.find("behaviors");
        if (behaviors == component.end() || !behaviors->is_array())
        {
            error = "the selected Script component has no behavior list";
            return std::nullopt;
        }
        settings["behaviors"] = Json::array();
        for (const auto &behavior : *behaviors)
        {
            if (!behavior.is_object() || !behavior.contains("type") || !behavior["type"].is_string())
            {
                error = "a behavior has no declared type and cannot be copied safely";
                return std::nullopt;
            }
            const std::string name = behavior["type"].get<std::string>();
            const Json *descriptor = behavior_descriptor(catalog, name);
            const Json *fields = nullptr;
            if (descriptor == nullptr || !behavior_fields(*descriptor, fields))
            {
                error = "field schema is unavailable for behavior '" + name + "'";
                return std::nullopt;
            }
            Json copiedFields = Json::object();
            for (const auto &field : *fields)
            {
                if (!field.is_object())
                    continue;
                const std::string fieldName = field.value("name", std::string{});
                if (fieldName.empty())
                    continue;
                const auto value = behavior.find(fieldName);
                if (value != behavior.end())
                    copiedFields[fieldName] = *value;
            }
            settings["behaviors"].push_back(Json{{"type", name}, {"fields", std::move(copiedFields)}});
        }
        error.clear();
        return settings;
    }

    const Json defaults = default_component_record(*type);
    if (defaults.empty() || *type < 0)
    {
        error = "settings operations are unavailable for this component type";
        return std::nullopt;
    }
    Json fields = Json::object();
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
    {
        if (it.key() == "type" || it.value().is_array())
            continue;
        const auto source = component.find(it.key());
        fields[it.key()] = sanitized_value(it.value(), source == component.end() ? nullptr : &*source);
    }
    if (*type == 6)
    {
        const auto sourceRect = component.find("sourceRect");
        if (sourceRect != component.end())
            fields["sourceRect"] = sanitized_value(sprite_source_rect_schema(), &*sourceRect);
    }
    settings["fields"] = std::move(fields);
    error.clear();
    return settings;
}

CommandResult paste_component_settings(DocumentSession &document, EditCommandService &commands,
                                       int objectId, int componentId, const Json &settings,
                                       const BehaviorCatalog &catalog, SelectionState selection)
{
    return paste_component_settings_group(document, commands, {{objectId, componentId}},
                                          settings, catalog, std::move(selection));
}

CommandResult paste_component_settings_group(DocumentSession &document, EditCommandService &commands,
                                              const std::vector<ComponentSettingsTarget> &targets,
                                              const Json &settings, const BehaviorCatalog &catalog,
                                              SelectionState selection)
{
    if (targets.empty())
        return rejected("no compatible components are selected");
    std::string error;
    for (const auto &[objectId, componentId] : targets)
    {
        const Json *component = find_component(document, objectId, componentId, error);
        if (component == nullptr)
            return rejected(std::move(error));
        const auto targetType = component_type(*component);
        if (!targetType || settings_component_type(settings) != *targetType)
            return rejected("copied settings require components of the same type");
    }
    return apply_settings(document, commands, targets, &settings, catalog,
                          false, std::move(selection));
}

CommandResult reset_component_settings(DocumentSession &document, EditCommandService &commands,
                                       int objectId, int componentId, const BehaviorCatalog &catalog,
                                       SelectionState selection)
{
    return reset_component_settings_group(document, commands, {{objectId, componentId}},
                                          catalog, std::move(selection));
}

CommandResult reset_component_settings_group(DocumentSession &document, EditCommandService &commands,
                                              const std::vector<ComponentSettingsTarget> &targets,
                                              const BehaviorCatalog &catalog, SelectionState selection)
{
    return apply_settings(document, commands, targets, nullptr, catalog, true, std::move(selection));
}
} // namespace platformator_editor
