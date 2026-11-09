#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "behaviorcatalog.h"
#include "editcommands.h"

namespace platformator_editor
{
using ComponentSettingsTarget = std::pair<int, int>; // object ID, component ID

std::optional<Json> copy_component_settings(const Json &component,
                                           const BehaviorCatalog &catalog,
                                           std::string &error);

CommandResult paste_component_settings(DocumentSession &document, EditCommandService &commands,
                                      int objectId, int componentId, const Json &settings,
                                      const BehaviorCatalog &catalog, SelectionState selection = {});

CommandResult paste_component_settings_group(DocumentSession &document, EditCommandService &commands,
                                             const std::vector<ComponentSettingsTarget> &targets,
                                             const Json &settings, const BehaviorCatalog &catalog,
                                             SelectionState selection = {});

CommandResult reset_component_settings(DocumentSession &document, EditCommandService &commands,
                                      int objectId, int componentId, const BehaviorCatalog &catalog,
                                      SelectionState selection = {});

CommandResult reset_component_settings_group(DocumentSession &document, EditCommandService &commands,
                                             const std::vector<ComponentSettingsTarget> &targets,
                                             const BehaviorCatalog &catalog,
                                             SelectionState selection = {});
} // namespace platformator_editor
