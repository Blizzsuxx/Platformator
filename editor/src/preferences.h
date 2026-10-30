#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
struct RunPreferences
{
    std::string configurePreset;
    std::string buildPreset;
    std::string target;
    std::string executable;
    std::string catalogProtocol = "unavailable";
    bool production = false;
    int width = 1280;
    int height = 800;
    bool fullscreen = false;
    bool maximized = false;
    bool preserveAspect = false;
    bool startPaused = false;
    std::vector<std::string> overlays;

    bool operator==(const RunPreferences &) const = default;
};

struct WorkspacePreferences
{
    int version = 1;
    std::string projectKey;
    std::optional<std::filesystem::path> recentScene;
    std::map<std::string, bool> panelVisibility;
    float userScale = 1.0f;
    float navigationZoom = 1.0f;
    float navigationX = 0.0f;
    float navigationY = 0.0f;
    RunPreferences run;
};

std::map<std::string, bool> default_panel_visibility();
bool is_panel_visible(const WorkspacePreferences &preferences, std::string_view stableId);
void reset_workspace_layout(WorkspacePreferences &preferences);
bool layout_needs_recovery(std::string_view layout, int displayWidth, int displayHeight);
std::string migrate_scene_window_identity(std::string_view layout);

struct ProjectPreferencePaths
{
    std::filesystem::path settingsPath;
    std::filesystem::path layoutPath;
    std::string projectKey;
};

struct WorkspacePreferencesLoadResult
{
    WorkspacePreferences preferences;
    std::string layout;
    bool settingsPresent = false;
    bool recovered = false;
    std::string message;
};

struct WorkspacePreferencesSaveResult
{
    bool settingsSaved = false;
    bool layoutSaved = false;
    std::string message;

    bool succeeded() const noexcept
    {
        return settingsSaved && layoutSaved;
    }
};

struct StartupSceneResolution
{
    std::optional<std::filesystem::path> scenePath;
    bool usedRestoration = false;
    bool missingRestoredScene = false;
    bool startWithStarterDocument = false;
    std::string message;
};

ProjectPreferencePaths make_project_preference_paths(const std::filesystem::path &preferencesRoot,
                                                      const std::filesystem::path &projectRoot);
WorkspacePreferencesLoadResult load_workspace_preferences(const std::filesystem::path &settingsPath,
                                                          const std::filesystem::path &layoutPath,
                                                          const std::filesystem::path &projectRoot);
void apply_known_bundled_run_defaults(WorkspacePreferences &preferences,
                                     const std::filesystem::path &projectRoot);
WorkspacePreferencesSaveResult save_workspace_preferences(const ProjectPreferencePaths &paths,
                                                          const WorkspacePreferences &preferences,
                                                          const std::string &layout);
void record_adopted_scene(WorkspacePreferences &preferences, const std::filesystem::path &scenePath);
StartupSceneResolution resolve_startup_scene(const std::optional<std::filesystem::path> &explicitScene,
                                              const std::optional<std::filesystem::path> &recentScene,
                                              bool noRestore);
} // namespace platformator_editor
