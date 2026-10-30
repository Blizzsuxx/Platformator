#include "platformator/scriptmetadata.h"

#include <utility>

namespace platformator
{
const char *script_field_kind_name(ScriptFieldKind kind) noexcept
{
    switch (kind)
    {
    case ScriptFieldKind::Boolean:
        return "boolean";
    case ScriptFieldKind::SignedInteger:
        return "signed_integer";
    case ScriptFieldKind::UnsignedInteger:
        return "unsigned_integer";
    case ScriptFieldKind::Number:
        return "number";
    case ScriptFieldKind::String:
        return "string";
    case ScriptFieldKind::Enum:
        return "enum";
    case ScriptFieldKind::Vector2:
        return "vector2";
    case ScriptFieldKind::Rectangle:
        return "rectangle";
    case ScriptFieldKind::Array:
        return "array";
    case ScriptFieldKind::Optional:
        return "optional";
    case ScriptFieldKind::AssetReference:
        return "asset_reference";
    case ScriptFieldKind::ObjectReference:
        return "object_reference";
    case ScriptFieldKind::OpaqueJson:
        return "opaque_json";
    }
    return "opaque_json";
}

nlohmann::json script_field_descriptor_json(const ScriptFieldDescriptor &field)
{
    nlohmann::json result{{"name", field.name}, {"kind", script_field_kind_name(field.kind)}};
    if (field.minimum)
        result["minimum"] = *field.minimum;
    if (field.maximum)
        result["maximum"] = *field.maximum;
    if (!field.targetType.empty())
        result["target"] = field.targetType;
    if (field.nullable)
        result["nullable"] = true;
    if (!field.enumValues.empty())
    {
        result["enum_values"] = nlohmann::json::array();
        for (const auto &value : field.enumValues)
            result["enum_values"].push_back({{"name", value.name}, {"value", value.value}});
    }
    if (field.element)
        result["element"] = script_field_descriptor_json(*field.element);
    return result;
}

nlohmann::json script_descriptor_json(const ScriptDescriptor &descriptor)
{
    nlohmann::json fields = nlohmann::json::array();
    if (descriptor.fieldSchemaAvailable)
    {
        for (const auto &field : descriptor.fields)
            fields.push_back(script_field_descriptor_json(field));
    }
    return {{"name", descriptor.name},
            {"field_schema_available", descriptor.fieldSchemaAvailable},
            {"fields", std::move(fields)}};
}
} // namespace platformator
