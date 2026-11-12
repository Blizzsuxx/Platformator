#include "editcommands.h"

#include "editorwork.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <platformator/gameobject.h>

namespace platformator_editor
{
namespace
{
DocumentIdentity document_identity(const DocumentSession &document)
{
    return {ProjectGeneration{document.projectGeneration}, document.tabId,
            DocumentGeneration{document.documentGeneration}};
}

bool address_matches_document(const DocumentSession &document, const PropertyAddress &address)
{
    if (document.documentGeneration != address.documentGeneration)
        return false;
    const bool scoped = static_cast<bool>(address.identity.projectGeneration) ||
                        static_cast<bool>(address.identity.tabId) ||
                        static_cast<bool>(address.identity.documentGeneration);
    if (document.tabId)
        return scoped && address.identity == document_identity(document);
    if (!scoped)
        return true;
    return address.identity.projectGeneration == ProjectGeneration{document.projectGeneration} &&
           !address.identity.tabId &&
           address.identity.documentGeneration == DocumentGeneration{document.documentGeneration};
}

template <typename Value>
struct ResolvedTarget
{
    Value *parent = nullptr;
    bool finalKey = false;
    std::string key;
    std::size_t index = 0;
    PropertyValue current;
};

struct ResolveFailure
{
    CommandStatus status = CommandStatus::Rejected;
    std::string message;
};

template <typename Value>
Value *resolve_object_at(Value &root, const ObjectLocation &location)
{
    if (!root.is_array() || location.childIndices.empty())
        return nullptr;
    Value *object = nullptr;
    Value *siblings = &root;
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

template <typename Value>
Value *find_component(Value &object, int componentId)
{
    if (!object.is_object())
        return nullptr;
    auto components = object.find("components");
    if (components == object.end() || !components->is_array())
        return nullptr;
    for (auto &component : *components)
    {
        if (!component.is_object())
            continue;
        const auto id = component.find("id");
        if (id == component.end() || !(id->is_number_integer() || id->is_number_unsigned()))
            continue;
        try
        {
            if (id->template get<std::int64_t>() == componentId)
                return &component;
        }
        catch (...)
        {
        }
    }
    return nullptr;
}

Json *find_address_owner(DocumentSession &document, const PropertyAddress &address, ResolveFailure &failure)
{
    if (address.behaviorOrdinal && !address.componentId)
    {
        failure = {CommandStatus::Rejected, "behavior address requires a component ID"};
        return nullptr;
    }
    if (!document.indexes.current_for(document.documentGeneration, document.structureRevision))
    {
        try
        {
            rebuild_document_indexes(document);
        }
        catch (...)
        {
            // Malformed unrelated components must not block editing a valid
            // target. Fall back to targeted tree lookup when indexes cannot be rebuilt.
        }
    }
    Json *object = nullptr;
    if (document.indexes.current_for(document.documentGeneration, document.structureRevision))
    {
        const auto location = document.indexes.objectsById.find(address.objectId);
        if (location != document.indexes.objectsById.end())
            object = resolve_object_at(document.root, location->second);
    }
    else if (const auto location = find_object_location(document.root, address.objectId))
        object = resolve_object_at(document.root, *location);
    if (object == nullptr)
    {
        failure = {CommandStatus::StaleAddress, "object ID is no longer present"};
        return nullptr;
    }
    if (!address.componentId)
        return object;

    Json *component = find_component(*object, *address.componentId);
    if (component == nullptr)
    {
        failure = {CommandStatus::StaleAddress, "component ID is no longer present on the object"};
        return nullptr;
    }
    if (!address.behaviorOrdinal)
        return component;

    auto behaviors = component->find("behaviors");
    if (behaviors == component->end() || !behaviors->is_array() || *address.behaviorOrdinal >= behaviors->size())
    {
        failure = {CommandStatus::StaleAddress, "behavior ordinal is no longer present"};
        return nullptr;
    }
    return &(*behaviors)[*address.behaviorOrdinal];
}

using CanonicalJsonPath = std::vector<FieldPathSegment>;

struct CanonicalJsonPathLess
{
    bool operator()(const CanonicalJsonPath &left, const CanonicalJsonPath &right) const
    {
        work::record(work::Counter::BatchOverlapPathComparisons);
        return left < right;
    }
};

bool path_has_prefix(const CanonicalJsonPath &path, const CanonicalJsonPath &prefix)
{
    return prefix.size() <= path.size() &&
           std::equal(prefix.begin(), prefix.end(), path.begin());
}

std::optional<CanonicalJsonPath> canonical_property_path(DocumentSession &document,
                                                          const PropertyAddress &address,
                                                          ResolveFailure &failure)
{
    Json *owner = find_address_owner(document, address, failure);
    if (owner == nullptr)
        return std::nullopt;

    std::optional<ObjectLocation> location;
    if (document.indexes.current_for(document.documentGeneration, document.structureRevision))
    {
        const auto found = document.indexes.objectsById.find(address.objectId);
        if (found != document.indexes.objectsById.end())
            location = found->second;
    }
    else
        location = find_object_location(document.root, address.objectId);
    if (!location || location->childIndices.empty())
    {
        failure = {CommandStatus::StaleAddress, "object ID is no longer present"};
        return std::nullopt;
    }

    CanonicalJsonPath path;
    path.reserve(location->childIndices.size() * 2 + address.fieldPath.size() + 4);
    path.emplace_back(location->childIndices.front());
    for (std::size_t depth = 1; depth < location->childIndices.size(); ++depth)
    {
        path.emplace_back(std::string("children"));
        path.emplace_back(location->childIndices[depth]);
    }

    Json *object = resolve_object_at(document.root, *location);
    if (object == nullptr)
    {
        failure = {CommandStatus::StaleAddress, "object location is no longer valid"};
        return std::nullopt;
    }
    enum class CanonicalOwner { Object, Component, Behavior, Opaque } ownerKind = CanonicalOwner::Object;
    if (address.componentId)
    {
        auto components = object->find("components");
        if (components == object->end() || !components->is_array())
        {
            failure = {CommandStatus::StaleAddress, "component collection is no longer present"};
            return std::nullopt;
        }
        std::optional<std::size_t> componentIndex;
        for (std::size_t index = 0; index < components->size(); ++index)
        {
            const auto &component = (*components)[index];
            if (!component.is_object())
                continue;
            const auto id = component.find("id");
            if (id == component.end() || !(id->is_number_integer() || id->is_number_unsigned()))
                continue;
            try
            {
                if (id->get<std::int64_t>() == *address.componentId)
                {
                    componentIndex = index;
                    break;
                }
            }
            catch (...)
            {
            }
        }
        if (!componentIndex)
        {
            failure = {CommandStatus::StaleAddress, "component ID is no longer present on the object"};
            return std::nullopt;
        }
        path.emplace_back(std::string("components"));
        path.emplace_back(*componentIndex);
        owner = &(*components)[*componentIndex];
        ownerKind = CanonicalOwner::Component;
        if (address.behaviorOrdinal)
        {
            auto behaviors = owner->find("behaviors");
            if (behaviors == owner->end() || !behaviors->is_array() ||
                *address.behaviorOrdinal >= behaviors->size())
            {
                failure = {CommandStatus::StaleAddress, "behavior ordinal is no longer present"};
                return std::nullopt;
            }
            path.emplace_back(std::string("behaviors"));
            path.emplace_back(*address.behaviorOrdinal);
            owner = &(*behaviors)[*address.behaviorOrdinal];
            ownerKind = CanonicalOwner::Behavior;
        }
    }

    if (address.fieldPath.empty())
    {
        failure = {CommandStatus::Rejected, "property address has an empty field path"};
        return std::nullopt;
    }

    Json *current = owner;
    for (std::size_t index = 0; index < address.fieldPath.size();)
    {
        const bool final = index + 1 == address.fieldPath.size();
        const auto &segment = address.fieldPath[index];
        if (const auto *key = std::get_if<std::string>(&segment))
        {
            if ((ownerKind == CanonicalOwner::Object || ownerKind == CanonicalOwner::Component) &&
                (*key == "id" || *key == "type"))
            {
                failure = {CommandStatus::Rejected, "IDs and type discriminators are not editable properties"};
                return std::nullopt;
            }
            if (ownerKind == CanonicalOwner::Behavior && *key == "type")
            {
                failure = {CommandStatus::Rejected, "IDs and type discriminators are not editable properties"};
                return std::nullopt;
            }

            const bool childrenCollection = ownerKind == CanonicalOwner::Object && *key == "children";
            const bool componentsCollection = ownerKind == CanonicalOwner::Object && *key == "components";
            const bool behaviorsCollection = ownerKind == CanonicalOwner::Component && *key == "behaviors";
            if (childrenCollection || componentsCollection || behaviorsCollection)
            {
                const auto collection = current->find(*key);
                if (!current->is_object() || collection == current->end() || !collection->is_array())
                {
                    failure = {CommandStatus::Rejected, "structural collection path is not an array"};
                    return std::nullopt;
                }
                path.emplace_back(*key);
                if (final || index + 1 >= address.fieldPath.size())
                {
                    failure = {CommandStatus::Rejected, "structural collections are changed with structural commands"};
                    return std::nullopt;
                }
                const auto *childIndex = std::get_if<std::size_t>(&address.fieldPath[index + 1]);
                if (childIndex == nullptr || *childIndex >= collection->size())
                {
                    failure = {CommandStatus::Rejected, "structural collection requires a valid item index"};
                    return std::nullopt;
                }
                path.emplace_back(*childIndex);
                if (index + 2 == address.fieldPath.size())
                {
                    failure = {CommandStatus::Rejected, "structural records are changed with structural commands"};
                    return std::nullopt;
                }
                current = &(*collection)[*childIndex];
                ownerKind = childrenCollection ? CanonicalOwner::Object
                           : componentsCollection ? CanonicalOwner::Component
                                                  : CanonicalOwner::Behavior;
                index += 2;
                continue;
            }

            if (!current->is_object())
            {
                failure = {CommandStatus::Rejected, "field path expects an object"};
                return std::nullopt;
            }
            const auto found = current->find(*key);
            if (!final && found == current->end())
            {
                failure = {CommandStatus::Rejected, "intermediate object key is missing"};
                return std::nullopt;
            }
            path.emplace_back(*key);
            if (!final)
                current = &*found;
            ownerKind = CanonicalOwner::Opaque;
        }
        else
        {
            const auto arrayIndex = std::get<std::size_t>(segment);
            if (!current->is_array() || arrayIndex >= current->size())
            {
                failure = {CommandStatus::Rejected, "field path array index is out of range"};
                return std::nullopt;
            }
            path.emplace_back(arrayIndex);
            if (!final)
                current = &(*current)[arrayIndex];
            ownerKind = CanonicalOwner::Opaque;
        }
        ++index;
    }
    return path;
}

std::optional<ResolvedTarget<Json>> resolve_target(DocumentSession &document,
                                                   const PropertyAddress &address,
                                                   ResolveFailure &failure)
{
    if (address.fieldPath.empty())
    {
        failure = {CommandStatus::Rejected, "property address has an empty field path"};
        return std::nullopt;
    }
    Json *current = find_address_owner(document, address, failure);
    if (current == nullptr)
        return std::nullopt;

    for (std::size_t depth = 0; depth + 1 < address.fieldPath.size(); ++depth)
    {
        const auto &segment = address.fieldPath[depth];
        if (const auto *key = std::get_if<std::string>(&segment))
        {
            if (!current->is_object())
            {
                failure = {CommandStatus::Rejected, "field path expects an object"};
                return std::nullopt;
            }
            auto found = current->find(*key);
            if (found == current->end())
            {
                failure = {CommandStatus::Rejected, "intermediate object key is missing"};
                return std::nullopt;
            }
            current = &*found;
        }
        else
        {
            const auto index = std::get<std::size_t>(segment);
            if (!current->is_array() || index >= current->size())
            {
                failure = {CommandStatus::Rejected, "field path array index is out of range"};
                return std::nullopt;
            }
            current = &(*current)[index];
        }
    }

    ResolvedTarget<Json> target;
    const auto &last = address.fieldPath.back();
    if (const auto *key = std::get_if<std::string>(&last))
    {
        if (!current->is_object())
        {
            failure = {CommandStatus::Rejected, "final field path expects an object"};
            return std::nullopt;
        }
        target.parent = current;
        target.finalKey = true;
        target.key = *key;
        const auto found = current->find(*key);
        if (found != current->end())
            target.current = PropertyValue::of(*found);
        else
            target.current = PropertyValue::missing();
    }
    else
    {
        const auto index = std::get<std::size_t>(last);
        if (!current->is_array() || index >= current->size())
        {
            failure = {CommandStatus::Rejected, "final field path array index is out of range"};
            return std::nullopt;
        }
        target.parent = current;
        target.index = index;
        target.current = PropertyValue::of((*current)[index]);
    }
    return target;
}

bool values_equal(const PropertyValue &left, const PropertyValue &right);

Json *resolve_gesture_owner(DocumentSession &document, const PropertyAddress &address) noexcept
{
    const auto location = document.indexes.objectsById.find(address.objectId);
    if (location == document.indexes.objectsById.end())
        return nullptr;
    Json *object = resolve_object_at(document.root, location->second);
    if (object == nullptr || !address.componentId)
        return address.componentId ? nullptr : object;
    Json *component = find_component(*object, *address.componentId);
    if (component == nullptr || !address.behaviorOrdinal)
        return address.behaviorOrdinal ? nullptr : component;
    auto behaviors = component->find("behaviors");
    if (behaviors == component->end() || !behaviors->is_array() ||
        *address.behaviorOrdinal >= behaviors->size())
        return nullptr;
    return &(*behaviors)[*address.behaviorOrdinal];
}

bool restore_gesture_property(DocumentSession &document, const PropertyAddress &address,
                              PropertyValue &before) noexcept
{
    if (address.fieldPath.empty())
        return false;
    Json *current = resolve_gesture_owner(document, address);
    if (current == nullptr)
        return false;
    for (std::size_t index = 0; index + 1 < address.fieldPath.size(); ++index)
    {
        const auto &segment = address.fieldPath[index];
        if (const auto *key = std::get_if<std::string>(&segment))
        {
            if (!current->is_object())
                return false;
            auto found = current->find(*key);
            if (found == current->end())
                return false;
            current = &*found;
        }
        else
        {
            const auto childIndex = std::get<std::size_t>(segment);
            if (!current->is_array() || childIndex >= current->size())
                return false;
            current = &(*current)[childIndex];
        }
    }

    const auto &last = address.fieldPath.back();
    if (const auto *key = std::get_if<std::string>(&last))
    {
        if (!current->is_object())
            return false;
        auto found = current->find(*key);
        if (!before.present)
        {
            if (found != current->end())
                current->erase(found);
            return true;
        }
        if (found == current->end())
            return false;
        found.value().swap(before.value);
        return true;
    }

    const auto childIndex = std::get<std::size_t>(last);
    if (!current->is_array() || childIndex >= current->size() || !before.present)
        return false;
    (*current)[childIndex].swap(before.value);
    return true;
}

struct PreparedPropertyPatch
{
    PropertyAddress address;
    std::size_t commandIndex = 0;
    Json *parent = nullptr;
    bool finalKey = false;
    std::string key;
    std::size_t index = 0;
    Json replacement;
    Json::object_t::node_type insertionNode;
    Json::object_t::node_type removedNode;
    bool insertsMissingKey = false;
    bool erasesExistingKey = false;
    bool applied = false;
};

bool prepare_property_patch(const ResolvedTarget<Json> &target, const PropertyValue &expected,
                            const PropertyValue &replacementValue, PreparedPropertyPatch &patch,
                            CommandStatus &status, std::string &message)
{
    if (!values_equal(target.current, expected))
    {
        status = CommandStatus::Rejected;
        message = "property command precondition no longer matches";
        return false;
    }
    if (!replacementValue.present && !target.finalKey)
    {
        status = CommandStatus::Rejected;
        message = "array entries cannot be removed by a property command";
        return false;
    }
    patch.parent = target.parent;
    patch.finalKey = target.finalKey;
    patch.key = target.key;
    patch.index = target.index;
    patch.insertsMissingKey = target.finalKey && !expected.present && replacementValue.present;
    patch.erasesExistingKey = target.finalKey && expected.present && !replacementValue.present;
    if (patch.insertsMissingKey)
    {
        Json holder = Json::object();
        holder.emplace(patch.key, replacementValue.value);
        patch.insertionNode = holder.get_ref<Json::object_t &>().extract(patch.key);
    }
    else if (!patch.erasesExistingKey)
        patch.replacement = replacementValue.present ? replacementValue.value : Json{};
    status = CommandStatus::Changed;
    return true;
}

void apply_property_patch(PreparedPropertyPatch &patch)
{
    if (patch.finalKey)
    {
        auto &object = patch.parent->get_ref<Json::object_t &>();
        if (patch.insertsMissingKey)
        {
            auto inserted = object.insert(std::move(patch.insertionNode));
            if (!inserted.inserted)
            {
                patch.insertionNode = std::move(inserted.node);
                throw std::logic_error("property key appeared after batch prevalidation");
            }
        }
        else if (patch.erasesExistingKey)
        {
            patch.removedNode = object.extract(patch.key);
            if (patch.removedNode.empty())
                throw std::logic_error("property key disappeared after batch prevalidation");
        }
        else
        {
            auto found = object.find(patch.key);
            if (found == object.end())
                throw std::logic_error("property key disappeared after batch prevalidation");
            found->second.swap(patch.replacement);
        }
    }
    else
    {
        if (!patch.parent->is_array() || patch.index >= patch.parent->size())
            throw std::logic_error("array entry changed after batch prevalidation");
        (*patch.parent)[patch.index].swap(patch.replacement);
    }
    patch.applied = true;
}

void rollback_property_patches(std::vector<PreparedPropertyPatch> &patches) noexcept
{
    for (auto patch = patches.rbegin(); patch != patches.rend(); ++patch)
    {
        if (!patch->applied)
            continue;
        if (patch->finalKey)
        {
            auto &object = patch->parent->get_ref<Json::object_t &>();
            if (patch->insertsMissingKey)
                patch->insertionNode = object.extract(patch->key);
            else if (patch->erasesExistingKey)
                object.insert(std::move(patch->removedNode));
            else
            {
                auto found = object.find(patch->key);
                if (found != object.end())
                    found->second.swap(patch->replacement);
            }
        }
        else if (patch->parent->is_array() && patch->index < patch->parent->size())
            (*patch->parent)[patch->index].swap(patch->replacement);
        patch->applied = false;
    }
}

CommandResult failed(CommandStatus status, std::string message, const DocumentSession &document)
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                   document.valueRevision, false};
    return result;
}

bool values_equal(const PropertyValue &left, const PropertyValue &right)
{
    return left.present == right.present && (!left.present || left.value == right.value);
}

void normalize_affected_ids(std::vector<int> &ids);

std::size_t json_payload_bytes(const Json &value)
{
    return value.dump().size();
}

std::size_t command_payload_bytes(const PropertyCommand &command)
{
    std::size_t bytes = sizeof(PropertyCommand) + command.address.fieldPath.size() * sizeof(FieldPathSegment);
    for (const auto &segment : command.address.fieldPath)
        if (const auto *key = std::get_if<std::string>(&segment))
            bytes += key->size();
    if (command.before.present)
        bytes += json_payload_bytes(command.before.value);
    if (command.after.present)
        bytes += json_payload_bytes(command.after.value);
    return bytes;
}

std::size_t command_payload_bytes(const StructuralCommand &command)
{
    return sizeof(StructuralCommand) + json_payload_bytes(command.payload);
}

std::size_t command_payload_bytes(const BatchCommand &command)
{
    std::size_t bytes = sizeof(BatchCommand);
    for (const auto &operation : command.operations)
        bytes += std::visit([](const auto &stored) { return command_payload_bytes(stored); }, operation);
    return bytes;
}

void merge_document_change(DocumentChange &into, const DocumentChange &part)
{
    into.structural = into.structural || part.structural;
    into.affectedObjectIds.insert(into.affectedObjectIds.end(), part.affectedObjectIds.begin(),
                                  part.affectedObjectIds.end());
    into.changedPaths.insert(into.changedPaths.end(), part.changedPaths.begin(), part.changedPaths.end());
    into.categories.insert(into.categories.end(), part.categories.begin(), part.categories.end());
    normalize_affected_ids(into.affectedObjectIds);
    std::sort(into.changedPaths.begin(), into.changedPaths.end());
    into.changedPaths.erase(std::unique(into.changedPaths.begin(), into.changedPaths.end()),
                            into.changedPaths.end());
    std::sort(into.categories.begin(), into.categories.end());
    into.categories.erase(std::unique(into.categories.begin(), into.categories.end()),
                          into.categories.end());
}

void normalize_batch_structure_revision(const DocumentSession &original,
                                       DocumentSession &candidate,
                                       bool structuralChange)
{
    if (!structuralChange)
        return;
    if (original.structureRevision == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("document structure revision space exhausted");
    candidate.structureRevision = original.structureRevision + 1;
    rebuild_document_indexes(candidate);
}

CommandResult command_result(const StructuralCommandResult &structural)
{
    CommandResult result;
    result.status = structural.status;
    result.message = structural.message;
    result.affectedObjectIds = structural.affectedObjectIds;
    result.change = structural.change;
    return result;
}

bool selection_is_empty(const Selection &selection)
{
    return !selection.objectId && !selection.componentId && !selection.behaviorOrdinal &&
           selection.focusedPanel == FocusedPanel::None && !selection.focusedHost &&
           selection.selectedObjectIds.empty() && !selection.rangeAnchorObjectId;
}

Json *mutable_object(DocumentSession &document, int objectId)
{
    const auto found = document.indexes.objectsById.find(objectId);
    if (found == document.indexes.objectsById.end())
        return nullptr;
    return resolve_object_at(document.root, found->second);
}

Json *mutable_siblings(DocumentSession &document, const std::optional<int> &parentObjectId)
{
    if (!parentObjectId)
        return &document.root;
    Json *parent = mutable_object(document, *parentObjectId);
    if (parent == nullptr || !parent->is_object())
        return nullptr;
    auto children = parent->find("children");
    return children != parent->end() && children->is_array() ? &*children : nullptr;
}

std::optional<int> component_type(const Json &component)
{
    const auto type = component.find("type");
    if (type == component.end() || !(type->is_number_integer() || type->is_number_unsigned()))
        return std::nullopt;
    try
    {
        const auto value = type->get<std::int64_t>();
        if (value < 0 || value > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::string changed_path(const PropertyAddress &address)
{
    std::string path = "object[" + std::to_string(address.objectId) + "]";
    if (address.componentId)
        path += ".components[" + std::to_string(*address.componentId) + "]";
    if (address.behaviorOrdinal)
        path += ".behaviors[" + std::to_string(*address.behaviorOrdinal) + "]";
    for (const auto &segment : address.fieldPath)
    {
        if (const auto *key = std::get_if<std::string>(&segment))
            path += "." + *key;
        else
            path += "[" + std::to_string(std::get<std::size_t>(segment)) + "]";
    }
    return path;
}

void normalize_affected_ids(std::vector<int> &ids)
{
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

void set_property_change_metadata(DocumentSession &document, const PropertyAddress &address,
                                  DocumentChange &change, std::vector<int> &affectedObjectIds)
{
    normalize_affected_ids(affectedObjectIds);
    change.identity = document_identity(document);
    change.affectedObjectIds = affectedObjectIds;
    change.changedPaths.push_back(changed_path(address));

    const std::string *topField = address.fieldPath.empty()
                                      ? nullptr
                                      : std::get_if<std::string>(&address.fieldPath.front());
    if (address.behaviorOrdinal && address.componentId)
    {
        bool references = false;
        for (const auto &segment : address.fieldPath)
            if (const auto *key = std::get_if<std::string>(&segment); key && *key == "references")
                references = true;
        if (references)
            change.categories.push_back(DocumentChangeCategory::BehaviorReference);
    }
    else if (!address.componentId && topField != nullptr)
    {
        if (*topField == "name" || *topField == "label" || *topField == "tag")
            change.categories.push_back(DocumentChangeCategory::Label);
        else if (*topField == "position" || *topField == "rotation" || *topField == "scale")
            change.categories.push_back(DocumentChangeCategory::Pose);
        else if (*topField == "active")
            change.categories.push_back(DocumentChangeCategory::Active);
    }
    else if (address.componentId)
    {
        ResolveFailure ignored;
        const Json *component = find_address_owner(document, address, ignored);
        const auto type = component == nullptr ? std::nullopt : component_type(*component);
        if (type == SPRITE)
            change.categories.push_back(DocumentChangeCategory::Sprite);
        else if (type == COLLIDER)
            change.categories.push_back(DocumentChangeCategory::Collider);
        else if (type == CAMERA)
            change.categories.push_back(DocumentChangeCategory::Camera);
    }
}

std::optional<int> record_id(const Json &record)
{
    const auto id = record.find("id");
    if (id == record.end() || !(id->is_number_integer() || id->is_number_unsigned()))
        return std::nullopt;
    try
    {
        const auto value = id->get<std::int64_t>();
        if (value < 0 || value > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<int> read_nonnegative_json_id(const Json &value)
{
    if (!value.is_number_integer() && !value.is_number_unsigned())
        return std::nullopt;
    try
    {
        const auto parsed = value.get<std::int64_t>();
        if (parsed < 0 || parsed > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(parsed);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool file_id_in_use(const DocumentSession &document, int id)
{
    return document.indexes.objectsById.contains(id) || document.indexes.componentsById.contains(id);
}

std::optional<int> first_unused_id(const DocumentSession &document)
{
    int candidate = 0;
    for (const int id : document.indexes.orderedObjectIds)
    {
        if (id == std::numeric_limits<int>::max())
            return std::nullopt;
        candidate = std::max(candidate, id + 1);
    }
    for (const auto &[id, location] : document.indexes.componentsById)
    {
        (void)location;
        if (id == std::numeric_limits<int>::max())
            return std::nullopt;
        candidate = std::max(candidate, id + 1);
    }
    while (file_id_in_use(document, candidate))
    {
        if (candidate == std::numeric_limits<int>::max())
            return std::nullopt;
        ++candidate;
    }
    return candidate;
}

std::optional<int> next_file_id_from_set(int &candidate, const std::vector<int> &assigned)
{
    while (candidate >= 0 && std::find(assigned.begin(), assigned.end(), candidate) != assigned.end())
    {
        if (candidate == std::numeric_limits<int>::max())
            return std::nullopt;
        ++candidate;
    }
    if (candidate < 0)
        return std::nullopt;
    const int result = candidate;
    if (candidate == std::numeric_limits<int>::max())
        candidate = -1;
    else
        ++candidate;
    return result;
}

void collect_subtree_object_ids(const Json &root, std::vector<int> &ids)
{
    std::vector<const Json *> pending{&root};
    while (!pending.empty())
    {
        const Json *object = pending.back();
        pending.pop_back();
        if (!object->is_object())
            continue;
        if (auto id = record_id(*object))
            ids.push_back(*id);
        const auto components = object->find("components");
        if (components != object->end() && components->is_array())
        {
            for (const auto &component : *components)
                if (auto id = record_id(component))
                    ids.push_back(*id);
        }
        const auto children = object->find("children");
        if (children != object->end() && children->is_array())
            for (const auto &child : *children)
                pending.push_back(&child);
    }
}

void remap_subtree_ids(Json &root, int firstId, std::vector<int> &assigned,
                       std::unordered_map<int, int> *sourceToDuplicate = nullptr)
{
    int candidate = firstId;
    std::vector<Json *> pending{&root};
    while (!pending.empty())
    {
        Json *object = pending.back();
        pending.pop_back();
        if (!object->is_object())
            continue;
        const auto sourceObjectId = record_id(*object);
        auto objectId = next_file_id_from_set(candidate, assigned);
        if (!objectId)
            throw std::overflow_error("no file IDs remain for duplicated subtree");
        (*object)["id"] = *objectId;
        assigned.push_back(*objectId);
        if (sourceToDuplicate != nullptr && sourceObjectId &&
            !sourceToDuplicate->emplace(*sourceObjectId, *objectId).second)
            throw std::invalid_argument("selected duplicate roots contain overlapping object identities");
        auto components = object->find("components");
        if (components != object->end() && components->is_array())
        {
            for (auto &component : *components)
            {
                const auto sourceComponentId = record_id(component);
                auto componentId = next_file_id_from_set(candidate, assigned);
                if (!componentId)
                    throw std::overflow_error("no file IDs remain for duplicated subtree");
                component["id"] = *componentId;
                assigned.push_back(*componentId);
                if (sourceToDuplicate != nullptr && sourceComponentId &&
                    !sourceToDuplicate->emplace(*sourceComponentId, *componentId).second)
                    throw std::invalid_argument("selected duplicate roots contain overlapping component identities");
            }
        }
        auto children = object->find("children");
        if (children != object->end() && children->is_array())
            for (auto &child : *children)
                pending.push_back(&child);
    }
}

bool duplicate_target_kind_exists(const DocumentSession &document, int id, std::string_view kind)
{
    if (kind == "GameObject" || kind == "Object")
        return document.indexes.objectsById.contains(id);
    if (kind == "Component")
        return document.indexes.componentsById.contains(id);
    return false;
}

Json *find_object_by_id(Json &root, int objectId)
{
    std::vector<Json *> pending;
    if (root.is_array())
    {
        for (auto &object : root)
            pending.push_back(&object);
    }
    else if (root.is_object())
        pending.push_back(&root);
    while (!pending.empty())
    {
        Json *object = pending.back();
        pending.pop_back();
        if (!object->is_object())
            continue;
        if (record_id(*object) == objectId)
            return object;
        const auto children = object->find("children");
        if (children != object->end() && children->is_array())
            for (auto &child : *children)
                pending.push_back(&child);
    }
    return nullptr;
}

bool parse_reference_pointer(std::string_view pointer, std::vector<std::string> &segments)
{
    if (pointer.empty() || pointer.front() != '/')
        return false;
    std::size_t start = 1;
    while (start <= pointer.size())
    {
        const auto slash = pointer.find('/', start);
        const auto end = slash == std::string_view::npos ? pointer.size() : slash;
        std::string segment;
        for (std::size_t index = start; index < end; ++index)
        {
            if (pointer[index] != '~')
            {
                segment.push_back(pointer[index]);
                continue;
            }
            if (index + 1 >= end)
                return false;
            const char escape = pointer[++index];
            if (escape == '0') segment.push_back('~');
            else if (escape == '1') segment.push_back('/');
            else return false;
        }
        if (segment.empty())
            return false;
        segments.push_back(std::move(segment));
        if (slash == std::string_view::npos)
            break;
        start = slash + 1;
    }
    return !segments.empty();
}

Json *resolve_reference_pointer(Json &owner, const std::vector<std::string> &segments)
{
    Json *value = &owner;
    for (const auto &segment : segments)
    {
        if (value->is_object())
        {
            const auto found = value->find(segment);
            if (found == value->end()) return nullptr;
            value = &*found;
        }
        else if (value->is_array())
        {
            std::size_t index = 0;
            const auto parsed = std::from_chars(segment.data(), segment.data() + segment.size(), index);
            if ((segment.size() > 1 && segment.front() == '0') ||
                parsed.ec != std::errc{} || parsed.ptr != segment.data() + segment.size() ||
                index >= value->size())
                return nullptr;
            value = &(*value)[index];
        }
        else
            return nullptr;
    }
    return value;
}

bool remap_duplicate_reference_sites(const DocumentSession &document,
                                     const std::vector<Json *> &duplicateRoots,
                                     const std::unordered_map<int, int> &sourceToDuplicate,
                                     const Json &bindings, std::string &error)
{
    if (!bindings.is_array())
    {
        error = "typed duplicate reference bindings must be an array";
        return false;
    }
    std::unordered_set<std::string> sites;
    for (const auto &binding : bindings)
    {
        if (!binding.is_object() || !binding.contains("owner_id") ||
            !binding.contains("path") || !binding["path"].is_string() ||
            !binding.contains("target_kind") || !binding["target_kind"].is_string() ||
            !binding.contains("scope") || !binding["scope"].is_string())
        {
            error = "typed duplicate reference binding is malformed";
            return false;
        }
        const auto sourceOwner = read_nonnegative_json_id(binding["owner_id"]);
        std::optional<int> sourceComponent;
        if (binding.contains("component_id"))
            sourceComponent = read_nonnegative_json_id(binding["component_id"]);
        if (!sourceOwner || (binding.contains("component_id") && !sourceComponent) ||
            (binding["scope"] != "internal" && binding["scope"] != "external"))
        {
            error = "typed duplicate reference owner or scope is invalid";
            return false;
        }
        const auto mappedOwner = sourceToDuplicate.find(*sourceOwner);
        const auto mappedComponent = sourceComponent
            ? sourceToDuplicate.find(*sourceComponent) : sourceToDuplicate.end();
        if (mappedOwner == sourceToDuplicate.end() ||
            (sourceComponent && mappedComponent == sourceToDuplicate.end()))
        {
            error = "typed duplicate reference owner is outside the duplicated roots";
            return false;
        }
        Json *owner = nullptr;
        for (Json *root : duplicateRoots)
        {
            Json *candidate = find_object_by_id(*root, mappedOwner->second);
            if (candidate != nullptr)
            {
                owner = candidate;
                break;
            }
        }
        if (owner == nullptr)
        {
            error = "typed duplicate reference owner could not be found in the copied roots";
            return false;
        }
        if (sourceComponent)
        {
            Json *component = nullptr;
            auto components = owner->find("components");
            if (components != owner->end() && components->is_array())
                for (auto &entry : *components)
                    if (record_id(entry) == mappedComponent->second)
                    {
                        component = &entry;
                        break;
                    }
            if (component == nullptr)
            {
                error = "typed duplicate reference component is not owned by the declared object";
                return false;
            }
            owner = component;
        }

        const std::string path = binding["path"].get<std::string>();
        std::vector<std::string> segments;
        if (!parse_reference_pointer(path, segments) ||
            std::any_of(segments.begin(), segments.end(), [](const std::string &part) {
                return part == "id" || part == "type";
            }))
        {
            error = "typed duplicate reference path is invalid or targets a discriminator";
            return false;
        }
        const std::string site = std::to_string(*sourceOwner) + ":" +
            std::to_string(sourceComponent.value_or(-1)) + ":" + path;
        if (!sites.insert(site).second)
        {
            error = "typed duplicate reference sites must be unique";
            return false;
        }
        Json *target = resolve_reference_pointer(*owner, segments);
        if (target == nullptr || !target->is_number_integer())
        {
            error = "typed duplicate reference path must resolve to an integer target ID";
            return false;
        }
        const auto sourceTargetValue = read_nonnegative_json_id(*target);
        if (!sourceTargetValue)
        {
            error = "typed duplicate reference target ID is outside the supported range";
            return false;
        }
        const int sourceTarget = *sourceTargetValue;
        const std::string targetKind = binding["target_kind"].get<std::string>();
        if (!duplicate_target_kind_exists(document, sourceTarget, targetKind))
        {
            error = "typed duplicate reference target is absent or has the wrong kind";
            return false;
        }
        const auto mappedTarget = sourceToDuplicate.find(sourceTarget);
        if (mappedTarget != sourceToDuplicate.end())
            *target = mappedTarget->second;
        else if (binding["scope"] == "internal")
        {
            error = "declared internal duplicate reference target is outside the complete copied set";
            return false;
        }
    }
    return true;
}

StructuralCommandResult failed_structural(CommandStatus status, std::string message,
                                          const DocumentSession &document)
{
    StructuralCommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                   document.valueRevision, false};
    return result;
}

StructuralCommandResult commit_structural(DocumentSession &document, StructuralCommand command,
                                           bool inserting)
{
    if (document.structureRevision == std::numeric_limits<std::uint64_t>::max())
        return failed_structural(CommandStatus::Rejected, "document structure revision space exhausted", document);
    if (document.documentGeneration != command.documentGeneration)
        return failed_structural(CommandStatus::StaleAddress, "structural command belongs to an older document generation", document);
    if (document.tabId && command.identity != document_identity(document))
        return failed_structural(CommandStatus::StaleAddress,
                                 "structural command belongs to an older tab or project generation", document);
    rebuild_document_indexes(document);

    std::vector<int> preparedAffectedIds;
    if (command.kind == StructuralCommandKind::InsertObject ||
        command.kind == StructuralCommandKind::DeleteObject)
        collect_subtree_object_ids(command.payload, preparedAffectedIds);
    else
        preparedAffectedIds.push_back(command.ownerObjectId);
    normalize_affected_ids(preparedAffectedIds);
    DocumentChange preparedChange{document.documentGeneration, document.structureRevision + 1,
                                  document.valueRevision, true};
    preparedChange.identity = document_identity(document);
    preparedChange.affectedObjectIds = preparedAffectedIds;
    preparedChange.changedPaths.push_back("structure");
    preparedChange.categories.push_back(DocumentChangeCategory::Structure);

    Json *siblings = nullptr;
    if (command.kind == StructuralCommandKind::InsertObject || command.kind == StructuralCommandKind::DeleteObject)
        siblings = mutable_siblings(document, command.parentObjectId);
    else
    {
        Json *owner = mutable_object(document, command.ownerObjectId);
        if (owner == nullptr)
            return failed_structural(CommandStatus::StaleAddress, "structural command owner is no longer present", document);
        if (command.kind == StructuralCommandKind::InsertBehavior ||
            command.kind == StructuralCommandKind::DeleteBehavior)
        {
            if (!command.componentId)
                return failed_structural(CommandStatus::Rejected, "behavior command requires a script component ID", document);
            Json *script = find_component(*owner, *command.componentId);
            if (script == nullptr || component_type(*script) != SCRIPT)
                return failed_structural(CommandStatus::StaleAddress, "script component is no longer present", document);
            auto collection = script->find("behaviors");
            if (collection == script->end() || !collection->is_array())
                return failed_structural(CommandStatus::Rejected, "script behavior list is malformed", document);
            siblings = &*collection;
        }
        else
        {
            auto collection = owner->find("components");
            if (collection == owner->end() || !collection->is_array())
                return failed_structural(CommandStatus::Rejected, "component collection is missing or malformed", document);
            siblings = &*collection;
        }
    }
    if (siblings == nullptr || !siblings->is_array())
        return failed_structural(CommandStatus::StaleAddress, "structural destination no longer exists", document);

    const std::size_t oldIndex = command.insertionIndex;
    if (inserting)
    {
        if (oldIndex > siblings->size())
            return failed_structural(CommandStatus::Rejected, "structural insertion index is out of range", document);
        try
        {
            siblings->insert(siblings->begin() + static_cast<Json::difference_type>(oldIndex), command.payload);
        }
        catch (const std::exception &error)
        {
            return failed_structural(CommandStatus::Rejected, std::string("cannot insert structural payload: ") + error.what(), document);
        }
    }
    else
    {
        if (oldIndex >= siblings->size() || (*siblings)[oldIndex] != command.payload)
            return failed_structural(CommandStatus::Rejected, "structural command precondition no longer matches", document);
        siblings->erase(siblings->begin() + static_cast<Json::difference_type>(oldIndex));
    }

    const auto previousRevision = document.structureRevision;
    document.structureRevision = previousRevision + 1;
    try
    {
        rebuild_document_indexes(document);
    }
    catch (const std::exception &error)
    {
        document.structureRevision = previousRevision;
        if (inserting)
            siblings->erase(siblings->begin() + static_cast<Json::difference_type>(oldIndex));
        else
            siblings->insert(siblings->begin() + static_cast<Json::difference_type>(oldIndex), std::move(command.payload));
        return failed_structural(CommandStatus::Rejected, std::string("structural payload is invalid: ") + error.what(), document);
    }

    StructuralCommandResult result;
    result.status = CommandStatus::Changed;
    result.change = std::move(preparedChange);
    result.command = std::move(command);
    result.affectedObjectIds = std::move(preparedAffectedIds);
    // The service-level observer list is attached by the caller after the commit.
    return result;
}
} // namespace

PropertyValue PropertyValue::missing()
{
    return PropertyValue{false, Json{}};
}

PropertyValue PropertyValue::of(Json value)
{
    return PropertyValue{true, std::move(value)};
}

EditCommandService::EditCommandService(std::size_t historyBudgetBytes)
    : historyBudgetBytes_(historyBudgetBytes)
{
}

void EditCommandService::subscribe(Observer observer)
{
    if (observer)
        observers_.push_back(std::move(observer));
}

void EditCommandService::set_selection_provider(SelectionProvider provider)
{
    selectionProvider_ = std::move(provider);
}

SelectionState EditCommandService::capture_selection(SelectionState selection) const
{
    if (selection_is_empty(selection.before) && selection_is_empty(selection.after) && selectionProvider_)
    {
        try
        {
            selection.before = selectionProvider_();
            selection.after = selection.before;
        }
        catch (...)
        {
        }
    }
    return selection;
}

void EditCommandService::reset_history(std::uint64_t documentGeneration)
{
    history_.clear();
    cursor_ = 0;
    retainedBytes_ = 0;
    activeGestures_ = 0;
    activeChangedGestures_ = 0;
    historyGeneration_ = documentGeneration;
    lastError_.clear();
    failNextHistoryRetentionForTesting_ = false;
    baseToken_ = nextToken_;
    if (nextToken_ != std::numeric_limits<std::uint64_t>::max())
        ++nextToken_;
    savedToken_ = baseToken_;
}

bool EditCommandService::can_migrate_project_identity(const DocumentIdentity &from,
                                                       const DocumentIdentity &to) const noexcept
{
    if (!from.projectGeneration || !from.documentGeneration || !to.projectGeneration ||
        to.projectGeneration <= from.projectGeneration || from.tabId != to.tabId ||
        from.documentGeneration != to.documentGeneration || historyGeneration_ != from.documentGeneration.value)
        return false;

    const auto isUnscoped = [](const DocumentIdentity &identity) {
        return !identity.projectGeneration && !identity.tabId && !identity.documentGeneration;
    };
    const auto validProperty = [&](const PropertyCommand &property) {
        const auto &address = property.address;
        return address.documentGeneration == from.documentGeneration.value &&
               (address.identity == from || (!from.tabId && isUnscoped(address.identity)));
    };
    const auto validStructural = [&](const StructuralCommand &structural) {
        return structural.documentGeneration == from.documentGeneration.value &&
               (structural.identity == from || (!from.tabId && isUnscoped(structural.identity)));
    };
    for (const auto &entry : history_)
    {
        const bool valid = std::visit([&](const auto &stored) {
            using Stored = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<Stored, PropertyCommand>)
                return validProperty(stored);
            else if constexpr (std::is_same_v<Stored, StructuralCommand>)
                return validStructural(stored);
            else
            {
                if (stored.identity != from)
                    return false;
                for (const auto &operation : stored.operations)
                {
                    if (const auto *property = std::get_if<PropertyCommand>(&operation))
                    {
                        if (!validProperty(*property))
                            return false;
                    }
                    else if (!validStructural(std::get<StructuralCommand>(operation)))
                        return false;
                }
                return true;
            }
        }, entry.command);
        if (!valid)
            return false;
    }
    return true;
}

void EditCommandService::migrate_project_identity(const DocumentIdentity &from,
                                                  const DocumentIdentity &to) noexcept
{
    assert(can_migrate_project_identity(from, to));
    const auto migrateAddress = [&](PropertyAddress &address) {
        if (address.identity.projectGeneration || address.identity.tabId ||
            address.identity.documentGeneration)
            address.identity = to;
    };
    const auto migrateStructural = [&](StructuralCommand &structural) {
        if (structural.identity.projectGeneration || structural.identity.tabId ||
            structural.identity.documentGeneration)
            structural.identity = to;
    };
    for (auto &entry : history_)
        std::visit([&](auto &stored) {
            using Stored = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<Stored, PropertyCommand>)
                migrateAddress(stored.address);
            else if constexpr (std::is_same_v<Stored, StructuralCommand>)
                migrateStructural(stored);
            else
            {
                stored.identity = to;
                for (auto &operation : stored.operations)
                    if (auto *property = std::get_if<PropertyCommand>(&operation))
                        migrateAddress(property->address);
                    else
                        migrateStructural(std::get<StructuralCommand>(operation));
            }
        }, entry.command);
}

void EditCommandService::mark_saved() noexcept
{
    savedToken_ = current_token();
}

std::uint64_t EditCommandService::current_token() const noexcept
{
    return cursor_ == 0 ? baseToken_ : history_[cursor_ - 1].afterToken;
}

bool EditCommandService::token_reachable(std::uint64_t token) const noexcept
{
    if (token == baseToken_ || token == current_token())
        return true;
    for (const auto &entry : history_)
        if (entry.beforeToken == token || entry.afterToken == token)
            return true;
    return false;
}

bool EditCommandService::dirty() const noexcept
{
    return activeChangedGestures_ > 0 || current_token() != savedToken_ || !token_reachable(savedToken_);
}

HistoryStatus EditCommandService::history_status() const noexcept
{
    HistoryStatus status;
    status.canUndo = cursor_ > 0;
    status.canRedo = cursor_ < history_.size();
    status.dirty = dirty();
    status.savedTokenReachable = token_reachable(savedToken_);
    status.overBudget = retainedBytes_ > historyBudgetBytes_;
    status.retainedBytes = retainedBytes_;
    status.budgetBytes = historyBudgetBytes_;
    status.undoCommands = cursor_;
    status.redoCommands = history_.size() - cursor_;
    status.currentToken = current_token();
    status.savedToken = savedToken_;
    return status;
}

void EditCommandService::set_last_selection_state(SelectionState selection)
{
    if (cursor_ == 0)
        return;
    auto &command = history_[cursor_ - 1].command;
    std::visit([&](auto &stored) { stored.selection = std::move(selection); }, command);
}

void EditCommandService::inject_history_retention_failure_once_for_testing() noexcept
{
    failNextHistoryRetentionForTesting_ = true;
}

const std::string &EditCommandService::last_error() const noexcept
{
    return lastError_;
}

const EditCommandWorkStats &EditCommandService::last_work_stats() const noexcept
{
    return lastWorkStats_;
}

EditCommandService::PreparedHistory EditCommandService::prepare_history(
    std::variant<PropertyCommand, StructuralCommand, BatchCommand> command)
{
    if (failNextHistoryRetentionForTesting_)
    {
        failNextHistoryRetentionForTesting_ = false;
        throw std::bad_alloc();
    }
    if (nextToken_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("history state token space exhausted");
    const std::size_t bytes = std::visit([](const auto &stored) { return command_payload_bytes(stored); }, command);
    const std::uint64_t beforeToken = cursor_ == 0 ? baseToken_ : history_[cursor_ - 1].afterToken;
    const std::uint64_t afterToken = nextToken_;
    // A single staged tail node reserves the only history allocation needed by
    // this edit. It is invisible to observers until commit_history finalizes it.
    history_.push_back(HistoryEntry{std::move(command), beforeToken, afterToken, bytes});
    return PreparedHistory{cursor_, bytes, afterToken, true};
}

void EditCommandService::commit_history(PreparedHistory &&prepared) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<HistoryEntry>);
    static_assert(std::is_nothrow_move_assignable_v<HistoryEntry>);
    assert(prepared.staged && !history_.empty() && history_.back().afterToken == prepared.afterToken);
    assert(cursor_ == prepared.priorCursor);

    const std::size_t oldHistorySize = history_.size() - 1;
    if (oldHistorySize > prepared.priorCursor)
    {
        // Preserve the staged entry while discarding the redo branch. Moving it
        // into the first redo slot and popping the tail needs no new storage.
        HistoryEntry staged = std::move(history_.back());
        history_.pop_back();
        retainedBytes_ -= history_[prepared.priorCursor].retainedBytes;
        history_[prepared.priorCursor] = std::move(staged);
        while (history_.size() > prepared.priorCursor + 1)
        {
            retainedBytes_ -= history_.back().retainedBytes;
            history_.pop_back();
        }
    }
    retainedBytes_ += prepared.payloadBytes;
    cursor_ = prepared.priorCursor + 1;
    nextToken_ = prepared.afterToken + 1;
    while (retainedBytes_ > historyBudgetBytes_ && history_.size() > 1)
    {
        const std::size_t removedBytes = history_.front().retainedBytes;
        const std::uint64_t removedBeforeToken = history_.front().beforeToken;
        const std::uint64_t removedAfterToken = history_.front().afterToken;
        retainedBytes_ -= removedBytes;
        history_.pop_front();
        if (cursor_ > 0)
        {
            --cursor_;
            baseToken_ = removedAfterToken;
        }
        else
            baseToken_ = removedBeforeToken;
    }
    prepared.staged = false;
    lastError_.clear();
}

void EditCommandService::rollback_history(PreparedHistory &prepared) noexcept
{
    if (!prepared.staged)
        return;
    assert(!history_.empty() && history_.back().afterToken == prepared.afterToken);
    history_.pop_back();
    prepared.staged = false;
}

void EditCommandService::append_history(std::variant<PropertyCommand, StructuralCommand, BatchCommand> command)
{
    auto prepared = prepare_history(std::move(command));
    commit_history(std::move(prepared));
}

void EditCommandService::prune_history()
{
    while (retainedBytes_ > historyBudgetBytes_ && history_.size() > 1)
    {
        const auto removed = history_.front();
        retainedBytes_ -= removed.retainedBytes;
        history_.pop_front();
        if (cursor_ > 0)
        {
            --cursor_;
            baseToken_ = removed.afterToken;
        }
        else
            baseToken_ = removed.beforeToken;
    }
}

CommandResult EditCommandService::undo(DocumentSession &document, SelectionModel &selection)
{
    if (cursor_ == 0)
        return failed(CommandStatus::Rejected, "there is no edit to undo", document);
    if (document.documentGeneration != historyGeneration_)
        return failed(CommandStatus::StaleAddress, "history belongs to an older document generation", document);

    auto &entry = history_[cursor_ - 1];
    ++suppressHistory_;
    CommandResult result = std::visit([&](const auto &command) {
        using StoredCommand = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<StoredCommand, PropertyCommand>)
            return apply(document, command, CommandDirection::Reverse);
        else if constexpr (std::is_same_v<StoredCommand, StructuralCommand>)
        {
            return command_result(apply(document, command, CommandDirection::Reverse));
        }
        else
            return apply_batch_entry(document, command, CommandDirection::Reverse);
    }, entry.command);
    --suppressHistory_;
    if (result.status == CommandStatus::Changed || result.status == CommandStatus::NoChange)
    {
        --cursor_;
        const SelectionState stored = std::visit([](const auto &command) { return command.selection; }, entry.command);
        selection.set(document, stored.before);
    }
    return result;
}

CommandResult EditCommandService::redo(DocumentSession &document, SelectionModel &selection)
{
    if (cursor_ >= history_.size())
        return failed(CommandStatus::Rejected, "there is no edit to redo", document);
    if (document.documentGeneration != historyGeneration_)
        return failed(CommandStatus::StaleAddress, "history belongs to an older document generation", document);

    auto &entry = history_[cursor_];
    ++suppressHistory_;
    CommandResult result = std::visit([&](const auto &command) {
        using StoredCommand = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<StoredCommand, PropertyCommand>)
            return apply(document, command, CommandDirection::Forward);
        else if constexpr (std::is_same_v<StoredCommand, StructuralCommand>)
        {
            return command_result(apply(document, command, CommandDirection::Forward));
        }
        else
            return apply_batch_entry(document, command, CommandDirection::Forward);
    }, entry.command);
    --suppressHistory_;
    if (result.status == CommandStatus::Changed || result.status == CommandStatus::NoChange)
    {
        ++cursor_;
        const SelectionState stored = std::visit([](const auto &command) { return command.selection; }, entry.command);
        selection.set(document, stored.after);
    }
    return result;
}

void EditCommandService::update_gesture_dirty(PropertyGesture &gesture, bool changed) noexcept
{
    if (gesture.dirtyCounted_ == changed)
        return;
    gesture.dirtyCounted_ = changed;
    if (changed)
        ++activeChangedGestures_;
    else if (activeChangedGestures_ > 0)
        --activeChangedGestures_;
}

void EditCommandService::rollback_gesture(PropertyGesture &gesture) noexcept
{
    if (gesture.document_ == nullptr)
        return;
    if (!address_matches_document(*gesture.document_, gesture.command_.address))
    {
        try
        {
            lastError_ = "gesture rollback skipped because its document identity changed";
        }
        catch (...)
        {
        }
        return;
    }
    const bool restored = restore_gesture_property(*gesture.document_, gesture.command_.address,
                                                   gesture.command_.before);
    gesture.document_->valueRevision = gesture.startingValueRevision_;
    if (!restored)
    {
        try
        {
            lastError_ = "gesture rollback could not resolve its property target";
        }
        catch (...)
        {
        }
        return;
    }
    try
    {
        DocumentChange change{gesture.document_->documentGeneration,
                              gesture.document_->structureRevision,
                              gesture.document_->valueRevision, false};
        change.identity = document_identity(*gesture.document_);
        change.affectedObjectIds.push_back(gesture.command_.address.objectId);
        change.changedPaths.push_back(changed_path(gesture.command_.address));
        for (const auto &observer : observers_)
        {
            try
            {
                observer(change, change.affectedObjectIds);
            }
            catch (...)
            {
            }
        }
    }
    catch (...)
    {
    }
}

bool EditCommandService::finish_gesture(PropertyGesture &gesture, bool commit) noexcept
{
    if (!gesture.active_)
        return false;
    bool retained = true;
    const bool changed = !values_equal(gesture.command_.before, gesture.command_.after);
    if (commit && changed)
    {
        if (gesture.document_ == nullptr ||
            gesture.command_.address.documentGeneration != historyGeneration_ ||
            gesture.document_->documentGeneration != gesture.command_.address.documentGeneration)
        {
            retained = false;
            try
            {
                lastError_ = "cannot retain gesture history for a stale document";
            }
            catch (...)
            {
            }
        }
        else
        {
            try
            {
                auto prepared = prepare_history(gesture.command_);
                commit_history(std::move(prepared));
            }
            catch (...)
            {
                retained = false;
                try
                {
                    lastError_ = "cannot retain gesture history";
                }
                catch (...)
                {
                }
            }
        }
        if (!retained)
            rollback_gesture(gesture);
    }
    if (gesture.dirtyCounted_ && activeChangedGestures_ > 0)
        --activeChangedGestures_;
    if (activeGestures_ > 0)
        --activeGestures_;
    gesture.active_ = false;
    gesture.dirtyCounted_ = false;
    gesture.service_ = nullptr;
    gesture.document_ = nullptr;
    return retained;
}

void EditCommandService::update_batch_gesture_dirty(BatchPropertyGesture &gesture, bool changed) noexcept
{
    if (gesture.dirtyCounted_ == changed)
        return;
    gesture.dirtyCounted_ = changed;
    if (changed)
        ++activeChangedGestures_;
    else if (activeChangedGestures_ > 0)
        --activeChangedGestures_;
}

void EditCommandService::rollback_batch_gesture(BatchPropertyGesture &gesture) noexcept
{
    if (gesture.document_ == nullptr)
        return;
    if (gesture.command_.identity != document_identity(*gesture.document_))
    {
        try
        {
            lastError_ = "batch gesture rollback skipped because its document identity changed";
        }
        catch (...)
        {
        }
        return;
    }
    bool restored = true;
    for (auto operation = gesture.command_.operations.rbegin();
         operation != gesture.command_.operations.rend(); ++operation)
        if (auto *property = std::get_if<PropertyCommand>(&*operation))
            restored = restore_gesture_property(*gesture.document_, property->address, property->before) && restored;
    gesture.document_->valueRevision = gesture.startingValueRevision_;
    if (!restored)
    {
        try
        {
            lastError_ = "batch gesture rollback could not resolve every property target";
        }
        catch (...)
        {
        }
    }
    try
    {
        DocumentChange change{gesture.document_->documentGeneration,
                              gesture.document_->structureRevision,
                              gesture.document_->valueRevision, false};
        change.identity = document_identity(*gesture.document_);
        for (const auto &operation : gesture.command_.operations)
            if (const auto *property = std::get_if<PropertyCommand>(&operation))
            {
                change.affectedObjectIds.push_back(property->address.objectId);
                change.changedPaths.push_back(changed_path(property->address));
            }
        normalize_affected_ids(change.affectedObjectIds);
        for (const auto &observer : observers_)
        {
            try
            {
                observer(change, change.affectedObjectIds);
            }
            catch (...)
            {
            }
        }
    }
    catch (...)
    {
    }
}

bool EditCommandService::finish_batch_gesture(BatchPropertyGesture &gesture, bool commit) noexcept
{
    if (!gesture.active_)
        return false;
    bool retained = true;
    bool changed = false;
    std::uint64_t generation = 0;
    for (const auto &operation : gesture.command_.operations)
        if (const auto *property = std::get_if<PropertyCommand>(&operation))
        {
            generation = property->address.documentGeneration;
            changed = changed || !(property->before == property->after);
        }
    if (commit && changed)
    {
        if (gesture.document_ == nullptr || generation != historyGeneration_ ||
            gesture.document_->documentGeneration != generation ||
            gesture.command_.identity != document_identity(*gesture.document_))
        {
            retained = false;
            try
            {
                lastError_ = "cannot retain batch gesture history for a stale document";
            }
            catch (...)
            {
            }
        }
        else
        {
            try
            {
                auto prepared = prepare_history(gesture.command_);
                commit_history(std::move(prepared));
            }
            catch (...)
            {
                retained = false;
                try
                {
                    lastError_ = "cannot retain batch gesture history";
                }
                catch (...)
                {
                }
            }
        }
        if (!retained)
            rollback_batch_gesture(gesture);
    }
    if (gesture.dirtyCounted_ && activeChangedGestures_ > 0)
        --activeChangedGestures_;
    if (activeGestures_ > 0)
        --activeGestures_;
    gesture.active_ = false;
    gesture.dirtyCounted_ = false;
    gesture.service_ = nullptr;
    gesture.document_ = nullptr;
    return retained;
}

CommandResult EditCommandService::set_property(DocumentSession &document, const PropertyAddress &address,
                                               Json value, SelectionState selection)
{
    if (!address_matches_document(document, address))
        return failed(CommandStatus::StaleAddress, "property address belongs to an older document generation", document);
    ResolveFailure failure;
    if (!canonical_property_path(document, address, failure))
        return failed(failure.status, std::move(failure.message), document);
    auto target = resolve_target(document, address, failure);
    if (!target)
        return failed(failure.status, std::move(failure.message), document);
    PropertyCommand command{address, target->current, PropertyValue::of(std::move(value)), std::move(selection)};
    if (values_equal(command.before, command.after))
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    return apply(document, command, CommandDirection::Forward);
}

CommandResult EditCommandService::erase_property(DocumentSession &document, const PropertyAddress &address,
                                                 SelectionState selection)
{
    if (!address_matches_document(document, address))
        return failed(CommandStatus::StaleAddress, "property address belongs to an older document generation", document);
    ResolveFailure failure;
    if (!canonical_property_path(document, address, failure))
        return failed(failure.status, std::move(failure.message), document);
    auto target = resolve_target(document, address, failure);
    if (!target)
        return failed(failure.status, std::move(failure.message), document);
    PropertyCommand command{address, target->current, PropertyValue::missing(), std::move(selection)};
    if (!command.before.present)
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    return apply(document, command, CommandDirection::Forward);
}

CommandResult EditCommandService::apply(DocumentSession &document, const PropertyCommand &command,
                                       CommandDirection direction)
{
    lastError_.clear();
    if (!address_matches_document(document, command.address))
        return failed(CommandStatus::StaleAddress, "property command belongs to an older document generation", document);
    if (command.address.objectId < 0)
        return failed(CommandStatus::Rejected, "property address has an invalid object ID", document);
    ResolveFailure validationFailure;
    if (!canonical_property_path(document, command.address, validationFailure))
        return failed(validationFailure.status, std::move(validationFailure.message), document);

    const PropertyValue &expected = direction == CommandDirection::Forward ? command.before : command.after;
    const PropertyValue &replacementValue = direction == CommandDirection::Forward ? command.after : command.before;
    if (values_equal(expected, replacementValue))
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    if (document.valueRevision == std::numeric_limits<std::uint64_t>::max())
        return failed(CommandStatus::Rejected, "document value revision space exhausted", document);
    const bool recordHistory = direction == CommandDirection::Forward && suppressHistory_ == 0;
    ResolveFailure failure;
    std::optional<ResolvedTarget<Json>> target;
    PropertyCommand retainedCommand;
    CommandResult result;
    std::vector<Observer> observers;
    Json replacement;
    std::optional<PreparedHistory> preparedHistory;
    try
    {
        target = resolve_target(document, command.address, failure);
    }
    catch (const std::exception &error)
    {
        lastError_ = std::string("cannot resolve property command: ") + error.what();
        return failed(CommandStatus::Rejected, lastError_, document);
    }
    if (!target)
        return failed(failure.status, std::move(failure.message), document);
    if (!values_equal(target->current, expected))
        return failed(CommandStatus::Rejected, "property command precondition no longer matches", document);
    if (!replacementValue.present && !target->finalKey)
        return failed(CommandStatus::Rejected, "array entries cannot be removed by a property command", document);

    // Prepare every allocation, including retained history, before mutating the
    // authored DOM. The eventual history swap cannot fail.
    try
    {
        if (recordHistory)
        {
            retainedCommand = command;
            retainedCommand.selection = capture_selection(std::move(retainedCommand.selection));
        }
        result.status = CommandStatus::Changed;
        result.affectedObjectIds.push_back(command.address.objectId);
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision + 1, false};
        set_property_change_metadata(document, command.address, result.change, result.affectedObjectIds);
        result.propertyCommand = recordHistory ? retainedCommand : command;
        observers = observers_;
        replacement = replacementValue.present ? replacementValue.value : Json{};
        if (recordHistory)
            preparedHistory = prepare_history(std::move(retainedCommand));
    }
    catch (const std::exception &error)
    {
        lastError_ = std::string(recordHistory ? "cannot retain property history: " :
                                                  "cannot prepare property edit: ") + error.what();
        return failed(CommandStatus::Rejected, lastError_, document);
    }

    try
    {
        if (target->finalKey)
        {
            auto found = target->parent->find(target->key);
            if (!replacementValue.present)
                target->parent->erase(target->key);
            else if (found == target->parent->end())
                target->parent->emplace(target->key, std::move(replacement));
            else
                found.value().swap(replacement);
        }
        else
            (*target->parent)[target->index].swap(replacement);
    }
    catch (const std::exception &error)
    {
        if (preparedHistory)
            rollback_history(*preparedHistory);
        lastError_ = std::string("cannot apply property edit: ") + error.what();
        return failed(CommandStatus::Rejected, lastError_, document);
    }

    ++document.valueRevision;
    work::record(work::Counter::ChangedRecords, result.affectedObjectIds.size());
    result.change.valueRevision = document.valueRevision;
    if (preparedHistory)
    {
        commit_history(std::move(*preparedHistory));
        if (historyGeneration_ == 0)
            historyGeneration_ = document.documentGeneration;
    }
    for (const auto &observer : observers)
    {
        try
        {
            observer(result.change, result.affectedObjectIds);
        }
        catch (...)
        {
            // A notification is advisory; committed document state is retained.
        }
    }
    return result;
}

CommandResult EditCommandService::apply_property_batch(DocumentSession &document, BatchCommand &&command,
                                                       CommandDirection direction, bool recordHistory,
                                                       bool deriveBefore)
{
    lastWorkStats_ = {};
    lastError_.clear();
    if (command.identity != document_identity(document))
        return failed(CommandStatus::StaleAddress, "batch belongs to an older tab or project generation", document);
    if (historyGeneration_ != 0 && historyGeneration_ != document.documentGeneration)
        return failed(CommandStatus::StaleAddress, "history belongs to an older document generation", document);
    if (command.operations.empty())
        return failed(CommandStatus::NoChange, "batch contains no changes", document);

    try
    {
        std::vector<PreparedPropertyPatch> patches;
        patches.reserve(command.operations.size());
        std::set<CanonicalJsonPath, CanonicalJsonPathLess> normalizedPaths;
        DocumentChange change{document.documentGeneration, document.structureRevision,
                              document.valueRevision, false};
        change.identity = document_identity(document);
        std::size_t changedCount = 0;

        for (std::size_t index = 0; index < command.operations.size(); ++index)
        {
            auto *property = std::get_if<PropertyCommand>(&command.operations[index]);
            if (property == nullptr)
                return failed(CommandStatus::Rejected, "property batch contains a structural edit", document);
            if (!address_matches_document(document, property->address))
                return failed(CommandStatus::StaleAddress,
                              "property address belongs to an older document generation", document);
            ResolveFailure failure;
            auto normalized = canonical_property_path(document, property->address, failure);
            if (!normalized)
                return failed(failure.status, std::move(failure.message), document);
            const auto next = normalizedPaths.lower_bound(*normalized);
            ++lastWorkStats_.propertyBatchOverlapChecks;
            // Every accepted prefix is unique, so a conflict can only be the
            // first descendant at lower_bound or its immediately preceding ancestor.
            bool overlaps = next != normalizedPaths.end() && path_has_prefix(*next, *normalized);
            if (next != normalizedPaths.begin())
            {
                ++lastWorkStats_.propertyBatchOverlapChecks;
                const auto previous = std::prev(next);
                overlaps = overlaps || path_has_prefix(*normalized, *previous);
            }
            if (overlaps)
                return failed(CommandStatus::Rejected,
                              "property batch contains duplicate or overlapping addresses", document);
            normalizedPaths.insert(std::move(*normalized));

            if (!document.indexes.current_for(document.documentGeneration, document.structureRevision))
                ++lastWorkStats_.propertyBatchIndexRebuildAttempts;
            auto target = resolve_target(document, property->address, failure);
            ++lastWorkStats_.propertyBatchTargetsResolved;
            if (!target)
                return failed(failure.status, std::move(failure.message), document);
            work::record(work::Counter::BatchPropertyTargetsResolved);
            if (document.indexes.current_for(document.documentGeneration, document.structureRevision))
            {
                const auto location = document.indexes.objectsById.find(property->address.objectId);
                if (location != document.indexes.objectsById.end())
                    lastWorkStats_.propertyBatchPathSegmentsResolved +=
                        location->second.childIndices.size() + property->address.fieldPath.size();
            }
            if (deriveBefore)
                property->before = target->current;
            const PropertyValue &expected = direction == CommandDirection::Forward
                                                ? property->before
                                                : property->after;
            const PropertyValue &replacement = direction == CommandDirection::Forward
                                                   ? property->after
                                                   : property->before;
            if (values_equal(expected, replacement))
                continue;
            if (document.valueRevision == std::numeric_limits<std::uint64_t>::max() - changedCount)
                return failed(CommandStatus::Rejected, "document value revision space exhausted", document);

            PreparedPropertyPatch patch;
            patch.address = property->address;
            patch.commandIndex = index;
            CommandStatus status = CommandStatus::Changed;
            std::string message;
            if (!prepare_property_patch(*target, expected, replacement, patch, status, message))
                return failed(status, std::move(message), document);
            patches.push_back(std::move(patch));
            ++lastWorkStats_.propertyBatchPatchesPrepared;
            ++changedCount;

            std::vector<int> operationObjectIds{property->address.objectId};
            DocumentChange operationChange{document.documentGeneration, document.structureRevision,
                                           document.valueRevision + changedCount, false};
            set_property_change_metadata(document, property->address, operationChange, operationObjectIds);
            merge_document_change(change, operationChange);
        }

        if (patches.empty())
        {
            CommandResult result;
            result.status = CommandStatus::NoChange;
            result.change = std::move(change);
            return result;
        }

        if (recordHistory && suppressHistory_ == 0)
        {
            std::vector<BatchOperation> changedOperations;
            changedOperations.reserve(patches.size());
            for (const auto &patch : patches)
                changedOperations.emplace_back(std::move(command.operations[patch.commandIndex]));
            command.operations = std::move(changedOperations);
            command.selection = capture_selection(std::move(command.selection));
        }
        else
            recordHistory = false;

        change.valueRevision = document.valueRevision + changedCount;

        CommandResult result;
        result.status = CommandStatus::Changed;
        result.affectedObjectIds = change.affectedObjectIds;
        result.change = change;
        std::vector<Observer> observers = observers_;
        std::optional<PreparedHistory> preparedHistory;
        if (recordHistory)
            preparedHistory = prepare_history(std::move(command));

        // Json mutations share one owner-thread document. oneTBB would add
        // coordination while invalidating these prepared pointers; serial patch
        // application keeps work proportional to the touched fields.
        try
        {
            for (auto &patch : patches)
                apply_property_patch(patch);
        }
        catch (const std::exception &error)
        {
            rollback_property_patches(patches);
            if (preparedHistory)
                rollback_history(*preparedHistory);
            lastError_ = std::string("cannot apply property batch: ") + error.what();
            return failed(CommandStatus::Rejected, lastError_, document);
        }

        lastWorkStats_.propertyBatchPatchesCommitted = patches.size();
        work::record(work::Counter::BatchPropertyPatchesCommitted, patches.size());
        document.valueRevision += changedCount;
        if (preparedHistory)
        {
            commit_history(std::move(*preparedHistory));
            if (historyGeneration_ == 0)
                historyGeneration_ = document.documentGeneration;
        }
        result.change = std::move(change);
        for (const auto &observer : observers)
        {
            try
            {
                observer(result.change, result.affectedObjectIds);
            }
            catch (...)
            {
                // Batch notifications are advisory after the full patch commits.
            }
        }
        return result;
    }
    catch (const std::exception &error)
    {
        lastError_ = std::string("cannot prepare property batch: ") + error.what();
        return failed(CommandStatus::Rejected, lastError_, document);
    }
}

CommandResult EditCommandService::commit_batch_candidate(DocumentSession &document,
                                                         DocumentSession &&candidate,
                                                         BatchCommand &&command,
                                                         DocumentChange change,
                                                         bool recordHistory)
{
    lastError_.clear();
    if (command.operations.empty())
        return failed(CommandStatus::NoChange, "batch contains no changes", document);
    if (command.identity != document_identity(document))
        return failed(CommandStatus::StaleAddress, "batch belongs to an older tab or project generation", document);
    if (historyGeneration_ != 0 && historyGeneration_ != document.documentGeneration)
        return failed(CommandStatus::StaleAddress, "history belongs to an older document generation", document);
    if (recordHistory && suppressHistory_ == 0 && nextToken_ == std::numeric_limits<std::uint64_t>::max())
        return failed(CommandStatus::Rejected, "history state token space exhausted", document);

    change.identity = document_identity(document);
    change.documentGeneration = document.documentGeneration;
    change.structureRevision = candidate.structureRevision;
    change.valueRevision = candidate.valueRevision;
    CommandResult result;
    result.status = CommandStatus::Changed;
    result.affectedObjectIds = change.affectedObjectIds;
    result.change = change;
    std::vector<Observer> observers;
    try
    {
        observers = observers_;
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected, std::string("cannot prepare batch notifications: ") + error.what(),
                      document);
    }

    if (recordHistory && suppressHistory_ == 0)
    {
        command.selection = capture_selection(std::move(command.selection));
        try
        {
            append_history(std::move(command));
        }
        catch (const std::exception &error)
        {
            lastError_ = std::string("cannot retain batch history: ") + error.what();
            return failed(CommandStatus::Rejected, lastError_, document);
        }
        if (historyGeneration_ == 0)
            historyGeneration_ = document.documentGeneration;
    }

    document.root.swap(candidate.root);
    document.indexes = std::move(candidate.indexes);
    document.structureRevision = candidate.structureRevision;
    document.valueRevision = candidate.valueRevision;
    result.change = std::move(change);
    for (const auto &observer : observers)
    {
        try
        {
            observer(result.change, result.affectedObjectIds);
        }
        catch (...)
        {
            // A notification is advisory after the complete transaction commits.
        }
    }
    return result;
}

CommandResult EditCommandService::apply_batch(DocumentSession &document,
                                              const std::vector<BatchPropertyEdit> &edits,
                                              SelectionState selection)
{
    if (edits.empty())
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }

    try
    {
        BatchCommand batch;
        batch.operations.reserve(edits.size());
        batch.identity = document_identity(document);
        batch.selection = std::move(selection);
        for (const auto &edit : edits)
            batch.operations.emplace_back(PropertyCommand{edit.address, PropertyValue::missing(), edit.value,
                                                           batch.selection});
        return apply_property_batch(document, std::move(batch), CommandDirection::Forward, true, true);
    }
    catch (const std::exception &error)
    {
        lastError_ = std::string("cannot prepare property batch: ") + error.what();
        return failed(CommandStatus::Rejected, lastError_, document);
    }
}

CommandResult EditCommandService::apply_batch(DocumentSession &document,
                                              const std::vector<BatchOperation> &operations,
                                              SelectionState selection)
{
    if (operations.empty())
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }

    if (std::all_of(operations.begin(), operations.end(), [](const BatchOperation &operation) {
            return std::holds_alternative<PropertyCommand>(operation);
        }))
    {
        try
        {
            BatchCommand batch;
            batch.operations = operations;
            batch.identity = document_identity(document);
            batch.selection = std::move(selection);
            return apply_property_batch(document, std::move(batch), CommandDirection::Forward, true);
        }
        catch (const std::exception &error)
        {
            lastError_ = std::string("cannot prepare property batch: ") + error.what();
            return failed(CommandStatus::Rejected, lastError_, document);
        }
    }

    try
    {
        DocumentSession candidate = document;
        rebuild_document_indexes(candidate);
        BatchCommand batch;
        batch.operations.reserve(operations.size());
        batch.identity = document_identity(document);
        batch.selection = std::move(selection);
        DocumentChange change{document.documentGeneration, document.structureRevision,
                              document.valueRevision, false};
        change.identity = document_identity(document);
        EditCommandService worker(historyBudgetBytes_);
        worker.suppressHistory_ = 1;

        for (const auto &operation : operations)
        {
            if (const auto *property = std::get_if<PropertyCommand>(&operation))
            {
                auto applied = worker.apply(candidate, *property, CommandDirection::Forward);
                if (applied.status != CommandStatus::Changed && applied.status != CommandStatus::NoChange)
                    return failed(applied.status, std::move(applied.message), document);
                if (applied.status == CommandStatus::Changed)
                {
                    batch.operations.emplace_back(*applied.propertyCommand);
                    merge_document_change(change, applied.change);
                }
            }
            else
            {
                const auto &structural = std::get<StructuralCommand>(operation);
                auto applied = worker.apply(candidate, structural, CommandDirection::Forward);
                if (applied.status != CommandStatus::Changed && applied.status != CommandStatus::NoChange)
                    return failed(applied.status, std::move(applied.message), document);
                if (applied.status == CommandStatus::Changed)
                {
                    batch.operations.emplace_back(*applied.command);
                    merge_document_change(change, applied.change);
                }
            }
        }
        if (batch.operations.empty())
        {
            CommandResult result;
            result.status = CommandStatus::NoChange;
            result.change = std::move(change);
            return result;
        }
        normalize_batch_structure_revision(document, candidate, change.structural);
        return commit_batch_candidate(document, std::move(candidate), std::move(batch),
                                      std::move(change), true);
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected, std::string("cannot prepare command batch: ") + error.what(),
                      document);
    }
}

CommandResult EditCommandService::apply_batch_entry(DocumentSession &document, const BatchCommand &command,
                                                    CommandDirection direction)
{
    if (command.identity != document_identity(document))
        return failed(CommandStatus::StaleAddress, "batch history belongs to an older tab or project generation",
                      document);
    if (command.operations.empty())
        return failed(CommandStatus::Rejected, "batch history contains no operations", document);

    if (std::all_of(command.operations.begin(), command.operations.end(), [](const BatchOperation &operation) {
            return std::holds_alternative<PropertyCommand>(operation);
        }))
    {
        try
        {
            BatchCommand replay = command;
            return apply_property_batch(document, std::move(replay), direction, false);
        }
        catch (const std::exception &error)
        {
            lastError_ = std::string("cannot prepare property replay: ") + error.what();
            return failed(CommandStatus::Rejected, lastError_, document);
        }
    }

    try
    {
        DocumentSession candidate = document;
        rebuild_document_indexes(candidate);
        DocumentChange change{document.documentGeneration, document.structureRevision,
                              document.valueRevision, false};
        change.identity = document_identity(document);
        EditCommandService worker(historyBudgetBytes_);
        worker.suppressHistory_ = 1;

        auto apply_operation = [&](const BatchOperation &operation) -> CommandResult {
            if (const auto *property = std::get_if<PropertyCommand>(&operation))
                return worker.apply(candidate, *property, direction);
            return command_result(worker.apply(candidate, std::get<StructuralCommand>(operation), direction));
        };
        bool changed = false;
        auto apply_and_merge = [&](const BatchOperation &operation) -> std::optional<CommandResult> {
            CommandResult applied = apply_operation(operation);
            if (applied.status != CommandStatus::Changed && applied.status != CommandStatus::NoChange)
                return applied;
            if (applied.status == CommandStatus::Changed)
            {
                changed = true;
                merge_document_change(change, applied.change);
            }
            return std::nullopt;
        };

        if (direction == CommandDirection::Forward)
        {
            for (const auto &operation : command.operations)
                if (auto failure = apply_and_merge(operation))
                    return failed(failure->status, std::move(failure->message), document);
        }
        else
        {
            for (auto operation = command.operations.rbegin(); operation != command.operations.rend(); ++operation)
                if (auto failure = apply_and_merge(*operation))
                    return failed(failure->status, std::move(failure->message), document);
        }
        if (!changed)
        {
            CommandResult result;
            result.status = CommandStatus::NoChange;
            result.change = std::move(change);
            return result;
        }
        normalize_batch_structure_revision(document, candidate, change.structural);
        BatchCommand retained = command;
        return commit_batch_candidate(document, std::move(candidate), std::move(retained),
                                      std::move(change), false);
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected, std::string("cannot replay command batch: ") + error.what(),
                      document);
    }
}

StructuralCommandResult EditCommandService::apply(DocumentSession &document, const StructuralCommand &command,
                                                   CommandDirection direction)
{
    lastError_.clear();
    const bool insertKind = command.kind == StructuralCommandKind::InsertObject ||
                            command.kind == StructuralCommandKind::InsertComponent ||
                            command.kind == StructuralCommandKind::InsertBehavior;
    const bool deleteKind = command.kind == StructuralCommandKind::DeleteObject ||
                            command.kind == StructuralCommandKind::DeleteComponent ||
                            command.kind == StructuralCommandKind::DeleteBehavior;
    if (!insertKind && !deleteKind)
        return failed_structural(CommandStatus::Rejected, "unknown structural command kind", document);
    const bool inserting = (direction == CommandDirection::Forward) == insertKind;
    const bool recordHistory = direction == CommandDirection::Forward && suppressHistory_ == 0;
    StructuralCommand retainedCommand;
    std::vector<Observer> observers;
    std::optional<PreparedHistory> preparedHistory;
    try
    {
        retainedCommand = command;
    }
    catch (const std::exception &error)
    {
        lastError_ = std::string("cannot prepare structural command: ") + error.what();
        return failed_structural(CommandStatus::Rejected, lastError_, document);
    }
    if (document.tabId)
    {
        const bool scoped = static_cast<bool>(retainedCommand.identity.projectGeneration) ||
                            static_cast<bool>(retainedCommand.identity.tabId) ||
                            static_cast<bool>(retainedCommand.identity.documentGeneration);
        if (scoped && retainedCommand.identity != document_identity(document))
            return failed_structural(CommandStatus::StaleAddress,
                                     "structural command belongs to an older tab or project generation", document);
        if (!scoped)
            retainedCommand.identity = document_identity(document);
    }
    try
    {
        if (recordHistory)
        {
            retainedCommand.selection = capture_selection(std::move(retainedCommand.selection));
            preparedHistory = prepare_history(
                std::variant<PropertyCommand, StructuralCommand, BatchCommand>(retainedCommand));
        }
        observers = observers_;
    }
    catch (const std::exception &error)
    {
        lastError_ = std::string(recordHistory ? "cannot retain structural history: " :
                                                  "cannot prepare structural notifications: ") + error.what();
        return failed_structural(CommandStatus::Rejected, lastError_, document);
    }
    StructuralCommandResult result;
    try
    {
        result = commit_structural(document, std::move(retainedCommand), inserting);
    }
    catch (const std::exception &error)
    {
        if (preparedHistory)
            rollback_history(*preparedHistory);
        lastError_ = std::string("cannot commit structural edit: ") + error.what();
        return failed_structural(CommandStatus::Rejected, lastError_, document);
    }
    if (result.status == CommandStatus::Changed)
    {
        if (preparedHistory)
        {
            commit_history(std::move(*preparedHistory));
            if (historyGeneration_ == 0)
                historyGeneration_ = document.documentGeneration;
        }
        for (const auto &observer : observers)
        {
            try
            {
                observer(result.change, result.affectedObjectIds);
            }
            catch (...)
            {
                // Notifications are advisory after a committed structural edit.
            }
        }
    }
    else if (preparedHistory)
        rollback_history(*preparedHistory);
    return result;
}

StructuralCommandResult EditCommandService::create_object(DocumentSession &document,
                                                           std::optional<int> parentObjectId,
                                                           std::string name, std::size_t insertionIndex,
                                                           SelectionState selection,
                                                           Json components)
{
    rebuild_document_indexes(document);
    if (parentObjectId && !document.indexes.objectsById.contains(*parentObjectId))
        return failed_structural(CommandStatus::StaleAddress, "parent object is no longer present", document);
    Json *siblings = mutable_siblings(document, parentObjectId);
    if (siblings == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object insertion parent is unavailable", document);
    if (insertionIndex == std::numeric_limits<std::size_t>::max())
        insertionIndex = siblings->size();
    if (insertionIndex > siblings->size())
        return failed_structural(CommandStatus::Rejected, "object insertion index is out of range", document);
    const auto id = first_unused_id(document);
    if (!id)
        return failed_structural(CommandStatus::Rejected, "no file IDs remain for a new object", document);

    if (components.is_array())
    {
        int nextId = *id + 1;
        for (auto &comp : components)
        {
            if (comp.is_object() && (!comp.contains("id") || comp["id"].get<int>() <= 0))
            {
                while (document.indexes.objectsById.contains(nextId) ||
                       document.indexes.componentsById.contains(nextId))
                    ++nextId;
                comp["id"] = nextId++;
            }
        }
    }
    else
    {
        components = Json::array();
    }

    Json object = {{"id", *id},
                   {"name", std::move(name)},
                   {"tag", ""},
                   {"active", true},
                   {"position", {{"x", 0.0}, {"y", 0.0}}},
                   {"scale", {{"x", 1.0}, {"y", 1.0}}},
                   {"rotation", 0.0},
                   {"children", Json::array()},
                   {"components", std::move(components)}};
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertObject,
                              parentObjectId, *id, std::nullopt, insertionIndex, std::move(object),
                              std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::duplicate_object(DocumentSession &document, int objectId,
                                                              SelectionState selection,
                                                              DuplicateReferencePolicy referencePolicy,
                                                              const Json &typedReferenceBindings,
                                                              bool bindingCoverageAssured)
{
    rebuild_document_indexes(document);
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return failed_structural(CommandStatus::StaleAddress, "object to duplicate is no longer present", document);
    Json *source = resolve_object_at(document.root, location->second);
    if (source == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object location is no longer valid", document);
    if (referencePolicy == DuplicateReferencePolicy::RemapInternalTargets &&
        (!bindingCoverageAssured || !typedReferenceBindings.is_array()))
        return failed_structural(CommandStatus::Rejected,
            "internal reference remapping requires complete typed binding coverage", document);
    Json duplicate = *source;
    const auto parentEntry = document.indexes.parentById.find(objectId);
    if (parentEntry == document.indexes.parentById.end())
        return failed_structural(CommandStatus::StaleAddress, "object parent index is unavailable", document);
    Json *siblings = mutable_siblings(document, parentEntry->second);
    if (siblings == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object sibling list is unavailable", document);
    const std::size_t sourceIndex = location->second.childIndices.back();
    if (sourceIndex >= siblings->size())
        return failed_structural(CommandStatus::StaleAddress, "object sibling index is no longer valid", document);
    const auto firstId = first_unused_id(document);
    if (!firstId)
        return failed_structural(CommandStatus::Rejected, "no file IDs remain for duplicated subtree", document);

    std::vector<int> assigned;
    std::unordered_map<int, int> sourceToDuplicate;
    try
    {
        remap_subtree_ids(duplicate, *firstId, assigned, &sourceToDuplicate);
    }
    catch (const std::exception &error)
    {
        return failed_structural(CommandStatus::Rejected, error.what(), document);
    }
    if (referencePolicy == DuplicateReferencePolicy::RemapInternalTargets)
    {
        std::string error;
        const std::vector<Json *> duplicateRoots{&duplicate};
        if (!remap_duplicate_reference_sites(document, duplicateRoots, sourceToDuplicate,
                                             typedReferenceBindings, error))
            return failed_structural(CommandStatus::Rejected, std::move(error), document);
    }

    const std::string sourceName = source->value("name", std::string("GameObject"));
    std::string copiedName = sourceName + " Copy";
    auto nameTaken = [&](const std::string &candidate) {
        return std::any_of(siblings->begin(), siblings->end(), [&](const Json &sibling) {
            return sibling.is_object() && sibling.value("name", std::string{}) == candidate;
        });
    };
    for (std::size_t suffix = 2; nameTaken(copiedName); ++suffix)
        copiedName = sourceName + " Copy " + std::to_string(suffix);
    duplicate["name"] = std::move(copiedName);
    auto position = duplicate.find("position");
    if (position == duplicate.end() || !position->is_object() ||
        !position->contains("x") || !position->contains("y") ||
        !position->at("x").is_number() || !position->at("y").is_number())
        return failed_structural(CommandStatus::Rejected, "object position cannot be offset for duplication", document);
    const double x = position->at("x").get<double>() + 32.0;
    const double y = position->at("y").get<double>() + 32.0;
    if (!std::isfinite(x) || !std::isfinite(y))
        return failed_structural(CommandStatus::Rejected, "duplicated object position is outside the finite range", document);
    (*position)["x"] = x;
    (*position)["y"] = y;

    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertObject,
                              parentEntry->second, objectId, std::nullopt, sourceIndex + 1,
                              std::move(duplicate), std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

CommandResult EditCommandService::duplicate_objects(DocumentSession &document,
                                                      const std::vector<int> &objectIds,
                                                      SelectionState selection,
                                                      DuplicateReferencePolicy referencePolicy,
                                                      const Json &typedReferenceBindings,
                                                      bool bindingCoverageAssured)
{
    try
    {
        rebuild_document_indexes(document);
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected,
                      std::string("cannot prepare hierarchy duplicate indexes: ") + error.what(), document);
    }
    if (objectIds.empty())
        return failed(CommandStatus::Rejected, "a hierarchy duplicate requires at least one object", document);
    std::set<int> uniqueIds;
    for (const int id : objectIds)
    {
        if (!uniqueIds.insert(id).second)
            return failed(CommandStatus::Rejected, "a hierarchy duplicate cannot contain duplicate object IDs", document);
        if (!document.indexes.objectsById.contains(id))
            return failed(CommandStatus::StaleAddress, "an object to duplicate is no longer present", document);
    }

    Selection requestedSelection;
    requestedSelection.selectedObjectIds = objectIds;
    requestedSelection.objectId = objectIds.back();
    const auto roots = selected_top_level_roots(document, requestedSelection);
    if (roots.empty())
        return failed(CommandStatus::Rejected, "hierarchy duplicate contains no top-level objects", document);
    if (referencePolicy == DuplicateReferencePolicy::RemapInternalTargets &&
        (!bindingCoverageAssured || !typedReferenceBindings.is_array()))
        return failed(CommandStatus::Rejected,
            "internal reference remapping requires complete typed binding coverage", document);

    const auto firstId = first_unused_id(document);
    if (!firstId)
        return failed(CommandStatus::Rejected, "no file IDs remain for selected subtree duplicates", document);

    std::vector<int> duplicatedRootIds;
    std::vector<BatchOperation> operations;
    duplicatedRootIds.reserve(roots.size());
    operations.reserve(roots.size());
    std::vector<int> assignedIds;
    std::unordered_map<int, int> sourceToDuplicate;
    std::map<std::optional<int>, std::set<std::string>> namesByParent;
    std::map<std::optional<int>, std::vector<std::size_t>> priorDuplicateIndexes;
    const auto identity = document_identity(document);
    for (const int id : roots)
    {
        const auto location = document.indexes.objectsById.find(id);
        const auto parent = document.indexes.parentById.find(id);
        if (location == document.indexes.objectsById.end() || parent == document.indexes.parentById.end() ||
            location->second.childIndices.empty())
            return failed(CommandStatus::StaleAddress, "an object placement to duplicate is stale", document);
        Json *sourceSiblings = mutable_siblings(document, parent->second);
        const std::size_t sourceIndex = location->second.childIndices.back();
        if (sourceSiblings == nullptr || sourceIndex >= sourceSiblings->size() ||
            (*sourceSiblings)[sourceIndex].value("id", -1) != id)
            return failed(CommandStatus::StaleAddress, "an object source placement is unavailable", document);

        Json duplicate = (*sourceSiblings)[sourceIndex];
        try
        {
            remap_subtree_ids(duplicate, *firstId, assignedIds, &sourceToDuplicate);
        }
        catch (const std::exception &error)
        {
            return failed(CommandStatus::Rejected, error.what(), document);
        }

        auto &siblingNames = namesByParent[parent->second];
        if (siblingNames.empty())
            for (const auto &sibling : *sourceSiblings)
                if (sibling.is_object())
                    siblingNames.insert(sibling.value("name", std::string{}));
        const std::string sourceName = (*sourceSiblings)[sourceIndex].value(
            "name", std::string("GameObject"));
        std::string copiedName = sourceName + " Copy";
        for (std::size_t suffix = 2; siblingNames.contains(copiedName); ++suffix)
            copiedName = sourceName + " Copy " + std::to_string(suffix);
        siblingNames.insert(copiedName);
        duplicate["name"] = std::move(copiedName);

        auto position = duplicate.find("position");
        if (position == duplicate.end() || !position->is_object() || !position->contains("x") ||
            !position->contains("y") || !position->at("x").is_number() ||
            !position->at("y").is_number())
            return failed(CommandStatus::Rejected,
                          "object position cannot be offset for duplication", document);
        const double x = position->at("x").get<double>() + 32.0;
        const double y = position->at("y").get<double>() + 32.0;
        if (!std::isfinite(x) || !std::isfinite(y))
            return failed(CommandStatus::Rejected,
                          "duplicated object position is outside the finite range", document);
        (*position)["x"] = x;
        (*position)["y"] = y;

        const auto &priorIndexes = priorDuplicateIndexes[parent->second];
        const auto priorBefore = static_cast<std::size_t>(std::count_if(
            priorIndexes.begin(), priorIndexes.end(), [&](std::size_t index) { return index <= sourceIndex; }));
        const std::size_t insertion = sourceIndex + priorBefore + 1;
        priorDuplicateIndexes[parent->second].push_back(sourceIndex);
        const int duplicatedId = duplicate.value("id", 0);
        if (duplicatedId <= 0)
            return failed(CommandStatus::Rejected, "duplicate subtree did not receive a valid root ID", document);
        duplicatedRootIds.push_back(duplicatedId);
        StructuralCommand insert{document.documentGeneration, StructuralCommandKind::InsertObject,
            parent->second, id, std::nullopt, insertion, std::move(duplicate), selection};
        insert.identity = identity;
        operations.emplace_back(std::move(insert));
    }

    if (referencePolicy == DuplicateReferencePolicy::RemapInternalTargets)
    {
        std::vector<Json *> duplicatePayloads;
        duplicatePayloads.reserve(operations.size());
        for (auto &operation : operations)
        {
            auto *structural = std::get_if<StructuralCommand>(&operation);
            if (structural == nullptr)
                return failed(CommandStatus::Rejected,
                              "multi-root duplicate produced a non-structural operation", document);
            duplicatePayloads.push_back(&structural->payload);
        }
        std::string error;
        if (!remap_duplicate_reference_sites(document, duplicatePayloads, sourceToDuplicate,
                                             typedReferenceBindings, error))
            return failed(CommandStatus::Rejected, std::move(error), document);
    }

    Selection after = selection.after;
    after.selectedObjectIds = duplicatedRootIds;
    after.objectId = duplicatedRootIds.back();
    after.componentId.reset();
    after.behaviorOrdinal.reset();
    after.rangeAnchorObjectId = duplicatedRootIds.front();
    selection.after = std::move(after);
    auto result = apply_batch(document, operations, std::move(selection));
    if (result.status == CommandStatus::Changed)
        result.affectedObjectIds = duplicatedRootIds;
    return result;
}

CommandResult EditCommandService::move_object(DocumentSession &document, int objectId,
                                               std::optional<int> destinationParentObjectId,
                                               std::size_t insertionIndex,
                                               SelectionState selection)
{
    try
    {
        rebuild_document_indexes(document);
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected,
                      std::string("cannot prepare hierarchy move indexes: ") + error.what(), document);
    }
    const auto sourceLocation = document.indexes.objectsById.find(objectId);
    const auto sourceParent = document.indexes.parentById.find(objectId);
    if (sourceLocation == document.indexes.objectsById.end() ||
        sourceParent == document.indexes.parentById.end() || sourceLocation->second.childIndices.empty())
        return failed(CommandStatus::StaleAddress, "object to move is no longer present", document);
    if (destinationParentObjectId &&
        !document.indexes.objectsById.contains(*destinationParentObjectId))
        return failed(CommandStatus::StaleAddress, "destination parent is no longer present", document);

    for (auto ancestor = destinationParentObjectId; ancestor;)
    {
        if (*ancestor == objectId)
            return failed(CommandStatus::Rejected, "an object cannot be moved beneath itself or its descendant",
                          document);
        const auto parent = document.indexes.parentById.find(*ancestor);
        ancestor = parent == document.indexes.parentById.end() ? std::nullopt : parent->second;
    }

    Json *sourceSiblings = mutable_siblings(document, sourceParent->second);
    Json *destinationSiblings = mutable_siblings(document, destinationParentObjectId);
    const std::size_t sourceIndex = sourceLocation->second.childIndices.back();
    if (sourceSiblings == nullptr || destinationSiblings == nullptr ||
        sourceIndex >= sourceSiblings->size() || (*sourceSiblings)[sourceIndex].value("id", -1) != objectId)
        return failed(CommandStatus::StaleAddress, "object source or destination placement is unavailable", document);

    const bool sameParent = sourceParent->second == destinationParentObjectId;
    const std::size_t requestedIndex = insertionIndex == std::numeric_limits<std::size_t>::max()
        ? destinationSiblings->size() : insertionIndex;
    if (requestedIndex > destinationSiblings->size())
        return failed(CommandStatus::Rejected, "destination sibling index is out of range", document);
    const std::size_t destinationIndex = sameParent && requestedIndex > sourceIndex
        ? requestedIndex - 1 : requestedIndex;
    const std::size_t destinationSizeAfterRemoval = destinationSiblings->size() - (sameParent ? 1U : 0U);
    if (destinationIndex > destinationSizeAfterRemoval)
        return failed(CommandStatus::Rejected, "normalized destination sibling index is out of range", document);
    if (sameParent && destinationIndex == sourceIndex)
    {
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        result.change.identity = document_identity(document);
        return result;
    }

    auto world_translation = [&](int startId) -> std::optional<std::pair<double, double>> {
        double worldX = 0.0;
        double worldY = 0.0;
        int currentId = startId;
        for (std::size_t depth = 0; depth <= document.indexes.objectsById.size(); ++depth)
        {
            const auto location = document.indexes.objectsById.find(currentId);
            if (location == document.indexes.objectsById.end())
                return std::nullopt;
            const Json *object = resolve_object_at(document.root, location->second);
            if (object == nullptr)
                return std::nullopt;
            const auto position = object->find("position");
            if (position == object->end() || !position->is_object())
                return std::nullopt;
            const auto x = position->find("x");
            const auto y = position->find("y");
            if (x == position->end() || y == position->end() || !x->is_number() || !y->is_number())
                return std::nullopt;
            const double localX = x->get<double>();
            const double localY = y->get<double>();
            if (!std::isfinite(localX) || !std::isfinite(localY))
                return std::nullopt;
            worldX += localX;
            worldY += localY;
            if (!std::isfinite(worldX) || !std::isfinite(worldY))
                return std::nullopt;
            const auto parent = document.indexes.parentById.find(currentId);
            if (parent == document.indexes.parentById.end())
                return std::nullopt;
            if (!parent->second)
                return std::pair{worldX, worldY};
            currentId = *parent->second;
        }
        return std::nullopt;
    };

    Json beforeObject;
    Json afterObject;
    try
    {
        beforeObject = (*sourceSiblings)[sourceIndex];
        afterObject = beforeObject;
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected,
                      std::string("cannot retain hierarchy move payload: ") + error.what(), document);
    }
    if (!sameParent)
    {
        const auto oldWorld = world_translation(objectId);
        const auto destinationWorld = destinationParentObjectId
            ? world_translation(*destinationParentObjectId)
            : std::optional<std::pair<double, double>>{std::pair{0.0, 0.0}};
        if (!oldWorld || !destinationWorld)
            return failed(CommandStatus::Rejected,
                          "object and destination parent require finite local positions", document);
        const double localX = oldWorld->first - destinationWorld->first;
        const double localY = oldWorld->second - destinationWorld->second;
        if (!std::isfinite(localX) || !std::isfinite(localY))
            return failed(CommandStatus::Rejected, "destination local position is outside the finite range",
                          document);
        auto position = afterObject.find("position");
        if (position == afterObject.end() || !position->is_object())
            return failed(CommandStatus::Rejected, "object position is malformed", document);
        (*position)["x"] = localX;
        (*position)["y"] = localY;
    }

    MoveOperation move;
    move.identity = document_identity(document);
    move.objectId = objectId;
    move.beforeParentObjectId = sourceParent->second;
    move.afterParentObjectId = destinationParentObjectId;
    move.beforeIndex = sourceIndex;
    move.afterIndex = destinationIndex;
    move.beforePayload = std::move(beforeObject);
    move.afterPayload = std::move(afterObject);
    StructuralCommand remove{document.documentGeneration, StructuralCommandKind::DeleteObject,
        move.beforeParentObjectId, objectId, std::nullopt, move.beforeIndex,
        std::move(move.beforePayload), selection};
    remove.identity = move.identity;
    StructuralCommand insert{document.documentGeneration, StructuralCommandKind::InsertObject,
        move.afterParentObjectId, objectId, std::nullopt, move.afterIndex,
        std::move(move.afterPayload), selection};
    insert.identity = move.identity;
    std::vector<BatchOperation> operations;
    operations.reserve(2);
    operations.emplace_back(std::move(remove));
    operations.emplace_back(std::move(insert));
    return apply_batch(document, operations, std::move(selection));
}

CommandResult EditCommandService::move_objects(DocumentSession &document,
                                                const std::vector<int> &objectIds,
                                                std::optional<int> destinationParentObjectId,
                                                std::size_t insertionIndex,
                                                SelectionState selection)
{
    try
    {
        rebuild_document_indexes(document);
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected,
                      std::string("cannot prepare hierarchy move indexes: ") + error.what(), document);
    }
    if (objectIds.empty())
        return failed(CommandStatus::Rejected, "a hierarchy block move requires at least one object", document);
    if (destinationParentObjectId &&
        !document.indexes.objectsById.contains(*destinationParentObjectId))
        return failed(CommandStatus::StaleAddress, "destination parent is no longer present", document);

    std::set<int> uniqueIds;
    for (const int id : objectIds)
    {
        if (!uniqueIds.insert(id).second)
            return failed(CommandStatus::Rejected, "a hierarchy move cannot contain duplicate object IDs", document);
        if (!document.indexes.objectsById.contains(id) || !document.indexes.parentById.contains(id))
            return failed(CommandStatus::StaleAddress, "an object in the hierarchy move is no longer present", document);
    }

    Selection requestedSelection;
    requestedSelection.selectedObjectIds = objectIds;
    requestedSelection.objectId = objectIds.back();
    std::vector<int> orderedRoots = selected_top_level_roots(document, requestedSelection);
    if (orderedRoots.empty())
        return failed(CommandStatus::Rejected, "hierarchy move contains no top-level objects", document);

    for (auto ancestor = destinationParentObjectId; ancestor;)
    {
        if (uniqueIds.contains(*ancestor) &&
            std::find(orderedRoots.begin(), orderedRoots.end(), *ancestor) != orderedRoots.end())
            return failed(CommandStatus::Rejected,
                          "an object cannot be moved beneath itself or its descendant", document);
        const auto parent = document.indexes.parentById.find(*ancestor);
        ancestor = parent == document.indexes.parentById.end() ? std::nullopt : parent->second;
    }

    struct MovingObject
    {
        int id = 0;
        std::optional<int> parent;
        std::size_t index = 0;
        Json before;
        Json after;
    };
    std::vector<MovingObject> moving;
    moving.reserve(orderedRoots.size());
    for (const int id : orderedRoots)
    {
        const auto location = document.indexes.objectsById.find(id);
        const auto parent = document.indexes.parentById.find(id);
        if (location == document.indexes.objectsById.end() || parent == document.indexes.parentById.end() ||
            location->second.childIndices.empty())
            return failed(CommandStatus::StaleAddress, "an object placement in the hierarchy move is stale", document);
        Json *siblings = mutable_siblings(document, parent->second);
        const std::size_t index = location->second.childIndices.back();
        if (siblings == nullptr || index >= siblings->size() || (*siblings)[index].value("id", -1) != id)
            return failed(CommandStatus::StaleAddress, "an object source placement is unavailable", document);
        moving.push_back({id, parent->second, index, (*siblings)[index], (*siblings)[index]});
    }

    Json *destinationSiblings = mutable_siblings(document, destinationParentObjectId);
    if (destinationSiblings == nullptr)
        return failed(CommandStatus::StaleAddress, "destination sibling collection is unavailable", document);
    const std::size_t requestedIndex = insertionIndex == std::numeric_limits<std::size_t>::max()
        ? destinationSiblings->size() : insertionIndex;
    if (requestedIndex > destinationSiblings->size())
        return failed(CommandStatus::Rejected, "destination sibling index is out of range", document);

    std::size_t selectedDestinationBeforeIndex = 0;
    std::size_t selectedDestinationCount = 0;
    for (const auto &entry : moving)
    {
        if (entry.parent != destinationParentObjectId)
            continue;
        ++selectedDestinationCount;
        if (entry.index < requestedIndex)
            ++selectedDestinationBeforeIndex;
    }
    const std::size_t destinationIndex = requestedIndex - selectedDestinationBeforeIndex;
    const std::size_t destinationSizeAfterRemoval = destinationSiblings->size() - selectedDestinationCount;
    if (destinationIndex > destinationSizeAfterRemoval)
        return failed(CommandStatus::Rejected, "normalized destination sibling index is out of range", document);

    if (selectedDestinationCount == moving.size())
    {
        std::vector<std::size_t> orderedIndexes;
        orderedIndexes.reserve(moving.size());
        for (const auto &entry : moving)
            orderedIndexes.push_back(entry.index);
        bool alreadyOrdered = std::is_sorted(orderedIndexes.begin(), orderedIndexes.end());
        for (std::size_t index = 1; alreadyOrdered && index < orderedIndexes.size(); ++index)
            alreadyOrdered = orderedIndexes[index] == orderedIndexes[index - 1] + 1;
        if (alreadyOrdered && destinationIndex == orderedIndexes.front())
        {
            CommandResult result;
            result.status = CommandStatus::NoChange;
            result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                           document.valueRevision, false};
            result.change.identity = document_identity(document);
            return result;
        }
    }

    auto world_translation = [&](int startId) -> std::optional<std::pair<double, double>> {
        double worldX = 0.0;
        double worldY = 0.0;
        int currentId = startId;
        for (std::size_t depth = 0; depth <= document.indexes.objectsById.size(); ++depth)
        {
            const auto location = document.indexes.objectsById.find(currentId);
            if (location == document.indexes.objectsById.end())
                return std::nullopt;
            const Json *object = resolve_object_at(document.root, location->second);
            if (object == nullptr)
                return std::nullopt;
            const auto position = object->find("position");
            if (position == object->end() || !position->is_object())
                return std::nullopt;
            const auto x = position->find("x");
            const auto y = position->find("y");
            if (x == position->end() || y == position->end() || !x->is_number() || !y->is_number())
                return std::nullopt;
            const double localX = x->get<double>();
            const double localY = y->get<double>();
            if (!std::isfinite(localX) || !std::isfinite(localY))
                return std::nullopt;
            worldX += localX;
            worldY += localY;
            if (!std::isfinite(worldX) || !std::isfinite(worldY))
                return std::nullopt;
            const auto parent = document.indexes.parentById.find(currentId);
            if (parent == document.indexes.parentById.end())
                return std::nullopt;
            if (!parent->second)
                return std::pair{worldX, worldY};
            currentId = *parent->second;
        }
        return std::nullopt;
    };

    std::optional<std::pair<double, double>> destinationWorld = std::pair{0.0, 0.0};
    if (destinationParentObjectId)
        destinationWorld = world_translation(*destinationParentObjectId);
    for (auto &entry : moving)
    {
        if (entry.parent == destinationParentObjectId)
            continue;
        const auto oldWorld = world_translation(entry.id);
        if (!oldWorld || !destinationWorld)
            return failed(CommandStatus::Rejected,
                          "moved objects and destination parent require finite local positions", document);
        const double localX = oldWorld->first - destinationWorld->first;
        const double localY = oldWorld->second - destinationWorld->second;
        if (!std::isfinite(localX) || !std::isfinite(localY))
            return failed(CommandStatus::Rejected,
                          "destination local position is outside the finite range", document);
        auto position = entry.after.find("position");
        if (position == entry.after.end() || !position->is_object())
            return failed(CommandStatus::Rejected, "moved object position is malformed", document);
        (*position)["x"] = localX;
        (*position)["y"] = localY;
    }

    const DocumentIdentity identity = document_identity(document);
    std::vector<const MovingObject *> removals;
    removals.reserve(moving.size());
    for (const auto &entry : moving)
        removals.push_back(&entry);
    std::sort(removals.begin(), removals.end(), [](const MovingObject *left, const MovingObject *right) {
        if (left->parent != right->parent)
            return left->parent < right->parent;
        return left->index > right->index;
    });

    std::vector<BatchOperation> operations;
    operations.reserve(moving.size() * 2);
    for (const MovingObject *entry : removals)
    {
        StructuralCommand remove{document.documentGeneration, StructuralCommandKind::DeleteObject,
            entry->parent, entry->id, std::nullopt, entry->index, entry->before, selection};
        remove.identity = identity;
        operations.emplace_back(std::move(remove));
    }
    for (std::size_t index = 0; index < moving.size(); ++index)
    {
        StructuralCommand insert{document.documentGeneration, StructuralCommandKind::InsertObject,
            destinationParentObjectId, moving[index].id, std::nullopt, destinationIndex + index,
            moving[index].after, selection};
        insert.identity = identity;
        operations.emplace_back(std::move(insert));
    }
    return apply_batch(document, operations, std::move(selection));
}

StructuralCommandResult EditCommandService::delete_object(DocumentSession &document, int objectId,
                                                           SelectionState selection)
{
    rebuild_document_indexes(document);
    const auto location = document.indexes.objectsById.find(objectId);
    const auto parent = document.indexes.parentById.find(objectId);
    if (location == document.indexes.objectsById.end() || parent == document.indexes.parentById.end())
        return failed_structural(CommandStatus::StaleAddress, "object to delete is no longer present", document);
    Json *object = resolve_object_at(document.root, location->second);
    if (object == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "object location is no longer valid", document);
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteObject,
                              parent->second, objectId, std::nullopt,
                              location->second.childIndices.back(), *object, std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

CommandResult EditCommandService::delete_objects(DocumentSession &document,
                                                  const std::vector<int> &objectIds,
                                                  SelectionState selection)
{
    try
    {
        rebuild_document_indexes(document);
    }
    catch (const std::exception &error)
    {
        return failed(CommandStatus::Rejected,
                      std::string("cannot prepare hierarchy delete indexes: ") + error.what(), document);
    }
    if (objectIds.empty())
        return failed(CommandStatus::Rejected, "a hierarchy delete requires at least one object", document);
    std::set<int> uniqueIds;
    for (const int id : objectIds)
    {
        if (!uniqueIds.insert(id).second)
            return failed(CommandStatus::Rejected, "a hierarchy delete cannot contain duplicate object IDs", document);
        if (!document.indexes.objectsById.contains(id))
            return failed(CommandStatus::StaleAddress, "an object to delete is no longer present", document);
    }

    Selection requestedSelection;
    requestedSelection.selectedObjectIds = objectIds;
    requestedSelection.objectId = objectIds.back();
    const auto roots = selected_top_level_roots(document, requestedSelection);
    if (roots.empty())
        return failed(CommandStatus::Rejected, "hierarchy delete contains no top-level objects", document);

    struct DeletingObject
    {
        int id = 0;
        std::optional<int> parent;
        std::size_t index = 0;
        Json payload;
    };
    std::vector<DeletingObject> deleting;
    deleting.reserve(roots.size());
    for (const int id : roots)
    {
        const auto location = document.indexes.objectsById.find(id);
        const auto parent = document.indexes.parentById.find(id);
        if (location == document.indexes.objectsById.end() || parent == document.indexes.parentById.end() ||
            location->second.childIndices.empty())
            return failed(CommandStatus::StaleAddress, "an object placement to delete is stale", document);
        Json *siblings = mutable_siblings(document, parent->second);
        const auto index = location->second.childIndices.back();
        if (siblings == nullptr || index >= siblings->size() || (*siblings)[index].value("id", -1) != id)
            return failed(CommandStatus::StaleAddress, "an object source placement is unavailable", document);
        deleting.push_back({id, parent->second, index, (*siblings)[index]});
    }
    std::sort(deleting.begin(), deleting.end(), [](const DeletingObject &left, const DeletingObject &right) {
        if (left.parent != right.parent)
            return left.parent < right.parent;
        return left.index > right.index;
    });

    const auto identity = document_identity(document);
    std::vector<BatchOperation> operations;
    operations.reserve(deleting.size());
    for (const auto &entry : deleting)
    {
        StructuralCommand remove{document.documentGeneration, StructuralCommandKind::DeleteObject,
            entry.parent, entry.id, std::nullopt, entry.index, entry.payload, selection};
        remove.identity = identity;
        operations.emplace_back(std::move(remove));
    }
    return apply_batch(document, operations, std::move(selection));
}

StructuralCommandResult EditCommandService::add_component(DocumentSession &document, int objectId,
                                                          Json component, std::size_t insertionIndex,
                                                          SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "component owner is no longer present", document);
    if (!component.is_object())
        return failed_structural(CommandStatus::Rejected, "component payload must be an object", document);
    const auto type = component_type(component);
    if (!type)
        return failed_structural(CommandStatus::Rejected, "component payload requires a valid type discriminator", document);
    auto components = owner->find("components");
    if (components == owner->end() || !components->is_array())
        return failed_structural(CommandStatus::Rejected, "component collection is malformed", document);
    if (std::any_of(components->begin(), components->end(), [&](const Json &existing) {
            return component_type(existing) == type;
        }))
        return failed_structural(CommandStatus::Rejected, "the object already has this component slot", document);
    if (insertionIndex == std::numeric_limits<std::size_t>::max())
        insertionIndex = components->size();
    if (insertionIndex > components->size())
        return failed_structural(CommandStatus::Rejected, "component insertion index is out of range", document);
    const auto id = first_unused_id(document);
    if (!id)
        return failed_structural(CommandStatus::Rejected, "no file IDs remain for a new component", document);
    component["id"] = *id;
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertComponent,
                              std::nullopt, objectId, std::nullopt, insertionIndex,
                              std::move(component), std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::remove_component(DocumentSession &document, int objectId,
                                                             int componentId, SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "component owner is no longer present", document);
    auto components = owner->find("components");
    if (components == owner->end() || !components->is_array())
        return failed_structural(CommandStatus::Rejected, "component collection is malformed", document);
    for (std::size_t index = 0; index < components->size(); ++index)
    {
        if (record_id((*components)[index]) != componentId)
            continue;
        StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteComponent,
                                  std::nullopt, objectId, std::nullopt, index,
                                  (*components)[index], std::move(selection)};
        return apply(document, command, CommandDirection::Forward);
    }
    return failed_structural(CommandStatus::StaleAddress, "component ID is no longer present on the object", document);
}

StructuralCommandResult EditCommandService::add_behavior(DocumentSession &document, int objectId,
                                                         int scriptComponentId, Json behavior,
                                                         std::size_t insertionIndex,
                                                         SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "behavior owner is no longer present", document);
    Json *script = find_component(*owner, scriptComponentId);
    if (script == nullptr || component_type(*script) != SCRIPT)
        return failed_structural(CommandStatus::StaleAddress, "script component is no longer present", document);
    if (!behavior.is_object() || !behavior.contains("type") || !behavior.at("type").is_string())
        return failed_structural(CommandStatus::Rejected, "behavior payload requires a string type", document);
    auto behaviors = script->find("behaviors");
    if (behaviors == script->end() || !behaviors->is_array())
        return failed_structural(CommandStatus::Rejected, "script behavior list is malformed", document);
    if (insertionIndex == std::numeric_limits<std::size_t>::max())
        insertionIndex = behaviors->size();
    if (insertionIndex > behaviors->size())
        return failed_structural(CommandStatus::Rejected, "behavior insertion index is out of range", document);
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::InsertBehavior,
                              std::nullopt, objectId, scriptComponentId, insertionIndex,
                              std::move(behavior), std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

StructuralCommandResult EditCommandService::remove_behavior(DocumentSession &document, int objectId,
                                                            int scriptComponentId,
                                                            std::size_t behaviorOrdinal,
                                                            SelectionState selection)
{
    rebuild_document_indexes(document);
    Json *owner = mutable_object(document, objectId);
    if (owner == nullptr)
        return failed_structural(CommandStatus::StaleAddress, "behavior owner is no longer present", document);
    Json *script = find_component(*owner, scriptComponentId);
    if (script == nullptr || component_type(*script) != SCRIPT)
        return failed_structural(CommandStatus::StaleAddress, "script component is no longer present", document);
    auto behaviors = script->find("behaviors");
    if (behaviors == script->end() || !behaviors->is_array() || behaviorOrdinal >= behaviors->size())
        return failed_structural(CommandStatus::StaleAddress, "behavior ordinal is no longer present", document);
    StructuralCommand command{document.documentGeneration, StructuralCommandKind::DeleteBehavior,
                              std::nullopt, objectId, scriptComponentId, behaviorOrdinal,
                              (*behaviors)[behaviorOrdinal], std::move(selection)};
    return apply(document, command, CommandDirection::Forward);
}

std::optional<PropertyGesture> EditCommandService::begin_gesture(DocumentSession &document,
                                                                 const PropertyAddress &address,
                                                                 SelectionState selection)
{
    if (historyGeneration_ == 0)
        historyGeneration_ = document.documentGeneration;
    if (!address_matches_document(document, address))
        return std::nullopt;
    ResolveFailure failure;
    if (!canonical_property_path(document, address, failure))
        return std::nullopt;
    auto target = resolve_target(document, address, failure);
    if (!target)
        return std::nullopt;
    PropertyCommand command{address, target->current, target->current,
                            capture_selection(std::move(selection))};
    ++activeGestures_;
    return PropertyGesture(*this, document, std::move(command));
}

std::optional<BatchPropertyGesture> EditCommandService::begin_batch_gesture(
    DocumentSession &document, const std::vector<PropertyAddress> &addresses,
    SelectionState selection)
{
    if (addresses.empty())
        return std::nullopt;
    if (historyGeneration_ == 0)
        historyGeneration_ = document.documentGeneration;
    BatchCommand batch;
    batch.identity = document_identity(document);
    batch.selection = capture_selection(std::move(selection));
    batch.operations.reserve(addresses.size());
    std::set<CanonicalJsonPath, CanonicalJsonPathLess> normalizedPaths;
    for (const auto &address : addresses)
    {
        if (!address_matches_document(document, address))
            return std::nullopt;
        ResolveFailure failure;
        auto normalized = canonical_property_path(document, address, failure);
        if (!normalized)
            return std::nullopt;
        const auto next = normalizedPaths.lower_bound(*normalized);
        if ((next != normalizedPaths.end() && path_has_prefix(*next, *normalized)) ||
            (next != normalizedPaths.begin() &&
             path_has_prefix(*normalized, *std::prev(next))))
            return std::nullopt;
        normalizedPaths.insert(std::move(*normalized));
        auto target = resolve_target(document, address, failure);
        if (!target)
            return std::nullopt;
        batch.operations.emplace_back(PropertyCommand{address, target->current, target->current,
                                                       batch.selection});
    }
    ++activeGestures_;
    return BatchPropertyGesture(*this, document, std::move(batch));
}

PropertyGesture::PropertyGesture(EditCommandService &service, DocumentSession &document,
                                 PropertyCommand command)
    : service_(&service), document_(&document), startingValueRevision_(document.valueRevision),
      command_(std::move(command)), active_(true)
{
}

PropertyGesture::PropertyGesture(PropertyGesture &&other) noexcept
    : service_(std::exchange(other.service_, nullptr)), document_(std::exchange(other.document_, nullptr)),
      startingValueRevision_(other.startingValueRevision_), command_(std::move(other.command_)),
      active_(std::exchange(other.active_, false)), dirtyCounted_(std::exchange(other.dirtyCounted_, false))
{
}

PropertyGesture &PropertyGesture::operator=(PropertyGesture &&other) noexcept
{
    if (this == &other)
        return *this;
    if (active_ && service_ != nullptr)
        service_->finish_gesture(*this, true);
    service_ = std::exchange(other.service_, nullptr);
    document_ = std::exchange(other.document_, nullptr);
    startingValueRevision_ = other.startingValueRevision_;
    command_ = std::move(other.command_);
    active_ = std::exchange(other.active_, false);
    dirtyCounted_ = std::exchange(other.dirtyCounted_, false);
    return *this;
}

PropertyGesture::~PropertyGesture()
{
    if (active_ && service_ != nullptr)
        service_->finish_gesture(*this, true);
}

bool PropertyGesture::active() const noexcept
{
    return active_;
}

CommandResult PropertyGesture::update(EditCommandService &service, DocumentSession &document, Json value)
{
    if (!active_)
        return failed(CommandStatus::Rejected, "property gesture is no longer active", document);
    Json retainedValue = value;
    ++service.suppressHistory_;
    auto result = service.set_property(document, command_.address, std::move(value), command_.selection);
    --service.suppressHistory_;
    if (result.status == CommandStatus::Changed || result.status == CommandStatus::NoChange)
    {
        command_.after = PropertyValue::of(std::move(retainedValue));
        service.update_gesture_dirty(*this, !values_equal(command_.before, command_.after));
    }
    return result;
}

std::optional<PropertyCommand> PropertyGesture::commit()
{
    if (!active_)
        return std::nullopt;
    if (values_equal(command_.before, command_.after))
    {
        if (service_ != nullptr)
            service_->finish_gesture(*this, false);
        return std::nullopt;
    }
    std::optional<PropertyCommand> committed;
    try
    {
        committed = command_;
    }
    catch (...)
    {
        if (service_ != nullptr)
        {
            try
            {
                service_->lastError_ = "cannot prepare gesture commit result";
            }
            catch (...)
            {
            }
            service_->rollback_gesture(*this);
            service_->finish_gesture(*this, false);
        }
        return std::nullopt;
    }
    if (service_ != nullptr && !service_->finish_gesture(*this, true))
        return std::nullopt;
    if (service_ == nullptr)
        active_ = false;
    return committed;
}

CommandResult PropertyGesture::cancel(EditCommandService &service, DocumentSession &document)
{
    if (!active_)
        return failed(CommandStatus::Rejected, "property gesture is no longer active", document);
    if (values_equal(command_.before, command_.after))
    {
        service.finish_gesture(*this, false);
        CommandResult result;
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
        return result;
    }
    auto result = service.apply(document, command_, CommandDirection::Reverse);
    service.finish_gesture(*this, false);
    return result;
}

BatchPropertyGesture::BatchPropertyGesture(EditCommandService &service, DocumentSession &document,
                                           BatchCommand command)
    : service_(&service), document_(&document), startingValueRevision_(document.valueRevision),
      command_(std::move(command)), active_(true)
{
}

BatchPropertyGesture::BatchPropertyGesture(BatchPropertyGesture &&other) noexcept
    : service_(std::exchange(other.service_, nullptr)), document_(std::exchange(other.document_, nullptr)),
      startingValueRevision_(other.startingValueRevision_), command_(std::move(other.command_)),
      active_(std::exchange(other.active_, false)), dirtyCounted_(std::exchange(other.dirtyCounted_, false))
{
}

BatchPropertyGesture &BatchPropertyGesture::operator=(BatchPropertyGesture &&other) noexcept
{
    if (this == &other)
        return *this;
    if (active_ && service_ != nullptr)
        service_->finish_batch_gesture(*this, true);
    service_ = std::exchange(other.service_, nullptr);
    document_ = std::exchange(other.document_, nullptr);
    startingValueRevision_ = other.startingValueRevision_;
    command_ = std::move(other.command_);
    active_ = std::exchange(other.active_, false);
    dirtyCounted_ = std::exchange(other.dirtyCounted_, false);
    return *this;
}

BatchPropertyGesture::~BatchPropertyGesture()
{
    if (active_ && service_ != nullptr)
        service_->finish_batch_gesture(*this, true);
}

bool BatchPropertyGesture::active() const noexcept
{
    return active_;
}

CommandResult BatchPropertyGesture::update(EditCommandService &service, DocumentSession &document,
                                           const std::vector<Json> &values)
{
    if (!active_ || service_ != &service || values.size() != command_.operations.size())
        return failed(CommandStatus::Rejected, "batch property gesture is no longer valid", document);
    std::vector<BatchPropertyEdit> edits;
    std::vector<PropertyValue> retainedValues;
    edits.reserve(values.size());
    retainedValues.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto *property = std::get_if<PropertyCommand>(&command_.operations[index]);
        if (property == nullptr)
            return failed(CommandStatus::Rejected, "batch property gesture contains a structural edit", document);
        edits.push_back({property->address, PropertyValue::of(values[index])});
        retainedValues.push_back(PropertyValue::of(values[index]));
    }
    ++service.suppressHistory_;
    CommandResult result = service.apply_batch(document, edits, command_.selection);
    --service.suppressHistory_;
    if (!result)
        return result;
    bool changed = false;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        auto &property = std::get<PropertyCommand>(command_.operations[index]);
        property.after = std::move(retainedValues[index]);
        changed = changed || !(property.before == property.after);
    }
    service.update_batch_gesture_dirty(*this, changed);
    return result;
}

bool BatchPropertyGesture::commit()
{
    if (!active_)
        return false;
    bool changed = false;
    for (const auto &operation : command_.operations)
        if (const auto *property = std::get_if<PropertyCommand>(&operation))
            changed = changed || !(property->before == property->after);
    if (service_ != nullptr)
    {
        if (!service_->finish_batch_gesture(*this, true))
            return false;
    }
    else
        active_ = false;
    return changed;
}

CommandResult BatchPropertyGesture::cancel(EditCommandService &service, DocumentSession &document)
{
    if (!active_ || service_ != &service)
        return failed(CommandStatus::Rejected, "batch property gesture is no longer active", document);
    bool changed = false;
    for (const auto &operation : command_.operations)
        if (const auto *property = std::get_if<PropertyCommand>(&operation))
            changed = changed || !(property->before == property->after);
    CommandResult result;
    if (changed)
        result = service.apply_batch_entry(document, command_, CommandDirection::Reverse);
    else
    {
        result.status = CommandStatus::NoChange;
        result.change = DocumentChange{document.documentGeneration, document.structureRevision,
                                       document.valueRevision, false};
    }
    service.finish_batch_gesture(*this, false);
    return result;
}
} // namespace platformator_editor
