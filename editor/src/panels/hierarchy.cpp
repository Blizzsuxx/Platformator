#include "hierarchy.h"

#include "../actions.h"
#include "../componenttypes.h"
#include "../editorwork.h"
#include "../preferences.h"
#include "../style.h"
#include "../discovery.h"
#include "../templates.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
const char *component_name(const Json &component)
{
    const auto found = component.find("type");
    if (found == component.end() || !(found->is_number_integer() || found->is_number_unsigned()))
        return "Component";
    return component_label(found->get<int>());
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
        const auto index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}

Selection selected_object(int objectId)
{
    return Selection{objectId, std::nullopt, std::nullopt, FocusedPanel::Hierarchy};
}

std::string object_hierarchy_label(const std::string &name, int objectId)
{
    return name + " (#" + std::to_string(objectId) + ")";
}

void collect_duplicate_identity_set(const Json &root, std::unordered_set<int> &objectIds,
                                    std::unordered_set<int> &componentIds)
{
    std::vector<const Json *> pending{&root};
    while (!pending.empty())
    {
        const Json *object = pending.back();
        pending.pop_back();
        if (!object->is_object()) continue;
        const auto objectId = object->find("id");
        if (objectId != object->end() && objectId->is_number_integer())
            objectIds.insert(objectId->get<int>());
        const auto components = object->find("components");
        if (components != object->end() && components->is_array())
            for (const auto &component : *components)
            {
                const auto id = component.find("id");
                if (id != component.end() && id->is_number_integer())
                    componentIds.insert(id->get<int>());
            }
        const auto children = object->find("children");
        if (children != object->end() && children->is_array())
            for (const auto &child : *children)
                pending.push_back(&child);
    }
}

struct DuplicateBindingSet
{
    Json bindings = Json::array();
    bool assured = false;
    std::string error;
};

DuplicateBindingSet collect_duplicate_bindings(const DocumentSession &document,
                                                const std::vector<int> &selectedIds,
                                                const Json *catalog,
                                                const std::vector<Json> &authorBindings,
                                                const std::vector<TemplateAuthorSchemaDeclaration> &authorSchemas)
{
    DuplicateBindingSet result;
    if (catalog == nullptr)
    {
        result.error = "behavior reference metadata is unavailable; preserve existing targets or refresh the catalog";
        return result;
    }
    Selection selected;
    selected.selectedObjectIds = selectedIds;
    if (!selectedIds.empty()) selected.objectId = selectedIds.back();
    const auto roots = selected_top_level_roots(document, selected);
    if (roots.empty())
    {
        result.error = "no top-level objects are selected for duplication";
        return result;
    }
    std::unordered_set<int> selectedObjects;
    std::unordered_set<int> selectedComponents;
    for (const int id : roots)
    {
        const auto location = document.indexes.objectsById.find(id);
        const Json *object = location == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, location->second);
        if (object == nullptr)
        {
            result.error = "a selected object changed before duplicate references were resolved";
            return result;
        }
        collect_duplicate_identity_set(*object, selectedObjects, selectedComponents);
    }
    std::string sceneIdentity = template_scene_identity(document);
    if (sceneIdentity.empty())
        sceneIdentity = "unsaved-document:" + std::to_string(document.tabId.value) + ":" +
                        std::to_string(document.documentGeneration);
    for (const int id : roots)
    {
        const auto location = document.indexes.objectsById.find(id);
        const Json *object = location == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, location->second);
        if (object == nullptr)
        {
            result.error = "a selected object changed before duplicate references were resolved";
            return result;
        }
        std::unordered_set<int> localObjects;
        std::unordered_set<int> localComponents;
        collect_duplicate_identity_set(*object, localObjects, localComponents);
        std::vector<Json> localAuthorBindings;
        for (const auto &binding : authorBindings)
            if (binding.is_object() && binding.contains("owner_id") &&
                binding["owner_id"].is_number_integer() &&
                localObjects.contains(binding["owner_id"].get<int>()))
                localAuthorBindings.push_back(binding);
        std::vector<TemplateAuthorSchemaDeclaration> localAuthorSchemas;
        for (const auto &declaration : authorSchemas)
            if (localObjects.contains(declaration.ownerId) &&
                localComponents.contains(declaration.componentId))
                localAuthorSchemas.push_back(declaration);
        const auto collected = collect_template_bindings(document, *object, sceneIdentity, *catalog,
                                                         localAuthorBindings, localAuthorSchemas);
        if (!collected || !collected.assured)
        {
            result.error = collected.error.empty()
                ? "behavior schema is unavailable; declare its reference fields before remapping"
                : collected.error;
            if (!collected.unresolvedBehaviors.empty())
                result.error += " (" + collected.unresolvedBehaviors.front() + ")";
            return result;
        }
        for (auto binding : collected.bindings)
        {
            if (binding.value("scope", std::string{}) == "external")
            {
                const int targetId = binding.value("original_target_id", -1);
                const std::string targetKind = binding.value("target_kind", std::string{});
                const bool selectedTarget = targetKind == "Component"
                    ? selectedComponents.contains(targetId)
                    : selectedObjects.contains(targetId);
                if (selectedTarget)
                {
                    binding["scope"] = "internal";
                    binding.erase("source_scene_identity");
                    binding.erase("original_target_kind");
                    binding.erase("original_target_id");
                }
            }
            result.bindings.push_back(std::move(binding));
        }
    }
    result.assured = true;
    return result;
}

int resize_rename_buffer(ImGuiInputTextCallbackData *data)
{
    if (data == nullptr || data->EventFlag != ImGuiInputTextFlags_CallbackResize)
        return 0;
    auto *buffer = static_cast<std::vector<char> *>(data->UserData);
    if (buffer == nullptr)
        return 0;
    buffer->resize(static_cast<std::size_t>(data->BufTextLen) + 1);
    data->Buf = buffer->data();
    return 0;
}

} // namespace

const std::vector<HierarchyRow> &HierarchyPanel::visible_rows(const DocumentSession &document)
{
    if (!cacheValid_ || cachedGeneration_ != document.documentGeneration ||
        cachedStructureRevision_ != document.structureRevision)
        rebuild(document);
    if (cachedValueRevision_ != document.valueRevision)
        refresh_labels(document);
    return rows_;
}

void HierarchyPanel::set_search_query(std::string query)
{
    if (query == searchQuery_)
        return;
    if (searchQuery_.empty() && !query.empty())
    {
        searchExpansionBackup_ = expandedObjects_;
        hasSearchExpansionBackup_ = true;
    }
    else if (!searchQuery_.empty() && query.empty())
    {
        if (hasSearchExpansionBackup_)
            expandedObjects_ = std::move(searchExpansionBackup_);
        searchExpansionBackup_.clear();
        hasSearchExpansionBackup_ = false;
        searchExpansionQuery_.clear();
    }
    else if (!searchQuery_.empty() && !query.empty() && hasSearchExpansionBackup_)
        expandedObjects_ = searchExpansionBackup_;
    searchQuery_ = std::move(query);
    cachedSearchQuery_.clear();
    searchVisibilityQuery_.clear();
    invalidate();
}

void HierarchyPanel::invalidate() noexcept
{
    cacheValid_ = false;
}

void HierarchyPanel::reveal_object(const DocumentSession &document, int objectId)
{
    if (!document.indexes.objectsById.contains(objectId))
        return;
    pendingScrollObjectId_ = objectId;
    lastRevealedObjectId_ = objectId;
    int currentId = objectId;
    while (true)
    {
        const auto parent = document.indexes.parentById.find(currentId);
        if (parent == document.indexes.parentById.end() || !parent->second)
            break;
        expandedObjects_.insert(*parent->second);
        currentId = *parent->second;
    }
    invalidate();
}

std::vector<int> HierarchyPanel::expanded_object_ids() const
{
    std::vector<int> result(expandedObjects_.begin(), expandedObjects_.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<SemanticNode> HierarchyPanel::semantic_nodes(
    const DocumentSession &document, const Selection &selection,
    std::string_view scope) const
{
    const std::string rootId = std::string(scope) + "/hierarchy";
    std::vector<SemanticNode> result;
    result.reserve(semanticVisibleRows_.size() + 1);

    SemanticNode root;
    root.id = rootId;
    root.role = SemanticRole::Group;
    root.name = "Hierarchy";
    root.visible = true;
    root.enabled = true;
    root.expanded = true;
    result.push_back(std::move(root));

    if (!cacheValid_ || cachedGeneration_ != document.documentGeneration ||
        cachedStructureRevision_ != document.structureRevision ||
        cachedValueRevision_ != document.valueRevision)
        return result;

    const auto objectNodeId = [&](int id) {
        return std::string(scope) + "/object/" + std::to_string(id);
    };
    const auto componentsNodeId = [&](int id) {
        return std::string(scope) + "/components/" + std::to_string(id);
    };
    const auto componentNodeId = [&](int id) {
        return std::string(scope) + "/component/" + std::to_string(id);
    };
    std::unordered_set<int> visibleRows;
    visibleRows.reserve(semanticVisibleRows_.size());
    for (const auto &visible : semanticVisibleRows_)
        visibleRows.insert(visible.rowIndex);

    for (const auto &visible : semanticVisibleRows_)
    {
        if (visible.rowIndex < 0 || static_cast<std::size_t>(visible.rowIndex) >= rows_.size())
            continue;
        const auto &row = rows_[static_cast<std::size_t>(visible.rowIndex)];
        if ((row.kind == HierarchyRowKind::Object &&
             !document.indexes.objectsById.contains(row.id)) ||
            (row.kind == HierarchyRowKind::ComponentsNode &&
             !document.indexes.objectsById.contains(row.ownerObjectId)) ||
            (row.kind == HierarchyRowKind::Component &&
             !document.indexes.componentsById.contains(row.id)))
            continue;
        SemanticNode node;
        node.role = SemanticRole::ListItem;
        node.name = row.label;
        node.value = std::to_string(row.id);
        node.visible = true;
        node.enabled = true;
        node.bounds = visible.bounds;
        node.actions = {"activate"};

        if (row.kind == HierarchyRowKind::Object)
        {
            node.id = objectNodeId(row.id);
            node.parentId = rootId;
            if (row.parentRowIndex >= 0 && visibleRows.contains(row.parentRowIndex))
            {
                const auto &parent = rows_[static_cast<std::size_t>(row.parentRowIndex)];
                if (parent.kind == HierarchyRowKind::Object)
                    node.parentId = objectNodeId(parent.id);
            }
            node.selected = !selection.componentId &&
                (selection.objectId == row.id ||
                 std::find(selection.selectedObjectIds.begin(),
                           selection.selectedObjectIds.end(), row.id) !=
                     selection.selectedObjectIds.end());
            node.focused = node.selected && selection.focusedPanel == FocusedPanel::Hierarchy;
            node.expanded = expandedObjects_.contains(row.id);
            node.help = "GameObject, ID " + std::to_string(row.id);
            node.actions.push_back("reveal");
        }
        else if (row.kind == HierarchyRowKind::ComponentsNode)
        {
            node.id = componentsNodeId(row.ownerObjectId);
            node.parentId = objectNodeId(row.ownerObjectId);
            if (row.parentRowIndex >= 0 && visibleRows.contains(row.parentRowIndex))
            {
                const auto &parent = rows_[static_cast<std::size_t>(row.parentRowIndex)];
                node.parentId = parent.kind == HierarchyRowKind::Object
                    ? objectNodeId(parent.id) : rootId;
            }
            else
                node.parentId = rootId;
            node.expanded = !collapsedComponentGroups_.contains(row.ownerObjectId);
            node.selected = selection.objectId == row.ownerObjectId;
            node.focused = node.selected && selection.focusedPanel == FocusedPanel::Hierarchy;
            node.help = "Components for GameObject " + std::to_string(row.ownerObjectId);
        }
        else
        {
            node.id = componentNodeId(row.id);
            node.parentId = componentsNodeId(row.ownerObjectId);
            if (row.parentRowIndex < 0 || !visibleRows.contains(row.parentRowIndex))
                node.parentId = rootId;
            else
            {
                const auto &parent = rows_[static_cast<std::size_t>(row.parentRowIndex)];
                if (parent.kind != HierarchyRowKind::ComponentsNode ||
                    parent.ownerObjectId != row.ownerObjectId)
                    node.parentId = rootId;
            }
            node.selected = selection.componentId == row.id &&
                            selection.objectId == row.ownerObjectId;
            node.focused = node.selected && selection.focusedPanel == FocusedPanel::Hierarchy;
            node.help = "Component " + std::to_string(row.id) + " on GameObject " +
                        std::to_string(row.ownerObjectId);
        }
        result.push_back(std::move(node));
    }
    return result;
}

bool HierarchyPanel::dispatch_semantic_action(
    DocumentSession &document, SelectionModel &selection, const SemanticAction &action,
    std::string_view scope, WindowHostGeneration hostGeneration)
{
    const std::string base(scope);
    const std::string objectPrefix = base + "/object/";
    const std::string componentsPrefix = base + "/components/";
    const std::string componentPrefix = base + "/component/";
    const auto parse_id = [](std::string_view text) -> std::optional<int> {
        int value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0)
            return std::nullopt;
        return value;
    };
    const auto select_object = [&](int objectId) {
        selection.set(document, Selection{objectId, std::nullopt, std::nullopt,
            FocusedPanel::Hierarchy, hostGeneration, {objectId}, objectId});
    };

    if (action.nodeId.starts_with(objectPrefix))
    {
        const auto objectId = parse_id(std::string_view(action.nodeId).substr(objectPrefix.size()));
        if (!objectId || !document.indexes.objectsById.contains(*objectId))
            return true;
        if (action.action == "activate")
            select_object(*objectId);
        else if (action.action == "reveal")
            reveal_object(document, *objectId);
        return action.action == "activate" || action.action == "reveal";
    }
    if (action.nodeId.starts_with(componentsPrefix))
    {
        const auto objectId = parse_id(
            std::string_view(action.nodeId).substr(componentsPrefix.size()));
        if (objectId && document.indexes.objectsById.contains(*objectId) &&
            action.action == "activate")
            select_object(*objectId);
        return action.action == "activate";
    }
    if (action.nodeId.starts_with(componentPrefix))
    {
        const auto componentId = parse_id(
            std::string_view(action.nodeId).substr(componentPrefix.size()));
        if (componentId && action.action == "activate")
        {
            const auto component = document.indexes.componentsById.find(*componentId);
            if (component != document.indexes.componentsById.end())
                selection.set(document, Selection{component->second.ownerObjectId, *componentId,
                    std::nullopt, FocusedPanel::Hierarchy, hostGeneration});
        }
        return action.action == "activate";
    }
    return false;
}

void HierarchyPanel::restore_expanded_object_ids(const DocumentSession &document,
                                                 const std::vector<int> &objectIds)
{
    expandedObjects_.clear();
    for (const int id : objectIds)
        if (document.indexes.objectsById.contains(id))
            expandedObjects_.insert(id);
    knownObjectIds_.clear();
    knownObjectIds_.reserve(document.indexes.objectsById.size());
    for (const auto &[id, location] : document.indexes.objectsById)
    {
        (void)location;
        knownObjectIds_.insert(id);
    }
    collapsedComponentGroups_.clear();
    cachedGeneration_ = document.documentGeneration;
    initializedExpansion_ = true;
    cacheValid_ = false;
    lastRevealedObjectId_.reset();
    pendingScrollObjectId_.reset();
}

void HierarchyPanel::reset_expansion_for_document() noexcept
{
    expandedObjects_.clear();
    knownObjectIds_.clear();
    collapsedComponentGroups_.clear();
    initializedExpansion_ = false;
    cachedGeneration_ = 0;
    cacheValid_ = false;
    lastRevealedObjectId_.reset();
    pendingScrollObjectId_.reset();
}

void HierarchyPanel::apply_change(const DocumentSession &document, const DocumentChange &change)
{
    if (!cacheValid_ || change.structural || cachedGeneration_ != document.documentGeneration ||
        cachedStructureRevision_ != document.structureRevision ||
        change.documentGeneration != document.documentGeneration ||
        change.structureRevision != document.structureRevision ||
        change.valueRevision != document.valueRevision ||
        change.identity != DocumentIdentity{ProjectGeneration{document.projectGeneration},
                                            document.tabId,
                                            DocumentGeneration{document.documentGeneration}})
    {
        cacheValid_ = false;
        return;
    }
    if (cachedValueRevision_ == document.valueRevision)
        return;
    if (cachedValueRevision_ == std::numeric_limits<std::uint64_t>::max() ||
        cachedValueRevision_ + 1 != document.valueRevision)
    {
        cachedValueRevision_ = std::numeric_limits<std::uint64_t>::max();
        return;
    }

    const bool labelChanged = std::find(change.categories.begin(), change.categories.end(),
                                         DocumentChangeCategory::Label) != change.categories.end();
    if (!labelChanged)
    {
        cachedValueRevision_ = document.valueRevision;
        return;
    }
    if (change.affectedObjectIds.empty())
    {
        cachedValueRevision_ = std::numeric_limits<std::uint64_t>::max();
        return;
    }

    std::size_t visited = 0;
    for (const int objectId : change.affectedObjectIds)
    {
        const auto row = objectRowIndexes_.find(objectId);
        if (row == objectRowIndexes_.end())
            continue;
        const auto location = document.indexes.objectsById.find(objectId);
        if (location == document.indexes.objectsById.end())
            continue;
        const Json *object = resolve_object(document.root, location->second);
        if (object == nullptr || !object->is_object())
            continue;
        std::string label = object_hierarchy_label(
            object->value("name", std::string("GameObject")), objectId);
        auto &cachedLabel = rows_[row->second].label;
        if (label.capacity() > cachedLabel.capacity())
            work::record(work::Counter::HierarchyLabelStringGrowths);
        cachedLabel = std::move(label);
        ++visited;
    }
    work::record(work::Counter::HierarchyLabelRows, visited);
    cachedValueRevision_ = document.valueRevision;
}

void HierarchyPanel::rebuild(const DocumentSession &document)
{
    rows_.clear();
    objectRowIndexes_.clear();
    displayedObjectOrder_.clear();
    const std::size_t objectCount = document.indexes.objectsById.size();
    const std::size_t componentCount = document.indexes.componentsById.size();
    if (objectCount <= (std::numeric_limits<std::size_t>::max() - componentCount) / 2)
        rows_.reserve(objectCount * 2 + componentCount);
    objectRowIndexes_.reserve(objectCount);
    displayedObjectOrder_.reserve(objectCount);
    const bool newDocument = !initializedExpansion_ || cachedGeneration_ != document.documentGeneration;
    if (newDocument)
    {
        expandedObjects_.clear();
        knownObjectIds_.clear();
        collapsedComponentGroups_.clear();
    }

    for (const int id : document.indexes.orderedObjectIds)
    {
        const auto location = document.indexes.objectsById.find(id);
        const Json *object = location == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, location->second);
        if (object == nullptr)
            continue;
        const bool isNewObject = newDocument || !knownObjectIds_.contains(id);
        if (!isNewObject)
            continue;
        const auto children = object->find("children");
        const auto components = object->find("components");
        const bool hasChildren = children != object->end() && children->is_array() && !children->empty();
        const bool hasComponents = components != object->end() && components->is_array() && !components->empty();
        if (hasChildren || hasComponents)
            expandedObjects_.insert(id);
    }
    knownObjectIds_.clear();
    knownObjectIds_.reserve(document.indexes.objectsById.size());
    for (const auto &[id, location] : document.indexes.objectsById)
    {
        (void)location;
        knownObjectIds_.insert(id);
    }

    if (newDocument)
    {
        for (const auto &[componentId, location] : document.indexes.componentsById)
        {
            (void)componentId;
            int ownerObjectId = location.ownerObjectId;
            while (true)
            {
                expandedObjects_.insert(ownerObjectId);
                const auto parent = document.indexes.parentById.find(ownerObjectId);
                if (parent == document.indexes.parentById.end() || !parent->second)
                    break;
                ownerObjectId = *parent->second;
            }
        }
        initializedExpansion_ = true;
    }

    if (pendingScrollObjectId_ && document.indexes.objectsById.contains(*pendingScrollObjectId_))
    {
        int currentId = *pendingScrollObjectId_;
        while (true)
        {
            const auto parent = document.indexes.parentById.find(currentId);
            if (parent == document.indexes.parentById.end() || !parent->second)
                break;
            expandedObjects_.insert(*parent->second);
            currentId = *parent->second;
        }
    }

    for (auto it = expandedObjects_.begin(); it != expandedObjects_.end();)
    {
        if (!document.indexes.objectsById.contains(*it))
            it = expandedObjects_.erase(it);
        else
            ++it;
    }

    for (auto it = collapsedComponentGroups_.begin(); it != collapsedComponentGroups_.end();)
    {
        const auto location = document.indexes.objectsById.find(*it);
        const Json *object = location == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, location->second);
        if (object == nullptr)
        {
            it = collapsedComponentGroups_.erase(it);
            continue;
        }
        const auto components = object->find("components");
        if (components == object->end() || !components->is_array() || components->empty())
            it = collapsedComponentGroups_.erase(it);
        else
            ++it;
    }

    struct Pending
    {
        const Json *value = nullptr;
        int ownerObjectId = 0;
        std::size_t depth = 0;
        HierarchyRowKind kind = HierarchyRowKind::Object;
        int parentRowIndex = -1;
    };
    std::vector<Pending> pending;
    pending.reserve(document.indexes.objectsById.size() + document.indexes.componentsById.size());
    for (std::size_t index = document.root.size(); index > 0; --index)
        pending.push_back({&document.root[index - 1], 0, 0, HierarchyRowKind::Object, -1});

    while (!pending.empty())
    {
        const Pending current = pending.back();
        pending.pop_back();
        if (current.value == nullptr || !current.value->is_object())
            continue;
        if (current.kind == HierarchyRowKind::ComponentsNode)
        {
            const int groupIndex = static_cast<int>(rows_.size());
            rows_.push_back(HierarchyRow{HierarchyRowKind::ComponentsNode, current.ownerObjectId,
                                         current.ownerObjectId, current.depth, "Components", true,
                                         current.parentRowIndex});
            const auto components = current.value->find("components");
            if (!collapsedComponentGroups_.contains(current.ownerObjectId) &&
                components != current.value->end() && components->is_array())
            {
                for (std::size_t index = components->size(); index > 0; --index)
                    pending.push_back({&(*components)[index - 1], current.ownerObjectId,
                                       current.depth + 1, HierarchyRowKind::Component, groupIndex});
            }
            continue;
        }
        if (current.kind == HierarchyRowKind::Component)
        {
            const int componentId = current.value->value("id", 0);
            rows_.push_back(HierarchyRow{HierarchyRowKind::Component, componentId,
                                         current.ownerObjectId, current.depth,
                                         component_name(*current.value), false,
                                         current.parentRowIndex});
            continue;
        }

        const int objectId = current.value->value("id", 0);
        const auto name = current.value->value("name", std::string("GameObject"));
        const auto children = current.value->find("children");
        const auto components = current.value->find("components");
        const bool hasChildren = (children != current.value->end() && children->is_array() && !children->empty()) ||
                                 (components != current.value->end() && components->is_array() && !components->empty());
        const int objectRowIndex = static_cast<int>(rows_.size());
        rows_.push_back(HierarchyRow{HierarchyRowKind::Object, objectId, objectId,
                                     current.depth, object_hierarchy_label(name, objectId), hasChildren,
                                     current.parentRowIndex});
        objectRowIndexes_.insert_or_assign(objectId, static_cast<std::size_t>(objectRowIndex));
        displayedObjectOrder_.push_back(objectId);
        if (!expandedObjects_.contains(objectId))
            continue;

        if (children != current.value->end() && children->is_array())
        {
            for (std::size_t index = children->size(); index > 0; --index)
                pending.push_back({&(*children)[index - 1], objectId, current.depth + 1,
                                   HierarchyRowKind::Object, objectRowIndex});
        }
        if (components != current.value->end() && components->is_array() && !components->empty())
        {
            pending.push_back({current.value, objectId, current.depth + 1,
                               HierarchyRowKind::ComponentsNode, objectRowIndex});
        }
    }
    firstChildRows_.assign(rows_.size(), -1);
    lastChildRows_.assign(rows_.size(), -1);
    connectorVisitMarks_.assign(rows_.size(), 0);
    connectorCandidates_.clear();
    connectorCandidates_.reserve(rows_.size());
    for (std::size_t rowIndex = 0; rowIndex < rows_.size(); ++rowIndex)
    {
        const int parentIndex = rows_[rowIndex].parentRowIndex;
        if (parentIndex < 0)
            continue;
        auto &first = firstChildRows_[static_cast<std::size_t>(parentIndex)];
        if (first < 0)
            first = static_cast<int>(rowIndex);
        lastChildRows_[static_cast<std::size_t>(parentIndex)] = static_cast<int>(rowIndex);
    }
    cachedGeneration_ = document.documentGeneration;
    cachedStructureRevision_ = document.structureRevision;
    cachedValueRevision_ = document.valueRevision;
    cacheValid_ = true;
}

void HierarchyPanel::refresh_labels(const DocumentSession &document)
{
    work::record(work::Counter::HierarchyLabelRows, rows_.size());
    for (auto &row : rows_)
    {
        if (row.kind == HierarchyRowKind::ComponentsNode)
            continue;
        if (row.kind == HierarchyRowKind::Object)
        {
            const auto location = document.indexes.objectsById.find(row.id);
            if (location == document.indexes.objectsById.end())
                continue;
            const Json *object = resolve_object(document.root, location->second);
            if (object != nullptr && object->is_object())
            {
                row.label = object_hierarchy_label(
                    object->value("name", std::string("GameObject")), row.id);
            }
            continue;
        }
        const auto location = document.indexes.componentsById.find(row.id);
        if (location == document.indexes.componentsById.end())
            continue;
        const Json *owner = resolve_object(document.root, document.indexes.objectsById.at(location->second.ownerObjectId));
        if (owner == nullptr)
            continue;
        const auto components = owner->find("components");
        if (components != owner->end() && components->is_array() &&
            location->second.componentIndex < components->size())
            row.label = component_name((*components)[location->second.componentIndex]);
    }
    cachedValueRevision_ = document.valueRevision;
}

void HierarchyPanel::update_search_matches(const DocumentSession &document)
{
    if (searchQuery_ == cachedSearchQuery_ &&
        cachedSearchStructureRevision_ == document.structureRevision &&
        cachedSearchValueRevision_ == document.valueRevision)
        return;
    cachedSearchQuery_ = searchQuery_;
    cachedSearchStructureRevision_ = document.structureRevision;
    cachedSearchValueRevision_ = document.valueRevision;
    searchMatchedObjectIds_.clear();
    searchContextObjectIds_.clear();
    searchMatchedComponentIds_.clear();
    searchComponentOwnerIds_.clear();
    searchResultObjectOrder_.clear();

    std::string query = searchQuery_;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (query.empty())
        return;
    const auto contains = [&](std::string_view value) {
        return discovery_query_matches(searchQuery_, value);
    };

    for (const int objectId : document.indexes.orderedObjectIds)
    {
        const auto location = document.indexes.objectsById.find(objectId);
        const Json *object = location == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, location->second);
        if (object == nullptr)
            continue;
        bool matched = contains(object->value("name", std::string("GameObject"))) ||
                       contains(object->value("tag", std::string{})) ||
                       contains(std::to_string(objectId)) ||
                       contains("#" + std::to_string(objectId)) ||
                       query == "gameobject" || query == "object";
        std::string path;
        int currentId = objectId;
        while (true)
        {
            const auto current = document.indexes.objectsById.find(currentId);
            if (current == document.indexes.objectsById.end())
                break;
            const Json *ancestor = resolve_object(document.root, current->second);
            if (ancestor == nullptr)
                break;
            if (!path.empty())
                path.insert(0, " / ");
            path.insert(0, ancestor->value("name", std::string("GameObject")));
            const auto parent = document.indexes.parentById.find(currentId);
            if (parent == document.indexes.parentById.end() || !parent->second)
                break;
            currentId = *parent->second;
        }
        matched = matched || contains(path);

        const auto components = object->find("components");
        if (components != object->end() && components->is_array())
        {
            for (const auto &component : *components)
            {
                if (!component.is_object())
                    continue;
                const int componentId = component.value("id", -1);
                const int type = component.value("type", -1);
                const bool componentMatched = contains(component_name(component)) ||
                                              contains(std::to_string(type)) ||
                                              contains(std::to_string(componentId)) ||
                                              contains("#" + std::to_string(componentId));
                if (componentMatched && componentId >= 0)
                {
                    searchMatchedComponentIds_.insert(componentId);
                    searchComponentOwnerIds_.insert(objectId);
                    matched = true;
                }
            }
        }
        if (matched)
            searchMatchedObjectIds_.insert(objectId);
    }
    for (const int objectId : document.indexes.orderedObjectIds)
    {
        if (!searchMatchedObjectIds_.contains(objectId))
            continue;
        searchResultObjectOrder_.push_back(objectId);
        int currentId = objectId;
        while (true)
        {
            const auto parent = document.indexes.parentById.find(currentId);
            if (parent == document.indexes.parentById.end() || !parent->second)
                break;
            currentId = *parent->second;
            searchContextObjectIds_.insert(currentId);
            if (hasSearchExpansionBackup_)
                expandedObjects_.insert(currentId);
        }
        if (searchComponentOwnerIds_.contains(objectId) && hasSearchExpansionBackup_)
            expandedObjects_.insert(objectId);
    }
    searchExpansionQuery_ = searchQuery_;
    invalidate();
}

void HierarchyPanel::update_search_visibility(const DocumentSession &document,
                                             std::optional<int> selectedObjectId)
{
    if (searchVisibilityQuery_ == searchQuery_ &&
        searchVisibilityStructureRevision_ == document.structureRevision &&
        searchVisibilityValueRevision_ == document.valueRevision &&
        searchVisibleRows_.size() == rows_.size())
        return;
    searchVisibilityQuery_ = searchQuery_;
    searchVisibilityStructureRevision_ = document.structureRevision;
    searchVisibilityValueRevision_ = document.valueRevision;
    searchVisibleRows_.assign(rows_.size(), searchQuery_.empty() ? 1 : 0);
    if (searchQuery_.empty())
        return;

    std::string query = searchQuery_;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    const bool componentsQuery = query.find("component") != std::string::npos;
    const auto mark_ancestors = [&](int rowIndex) {
        int current = rowIndex;
        while (current >= 0)
        {
            searchVisibleRows_[static_cast<std::size_t>(current)] = 1;
            current = rows_[static_cast<std::size_t>(current)].parentRowIndex;
        }
    };
    for (std::size_t rowIndex = 0; rowIndex < rows_.size(); ++rowIndex)
    {
        const auto &row = rows_[rowIndex];
        bool visible = false;
        if (row.kind == HierarchyRowKind::Object)
            visible = searchMatchedObjectIds_.contains(row.id) ||
                      searchContextObjectIds_.contains(row.id) ||
                      selectedObjectId == row.id;
        else if (row.kind == HierarchyRowKind::Component)
            visible = searchMatchedComponentIds_.contains(row.id);
        else if (row.kind == HierarchyRowKind::ComponentsNode)
            visible = (componentsQuery && document.indexes.objectsById.contains(row.ownerObjectId)) ||
                      searchComponentOwnerIds_.contains(row.ownerObjectId);
        if (visible)
            mark_ancestors(static_cast<int>(rowIndex));
    }
}

void HierarchyPanel::draw(DocumentSession &document, SelectionModel &selection,
                          EditCommandService &commands, bool editingAllowed,
                          std::vector<int> *hiddenObjectIds,
                          std::vector<int> *lockedObjectIds,
                          std::string *duplicationReferencePolicy,
                          const Json *behaviorCatalog,
                          const std::vector<Json> &authorReferenceBindings,
                          const std::vector<TemplateAuthorSchemaDeclaration> &authorSchemaDeclarations,
                          bool deleteKeyPressed,
                          bool mouseButtonEventThisFrame)
{
    semanticVisibleRows_.clear();
    selection.repair(document);
    if (cachedGeneration_ != document.documentGeneration)
    {
        lastRevealedObjectId_.reset();
        pendingScrollObjectId_.reset();
    }
    const auto focus_hierarchy = [&]() {
        if (selection.current().focusedPanel == FocusedPanel::Hierarchy)
            return;
        Selection focused = selection.current();
        focused.focusedPanel = FocusedPanel::Hierarchy;
        selection.set(document, std::move(focused));
    };
    if (renameTargetObjectId_ != 0 &&
        (!document.indexes.objectsById.contains(renameTargetObjectId_) ||
         selection.current().objectId != renameTargetObjectId_))
    {
        renameTargetObjectId_ = 0;
        renameBuffer_.clear();
        renameFocusPending_ = false;
    }
    const ImGuiIO &io = ImGui::GetIO();
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool hierarchyFocused = windowFocused &&
                                  selection.current().focusedPanel == FocusedPanel::Hierarchy;
    const bool modalOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool activeGesture = ImGui::GetDragDropPayload() != nullptr;
    const bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                              ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                              ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    const bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                               ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
                               ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
    const bool mouseInteractingThisFrame = mouseClicked || mouseReleased ||
                                           ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                                           ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                                           ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const int frameCount = ImGui::GetFrameCount();
    const bool mouseInteracting = mouseInteractingThisFrame ||
                                  lastMouseInteractionFrame_ == frameCount - 1;
    if (mouseInteractingThisFrame)
        lastMouseInteractionFrame_ = frameCount;
    const EditorInputCapture keyboardCapture{true, io.WantTextInput || ImGui::IsAnyItemActive(),
        modalOpen, activeGesture, false, hierarchyFocused};
    const auto start_rename = [&]() {
        const auto objectId = selection.current().objectId;
        if (!objectId || selection.current().componentId)
            return;
        const auto location = document.indexes.objectsById.find(*objectId);
        const Json *object = location == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, location->second);
        if (object == nullptr)
            return;
        const std::string name = object->value("name", std::string("GameObject"));
        renameBuffer_.assign(name.begin(), name.end());
        renameBuffer_.push_back('\0');
        renameTargetObjectId_ = *objectId;
        renameFocusPending_ = true;
    };
    if (editingAllowed && ImGui::IsKeyPressed(ImGuiKey_F2, false) &&
        dispatch_editor_key(EditorKey::F2, io.KeyShift, io.KeyCtrl,
                            keyboardCapture) == EditorAction::RenameSelection)
        start_rename();
    // The SDL edge verifies that Delete came from the focused host window;
    // ImGui's key edge verifies that it is still the current input event.
    // Requiring both prevents an unrelated mouse interaction from consuming
    // stale or misrouted destructive input.
    if (editingAllowed && deleteKeyPressed && ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        !mouseButtonEventThisFrame && !mouseInteracting &&
        dispatch_editor_key(EditorKey::Delete, io.KeyShift, io.KeyCtrl,
                            keyboardCapture) == EditorAction::DeleteSelection)
    {
        const Selection before = selection.current();
        std::vector<int> ids = before.selectedObjectIds;
        if (ids.empty() && before.objectId)
            ids.push_back(*before.objectId);
        const auto deleted = commands.delete_objects(document, ids, {before, before});
        if (deleted.status == CommandStatus::Changed)
        {
            selection.repair(document);
            commands.set_last_selection_state({before, selection.current()});
            invalidate();
        }
    }
    if (editingAllowed && ImGui::IsKeyPressed(ImGuiKey_D, false) &&
        dispatch_editor_key(EditorKey::D, io.KeyShift, io.KeyCtrl,
                            keyboardCapture) == EditorAction::DuplicateSelection)
    {
        const Selection before = selection.current();
        std::vector<int> ids = before.selectedObjectIds;
        if (ids.empty() && before.objectId)
            ids.push_back(*before.objectId);
        const bool remapReferences = duplicationReferencePolicy != nullptr &&
            *duplicationReferencePolicy == "remap-internal-targets";
        const auto bindingCoverage = remapReferences
            ? collect_duplicate_bindings(document, ids, behaviorCatalog,
                                         authorReferenceBindings, authorSchemaDeclarations)
            : DuplicateBindingSet{};
        const auto duplicated = commands.duplicate_objects(document, ids, {before, before},
            remapReferences ? DuplicateReferencePolicy::RemapInternalTargets
                            : DuplicateReferencePolicy::PreserveExistingTargets,
            bindingCoverage.bindings, !remapReferences || bindingCoverage.assured);
        duplicatePolicyStatus_ = duplicated.status == CommandStatus::Changed
            ? std::string{} : (!bindingCoverage.error.empty() ? bindingCoverage.error : duplicated.message);
        if (duplicated.status == CommandStatus::Changed && !duplicated.affectedObjectIds.empty())
        {
            selection.set_objects(document, duplicated.affectedObjectIds,
                                  duplicated.affectedObjectIds.back(), duplicated.affectedObjectIds.front());
            focus_hierarchy();
            commands.set_last_selection_state({before, selection.current()});
            invalidate();
        }
    }
    const auto commit_rename = [&]() {
        if (renameTargetObjectId_ == 0 || renameBuffer_.empty())
            return;
        const std::string name(renameBuffer_.data());
        const Selection before = selection.current();
        const auto result = commands.set_property(document,
            address_for_document(document, renameTargetObjectId_, std::nullopt, std::nullopt,
                                 {std::string("name")}), Json(name), {before, before});
        if (result.status == CommandStatus::Changed)
        {
            commands.set_last_selection_state({before, before});
            invalidate();
        }
        renameTargetObjectId_ = 0;
        renameBuffer_.clear();
        renameFocusPending_ = false;
    };
    const auto create_object = [&](std::optional<int> parentId)
    {
        const Selection before = selection.current();
        const auto result = commands.create_object(document, parentId, "GameObject");
        if (result.status == CommandStatus::Changed && result.command)
        {
            const int createdId = result.command->payload.value("id", 0);
            if (parentId)
                expandedObjects_.insert(*parentId);
            expandedObjects_.insert(createdId);
            selection.set(document, selected_object(createdId));
            commands.set_last_selection_state({before, selection.current()});
        }
        invalidate();
    };
    const auto add_component = [&](int objectId, int type)
    {
        const auto location = document.indexes.objectsById.find(objectId);
        const Json *object = location == document.indexes.objectsById.end()
            ? nullptr : resolve_object(document.root, location->second);
        if (object == nullptr || has_component_type(*object, type))
            return;
        const Selection before = selection.current();
        const auto result = commands.add_component(document, objectId, default_component_record(type));
        if (result.status == CommandStatus::Changed && result.command)
        {
            const int componentId = result.command->payload.value("id", 0);
            expandedObjects_.insert(objectId);
            collapsedComponentGroups_.erase(objectId);
            selection.set(document, Selection{objectId, componentId, std::nullopt,
                                              FocusedPanel::Hierarchy});
            commands.set_last_selection_state({before, selection.current()});
        }
        invalidate();
    };
    if (ImGui::Button("+ Object"))
        create_object(std::nullopt);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu objects", document.indexes.objectsById.size());
    if (!duplicatePolicyStatus_.empty())
        ImGui::TextWrapped("%s", duplicatePolicyStatus_.c_str());
    ImGui::Separator();

    std::array<char, 256> searchBuffer{};
    const std::size_t searchLength = std::min(searchQuery_.size(), searchBuffer.size() - 1);
    std::memcpy(searchBuffer.data(), searchQuery_.data(), searchLength);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##hierarchy-search", "Search objects",
                                 searchBuffer.data(), searchBuffer.size()))
        set_search_query(searchBuffer.data());
    update_search_matches(document);
    // Search expansion restores the user's pre-search state. Reveal the active
    // selection after that restoration so navigation from another panel remains
    // visible even when the current query does not match the selected object.
    if (selection.current().objectId &&
        lastRevealedObjectId_ != selection.current().objectId)
        reveal_object(document, *selection.current().objectId);
    else if (!selection.current().objectId)
        lastRevealedObjectId_.reset();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("HierarchyRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    const auto &rows = visible_rows(document);
    update_search_visibility(document, selection.current().objectId);
    const std::vector<int> &selectionOrder = searchQuery_.empty()
        ? displayedObjectOrder_ : searchResultObjectOrder_;
    constexpr float hierarchyIndent = 24.0f;
    const float treeBaseX = ImGui::GetCursorScreenPos().x;
    const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_TreeLines);
    const float lineThickness = std::max(1.0f, ImGui::GetStyle().TreeLinesSize);
    const ImVec2 firstRowPosition = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetFrameHeight();
    const float rowStride = rowHeight + ImGui::GetStyle().ItemSpacing.y;
    if (pendingScrollObjectId_)
    {
        const auto row = objectRowIndexes_.find(*pendingScrollObjectId_);
        if (row != objectRowIndexes_.end())
        {
            const float targetTop = static_cast<float>(row->second) * rowStride;
            const float scrollTop = ImGui::GetScrollY();
            const float viewHeight = ImGui::GetWindowHeight();
            const float margin = rowHeight;
            const bool above = targetTop < scrollTop + margin;
            const bool below = targetTop + rowHeight > scrollTop + viewHeight - margin;
            if (above || below)
            {
                const float maxScroll = std::max(0.0f, ImGui::GetScrollMaxY());
                const float target = targetTop - margin;
                ImGui::SetScrollY(std::clamp(target, 0.0f, maxScroll));
            }
            pendingScrollObjectId_.reset();
        }
    }
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 clipMinimum = drawList->GetClipRectMin();
    const ImVec2 clipMaximum = drawList->GetClipRectMax();
    const int firstVisibleRow = std::max(0, static_cast<int>(std::floor(
        (clipMinimum.y - firstRowPosition.y) / rowStride)) - 1);
    const int lastVisibleRow = std::min(static_cast<int>(rows.size()), static_cast<int>(std::ceil(
        (clipMaximum.y - firstRowPosition.y) / rowStride)) + 1);
    const auto candidateCapacity = connectorCandidates_.capacity();
    connectorCandidates_.clear();
    if (++connectorVisitGeneration_ == 0)
    {
        std::fill(connectorVisitMarks_.begin(), connectorVisitMarks_.end(), 0);
        connectorVisitGeneration_ = 1;
    }
    for (int rowIndex = firstVisibleRow; rowIndex < lastVisibleRow; ++rowIndex)
    {
        if (!searchVisibleRows_[static_cast<std::size_t>(rowIndex)])
            continue;
        int parentIndex = rows[static_cast<std::size_t>(rowIndex)].parentRowIndex;
        while (parentIndex >= 0)
        {
            work::record(work::Counter::HierarchyConnectorParentVisits);
            const auto parentSlot = static_cast<std::size_t>(parentIndex);
            if (connectorVisitMarks_[parentSlot] != connectorVisitGeneration_)
            {
                connectorVisitMarks_[parentSlot] = connectorVisitGeneration_;
                connectorCandidates_.push_back(parentIndex);
            }
            parentIndex = rows[parentSlot].parentRowIndex;
        }
    }
    if (connectorCandidates_.capacity() != candidateCapacity)
        work::record(work::Counter::HierarchyScratchCapacityGrowths);
    work::record(work::Counter::HierarchyConnectorCandidates, connectorCandidates_.size());
    for (const int parentIndex : connectorCandidates_)
    {
        const auto parentSlot = static_cast<std::size_t>(parentIndex);
        if (!searchVisibleRows_[parentSlot])
            continue;
        const int firstChild = firstChildRows_[parentSlot];
        const int lastChild = lastChildRows_[parentSlot];
        if (firstChild < 0 || lastChild < firstVisibleRow || firstChild >= lastVisibleRow)
            continue;
        const auto &parent = rows[parentSlot];
        const float parentX = treeBaseX + static_cast<float>(parent.depth) * hierarchyIndent + 8.0f;
        const float parentCenterY = firstRowPosition.y + static_cast<float>(parentIndex) * rowStride +
                                    rowHeight * 0.5f;
        const float lastChildCenterY = firstRowPosition.y + static_cast<float>(lastChild) * rowStride +
                                       rowHeight * 0.5f;
        if (lastChildCenterY < clipMinimum.y || parentCenterY > clipMaximum.y)
            continue;
        drawList->AddLine({parentX, parentCenterY}, {parentX, lastChildCenterY},
                          lineColor, lineThickness);
        work::record(work::Counter::HierarchyConnectors);
    }
    for (int rowIndex = firstVisibleRow; rowIndex < lastVisibleRow; ++rowIndex)
    {
        if (!searchVisibleRows_[static_cast<std::size_t>(rowIndex)])
            continue;
        const auto &row = rows[static_cast<std::size_t>(rowIndex)];
        const int parentIndex = row.parentRowIndex;
        if (parentIndex < 0)
            continue;
        const auto &parent = rows[static_cast<std::size_t>(parentIndex)];
        const auto rowX = treeBaseX + static_cast<float>(row.depth) * hierarchyIndent +
                          (row.kind == HierarchyRowKind::Component ? 2.0f : 8.0f);
        const float parentX = treeBaseX + static_cast<float>(parent.depth) * hierarchyIndent + 8.0f;
        const float rowCenterY = firstRowPosition.y + static_cast<float>(rowIndex) * rowStride + rowHeight * 0.5f;
        drawList->AddLine({parentX, rowCenterY}, {rowX, rowCenterY}, lineColor, lineThickness);
        work::record(work::Counter::HierarchyConnectors);
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    bool structureChanged = false;
    while (clipper.Step())
    {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
        {
            work::record(work::Counter::HierarchyVisibleRows);
            const auto &row = rows[static_cast<std::size_t>(rowIndex)];
            if (!searchVisibleRows_[static_cast<std::size_t>(rowIndex)])
            {
                ImGui::Dummy({1.0f, rowHeight});
                continue;
            }
            ImGui::PushID(row.id);
            ImGui::PushID(static_cast<int>(row.kind));
            ImGui::Indent(static_cast<float>(row.depth) * hierarchyIndent);
            SemanticBounds semanticBounds;
            bool hasSemanticBounds = false;
            if (row.kind == HierarchyRowKind::Object)
            {
                const auto &currentSelection = selection.current();
                const bool selected = !currentSelection.componentId &&
                    (currentSelection.objectId == row.id ||
                     std::find(currentSelection.selectedObjectIds.begin(),
                               currentSelection.selectedObjectIds.end(), row.id) !=
                         currentSelection.selectedObjectIds.end());
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (selected)
                    flags |= ImGuiTreeNodeFlags_Selected;
                if (!row.hasChildren)
                    flags |= ImGuiTreeNodeFlags_Leaf;
                const bool wasExpanded = expandedObjects_.contains(row.id);
                ImGui::SetNextItemOpen(wasExpanded, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx(row.label.c_str(), flags);
                const ImVec2 rowMinimum = ImGui::GetItemRectMin();
                const ImVec2 rowMaximum = ImGui::GetItemRectMax();
                semanticBounds = {rowMinimum.x, rowMinimum.y,
                                  rowMaximum.x - rowMinimum.x,
                                  rowMaximum.y - rowMinimum.y};
                hasSemanticBounds = true;
                const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
                const float iconSize = 14.0f * uiScale;
                const float iconX = rowMinimum.x + ImGui::GetTreeNodeToLabelSpacing() + 3.0f * uiScale;
                const float iconY = rowMinimum.y + (rowMaximum.y - rowMinimum.y - iconSize) * 0.5f;
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                draw_editor_icon(EditorIcon::GameObject, drawList,
                                 {rowMaximum.x - iconSize - 4.0f, iconY}, iconSize,
                                 ImGui::GetColorU32(ImGuiCol_Text));
                if (row.hasChildren)
                {
                    if (open != wasExpanded)
                    {
                        if (open)
                            expandedObjects_.insert(row.id);
                        else
                            expandedObjects_.erase(row.id);
                        invalidate();
                    }
                }
                if (ImGui::IsItemClicked())
                {
                    const ImGuiIO &io = ImGui::GetIO();
                    if (io.KeyShift)
                        selection.select_range(document, row.id, selectionOrder, io.KeyCtrl);
                    else if (io.KeyCtrl)
                        selection.toggle_object(document, row.id);
                    else
                        selection.set_objects(document, {row.id}, row.id, row.id);
                    // The clicked row is already visible; do not ask for a reveal/scroll.
                    lastRevealedObjectId_ = row.id;
                    focus_hierarchy();
                }
                else if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))
                {
                    selection.set_objects(document, {row.id}, row.id, row.id);
                    lastRevealedObjectId_ = row.id;
                    focus_hierarchy();
                }
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("PLATFORMATOR_HIERARCHY_OBJECT", &row.id, sizeof(row.id));
                    ImGui::TextUnformatted(row.label.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
                        "PLATFORMATOR_HIERARCHY_OBJECT");
                    if (payload != nullptr && payload->Data != nullptr && payload->DataSize == sizeof(int))
                    {
                        int draggedObjectId = 0;
                        std::memcpy(&draggedObjectId, payload->Data, sizeof(draggedObjectId));
                        const auto &current = selection.current();
                        std::vector<int> moveIds = current.selectedObjectIds;
                        if (std::find(moveIds.begin(), moveIds.end(), draggedObjectId) == moveIds.end())
                            moveIds = {draggedObjectId};

                        const float dragTargetHeight = std::max(1.0f, rowMaximum.y - rowMinimum.y);
                        const float relativeY = ImGui::GetMousePos().y - rowMinimum.y;
                        std::optional<int> destinationParent;
                        std::size_t destinationIndex = std::numeric_limits<std::size_t>::max();
                        const auto location = document.indexes.objectsById.find(row.id);
                        const auto parent = document.indexes.parentById.find(row.id);
                        if (relativeY < dragTargetHeight * 0.25f && location != document.indexes.objectsById.end() &&
                            parent != document.indexes.parentById.end())
                        {
                            destinationParent = parent->second;
                            destinationIndex = location->second.childIndices.back();
                        }
                        else if (relativeY > dragTargetHeight * 0.75f &&
                                 location != document.indexes.objectsById.end() &&
                                 parent != document.indexes.parentById.end())
                        {
                            destinationParent = parent->second;
                            destinationIndex = location->second.childIndices.back() + 1;
                        }
                        else
                            destinationParent = row.id;

                        const auto moved = commands.move_objects(document, moveIds, destinationParent,
                            destinationIndex, {current, current});
                        if (moved.status == CommandStatus::Changed)
                        {
                            invalidate();
                            structureChanged = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("Object actions"))
                {
                    if (ImGui::MenuItem("Create Child"))
                        create_object(row.id);
                    if (hiddenObjectIds != nullptr)
                    {
                        const bool hidden = std::binary_search(hiddenObjectIds->begin(),
                                                               hiddenObjectIds->end(), row.id);
                        if (ImGui::MenuItem(hidden ? "Show in Scene" : "Hide in Scene", nullptr, hidden))
                            set_editor_object_id_state(*hiddenObjectIds, row.id, !hidden);
                    }
                    if (lockedObjectIds != nullptr)
                    {
                        const bool locked = std::binary_search(lockedObjectIds->begin(),
                                                               lockedObjectIds->end(), row.id);
                        if (ImGui::MenuItem(locked ? "Unlock Scene Picking" : "Lock Scene Picking",
                                            nullptr, locked))
                            set_editor_object_id_state(*lockedObjectIds, row.id, !locked);
                    }
                    if (ImGui::BeginMenu("Add Component", std::any_of(
                            kComponentChoices.begin(), kComponentChoices.end(), [&](const auto &choice) {
                                const auto location = document.indexes.objectsById.find(row.id);
                                const Json *object = location == document.indexes.objectsById.end()
                                    ? nullptr : resolve_object(document.root, location->second);
                                return object != nullptr && !has_component_type(*object, choice.first);
                            })))
                    {
                        const auto location = document.indexes.objectsById.find(row.id);
                        const Json *object = location == document.indexes.objectsById.end()
                            ? nullptr : resolve_object(document.root, location->second);
                        for (const auto &[type, label] : kComponentChoices)
                        {
                            if (object != nullptr && has_component_type(*object, type))
                                continue;
                            if (ImGui::MenuItem(label))
                            {
                                add_component(row.id, type);
                                break;
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Duplicate"))
                    {
                        const Selection before = selection.current();
                        const bool remapReferences = duplicationReferencePolicy != nullptr &&
                            *duplicationReferencePolicy == "remap-internal-targets";
                        const auto bindingCoverage = remapReferences
                            ? collect_duplicate_bindings(document, {row.id}, behaviorCatalog,
                                                         authorReferenceBindings,
                                                         authorSchemaDeclarations)
                            : DuplicateBindingSet{};
                        const auto duplicated = commands.duplicate_object(document, row.id,
                            {before, before},
                            remapReferences ? DuplicateReferencePolicy::RemapInternalTargets
                                            : DuplicateReferencePolicy::PreserveExistingTargets,
                            bindingCoverage.bindings, !remapReferences || bindingCoverage.assured);
                        duplicatePolicyStatus_ = duplicated.status == CommandStatus::Changed
                            ? std::string{} : (!bindingCoverage.error.empty()
                                ? bindingCoverage.error : duplicated.message);
                        if (duplicated.status == CommandStatus::Changed && duplicated.command)
                        {
                            selection.set(document, selected_object(duplicated.command->payload.value("id", 0)));
                            commands.set_last_selection_state({before, selection.current()});
                        }
                        invalidate();
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        const Selection before = selection.current();
                        const auto deleted = commands.delete_object(document, row.id);
                        selection.repair(document);
                        if (deleted.status == CommandStatus::Changed)
                            commands.set_last_selection_state({before, selection.current()});
                        invalidate();
                    }
                    ImGui::EndPopup();
                }
                if (row.id == renameTargetObjectId_ && !renameBuffer_.empty())
                {
                    ImGui::SetCursorScreenPos({iconX + iconSize + 5.0f * uiScale,
                        rowMinimum.y + (rowMaximum.y - rowMinimum.y - ImGui::GetFrameHeight()) * 0.5f});
                    ImGui::SetNextItemWidth(std::max(48.0f * uiScale,
                        rowMaximum.x - (iconX + iconSize + 5.0f * uiScale)));
                    if (renameFocusPending_)
                    {
                        ImGui::SetKeyboardFocusHere();
                        renameFocusPending_ = false;
                    }
                    const bool submit = ImGui::InputText("##hierarchy-rename", renameBuffer_.data(),
                        renameBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue |
                            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackResize,
                        resize_rename_buffer, &renameBuffer_);
                    if (submit)
                        commit_rename();
                    else if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
                    {
                        renameTargetObjectId_ = 0;
                        renameBuffer_.clear();
                        renameFocusPending_ = false;
                    }
                }
            }
            else if (row.kind == HierarchyRowKind::ComponentsNode)
            {
                const bool wasExpanded = !collapsedComponentGroups_.contains(row.ownerObjectId);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
                ImGui::SetNextItemOpen(wasExpanded, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx(row.label.c_str(), flags);
                const ImVec2 rowMinimum = ImGui::GetItemRectMin();
                const ImVec2 rowMaximum = ImGui::GetItemRectMax();
                semanticBounds = {rowMinimum.x, rowMinimum.y,
                                  rowMaximum.x - rowMinimum.x,
                                  rowMaximum.y - rowMinimum.y};
                hasSemanticBounds = true;
                if (open != wasExpanded)
                {
                    if (open)
                        collapsedComponentGroups_.erase(row.ownerObjectId);
                    else
                        collapsedComponentGroups_.insert(row.ownerObjectId);
                    invalidate();
                }
                if (ImGui::IsItemClicked())
                {
                    selection.set(document, selected_object(row.ownerObjectId));
                    lastRevealedObjectId_ = row.ownerObjectId;
                }
                else if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))
                {
                    selection.set(document, selected_object(row.ownerObjectId));
                    lastRevealedObjectId_ = row.ownerObjectId;
                }
            }
            else
            {
                const bool selected = selection.current().componentId == row.id;
                if (ImGui::Selectable(row.label.c_str(), selected))
                {
                    selection.set(document, Selection{row.ownerObjectId, row.id, std::nullopt,
                                                      FocusedPanel::Hierarchy});
                    lastRevealedObjectId_ = row.ownerObjectId;
                }
                const ImVec2 rowMinimum = ImGui::GetItemRectMin();
                const ImVec2 rowMaximum = ImGui::GetItemRectMax();
                semanticBounds = {rowMinimum.x, rowMinimum.y,
                                  rowMaximum.x - rowMinimum.x,
                                  rowMaximum.y - rowMinimum.y};
                hasSemanticBounds = true;
                if (ImGui::BeginPopupContextItem("Component actions"))
                {
                    if (ImGui::MenuItem("Remove Component"))
                    {
                        const Selection before = selection.current();
                        const auto removed = commands.remove_component(document, row.ownerObjectId, row.id);
                        selection.repair(document);
                        if (removed.status == CommandStatus::Changed)
                            commands.set_last_selection_state({before, selection.current()});
                        invalidate();
                    }
                    ImGui::EndPopup();
                }
            }
            if (hasSemanticBounds)
                semanticVisibleRows_.push_back({rowIndex, semanticBounds});
            ImGui::Unindent(static_cast<float>(row.depth) * hierarchyIndent);
            ImGui::PopID();
            ImGui::PopID();
            if (structureChanged)
                break;
        }
        if (structureChanged)
            break;
    }
    if (ImGui::BeginPopupContextWindow("Hierarchy empty-space actions",
                                        ImGuiPopupFlags_MouseButtonRight |
                                            ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("Create Empty Object"))
            create_object(std::nullopt);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace platformator_editor
