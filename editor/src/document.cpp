#include "document.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <platformator/gameobject.h>

namespace platformator_editor
{
namespace
{
constexpr int kScriptType = SCRIPT;
constexpr int kSpriteType = SPRITE;
constexpr int kCameraType = CAMERA;

std::optional<std::int64_t> read_integer(const Json &value)
{
    try
    {
        if (value.is_number_unsigned())
        {
            const auto unsignedValue = value.get<std::uint64_t>();
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                return std::nullopt;
            return static_cast<std::int64_t>(unsignedValue);
        }
        if (value.is_number_integer())
            return value.get<std::int64_t>();
    }
    catch (...)
    {
    }
    return std::nullopt;
}

int read_file_id(const Json &record, const char *key, std::string_view context)
{
    const auto found = record.find(key);
    if (found == record.end())
        throw std::invalid_argument(std::string(context) + " is missing " + key);
    const auto value = read_integer(*found);
    if (!value || *value < 0 || *value > std::numeric_limits<int>::max())
        throw std::invalid_argument(std::string(context) + " has an invalid file ID");
    return static_cast<int>(*value);
}

int read_component_type(const Json &component)
{
    const auto found = component.find("type");
    if (found == component.end())
        throw std::invalid_argument("component is missing type");
    const auto value = read_integer(*found);
    if (!value || *value < 0 || *value > std::numeric_limits<int>::max())
        throw std::invalid_argument("component has an invalid type discriminator");
    return static_cast<int>(*value);
}

void require_object_field(const Json &record, const char *name, std::string_view context)
{
    const auto found = record.find(name);
    if (found == record.end() || !found->is_object())
        throw std::invalid_argument(std::string(context) + "." + name + " must be an object");
}

void require_number_field(const Json &record, const char *name, std::string_view context)
{
    const auto found = record.find(name);
    if (found == record.end() || !found->is_number())
        throw std::invalid_argument(std::string(context) + "." + name + " must be numeric");
}

void validate_object_record(const Json &record, std::string_view context)
{
    if (!record.is_object())
        throw std::invalid_argument(std::string(context) + " must be an object");
    (void)read_file_id(record, "id", context);
    const auto name = record.find("name");
    const auto tag = record.find("tag");
    const auto active = record.find("active");
    if (name == record.end() || !name->is_string())
        throw std::invalid_argument(std::string(context) + ".name must be a string");
    if (tag == record.end() || !tag->is_string())
        throw std::invalid_argument(std::string(context) + ".tag must be a string");
    if (active == record.end() || !active->is_boolean())
        throw std::invalid_argument(std::string(context) + ".active must be a boolean");
    require_number_field(record, "rotation", context);
    require_object_field(record, "position", context);
    require_object_field(record, "scale", context);
    require_number_field(record.at("position"), "x", "position");
    require_number_field(record.at("position"), "y", "position");
    require_number_field(record.at("scale"), "x", "scale");
    require_number_field(record.at("scale"), "y", "scale");
    const auto children = record.find("children");
    const auto components = record.find("components");
    if (children == record.end() || !children->is_array())
        throw std::invalid_argument(std::string(context) + ".children must be an array");
    if (components == record.end() || !components->is_array())
        throw std::invalid_argument(std::string(context) + ".components must be an array");
}

void validate_behavior_collection(const Json &component)
{
    const auto behaviors = component.find("behaviors");
    if (behaviors == component.end() || !behaviors->is_array())
        throw std::invalid_argument("script component behaviors must be an array");
    for (const auto &behavior : *behaviors)
    {
        if (!behavior.is_object())
            throw std::invalid_argument("script behavior records must be objects");
        const auto type = behavior.find("type");
        if (type == behavior.end() || !type->is_string())
            throw std::invalid_argument("script behavior type must be a string");
    }
}

DocumentIndexes build_indexes(const Json &root, std::uint64_t generation, std::uint64_t revision)
{
    if (!root.is_array())
        throw std::invalid_argument("scene root must be a top-level array");

    struct PendingObject
    {
        const Json *record = nullptr;
        std::optional<int> parentId;
        ObjectLocation location;
        std::string path;
    };

    DocumentIndexes indexes;
    indexes.documentGeneration = generation;
    indexes.structureRevision = revision;
    indexes.orderedObjectIds.reserve(root.size());
    std::unordered_set<int> allIds;
    std::vector<PendingObject> pending;
    pending.reserve(root.size());
    for (std::size_t i = root.size(); i > 0; --i)
        pending.push_back(PendingObject{&root[i - 1], std::nullopt, {{i - 1}},
                                        "$[" + std::to_string(i - 1) + "]"});

    while (!pending.empty())
    {
        auto current = std::move(pending.back());
        pending.pop_back();
        validate_object_record(*current.record, current.path);
        const int objectId = read_file_id(*current.record, "id", current.path);
        if (!allIds.insert(objectId).second)
            throw std::invalid_argument("duplicate file ID " + std::to_string(objectId));
        indexes.objectsById.emplace(objectId, current.location);
        indexes.parentById.emplace(objectId, current.parentId);
        indexes.orderedObjectIds.push_back(objectId);

        std::unordered_set<int> componentSlots;
        const auto &components = current.record->at("components");
        for (std::size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex)
        {
            const auto &component = components[componentIndex];
            if (!component.is_object())
                throw std::invalid_argument(current.path + ".components entries must be objects");
            const int componentId = read_file_id(component, "id", "component");
            if (!allIds.insert(componentId).second)
                throw std::invalid_argument("duplicate file ID " + std::to_string(componentId));
            const int componentType = read_component_type(component);
            if (!componentSlots.insert(componentType).second)
                throw std::invalid_argument("duplicate component slot on object " + std::to_string(objectId));
            indexes.componentsById.emplace(componentId, ComponentLocation{objectId, componentIndex});
            if (componentType == kScriptType)
                validate_behavior_collection(component);
            if (componentType == kSpriteType)
                indexes.orderedSpriteIds.push_back(objectId);
        }

        const auto &children = current.record->at("children");
        for (std::size_t i = children.size(); i > 0; --i)
        {
            ObjectLocation location = current.location;
            location.childIndices.push_back(i - 1);
            pending.push_back(PendingObject{&children[i - 1], objectId, std::move(location),
                                            current.path + ".children[" + std::to_string(i - 1) + "]"});
        }
    }
    return indexes;
}

std::string normalize_known_asset_path(std::string raw)
{
    if (raw.empty())
        return raw;
    const std::string original = raw;
    std::replace(raw.begin(), raw.end(), '\\', '/');
    if (raw.rfind("assets/", 0) != 0)
        return original;

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= raw.size())
    {
        const auto end = raw.find('/', start);
        const auto length = (end == std::string::npos ? raw.size() : end) - start;
        const auto part = raw.substr(start, length);
        if (part == "..")
            return original;
        if (!part.empty() && part != ".")
            parts.push_back(part);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (parts.size() < 2)
        return original;
    std::string normalized;
    for (const auto &part : parts)
    {
        if (!normalized.empty())
            normalized.push_back('/');
        normalized += part;
    }
    return normalized;
}

void normalize_known_asset_paths(Json &root)
{
    if (!root.is_array())
        return;
    struct Pending
    {
        Json *record = nullptr;
    };
    std::vector<Pending> pending;
    pending.reserve(root.size());
    for (Json &object : root)
        pending.push_back({&object});

    while (!pending.empty())
    {
        Json *object = pending.back().record;
        pending.pop_back();
        if (!object->is_object())
            continue;
        auto components = object->find("components");
        if (components != object->end() && components->is_array())
        {
            for (Json &component : *components)
            {
                if (!component.is_object())
                    continue;
                const auto typeIt = component.find("type");
                const auto type = typeIt == component.end() ? std::optional<std::int64_t>{}
                                                            : read_integer(*typeIt);
                if (!type)
                    continue;
                const char *assetField = nullptr;
                if (*type == SPRITE)
                    assetField = "textureFilePath";
                else if (*type == ANIMATOR)
                    assetField = "animationClipFilePath";
                else if (*type == AUDIO)
                    assetField = "filePath";
                if (assetField != nullptr)
                {
                    auto path = component.find(assetField);
                    if (path != component.end() && path->is_string())
                        path->get_ref<std::string &>() = normalize_known_asset_path(path->get<std::string>());
                }
                else if (*type == SCRIPT)
                {
                    auto behaviors = component.find("behaviors");
                    if (behaviors == component.end() || !behaviors->is_array())
                        continue;
                    for (Json &behavior : *behaviors)
                    {
                        if (!behavior.is_object())
                            continue;
                        for (const char *field : {"icon", "idleClip", "sound"})
                        {
                            auto path = behavior.find(field);
                            if (path != behavior.end() && path->is_string())
                                path->get_ref<std::string &>() = normalize_known_asset_path(path->get<std::string>());
                        }
                    }
                }
            }
        }
        auto children = object->find("children");
        if (children != object->end() && children->is_array())
        {
            for (Json &child : *children)
                pending.push_back({&child});
        }
    }
}
} // namespace

DocumentParseResult parse_document(Json root, std::uint64_t documentGeneration,
                                   std::optional<std::filesystem::path> sourcePath,
                                   std::filesystem::path projectRoot,
                                   std::filesystem::path assetRoot)
{
    try
    {
        normalize_known_asset_paths(root);
        DocumentSession document;
        document.root = std::move(root);
        document.sourcePath = std::move(sourcePath);
        document.projectRoot = std::move(projectRoot);
        document.assetRoot = std::move(assetRoot);
        document.documentGeneration = documentGeneration;
        rebuild_document_indexes(document);
        return DocumentParseResult{std::move(document), {}};
    }
    catch (const std::exception &error)
    {
        return DocumentParseResult{std::nullopt, error.what()};
    }
}

DocumentSession make_default_document(std::filesystem::path projectRoot,
                                      std::filesystem::path assetRoot,
                                      std::uint64_t documentGeneration)
{
    DocumentSession document;
    document.projectRoot = std::move(projectRoot);
    document.assetRoot = std::move(assetRoot);
    document.documentGeneration = documentGeneration;
    document.root = Json::array({Json{{"id", 0},
                                      {"name", "Camera"},
                                      {"tag", ""},
                                      {"active", true},
                                      {"position", {{"x", 0.0}, {"y", 0.0}}},
                                      {"scale", {{"x", 1.0}, {"y", 1.0}}},
                                      {"rotation", 0.0},
                                      {"children", Json::array()},
                                      {"components", Json::array({Json{{"id", 1},
                                                                       {"type", kCameraType},
                                                                       {"width", 640.0},
                                                                       {"height", 480.0}}})}}});
    rebuild_document_indexes(document);
    return document;
}

void rebuild_document_indexes(DocumentSession &document)
{
    if (document.indexes.current_for(document.documentGeneration, document.structureRevision))
        return;
    auto next = build_indexes(document.root, document.documentGeneration, document.structureRevision);
    document.indexes = std::move(next);
}
} // namespace platformator_editor
