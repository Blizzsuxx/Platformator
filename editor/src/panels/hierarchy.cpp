#include "hierarchy.h"

#include <algorithm>
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
    const int type = found->get<int>();
    switch (type)
    {
    case 0:
        return "Animator";
    case 1:
        return "Audio";
    case 2:
        return "Camera";
    case 3:
        return "Collider";
    case 4:
        return "Rigidbody";
    case 5:
        return "Script";
    case 6:
        return "Sprite";
    default:
        return "Unknown Component";
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
} // namespace

const std::vector<HierarchyRow> &HierarchyPanel::visible_rows(const DocumentSession &document)
{
    if (!cacheValid_ || cachedGeneration_ != document.documentGeneration ||
        cachedStructureRevision_ != document.structureRevision)
        rebuild(document);
    refresh_labels(document);
    return rows_;
}

void HierarchyPanel::invalidate() noexcept
{
    cacheValid_ = false;
}

void HierarchyPanel::rebuild(const DocumentSession &document)
{
    rows_.clear();
    if (!initializedExpansion_ || cachedGeneration_ != document.documentGeneration)
    {
        expandedObjects_.clear();
        for (const auto &object : document.root)
        {
            if (!object.is_object())
                continue;
            const int id = object.value("id", 0);
            const auto children = object.find("children");
            const auto components = object.find("components");
            if ((children != object.end() && children->is_array() && !children->empty()) ||
                (components != object.end() && components->is_array() && !components->empty()))
                expandedObjects_.insert(id);
        }
        initializedExpansion_ = true;
    }

    for (auto it = expandedObjects_.begin(); it != expandedObjects_.end();)
    {
        if (!document.indexes.objectsById.contains(*it))
            it = expandedObjects_.erase(it);
        else
            ++it;
    }

    struct Pending
    {
        const Json *value = nullptr;
        int ownerObjectId = 0;
        std::size_t depth = 0;
        bool component = false;
    };
    std::vector<Pending> pending;
    pending.reserve(document.indexes.objectsById.size() + document.indexes.componentsById.size());
    for (std::size_t index = document.root.size(); index > 0; --index)
        pending.push_back({&document.root[index - 1], 0, 0, false});

    while (!pending.empty())
    {
        const Pending current = pending.back();
        pending.pop_back();
        if (current.value == nullptr || !current.value->is_object())
            continue;
        if (current.component)
        {
            const int componentId = current.value->value("id", 0);
            rows_.push_back(HierarchyRow{HierarchyRowKind::Component, componentId,
                                         current.ownerObjectId, current.depth,
                                         component_name(*current.value), false});
            continue;
        }

        const int objectId = current.value->value("id", 0);
        const auto name = current.value->value("name", std::string("GameObject"));
        const auto children = current.value->find("children");
        const auto components = current.value->find("components");
        const bool hasChildren = (children != current.value->end() && children->is_array() && !children->empty()) ||
                                 (components != current.value->end() && components->is_array() && !components->empty());
        rows_.push_back(HierarchyRow{HierarchyRowKind::Object, objectId, objectId,
                                     current.depth, name, hasChildren});
        if (!expandedObjects_.contains(objectId))
            continue;

        if (children != current.value->end() && children->is_array())
        {
            for (std::size_t index = children->size(); index > 0; --index)
                pending.push_back({&(*children)[index - 1], objectId, current.depth + 1, false});
        }
        if (components != current.value->end() && components->is_array())
        {
            for (std::size_t index = components->size(); index > 0; --index)
                pending.push_back({&(*components)[index - 1], objectId, current.depth + 1, true});
        }
    }
    cachedGeneration_ = document.documentGeneration;
    cachedStructureRevision_ = document.structureRevision;
    cacheValid_ = true;
}

void HierarchyPanel::refresh_labels(const DocumentSession &document)
{
    for (auto &row : rows_)
    {
        if (row.kind == HierarchyRowKind::Object)
        {
            const auto location = document.indexes.objectsById.find(row.id);
            if (location == document.indexes.objectsById.end())
                continue;
            const Json *object = resolve_object(document.root, location->second);
            if (object != nullptr && object->is_object())
                row.label = object->value("name", std::string("GameObject"));
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
}

void HierarchyPanel::draw(DocumentSession &document, SelectionModel &selection,
                          EditCommandService &commands)
{
    selection.repair(document);
    if (ImGui::Button("+ Root Object"))
    {
        const Selection before = selection.current();
        const auto result = commands.create_object(document, std::nullopt, "GameObject");
        if (result.status == CommandStatus::Changed && result.command)
        {
            selection.set(document, selected_object(result.command->payload.value("id", 0)));
            commands.set_last_selection_state({before, selection.current()});
        }
        invalidate();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu objects", document.indexes.objectsById.size());
    ImGui::Separator();

    const auto &rows = visible_rows(document);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step())
    {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
        {
            const auto &row = rows[static_cast<std::size_t>(rowIndex)];
            ImGui::PushID(row.id);
            ImGui::Indent(static_cast<float>(row.depth) * 14.0f);
            if (row.kind == HierarchyRowKind::Object)
            {
                const bool selected = selection.current().objectId == row.id &&
                                      !selection.current().componentId;
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (selected)
                    flags |= ImGuiTreeNodeFlags_Selected;
                if (!row.hasChildren)
                    flags |= ImGuiTreeNodeFlags_Leaf;
                ImGui::SetNextItemOpen(expandedObjects_.contains(row.id), ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx("object", flags, "%s", row.label.c_str());
                if (row.hasChildren)
                {
                    if (open)
                        expandedObjects_.insert(row.id);
                    else
                        expandedObjects_.erase(row.id);
                }
                if (ImGui::IsItemClicked())
                    selection.set(document, selected_object(row.id));
                if (ImGui::BeginPopupContextItem("Object actions"))
                {
                    if (ImGui::MenuItem("Create Child"))
                    {
                        const Selection before = selection.current();
                        const auto created = commands.create_object(document, row.id, "GameObject");
                        if (created.status == CommandStatus::Changed && created.command)
                        {
                            expandedObjects_.insert(row.id);
                            selection.set(document, selected_object(created.command->payload.value("id", 0)));
                            commands.set_last_selection_state({before, selection.current()});
                        }
                        invalidate();
                    }
                    if (ImGui::MenuItem("Duplicate"))
                    {
                        const Selection before = selection.current();
                        const auto duplicated = commands.duplicate_object(document, row.id);
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
            }
            else
            {
                const bool selected = selection.current().componentId == row.id;
                if (ImGui::Selectable(row.label.c_str(), selected))
                    selection.set(document, Selection{row.ownerObjectId, row.id, std::nullopt,
                                                      FocusedPanel::Hierarchy});
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
            ImGui::Unindent(static_cast<float>(row.depth) * 14.0f);
            ImGui::PopID();
        }
    }
}
} // namespace platformator_editor
