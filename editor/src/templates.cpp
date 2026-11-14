#include "templates.h"
#include "documentio.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>
#include <platformator/gameobject.h>
#include "componenttypes.h"
#include "selection.h"

namespace platformator_editor
{
namespace
{
std::vector<std::string> template_source_paths(const Json &sourceNode);

bool is_game_object_envelope_metadata_key(std::string_view key) noexcept
{
    return key == "id" || key == "template" || key == "templateNodeId" ||
        key == "templateSourceKeys" || key == "templateSourcePaths" ||
        key == "_editorTemplateReferenceBindings" || key == "overrides" ||
        key == "components" || key == "children";
}

bool is_component_envelope_metadata_key(std::string_view key) noexcept
{
    return key == "id" || key == "templateComponentId" ||
        key == "templateSourceFields" || key == "templateSourcePaths";
}

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

void strip_nested_template_metadata(Json &subtree)
{
    if (!subtree.is_object())
        return;
    subtree.erase("template");
    subtree.erase("templateNodeId");
    subtree.erase("templateSourceKeys");
    subtree.erase("templateSourcePaths");
    subtree.erase("overrides");
    subtree.erase(std::string(EditorTemplateBindingsKey));
    if (subtree.contains("components") && subtree["components"].is_array())
        for (auto &component : subtree["components"])
            if (component.is_object())
            {
                component.erase("templateComponentId");
                component.erase("templateSourceFields");
                component.erase("templateSourcePaths");
            }
    auto childrenIt = subtree.find("children");
    if (childrenIt != subtree.end() && childrenIt->is_array())
    {
        for (auto &child : *childrenIt)
            strip_nested_template_metadata(child);
    }
}

Json make_template_envelope(std::string name, Json root, Json referenceBindings)
{
    strip_nested_template_metadata(root);
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
    SelectionState selection,
    std::string_view linkedTemplatePath,
    std::optional<std::pair<float, float>> rootPosition)
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
        if (rootPosition)
        {
            if (!inserted.contains("position") || !inserted["position"].is_object())
                inserted["position"] = Json::object();
            inserted["position"]["x"] = rootPosition->first;
            inserted["position"]["y"] = rootPosition->second;
        }
        std::vector<int> insertedObjects;
        std::vector<Json *> objectStack{&inserted};
        bool isRoot = true;
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
            if (!linkedTemplatePath.empty())
            {
                if (isRoot)
                    (*object)["template"] = std::string(linkedTemplatePath);
                (*object)["templateNodeId"] = oldObjectId;
                std::vector<std::string> sourceKeys;
                for (auto it = object->begin(); it != object->end(); ++it)
                    if (!is_game_object_envelope_metadata_key(it.key()))
                        sourceKeys.push_back(it.key());
                (*object)["templateSourceKeys"] = std::move(sourceKeys);
            }
            isRoot = false;
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
                if (!linkedTemplatePath.empty())
                {
                    component["templateComponentId"] = oldComponentId;
                    std::vector<std::string> sourceFields;
                    for (auto it = component.begin(); it != component.end(); ++it)
                        if (!is_component_envelope_metadata_key(it.key()))
                            sourceFields.push_back(it.key());
                    component["templateSourceFields"] = std::move(sourceFields);
                }
            }
            if (!linkedTemplatePath.empty())
                (*object)["templateSourcePaths"] = template_source_paths(*object);
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

bool instantiate_asset_drop(DocumentSession &document, EditCommandService &commands,
                            SelectionModel &selection, const std::string &assetPath,
                            EditorAssetKind kind, float targetX, float targetY,
                            std::optional<int> parentObjectId)
{
    std::filesystem::path p(assetPath);
    std::string stemName = p.stem().string();
    if (stemName.empty())
        stemName = "GameObject";

    if (kind == EditorAssetKind::Template || p.extension() == ".pttemplate")
    {
        std::filesystem::path fullPath = p;
        std::error_code fileError;
        if (!std::filesystem::exists(fullPath, fileError) && !document.assetRoot.empty())
        {
            auto rel = p;
            if (p.is_relative() && p.string().starts_with("assets/"))
                rel = p.string().substr(7);
            fullPath = document.assetRoot / rel;
            fileError.clear();
            if (!std::filesystem::exists(fullPath, fileError))
                fullPath = document.assetRoot / p;
        }
        std::ifstream file(fullPath);
        if (file.is_open())
        {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto loadResult = parse_template_envelope(content);
            if (loadResult)
            {
                auto coverage = template_asset_binding_coverage(*loadResult.asset);
                const std::string sceneId = template_scene_identity(document);
                const std::string normalizedRef = normalize_template_asset_ref(document.assetRoot, fullPath);
                const std::optional<std::pair<float, float>> rootPosition =
                    targetX != 0.0f || targetY != 0.0f
                        ? std::optional<std::pair<float, float>>{{targetX, targetY}}
                        : std::nullopt;
                auto insertResult = insert_template(commands, document, *loadResult.asset,
                                                    sceneId, coverage, parentObjectId,
                                                    std::numeric_limits<std::size_t>::max(),
                                                    {}, {}, normalizedRef, rootPosition);
                if (insertResult && !insertResult.insertedObjectIds.empty())
                {
                    const int rootObjId = insertResult.insertedObjectIds.front();
                    Selection sel;
                    sel.objectId = rootObjId;
                    sel.selectedObjectIds = {rootObjId};
                    sel.focusedPanel = FocusedPanel::Scene;
                    selection.set(document, sel);
                    return true;
                }
            }
        }
    }

    if (kind == EditorAssetKind::Script && parentObjectId)
    {
        std::string behaviorType = stemName;
        std::optional<int> scriptId;
        const auto loc = document.indexes.objectsById.find(*parentObjectId);
        if (loc != document.indexes.objectsById.end())
        {
            const Json *current = &document.root;
            for (const std::size_t idx : loc->second.childIndices)
            {
                if (current == nullptr || !current->is_array() || idx >= current->size())
                {
                    current = nullptr;
                    break;
                }
                current = &(*current)[idx];
                if (idx != loc->second.childIndices.back())
                {
                    const auto children = current->find("children");
                    if (children == current->end() || !children->is_array())
                    {
                        current = nullptr;
                        break;
                    }
                    current = &(*children);
                }
            }
            if (current != nullptr && current->contains("components") && (*current)["components"].is_array())
            {
                for (const auto &comp : (*current)["components"])
                {
                    if (comp.is_object() && comp.value("type", -1) == 5)
                    {
                        scriptId = comp.value("id", -1);
                        break;
                    }
                }
            }
        }
        if (scriptId && *scriptId >= 0)
        {
            commands.add_behavior(document, *parentObjectId, *scriptId, Json{{"type", behaviorType}});
        }
        else
        {
            Json script = default_component_record(5);
            script["behaviors"].push_back(Json{{"type", behaviorType}});
            commands.add_component(document, *parentObjectId, std::move(script));
        }
        return true;
    }

    auto createResult = commands.create_object(document, parentObjectId, stemName);
    if (createResult.status == CommandStatus::Changed && createResult.command)
    {
        const int newId = createResult.command->payload.value("id", 0);
        if (newId != 0)
        {
            if (targetX != 0.0f || targetY != 0.0f)
            {
                commands.set_property(document, address_for_document(document, newId, std::nullopt, std::nullopt, {std::string("position"), std::string("x")}), targetX);
                commands.set_property(document, address_for_document(document, newId, std::nullopt, std::nullopt, {std::string("position"), std::string("y")}), targetY);
            }

            if (kind == EditorAssetKind::Image || editor_asset_format(p).textureReference)
            {
                Json sprite = default_component_record(6);
                sprite["textureFilePath"] = assetPath;
                commands.add_component(document, newId, std::move(sprite));
            }
            else if (kind == EditorAssetKind::Audio || editor_asset_format(p).audioReference)
            {
                Json audio = default_component_record(1);
                audio["filePath"] = assetPath;
                commands.add_component(document, newId, std::move(audio));
            }
            else if (kind == EditorAssetKind::Script)
            {
                Json script = default_component_record(5);
                script["behaviors"].push_back(Json{{"type", stemName}});
                commands.add_component(document, newId, std::move(script));
            }

            Selection sel;
            sel.objectId = newId;
            sel.selectedObjectIds = {newId};
            sel.focusedPanel = FocusedPanel::Scene;
            selection.set(document, sel);
            return true;
        }
    }
    return false;
}

namespace
{
const Json *resolve_object_at(const Json &root, const ObjectLocation &loc)
{
    const Json *current = &root;
    for (std::size_t i = 0; i < loc.childIndices.size(); ++i)
    {
        if (!current->is_array() || loc.childIndices[i] >= current->size())
            return nullptr;
        current = &(*current)[loc.childIndices[i]];
        if (i + 1 < loc.childIndices.size())
        {
            const auto childrenIt = current->find("children");
            if (childrenIt == current->end() || !childrenIt->is_array())
                return nullptr;
            current = &(*childrenIt);
        }
    }
    return current->is_object() ? current : nullptr;
}

Json *resolve_object_mutable_at(Json &root, const ObjectLocation &loc)
{
    Json *current = &root;
    for (std::size_t i = 0; i < loc.childIndices.size(); ++i)
    {
        if (!current->is_array() || loc.childIndices[i] >= current->size())
            return nullptr;
        current = &(*current)[loc.childIndices[i]];
        if (i + 1 < loc.childIndices.size())
        {
            auto childrenIt = current->find("children");
            if (childrenIt == current->end() || !childrenIt->is_array())
                return nullptr;
            current = &(*childrenIt);
        }
    }
    return current->is_object() ? current : nullptr;
}
} // namespace

const Json *find_document_object_by_id(const DocumentSession &document, int objectId)
{
    const auto loc = document.indexes.objectsById.find(objectId);
    if (loc == document.indexes.objectsById.end())
        return nullptr;
    return resolve_object_at(document.root, loc->second);
}

bool is_template_instance_root(const Json &object) noexcept
{
    return object.is_object() && object.contains("template") && object["template"].is_string() &&
           !object["template"].get<std::string>().empty();
}

bool is_template_node(const Json &object) noexcept
{
    return is_template_instance_root(object) ||
           (object.is_object() && object.contains("templateNodeId") && object["templateNodeId"].is_number_integer());
}

std::optional<int> find_template_instance_root(const DocumentSession &document, int objectId)
{
    std::optional<int> current = objectId;
    while (current)
    {
        const auto loc = document.indexes.objectsById.find(*current);
        if (loc == document.indexes.objectsById.end())
            return std::nullopt;
        const Json *obj = resolve_object_at(document.root, loc->second);
        if (obj == nullptr)
            return std::nullopt;
        if (is_template_instance_root(*obj))
            return *current;
        if (!is_template_node(*obj))
            return std::nullopt;
        const auto parentIt = document.indexes.parentById.find(*current);
        current = parentIt == document.indexes.parentById.end() ? std::nullopt : parentIt->second;
    }
    return std::nullopt;
}

bool is_property_overridden(const Json &object, std::string_view fieldPath)
{
    if (!object.is_object())
        return false;
    const auto it = object.find("overrides");
    if (it == object.end() || !it->is_array())
        return false;
    for (const auto &entry : *it)
    {
        if (entry.is_string() && entry.get<std::string>() == fieldPath)
            return true;
    }
    return false;
}

std::string template_override_path_for_address(const DocumentSession &document,
                                              const PropertyAddress &address)
{
    std::string path;
    if (address.componentId)
    {
        const auto componentLocation = document.indexes.componentsById.find(*address.componentId);
        const Json *object = find_document_object_by_id(document, address.objectId);
        if (componentLocation == document.indexes.componentsById.end() || object == nullptr ||
            !object->contains("components") || !(*object)["components"].is_array() ||
            componentLocation->second.componentIndex >= (*object)["components"].size())
            return {};
        const Json &component = (*object)["components"][componentLocation->second.componentIndex];
        const int templateId = component.value("templateComponentId", component.value("id", -1));
        path = "component[id=" + std::to_string(templateId) + "].";
        if (address.behaviorOrdinal)
            path += "behaviors[" + std::to_string(*address.behaviorOrdinal) + "].";
    }
    for (const auto &segment : address.fieldPath)
    {
        if (const auto *key = std::get_if<std::string>(&segment))
        {
            if (!path.empty() && path.back() != '.')
                path.push_back('.');
            path += *key;
        }
        else
        {
            path.push_back('[');
            path += std::to_string(std::get<std::size_t>(segment));
            path.push_back(']');
        }
    }
    return path;
}

void mark_template_override(DocumentSession &document, int objectId, std::string_view fieldPath)
{
    if (fieldPath.empty())
        return;
    const auto loc = document.indexes.objectsById.find(objectId);
    if (loc == document.indexes.objectsById.end())
        return;
    Json *obj = resolve_object_mutable_at(document.root, loc->second);
    if (obj == nullptr || !is_template_node(*obj))
        return;
    if (!obj->contains("overrides") || !(*obj)["overrides"].is_array())
        (*obj)["overrides"] = Json::array();
    auto &overrides = (*obj)["overrides"];
    for (const auto &entry : overrides)
    {
        if (entry.is_string() && entry.get<std::string>() == fieldPath)
            return;
    }
    overrides.push_back(std::string(fieldPath));
}

std::filesystem::path resolve_template_asset_path(const std::filesystem::path &assetRoot,
                                                  std::string_view templateRef)
{
    std::filesystem::path p(templateRef);
    if (p.is_absolute() || assetRoot.empty())
        return p;
    std::string raw = p.generic_string();
    if (raw.starts_with("assets/"))
        raw = raw.substr(7);
    std::filesystem::path candidate = assetRoot / raw;
    std::error_code fileError;
    if (std::filesystem::exists(candidate, fileError))
        return candidate;
    return assetRoot / p;
}

std::string normalize_template_asset_ref(const std::filesystem::path &assetRoot,
                                         const std::filesystem::path &fullPath)
{
    if (assetRoot.empty())
        return fullPath.generic_string();
    std::error_code ec;
    const auto canonRoot = std::filesystem::weakly_canonical(assetRoot, ec);
    const auto canonFull = std::filesystem::weakly_canonical(fullPath, ec);
    const auto rel = std::filesystem::relative(canonFull, canonRoot, ec);
    if (!ec && !rel.empty() && !rel.generic_string().starts_with(".."))
        return rel.generic_string();
    return fullPath.generic_string();
}

namespace
{
std::vector<BatchOperation> object_snapshot_operations(const DocumentSession &document,
                                                       int objectId, const Json &before,
                                                       const Json &after)
{
    std::set<std::string> keys;
    for (auto it = before.begin(); it != before.end(); ++it)
        if (it.key() != "id" && it.key() != "components" && it.key() != "children")
            keys.insert(it.key());
    for (auto it = after.begin(); it != after.end(); ++it)
        if (it.key() != "id" && it.key() != "components" && it.key() != "children")
            keys.insert(it.key());

    std::vector<BatchOperation> operations;
    operations.reserve(keys.size());
    for (const auto &key : keys)
    {
        const auto old = before.find(key);
        const auto next = after.find(key);
        if (old != before.end() && next != after.end() && *old == *next)
            continue;
        PropertyAddress address = address_for_document(document, objectId, std::nullopt,
                                                        std::nullopt, {key});
        PropertyCommand command;
        command.address = std::move(address);
        command.before = old == before.end() ? PropertyValue::missing() : PropertyValue::of(*old);
        command.after = next == after.end() ? PropertyValue::missing() : PropertyValue::of(*next);
        command.selection = {};
        operations.emplace_back(std::move(command));
    }

    const auto children_of = [](const Json &object) {
        const auto children = object.find("children");
        return children != object.end() && children->is_array() ? *children : Json::array();
    };
    const auto components_of = [](const Json &object) {
        const auto components = object.find("components");
        return components != object.end() && components->is_array() ? *components : Json::array();
    };
    const Json beforeChildren = children_of(before);
    const Json afterChildren = children_of(after);
    if (beforeChildren != afterChildren)
    {
        for (std::size_t index = beforeChildren.size(); index > 0; --index)
        {
            const Json &child = beforeChildren[index - 1];
            StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteObject,
                objectId, child.value("id", -1), std::nullopt, index - 1, child, {}};
            operations.emplace_back(std::move(command));
        }
        for (std::size_t index = 0; index < afterChildren.size(); ++index)
        {
            const Json &child = afterChildren[index];
            StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertObject,
                objectId, child.value("id", -1), std::nullopt, index, child, {}};
            operations.emplace_back(std::move(command));
        }
    }

    const Json beforeComponents = components_of(before);
    const Json afterComponents = components_of(after);
    if (beforeComponents != afterComponents)
    {
        for (std::size_t index = beforeComponents.size(); index > 0; --index)
        {
            const Json &component = beforeComponents[index - 1];
            StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteComponent,
                std::nullopt, objectId, std::nullopt, index - 1, component, {}};
            operations.emplace_back(std::move(command));
        }
        for (std::size_t index = 0; index < afterComponents.size(); ++index)
        {
            const Json &component = afterComponents[index];
            StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertComponent,
                std::nullopt, objectId, std::nullopt, index, component, {}};
            operations.emplace_back(std::move(command));
        }
    }
    return operations;
}

CommandResult apply_object_snapshot(DocumentSession &document, EditCommandService &commands,
                                    int objectId, const Json &before, const Json &after)
{
    if (!before.is_object() || !after.is_object())
        return {CommandStatus::Rejected, "template object snapshot is invalid"};
    auto operations = object_snapshot_operations(document, objectId, before, after);
    if (operations.empty())
        return {CommandStatus::NoChange, {}};
    return commands.apply_batch(document, operations);
}

void clear_overrides_recursive(Json &object)
{
    if (!object.is_object())
        return;
    object.erase("overrides");
    if (object.contains("children") && object["children"].is_array())
    {
        for (auto &child : object["children"])
            clear_overrides_recursive(child);
    }
}

void append_template_source_paths(const Json &value, const std::string &path,
                                  std::vector<std::string> &paths)
{
    if (!path.empty())
        paths.push_back(path);
    if (value.is_object())
    {
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            const std::string childPath = path.empty() ? it.key() : path + "." + it.key();
            append_template_source_paths(it.value(), childPath, paths);
        }
    }
    else if (value.is_array())
    {
        for (std::size_t i = 0; i < value.size(); ++i)
            append_template_source_paths(value[i], path + "[" + std::to_string(i) + "]", paths);
    }
}

std::vector<std::string> template_source_paths(const Json &sourceNode)
{
    std::vector<std::string> paths;
    if (!sourceNode.is_object())
        return paths;
    for (auto it = sourceNode.begin(); it != sourceNode.end(); ++it)
        if (!is_game_object_envelope_metadata_key(it.key()))
            append_template_source_paths(it.value(), it.key(), paths);
    const auto components = sourceNode.find("components");
    if (components != sourceNode.end() && components->is_array())
        for (const auto &component : *components)
        {
            const int componentId = component.value("templateComponentId",
                                                    component.value("id", -1));
            const std::string prefix = "component[id=" + std::to_string(componentId) + "].";
            if (!component.is_object())
                continue;
            for (auto it = component.begin(); it != component.end(); ++it)
            {
                if (is_component_envelope_metadata_key(it.key()))
                    continue;
                append_template_source_paths(it.value(), prefix + it.key(), paths);
            }
        }
    return paths;
}

void sync_node_from_template(Json &instanceNode, const Json &templateNode,
                             std::unordered_set<int> &usedIds, int &nextId,
                             bool isRoot)
{
    auto is_overridden_path = [&](std::string path) {
        if (is_property_overridden(instanceNode, path))
            return true;
        for (std::size_t index = 0; index < path.size(); ++index)
        {
            if (path[index] == '.')
            {
                if (is_property_overridden(instanceNode, path.substr(0, index)))
                    return true;
            }
            else if (path[index] == '[')
            {
                if (is_property_overridden(instanceNode, path.substr(0, index)))
                    return true;
                const auto close = path.find(']', index + 1);
                if (close == std::string::npos)
                    return false;
                if (is_property_overridden(instanceNode, path.substr(0, close + 1)))
                    return true;
                index = close;
            }
        }
        return false;
    };
    const Json previousSourcePaths = instanceNode.value("templateSourcePaths", Json::array());
    std::set<std::string> previouslySourced;
    if (previousSourcePaths.is_array())
        for (const auto &entry : previousSourcePaths)
            if (entry.is_string())
                previouslySourced.insert(entry.get<std::string>());
    if (previouslySourced.empty())
    {
        const Json legacyKeys = instanceNode.value("templateSourceKeys", Json::array());
        if (legacyKeys.is_array())
            for (const auto &entry : legacyKeys)
                if (entry.is_string())
                    previouslySourced.insert(entry.get<std::string>());
        if (instanceNode.contains("components") && instanceNode["components"].is_array())
            for (const auto &component : instanceNode["components"])
            {
                const int sourceId = component.value("templateComponentId", -1);
                const Json fields = component.value("templateSourceFields", Json::array());
                if (!fields.is_array())
                    continue;
                for (const auto &field : fields)
                    if (field.is_string())
                        previouslySourced.insert("component[id=" + std::to_string(sourceId) + "]." +
                                                 field.get<std::string>());
            }
    }
    const auto source_owned_at = [&](const std::string &path) {
        for (const auto &previous : previouslySourced)
            if (previous == path || previous.starts_with(path + ".") ||
                previous.starts_with(path + "["))
                return true;
        return false;
    };
    const auto has_override_at_or_below = [&](const std::string &path) {
        if (is_overridden_path(path))
            return true;
        const auto overrides = instanceNode.find("overrides");
        if (overrides == instanceNode.end() || !overrides->is_array())
            return false;
        for (const auto &entry : *overrides)
            if (entry.is_string())
            {
                const std::string value = entry.get<std::string>();
                if (value.starts_with(path + ".") || value.starts_with(path + "["))
                    return true;
            }
        return false;
    };
    std::vector<std::pair<std::string, std::string>> remappedOverridePaths;
    auto merge_value = [&](auto &&self, const Json &current, const Json &source,
                           const std::string &path) -> Json {
        if (!path.empty() && (is_overridden_path(path) ||
                              (has_override_at_or_below(path) &&
                               current.type() != source.type())))
            return current;
        if (current.is_object() && source.is_object())
        {
            Json merged = current;
            for (auto it = current.begin(); it != current.end(); ++it)
            {
                if (source.contains(it.key()))
                    continue;
                const std::string childPath = path.empty() ? it.key() : path + "." + it.key();
                if (source_owned_at(childPath) && !has_override_at_or_below(childPath))
                    merged.erase(it.key());
            }
            for (auto it = source.begin(); it != source.end(); ++it)
            {
                const auto existing = merged.find(it.key());
                const std::string childPath = path.empty() ? it.key() : path + "." + it.key();
                if (existing == merged.end())
                    merged[it.key()] = it.value();
                else
                    merged[it.key()] = self(self, *existing, it.value(), childPath);
            }
            return merged;
        }
        if (current.is_array() && source.is_array())
        {
            Json merged = Json::array();
            for (std::size_t i = 0; i < source.size(); ++i)
            {
                const std::string childPath = path + "[" + std::to_string(i) + "]";
                if (i >= current.size())
                    merged.push_back(source[i]);
                else
                    merged.push_back(self(self, current[i], source[i], childPath));
            }
            for (std::size_t i = source.size(); i < current.size(); ++i)
            {
                const std::string childPath = path + "[" + std::to_string(i) + "]";
                if (!source_owned_at(childPath) || has_override_at_or_below(childPath))
                {
                    const std::size_t compactedIndex = merged.size();
                    merged.push_back(current[i]);
                    if (compactedIndex != i)
                        remappedOverridePaths.emplace_back(
                            childPath, path + "[" + std::to_string(compactedIndex) + "]");
                }
            }
            return merged;
        }
        return source;
    };

    const std::vector<std::string> nextSourcePaths = template_source_paths(templateNode);
    std::vector<std::string> nextSourceKeys;
    for (auto it = templateNode.begin(); it != templateNode.end(); ++it)
        if (!is_game_object_envelope_metadata_key(it.key())) nextSourceKeys.push_back(it.key());
    const Json previousSourceKeys = instanceNode.value("templateSourceKeys", Json::array());
    if (previousSourceKeys.is_array())
    {
        for (const auto &entry : previousSourceKeys)
        {
            if (!entry.is_string()) continue;
            const std::string key = entry.get<std::string>();
            if (std::find(nextSourceKeys.begin(), nextSourceKeys.end(), key) == nextSourceKeys.end() &&
                !has_override_at_or_below(key))
                instanceNode.erase(key);
        }
    }
    for (const auto &key : nextSourceKeys)
    {
        const std::string path = key;
        const auto current = instanceNode.find(key);
        if (key == "position" && isRoot)
            continue;
        if (current == instanceNode.end())
            instanceNode[key] = templateNode[key];
        else
            instanceNode[key] = merge_value(merge_value, *current, templateNode[key], path);
    }
    instanceNode["templateSourceKeys"] = nextSourceKeys;

    // Sync components by type (matching existing or adding new ones from template).
    if (templateNode.contains("components") && templateNode["components"].is_array())
    {
        Json updatedComponents = Json::array();
        const Json existingComponents = instanceNode.contains("components") && instanceNode["components"].is_array()
            ? instanceNode["components"] : Json::array();
        std::vector<bool> consumed(existingComponents.size(), false);
        for (const auto &tmplComp : templateNode["components"])
        {
            if (!tmplComp.is_object())
                continue;
            const int tmplComponentId = tmplComp.value("id", -1);
            std::optional<std::size_t> matchIdx;
            for (std::size_t i = 0; i < existingComponents.size(); ++i)
            {
                if (!consumed[i] && existingComponents[i].is_object() &&
                    existingComponents[i].value("templateComponentId", -2) == tmplComponentId)
                {
                    matchIdx = i;
                    consumed[i] = true;
                    break;
                }
            }
            if (matchIdx)
            {
                Json merged = existingComponents[*matchIdx];
                const Json priorSourceFields = merged.value("templateSourceFields", Json::array());
                std::vector<std::string> nextSourceFields;
                for (auto it = tmplComp.begin(); it != tmplComp.end(); ++it)
                {
                    if (is_component_envelope_metadata_key(it.key()))
                        continue;
                    nextSourceFields.push_back(it.key());
                    const std::string fieldKey = "component[id=" + std::to_string(tmplComponentId) + "]." + it.key();
                    const auto current = merged.find(it.key());
                    if (current == merged.end())
                        merged[it.key()] = it.value();
                    else if (!is_overridden_path(fieldKey))
                        merged[it.key()] = merge_value(merge_value, *current, it.value(), fieldKey);
                }
                if (priorSourceFields.is_array())
                    for (const auto &key : priorSourceFields)
                        if (key.is_string() &&
                            std::find(nextSourceFields.begin(), nextSourceFields.end(), key.get<std::string>()) ==
                                nextSourceFields.end() &&
                            !has_override_at_or_below("component[id=" + std::to_string(tmplComponentId) + "]." +
                                                      key.get<std::string>()))
                            merged.erase(key.get<std::string>());
                merged["templateComponentId"] = tmplComponentId;
                merged["templateSourceFields"] = nextSourceFields;
                updatedComponents.push_back(std::move(merged));
            }
            else
            {
                Json added = tmplComp;
                while (usedIds.contains(nextId))
                    ++nextId;
                added["id"] = nextId++;
                usedIds.insert(added["id"].get<int>());
                added["templateComponentId"] = tmplComponentId;
                std::vector<std::string> sourceFields;
                for (auto it = tmplComp.begin(); it != tmplComp.end(); ++it)
                    if (!is_component_envelope_metadata_key(it.key()))
                        sourceFields.push_back(it.key());
                added["templateSourceFields"] = std::move(sourceFields);
                updatedComponents.push_back(std::move(added));
            }
        }
        for (std::size_t i = 0; i < existingComponents.size(); ++i)
        {
            if (consumed[i] || !existingComponents[i].is_object() ||
                existingComponents[i].contains("templateComponentId"))
                continue;
            updatedComponents.push_back(existingComponents[i]);
        }
        instanceNode["components"] = std::move(updatedComponents);
    }
    instanceNode["templateSourcePaths"] = nextSourcePaths;

    if (!remappedOverridePaths.empty())
    {
        std::sort(remappedOverridePaths.begin(), remappedOverridePaths.end(),
                  [](const auto &left, const auto &right) {
                      return left.first.size() > right.first.size();
                  });
        Json remappedOverrides = Json::array();
        std::set<std::string> uniqueOverrides;
        const auto overrides = instanceNode.find("overrides");
        if (overrides != instanceNode.end() && overrides->is_array())
            for (const auto &entry : *overrides)
            {
                if (!entry.is_string())
                {
                    remappedOverrides.push_back(entry);
                    continue;
                }
                std::string path = entry.get<std::string>();
                for (const auto &[oldPrefix, newPrefix] : remappedOverridePaths)
                {
                    if (path == oldPrefix || path.starts_with(oldPrefix + ".") ||
                        path.starts_with(oldPrefix + "["))
                    {
                        path.replace(0, oldPrefix.size(), newPrefix);
                        break;
                    }
                }
                if (uniqueOverrides.insert(path).second)
                    remappedOverrides.push_back(std::move(path));
            }
        if (remappedOverrides.empty())
            instanceNode.erase("overrides");
        else
            instanceNode["overrides"] = std::move(remappedOverrides);
    }

    // Sync children by templateNodeId.
    if (templateNode.contains("children") && templateNode["children"].is_array())
    {
        Json updatedChildren = Json::array();
        const Json existingChildren = instanceNode.contains("children") && instanceNode["children"].is_array()
            ? instanceNode["children"] : Json::array();
        std::vector<bool> childConsumed(existingChildren.size(), false);
        for (const auto &tmplChild : templateNode["children"])
        {
            if (!tmplChild.is_object())
                continue;
            const int tmplNodeId = tmplChild.value("id", -1);
            std::optional<std::size_t> matchIdx;
            for (std::size_t i = 0; i < existingChildren.size(); ++i)
            {
                if (!childConsumed[i] && existingChildren[i].is_object() &&
                    existingChildren[i].value("templateNodeId", -2) == tmplNodeId)
                {
                    matchIdx = i;
                    childConsumed[i] = true;
                    break;
                }
            }
            if (matchIdx)
            {
                Json childCopy = existingChildren[*matchIdx];
                sync_node_from_template(childCopy, tmplChild, usedIds, nextId, false);
                updatedChildren.push_back(std::move(childCopy));
            }
            else
            {
                Json newChild = tmplChild;
                // Allocate fresh IDs recursively and stamp templateNodeId.
                std::vector<Json *> stack{&newChild};
                while (!stack.empty())
                {
                    Json *n = stack.back();
                    stack.pop_back();
                    const int origId = n->value("id", -1);
                    (*n)["templateNodeId"] = origId;
                    while (usedIds.contains(nextId))
                        ++nextId;
                    (*n)["id"] = nextId++;
                    usedIds.insert((*n)["id"].get<int>());
                    std::vector<std::string> sourceKeys;
                    for (auto it = n->begin(); it != n->end(); ++it)
                        if (!is_game_object_envelope_metadata_key(it.key()))
                            sourceKeys.push_back(it.key());
                    (*n)["templateSourceKeys"] = std::move(sourceKeys);
                    if (n->contains("components") && (*n)["components"].is_array())
                    {
                        for (auto &c : (*n)["components"])
                        {
                            const int oldComponentId = c.value("id", -1);
                            while (usedIds.contains(nextId))
                                ++nextId;
                            c["id"] = nextId++;
                            usedIds.insert(c["id"].get<int>());
                            c["templateComponentId"] = oldComponentId;
                            std::vector<std::string> sourceFields;
                            for (auto it = c.begin(); it != c.end(); ++it)
                                if (!is_component_envelope_metadata_key(it.key()))
                                    sourceFields.push_back(it.key());
                            c["templateSourceFields"] = std::move(sourceFields);
                        }
                    }
                    (*n)["templateSourcePaths"] = template_source_paths(*n);
                    if (n->contains("children") && (*n)["children"].is_array())
                    {
                        for (auto &gc : (*n)["children"])
                            stack.push_back(&gc);
                    }
                }
                updatedChildren.push_back(std::move(newChild));
            }
        }
        for (std::size_t i = 0; i < existingChildren.size(); ++i)
            if (!childConsumed[i] && existingChildren[i].is_object() &&
                !existingChildren[i].contains("templateNodeId"))
                updatedChildren.push_back(existingChildren[i]);
        instanceNode["children"] = std::move(updatedChildren);
    }
}

using TemplateIdMap = std::unordered_map<int, int>;

void collect_template_instance_ids(const Json &object, TemplateIdMap &sourceToInstance)
{
    if (!object.is_object())
        return;
    const int instanceId = object.value("id", -1);
    const int sourceId = object.value("templateNodeId", instanceId);
    if (sourceId >= 0 && instanceId >= 0)
        sourceToInstance[sourceId] = instanceId;
    if (object.contains("components") && object["components"].is_array())
        for (const auto &component : object["components"])
        {
            if (!component.is_object())
                continue;
            const int instanceComponentId = component.value("id", -1);
            const int sourceComponentId = component.value("templateComponentId", instanceComponentId);
            if (sourceComponentId >= 0 && instanceComponentId >= 0)
                sourceToInstance[sourceComponentId] = instanceComponentId;
        }
    if (object.contains("children") && object["children"].is_array())
        for (const auto &child : object["children"])
            collect_template_instance_ids(child, sourceToInstance);
}

Json *find_object_by_id(Json &object, int objectId)
{
    if (!object.is_object())
        return nullptr;
    if (object.value("id", -1) == objectId)
        return &object;
    auto children = object.find("children");
    if (children != object.end() && children->is_array())
        for (auto &child : *children)
            if (Json *found = find_object_by_id(child, objectId))
                return found;
    return nullptr;
}

std::string binding_override_path(const Json &binding)
{
    std::string result;
    const int componentId = binding.value("component_id", -1);
    if (componentId >= 0)
        result = "component[id=" + std::to_string(componentId) + "].";
    std::vector<std::string> segments;
    if (!parse_pointer(binding.value("path", std::string{}), segments))
        return {};
    for (const auto &segment : segments)
    {
        const bool index = !segment.empty() &&
            std::all_of(segment.begin(), segment.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            });
        if (index)
            result += "[" + segment + "]";
        else
        {
            if (!result.empty() && result.back() != '.')
                result.push_back('.');
            result += segment;
        }
    }
    return result;
}

bool has_override_at_or_above(const Json &object, std::string_view path)
{
    if (!object.is_object() || path.empty())
        return false;
    const std::string value(path);
    if (is_property_overridden(object, value))
        return true;

    std::size_t firstBoundary = 0;
    if (value.starts_with("component[id="))
    {
        const auto close = value.find(']');
        if (close == std::string::npos)
            return false;
        if (is_property_overridden(object, value.substr(0, close + 1)))
            return true;
        firstBoundary = close + 1;
    }
    for (std::size_t index = firstBoundary; index < value.size(); ++index)
    {
        if (value[index] == '.')
        {
            if (is_property_overridden(object, value.substr(0, index)))
                return true;
        }
        else if (value[index] == '[')
        {
            if (is_property_overridden(object, value.substr(0, index)))
                return true;
            const auto close = value.find(']', index + 1);
            if (close == std::string::npos)
                return false;
            if (is_property_overridden(object, value.substr(0, close + 1)))
                return true;
            index = close;
        }
    }
    return false;
}

bool sync_template_reference_values(Json &instanceRoot, const Json &previousInstanceRoot,
                                    const TemplateAssetV1 &asset, std::string &error)
{
    TemplateIdMap currentIds;
    TemplateIdMap previousIds;
    collect_template_instance_ids(instanceRoot, currentIds);
    collect_template_instance_ids(previousInstanceRoot, previousIds);
    TemplateIndex sourceIndex;
    if (!collect_object(asset.root(), sourceIndex, error))
        return false;
    Json previousCopy = previousInstanceRoot;
    for (const auto &binding : asset.reference_bindings())
    {
        const int sourceOwner = binding.value("owner_id", -1);
        const int sourceComponent = binding.value("component_id", -1);
        const auto currentOwnerId = currentIds.find(sourceOwner);
        const auto previousOwnerId = previousIds.find(sourceOwner);
        const auto currentComponentId = sourceComponent >= 0
            ? currentIds.find(sourceComponent) : currentIds.end();
        const auto previousComponentId = sourceComponent >= 0
            ? previousIds.find(sourceComponent) : previousIds.end();
        if (currentOwnerId == currentIds.end() ||
            (sourceComponent >= 0 && currentComponentId == currentIds.end()))
        {
            error = "template reference owner could not be mapped to its instance identity";
            return false;
        }
        std::vector<std::string> path;
        if (!parse_pointer(binding.value("path", std::string{}), path))
        {
            error = "template reference contains an invalid JSON pointer";
            return false;
        }
        Json *currentOwner = component_or_object(instanceRoot, currentOwnerId->second,
            sourceComponent >= 0 ? std::optional<int>(currentComponentId->second) : std::nullopt);
        if (currentOwner == nullptr)
        {
            error = "template reference owner could not be mapped to its instance site";
            return false;
        }
        Json *currentValue = currentOwner != nullptr ? resolve_pointer(*currentOwner, path) : nullptr;
        const Json *sourceOwnerValue = component_or_object(sourceIndex, sourceOwner,
            sourceComponent >= 0 ? std::optional<int>(sourceComponent) : std::nullopt);
        const Json *sourceValue = sourceOwnerValue != nullptr
            ? resolve_pointer(*sourceOwnerValue, path) : nullptr;
        if (sourceValue == nullptr)
        {
            error = "template reference path no longer resolves in its source asset";
            return false;
        }

        Json *instanceOwnerObject = find_object_by_id(instanceRoot, currentOwnerId->second);
        const std::string overridePath = binding_override_path(binding);
        if (instanceOwnerObject != nullptr && !overridePath.empty() &&
            has_override_at_or_above(*instanceOwnerObject, overridePath))
            continue;
        if (currentValue == nullptr)
        {
            error = "template reference path could not be mapped to its instance site";
            return false;
        }

        const std::string scope = binding.value("scope", std::string{});
        if (scope == "internal")
        {
            const auto sourceTarget = read_identity(*sourceValue);
            const auto mappedTarget = sourceTarget ? currentIds.find(*sourceTarget) : currentIds.end();
            if (mappedTarget == currentIds.end())
            {
                error = "internal template reference target could not be mapped to its instance identity";
                return false;
            }
            *currentValue = mappedTarget->second;
        }
        else if (scope == "external")
        {
            if (previousOwnerId != previousIds.end() &&
                (sourceComponent < 0 || previousComponentId != previousIds.end()))
            {
                Json *previousOwner = component_or_object(previousCopy, previousOwnerId->second,
                    sourceComponent >= 0 ? std::optional<int>(previousComponentId->second) : std::nullopt);
                const Json *previousValue = previousOwner != nullptr
                    ? resolve_pointer(*previousOwner, path) : nullptr;
                if (previousValue != nullptr)
                    *currentValue = *previousValue;
            }
        }
        else
        {
            error = "template reference scope is invalid";
            return false;
        }
    }
    return true;
}

bool restore_template_source_ids(Json &root, Json &bindings, std::string &error,
                                TemplateIdMap *destinationToSourceResult = nullptr,
                                bool bindingsUseInstanceIds = false)
{
    TemplateIdMap destinationToSource;
    std::unordered_set<int> reservedSourceIds;
    const auto reserve = [&](const auto &self, const Json &object) -> void {
        if (!object.is_object())
            return;
        if (object.contains("templateNodeId") && object["templateNodeId"].is_number_integer())
            reservedSourceIds.insert(object["templateNodeId"].get<int>());
        if (object.contains("components") && object["components"].is_array())
            for (const auto &component : object["components"])
                if (component.is_object() && component.contains("templateComponentId") &&
                    component["templateComponentId"].is_number_integer())
                    reservedSourceIds.insert(component["templateComponentId"].get<int>());
        if (object.contains("children") && object["children"].is_array())
            for (const auto &child : object["children"])
                self(self, child);
    };
    reserve(reserve, root);
    int nextSourceId = 0;
    for (const int id : reservedSourceIds)
        if (id < std::numeric_limits<int>::max())
            nextSourceId = std::max(nextSourceId, id + 1);
    const auto allocateSourceId = [&] {
        while (reservedSourceIds.contains(nextSourceId) &&
               nextSourceId < std::numeric_limits<int>::max())
            ++nextSourceId;
        const int result = nextSourceId;
        reservedSourceIds.insert(result);
        if (nextSourceId < std::numeric_limits<int>::max())
            ++nextSourceId;
        return result;
    };
    const auto collect = [&](const auto &self, Json &object) -> void {
        if (!object.is_object())
            return;
        const int destinationId = object.value("id", -1);
        const int sourceId = object.contains("templateNodeId")
            ? object.value("templateNodeId", destinationId) : allocateSourceId();
        if (destinationId >= 0 && sourceId >= 0)
        {
            destinationToSource[destinationId] = sourceId;
            object["id"] = sourceId;
        }
        if (object.contains("components") && object["components"].is_array())
            for (auto &component : object["components"])
            {
                if (!component.is_object())
                    continue;
                const int destinationComponentId = component.value("id", -1);
                const int sourceComponentId = component.contains("templateComponentId")
                    ? component.value("templateComponentId", destinationComponentId) : allocateSourceId();
                if (destinationComponentId >= 0 && sourceComponentId >= 0)
                {
                    destinationToSource[destinationComponentId] = sourceComponentId;
                    component["id"] = sourceComponentId;
                }
            }
        if (object.contains("children") && object["children"].is_array())
            for (auto &child : object["children"])
                self(self, child);
    };
    collect(collect, root);
    for (auto &binding : bindings)
    {
        const std::string scope = binding.value("scope", std::string{});
        int ownerId = binding.value("owner_id", -1);
        int componentId = binding.value("component_id", -1);
        if (bindingsUseInstanceIds)
        {
            const auto sourceOwner = destinationToSource.find(ownerId);
            if (sourceOwner == destinationToSource.end())
            {
                error = "template reference owner could not be assigned a source identity";
                return false;
            }
            ownerId = sourceOwner->second;
            binding["owner_id"] = ownerId;
            if (componentId >= 0)
            {
                const auto sourceComponent = destinationToSource.find(componentId);
                if (sourceComponent == destinationToSource.end())
                {
                    error = "template reference component could not be assigned a source identity";
                    return false;
                }
                componentId = sourceComponent->second;
                binding["component_id"] = componentId;
            }
        }
        std::vector<std::string> path;
        if (!parse_pointer(binding.value("path", std::string{}), path))
        {
            error = "template reference contains an invalid JSON pointer";
            return false;
        }
        Json *owner = component_or_object(root, ownerId,
            componentId >= 0 ? std::optional<int>(componentId) : std::nullopt);
        Json *targetValue = owner != nullptr ? resolve_pointer(*owner, path) : nullptr;
        if (targetValue == nullptr)
        {
            error = "template reference path could not be restored in the template asset";
            return false;
        }
        if (scope == "internal")
        {
            const auto destinationTarget = read_identity(*targetValue);
            const auto sourceTarget = destinationTarget ? destinationToSource.find(*destinationTarget)
                                                        : destinationToSource.end();
            if (sourceTarget == destinationToSource.end())
            {
                error = "internal template reference target could not be restored to its source identity";
                return false;
            }
            *targetValue = sourceTarget->second;
        }
        else if (scope == "external")
        {
            const auto original = binding.find("original_target_id");
            if (original == binding.end())
            {
                error = "external template reference has no original target identity";
                return false;
            }
            *targetValue = *original;
        }
    }
    if (destinationToSourceResult != nullptr)
        *destinationToSourceResult = std::move(destinationToSource);
    return true;
}
} // namespace

bool apply_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                             int instanceRootId, const std::filesystem::path &assetRoot,
                             std::string &errorOut, const Json *catalog)
{
    rebuild_document_indexes(sceneDocument);
    const auto loc = sceneDocument.indexes.objectsById.find(instanceRootId);
    if (loc == sceneDocument.indexes.objectsById.end())
    {
        errorOut = "Template instance not found.";
        return false;
    }
    const Json *rootObj = resolve_object_at(sceneDocument.root, loc->second);
    if (rootObj == nullptr || !is_template_instance_root(*rootObj))
    {
        errorOut = "Selected object is not a linked template instance.";
        return false;
    }
    const std::string templateRef = rootObj->at("template").get<std::string>();
    const auto fullPath = resolve_template_asset_path(assetRoot, templateRef);
    std::string tmplName = rootObj->value("name", fullPath.stem().string());
    Json referenceBindings = Json::array();
    std::string previousContents;
    bool hadPreviousFile = false;
    std::error_code templateFileError;
    if (std::filesystem::exists(fullPath, templateFileError))
    {
        std::ifstream existingIn(fullPath);
        if (existingIn.is_open())
        {
            previousContents.assign(std::istreambuf_iterator<char>(existingIn), std::istreambuf_iterator<char>());
            hadPreviousFile = true;
            if (auto loaded = parse_template_envelope(previousContents))
            {
                tmplName = loaded.asset->envelope().value("name", tmplName);
                referenceBindings = loaded.asset->reference_bindings();
            }
        }
    }

    bool bindingsUseInstanceIds = false;
    if (catalog != nullptr)
    {
        const auto collected = collect_template_bindings(
            sceneDocument, *rootObj, template_scene_identity(sceneDocument), *catalog);
        if (!collected || !collected.assured)
        {
            errorOut = collected.error.empty()
                ? "Template apply is blocked until every behavior schema has complete reference declarations."
                : collected.error;
            return false;
        }
        referenceBindings = collected.bindings;
        bindingsUseInstanceIds = true;
    }

    Json cleanRoot = *rootObj;
    TemplateIdMap instanceToSource;
    if (!restore_template_source_ids(cleanRoot, referenceBindings, errorOut,
                                     &instanceToSource, bindingsUseInstanceIds))
        return false;
    strip_nested_template_metadata(cleanRoot);
    const Json envelope = make_template_envelope(tmplName, std::move(cleanRoot), referenceBindings);
    const auto validatedEnvelope = load_template_envelope(envelope);
    if (!validatedEnvelope)
    {
        errorOut = validatedEnvelope.error;
        return false;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(fullPath.parent_path(), directoryError);
    if (directoryError)
    {
        errorOut = "Unable to create template directory: " + directoryError.message();
        return false;
    }
    const auto written = write_file_atomically(fullPath, envelope.dump(2) + "\n", hadPreviousFile);
    if (!written.succeeded())
    {
        errorOut = "Unable to write template file: " + written.message;
        return false;
    }

    // Clear overrides through the same history as the authored values.
    Json updatedInstance = *rootObj;
    clear_overrides_recursive(updatedInstance);
    const auto stampSourceMetadata = [&](const auto &self, Json &object) -> bool {
        if (!object.is_object())
            return true;
        const int instanceId = object.value("id", -1);
        const auto sourceId = instanceToSource.find(instanceId);
        if (sourceId == instanceToSource.end())
        {
            errorOut = "an applied object could not be assigned its saved template identity";
            return false;
        }
        object["templateNodeId"] = sourceId->second;
        std::vector<std::string> sourceKeys;
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!is_game_object_envelope_metadata_key(it.key()))
                sourceKeys.push_back(it.key());
        object["templateSourceKeys"] = std::move(sourceKeys);
        if (object.contains("components") && object["components"].is_array())
            for (auto &component : object["components"])
            {
                if (!component.is_object())
                    continue;
                const int instanceComponentId = component.value("id", -1);
                const auto sourceComponentId = instanceToSource.find(instanceComponentId);
                if (sourceComponentId == instanceToSource.end())
                {
                    errorOut = "an applied component could not be assigned its saved template identity";
                    return false;
                }
                component["templateComponentId"] = sourceComponentId->second;
                std::vector<std::string> sourceFields;
                for (auto it = component.begin(); it != component.end(); ++it)
                    if (!is_component_envelope_metadata_key(it.key()))
                        sourceFields.push_back(it.key());
                component["templateSourceFields"] = std::move(sourceFields);
            }
        object["templateSourcePaths"] = template_source_paths(object);
        if (object.contains("children") && object["children"].is_array())
            for (auto &child : object["children"])
                if (!self(self, child))
                    return false;
        return true;
    };
    if (!stampSourceMetadata(stampSourceMetadata, updatedInstance))
    {
        if (hadPreviousFile)
            (void)write_file_atomically(fullPath, previousContents, true);
        else
            std::filesystem::remove(fullPath, directoryError);
        return false;
    }
    const auto committed = apply_object_snapshot(sceneDocument, commands, instanceRootId,
                                                 *rootObj, updatedInstance);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
    {
        if (hadPreviousFile)
            (void)write_file_atomically(fullPath, previousContents, true);
        else
            std::filesystem::remove(fullPath, directoryError);
        errorOut = committed.message.empty() ? "Could not record template Apply in edit history." : committed.message;
        return false;
    }
    return true;
}

bool revert_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                              int instanceRootId, const std::filesystem::path &assetRoot,
                              std::string &errorOut)
{
    rebuild_document_indexes(sceneDocument);
    const auto loc = sceneDocument.indexes.objectsById.find(instanceRootId);
    if (loc == sceneDocument.indexes.objectsById.end())
    {
        errorOut = "Template instance not found.";
        return false;
    }
    const Json *rootObj = resolve_object_at(sceneDocument.root, loc->second);
    if (rootObj == nullptr || !is_template_instance_root(*rootObj))
    {
        errorOut = "Selected object is not a linked template instance.";
        return false;
    }
    const std::string templateRef = rootObj->at("template").get<std::string>();
    const auto fullPath = resolve_template_asset_path(assetRoot, templateRef);
    std::ifstream in(fullPath);
    if (!in.is_open())
    {
        errorOut = "Template file not found: " + fullPath.string();
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto loaded = parse_template_envelope(text);
    if (!loaded)
    {
        errorOut = loaded.error;
        return false;
    }

    const Json savedPos = rootObj->contains("position") ? rootObj->at("position") : Json{{"x", 0.0}, {"y", 0.0}};

    std::unordered_set<int> usedIds;
    for (const auto &[id, _] : sceneDocument.indexes.objectsById)
        usedIds.insert(id);
    for (const auto &[id, _] : sceneDocument.indexes.componentsById)
        usedIds.insert(id);
    int nextId = 1;
    for (const int id : usedIds)
        nextId = std::max(nextId, id + 1);

    Json updatedInstance = *rootObj;
    clear_overrides_recursive(updatedInstance);
    sync_node_from_template(updatedInstance, loaded.asset->root(), usedIds, nextId, true);
    if (!sync_template_reference_values(updatedInstance, *rootObj, *loaded.asset, errorOut))
        return false;
    updatedInstance["position"] = savedPos;
    const auto committed = apply_object_snapshot(sceneDocument, commands, instanceRootId,
                                                 *rootObj, updatedInstance);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
    {
        errorOut = committed.message.empty() ? "Could not record template Revert in edit history." : committed.message;
        return false;
    }
    return true;
}

bool relink_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                              int instanceRootId, const std::filesystem::path &assetRoot,
                              const std::filesystem::path &candidateTemplate,
                              std::string &errorOut)
{
    rebuild_document_indexes(sceneDocument);
    const auto location = sceneDocument.indexes.objectsById.find(instanceRootId);
    if (location == sceneDocument.indexes.objectsById.end())
    {
        errorOut = "Template instance not found.";
        return false;
    }
    const Json *rootObject = resolve_object_at(sceneDocument.root, location->second);
    if (rootObject == nullptr || !is_template_instance_root(*rootObject))
    {
        errorOut = "Selected object is not a linked template instance.";
        return false;
    }

    std::error_code filesystemError;
    const auto canonicalRoot = std::filesystem::weakly_canonical(assetRoot, filesystemError);
    if (filesystemError)
    {
        errorOut = "Project asset root cannot be resolved.";
        return false;
    }
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidateTemplate, filesystemError);
    if (filesystemError || !std::filesystem::is_regular_file(canonicalCandidate, filesystemError) ||
        filesystemError || canonicalCandidate.extension() != ".pttemplate")
    {
        errorOut = "Choose an available .pttemplate asset inside this project.";
        return false;
    }
    const auto relativeCandidate = canonicalCandidate.lexically_relative(canonicalRoot);
    if (relativeCandidate.empty() || relativeCandidate.is_absolute() ||
        (!relativeCandidate.empty() && *relativeCandidate.begin() == ".."))
    {
        errorOut = "Template asset must be inside the current project asset root.";
        return false;
    }

    std::ifstream input(canonicalCandidate, std::ios::binary);
    if (!input.is_open())
    {
        errorOut = "Template asset could not be opened.";
        return false;
    }
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto loaded = parse_template_envelope(contents);
    if (!loaded)
    {
        errorOut = loaded.error;
        return false;
    }
    if (rootObject->value("templateNodeId", -1) != loaded.asset->root().value("id", -2))
    {
        errorOut = "The selected template has a different source identity. Unpack this instance before relinking.";
        return false;
    }

    std::unordered_set<int> usedIds;
    for (const auto &[id, _] : sceneDocument.indexes.objectsById)
        usedIds.insert(id);
    for (const auto &[id, _] : sceneDocument.indexes.componentsById)
        usedIds.insert(id);
    int nextId = 1;
    for (const int id : usedIds)
        nextId = std::max(nextId, id + 1);

    Json updated = *rootObject;
    clear_overrides_recursive(updated);
    sync_node_from_template(updated, loaded.asset->root(), usedIds, nextId, true);
    if (!sync_template_reference_values(updated, *rootObject, *loaded.asset, errorOut))
        return false;
    updated["template"] = normalize_template_asset_ref(assetRoot, canonicalCandidate);
    const auto committed = apply_object_snapshot(sceneDocument, commands, instanceRootId,
                                                 *rootObject, updated);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
    {
        errorOut = committed.message.empty() ? "Could not record template relink in edit history." : committed.message;
        return false;
    }
    return true;
}

bool revert_template_property(DocumentSession &sceneDocument, EditCommandService &commands,
                              int objectId, std::optional<int> componentId,
                              std::optional<std::size_t> behaviorOrdinal,
                              const std::vector<FieldPathSegment> &fieldPath,
                              const std::filesystem::path &assetRoot,
                              std::string &errorOut)
{
    rebuild_document_indexes(sceneDocument);
    const auto instanceRootId = find_template_instance_root(sceneDocument, objectId);
    if (!instanceRootId)
    {
        errorOut = "The selected field is not part of a linked template instance.";
        return false;
    }
    const Json *instanceRoot = find_document_object_by_id(sceneDocument, *instanceRootId);
    const Json *instanceObject = find_document_object_by_id(sceneDocument, objectId);
    if (instanceRoot == nullptr || instanceObject == nullptr)
    {
        errorOut = "Template instance data is no longer available.";
        return false;
    }
    const auto fullPath = resolve_template_asset_path(
        assetRoot, instanceRoot->value("template", std::string{}));
    std::ifstream input(fullPath, std::ios::binary);
    if (!input.is_open())
    {
        errorOut = "Template file not found: " + fullPath.string();
        return false;
    }
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto loaded = parse_template_envelope(contents);
    if (!loaded)
    {
        errorOut = loaded.error;
        return false;
    }
    const int templateObjectId = instanceObject->value("templateNodeId", objectId);
    std::function<const Json *(const Json &)> findSourceObject = [&](const Json &object) -> const Json * {
        if (!object.is_object())
            return nullptr;
        if (object.value("id", -1) == templateObjectId)
            return &object;
        const auto children = object.find("children");
        if (children != object.end() && children->is_array())
            for (const auto &child : *children)
                if (const Json *found = findSourceObject(child))
                    return found;
        return nullptr;
    };
    const Json *sourceObject = findSourceObject(loaded.asset->root());
    if (sourceObject == nullptr)
    {
        errorOut = "The template no longer contains this linked object.";
        return false;
    }

    auto component_by_id = [](const Json *object, int id) -> const Json * {
        if (object == nullptr || !object->contains("components") || !(*object)["components"].is_array())
            return nullptr;
        for (const auto &component : (*object)["components"])
            if (component.is_object() && component.value("id", -1) == id)
                return &component;
        return nullptr;
    };
    const Json *currentOwner = instanceObject;
    const Json *sourceOwner = sourceObject;
    int templateComponentId = -1;
    if (componentId)
    {
        const auto componentLocation = sceneDocument.indexes.componentsById.find(*componentId);
        const Json *currentComponent = component_by_id(instanceObject, *componentId);
        if (componentLocation == sceneDocument.indexes.componentsById.end() ||
            currentComponent == nullptr ||
            !currentComponent->contains("templateComponentId") ||
            !currentComponent->at("templateComponentId").is_number_integer())
        {
            errorOut = "This component was added locally and has no template source field to revert.";
            return false;
        }
        const int sourceComponentId = currentComponent->at("templateComponentId").get<int>();
        templateComponentId = sourceComponentId;
        currentOwner = currentComponent;
        sourceOwner = component_by_id(sourceObject, sourceComponentId);
        if (sourceOwner == nullptr)
        {
            errorOut = "The template no longer contains this linked component.";
            return false;
        }
        if (behaviorOrdinal)
        {
            const auto currentBehaviors = currentOwner->find("behaviors");
            const auto sourceBehaviors = sourceOwner->find("behaviors");
            if (currentBehaviors == currentOwner->end() || !currentBehaviors->is_array() ||
                sourceBehaviors == sourceOwner->end() || !sourceBehaviors->is_array() ||
                *behaviorOrdinal >= currentBehaviors->size() ||
                *behaviorOrdinal >= sourceBehaviors->size() ||
                (*currentBehaviors)[*behaviorOrdinal].value("type", std::string{}) !=
                    (*sourceBehaviors)[*behaviorOrdinal].value("type", std::string{}))
            {
                errorOut = "The template behavior no longer matches this field.";
                return false;
            }
            currentOwner = &(*currentBehaviors)[*behaviorOrdinal];
            sourceOwner = &(*sourceBehaviors)[*behaviorOrdinal];
        }
    }
    else if (behaviorOrdinal)
    {
        errorOut = "Behavior fields require their owning Script component.";
        return false;
    }

    const auto resolveField = [&](const Json *owner) -> const Json * {
        const Json *value = owner;
        for (const auto &segment : fieldPath)
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
                const std::size_t index = std::get<std::size_t>(segment);
                if (!value->is_array() || index >= value->size())
                    return nullptr;
                value = &(*value)[index];
            }
        }
        return value;
    };
    const Json *currentValue = resolveField(currentOwner);
    const Json *sourceValue = resolveField(sourceOwner);
    const std::string overrideKey = template_override_path_for_address(
        sceneDocument, address_for_document(sceneDocument, objectId, componentId,
                                            behaviorOrdinal, fieldPath));
    if (overrideKey.empty())
    {
        errorOut = "The selected field has no stable template source path.";
        return false;
    }

    struct OverridePathPart
    {
        enum class Kind { Property, Index, Component } kind = Kind::Property;
        std::string property;
        std::size_t index = 0;
        int componentId = -1;

        bool operator==(const OverridePathPart &) const = default;
    };
    const auto parseOverridePath = [](std::string_view text,
                                      std::vector<OverridePathPart> &parts) {
        std::size_t cursor = 0;
        if (text.starts_with("component[id="))
        {
            const auto close = text.find(']', 13);
            if (close == std::string_view::npos)
                return false;
            int component = -1;
            const auto digits = text.substr(13, close - 13);
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), component);
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || component < 0)
                return false;
            parts.push_back({OverridePathPart::Kind::Component, {}, 0, component});
            cursor = close + 1;
        }
        while (cursor < text.size())
        {
            if (text[cursor] == '.')
            {
                ++cursor;
                if (cursor == text.size())
                    return false;
            }
            if (text[cursor] == '[')
            {
                const auto close = text.find(']', cursor + 1);
                if (close == std::string_view::npos)
                    return false;
                std::size_t index = 0;
                const auto digits = text.substr(cursor + 1, close - cursor - 1);
                const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), index);
                if (digits.empty() || parsed.ec != std::errc{} ||
                    parsed.ptr != digits.data() + digits.size())
                    return false;
                parts.push_back({OverridePathPart::Kind::Index, {}, index, -1});
                cursor = close + 1;
                continue;
            }
            const std::size_t start = cursor;
            while (cursor < text.size() && text[cursor] != '.' && text[cursor] != '[')
                ++cursor;
            if (cursor == start)
                return false;
            parts.push_back({OverridePathPart::Kind::Property,
                             std::string(text.substr(start, cursor - start)), 0, -1});
        }
        return !parts.empty();
    };
    const auto resolveOverridePath = [&](const Json &root, const std::vector<OverridePathPart> &parts,
                                         bool sourcePath) -> const Json * {
        const Json *value = &root;
        for (const auto &part : parts)
        {
            if (part.kind == OverridePathPart::Kind::Component)
            {
                if (!value->is_object() || !value->contains("components") ||
                    !(*value)["components"].is_array())
                    return nullptr;
                const Json *found = nullptr;
                for (const auto &component : (*value)["components"])
                    if (component.is_object() &&
                        (sourcePath ? component.value("id", -1) == part.componentId
                                    : component.value("templateComponentId",
                                                      component.value("id", -1)) == part.componentId))
                    {
                        found = &component;
                        break;
                    }
                value = found;
            }
            else if (part.kind == OverridePathPart::Kind::Index)
            {
                if (!value->is_array() || part.index >= value->size())
                    return nullptr;
                value = &(*value)[part.index];
            }
            else
            {
                if (!value->is_object())
                    return nullptr;
                const auto child = value->find(part.property);
                if (child == value->end())
                    return nullptr;
                value = &*child;
            }
            if (value == nullptr)
                return nullptr;
        }
        return value;
    };
    const auto formatOverridePath = [](const std::vector<OverridePathPart> &parts) {
        std::string path;
        for (const auto &part : parts)
        {
            if (part.kind == OverridePathPart::Kind::Component)
                path += "component[id=" + std::to_string(part.componentId) + "]";
            else if (part.kind == OverridePathPart::Kind::Index)
                path += "[" + std::to_string(part.index) + "]";
            else
            {
                if (!path.empty())
                    path.push_back('.');
                path += part.property;
            }
        }
        return path;
    };
    const auto is_envelope_metadata_at_path = [](const std::vector<OverridePathPart> &path,
                                                 std::string_view key) {
        if (path.empty())
            return is_game_object_envelope_metadata_key(key);
        if (path.size() == 1 && path.front().kind == OverridePathPart::Kind::Component)
            return is_component_envelope_metadata_key(key);
        return false;
    };
    std::vector<OverridePathPart> selectedParts;
    if (!parseOverridePath(overrideKey, selectedParts))
    {
        errorOut = "The selected field has an invalid stable template source path.";
        return false;
    }

    Json desiredValue;
    if (sourceValue != nullptr)
        desiredValue = *sourceValue;
    for (const auto &binding : loaded.asset->reference_bindings())
    {
        if (binding.value("owner_id", -1) != templateObjectId ||
            binding.value("component_id", -1) != templateComponentId ||
            binding_override_path(binding) != overrideKey)
            continue;
        const std::string scope = binding.value("scope", std::string{});
        if (scope == "internal" && sourceValue != nullptr)
        {
            const auto sourceTarget = read_identity(*sourceValue);
            TemplateIdMap sourceToInstance;
            collect_template_instance_ids(*instanceRoot, sourceToInstance);
            const auto instanceTarget = sourceTarget ? sourceToInstance.find(*sourceTarget)
                                                     : sourceToInstance.end();
            if (instanceTarget == sourceToInstance.end())
            {
                errorOut = "The template reference target could not be mapped to this instance.";
                return false;
            }
            desiredValue = instanceTarget->second;
        }
        else if (scope == "external")
        {
            const auto originalTarget = binding.find("original_target_id");
            if (originalTarget == binding.end())
            {
                errorOut = "The external template reference has no original target identity.";
                return false;
            }
            desiredValue = *originalTarget;
        }
        break;
    }

    Json nextOverrides = instanceObject->value("overrides", Json::array());
    if (!nextOverrides.is_array())
        nextOverrides = Json::array();
    std::vector<std::pair<std::string, std::vector<OverridePathPart>>> coveringOverrides;
    for (const auto &entry : nextOverrides)
    {
        if (!entry.is_string())
            continue;
        const std::string value = entry.get<std::string>();
        std::vector<OverridePathPart> parts;
        if (!parseOverridePath(value, parts) || parts.size() >= selectedParts.size())
            continue;
        if (std::equal(parts.begin(), parts.end(), selectedParts.begin()))
            coveringOverrides.emplace_back(value, std::move(parts));
    }
    std::vector<std::string> splitSiblingOverrides;
    if (!coveringOverrides.empty())
    {
        std::sort(coveringOverrides.begin(), coveringOverrides.end(),
                  [](const auto &left, const auto &right) {
                      return left.second.size() < right.second.size();
                  });
        const auto &ancestor = coveringOverrides.front();
        const Json *currentContainer = resolveOverridePath(*instanceObject, ancestor.second, false);
        const Json *sourceContainer = resolveOverridePath(*sourceObject, ancestor.second, true);
        TemplateIdMap sourceToInstance;
        collect_template_instance_ids(*instanceRoot, sourceToInstance);
        std::unordered_set<std::string> internalReferencePaths;
        for (const auto &binding : loaded.asset->reference_bindings())
        {
            if (binding.value("owner_id", -1) != templateObjectId ||
                binding.value("scope", std::string{}) != "internal")
                continue;
            const std::string path = binding_override_path(binding);
            if (!path.empty())
                internalReferencePaths.insert(path);
        }
        const auto same_after_identity_mapping = [&](const auto &self, const Json &current,
                                                    const Json &source,
                                                    const std::vector<OverridePathPart> &path) -> bool {
            if (source.is_number_integer() &&
                internalReferencePaths.contains(formatOverridePath(path)))
            {
                const auto sourceId = read_identity(source);
                if (!sourceId)
                    return false;
                const auto mapped = sourceToInstance.find(*sourceId);
                const auto currentId = read_identity(current);
                return mapped != sourceToInstance.end() && currentId &&
                    *currentId == mapped->second;
            }
            if (current.is_array() && source.is_array())
            {
                if (current.size() != source.size())
                    return false;
                for (std::size_t index = 0; index < current.size(); ++index)
                {
                    auto childPath = path;
                    childPath.push_back({OverridePathPart::Kind::Index, {}, index, -1});
                    if (!self(self, current[index], source[index], childPath))
                        return false;
                }
                return true;
            }
            if (current.is_object() && source.is_object())
            {
                for (auto it = current.begin(); it != current.end(); ++it)
                {
                    if (is_envelope_metadata_at_path(path, it.key()))
                        continue;
                    const auto sourceChild = source.find(it.key());
                    auto childPath = path;
                    childPath.push_back({OverridePathPart::Kind::Property, it.key(), 0, -1});
                    if (sourceChild == source.end() ||
                        !self(self, it.value(), *sourceChild, childPath))
                        return false;
                }
                for (auto it = source.begin(); it != source.end(); ++it)
                    if (!is_envelope_metadata_at_path(path, it.key()) && !current.contains(it.key()))
                        return false;
                return true;
            }
            return current == source;
        };
        std::function<void(const Json *, const Json *, std::vector<OverridePathPart>, std::size_t)> split;
        split = [&](const Json *current, const Json *source,
                    std::vector<OverridePathPart> path, std::size_t targetOffset) {
            if (targetOffset >= selectedParts.size() || current == nullptr)
                return;
            const auto &targetPart = selectedParts[targetOffset];
            if (current->is_object() && targetPart.kind == OverridePathPart::Kind::Property)
            {
                std::set<std::string> keys;
                for (auto it = current->begin(); it != current->end(); ++it)
                    if (!is_envelope_metadata_at_path(path, it.key()))
                        keys.insert(it.key());
                if (source != nullptr && source->is_object())
                    for (auto it = source->begin(); it != source->end(); ++it)
                        if (!is_envelope_metadata_at_path(path, it.key()))
                            keys.insert(it.key());
                for (const auto &key : keys)
                {
                    const auto currentIt = current->find(key);
                    const Json *currentChild = currentIt == current->end() ? nullptr : &*currentIt;
                    const Json *sourceChild = source != nullptr && source->is_object() &&
                        source->contains(key) ? &source->at(key) : nullptr;
                    auto childPath = path;
                    childPath.push_back({OverridePathPart::Kind::Property, key, 0, -1});
                    if (key == targetPart.property)
                        split(currentChild, sourceChild, std::move(childPath), targetOffset + 1);
                    else if (currentChild != nullptr &&
                             (sourceChild == nullptr || !same_after_identity_mapping(
                                 same_after_identity_mapping, *currentChild, *sourceChild, childPath)))
                        splitSiblingOverrides.push_back(formatOverridePath(childPath));
                }
            }
            else if (current->is_array() && targetPart.kind == OverridePathPart::Kind::Index)
            {
                const std::size_t sourceSize = source != nullptr && source->is_array()
                    ? source->size() : 0;
                for (std::size_t index = 0; index < current->size(); ++index)
                {
                    const Json *sourceChild = index < sourceSize ? &(*source)[index] : nullptr;
                    auto childPath = path;
                    childPath.push_back({OverridePathPart::Kind::Index, {}, index, -1});
                    if (index == targetPart.index)
                        split(&(*current)[index], sourceChild, std::move(childPath), targetOffset + 1);
                    else if (sourceChild == nullptr || !same_after_identity_mapping(
                                 same_after_identity_mapping, (*current)[index], *sourceChild, childPath))
                        splitSiblingOverrides.push_back(formatOverridePath(childPath));
                }
            }
        };
        if (currentContainer != nullptr)
            split(currentContainer, sourceContainer, ancestor.second, ancestor.second.size());
    }
    Json filteredOverrides = Json::array();
    for (const auto &entry : nextOverrides)
    {
        bool remove = false;
        if (entry.is_string())
        {
            const std::string value = entry.get<std::string>();
            remove = value == overrideKey || value.starts_with(overrideKey + ".") ||
                     value.starts_with(overrideKey + "[") ||
                     std::any_of(coveringOverrides.begin(), coveringOverrides.end(),
                         [&](const auto &ancestor) { return value == ancestor.first; });
        }
        if (!remove)
            filteredOverrides.push_back(entry);
    }
    for (const auto &sibling : splitSiblingOverrides)
        if (std::find(filteredOverrides.begin(), filteredOverrides.end(), sibling) == filteredOverrides.end())
            filteredOverrides.push_back(sibling);
    const auto existingOverrides = instanceObject->find("overrides");
    const PropertyValue beforeOverrides = existingOverrides == instanceObject->end()
        ? PropertyValue::missing() : PropertyValue::of(*existingOverrides);
    const PropertyValue afterOverrides = filteredOverrides.empty()
        ? PropertyValue::missing() : PropertyValue::of(std::move(filteredOverrides));

    std::vector<BatchOperation> operations;
    operations.reserve(2);
    if ((currentValue == nullptr) != (sourceValue == nullptr) ||
        (currentValue != nullptr && sourceValue != nullptr && *currentValue != desiredValue))
    {
        operations.emplace_back(PropertyCommand{
            address_for_document(sceneDocument, objectId, componentId, behaviorOrdinal, fieldPath),
            currentValue == nullptr ? PropertyValue::missing() : PropertyValue::of(*currentValue),
            sourceValue == nullptr ? PropertyValue::missing() : PropertyValue::of(desiredValue), {}});
    }
    if (beforeOverrides != afterOverrides)
    {
        operations.emplace_back(PropertyCommand{
            address_for_document(sceneDocument, objectId, std::nullopt, std::nullopt,
                                 {std::string("overrides")}),
            beforeOverrides, afterOverrides, {}});
    }
    if (operations.empty())
        return true;
    const auto committed = commands.apply_batch(sceneDocument, operations);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
    {
        errorOut = committed.message.empty() ? "Could not record property revert in edit history." : committed.message;
        return false;
    }
    return true;
}

bool revert_template_properties(DocumentSession &sceneDocument, EditCommandService &commands,
                                const std::vector<TemplatePropertyTarget> &targets,
                                const std::filesystem::path &assetRoot,
                                std::string &errorOut)
{
    if (targets.empty())
        return true;

    DocumentSession working = sceneDocument;
    EditCommandService previewCommands;
    previewCommands.reset_history(working.documentGeneration);
    for (const auto &target : targets)
        if (!revert_template_property(working, previewCommands, target.objectId,
                                      target.componentId, target.behaviorOrdinal,
                                      target.fieldPath, assetRoot, errorOut))
            return false;

    const auto read_value = [](const DocumentSession &document,
                               const TemplatePropertyTarget &target) -> PropertyValue {
        const Json *value = find_document_object_by_id(document, target.objectId);
        if (value == nullptr)
            return PropertyValue::missing();
        if (target.componentId)
        {
            const auto components = value->find("components");
            if (components == value->end() || !components->is_array())
                return PropertyValue::missing();
            value = nullptr;
            for (const auto &component : *components)
                if (component.is_object() && component.value("id", -1) == *target.componentId)
                {
                    value = &component;
                    break;
                }
            if (value == nullptr)
                return PropertyValue::missing();
            if (target.behaviorOrdinal)
            {
                const auto behaviors = value->find("behaviors");
                if (behaviors == value->end() || !behaviors->is_array() ||
                    *target.behaviorOrdinal >= behaviors->size())
                    return PropertyValue::missing();
                value = &(*behaviors)[*target.behaviorOrdinal];
            }
        }
        for (const auto &segment : target.fieldPath)
        {
            if (const auto *key = std::get_if<std::string>(&segment))
            {
                if (!value->is_object())
                    return PropertyValue::missing();
                const auto found = value->find(*key);
                if (found == value->end())
                    return PropertyValue::missing();
                value = &*found;
            }
            else
            {
                const std::size_t index = std::get<std::size_t>(segment);
                if (!value->is_array() || index >= value->size())
                    return PropertyValue::missing();
                value = &(*value)[index];
            }
        }
        return PropertyValue::of(*value);
    };

    std::vector<BatchOperation> operations;
    std::set<std::string> emittedAddresses;
    std::set<int> affectedObjects;
    const auto append_change = [&](const PropertyAddress &address,
                                   const PropertyValue &before,
                                   const PropertyValue &after) {
        std::string key = std::to_string(address.objectId) + ":" +
            std::to_string(address.componentId.value_or(-1)) + ":" +
            std::to_string(address.behaviorOrdinal.value_or(std::numeric_limits<std::size_t>::max()));
        for (const auto &segment : address.fieldPath)
        {
            if (const auto *name = std::get_if<std::string>(&segment))
                key += "s" + std::to_string(name->size()) + ":" + *name;
            else
                key += "i" + std::to_string(std::get<std::size_t>(segment));
            key.push_back(';');
        }
        if (before == after || !emittedAddresses.insert(std::move(key)).second)
            return;
        operations.emplace_back(PropertyCommand{address, before, after, {}});
    };

    for (const auto &target : targets)
    {
        const PropertyAddress address = address_for_document(
            sceneDocument, target.objectId, target.componentId,
            target.behaviorOrdinal, target.fieldPath);
        append_change(address, read_value(sceneDocument, target), read_value(working, target));
        if (find_template_instance_root(sceneDocument, target.objectId))
            affectedObjects.insert(target.objectId);
    }
    for (const int objectId : affectedObjects)
    {
        const TemplatePropertyTarget overridesTarget{objectId, std::nullopt, std::nullopt,
                                                      {std::string("overrides")}};
        append_change(address_for_document(sceneDocument, objectId, std::nullopt, std::nullopt,
                                           {std::string("overrides")}),
                      read_value(sceneDocument, overridesTarget), read_value(working, overridesTarget));
    }

    if (operations.empty())
        return true;
    const auto committed = commands.apply_batch(sceneDocument, operations);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
    {
        errorOut = committed.message.empty()
            ? "Could not record grouped property revert in edit history."
            : committed.message;
        return false;
    }
    return true;
}

bool unpack_template_instance(DocumentSession &sceneDocument, EditCommandService &commands,
                              int instanceRootId, std::string &errorOut)
{
    rebuild_document_indexes(sceneDocument);
    const auto loc = sceneDocument.indexes.objectsById.find(instanceRootId);
    if (loc == sceneDocument.indexes.objectsById.end())
    {
        errorOut = "Template instance not found.";
        return false;
    }
    const Json *rootObj = resolve_object_at(sceneDocument.root, loc->second);
    if (rootObj == nullptr || !is_template_instance_root(*rootObj))
    {
        errorOut = "Selected object is not a linked template instance.";
        return false;
    }
    Json unpacked = *rootObj;
    strip_nested_template_metadata(unpacked);
    const auto committed = apply_object_snapshot(sceneDocument, commands, instanceRootId,
                                                 *rootObj, unpacked);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
    {
        errorOut = committed.message.empty() ? "Could not record template Unpack in edit history." : committed.message;
        return false;
    }
    return true;
}

bool replace_template_document_root(DocumentSession &document, EditCommandService &commands,
                                    const Json &newRoot, std::string &errorOut)
{
    if (!document.root.is_array() || document.root.size() != 1 ||
        !document.root[0].is_object() || !newRoot.is_object())
    {
        errorOut = "template document must contain one object root";
        return false;
    }
    const int rootId = document.root[0].value("id", -1);
    if (rootId < 0 || newRoot.value("id", -1) != rootId)
    {
        errorOut = "template root identity cannot change during an in-document replacement";
        return false;
    }
    const auto committed = apply_object_snapshot(document, commands, rootId,
                                                  document.root[0], newRoot);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
    {
        errorOut = committed.message.empty()
            ? "Could not record template document replacement in edit history."
            : committed.message;
        return false;
    }
    return true;
}

std::size_t propagate_template_to_scene(DocumentSession &sceneDocument, EditCommandService &commands,
                                        std::string_view templateRef, const TemplateAssetV1 &asset)
{
    rebuild_document_indexes(sceneDocument);
    std::unordered_set<int> usedIds;
    for (const auto &[id, _] : sceneDocument.indexes.objectsById)
        usedIds.insert(id);
    for (const auto &[id, _] : sceneDocument.indexes.componentsById)
        usedIds.insert(id);
    int nextId = 1;
    for (const int id : usedIds)
        nextId = std::max(nextId, id + 1);

    std::vector<int> matchingRootIds;
    const auto normalizedRef = [&](std::string_view ref) {
        const auto resolved = resolve_template_asset_path(sceneDocument.assetRoot, ref);
        return normalize_template_asset_ref(sceneDocument.assetRoot, resolved);
    };
    const std::string targetRef = normalizedRef(templateRef);
    for (const int objId : sceneDocument.indexes.orderedObjectIds)
    {
        const auto loc = sceneDocument.indexes.objectsById.find(objId);
        if (loc == sceneDocument.indexes.objectsById.end())
            continue;
        const Json *obj = resolve_object_at(sceneDocument.root, loc->second);
        if (obj != nullptr && is_template_instance_root(*obj) &&
            normalizedRef(obj->at("template").get<std::string>()) == targetRef)
        {
            matchingRootIds.push_back(objId);
        }
    }
    std::vector<BatchOperation> operations;
    for (const int rootId : matchingRootIds)
    {
        const auto loc = sceneDocument.indexes.objectsById.find(rootId);
        if (loc == sceneDocument.indexes.objectsById.end())
            continue;
        const Json *root = resolve_object_at(sceneDocument.root, loc->second);
        if (root == nullptr)
            continue;
        Json updated = *root;
        sync_node_from_template(updated, asset.root(), usedIds, nextId, true);
        std::string referenceError;
        if (!sync_template_reference_values(updated, *root, asset, referenceError))
            return 0;
        auto rootOperations = object_snapshot_operations(sceneDocument, rootId, *root, updated);
        operations.insert(operations.end(),
                          std::make_move_iterator(rootOperations.begin()),
                          std::make_move_iterator(rootOperations.end()));
    }
    if (operations.empty())
        return matchingRootIds.size();
    const auto committed = commands.apply_batch(sceneDocument, operations);
    if (committed.status != CommandStatus::Changed && committed.status != CommandStatus::NoChange)
        return 0;
    return matchingRootIds.size();
}
} // namespace platformator_editor
