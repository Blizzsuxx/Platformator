#include "templates.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>
#include <platformator/gameobject.h>

namespace platformator_editor
{
namespace
{
struct TemplateIndex
{
    std::unordered_map<int, const Json *> objects;
    std::unordered_map<int, const Json *> components;
    std::unordered_map<int, int> componentOwners;
    std::unordered_set<int> allIds;
    std::vector<int> orderedIds;
};

std::optional<int> read_identity(const Json &value)
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

bool collect_object(const Json &object, TemplateIndex &index, std::string &error)
{
    if (!object.is_object())
    {
        error = "template roots and children must be JSON objects";
        return false;
    }
    const auto objectIdField = object.find("id");
    if (objectIdField == object.end())
    {
        error = "every template object requires an integer ID";
        return false;
    }
    const auto objectId = read_identity(*objectIdField);
    if (!objectId)
    {
        error = "template object IDs must be unique nonnegative 32-bit integers";
        return false;
    }
    if (!index.objects.emplace(*objectId, &object).second)
    {
        error = "template object IDs must be unique within the subtree";
        return false;
    }
    if (!index.allIds.insert(*objectId).second)
    {
        error = "object and component IDs must be unique within the subtree";
        return false;
    }
    index.orderedIds.push_back(*objectId);

    const auto components = object.find("components");
    if (components == object.end() || !components->is_array())
    {
        error = "every template object requires a components array";
        return false;
    }
    for (const auto &component : *components)
    {
        if (!component.is_object())
        {
            error = "template components must be JSON objects";
            return false;
        }
        const auto componentIdField = component.find("id");
        const auto componentType = component.find("type");
        if (componentIdField == component.end() || componentType == component.end() ||
            (!componentType->is_number_integer() && !componentType->is_number_unsigned()))
        {
            error = "every template component requires integer ID and type fields";
            return false;
        }
        const auto componentId = read_identity(*componentIdField);
        if (!componentId)
        {
            error = "template component IDs must be unique nonnegative 32-bit integers";
            return false;
        }
        if (!index.components.emplace(*componentId, &component).second)
        {
            error = "template component IDs must be unique within the subtree";
            return false;
        }
        if (!index.allIds.insert(*componentId).second)
        {
            error = "object and component IDs must be unique within the subtree";
            return false;
        }
        index.orderedIds.push_back(*componentId);
        index.componentOwners.emplace(*componentId, *objectId);
    }

    const auto children = object.find("children");
    if (children == object.end() || !children->is_array())
    {
        error = "every template object requires a children array";
        return false;
    }
    for (const auto &child : *children)
        if (!collect_object(child, index, error))
            return false;
    return true;
}

bool parse_pointer(std::string_view pointer, std::vector<std::string> &segments)
{
    if (pointer.empty() || pointer.front() != '/')
        return false;
    std::size_t start = 1;
    while (start <= pointer.size())
    {
        const auto slash = pointer.find('/', start);
        const auto end = slash == std::string_view::npos ? pointer.size() : slash;
        std::string decoded;
        for (std::size_t index = start; index < end; ++index)
        {
            if (pointer[index] != '~')
            {
                decoded.push_back(pointer[index]);
                continue;
            }
            if (index + 1 >= end)
                return false;
            const char escape = pointer[++index];
            if (escape == '0') decoded.push_back('~');
            else if (escape == '1') decoded.push_back('/');
            else return false;
        }
        if (decoded.empty())
            return false;
        segments.push_back(std::move(decoded));
        if (slash == std::string_view::npos)
            break;
        start = slash + 1;
    }
    return !segments.empty();
}

const Json *resolve_pointer(const Json &owner, const std::vector<std::string> &segments)
{
    const Json *value = &owner;
    for (const auto &segment : segments)
    {
        if (value->is_object())
        {
            const auto found = value->find(segment);
            if (found == value->end())
                return nullptr;
            value = &*found;
        }
        else if (value->is_array())
        {
            if (segment.size() > 1 && segment.front() == '0')
                return nullptr;
            std::size_t index = 0;
            const auto parsed = std::from_chars(segment.data(), segment.data() + segment.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != segment.data() + segment.size() ||
                index >= value->size())
                return nullptr;
            value = &(*value)[index];
        }
        else
            return nullptr;
    }
    return value;
}

Json *resolve_pointer(Json &owner, const std::vector<std::string> &segments)
{
    Json *value = &owner;
    for (const auto &segment : segments)
    {
        if (value->is_object())
        {
            const auto found = value->find(segment);
            if (found == value->end())
                return nullptr;
            value = &*found;
        }
        else if (value->is_array())
        {
            if (segment.size() > 1 && segment.front() == '0')
                return nullptr;
            std::size_t index = 0;
            const auto parsed = std::from_chars(segment.data(), segment.data() + segment.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != segment.data() + segment.size() ||
                index >= value->size())
                return nullptr;
            value = &(*value)[index];
        }
        else
            return nullptr;
    }
    return value;
}

std::string escape_pointer_segment(std::string_view segment)
{
    std::string escaped;
    for (const char value : segment)
    {
        if (value == '~') escaped += "~0";
        else if (value == '/') escaped += "~1";
        else escaped.push_back(value);
    }
    return escaped;
}

std::string pointer_string(const std::vector<std::string> &segments)
{
    std::string pointer;
    for (const auto &segment : segments)
        pointer += "/" + escape_pointer_segment(segment);
    return pointer;
}

bool is_object_kind(std::string_view kind)
{
    return kind == "GameObject" || kind == "Object";
}

bool target_exists(const TemplateIndex &index, int id, std::string_view kind)
{
    if (is_object_kind(kind)) return index.objects.contains(id);
    if (kind == "Component") return index.components.contains(id);
    return false;
}

bool target_exists(const DocumentSession &document, int id, std::string_view kind)
{
    if (is_object_kind(kind)) return document.indexes.objectsById.contains(id);
    if (kind == "Component") return document.indexes.componentsById.contains(id);
    return false;
}

const Json *component_or_object(const TemplateIndex &index, int ownerId,
                               std::optional<int> componentId)
{
    if (!componentId)
    {
        const auto found = index.objects.find(ownerId);
        return found == index.objects.end() ? nullptr : found->second;
    }
    const auto found = index.components.find(*componentId);
    const auto parent = index.componentOwners.find(*componentId);
    return found == index.components.end() || parent == index.componentOwners.end() ||
                   parent->second != ownerId
        ? nullptr : found->second;
}

Json *component_or_object(Json &subtree, int ownerId, std::optional<int> componentId)
{
    std::vector<Json *> pending{&subtree};
    while (!pending.empty())
    {
        Json *object = pending.back();
        pending.pop_back();
        if (object->value("id", -1) == ownerId)
        {
            if (!componentId) return object;
            auto &components = object->at("components");
            for (auto &component : components)
                if (component.value("id", -1) == *componentId) return &component;
            return nullptr;
        }
        for (auto &child : object->at("children"))
            pending.push_back(&child);
    }
    return nullptr;
}

Json *destination_siblings(DocumentSession &document, std::optional<int> parentObjectId)
{
    if (!parentObjectId)
        return document.root.is_array() ? &document.root : nullptr;
    const auto location = document.indexes.objectsById.find(*parentObjectId);
    if (location == document.indexes.objectsById.end() || location->second.childIndices.empty())
        return nullptr;
    Json *object = &document.root;
    const auto &indices = location->second.childIndices;
    if (!object->is_array() || indices.front() >= object->size())
        return nullptr;
    object = &(*object)[indices.front()];
    for (std::size_t depth = 1; depth < indices.size(); ++depth)
    {
        auto children = object->find("children");
        if (children == object->end() || !children->is_array() || indices[depth] >= children->size())
            return nullptr;
        object = &(*children)[indices[depth]];
    }
    auto children = object->find("children");
    return children != object->end() && children->is_array() ? &*children : nullptr;
}

bool make_binding(const TemplateIndex &subtreeIndex, const TemplateIndex &sceneIndex,
                  int ownerId, std::optional<int> componentId,
                  const std::vector<std::string> &path, std::string_view kind,
                  const Json &value, std::string_view sourceSceneIdentity,
                  Json &binding, std::string &error)
{
    if (value.is_null())
        return true;
    const auto targetId = read_identity(value);
    if (!targetId)
    {
        error = "declared object-reference fields must contain a nonnegative integer or null";
        return false;
    }
    const bool internal = target_exists(subtreeIndex, *targetId, kind);
    if (internal)
    {
        binding = Json{{"owner_id", ownerId}, {"path", pointer_string(path)},
                       {"target_kind", kind}, {"scope", "internal"}};
        if (componentId) binding["component_id"] = *componentId;
        return true;
    }
    if (!target_exists(sceneIndex, *targetId, kind))
    {
        error = "declared reference target is absent or has the wrong kind in the source scene";
        return false;
    }
    if (sourceSceneIdentity.empty())
    {
        error = "external reference collection requires a source-scene identity";
        return false;
    }
    binding = Json{{"owner_id", ownerId}, {"path", pointer_string(path)},
                   {"target_kind", kind}, {"scope", "external"},
                   {"source_scene_identity", sourceSceneIdentity},
                   {"original_target_kind", kind}, {"original_target_id", *targetId}};
    if (componentId) binding["component_id"] = *componentId;
    return true;
}

bool collect_declared_value(const Json &descriptor, const Json &value,
                            std::vector<std::string> &path, const TemplateIndex &subtreeIndex,
                            const TemplateIndex &sceneIndex, int ownerId,
                            std::optional<int> componentId, std::string_view sourceSceneIdentity,
                            Json &bindings, std::set<std::string> &sites, std::string &error)
{
    if (!descriptor.is_object() || !descriptor.contains("kind") || !descriptor["kind"].is_string())
    {
        error = "compiled reference metadata contains a malformed field descriptor";
        return false;
    }
    const std::string kind = descriptor["kind"].get<std::string>();
    if (kind == "optional")
    {
        if (value.is_null()) return true;
        const auto element = descriptor.find("element");
        if (element == descriptor.end())
        {
            error = "optional reference metadata is missing its element descriptor";
            return false;
        }
        return collect_declared_value(*element, value, path, subtreeIndex, sceneIndex,
                                      ownerId, componentId, sourceSceneIdentity,
                                      bindings, sites, error);
    }
    if (kind == "array")
    {
        if (!value.is_array())
        {
            error = "compiled array reference field is not an array in the template subtree";
            return false;
        }
        const auto element = descriptor.find("element");
        if (element == descriptor.end())
        {
            error = "array reference metadata is missing its element descriptor";
            return false;
        }
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            path.push_back(std::to_string(index));
            const bool ok = collect_declared_value(*element, value[index], path, subtreeIndex,
                                                   sceneIndex, ownerId, componentId,
                                                   sourceSceneIdentity, bindings, sites, error);
            path.pop_back();
            if (!ok) return false;
        }
        return true;
    }
    if (kind != "object_reference")
        return true;
    const std::string targetKind = descriptor.value("target", std::string("GameObject"));
    if (!is_object_kind(targetKind) && targetKind != "Component")
    {
        error = "compiled object-reference metadata has an unsupported target kind";
        return false;
    }
    Json binding;
    if (!make_binding(subtreeIndex, sceneIndex, ownerId, componentId, path,
                      targetKind, value, sourceSceneIdentity, binding, error))
        return false;
    if (value.is_null()) return true;
    const std::string site = std::to_string(ownerId) + ":" +
        std::to_string(componentId.value_or(-1)) + ":" + pointer_string(path);
    if (!sites.insert(site).second)
    {
        error = "duplicate declared template reference site";
        return false;
    }
    bindings.push_back(std::move(binding));
    return true;
}

const Json *find_behavior_schema(const Json &catalog, std::string_view name)
{
    const auto behaviors = catalog.find("behaviors");
    if (behaviors == catalog.end() || !behaviors->is_array()) return nullptr;
    for (const auto &behavior : *behaviors)
        if (behavior.is_object() && behavior.value("name", std::string{}) == name)
            return &behavior;
    return nullptr;
}

bool append_author_binding(const Json &declaration, const TemplateIndex &subtreeIndex,
                           const TemplateIndex &sceneIndex, std::string_view sourceSceneIdentity,
                           Json &bindings, std::set<std::string> &sites, std::string &error)
{
    if (!declaration.is_object() || !declaration.contains("owner_id") ||
        !declaration.contains("path") || !declaration["path"].is_string() ||
        !declaration.contains("target_kind") || !declaration["target_kind"].is_string())
    {
        error = "author reference declarations require owner_id, path, and target_kind";
        return false;
    }
    const auto owner = read_identity(declaration["owner_id"]);
    std::optional<int> componentId;
    if (declaration.contains("component_id"))
    {
        componentId = read_identity(declaration["component_id"]);
        if (!componentId)
        {
            error = "author reference declaration component_id is invalid";
            return false;
        }
    }
    if (!owner || component_or_object(subtreeIndex, *owner, componentId) == nullptr)
    {
        error = "author reference declaration owner/component is not in the template subtree";
        return false;
    }
    std::vector<std::string> path;
    if (!parse_pointer(declaration["path"].get_ref<const std::string &>(), path) ||
        std::any_of(path.begin(), path.end(), [](const auto &segment) {
            return segment == "id" || segment == "type";
        }))
    {
        error = "author reference declaration path is invalid or targets a discriminator";
        return false;
    }
    const Json *ownerRecord = component_or_object(subtreeIndex, *owner, componentId);
    const Json *value = resolve_pointer(*ownerRecord, path);
    if (value == nullptr)
    {
        error = "author reference declaration path does not resolve in the template subtree";
        return false;
    }
    const std::string targetKind = declaration["target_kind"].get<std::string>();
    if (!is_object_kind(targetKind) && targetKind != "Component")
    {
        error = "author reference declaration has an unsupported target kind";
        return false;
    }
    Json binding;
    if (!make_binding(subtreeIndex, sceneIndex, *owner, componentId, path,
                      targetKind, *value, sourceSceneIdentity, binding, error))
        return false;
    if (value->is_null()) return true;
    const std::string site = std::to_string(*owner) + ":" +
        std::to_string(componentId.value_or(-1)) + ":" + declaration["path"].get<std::string>();
    if (!sites.insert(site).second)
    {
        error = "duplicate declared template reference site";
        return false;
    }
    bindings.push_back(std::move(binding));
    return true;
}

bool validate_binding(const Json &binding, const TemplateIndex &index,
                      std::set<std::string> &sites, std::string &error)
{
    if (!binding.is_object())
    {
        error = "reference bindings must be JSON objects";
        return false;
    }
    const auto ownerField = binding.find("owner_id");
    const auto pathField = binding.find("path");
    const auto kindField = binding.find("target_kind");
    const auto scopeField = binding.find("scope");
    if (ownerField == binding.end() || pathField == binding.end() || !pathField->is_string() ||
        kindField == binding.end() || !kindField->is_string() ||
        scopeField == binding.end() || !scopeField->is_string())
    {
        error = "a reference binding requires owner_id, path, target_kind, and scope";
        return false;
    }
    const auto ownerId = read_identity(*ownerField);
    if (!ownerId || !index.objects.contains(*ownerId))
    {
        error = "reference binding owner_id must name an object in the template";
        return false;
    }
    const int componentId = [&] {
        const auto field = binding.find("component_id");
        if (field == binding.end())
            return -1;
        const auto parsed = read_identity(*field);
        return parsed.value_or(-2);
    }();
    const Json *owner = index.objects.at(*ownerId);
    if (componentId == -2)
    {
        error = "reference binding component_id must be a nonnegative integer";
        return false;
    }
    if (componentId >= 0)
    {
        const auto found = index.components.find(componentId);
        const auto componentOwner = index.componentOwners.find(componentId);
        if (found == index.components.end() || componentOwner == index.componentOwners.end() ||
            componentOwner->second != *ownerId)
        {
            error = "reference binding component_id must belong to its owner object";
            return false;
        }
        owner = found->second;
    }

    std::vector<std::string> segments;
    const auto &pointer = pathField->get_ref<const std::string &>();
    if (!parse_pointer(pointer, segments))
    {
        error = "reference binding path is not a valid document-relative JSON pointer";
        return false;
    }
    if (std::any_of(segments.begin(), segments.end(), [](const std::string &segment) {
            return segment == "id" || segment == "type";
        }))
    {
        error = "reference bindings cannot target identity or kind discriminators";
        return false;
    }
    const std::string site = std::to_string(*ownerId) + ":" + std::to_string(componentId) + ":" + pointer;
    if (!sites.insert(site).second)
    {
        error = "template reference binding sites must be unique";
        return false;
    }
    const Json *targetValue = resolve_pointer(*owner, segments);
    const auto targetId = targetValue != nullptr ? read_identity(*targetValue) : std::nullopt;
    if (!targetId)
    {
        error = "a declared reference binding must resolve to a nonnegative integer target ID";
        return false;
    }

    const std::string &kind = kindField->get_ref<const std::string &>();
    const std::string &scope = scopeField->get_ref<const std::string &>();
    if (!is_object_kind(kind) && kind != "Component")
    {
        error = "reference binding target_kind must be GameObject, Object, or Component";
        return false;
    }
    if (scope == "internal")
    {
        bool targetExists = false;
        if (kind == "GameObject" || kind == "Object")
            targetExists = index.objects.contains(*targetId);
        else if (kind == "Component")
            targetExists = index.components.contains(*targetId);
        else
        {
            error = "internal reference target_kind must be GameObject, Object, or Component";
            return false;
        }
        if (!targetExists)
        {
            error = "internal reference target ID must resolve to a compatible identity in the template";
            return false;
        }
    }
    else if (scope == "external")
    {
        const auto sourceScene = binding.find("source_scene_identity");
        const auto originalKind = binding.find("original_target_kind");
        const auto originalId = binding.find("original_target_id");
        const auto parsedOriginalId = originalId != binding.end() ? read_identity(*originalId) : std::nullopt;
        if (index.allIds.contains(*targetId) ||
            sourceScene == binding.end() || !sourceScene->is_string() || sourceScene->get<std::string>().empty() ||
            originalKind == binding.end() || !originalKind->is_string() ||
            originalKind->get<std::string>() != kind || !parsedOriginalId || *parsedOriginalId != *targetId)
        {
            error = "external bindings require source-scene identity and matching original target kind/ID";
            return false;
        }
    }
    else
    {
        error = "reference binding scope must be internal or external";
        return false;
    }
    return true;
}

std::string validate_envelope(const Json &document)
{
    if (!document.is_object())
        return "template envelope must be a JSON object";
    const auto format = document.find("format");
    const auto version = document.find("version");
    const auto name = document.find("name");
    const auto root = document.find("root");
    const auto bindings = document.find("reference_bindings");
    if (format == document.end() || !format->is_string() ||
        format->get<std::string>() != "platformator.object-template")
        return "file is not a Platformator object template";
    if (version == document.end() || (!version->is_number_integer() && !version->is_number_unsigned()))
        return "template version is missing or invalid";
    if (version->get<int>() != 1)
        return "unsupported object template version; this editor supports version 1";
    if (name == document.end() || !name->is_string() || name->get<std::string>().empty())
        return "template name must be a nonempty string";
    if (root == document.end())
        return "template root is missing";
    if (bindings == document.end() || !bindings->is_array())
        return "template reference_bindings must be an array";

    TemplateIndex index;
    std::string error;
    if (!collect_object(*root, index, error))
        return error;
    std::set<std::string> sites;
    for (const auto &binding : *bindings)
        if (!validate_binding(binding, index, sites, error))
            return error;
    return {};
}
} // namespace

TemplateAssetV1::TemplateAssetV1(Json envelope) : envelope_(std::move(envelope)) {}
const Json &TemplateAssetV1::root() const { return envelope_.at("root"); }
const Json &TemplateAssetV1::reference_bindings() const
{
    return envelope_.at("reference_bindings");
}
const Json &TemplateAssetV1::envelope() const noexcept { return envelope_; }
Json TemplateAssetV1::serialize() const { return envelope_; }

Json make_template_envelope(std::string name, Json root, Json referenceBindings)
{
    return Json{{"format", "platformator.object-template"}, {"version", 1},
                {"name", std::move(name)}, {"root", std::move(root)},
                {"reference_bindings", std::move(referenceBindings)}};
}

TemplateLoadResult load_template_envelope(Json document)
{
    try
    {
        const std::string error = validate_envelope(document);
        if (!error.empty())
            return {{}, error};
        return {TemplateAssetV1(std::move(document)), {}};
    }
    catch (const std::exception &error)
    {
        return {{}, std::string("template envelope is invalid: ") + error.what()};
    }
}

TemplateLoadResult parse_template_envelope(std::string_view text)
{
    try
    {
        return load_template_envelope(Json::parse(text));
    }
    catch (const std::exception &error)
    {
        return {{}, std::string("template JSON could not be parsed: ") + error.what()};
    }
}

TemplateBindingCollection template_asset_binding_coverage(const TemplateAssetV1 &asset)
{
    TemplateBindingCollection coverage;
    const auto checked = load_template_envelope(asset.envelope());
    if (!checked)
    {
        coverage.error = checked.error;
        return coverage;
    }
    coverage.bindings = checked.asset->reference_bindings();
    coverage.validatedSubtree = checked.asset->root();
    // A saved template's explicit, validated binding list is its reference contract.
    // Opaque integer fields remain opaque and are never inferred as references.
    coverage.assured = true;
    return coverage;
}

std::string template_scene_identity(const DocumentSession &document)
{
    if (!document.sourcePath)
        return {};
    std::error_code error;
    auto identity = std::filesystem::weakly_canonical(*document.sourcePath, error);
    if (error)
    {
        error.clear();
        identity = std::filesystem::absolute(*document.sourcePath, error);
        if (error)
            identity = document.sourcePath->lexically_normal();
    }
    std::string result = identity.lexically_normal().generic_string();
    if (document.sourceIdentity)
    {
        result += "|file:" + std::to_string(document.sourceIdentity->device) + ":" +
                  std::to_string(document.sourceIdentity->inode);
    }
    return result;
}

TemplateBindingCollection collect_template_bindings(
    const DocumentSession &sourceDocument, const Json &subtree,
    std::string_view sourceSceneIdentity, const Json &catalog,
    const std::vector<Json> &authorBindings,
    const std::vector<TemplateAuthorSchemaDeclaration> &authorDeclaredBehaviors)
{
    TemplateBindingCollection result;
    try
    {
        TemplateIndex subtreeIndex;
        TemplateIndex sceneIndex;
        std::string error;
        if (!collect_object(subtree, subtreeIndex, error))
        {
            result.error = std::move(error);
            return result;
        }
        if (!sourceDocument.root.is_array())
        {
            result.error = "source scene root must be an array";
            return result;
        }
        for (const auto &object : sourceDocument.root)
            if (!collect_object(object, sceneIndex, error))
            {
                result.error = std::move(error);
                return result;
            }

        std::set<std::string> sites;
        std::vector<const Json *> pending{&subtree};
        while (!pending.empty())
        {
            const Json &object = *pending.back();
            pending.pop_back();
            const int objectId = object.at("id").get<int>();
            for (const auto &component : object.at("components"))
            {
                if (component.at("type").get<int>() != SCRIPT)
                    continue;
                const int componentId = component.at("id").get<int>();
                const auto behaviorRecords = component.find("behaviors");
                if (behaviorRecords == component.end() || !behaviorRecords->is_array())
                {
                    result.error = "script component behaviors must be an array before template export";
                    return result;
                }
                for (const auto &behaviorRecord : *behaviorRecords)
                {
                    if (!behaviorRecord.is_object() || !behaviorRecord.contains("type") ||
                        !behaviorRecord["type"].is_string())
                    {
                        result.error = "template behavior record has no string type discriminator";
                        return result;
                    }
                    const std::string behaviorName = behaviorRecord["type"].get<std::string>();
                    std::size_t behaviorOrdinal = 0;
                    for (; behaviorOrdinal < behaviorRecords->size(); ++behaviorOrdinal)
                        if (&behaviorRecords->at(behaviorOrdinal) == &behaviorRecord)
                            break;
                    const bool authorDeclared = std::any_of(
                        authorDeclaredBehaviors.begin(), authorDeclaredBehaviors.end(),
                        [&](const TemplateAuthorSchemaDeclaration &declaration) {
                            return declaration.ownerId == objectId &&
                                   declaration.componentId == componentId &&
                                   declaration.behaviorOrdinal == behaviorOrdinal &&
                                   declaration.behaviorType == behaviorName;
                        });
                    const Json *schema = find_behavior_schema(catalog, behaviorName);
                    const bool schemaAvailable = schema != nullptr && schema->is_object() &&
                        schema->value("field_schema_available", false) &&
                        schema->contains("fields") && schema->at("fields").is_array();
                    if (!schemaAvailable)
                    {
                        if (!authorDeclared)
                        {
                            result.unresolvedBehaviors.push_back(
                                behaviorName + " (object #" + std::to_string(objectId) +
                                ", component #" + std::to_string(componentId) + ")");
                            continue;
                        }
                        continue;
                    }
                    for (const auto &descriptor : schema->at("fields"))
                    {
                        if (!descriptor.is_object() || !descriptor.contains("name") ||
                            !descriptor["name"].is_string())
                        {
                            result.error = "compiled behavior catalog contains a malformed field name";
                            return result;
                        }
                        const std::string fieldName = descriptor["name"].get<std::string>();
                        const auto value = behaviorRecord.find(fieldName);
                        if (value == behaviorRecord.end())
                            continue;
                        std::vector<std::string> path{"behaviors"};
                        path.push_back(std::to_string(behaviorOrdinal));
                        path.push_back(fieldName);
                        if (!collect_declared_value(descriptor, *value, path, subtreeIndex,
                                                    sceneIndex, objectId, componentId,
                                                    sourceSceneIdentity, result.bindings,
                                                    sites, result.error))
                            return result;
                    }
                }
            }
            for (const auto &child : object.at("children"))
                pending.push_back(&child);
        }

        for (const auto &declaration : authorBindings)
        {
            if (!append_author_binding(declaration, subtreeIndex, sceneIndex,
                                       sourceSceneIdentity, result.bindings, sites, result.error))
                return result;
        }
        result.assured = result.unresolvedBehaviors.empty();
        result.validatedSubtree = subtree;
        return result;
    }
    catch (const std::exception &error)
    {
        result.error = std::string("could not collect template references: ") + error.what();
        return result;
    }
}

TemplateInsertResult insert_template(
    EditCommandService &commands, DocumentSession &destination,
    const TemplateAssetV1 &asset, std::string_view destinationSceneIdentity,
    const TemplateBindingCollection &bindingCoverage, std::optional<int> parentObjectId,
    std::size_t insertionIndex,
    const std::vector<TemplateExternalAssignment> &externalAssignments,
    SelectionState selection)
{
    TemplateInsertResult result;
    if (!bindingCoverage.assured || bindingCoverage.validatedSubtree != asset.root() ||
        bindingCoverage.bindings != asset.reference_bindings())
    {
        result.error = "template reference coverage is incomplete; declare unavailable schemas before instantiation";
        return result;
    }
    const auto validated = load_template_envelope(asset.envelope());
    if (!validated)
    {
        result.error = validated.error;
        return result;
    }
    try
    {
        rebuild_document_indexes(destination);
        if (parentObjectId && !destination.indexes.objectsById.contains(*parentObjectId))
        {
            result.error = "template destination parent is not present in this scene";
            return result;
        }
        Json *siblings = destination_siblings(destination, parentObjectId);
        if (siblings == nullptr)
        {
            result.error = "template destination sibling collection is unavailable";
            return result;
        }
        if (insertionIndex == std::numeric_limits<std::size_t>::max())
            insertionIndex = siblings->size();
        if (insertionIndex > siblings->size())
        {
            result.error = "template insertion index is out of range";
            return result;
        }

        TemplateIndex templateIndex;
        std::string indexError;
        if (!collect_object(asset.root(), templateIndex, indexError))
        {
            result.error = std::move(indexError);
            return result;
        }
        std::unordered_set<int> usedIds;
        for (const auto &[id, location] : destination.indexes.objectsById)
        {
            (void)location;
            usedIds.insert(id);
        }
        for (const auto &[id, location] : destination.indexes.componentsById)
        {
            (void)location;
            usedIds.insert(id);
        }
        int candidate = 0;
        for (const int sourceId : templateIndex.orderedIds)
        {
            while (usedIds.contains(candidate))
            {
                if (candidate == std::numeric_limits<int>::max())
                {
                    result.error = "destination scene has no remaining object/component IDs";
                    return result;
                }
                ++candidate;
            }
            result.sourceToDestinationIds.emplace(sourceId, candidate);
            usedIds.insert(candidate);
            if (candidate != std::numeric_limits<int>::max())
                ++candidate;
        }

        Json inserted = asset.root();
        std::vector<int> insertedObjects;
        std::vector<Json *> objectStack{&inserted};
        while (!objectStack.empty())
        {
            Json *object = objectStack.back();
            objectStack.pop_back();
            const int oldObjectId = object->at("id").get<int>();
            const auto mappedObject = result.sourceToDestinationIds.find(oldObjectId);
            if (mappedObject == result.sourceToDestinationIds.end())
            {
                result.error = "template object identity has no destination allocation";
                return result;
            }
            object->at("id") = mappedObject->second;
            insertedObjects.push_back(mappedObject->second);
            for (auto &component : object->at("components"))
            {
                const int oldComponentId = component.at("id").get<int>();
                const auto mappedComponent = result.sourceToDestinationIds.find(oldComponentId);
                if (mappedComponent == result.sourceToDestinationIds.end())
                {
                    result.error = "template component identity has no destination allocation";
                    return result;
                }
                component.at("id") = mappedComponent->second;
            }
            for (auto &child : object->at("children"))
                objectStack.push_back(&child);
        }

        std::map<std::string, int> assignments;
        for (const auto &assignment : externalAssignments)
        {
            const std::string key = std::to_string(assignment.ownerId) + ":" +
                std::to_string(assignment.componentId.value_or(-1)) + ":" + assignment.path;
            if (!assignments.emplace(key, assignment.destinationTargetId).second)
            {
                result.error = "template external reference has more than one destination assignment";
                return result;
            }
        }
        std::set<std::string> consumedAssignments;
        const std::string resolvedDestinationIdentity = destinationSceneIdentity.empty()
            ? template_scene_identity(destination) : std::string(destinationSceneIdentity);
        for (const auto &binding : asset.reference_bindings())
        {
            const int ownerId = binding.at("owner_id").get<int>();
            const int sourceComponentId = binding.value("component_id", -1);
            const std::optional<int> componentId = sourceComponentId >= 0
                ? std::optional<int>(sourceComponentId) : std::nullopt;
            const std::string pathText = binding.at("path").get<std::string>();
            std::vector<std::string> path;
            if (!parse_pointer(pathText, path))
            {
                result.error = "template contains an invalid reference path";
                return result;
            }
            const auto mappedOwner = result.sourceToDestinationIds.find(ownerId);
            const auto mappedComponent = componentId
                ? result.sourceToDestinationIds.find(*componentId)
                : result.sourceToDestinationIds.end();
            if (mappedOwner == result.sourceToDestinationIds.end() ||
                (componentId && mappedComponent == result.sourceToDestinationIds.end()))
            {
                result.error = "template reference owner has no destination identity";
                return result;
            }
            Json *owner = component_or_object(inserted, mappedOwner->second,
                componentId ? std::optional<int>(mappedComponent->second) : std::nullopt);
            Json *target = owner != nullptr ? resolve_pointer(*owner, path) : nullptr;
            if (target == nullptr)
            {
                result.error = "template reference path no longer resolves after cloning";
                return result;
            }
            const int originalTarget = target->get<int>();
            const std::string kind = binding.at("target_kind").get<std::string>();
            if (binding.at("scope") == "internal")
            {
                const auto mappedTarget = result.sourceToDestinationIds.find(originalTarget);
                if (mappedTarget == result.sourceToDestinationIds.end() ||
                    !target_exists(templateIndex, originalTarget, kind))
                {
                    result.error = "declared internal reference does not resolve to its typed template target";
                    return result;
                }
                *target = mappedTarget->second;
                continue;
            }

            const std::string assignmentKey = std::to_string(ownerId) + ":" +
                std::to_string(componentId.value_or(-1)) + ":" + pathText;
            int destinationTarget = originalTarget;
            const auto assignment = assignments.find(assignmentKey);
            if (assignment != assignments.end())
            {
                destinationTarget = assignment->second;
                consumedAssignments.insert(assignmentKey);
            }
            else if (binding.at("source_scene_identity").get<std::string>() !=
                     resolvedDestinationIdentity)
            {
                result.error = "external template reference needs an explicit destination assignment";
                return result;
            }
            if (!target_exists(destination, destinationTarget, kind))
            {
                result.error = "external template assignment is missing or has the wrong target kind";
                return result;
            }
            if (assignment == assignments.end() &&
                binding.at("original_target_id").get<int>() != destinationTarget)
            {
                result.error = "external template target identity does not match its original target";
                return result;
            }
            *target = destinationTarget;
        }
        if (consumedAssignments.size() != assignments.size())
        {
            result.error = "an external destination assignment does not match a declared template binding";
            return result;
        }

        const int insertedRootId = inserted.at("id").get<int>();
        StructuralCommand command{destination.documentGeneration,
                                  StructuralCommandKind::InsertObject,
                                  parentObjectId, insertedRootId, std::nullopt,
                                  insertionIndex, std::move(inserted), std::move(selection)};
        const auto committed = commands.apply(destination, command, CommandDirection::Forward);
        result.status = committed.status;
        if (!committed)
        {
            result.error = committed.message.empty() ? "template insertion was rejected" : committed.message;
            result.sourceToDestinationIds.clear();
            return result;
        }
        result.insertedObjectIds = std::move(insertedObjects);
        return result;
    }
    catch (const std::exception &error)
    {
        result.sourceToDestinationIds.clear();
        result.error = std::string("could not prepare template insertion: ") + error.what();
        return result;
    }
}
} // namespace platformator_editor
