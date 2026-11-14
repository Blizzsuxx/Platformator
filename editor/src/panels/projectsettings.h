#pragma once

#include "../accessibility.h"
#include "../layersettings.h"
#include "../preferences.h"
#include "../process.h"
#include "../project.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
enum class ProjectSettingsCategory
{
    BuildAndRun,
    LayersAndPhysics
};

enum class ProjectSettingsAction
{
    None,
    BrowseExecutable,
    GenerateCMakeLists,
    RepairEnginePath,
    RegenerateExistingCMakeLists,
    SaveLayerDraft,
    DiscardLayerDraft
};

class ProjectSettingsPanel
{
  public:
    void set_project_root(std::filesystem::path projectRoot);
    void refresh_discovery(bool requestFileApiQuery = false);
    void set_category(ProjectSettingsCategory category) noexcept { category_ = category; }
    ProjectSettingsCategory category() const noexcept { return category_; }

    ProjectSettingsAction draw(RunPreferences &preferences, LayerSettings &layers,
                               bool missingCmake, bool busy,
                               bool &layersModified, bool &layersDirty);
    bool layer_draft_dirty() const noexcept { return layerDraftDirty_; }
    const LayerSettings &layer_draft() const noexcept { return layerDraft_; }
    void set_open_scene_collision_groups(std::uint32_t groups) noexcept { openSceneGroups_ = groups; }
    void commit_layer_draft(const LayerSettings &layers);
    void discard_layer_draft(const LayerSettings &layers);

    std::vector<SemanticNode> semantic_nodes(const RunPreferences &preferences,
                                             std::string_view parentId, bool busy = false,
                                             bool missingCmake = false);
    bool dispatch_semantic_action(RunPreferences &preferences, const SemanticAction &action);
    void request_cmake_regeneration_confirmation() noexcept
    {
        regenerateConfirmationRequested_ = true;
    }
    const std::string &focused_control() const noexcept { return focusedControl_; }

  private:
    struct SemanticChoice
    {
        enum class Kind
        {
            ConfigurePreset,
            BuildPreset,
            Executable
        } kind;
        std::string value;
        std::string executable;
    };

    void draw_build_and_run(RunPreferences &preferences, bool missingCmake,
                            bool busy,
                            ProjectSettingsAction &action);
    void draw_layers_and_physics(const LayerSettings &savedLayers, LayerSettings &draft,
                                 bool &layersModified,
                                 ProjectSettingsAction &action);

    ProjectSettingsCategory category_ = ProjectSettingsCategory::BuildAndRun;
    std::filesystem::path projectRoot_;
    ProjectRunDiscovery discovery_;
    std::map<std::string, SemanticChoice> semanticChoices_;
    std::map<std::string, std::string> textDrafts_;
    std::map<std::string, std::string> committedText_;
    std::string focusedControl_;
    std::string layerValidationError_;
    bool showEmptyLayers_ = false;
    LayerSettings layerDraft_;
    bool layerDraftInitialized_ = false;
    bool layerDraftDirty_ = false;
    std::uint32_t openSceneGroups_ = 0;
    bool regenerateConfirmationRequested_ = false;
};
} // namespace platformator_editor
