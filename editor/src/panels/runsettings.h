#pragma once

#include "../accessibility.h"
#include "../preferences.h"
#include "../process.h"
#include "../project.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
enum class RunPanelAction
{
    None,
    RefreshSources,
    RefreshDiscovery,
    BrowseExecutable,
    Build,
    BuildAndRun,
    Stop
};

class RunSettingsPanel
{
  public:
    void set_project_root(std::filesystem::path projectRoot);
    void refresh_discovery(bool requestFileApiQuery = false);
    RunPanelAction draw(RunPreferences &preferences, const PipelineStatus &status,
                        bool busy, bool documentDirty);
    std::vector<SemanticNode> semantic_nodes(const RunPreferences &preferences,
                                              std::string_view parentId, bool busy = false);
    bool dispatch_semantic_action(RunPreferences &preferences,
                                  const SemanticAction &action);
    const std::string &focused_control() const noexcept { return focusedControl_; }

  private:
    struct SemanticChoice
    {
        enum class Kind { ConfigurePreset, BuildPreset, Executable, CatalogProtocol } kind;
        std::string value;
        std::string executable;
    };

    std::filesystem::path projectRoot_;
    ProjectRunDiscovery discovery_;
    std::map<std::string, SemanticChoice> semanticChoices_;
    std::map<std::string, std::string> textDrafts_;
    std::map<std::string, std::string> committedText_;
    std::string focusedControl_;
};
} // namespace platformator_editor
