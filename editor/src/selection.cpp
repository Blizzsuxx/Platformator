#include "selection.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <unordered_set>

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

Selection repaired_selection(const DocumentSession &document, Selection selection)
{
    const Json &root = document.root;
    std::vector<int> repairedIds;
    repairedIds.reserve(selection.selectedObjectIds.size() + (selection.objectId ? 1U : 0U));
    std::unordered_set<int> seen;
    seen.reserve(repairedIds.capacity());
    const auto append_if_present = [&](int id) {
        if (document.indexes.objectsById.contains(id) && seen.insert(id).second)
            repairedIds.push_back(id);
    };
    for (const int id : selection.selectedObjectIds)
        append_if_present(id);
    if (selection.objectId)
        append_if_present(*selection.objectId);

    if (!selection.objectId ||
        std::find(repairedIds.begin(), repairedIds.end(), *selection.objectId) == repairedIds.end())
    {
        selection.objectId = repairedIds.empty() ? std::nullopt : std::optional<int>{repairedIds.front()};
    }
    selection.selectedObjectIds = std::move(repairedIds);
    if (!selection.objectId)
    {
        selection.componentId.reset();
        selection.behaviorOrdinal.reset();
        selection.rangeAnchorObjectId.reset();
        return selection;
    }
    if (!selection.rangeAnchorObjectId ||
        !document.indexes.objectsById.contains(*selection.rangeAnchorObjectId))
        selection.rangeAnchorObjectId = selection.objectId;
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
    Selection next = repaired_selection(document, std::move(selection));
    if (next == selection_)
        return;
    selection_ = std::move(next);
    publish(document);
}

void SelectionModel::set_objects(const DocumentSession &document,
                                 std::vector<int> orderedObjectIds,
                                 std::optional<int> primaryObjectId,
                                 std::optional<int> rangeAnchorObjectId)
{
    Selection next = selection_;
    next.selectedObjectIds = std::move(orderedObjectIds);
    next.objectId = primaryObjectId;
    if (!next.objectId && !next.selectedObjectIds.empty())
        next.objectId = next.selectedObjectIds.back();
    next.componentId.reset();
    next.behaviorOrdinal.reset();
    next.rangeAnchorObjectId = rangeAnchorObjectId ? rangeAnchorObjectId : next.objectId;
    set(document, std::move(next));
}

void SelectionModel::toggle_object(const DocumentSession &document, int objectId)
{
    std::vector<int> ids = selection_.selectedObjectIds;
    if (ids.empty() && selection_.objectId)
        ids.push_back(*selection_.objectId);
    const auto found = std::find(ids.begin(), ids.end(), objectId);
    if (found == ids.end())
    {
        ids.push_back(objectId);
        set_objects(document, std::move(ids), objectId,
                    selection_.rangeAnchorObjectId ? selection_.rangeAnchorObjectId
                                                   : selection_.objectId);
        return;
    }
    ids.erase(found);
    const auto primary = selection_.objectId == objectId
        ? (ids.empty() ? std::nullopt : std::optional<int>{ids.back()})
        : selection_.objectId;
    set_objects(document, std::move(ids), primary,
                selection_.rangeAnchorObjectId == objectId ? primary : selection_.rangeAnchorObjectId);
}

void SelectionModel::select_range(const DocumentSession &document, int objectId,
                                  const std::vector<int> &displayedObjectOrder, bool additive)
{
    const auto target = std::find(displayedObjectOrder.begin(), displayedObjectOrder.end(), objectId);
    if (target == displayedObjectOrder.end())
    {
        set_objects(document, {objectId}, objectId, objectId);
        return;
    }
    int anchorId = selection_.rangeAnchorObjectId.value_or(
        selection_.objectId.value_or(objectId));
    auto anchor = std::find(displayedObjectOrder.begin(), displayedObjectOrder.end(), anchorId);
    if (anchor == displayedObjectOrder.end())
    {
        anchorId = objectId;
        anchor = target;
    }
    const auto first = std::min(anchor, target);
    const auto last = std::max(anchor, target);
    std::vector<int> ids;
    if (additive)
        ids = selection_.selectedObjectIds;
    for (auto current = first; current != std::next(last); ++current)
        if (std::find(ids.begin(), ids.end(), *current) == ids.end())
            ids.push_back(*current);
    set_objects(document, std::move(ids), objectId, anchorId);
}

void SelectionModel::repair(const DocumentSession &document)
{
    Selection next = repaired_selection(document, selection_);
    if (next == selection_)
        return;
    selection_ = std::move(next);
    publish(document);
}

void SelectionModel::subscribe(Observer observer)
{
    if (observer)
        observers_.push_back(std::move(observer));
}

void SelectionModel::publish(const DocumentSession &document)
{
    const SelectionNotification notification{
        document.documentGeneration, selection_,
        DocumentIdentity{ProjectGeneration{document.projectGeneration}, document.tabId,
                         DocumentGeneration{document.documentGeneration}}};
    const auto observers = observers_;
    for (const auto &observer : observers)
        observer(notification);
}

std::vector<int> selected_top_level_roots(const DocumentSession &document,
                                          const Selection &selection)
{
    std::vector<int> selected = selection.selectedObjectIds;
    if (selected.empty() && selection.objectId)
        selected.push_back(*selection.objectId);
    const std::unordered_set<int> selectedSet(selected.begin(), selected.end());
    std::vector<int> roots;
    roots.reserve(selected.size());
    for (const int id : selected)
    {
        if (!document.indexes.objectsById.contains(id))
            continue;
        auto parent = document.indexes.parentById.find(id);
        bool hasSelectedAncestor = false;
        while (parent != document.indexes.parentById.end() && parent->second)
        {
            if (selectedSet.contains(*parent->second))
            {
                hasSelectedAncestor = true;
                break;
            }
            parent = document.indexes.parentById.find(*parent->second);
        }
        if (!hasSelectedAncestor)
            roots.push_back(id);
    }
    return roots;
}
} // namespace platformator_editor
