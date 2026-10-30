#pragma once

#include "../preferences.h"
#include "../process.h"

namespace platformator_editor
{
enum class RunPanelAction
{
    None,
    RefreshSources,
    Build,
    BuildAndRun,
    Stop
};

class RunSettingsPanel
{
  public:
    RunPanelAction draw(RunPreferences &preferences, const PipelineStatus &status,
                        bool busy, bool documentDirty);
};
} // namespace platformator_editor
