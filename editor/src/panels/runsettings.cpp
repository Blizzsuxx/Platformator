#include "runsettings.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
std::string path_to_utf8(const std::filesystem::path &path)
{
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

void input_string(const char *label, std::string &value, std::size_t capacity,
                  std::map<std::string, std::string> &drafts,
                  std::map<std::string, std::string> &committed)
{
    const std::string key(label);
    auto [committedIt, inserted] = committed.try_emplace(key, value);
    auto [draftIt, draftInserted] = drafts.try_emplace(key, value);
    if (!inserted && committedIt->second != value)
    {
        committedIt->second = value;
        draftIt->second = value;
    }
    (void)draftInserted;
    std::array<char, 1024> buffer{};
    const auto available = std::min(capacity, buffer.size());
    const auto size = std::min(draftIt->second.size(), available - 1);
    std::memcpy(buffer.data(), draftIt->second.data(), size);
    const bool changed = ImGui::InputText(label, buffer.data(), available,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    draftIt->second.assign(buffer.data());
    if (changed || ImGui::IsItemDeactivatedAfterEdit())
    {
        value = draftIt->second;
        committedIt->second = value;
    }
}

const char *stage_name(PipelineStage stage)
{
    switch (stage)
    {
    case PipelineStage::Idle: return "Idle";
    case PipelineStage::Configuring: return "Configuring";
    case PipelineStage::Building: return "Building";
    case PipelineStage::QueryingCatalog: return "Verifying behavior catalog";
    case PipelineStage::Launching: return "Launching";
    case PipelineStage::Running: return "Running";
    }
    return "Unknown";
}
} // namespace

void RunSettingsPanel::set_project_root(std::filesystem::path projectRoot)
{
    if (projectRoot_ == projectRoot)
        return;
    projectRoot_ = std::move(projectRoot);
    semanticChoices_.clear();
    refresh_discovery();
}

void RunSettingsPanel::refresh_discovery(bool requestFileApiQuery)
{
    semanticChoices_.clear();
    discovery_ = discover_project_run_options(projectRoot_, requestFileApiQuery);
}

std::vector<SemanticNode> RunSettingsPanel::semantic_nodes(
    const RunPreferences &preferences, std::string_view parentId, bool busy)
{
    const std::string parent(parentId);
    semanticChoices_.clear();
    std::vector<SemanticNode> nodes;
    nodes.reserve(24 + discovery_.configurePresets.size() + discovery_.buildPresets.size() +
                  discovery_.executableTargets.size());
    const auto add_value = [&](std::string suffix, std::string name,
                               std::string value, SemanticRole role = SemanticRole::Text) {
        SemanticNode node;
        node.id = parent + "/options/" + suffix;
        node.parentId = parent;
        node.role = role;
        node.name = std::move(name);
        if (value.size() > 512)
        {
            value.resize(509);
            value += "...";
        }
        node.value = std::move(value);
        node.visible = true;
        nodes.push_back(std::move(node));
    };
    const auto bool_value = [&](std::string suffix, std::string name, bool value,
                                bool enabled = true) {
        add_value(std::move(suffix), std::move(name), value ? "on" : "off",
                  SemanticRole::CheckBox);
        nodes.back().selected = value;
        if (enabled && !busy)
            nodes.back().actions = {"toggle"};
        nodes.back().enabled = enabled && !busy;
    };

    const auto add_choice_group = [&](std::string suffix, std::string name) {
        SemanticNode group;
        group.id = parent + "/options/choices/" + suffix;
        group.parentId = parent;
        group.role = SemanticRole::Group;
        group.name = std::move(name);
        group.visible = true;
        group.expanded = true;
        nodes.push_back(std::move(group));
        return nodes.back().id;
    };
    const auto add_choice = [&](const std::string &groupId, std::string choiceId,
                                std::string name, std::string value, bool selected,
                                SemanticChoice choice, std::string help = {}) {
        SemanticNode node;
        node.id = groupId + "/choice/" + std::move(choiceId);
        node.parentId = groupId;
        node.role = SemanticRole::ListItem;
        node.name = std::move(name);
        node.value = std::move(value);
        node.help = std::move(help);
        node.visible = true;
        node.enabled = !busy;
        node.selected = selected;
        if (!busy)
            node.actions = {"activate"};
        semanticChoices_.emplace(node.id, std::move(choice));
        nodes.push_back(std::move(node));
    };
    const auto encode_id = [](std::string_view value) {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(value.size() * 2);
        for (const unsigned char byte : value)
        {
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0f]);
        }
        return result;
    };

    add_value("discovery", "Project preset and executable discovery",
              discovery_.status.empty() ? "Discovery has not run." : discovery_.status);
    add_value("configure-preset", "Configure preset",
              preferences.configurePreset.empty() ? "None selected" : preferences.configurePreset);
    const std::string configureGroup = add_choice_group(
        "configure-presets", "Configure preset choices");
    for (const auto &preset : discovery_.configurePresets)
        add_choice(configureGroup, encode_id(preset), preset, preset,
                   preferences.configurePreset == preset,
                   {SemanticChoice::Kind::ConfigurePreset, preset, {}});

    add_value("build-preset", "Build preset",
              preferences.buildPreset.empty() ? "None selected" : preferences.buildPreset);
    const std::string buildGroup = add_choice_group("build-presets", "Build preset choices");
    for (const auto &preset : discovery_.buildPresets)
        add_choice(buildGroup, encode_id(preset), preset, preset,
                   preferences.buildPreset == preset,
                   {SemanticChoice::Kind::BuildPreset, preset, {}});

    add_value("target", "Executable target",
              preferences.target.empty() ? "None selected" : preferences.target);
    const std::string targetGroup = add_choice_group("executable-targets", "Built executable choices");
    for (const auto &target : discovery_.executableTargets)
    {
        const std::string executable = path_to_utf8(target.executablePath);
        std::string key = target.targetName;
        key.push_back('\0');
        key += executable;
        add_choice(targetGroup, encode_id(key), target.targetName, executable,
                   preferences.target == target.targetName && preferences.executable == executable,
                   {SemanticChoice::Kind::Executable, target.targetName, executable},
                   "Select this target and executable for Build and Run.");
    }
    add_value("executable", "Game executable path",
              preferences.executable.empty() ? "None selected" : preferences.executable);
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    constexpr bool sessionControlsEnabled = true;
#else
    constexpr bool sessionControlsEnabled = false;
#endif
    bool_value("embedded-game", "Embed Game view in editor", preferences.embeddedGameView,
               sessionControlsEnabled);
    add_value("catalog", "Automatic Run catalog integration",
              preferences.catalogProtocol == "common-runner-v1"
                  ? "common-runner-v1 available" : "Unavailable",
              SemanticRole::Text);
    nodes.back().error = preferences.catalogProtocol != "common-runner-v1";
    nodes.back().help = preferences.catalogProtocol == "common-runner-v1"
        ? "The selected runner declares the compatible behavior catalog protocol."
        : "Automatic Run stays disabled until the runner exports linked behaviors through common-runner-v1. Custom entries remain editable and buildable.";
    bool_value("production", "Production profile", preferences.production);
    const std::string catalogGroup = add_choice_group("catalog-protocols", "Catalog protocol choices");
    for (const std::string protocol : {std::string("unavailable"), std::string("common-runner-v1")})
        add_choice(catalogGroup, encode_id(protocol), protocol, protocol,
                   preferences.catalogProtocol == protocol,
                   {SemanticChoice::Kind::CatalogProtocol, protocol, {}});
    add_value("window-size", "Game window size",
              std::to_string(preferences.width) + " by " + std::to_string(preferences.height));
    const auto add_dimension = [&](std::string suffix, std::string name, int value) {
        SemanticNode node;
        node.id = parent + "/options/" + suffix;
        node.parentId = parent;
        node.role = SemanticRole::TextField;
        node.name = std::move(name);
        node.value = std::to_string(value);
        node.minimum = 1.0;
        node.maximum = 16384.0;
        node.visible = true;
        node.enabled = !busy;
        if (!busy)
            node.actions = {"set_value"};
        nodes.push_back(std::move(node));
    };
    add_dimension("window-width", "Game window width", preferences.width);
    add_dimension("window-height", "Game window height", preferences.height);
    bool_value("fullscreen", "Game fullscreen", preferences.fullscreen);
    bool_value("maximized", "Game maximized", preferences.maximized);
    bool_value("preserve-aspect", "Preserve game aspect ratio", preferences.preserveAspect);
    bool_value("start-paused", "Start game paused",
               !preferences.production && preferences.startPaused, !preferences.production);
    constexpr std::array<std::pair<std::string_view, std::string_view>, 4> overlays{{
        {"colliders", "Show colliders"}, {"collision-points", "Show collision points"},
        {"collision-normals", "Show collision normals"}, {"grid-cells", "Show grid cells"}}};
    for (const auto &[id, name] : overlays)
    {
        const bool enabled = !preferences.production &&
            (preferences.overlays.empty() ||
             std::find(preferences.overlays.begin(), preferences.overlays.end(), id) !=
                 preferences.overlays.end());
        bool_value(std::string("overlay/") + std::string(id), std::string(name), enabled,
                   !preferences.production);
    }
    return nodes;
}

bool RunSettingsPanel::dispatch_semantic_action(RunPreferences &preferences,
                                                const SemanticAction &action)
{
    if (action.action == "set_value")
    {
        int value = 0;
        const auto parsed = std::from_chars(action.value.data(),
            action.value.data() + action.value.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != action.value.data() + action.value.size() ||
            value < 1 || value > 16384)
            return false;
        if (action.nodeId == "main/panel/run/options/window-width")
            preferences.width = value;
        else if (action.nodeId == "main/panel/run/options/window-height")
            preferences.height = value;
        else
            return false;
        return true;
    }
    if (action.action == "activate")
    {
        const auto choice = semanticChoices_.find(action.nodeId);
        if (choice == semanticChoices_.end())
            return false;
        switch (choice->second.kind)
        {
        case SemanticChoice::Kind::ConfigurePreset:
            preferences.configurePreset = choice->second.value;
            return true;
        case SemanticChoice::Kind::BuildPreset:
            preferences.buildPreset = choice->second.value;
            return true;
        case SemanticChoice::Kind::Executable:
            preferences.target = choice->second.value;
            preferences.executable = choice->second.executable;
            return true;
        case SemanticChoice::Kind::CatalogProtocol:
            preferences.catalogProtocol = choice->second.value;
            return true;
        }
    }
    if (action.action != "toggle")
        return false;

    constexpr std::string_view prefix = "main/panel/run/options/";
    if (!action.nodeId.starts_with(prefix))
        return false;
    const std::string_view control(action.nodeId.data() + prefix.size(),
                                   action.nodeId.size() - prefix.size());
    if (control == "embedded-game")
    {
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        preferences.embeddedGameView = !preferences.embeddedGameView;
        return true;
#else
        return false;
#endif
    }
    if (control == "production")
    {
        preferences.production = !preferences.production;
        if (preferences.configurePreset == "production" || preferences.configurePreset == "debug")
            preferences.configurePreset = preferences.production ? "production" : "debug";
        if (preferences.buildPreset == "production" || preferences.buildPreset == "debug")
            preferences.buildPreset = preferences.production ? "production" : "debug";
        return true;
    }
    if (control == "fullscreen")
        preferences.fullscreen = !preferences.fullscreen;
    else if (control == "maximized")
        preferences.maximized = !preferences.maximized;
    else if (control == "preserve-aspect")
        preferences.preserveAspect = !preferences.preserveAspect;
    else if (control == "start-paused")
    {
        if (preferences.production)
            return false;
        preferences.startPaused = !preferences.startPaused;
    }
    else if (control.starts_with("overlay/"))
    {
        if (preferences.production)
            return false;
        const std::string_view overlay = control.substr(std::string_view("overlay/").size());
        constexpr std::array<std::string_view, 4> validOverlays{
            "colliders", "collision-points", "collision-normals", "grid-cells"};
        if (std::find(validOverlays.begin(), validOverlays.end(), overlay) == validOverlays.end())
            return false;
        if (preferences.overlays.empty())
            preferences.overlays = {"colliders", "collision-points", "collision-normals", "grid-cells"};
        preferences.overlays.erase(std::remove(preferences.overlays.begin(), preferences.overlays.end(), "none"),
                                   preferences.overlays.end());
        const auto found = std::find(preferences.overlays.begin(), preferences.overlays.end(), overlay);
        if (found == preferences.overlays.end())
            preferences.overlays.emplace_back(overlay);
        else
            preferences.overlays.erase(found);
        if (preferences.overlays.empty())
            preferences.overlays = {"none"};
        return true;
    }
    else
        return false;
    return true;
}

RunPanelAction RunSettingsPanel::draw(RunPreferences &preferences, const PipelineStatus &status,
                                      bool busy, bool documentDirty)
{
    RunPanelAction action = RunPanelAction::None;
    focusedControl_.clear();
    const auto remember_focus = [&](std::string_view id) {
        if (ImGui::IsItemFocused())
            focusedControl_ = id;
    };
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Rediscover Targets"))
    {
        refresh_discovery(true);
        action = RunPanelAction::RefreshDiscovery;
    }
    remember_focus("refresh-discovery");
    ImGui::TextWrapped("%s", discovery_.status.empty()
        ? "Project target discovery has not run." : discovery_.status.c_str());
    const auto choose_name = [](const char *label, std::string &current,
                                const std::vector<std::string> &choices) {
        if (choices.empty())
            return;
        const char *preview = current.empty() ? "Select..." : current.c_str();
        if (!ImGui::BeginCombo(label, preview))
            return;
        for (const auto &choice : choices)
        {
            if (ImGui::Selectable(choice.c_str(), current == choice))
                current = choice;
            if (current == choice)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    };
    choose_name("Configure preset", preferences.configurePreset, discovery_.configurePresets);
    choose_name("Build preset", preferences.buildPreset, discovery_.buildPresets);
    std::vector<std::string> discoveredTargets;
    discoveredTargets.reserve(discovery_.executableTargets.size());
    for (const auto &target : discovery_.executableTargets)
        discoveredTargets.push_back(target.targetName);
    choose_name("Executable target", preferences.target, discoveredTargets);
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
    ImGui::Checkbox("Embed Game view in editor", &preferences.embeddedGameView);
    ImGui::SetItemTooltip("Use the opt-in editor-session protocol to embed the game viewport and runtime controls in the editor. Click the view to capture gameplay keyboard and mouse input (Esc to release).");
#else
    ImGui::BeginDisabled();
    bool embeddedGameView = false;
    ImGui::Checkbox("Embed Game view in editor", &embeddedGameView);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Embedded Game view is unavailable in this build.");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rebuild with PLATFORMATOR_ENABLE_EDITOR_SESSION=ON to enable it.");
#endif
    if (!discovery_.executableTargets.empty())
    {
        const auto selected = std::find_if(discovery_.executableTargets.begin(),
            discovery_.executableTargets.end(), [&](const ProjectExecutableTarget &target) {
                return target.targetName == preferences.target;
            });
        std::string executablePreview = selected == discovery_.executableTargets.end()
            ? "Select built executable..."
            : selected->targetName + " — " + path_to_utf8(selected->executablePath.filename());
        if (ImGui::BeginCombo("Built executable", executablePreview.c_str()))
        {
            for (const auto &target : discovery_.executableTargets)
            {
                const std::string executablePath = path_to_utf8(target.executablePath);
                const std::string label = target.targetName + " — " +
                                          path_to_utf8(target.executablePath.filename());
                if (ImGui::Selectable(label.c_str(), preferences.executable == executablePath))
                {
                    preferences.target = target.targetName;
                    preferences.executable = executablePath;
                }
                if (preferences.executable == executablePath)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::Button("Browse executable..."))
        action = RunPanelAction::BrowseExecutable;
    remember_focus("browse-executable");
    if (ImGui::CollapsingHeader("Advanced manual overrides"))
    {
        input_string("Configure preset override", preferences.configurePreset, 256, textDrafts_, committedText_);
        input_string("Build preset override", preferences.buildPreset, 256, textDrafts_, committedText_);
        input_string("Target override", preferences.target, 256, textDrafts_, committedText_);
        input_string("Executable path override", preferences.executable, 1024, textDrafts_, committedText_);
    }
    const char *protocols[] = {"unavailable", "common-runner-v1"};
    int protocol = preferences.catalogProtocol == "common-runner-v1" ? 1 : 0;
    if (ImGui::Combo("Catalog protocol", &protocol, protocols, 2))
        preferences.catalogProtocol = protocols[protocol];
    if (ImGui::Checkbox("Production profile", &preferences.production))
    {
        const auto preset = preferences.production ? "production" : "debug";
        if (preferences.configurePreset == "production" || preferences.configurePreset == "debug")
            preferences.configurePreset = preset;
        if (preferences.buildPreset == "production" || preferences.buildPreset == "debug")
            preferences.buildPreset = preset;
    }
    ImGui::InputInt("Window width", &preferences.width);
    ImGui::InputInt("Window height", &preferences.height);
    ImGui::Checkbox("Fullscreen", &preferences.fullscreen);
    ImGui::Checkbox("Maximized", &preferences.maximized);
    ImGui::Checkbox("Preserve aspect ratio", &preferences.preserveAspect);
    bool startPaused = preferences.production ? false : preferences.startPaused;
    ImGui::BeginDisabled(preferences.production);
    if (ImGui::Checkbox("Start paused", &startPaused))
        preferences.startPaused = startPaused;
    ImGui::EndDisabled();
    const auto is_enabled = [&](const char *name) {
        return !preferences.production &&
               (preferences.overlays.empty() ||
                std::find(preferences.overlays.begin(), preferences.overlays.end(), name) != preferences.overlays.end());
    };
    auto overlay = [&](const char *label, const char *name) {
        bool enabled = is_enabled(name);
        ImGui::PushID(name);
        ImGui::BeginDisabled(preferences.production);
        if (ImGui::Checkbox(label, &enabled))
        {
            if (preferences.overlays.empty())
                preferences.overlays = {"colliders", "collision-points", "collision-normals", "grid-cells"};
            preferences.overlays.erase(std::remove(preferences.overlays.begin(), preferences.overlays.end(), "none"),
                                       preferences.overlays.end());
            const auto found = std::find(preferences.overlays.begin(), preferences.overlays.end(), name);
            if (enabled && found == preferences.overlays.end())
                preferences.overlays.emplace_back(name);
            else if (!enabled && found != preferences.overlays.end())
                preferences.overlays.erase(found);
            if (preferences.overlays.empty())
                preferences.overlays = {"none"};
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    };
    overlay("Show colliders", "colliders");
    overlay("Show collision points", "collision-points");
    overlay("Show collision normals", "collision-normals");
    overlay("Show grid cells", "grid-cells");
    ImGui::EndDisabled();

    if (preferences.catalogProtocol == "unavailable")
        ImGui::TextWrapped("Automatic Run needs a common-runner-v1 target that exports its behavior catalog. Custom entries can still be built and launched with advanced overrides.");
    ImGui::Text("Status: %s", stage_name(status.stage));
    if (!status.message.empty())
        ImGui::TextWrapped("%s", status.message.c_str());
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Recheck Sources"))
        action = RunPanelAction::RefreshSources;
    remember_focus("refresh-sources");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || preferences.target.empty() || preferences.executable.empty());
    if (ImGui::Button("Build"))
        action = RunPanelAction::Build;
    remember_focus("build");
    ImGui::SameLine();
    ImGui::BeginDisabled(preferences.catalogProtocol != "common-runner-v1");
    if (ImGui::Button("Build and Run"))
        action = RunPanelAction::BuildAndRun;
    remember_focus("build-and-run");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("Stop"))
        action = RunPanelAction::Stop;
    remember_focus("stop");
    ImGui::EndDisabled();
    if (documentDirty)
        ImGui::TextDisabled("Build and Run saves the current scene first.");
    return action;
}
} // namespace platformator_editor
