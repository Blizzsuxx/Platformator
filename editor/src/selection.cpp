#include "selection.h"

#include <algorithm>
#include <utility>

namespace platformator_editor
{
namespace
{
bool has_id(const Json &value, int id)
{
    if (!value.is_object())
        return false;
    const auto found = value.find("id");
    if (found == value.end() || !(found->is_number_integer() || found->is_number_unsigned()))
        return false;
    try
    {
        return found->get<std::int64_t>() == id;
    }
    catch (...)
    {
        return false;
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
            if (object == nullptr)
                return nullptr;
            const auto children = object->find("children");
            if (children == object->end())
                return nullptr;
            siblings = &*children;
        }
        const std::size_t index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}

std::optional<ObjectLocation> find_component_owner(const Json &root, int objectId)
{
    return find_object_location(root, objectId);
}

bool has_component(const Json &root, int objectId, int componentId)
{
    const auto location = find_component_owner(root, objectId);
    if (!location)
        return false;
    const Json *owner = resolve_object(root, *location);
    if (owner == nullptr)
        return false;
    const auto components = owner->find("components");
    if (components == owner->end() || !components->is_array())
        return false;
    return std::any_of(components->begin(), components->end(),
                       [componentId](const Json &component) { return has_id(component, componentId); });
}

bool has_behavior(const Json &root, int objectId, int componentId, std::size_t ordinal)
{
    const auto location = find_component_owner(root, objectId);
    if (!location)
        return false;
    const Json *owner = resolve_object(root, *location);
    if (owner == nullptr)
        return false;
    const auto components = owner->find("components");
    if (components == owner->end() || !components->is_array())
        return false;
    for (const Json &component : *components)
    {
        if (!has_id(component, componentId))
            continue;
        const auto behaviors = component.find("behaviors");
        return behaviors != component.end() && behaviors->is_array() && ordinal < behaviors->size();
    }
    return false;
}

Selection repaired_selection(const Json &root, Selection selection)
{
    if (!selection.objectId)
    {
        selection.componentId.reset();
        selection.behaviorOrdinal.reset();
        return selection;
    }
    if (!find_object_location(root, *selection.objectId))
    {
        selection.clear();
        return selection;
    }
    if (selection.componentId && !has_component(root, *selection.objectId, *selection.componentId))
    {
        selection.componentId.reset();
        selection.behaviorOrdinal.reset();
    }
    if (selection.behaviorOrdinal &&
        (!selection.componentId ||
         !has_behavior(root, *selection.objectId, *selection.componentId, *selection.behaviorOrdinal)))
    {
        selection.behaviorOrdinal.reset();
    }
    return selection;
}
} // namespace

std::optional<ObjectLocation> find_object_location(const Json &root, int objectId)
{
    if (!root.is_array())
        return std::nullopt;

    struct Pending
    {
        const Json *object = nullptr;
        ObjectLocation location;
    };
    std::vector<Pending> pending;
    pending.reserve(root.size());
    for (std::size_t i = root.size(); i > 0; --i)
        pending.push_back({&root[i - 1], {{i - 1}}});

    while (!pending.empty())
    {
        Pending current = std::move(pending.back());
        pending.pop_back();
        if (has_id(*current.object, objectId))
            return current.location;
        const auto children = current.object->find("children");
        if (children == current.object->end() || !children->is_array())
            continue;
        for (std::size_t i = children->size(); i > 0; --i)
        {
            ObjectLocation childLocation = current.location;
            childLocation.childIndices.push_back(i - 1);
            pending.push_back({&(*children)[i - 1], std::move(childLocation)});
        }
    }
    return std::nullopt;
}

const Selection &SelectionModel::current() const noexcept
{
    return selection_;
}

void SelectionModel::set(const DocumentSession &document, Selection selection)
{
    Selection next = repaired_selection(document.root, std::move(selection));
    if (next == selection_)
        return;
    selection_ = std::move(next);
    publish(document.documentGeneration);
}

void SelectionModel::repair(const DocumentSession &document)
{
    Selection next = repaired_selection(document.root, selection_);
    if (next == selection_)
        return;
    selection_ = std::move(next);
    publish(document.documentGeneration);
}

void SelectionModel::subscribe(Observer observer)
{
    if (observer)
        observers_.push_back(std::move(observer));
}

void SelectionModel::publish(std::uint64_t documentGeneration)
{
    const SelectionNotification notification{documentGeneration, selection_};
    const auto observers = observers_;
    for (const auto &observer : observers)
        observer(notification);
}
} // namespace platformator_editor
