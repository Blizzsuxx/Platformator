#include "hierarchy.h"

#include "../componenttypes.h"
#include "../style.h"

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
    return component_label(found->get<int>());
}

EditorIcon component_icon(const Json &component)
{
    const auto found = component.find("type");
    if (found == component.end() || !(found->is_number_integer() || found->is_number_unsigned()))
        return EditorIcon::UnknownComponent;
    switch (found->get<int>())
    {
    case 0:
        return EditorIcon::Animator;
    case 1:
        return EditorIcon::Audio;
    case 2:
        return EditorIcon::Camera;
    case 3:
        return EditorIcon::Collider;
    case 4:
        return EditorIcon::Rigidbody;
    case 5:
        return EditorIcon::Script;
    case 6:
        return EditorIcon::Sprite;
    default:
        return EditorIcon::UnknownComponent;
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
        collapsedComponentGroups_.clear();
        for (const auto &object : document.root)
        {
            if (!object.is_object())
                continue;
            const int id = object.value("id", 0);
            const auto children = object.find("children");
            const auto components = object.find("components");
            const bool hasChildren = children != object.end() && children->is_array() && !children->empty();
            const bool hasComponents = components != object.end() && components->is_array() && !components->empty();
            if (hasChildren || hasComponents)
                expandedObjects_.insert(id);
        }
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
                                     current.depth, name, hasChildren,
                                     current.parentRowIndex});
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
    cachedGeneration_ = document.documentGeneration;
    cachedStructureRevision_ = document.structureRevision;
    cacheValid_ = true;
}

void HierarchyPanel::refresh_labels(const DocumentSession &document)
{
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
            collapsedComponentGroups_.erase(objectId);
            selection.set(document, Selection{objectId, componentId, std::nullopt,
                                              FocusedPanel::Hierarchy});
            commands.set_last_selection_state({before, selection.current()});
        }
        invalidate();
    };
    if (ImGui::Button("+ Root Object"))
        create_object(std::nullopt);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu objects", document.indexes.objectsById.size());
    ImGui::Separator();

    const auto &rows = visible_rows(document);
    constexpr float hierarchyIndent = 24.0f;
    const float treeBaseX = ImGui::GetCursorScreenPos().x;
    const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_TreeLines);
    const float lineThickness = std::max(1.0f, ImGui::GetStyle().TreeLinesSize);
    const ImVec2 firstRowPosition = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetFrameHeight();
    const float rowStride = rowHeight + ImGui::GetStyle().ItemSpacing.y;
    std::vector<int> firstChild(rows.size(), -1);
    std::vector<int> lastChild(rows.size(), -1);
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const int parentIndex = rows[rowIndex].parentRowIndex;
        if (parentIndex < 0)
            continue;
        auto &first = firstChild[static_cast<std::size_t>(parentIndex)];
        if (first < 0)
            first = static_cast<int>(rowIndex);
        lastChild[static_cast<std::size_t>(parentIndex)] = static_cast<int>(rowIndex);
    }
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    for (std::size_t parentIndex = 0; parentIndex < rows.size(); ++parentIndex)
    {
        if (firstChild[parentIndex] < 0)
            continue;
        const auto &parent = rows[parentIndex];
        const float parentX = treeBaseX + static_cast<float>(parent.depth) * hierarchyIndent + 8.0f;
        const float parentCenterY = firstRowPosition.y + static_cast<float>(parentIndex) * rowStride +
                                    rowHeight * 0.5f;
        const float lastChildCenterY = firstRowPosition.y +
                                       static_cast<float>(lastChild[parentIndex]) * rowStride + rowHeight * 0.5f;
        drawList->AddLine({parentX, parentCenterY}, {parentX, lastChildCenterY},
                          lineColor, lineThickness);
    }
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const int parentIndex = rows[rowIndex].parentRowIndex;
        if (parentIndex < 0)
            continue;
        const auto &parent = rows[static_cast<std::size_t>(parentIndex)];
        const auto rowX = treeBaseX + static_cast<float>(rows[rowIndex].depth) * hierarchyIndent +
                          (rows[rowIndex].kind == HierarchyRowKind::Component ? 2.0f : 8.0f);
        const float parentX = treeBaseX + static_cast<float>(parent.depth) * hierarchyIndent + 8.0f;
        const float rowCenterY = firstRowPosition.y + static_cast<float>(rowIndex) * rowStride + rowHeight * 0.5f;
        drawList->AddLine({parentX, rowCenterY}, {rowX, rowCenterY}, lineColor, lineThickness);
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step())
    {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
        {
            const auto &row = rows[static_cast<std::size_t>(rowIndex)];
            ImGui::PushID(row.id);
            ImGui::PushID(static_cast<int>(row.kind));
            ImGui::Indent(static_cast<float>(row.depth) * hierarchyIndent);
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
                const bool wasExpanded = expandedObjects_.contains(row.id);
                ImGui::SetNextItemOpen(wasExpanded, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx("##hierarchy-object", flags, "");
                const ImVec2 rowMinimum = ImGui::GetItemRectMin();
                const ImVec2 rowMaximum = ImGui::GetItemRectMax();
                const float iconSize = 14.0f;
                const float iconX = rowMinimum.x + ImGui::GetTreeNodeToLabelSpacing() + 3.0f;
                const float iconY = rowMinimum.y + (rowMaximum.y - rowMinimum.y - iconSize) * 0.5f;
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                draw_editor_icon(EditorIcon::GameObject, drawList, {iconX, iconY}, iconSize,
                                 ImGui::GetColorU32(ImGuiCol_Text));
                const ImVec2 textSize = ImGui::CalcTextSize(row.label.c_str());
                drawList->AddText({iconX + iconSize + 5.0f,
                                   rowMinimum.y + (rowMaximum.y - rowMinimum.y - textSize.y) * 0.5f},
                                  ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str());
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
                    selection.set(document, selected_object(row.id));
                if (ImGui::BeginPopupContextItem("Object actions"))
                {
                    if (ImGui::MenuItem("Create Child"))
                        create_object(row.id);
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
            else if (row.kind == HierarchyRowKind::ComponentsNode)
            {
                const bool wasExpanded = !collapsedComponentGroups_.contains(row.ownerObjectId);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
                ImGui::SetNextItemOpen(wasExpanded, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx("##hierarchy-components", flags, "");
                const ImVec2 rowMinimum = ImGui::GetItemRectMin();
                const ImVec2 rowMaximum = ImGui::GetItemRectMax();
                const float iconSize = 14.0f;
                const float iconX = rowMinimum.x + ImGui::GetTreeNodeToLabelSpacing() + 3.0f;
                const float iconY = rowMinimum.y + (rowMaximum.y - rowMinimum.y - iconSize) * 0.5f;
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                draw_editor_icon(EditorIcon::Folder, drawList, {iconX, iconY}, iconSize,
                                 ImGui::GetColorU32(ImGuiCol_Text));
                const ImVec2 textSize = ImGui::CalcTextSize(row.label.c_str());
                drawList->AddText({iconX + iconSize + 5.0f,
                                   rowMinimum.y + (rowMaximum.y - rowMinimum.y - textSize.y) * 0.5f},
                                  ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str());
                if (open != wasExpanded)
                {
                    if (open)
                        collapsedComponentGroups_.erase(row.ownerObjectId);
                    else
                        collapsedComponentGroups_.insert(row.ownerObjectId);
                    invalidate();
                }
                if (ImGui::IsItemClicked())
                    selection.set(document, selected_object(row.ownerObjectId));
            }
            else
            {
                const bool selected = selection.current().componentId == row.id;
                if (ImGui::Selectable("##hierarchy-component", selected))
                    selection.set(document, Selection{row.ownerObjectId, row.id, std::nullopt,
                                                      FocusedPanel::Hierarchy});
                const ImVec2 rowMinimum = ImGui::GetItemRectMin();
                const ImVec2 rowMaximum = ImGui::GetItemRectMax();
                const float iconSize = 14.0f;
                const float iconX = rowMinimum.x + 4.0f;
                const float iconY = rowMinimum.y + (rowMaximum.y - rowMinimum.y - iconSize) * 0.5f;
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                const auto ownerLocation = document.indexes.objectsById.find(row.ownerObjectId);
                const Json *owner = ownerLocation == document.indexes.objectsById.end()
                    ? nullptr : resolve_object(document.root, ownerLocation->second);
                EditorIcon icon = EditorIcon::UnknownComponent;
                if (owner != nullptr)
                {
                    const auto components = owner->find("components");
                    const auto componentLocation = document.indexes.componentsById.find(row.id);
                    if (components != owner->end() && components->is_array() &&
                        componentLocation != document.indexes.componentsById.end() &&
                        componentLocation->second.componentIndex < components->size())
                        icon = component_icon((*components)[componentLocation->second.componentIndex]);
                }
                draw_editor_icon(icon, drawList, {iconX, iconY}, iconSize,
                                 ImGui::GetColorU32(ImGuiCol_Text));
                const ImVec2 textSize = ImGui::CalcTextSize(row.label.c_str());
                drawList->AddText({iconX + iconSize + 5.0f,
                                   rowMinimum.y + (rowMaximum.y - rowMinimum.y - textSize.y) * 0.5f},
                                  ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str());
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
            ImGui::Unindent(static_cast<float>(row.depth) * hierarchyIndent);
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    if (ImGui::BeginPopupContextWindow("Hierarchy empty-space actions",
                                        ImGuiPopupFlags_MouseButtonRight |
                                            ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("Create Empty Object"))
            create_object(std::nullopt);
        ImGui::EndPopup();
    }
}
} // namespace platformator_editor
