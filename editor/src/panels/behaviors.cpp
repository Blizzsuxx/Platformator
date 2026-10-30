#include "behaviors.h"

#include <algorithm>
#include <string>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
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

const Json *selected_script_component(const DocumentSession &document, const Selection &selection)
{
    if (!selection.objectId || !selection.componentId)
        return nullptr;
    const auto objectLocation = document.indexes.objectsById.find(*selection.objectId);
    const auto componentLocation = document.indexes.componentsById.find(*selection.componentId);
    if (objectLocation == document.indexes.objectsById.end() ||
        componentLocation == document.indexes.componentsById.end() ||
        componentLocation->second.ownerObjectId != *selection.objectId)
        return nullptr;
    const Json *object = resolve_object(document.root, objectLocation->second);
    if (object == nullptr)
        return nullptr;
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        componentLocation->second.componentIndex >= components->size())
        return nullptr;
    const auto &component = (*components)[componentLocation->second.componentIndex];
    return component.is_object() && component.value("type", -1) == 5 ? &component : nullptr;
}
} // namespace

void BehaviorsPanel::draw(DocumentSession &document, SelectionModel &selection,
                          EditCommandService &commands, const BehaviorCatalog &catalog,
                          const AssetBrowseCallback &browse)
{
    selection.repair(document);
    const Json *script = selected_script_component(document, selection.current());
    if (script == nullptr || !selection.current().objectId || !selection.current().componentId)
    {
        ImGui::TextDisabled("Select a Script component in the Hierarchy or Inspector.");
        return;
    }

    if (!catalog.can_query())
        ImGui::TextDisabled("Catalog protocol unavailable; saved behaviors remain editable and build-only is available. A custom runner needs explicit common-runner-v1 catalog export integration before Run.");
    else if (catalog.state() == CatalogState::Stale)
        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.25f, 1.0f),
                           "Catalog is stale; known types are shown for editing only.");
    else if (catalog.state() == CatalogState::Unavailable)
        ImGui::TextDisabled("Catalog has not been verified for the selected executable.");

    const auto snapshot = catalog.snapshot();
    if (snapshot)
    {
        const auto registered = snapshot->document.find("behaviors");
        if (registered != snapshot->document.end() && registered->is_array())
        {
            bool addedBehavior = false;
            if (ImGui::BeginCombo("Add Registered Behavior", "Choose a registered type..."))
            {
                for (const auto &entry : *registered)
                {
                    const std::string name = entry.value("name", std::string{});
                    if (name.empty())
                        continue;
                    ImGui::PushID(name.c_str());
                    const bool schemaAvailable = entry.value("field_schema_available", false);
                    const std::string label = schemaAvailable ? name : name + " (schema unavailable)";
                    if (ImGui::Selectable(label.c_str()))
                    {
                        const Selection before = selection.current();
                        const auto attached = script->find("behaviors");
                        const std::size_t ordinal = attached != script->end() && attached->is_array()
                                                        ? attached->size()
                                                        : 0;
                        const auto result = commands.add_behavior(
                            document, *selection.current().objectId, *selection.current().componentId,
                            Json{{"type", name}});
                        if (result.status == CommandStatus::Changed)
                        {
                            selection.set(document, Selection{selection.current().objectId,
                                                              selection.current().componentId,
                                                              ordinal, FocusedPanel::Behaviors});
                            commands.set_last_selection_state({before, selection.current()});
                            addedBehavior = true;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (addedBehavior)
            {
                fields_.draw(document, selection, commands, catalog, browse);
                return;
            }
        }
    }

    const auto behaviors = script->find("behaviors");
    const bool hasSelected = behaviors != script->end() && behaviors->is_array() &&
                             selection.current().behaviorOrdinal &&
                             *selection.current().behaviorOrdinal < behaviors->size();
    if (hasSelected && ImGui::Button("Remove Selected Behavior"))
    {
        const auto objectId = *selection.current().objectId;
        const auto componentId = *selection.current().componentId;
        const auto oldOrdinal = *selection.current().behaviorOrdinal;
        const Selection before = selection.current();
        const std::size_t oldSize = behaviors->size();
        const auto result = commands.remove_behavior(document, objectId, componentId, oldOrdinal);
        if (result.status == CommandStatus::Changed)
        {
            const std::size_t newSize = oldSize - 1;
            if (newSize == 0)
                selection.set(document, Selection{objectId, componentId, std::nullopt,
                                                  FocusedPanel::Behaviors});
            else
                selection.set(document, Selection{objectId, componentId,
                                                  std::min(oldOrdinal, newSize - 1),
                                                  FocusedPanel::Behaviors});
            commands.set_last_selection_state({before, selection.current()});
            fields_.draw(document, selection, commands, catalog, browse);
            return;
        }
    }

    ImGui::Separator();
    fields_.draw(document, selection, commands, catalog, browse);
}
} // namespace platformator_editor
