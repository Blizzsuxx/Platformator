#include "preferences.h"

#include "documentio.h"

#include <cmath>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

#include <json.hpp>

namespace platformator_editor
{
namespace
{
using Json = nlohmann::json;
constexpr int kPreferencesVersion = 1;
constexpr std::size_t kMaximumLayoutBytes = 1024 * 1024;

bool parse_pair(std::string_view text, int &first, int &second)
{
    const auto separator = text.find(',');
    if (separator == std::string_view::npos)
        return false;
    const auto left = text.substr(0, separator);
    const auto right = text.substr(separator + 1);
    const auto a = std::from_chars(left.data(), left.data() + left.size(), first);
    const auto b = std::from_chars(right.data(), right.data() + right.size(), second);
    return a.ec == std::errc{} && a.ptr == left.data() + left.size() &&
           b.ec == std::errc{} && b.ptr == right.data() + right.size();
}

bool geometry_is_far_offscreen(int x, int y, int width, int height, int displayWidth, int displayHeight)
{
    return x < -width + 32 || y < -height + 24 || x > displayWidth - 32 || y > displayHeight - 24;
}

std::string canonical_project_key(const std::filesystem::path &projectRoot)
{
    std::error_code error;
    const auto canonical = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonical))
        throw std::invalid_argument("cannot resolve project root for preferences: " + projectRoot.string());
    return canonical.string();
}

std::string project_suffix(std::string_view projectKey)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : projectKey)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}

void set_recovered(WorkspacePreferencesLoadResult &result, std::string message)
{
    result.recovered = true;
    if (result.message.empty())
        result.message = std::move(message);
}

bool read_string(const Json &source, const char *key, std::string &destination,
                 WorkspacePreferencesLoadResult &result)
{
    const auto found = source.find(key);
    if (found == source.end())
        return true;
    if (!found->is_string())
    {
        set_recovered(result, std::string("invalid preference field '") + key + "' was reset");
        return false;
    }
    destination = found->get<std::string>();
    return true;
}

bool read_bool(const Json &source, const char *key, bool &destination,
               WorkspacePreferencesLoadResult &result)
{
    const auto found = source.find(key);
    if (found == source.end())
        return true;
    if (!found->is_boolean())
    {
        set_recovered(result, std::string("invalid preference field '") + key + "' was reset");
        return false;
    }
    destination = found->get<bool>();
    return true;
}

bool read_number(const Json &source, const char *key, float &destination,
                 WorkspacePreferencesLoadResult &result, float minimum, float maximum)
{
    const auto found = source.find(key);
    if (found == source.end())
        return true;
    if (!found->is_number())
    {
        set_recovered(result, std::string("invalid preference field '") + key + "' was reset");
        return false;
    }
    const double value = found->get<double>();
    if (!std::isfinite(value) || value < minimum || value > maximum)
    {
        set_recovered(result, std::string("out-of-range preference field '") + key + "' was reset");
        return false;
    }
    destination = static_cast<float>(value);
    return true;
}

void load_run_preferences(const Json &source, RunPreferences &run,
                          WorkspacePreferencesLoadResult &result)
{
    const auto found = source.find("run");
    if (found == source.end())
        return;
    if (!found->is_object())
    {
        set_recovered(result, "invalid run preferences were reset");
        return;
    }
    read_string(*found, "configurePreset", run.configurePreset, result);
    read_string(*found, "buildPreset", run.buildPreset, result);
    read_string(*found, "target", run.target, result);
    read_string(*found, "executable", run.executable, result);
    read_string(*found, "catalogProtocol", run.catalogProtocol, result);
    if (run.catalogProtocol != "common-runner-v1" && run.catalogProtocol != "unavailable")
    {
        run.catalogProtocol = "unavailable";
        set_recovered(result, "invalid catalog protocol was reset to unavailable");
    }
    read_bool(*found, "production", run.production, result);
    read_bool(*found, "fullscreen", run.fullscreen, result);
    read_bool(*found, "maximized", run.maximized, result);
    read_bool(*found, "preserveAspect", run.preserveAspect, result);
    read_bool(*found, "startPaused", run.startPaused, result);

    auto read_dimension = [&](const char *key, int &destination) {
        const auto value = found->find(key);
        if (value == found->end())
            return;
        if (!value->is_number_integer() || value->get<std::int64_t>() < 1 || value->get<std::int64_t>() > 16384)
        {
            set_recovered(result, std::string("invalid run dimension '") + key + "' was reset");
            return;
        }
        destination = value->get<int>();
    };
    read_dimension("width", run.width);
    read_dimension("height", run.height);

    const auto overlays = found->find("overlays");
    if (overlays != found->end())
    {
        if (!overlays->is_array())
            set_recovered(result, "invalid run overlays were reset");
        else
        {
            run.overlays.clear();
            for (const auto &overlay : *overlays)
            {
                if (!overlay.is_string())
                {
                    set_recovered(result, "invalid run overlay entry was ignored");
                    continue;
                }
                run.overlays.push_back(overlay.get<std::string>());
            }
        }
    }
}

void load_editor_window_preferences(const Json &source, EditorWindowPreferences &window,
                                   WorkspacePreferencesLoadResult &result)
{
    const auto found = source.find("editorWindow");
    if (found == source.end())
        return;
    if (!found->is_object())
    {
        set_recovered(result, "invalid editor window preferences were reset");
        return;
    }
    auto read_dimension = [&](const char *key, int &destination) {
        const auto value = found->find(key);
        if (value == found->end())
            return;
        if (!value->is_number_integer() || value->get<std::int64_t>() < 320 ||
            value->get<std::int64_t>() > 16384)
        {
            set_recovered(result, std::string("invalid editor window dimension '") + key + "' was reset");
            return;
        }
        destination = value->get<int>();
    };
    read_dimension("width", window.width);
    read_dimension("height", window.height);
    read_bool(*found, "fullscreen", window.fullscreen, result);
    read_bool(*found, "maximized", window.maximized, result);
}

Json serialize_preferences(const WorkspacePreferences &preferences, std::string_view projectKey)
{
    Json panels = Json::object();
    for (const auto &[id, visible] : preferences.panelVisibility)
        panels[id] = visible;

    Json run = {{"configurePreset", preferences.run.configurePreset},
                {"buildPreset", preferences.run.buildPreset},
                {"target", preferences.run.target},
                {"executable", preferences.run.executable},
                {"catalogProtocol", preferences.run.catalogProtocol},
                {"production", preferences.run.production},
                {"width", preferences.run.width},
                {"height", preferences.run.height},
                {"fullscreen", preferences.run.fullscreen},
                {"maximized", preferences.run.maximized},
                {"preserveAspect", preferences.run.preserveAspect},
                {"startPaused", preferences.run.startPaused},
                {"overlays", preferences.run.overlays}};
    Json result = {{"version", kPreferencesVersion},
                   {"projectKey", projectKey},
                   {"panelVisibility", std::move(panels)},
                   {"userScale", preferences.userScale},
                   {"navigationZoom", preferences.navigationZoom},
                   {"navigationX", preferences.navigationX},
                   {"navigationY", preferences.navigationY},
                   {"editorWindow", {{"width", preferences.editorWindow.width},
                                     {"height", preferences.editorWindow.height},
                                     {"fullscreen", preferences.editorWindow.fullscreen},
                                     {"maximized", preferences.editorWindow.maximized}}},
                   {"run", std::move(run)}};
    if (preferences.recentScene)
        result["recentScene"] = preferences.recentScene->string();
    return result;
}

bool read_layout(const std::filesystem::path &path, std::string &layout, std::string &error)
{
    std::error_code filesystemError;
    const auto size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError)
    {
        if (filesystemError == std::errc::no_such_file_or_directory)
            return true;
        error = "cannot inspect layout preferences: " + filesystemError.message();
        return false;
    }
    if (size > kMaximumLayoutBytes)
    {
        error = "layout preferences exceeded the 1 MiB limit and were reset";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "cannot read layout preferences";
        return false;
    }
    layout.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (stream.bad())
    {
        layout.clear();
        error = "cannot finish reading layout preferences";
        return false;
    }
    return true;
}
} // namespace

std::map<std::string, bool> default_panel_visibility()
{
    return {{"panel.hierarchy", true}, {"panel.scene", true}, {"panel.inspector", true},
            {"panel.project", true}, {"panel.console", true},
            {"panel.run", false}, {"panel.validation", false}};
}

bool is_panel_visible(const WorkspacePreferences &preferences, std::string_view stableId)
{
    const auto found = preferences.panelVisibility.find(std::string(stableId));
    if (found != preferences.panelVisibility.end())
        return found->second;
    return stableId == "panel.hierarchy" || stableId == "panel.scene" ||
           stableId == "panel.inspector" ||
           stableId == "panel.project" || stableId == "panel.console";
}

void reset_workspace_layout(WorkspacePreferences &preferences)
{
    preferences.panelVisibility = default_panel_visibility();
    preferences.userScale = 1.0f;
    preferences.navigationZoom = 1.0f;
    preferences.navigationX = 0.0f;
    preferences.navigationY = 0.0f;
}

bool layout_needs_recovery(std::string_view layout, int displayWidth, int displayHeight)
{
    if (layout.empty() || layout.size() > kMaximumLayoutBytes || displayWidth <= 0 || displayHeight <= 0 ||
        layout.find("[Docking][Data]") == std::string_view::npos ||
        layout.find("DockSpace ID=0x") == std::string_view::npos ||
        layout.find('\0') != std::string_view::npos)
        return true;

    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool hasPosition = false;
    bool hasSize = false;
    bool hasDockspaceGeometry = false;
    const auto checkWindow = [&] {
        if (hasPosition && geometry_is_far_offscreen(x, y, hasSize ? width : displayWidth / 3,
                                                      hasSize ? height : displayHeight / 3,
                                                      displayWidth, displayHeight))
            return false;
        if (hasSize && (width < 0 || height < 0 || width > displayWidth * 2 || height > displayHeight * 2))
            return false;
        return true;
    };
    std::size_t offset = 0;
    while (offset < layout.size())
    {
        const auto end = layout.find('\n', offset);
        auto line = layout.substr(offset, end == std::string_view::npos ? layout.size() - offset : end - offset);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.starts_with("DockSpace ID="))
        {
            const auto position = line.find(" Pos=");
            const auto size = line.find(" Size=");
            const auto sizeEnd = line.find(' ', size == std::string_view::npos ? 0 : size + 6);
            int rootX = 0;
            int rootY = 0;
            int rootWidth = 0;
            int rootHeight = 0;
            if (position == std::string_view::npos || size == std::string_view::npos ||
                !parse_pair(line.substr(position + 5, size - (position + 5)), rootX, rootY) ||
                !parse_pair(line.substr(size + 6,
                    sizeEnd == std::string_view::npos ? line.size() - (size + 6) : sizeEnd - (size + 6)),
                    rootWidth, rootHeight) ||
                rootWidth <= 0 || rootHeight <= 0)
                return true;
            hasDockspaceGeometry = true;
        }
        if (line.starts_with("[Window]["))
        {
            if (!checkWindow())
                return true;
            x = y = width = height = 0;
            hasPosition = hasSize = false;
        }
        else if (line.starts_with("Pos="))
        {
            if (!parse_pair(line.substr(4), x, y))
                return true;
            hasPosition = true;
        }
        else if (line.starts_with("Size="))
        {
            if (!parse_pair(line.substr(5), width, height))
                return true;
            hasSize = true;
        }
        if (end == std::string_view::npos)
            break;
        offset = end + 1;
    }
    return !hasDockspaceGeometry || !checkWindow();
}

std::string migrate_scene_window_identity(std::string_view layout)
{
    constexpr std::string_view legacySection = "[Window][Scene]";
    constexpr std::string_view stableSection = "[Window][panel.scene]";
    constexpr std::string_view retiredBehaviorSection = "[Window][panel.behaviors]";
    constexpr std::string_view retiredSettingsSection = "[Window][panel.settings]";
    const bool hasLegacy = layout.find(legacySection) != std::string_view::npos;
    const bool hasRetiredBehavior = layout.find(retiredBehaviorSection) != std::string_view::npos;
    const bool hasRetiredSettings = layout.find(retiredSettingsSection) != std::string_view::npos;
    if (!hasLegacy && !hasRetiredBehavior && !hasRetiredSettings)
        return std::string(layout);
    const bool hasStable = layout.find(stableSection) != std::string_view::npos;

    std::string migrated;
    migrated.reserve(layout.size());
    bool skippingLegacy = false;
    std::size_t offset = 0;
    while (offset < layout.size())
    {
        const auto end = layout.find('\n', offset);
        auto line = layout.substr(offset, end == std::string_view::npos ? layout.size() - offset
                                                                        : end - offset);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line == retiredBehaviorSection || line == retiredSettingsSection)
            skippingLegacy = true;
        else if (line == legacySection)
        {
            skippingLegacy = hasStable;
            if (!hasStable)
                migrated.append(stableSection);
        }
        else if (line.starts_with("["))
        {
            skippingLegacy = false;
            migrated.append(line);
        }
        else if (!skippingLegacy)
            migrated.append(line);

        if (end != std::string_view::npos)
            migrated.push_back('\n');
        offset = end == std::string_view::npos ? layout.size() : end + 1;
    }
    return migrated;
}

ProjectPreferencePaths make_project_preference_paths(const std::filesystem::path &preferencesRoot,
                                                      const std::filesystem::path &projectRoot)
{
    const auto projectKey = canonical_project_key(projectRoot);
    const auto directory = preferencesRoot / ("project-" + project_suffix(projectKey));
    return {directory / "settings.json", directory / "layout.ini", projectKey};
}

WorkspacePreferencesLoadResult load_workspace_preferences(const std::filesystem::path &settingsPath,
                                                          const std::filesystem::path &layoutPath,
                                                          const std::filesystem::path &projectRoot)
{
    WorkspacePreferencesLoadResult result;
    result.preferences.projectKey = canonical_project_key(projectRoot);
    std::error_code filesystemError;
    const bool settingsExist = std::filesystem::exists(settingsPath, filesystemError);
    result.settingsPresent = !filesystemError && settingsExist;
    if (filesystemError)
        set_recovered(result, "settings could not be inspected; defaults were loaded");
    else if (settingsExist)
    {
        try
        {
            std::ifstream stream(settingsPath, std::ios::binary);
            if (!stream)
                throw std::runtime_error("settings file cannot be opened");
            const Json source = Json::parse(stream);
            const auto version = source.is_object() ? source.find("version") : Json::const_iterator{};
            const auto project = source.is_object() ? source.find("projectKey") : Json::const_iterator{};
            if (!source.is_object() || version == source.end() || !version->is_number_integer() ||
                version->get<int>() != kPreferencesVersion || project == source.end() || !project->is_string() ||
                project->get<std::string>() != result.preferences.projectKey)
            {
                set_recovered(result, "settings version or project identity did not match; defaults were loaded");
            }
            else
            {
                const auto recent = source.find("recentScene");
                if (recent != source.end())
                {
                    if (recent->is_string() && !recent->get<std::string>().empty())
                        result.preferences.recentScene = std::filesystem::path(recent->get<std::string>());
                    else
                        set_recovered(result, "invalid recent scene preference was cleared");
                }

                const auto panels = source.find("panelVisibility");
                if (panels != source.end())
                {
                    if (!panels->is_object())
                        set_recovered(result, "invalid panel visibility preferences were reset");
                    else
                    {
                        for (const auto &[id, visible] : panels->items())
                        {
                            if (id.empty() || !visible.is_boolean())
                            {
                                set_recovered(result, "invalid panel visibility entry was ignored");
                                continue;
                            }
                            result.preferences.panelVisibility.emplace(id, visible.get<bool>());
                        }
                    }
                }
                read_number(source, "userScale", result.preferences.userScale, result, 0.5f, 3.0f);
                read_number(source, "navigationZoom", result.preferences.navigationZoom, result, 0.01f, 100.0f);
                read_number(source, "navigationX", result.preferences.navigationX, result, -10000000.0f, 10000000.0f);
                read_number(source, "navigationY", result.preferences.navigationY, result, -10000000.0f, 10000000.0f);
                load_editor_window_preferences(source, result.preferences.editorWindow, result);
                load_run_preferences(source, result.preferences.run, result);
            }
        }
        catch (const std::exception &error)
        {
            result.preferences = WorkspacePreferences{};
            result.preferences.projectKey = canonical_project_key(projectRoot);
            set_recovered(result, std::string("settings were unreadable; defaults were loaded: ") + error.what());
        }
    }

    std::string layoutError;
    if (!read_layout(layoutPath, result.layout, layoutError))
    {
        result.layout.clear();
        set_recovered(result, std::move(layoutError));
    }
    return result;
}

void apply_known_bundled_run_defaults(WorkspacePreferences &preferences,
                                     const std::filesystem::path &projectRoot)
{
    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(root))
        return;

    const bool platformatorSourceRoot =
        std::filesystem::is_regular_file(root / "src" / "runner.cpp") &&
        std::filesystem::is_regular_file(root / "include" / "platformator" / "scriptregistration.h") &&
        std::filesystem::is_regular_file(root / "examples" / "mario" / "CMakeLists.txt");
    if (platformatorSourceRoot &&
        (preferences.run.target.empty() || preferences.run.target == "main"))
    {
        preferences.run.target = "main";
        if (preferences.run.buildPreset.empty())
            preferences.run.buildPreset = preferences.run.production ? "production" : "debug";
        if (preferences.run.configurePreset.empty())
            preferences.run.configurePreset = preferences.run.buildPreset;
        if (preferences.run.executable.empty())
            preferences.run.executable = "bin/" + preferences.run.buildPreset + "/main";
        preferences.run.catalogProtocol = "common-runner-v1";
        return;
    }

    const bool marioExampleRoot = root.filename() == "mario" &&
        std::filesystem::is_regular_file(root / "main.cpp") &&
        std::filesystem::is_regular_file(root / "assets" / "behaviors" / "mario_camera_rig.cpp") &&
        std::filesystem::is_regular_file(root / "CMakeLists.txt");
    if (marioExampleRoot &&
        (preferences.run.target.empty() || preferences.run.target == "mario_example"))
    {
        preferences.run.target = "mario_example";
        preferences.run.configurePreset.clear();
        preferences.run.buildPreset.clear();
        if (preferences.run.executable.empty())
            preferences.run.executable = "bin/editor-game/mario_example";
        preferences.run.catalogProtocol = "common-runner-v1";
    }
}

WorkspacePreferencesSaveResult save_workspace_preferences(const ProjectPreferencePaths &paths,
                                                          const WorkspacePreferences &preferences,
                                                          const std::string &layout)
{
    if (preferences.projectKey != paths.projectKey)
        return {false, false, "preferences belong to a different project"};
    if (!std::isfinite(preferences.userScale) || preferences.userScale < 0.5f || preferences.userScale > 3.0f ||
        !std::isfinite(preferences.navigationZoom) || preferences.navigationZoom < 0.01f ||
        preferences.navigationZoom > 100.0f || !std::isfinite(preferences.navigationX) ||
        !std::isfinite(preferences.navigationY) || preferences.editorWindow.width < 320 ||
        preferences.editorWindow.width > 16384 || preferences.editorWindow.height < 320 ||
        preferences.editorWindow.height > 16384 || preferences.run.width < 1 || preferences.run.width > 16384 ||
        preferences.run.height < 1 || preferences.run.height > 16384)
        return {false, false, "preferences contain out-of-range values"};
    if (preferences.run.catalogProtocol != "common-runner-v1" &&
        preferences.run.catalogProtocol != "unavailable")
        return {false, false, "catalog protocol must be common-runner-v1 or unavailable"};
    if (layout.size() > kMaximumLayoutBytes)
        return {false, false, "layout preferences exceed the 1 MiB limit"};

    std::error_code error;
    std::filesystem::create_directories(paths.settingsPath.parent_path(), error);
    if (error)
        return {false, false, "cannot create preference directory: " + error.message()};

    WorkspacePreferencesSaveResult result;
    const auto settings = serialize_preferences(preferences, paths.projectKey).dump(2) + "\n";
    const auto settingsWrite = write_file_atomically(paths.settingsPath, settings, true);
    result.settingsSaved = settingsWrite.succeeded();
    if (!result.settingsSaved)
        result.message = "settings write failed: " + settingsWrite.message;

    const auto layoutWrite = write_file_atomically(paths.layoutPath, layout, true);
    result.layoutSaved = layoutWrite.succeeded();
    if (!result.layoutSaved)
    {
        if (!result.message.empty())
            result.message += "; ";
        result.message += "layout write failed: " + layoutWrite.message;
    }
    if (result.succeeded())
    {
        if (settingsWrite.status == AtomicWriteStatus::DurabilityWarning ||
            layoutWrite.status == AtomicWriteStatus::DurabilityWarning)
            result.message = "preferences were replaced, but directory durability could not be confirmed";
    }
    return result;
}

LastWorkspaceLoadResult load_last_workspace(const std::filesystem::path &path)
{
    std::error_code filesystemError;
    const auto size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError == std::errc::no_such_file_or_directory)
        return {};
    if (filesystemError)
        return {{}, "Last workspace preference could not be inspected: " + filesystemError.message()};
    if (size > 16U * 1024U)
        return {{}, "Last workspace preference exceeds the 16 KiB limit and was ignored."};
    try
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("preference file cannot be opened");
        const Json value = Json::parse(stream);
        const auto version = value.is_object() ? value.find("version") : Json::const_iterator{};
        const auto root = value.is_object() ? value.find("projectRoot") : Json::const_iterator{};
        if (!value.is_object() || version == value.end() || !version->is_number_integer() ||
            version->get<int>() != 1 || root == value.end() || !root->is_string() ||
            root->get<std::string>().empty())
            return {{}, "Last workspace preference was invalid and was ignored."};
        return {std::filesystem::path(root->get<std::string>()), {}};
    }
    catch (const std::exception &error)
    {
        return {{}, std::string("Last workspace preference could not be read: ") + error.what()};
    }
}

bool save_last_workspace(const std::filesystem::path &path,
                         const std::filesystem::path &projectRoot, std::string &error)
{
    try
    {
        std::error_code filesystemError;
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError)
        {
            error = "cannot create last workspace preference directory: " + filesystemError.message();
            return false;
        }
        const Json value{{"version", 1}, {"projectRoot", canonical_project_key(projectRoot)}};
        const auto result = write_file_atomically(path, value.dump(2) + "\n", true);
        if (!result.succeeded())
        {
            error = "cannot save last workspace: " + result.message;
            return false;
        }
        error.clear();
        return true;
    }
    catch (const std::exception &exception)
    {
        error = std::string("cannot save last workspace: ") + exception.what();
        return false;
    }
}

void record_adopted_scene(WorkspacePreferences &preferences, const std::filesystem::path &scenePath)
{
    if (scenePath.empty())
    {
        preferences.recentScene.reset();
        return;
    }
    std::error_code error;
    auto absolute = std::filesystem::absolute(scenePath, error);
    if (error)
        return;
    const auto canonical = std::filesystem::canonical(absolute, error);
    preferences.recentScene = error ? absolute.lexically_normal() : canonical;
}

StartupSceneResolution resolve_startup_scene(const std::optional<std::filesystem::path> &explicitScene,
                                              const std::optional<std::filesystem::path> &recentScene,
                                              bool noRestore)
{
    if (explicitScene)
        return {explicitScene, false, false, false, {}};
    if (noRestore || !recentScene)
        return {std::nullopt, false, false, true, {}};

    std::error_code error;
    if (std::filesystem::is_regular_file(*recentScene, error) && !error)
        return {recentScene, true, false, false, {}};
    return {std::nullopt, true, true, true,
            "The last scene could not be restored; a new starter document was opened."};
}
} // namespace platformator_editor
