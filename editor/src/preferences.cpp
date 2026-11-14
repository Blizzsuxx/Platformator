#include "preferences.h"

#include "documentio.h"
#include "editorwork.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <json.hpp>

namespace platformator_editor
{
void DeferredPreferenceSave::schedule(Clock::time_point now, Clock::duration delay) noexcept
{
    due_ = now + std::max(delay, Clock::duration::zero());
    pending_ = true;
}

bool DeferredPreferenceSave::ready(Clock::time_point now) const noexcept
{
    return pending_ && now >= due_;
}

bool DeferredPreferenceSave::take_if_ready(Clock::time_point now) noexcept
{
    if (!ready(now))
        return false;
    pending_ = false;
    return true;
}

bool DeferredPreferenceSave::pending() const noexcept { return pending_; }
DeferredPreferenceSave::Clock::time_point DeferredPreferenceSave::deadline() const noexcept
{
    return pending_ ? due_ : Clock::time_point::max();
}
void DeferredPreferenceSave::clear() noexcept { pending_ = false; }

namespace
{
using Json = nlohmann::json;
constexpr int kPreferencesVersion = 2;
constexpr int kLegacyPreferencesVersion = 1;
constexpr std::size_t kMaximumLayoutBytes = 1024 * 1024;

std::string_view saved_document_kind_name(SavedDocumentKind kind)
{
    switch (kind)
    {
    case SavedDocumentKind::Template: return "template";
    case SavedDocumentKind::Animation: return "animation";
    case SavedDocumentKind::Scene: return "scene";
    }
    return "scene";
}

std::optional<SavedDocumentKind> parse_saved_document_kind(const Json &value)
{
    if (!value.is_string())
        return std::nullopt;
    const std::string kind = value.get<std::string>();
    if (kind == "scene") return SavedDocumentKind::Scene;
    if (kind == "template") return SavedDocumentKind::Template;
    if (kind == "animation") return SavedDocumentKind::Animation;
    return std::nullopt;
}

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
    auto canonical = std::filesystem::weakly_canonical(projectRoot, error);
    if (error)
    {
        error.clear();
        canonical = std::filesystem::absolute(projectRoot, error);
    }
    if (error || canonical.empty())
        return projectRoot.lexically_normal().string();
    return canonical.lexically_normal().string();
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
    read_bool(*found, "embeddedGameView", run.embeddedGameView, result);
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

std::optional<int> bounded_integer(const Json &value, std::int64_t minimum, std::int64_t maximum)
{
    try
    {
        if (value.is_number_unsigned())
        {
            const auto integer = value.get<std::uint64_t>();
            if (integer > static_cast<std::uint64_t>(maximum))
                return std::nullopt;
            const auto signedInteger = static_cast<std::int64_t>(integer);
            return signedInteger >= minimum ? std::optional<int>(static_cast<int>(signedInteger)) : std::nullopt;
        }
        if (!value.is_number_integer())
            return std::nullopt;
        const auto integer = value.get<std::int64_t>();
        if (integer < minimum || integer > maximum)
            return std::nullopt;
        return static_cast<int>(integer);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::vector<int>> read_object_ids(const Json &value)
{
    if (!value.is_array())
        return std::nullopt;
    std::vector<int> ids;
    ids.reserve(value.size());
    for (const auto &entry : value)
    {
        const auto id = bounded_integer(entry, 0, std::numeric_limits<int>::max());
        if (!id)
            return std::nullopt;
        ids.push_back(*id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

void load_scene_id_map(const Json &source, const char *key,
                       std::map<std::string, std::vector<int>> &destination,
                       WorkspacePreferencesLoadResult &result)
{
    const auto found = source.find(key);
    if (found == source.end())
        return;
    if (!found->is_object())
    {
        set_recovered(result, std::string("invalid scene object IDs in '") + key + "' were reset");
        return;
    }
    for (const auto &[scenePath, values] : found->items())
    {
        auto ids = read_object_ids(values);
        if (scenePath.empty() || !ids)
        {
            set_recovered(result, std::string("invalid scene object IDs in '") + key + "' were ignored");
            continue;
        }
        destination.emplace(scenePath, std::move(*ids));
    }
}

void load_v2_preferences(const Json &source, WorkspacePreferences &preferences,
                         WorkspacePreferencesLoadResult &result)
{
    const auto panelHosts = source.find("panelHostAssignments");
    if (panelHosts != source.end())
    {
        if (!panelHosts->is_object())
            set_recovered(result, "invalid panel host assignments were reset");
        else
        {
            for (const auto &[panel, hostValue] : panelHosts->items())
            {
                if (panel.empty() || !hostValue.is_number_unsigned() && !hostValue.is_number_integer())
                {
                    set_recovered(result, "invalid panel host assignment was ignored");
                    continue;
                }
                try
                {
                    const auto raw = hostValue.get<std::uint64_t>();
                    if (raw == 0)
                    {
                        set_recovered(result, "zero host layout IDs are invalid");
                        continue;
                    }
                    preferences.panelHostAssignments[panel] = HostLayoutId{raw};
                }
                catch (...)
                {
                    set_recovered(result, "invalid panel host assignment was ignored");
                }
            }
        }
    }

    const auto hosts = source.find("hosts");
    if (hosts != source.end())
    {
        if (!hosts->is_array())
            set_recovered(result, "invalid host layout preferences were reset");
        else
        {
            for (const auto &host : *hosts)
            {
                if (!host.is_object() || !host.contains("id") ||
                    (!host["id"].is_number_unsigned() && !host["id"].is_number_integer()))
                {
                    set_recovered(result, "invalid host layout entry was ignored");
                    continue;
                }
                std::uint64_t rawId = 0;
                try { rawId = host["id"].get<std::uint64_t>(); } catch (...) { rawId = 0; }
                if (rawId == 0)
                {
                    set_recovered(result, "invalid host layout ID was ignored");
                    continue;
                }
                const HostLayoutId id{rawId};
                auto &geometry = preferences.hostGeometry[id];
                const auto geometryValue = host.find("geometry");
                if (geometryValue != host.end())
                {
                    if (!geometryValue->is_object())
                        set_recovered(result, "invalid host geometry was reset");
                    else
                    {
                        for (const auto &[name, destination] :
                             std::array<std::pair<const char *, int *>, 4>{{
                                 {"x", &geometry.x}, {"y", &geometry.y},
                                 {"width", &geometry.width}, {"height", &geometry.height}}})
                        {
                            const auto value = geometryValue->find(name);
                            if (value == geometryValue->end())
                                continue;
                            const auto parsed = bounded_integer(*value,
                                name == std::string_view("x") || name == std::string_view("y") ? -1000000 : 320,
                                name == std::string_view("x") || name == std::string_view("y") ? 1000000 : 16384);
                            if (parsed)
                                *destination = *parsed;
                            else
                                set_recovered(result, "invalid host geometry value was reset");
                        }
                        read_bool(*geometryValue, "maximized", geometry.maximized, result);
                    }
                }
                const auto settings = host.find("settings");
                if (settings != host.end())
                {
                    if (settings->is_string() && settings->get<std::string>().size() <= kMaximumLayoutBytes)
                        preferences.hostLayoutSettings[id] = settings->get<std::string>();
                    else
                        set_recovered(result, "invalid host layout settings were ignored");
                }
            }
        }
    }

    const auto tabs = source.find("openTabs");
    if (tabs != source.end())
    {
        if (!tabs->is_array() || tabs->size() > 128)
            set_recovered(result, "invalid open tab preferences were reset");
        else
        {
            for (const auto &entry : *tabs)
            {
                if (!entry.is_object() || !entry.contains("path") || !entry["path"].is_string() ||
                    entry["path"].get<std::string>().empty())
                {
                    set_recovered(result, "invalid saved tab path was ignored");
                    continue;
                }
                SavedTabPreferences tab;
                tab.documentPath = entry["path"].get<std::string>();
                if (const auto kind = entry.find("kind"); kind != entry.end())
                {
                    const auto parsedKind = parse_saved_document_kind(*kind);
                    if (parsedKind)
                        tab.kind = *parsedKind;
                    else
                        set_recovered(result, "invalid saved document kind was reset to Scene");
                }
                const auto view = entry.find("view");
                if (view != entry.end() && view->is_object())
                {
                    read_number(*view, "centerX", tab.viewCenterX, result, -10000000.0f, 10000000.0f);
                    read_number(*view, "centerY", tab.viewCenterY, result, -10000000.0f, 10000000.0f);
                    read_number(*view, "zoom", tab.viewZoom, result, 0.01f, 100.0f);
                }
                const auto expanded = entry.find("expandedHierarchyIds");
                if (expanded != entry.end())
                {
                    const auto ids = read_object_ids(*expanded);
                    if (ids)
                        tab.expandedHierarchyIds = *ids;
                    else
                        set_recovered(result, "invalid hierarchy expansion IDs were ignored");
                }
                read_bool(entry, "hasExpansionState", tab.hasExpansionState, result);
                preferences.openTabs.push_back(std::move(tab));
            }
        }
    }
    const auto activeTab = source.find("activeTabPath");
    if (activeTab != source.end())
    {
        if (activeTab->is_string() && !activeTab->get<std::string>().empty())
            preferences.activeTabPath = activeTab->get<std::string>();
        else
            set_recovered(result, "invalid active tab path was cleared");
    }
    if (const auto activeKind = source.find("activeTabKind"); activeKind != source.end())
    {
        const auto parsedKind = parse_saved_document_kind(*activeKind);
        if (parsedKind)
            preferences.activeDocumentKind = *parsedKind;
        else
            set_recovered(result, "invalid active document kind was reset to Scene");
    }
    load_scene_id_map(source, "hiddenObjectIdsByScene", preferences.hiddenObjectIdsByScene, result);
    load_scene_id_map(source, "lockedObjectIdsByScene", preferences.lockedObjectIdsByScene, result);

    const auto snap = source.find("snap");
    if (snap != source.end())
    {
        if (!snap->is_object())
            set_recovered(result, "invalid snapping preferences were reset");
        else
        {
            read_number(*snap, "position", preferences.snap.position, result, 0.0001f, 1000000.0f);
            read_number(*snap, "rotationDegrees", preferences.snap.rotationDegrees, result, 0.001f, 360.0f);
            read_number(*snap, "scale", preferences.snap.scale, result, 0.0001f, 10.0f);
            read_string(*snap, "coordinateMode", preferences.snap.coordinateMode, result);
            if (preferences.snap.coordinateMode != "world" && preferences.snap.coordinateMode != "local")
            {
                preferences.snap.coordinateMode = "world";
                set_recovered(result, "invalid snap coordinate mode was reset");
            }
        }
    }
    read_string(source, "duplicationReferencePolicy", preferences.duplicationReferencePolicy, result);
    if (preferences.duplicationReferencePolicy != "preserve-existing-targets" &&
        preferences.duplicationReferencePolicy != "remap-internal-targets")
    {
        preferences.duplicationReferencePolicy = "preserve-existing-targets";
        set_recovered(result, "invalid duplication reference policy was reset");
    }
    const auto limits = source.find("resourceLimits");
    if (limits != source.end())
    {
        if (!limits->is_object())
            set_recovered(result, "invalid resource limits were reset");
        else
        {
            for (const auto &[name, value] : limits->items())
            {
                if (name.empty() || (!value.is_number_unsigned() && !value.is_number_integer()))
                {
                    set_recovered(result, "invalid resource limit was ignored");
                    continue;
                }
                try
                {
                    const auto limit = value.get<std::uint64_t>();
                    if (limit == 0 || limit > (1ULL << 40))
                        throw std::out_of_range("resource limit out of range");
                    preferences.resourceLimits[name] = limit;
                }
                catch (...)
                {
                    set_recovered(result, "invalid resource limit was ignored");
                }
            }
        }
    }
    preferences.hostGeometry.try_emplace(main_host_layout_id);
}

Json serialize_preferences(const WorkspacePreferences &preferences, std::string_view projectKey)
{
    Json panels = Json::object();
    for (const auto &[id, visible] : preferences.panelVisibility)
        panels[id] = visible;

    Json panelHosts = Json::object();
    for (const auto &[id, host] : preferences.panelHostAssignments)
        panelHosts[id] = host.value;
    Json hosts = Json::array();
    std::map<HostLayoutId, bool> emittedHosts;
    for (const auto &[id, geometry] : preferences.hostGeometry)
    {
        Json host{{"id", id.value},
                  {"geometry", {{"x", geometry.x}, {"y", geometry.y},
                                {"width", geometry.width}, {"height", geometry.height},
                                {"maximized", geometry.maximized}}}};
        if (const auto settings = preferences.hostLayoutSettings.find(id);
            settings != preferences.hostLayoutSettings.end())
            host["settings"] = settings->second;
        hosts.push_back(std::move(host));
        emittedHosts.emplace(id, true);
    }
    for (const auto &[id, settings] : preferences.hostLayoutSettings)
        if (!emittedHosts.contains(id))
            hosts.push_back(Json{{"id", id.value}, {"settings", settings}});

    Json tabs = Json::array();
    for (const auto &tab : preferences.openTabs)
    {
        tabs.push_back(Json{{"path", tab.documentPath.string()},
                            {"kind", saved_document_kind_name(tab.kind)},
                            {"view", {{"centerX", tab.viewCenterX}, {"centerY", tab.viewCenterY},
                                       {"zoom", tab.viewZoom}}},
                            {"expandedHierarchyIds", tab.expandedHierarchyIds},
                            {"hasExpansionState", tab.hasExpansionState}});
    }
    auto serialize_scene_ids = [](const std::map<std::string, std::vector<int>> &byScene) {
        Json result = Json::object();
        for (const auto &[scenePath, ids] : byScene)
            result[scenePath] = ids;
        return result;
    };
    Json resourceLimits = Json::object();
    for (const auto &[name, value] : preferences.resourceLimits)
        resourceLimits[name] = value;
    Json run = {{"configurePreset", preferences.run.configurePreset},
                {"buildPreset", preferences.run.buildPreset},
                {"target", preferences.run.target},
                {"executable", preferences.run.executable},
                {"embeddedGameView", preferences.run.embeddedGameView},
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
                   {"panelHostAssignments", std::move(panelHosts)},
                   {"hosts", std::move(hosts)},
                   {"openTabs", std::move(tabs)},
                   {"hiddenObjectIdsByScene", serialize_scene_ids(preferences.hiddenObjectIdsByScene)},
                   {"lockedObjectIdsByScene", serialize_scene_ids(preferences.lockedObjectIdsByScene)},
                   {"snap", {{"position", preferences.snap.position},
                              {"rotationDegrees", preferences.snap.rotationDegrees},
                              {"scale", preferences.snap.scale},
                              {"coordinateMode", preferences.snap.coordinateMode}}},
                   {"duplicationReferencePolicy", preferences.duplicationReferencePolicy},
                   {"resourceLimits", std::move(resourceLimits)},
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
    if (preferences.activeTabPath)
        result["activeTabPath"] = preferences.activeTabPath->string();
    result["activeTabKind"] = saved_document_kind_name(preferences.activeDocumentKind);
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
            {"panel.project", true}, {"panel.console", true}, {"panel.game", false},
            {"panel.project_settings", false}, {"panel.validation", false}};
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

bool set_editor_object_id_state(std::vector<int> &ids, int objectId, bool enabled)
{
    if (objectId < 0)
        return false;
    const auto position = std::lower_bound(ids.begin(), ids.end(), objectId);
    const bool present = position != ids.end() && *position == objectId;
    if (present == enabled)
        return false;
    if (enabled)
        ids.insert(position, objectId);
    else
        ids.erase(position);
    return true;
}

bool prune_editor_object_ids(std::vector<int> &ids,
                             const std::unordered_set<int> &liveObjectIds)
{
    const auto previousSize = ids.size();
    std::erase_if(ids, [&liveObjectIds](int id) { return !liveObjectIds.contains(id); });
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids.size() != previousSize;
}

void reset_workspace_layout(WorkspacePreferences &preferences)
{
    preferences.panelVisibility = default_panel_visibility();
    for (auto &[panelId, hostId] : preferences.panelHostAssignments)
    {
        (void)panelId;
        hostId = main_host_layout_id;
    }
    std::erase_if(preferences.hostGeometry, [](const auto &entry) {
        return entry.first != main_host_layout_id;
    });
    preferences.hostLayoutSettings.clear();
    preferences.hostGeometry.try_emplace(main_host_layout_id);
    preferences.userScale = 1.0f;
    preferences.navigationZoom = 1.0f;
    preferences.navigationX = 0.0f;
    preferences.navigationY = 0.0f;
}

bool layout_needs_recovery(std::string_view layout, int displayWidth, int displayHeight)
{
    if (layout.empty() || layout.size() > kMaximumLayoutBytes || displayWidth <= 0 || displayHeight <= 0 ||
        layout.find("[Docking][Data]") == std::string_view::npos ||
        layout.find('\0') != std::string_view::npos)
        return true;

    const auto dockspacePos = layout.find("DockSpace");
    if (dockspacePos == std::string_view::npos || layout.find("ID=0x", dockspacePos) == std::string_view::npos)
        return true;

    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool hasPosition = false;
    bool hasSize = false;
    bool isDocked = false;
    bool inWindowSection = false;
    bool hasDockspaceGeometry = false;
    const auto checkWindow = [&] {
        if (isDocked)
            return true;
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
        auto trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed.remove_prefix(1);
        if (trimmed.starts_with("DockSpace"))
        {
            auto after = trimmed.substr(9);
            while (!after.empty() && after.front() == ' ')
                after.remove_prefix(1);
            if (after.starts_with("ID=0x"))
            {
                const auto posIndex = line.find("Pos=");
                const auto sizeIndex = line.find("Size=");
                if (posIndex != std::string_view::npos && sizeIndex != std::string_view::npos)
                {
                    const auto posVal = line.substr(posIndex + 4);
                    const auto posEnd = posVal.find(' ');
                    const auto posStr = posEnd == std::string_view::npos ? posVal : posVal.substr(0, posEnd);

                    const auto sizeVal = line.substr(sizeIndex + 5);
                    const auto sizeEnd = sizeVal.find(' ');
                    const auto sizeStr = sizeEnd == std::string_view::npos ? sizeVal : sizeVal.substr(0, sizeEnd);

                    int rootX = 0;
                    int rootY = 0;
                    int rootWidth = 0;
                    int rootHeight = 0;
                    if (!parse_pair(posStr, rootX, rootY) ||
                        !parse_pair(sizeStr, rootWidth, rootHeight) ||
                        rootWidth <= 0 || rootHeight <= 0)
                        return true;
                    hasDockspaceGeometry = true;
                }
            }
        }
        if (line.starts_with("[Window]["))
        {
            if (inWindowSection && !checkWindow())
                return true;
            inWindowSection = true;
            x = y = width = height = 0;
            hasPosition = hasSize = false;
            isDocked = false;
        }
        else if (line.starts_with("["))
        {
            if (inWindowSection && !checkWindow())
                return true;
            inWindowSection = false;
        }
        else if (inWindowSection)
        {
            if (line.starts_with("DockId=0x"))
            {
                const auto idPart = line.substr(7);
                if (idPart.find_first_not_of("0,") != std::string_view::npos)
                    isDocked = true;
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
        }
        if (end == std::string_view::npos)
            break;
        offset = end + 1;
    }
    if (inWindowSection && !checkWindow())
        return true;
    return !hasDockspaceGeometry;
}

std::string migrate_scene_window_identity(std::string_view layout)
{
    constexpr std::string_view legacySection = "[Window][Scene]";
    constexpr std::string_view stableSection = "[Window][panel.scene]";
    constexpr std::string_view retiredBehaviorSection = "[Window][panel.behaviors]";
    constexpr std::string_view retiredSettingsSection = "[Window][panel.settings]";
    constexpr std::string_view gameSection = "[Window][panel.game]";
    constexpr std::string_view animSection = "[Window][animation-editor]";
    const bool hasDocking = layout.find("[Docking][Data]") != std::string_view::npos;
    const bool hasLegacy = layout.find(legacySection) != std::string_view::npos;
    const bool hasStable = layout.find(stableSection) != std::string_view::npos;
    const bool hasRetiredBehavior = layout.find(retiredBehaviorSection) != std::string_view::npos;
    const bool hasRetiredSettings = layout.find(retiredSettingsSection) != std::string_view::npos;
    const bool hasGame = layout.find(gameSection) != std::string_view::npos;
    const bool hasLegacyBottomSplit = layout.find("0x00004000") != std::string_view::npos ||
                                      layout.find("0x00004001") != std::string_view::npos;
    const bool hasAnim = layout.find(animSection) != std::string_view::npos;
    const bool needsGameMigration = hasDocking && (hasLegacy || hasStable) && !hasGame;
    const bool needsAnimMigration = hasDocking && hasLegacyBottomSplit && !hasAnim;
    if (!hasLegacy && !hasRetiredBehavior && !hasRetiredSettings && !needsGameMigration && !hasLegacyBottomSplit)
        return std::string(layout);

    std::string migrated;
    migrated.reserve(layout.size() + 256);
    bool skippingLegacy = false;
    std::string currentWindow;
    std::size_t offset = 0;
    while (offset < layout.size())
    {
        const auto end = layout.find('\n', offset);
        auto line = layout.substr(offset, end == std::string_view::npos ? layout.size() - offset
                                                                        : end - offset);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line == retiredBehaviorSection || line == retiredSettingsSection)
        {
            skippingLegacy = true;
            currentWindow.clear();
        }
        else if (line == legacySection)
        {
            skippingLegacy = hasStable;
            if (!hasStable)
                migrated.append(stableSection);
            currentWindow = "panel.scene";
        }
        else if (line.starts_with("["))
        {
            skippingLegacy = false;
            if (line.starts_with("[Window]["))
            {
                currentWindow = std::string(line.substr(9));
                if (!currentWindow.empty() && currentWindow.back() == ']')
                    currentWindow.pop_back();
            }
            else
            {
                currentWindow.clear();
            }
            migrated.append(line);
        }
        else if (hasLegacyBottomSplit && (line.find("ID=0x00004000") != std::string_view::npos ||
                                          line.find("ID=0x00004001") != std::string_view::npos))
        {
            // Skip legacy bottom child dock nodes completely
            offset = end == std::string_view::npos ? layout.size() : end + 1;
            continue;
        }
        else if (hasLegacyBottomSplit && line.find("ID=0x00001001") != std::string_view::npos)
        {
            // Remove Split=X from 0x00001001
            std::string modified(line);
            const auto splitPos = modified.find("Split=X");
            if (splitPos != std::string::npos)
            {
                modified.erase(splitPos, 7);
                while (!modified.empty() && modified.back() == ' ')
                    modified.pop_back();
            }
            migrated.append(modified);
        }
        else if (hasLegacyBottomSplit && line.starts_with("DockId="))
        {
            if (line.find("0x00004000") != std::string_view::npos ||
                line.find("0x00004001") != std::string_view::npos)
            {
                if (currentWindow == "panel.project")
                    migrated.append("DockId=0x00001001,0");
                else if (currentWindow == "panel.console")
                    migrated.append("DockId=0x00001001,1");
                else if (currentWindow == "animation-editor")
                    migrated.append("DockId=0x00001001,2");
                else if (currentWindow == "panel.run" || currentWindow == "panel.project_settings")
                    migrated.append("DockId=0x00001001,3");
                else if (currentWindow == "panel.validation")
                    migrated.append("DockId=0x00001001,4");
                else
                {
                    std::string modified(line);
                    size_t p = modified.find("0x00004000");
                    if (p != std::string::npos) modified.replace(p, 10, "0x00001001");
                    p = modified.find("0x00004001");
                    if (p != std::string::npos) modified.replace(p, 10, "0x00001001");
                    migrated.append(modified);
                }
            }
            else
            {
                migrated.append(line);
            }
        }
        else if (!skippingLegacy)
        {
            migrated.append(line);
        }

        if (end != std::string_view::npos)
            migrated.push_back('\n');
        offset = end == std::string_view::npos ? layout.size() : end + 1;
    }
    if (needsGameMigration)
    {
        if (!migrated.empty() && migrated.back() != '\n')
            migrated.push_back('\n');
        migrated.append("[Window][panel.game]\nPos=260,0\nSize=720,570\nDockId=0x00003000,1\n");
    }
    if (needsAnimMigration)
    {
        if (!migrated.empty() && migrated.back() != '\n')
            migrated.push_back('\n');
        migrated.append("[Window][animation-editor]\nPos=0,500\nSize=1280,300\nDockId=0x00001001,2\n");
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
                (version->get<int>() != kPreferencesVersion &&
                 version->get<int>() != kLegacyPreferencesVersion) ||
                project == source.end() || !project->is_string() ||
                project->get<std::string>() != result.preferences.projectKey)
            {
                set_recovered(result, "settings version or project identity did not match; defaults were loaded");
            }
            else
            {
                const int sourceVersion = version->get<int>();
                result.preferences.version = kPreferencesVersion;
                result.migrated = sourceVersion == kLegacyPreferencesVersion;
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
                if (sourceVersion == kPreferencesVersion)
                    load_v2_preferences(source, result.preferences, result);
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
    result.layout = migrate_scene_window_identity(result.layout);
    if (result.layout.empty())
    {
        const auto mainSettings = result.preferences.hostLayoutSettings.find(main_host_layout_id);
        if (mainSettings != result.preferences.hostLayoutSettings.end())
            result.layout = mainSettings->second;
    }
    if (!result.layout.empty())
        result.preferences.hostLayoutSettings[main_host_layout_id] = result.layout;
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
    }
}

void apply_generated_project_run_defaults(WorkspacePreferences &preferences,
                                          const std::filesystem::path &projectRoot)
{
    std::string generatedTarget = projectRoot.filename().string();
    std::string sanitizedTarget;
    for (const char character : generatedTarget)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0 || character == '_')
            sanitizedTarget.push_back(character);
        else if (character == '-' || character == ' ')
            sanitizedTarget.push_back('_');
    }
    generatedTarget = std::move(sanitizedTarget);
    if (generatedTarget.empty() ||
        std::isdigit(static_cast<unsigned char>(generatedTarget.front())) != 0)
        generatedTarget.insert(generatedTarget.begin(), 'p');
    if (preferences.run.target.empty())
        preferences.run.target = generatedTarget;
    if (preferences.run.executable.empty())
    {
        const std::string executableTarget = preferences.run.target.empty()
            ? generatedTarget : preferences.run.target;
        std::filesystem::path executable =
            std::filesystem::path("bin") / "debug" / executableTarget;
#if defined(_WIN32)
        executable += ".exe";
#endif
        preferences.run.executable = executable.generic_string();
    }

    if (!preferences.recentScene)
    {
        const auto scenesDirectory = projectRoot / "assets" / "scenes";
        const auto mainScene = scenesDirectory / "main.scene";
        std::error_code error;
        if (std::filesystem::is_regular_file(mainScene, error) && !error)
            preferences.recentScene = mainScene;
        else
        {
            error.clear();
            std::vector<std::filesystem::path> scenes;
            for (std::filesystem::directory_iterator it(scenesDirectory, error), end;
                 !error && it != end; it.increment(error))
            {
                std::error_code entryError;
                if (it->path().extension() == ".scene" && it->is_regular_file(entryError) && !entryError)
                    scenes.push_back(it->path());
            }
            if (!error && !scenes.empty())
            {
                std::sort(scenes.begin(), scenes.end());
                preferences.recentScene = scenes.front();
            }
        }
    }
    if (!preferences.activeTabPath && preferences.recentScene)
    {
        preferences.activeTabPath = preferences.recentScene;
        preferences.activeDocumentKind = SavedDocumentKind::Scene;
    }
}

WorkspacePreferencesSaveResult save_workspace_preferences(const ProjectPreferencePaths &paths,
                                                          const WorkspacePreferences &preferences,
                                                          const std::string &layout,
                                                          bool writeSettings,
                                                          bool writeLayout)
{
    WorkspacePreferencesSaveResult result{!writeSettings, !writeLayout, {}};
    const auto validation_failure = [&](const char *message) {
        result.settingsSaved = !writeSettings;
        result.layoutSaved = !writeLayout;
        result.message = message;
        return result;
    };
    if (writeSettings)
    {
        if (preferences.projectKey != paths.projectKey)
            return validation_failure("preferences belong to a different project");
        if (!std::isfinite(preferences.userScale) || preferences.userScale < 0.5f || preferences.userScale > 3.0f ||
            !std::isfinite(preferences.navigationZoom) || preferences.navigationZoom < 0.01f ||
            preferences.navigationZoom > 100.0f || !std::isfinite(preferences.navigationX) ||
            !std::isfinite(preferences.navigationY) || preferences.editorWindow.width < 320 ||
            preferences.editorWindow.width > 16384 || preferences.editorWindow.height < 320 ||
            preferences.editorWindow.height > 16384 || preferences.run.width < 1 || preferences.run.width > 16384 ||
            preferences.run.height < 1 || preferences.run.height > 16384 ||
            !std::isfinite(preferences.snap.position) || preferences.snap.position <= 0.0f ||
            preferences.snap.position > 1000000.0f || !std::isfinite(preferences.snap.rotationDegrees) ||
            preferences.snap.rotationDegrees <= 0.0f || preferences.snap.rotationDegrees > 360.0f ||
            !std::isfinite(preferences.snap.scale) || preferences.snap.scale <= 0.0f ||
            preferences.snap.scale > 10.0f || preferences.openTabs.size() > 128)
            return validation_failure("preferences contain out-of-range values");
        if ((preferences.snap.coordinateMode != "world" && preferences.snap.coordinateMode != "local") ||
            (preferences.duplicationReferencePolicy != "preserve-existing-targets" &&
             preferences.duplicationReferencePolicy != "remap-internal-targets"))
            return validation_failure("preferences contain an unsupported editing policy");
        for (const auto &[host, geometry] : preferences.hostGeometry)
            if (!host || geometry.width < 320 || geometry.width > 16384 ||
                geometry.height < 320 || geometry.height > 16384 ||
                geometry.x < -1000000 || geometry.x > 1000000 ||
                geometry.y < -1000000 || geometry.y > 1000000)
                return validation_failure("host geometry or identity is out of range");
        for (const auto &[panel, host] : preferences.panelHostAssignments)
            if (panel.empty() || !host)
                return validation_failure("panel host assignments require stable panel and host IDs");
        for (const auto &[host, settings] : preferences.hostLayoutSettings)
            if (!host || settings.size() > kMaximumLayoutBytes)
                return validation_failure("host layout settings exceed their size or identity limit");
        for (const auto &tab : preferences.openTabs)
        {
            if (tab.documentPath.empty() || !std::isfinite(tab.viewCenterX) || !std::isfinite(tab.viewCenterY) ||
                !std::isfinite(tab.viewZoom) || tab.viewZoom < 0.01f || tab.viewZoom > 100.0f ||
                std::any_of(tab.expandedHierarchyIds.begin(), tab.expandedHierarchyIds.end(),
                            [](int id) { return id < 0; }))
                return validation_failure("saved tab view or hierarchy state is invalid");
        }
        auto valid_scene_ids = [](const auto &states) {
            for (const auto &[scenePath, ids] : states)
                if (scenePath.empty() || std::any_of(ids.begin(), ids.end(), [](int id) { return id < 0; }))
                    return false;
            return true;
        };
        if (!valid_scene_ids(preferences.hiddenObjectIdsByScene) ||
            !valid_scene_ids(preferences.lockedObjectIdsByScene))
            return validation_failure("hidden and locked object IDs must be scene-scoped nonnegative IDs");
        for (const auto &[name, limit] : preferences.resourceLimits)
            if (name.empty() || limit == 0 || limit > (1ULL << 40))
                return validation_failure("resource limit is outside the supported range");
    }
    if (writeLayout && layout.size() > kMaximumLayoutBytes)
        return validation_failure("layout preferences exceed the 1 MiB limit");
    if (!writeSettings && !writeLayout)
        return result;

    std::error_code error;
    const auto parent = writeSettings ? paths.settingsPath.parent_path()
                                     : paths.layoutPath.parent_path();
    std::filesystem::create_directories(parent, error);
    if (error)
    {
        result.settingsSaved = !writeSettings;
        result.layoutSaved = !writeLayout;
        result.message = "cannot create preference directory: " + error.message();
        return result;
    }

    bool durabilityWarning = false;
    if (writeSettings)
    {
        WorkspacePreferences serializedPreferences = preferences;
        serializedPreferences.version = kPreferencesVersion;
        serializedPreferences.hostGeometry.try_emplace(main_host_layout_id);
        serializedPreferences.hostLayoutSettings[main_host_layout_id] = layout;
        const auto settings = serialize_preferences(serializedPreferences, paths.projectKey).dump(2) + "\n";
        work::record(work::Counter::PreferenceSettingsWriteAttempts);
        const auto settingsWrite = write_file_atomically(paths.settingsPath, settings, true);
        result.settingsSaved = settingsWrite.succeeded();
        durabilityWarning = durabilityWarning ||
            settingsWrite.status == AtomicWriteStatus::DurabilityWarning;
        if (!result.settingsSaved)
            result.message = "settings write failed: " + settingsWrite.message;
    }

    if (writeLayout)
    {
        work::record(work::Counter::PreferenceLayoutWriteAttempts);
        const auto layoutWrite = write_file_atomically(paths.layoutPath, layout, true);
        result.layoutSaved = layoutWrite.succeeded();
        durabilityWarning = durabilityWarning ||
            layoutWrite.status == AtomicWriteStatus::DurabilityWarning;
        if (!result.layoutSaved)
        {
            if (!result.message.empty())
                result.message += "; ";
            result.message += "layout write failed: " + layoutWrite.message;
        }
    }
    if (result.succeeded() && durabilityWarning)
        result.message = "preferences were replaced, but directory durability could not be confirmed";
    return result;
}

WorkspacePreferencePersistence::WorkspacePreferencePersistence(
    WorkspacePreferences persistedPreferences, std::string persistedLayout)
    : persistedPreferences_(std::move(persistedPreferences)), persistedLayout_(std::move(persistedLayout))
{
}

WorkspacePreferencesSaveResult WorkspacePreferencePersistence::flush(
    const WorkspacePreferences &preferences, const std::string &layout, const Writer &writer)
{
    const bool settingsDirty = preferences != persistedPreferences_;
    const bool layoutDirty = layout != persistedLayout_;
    if (!settingsDirty && !layoutDirty)
        return {true, true, {}};

    if (!writer)
        return {!settingsDirty, !layoutDirty, "preference writer is unavailable"};

    auto result = writer(settingsDirty, layoutDirty);
    if (settingsDirty && result.settingsSaved)
        persistedPreferences_ = preferences;
    if (layoutDirty && result.layoutSaved)
        persistedLayout_ = layout;
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

GlobalPreferencesLoadResult load_global_preferences(const std::filesystem::path &path)
{
    GlobalPreferencesLoadResult result;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec == std::errc::no_such_file_or_directory)
        return result;
    if (ec)
    {
        result.message = "Global preferences could not be inspected: " + ec.message();
        return result;
    }
    if (size > 256U * 1024U)
    {
        result.message = "Global preferences exceed size limit and were ignored.";
        return result;
    }
    try
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot open global preferences");
        const Json doc = Json::parse(stream);
        if (!doc.is_object())
            return result;
        if (const auto lastIt = doc.find("lastProjectRoot");
            lastIt != doc.end() && lastIt->is_string() && !lastIt->get<std::string>().empty())
            result.preferences.lastProjectRoot = std::filesystem::path(lastIt->get<std::string>());
        if (const auto rootIt = doc.find("rememberedPlatformatorRoot");
            rootIt != doc.end() && rootIt->is_string() && !rootIt->get<std::string>().empty())
            result.preferences.rememberedPlatformatorRoot = std::filesystem::path(rootIt->get<std::string>());
        if (const auto recentIt = doc.find("recentProjects"); recentIt != doc.end() && recentIt->is_array())
        {
            for (const auto &entry : *recentIt)
            {
                if (!entry.is_object())
                    continue;
                const auto p = entry.find("rootPath");
                if (p == entry.end() || !p->is_string() || p->get<std::string>().empty())
                    continue;
                RecentProjectEntry item;
                item.rootPath = std::filesystem::path(p->get<std::string>());
                item.name = entry.value("name", item.rootPath.filename().string());
                item.lastOpenedUtc = entry.value("lastOpenedUtc", std::int64_t{0});
                result.preferences.recentProjects.push_back(std::move(item));
            }
        }
    }
    catch (const std::exception &ex)
    {
        result.preferences = GlobalPreferences{};
        result.message = std::string("Global preferences could not be read: ") + ex.what();
    }
    return result;
}

bool save_global_preferences(const std::filesystem::path &path,
                             const GlobalPreferences &preferences, std::string &error)
{
    try
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "cannot create global preference directory: " + ec.message();
            return false;
        }
        Json recentJson = Json::array();
        for (const auto &entry : preferences.recentProjects)
        {
            recentJson.push_back({{"name", entry.name},
                                  {"rootPath", canonical_project_key(entry.rootPath)},
                                  {"lastOpenedUtc", entry.lastOpenedUtc}});
        }
        Json doc = {{"version", 1}, {"recentProjects", std::move(recentJson)}};
        if (preferences.lastProjectRoot && !preferences.lastProjectRoot->empty())
            doc["lastProjectRoot"] = canonical_project_key(*preferences.lastProjectRoot);
        if (preferences.rememberedPlatformatorRoot && !preferences.rememberedPlatformatorRoot->empty())
            doc["rememberedPlatformatorRoot"] = canonical_project_key(*preferences.rememberedPlatformatorRoot);

        const auto result = write_file_atomically(path, doc.dump(2) + "\n", true);
        if (!result.succeeded())
        {
            error = "cannot save global preferences: " + result.message;
            return false;
        }
        error.clear();
        return true;
    }
    catch (const std::exception &ex)
    {
        error = std::string("cannot save global preferences: ") + ex.what();
        return false;
    }
}

void record_recent_project(GlobalPreferences &preferences,
                           const std::filesystem::path &projectRoot,
                           std::string_view displayName,
                           std::int64_t timestampUtc)
{
    if (projectRoot.empty())
        return;
    const std::string key = canonical_project_key(projectRoot);
    const std::filesystem::path canonicalPath(key);
    std::string resolvedName(displayName);
    if (resolvedName.empty())
        resolvedName = canonicalPath.filename().string();
    if (resolvedName.empty())
        resolvedName = key;

    std::int64_t ts = timestampUtc;
    if (ts <= 0)
        ts = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

    preferences.lastProjectRoot = canonicalPath;
    std::erase_if(preferences.recentProjects, [&](const RecentProjectEntry &item) {
        return canonical_project_key(item.rootPath) == key;
    });
    preferences.recentProjects.insert(preferences.recentProjects.begin(),
                                      RecentProjectEntry{std::move(resolvedName), canonicalPath, ts});
    constexpr std::size_t MaxRecentProjects = 10;
    if (preferences.recentProjects.size() > MaxRecentProjects)
        preferences.recentProjects.resize(MaxRecentProjects);
}

void remove_recent_project(GlobalPreferences &preferences, const std::filesystem::path &projectRoot)
{
    const std::string key = canonical_project_key(projectRoot);
    std::erase_if(preferences.recentProjects, [&](const RecentProjectEntry &item) {
        return canonical_project_key(item.rootPath) == key;
    });
    if (preferences.lastProjectRoot && canonical_project_key(*preferences.lastProjectRoot) == key)
        preferences.lastProjectRoot.reset();
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
