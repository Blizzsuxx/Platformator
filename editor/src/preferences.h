#pragma once

#include <filesystem>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "sessionids.h"

namespace platformator_editor
{
class DeferredPreferenceSave
{
  public:
    using Clock = std::chrono::steady_clock;

    void schedule(Clock::time_point now, Clock::duration delay = std::chrono::milliseconds(500)) noexcept;
    bool ready(Clock::time_point now) const noexcept;
    bool take_if_ready(Clock::time_point now) noexcept;
    bool pending() const noexcept;
    Clock::time_point deadline() const noexcept;
    void clear() noexcept;

  private:
    Clock::time_point due_{};
    bool pending_ = false;
};

struct RunPreferences
{
    std::string configurePreset;
    std::string buildPreset;
    std::string target;
    std::string executable;
    std::string catalogProtocol = "unavailable";
    bool embeddedGameView = false;
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

struct EditorWindowPreferences
{
    int width = 1280;
    int height = 800;
    bool fullscreen = false;
    bool maximized = false;

    bool operator==(const EditorWindowPreferences &) const = default;
};

struct HostGeometryPreferences
{
    int x = 40;
    int y = 40;
    int width = 1280;
    int height = 800;
    bool maximized = false;

    bool operator==(const HostGeometryPreferences &) const = default;
};

struct SavedTabPreferences
{
    std::filesystem::path scenePath;
    float viewCenterX = 0.0f;
    float viewCenterY = 0.0f;
    float viewZoom = 1.0f;
    std::vector<int> expandedHierarchyIds;
    bool hasExpansionState = false;

    bool operator==(const SavedTabPreferences &) const = default;
};

struct SnapPreferences
{
    float position = 1.0f;
    float rotationDegrees = 15.0f;
    float scale = 0.1f;
    std::string coordinateMode = "world";

    bool operator==(const SnapPreferences &) const = default;
};

struct WorkspacePreferences
{
    int version = 2;
    std::string projectKey;
    std::optional<std::filesystem::path> recentScene;
    std::map<std::string, bool> panelVisibility;
    std::map<std::string, HostLayoutId> panelHostAssignments{
        {"panel.hierarchy", main_host_layout_id}, {"panel.scene", main_host_layout_id},
        {"panel.inspector", main_host_layout_id}, {"panel.project", main_host_layout_id},
        {"panel.console", main_host_layout_id}, {"panel.run", main_host_layout_id},
        {"panel.validation", main_host_layout_id}};
    std::map<HostLayoutId, HostGeometryPreferences> hostGeometry{{main_host_layout_id, {}}};
    std::map<HostLayoutId, std::string> hostLayoutSettings;
    std::vector<SavedTabPreferences> openTabs;
    std::optional<std::filesystem::path> activeTabPath;
    std::map<std::string, std::vector<int>> hiddenObjectIdsByScene;
    std::map<std::string, std::vector<int>> lockedObjectIdsByScene;
    SnapPreferences snap;
    std::string duplicationReferencePolicy = "preserve-existing-targets";
    std::map<std::string, std::uint64_t> resourceLimits;
    float userScale = 1.0f;
    float navigationZoom = 1.0f;
    float navigationX = 0.0f;
    float navigationY = 0.0f;
    EditorWindowPreferences editorWindow;
    RunPreferences run;

    bool operator==(const WorkspacePreferences &) const = default;
};

std::map<std::string, bool> default_panel_visibility();
bool is_panel_visible(const WorkspacePreferences &preferences, std::string_view stableId);
bool set_editor_object_id_state(std::vector<int> &ids, int objectId, bool enabled);
bool prune_editor_object_ids(std::vector<int> &ids,
                             const std::unordered_set<int> &liveObjectIds);
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
    bool migrated = false;
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

class WorkspacePreferencePersistence
{
  public:
    using Writer = std::function<WorkspacePreferencesSaveResult(bool writeSettings, bool writeLayout)>;

    WorkspacePreferencePersistence(WorkspacePreferences persistedPreferences, std::string persistedLayout);

    WorkspacePreferencesSaveResult flush(const WorkspacePreferences &preferences,
                                         const std::string &layout,
                                         const Writer &writer);

  private:
    WorkspacePreferences persistedPreferences_;
    std::string persistedLayout_;
};

struct LastWorkspaceLoadResult
{
    std::optional<std::filesystem::path> projectRoot;
    std::string message;
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
                                                          const std::string &layout,
                                                          bool writeSettings = true,
                                                          bool writeLayout = true);
LastWorkspaceLoadResult load_last_workspace(const std::filesystem::path &path);
bool save_last_workspace(const std::filesystem::path &path,
                         const std::filesystem::path &projectRoot, std::string &error);
void record_adopted_scene(WorkspacePreferences &preferences, const std::filesystem::path &scenePath);
StartupSceneResolution resolve_startup_scene(const std::optional<std::filesystem::path> &explicitScene,
                                              const std::optional<std::filesystem::path> &recentScene,
                                              bool noRestore);
} // namespace platformator_editor
