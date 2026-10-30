#include "runsettings.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
void input_string(const char *label, std::string &value, std::size_t capacity)
{
    std::array<char, 1024> buffer{};
    const auto available = std::min(capacity, buffer.size());
    const auto size = std::min(value.size(), available - 1);
    std::memcpy(buffer.data(), value.data(), size);
    if (ImGui::InputText(label, buffer.data(), available))
        value.assign(buffer.data());
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

RunPanelAction RunSettingsPanel::draw(RunPreferences &preferences, const PipelineStatus &status,
                                      bool busy, bool documentDirty)
{
    RunPanelAction action = RunPanelAction::None;
    ImGui::BeginDisabled(busy);
    input_string("Configure preset", preferences.configurePreset, 256);
    input_string("Build preset", preferences.buildPreset, 256);
    input_string("Target", preferences.target, 256);
    input_string("Executable", preferences.executable, 1024);
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
    ImGui::Checkbox("Start paused", &preferences.startPaused);
    const auto is_enabled = [&](const char *name) {
        return preferences.overlays.empty() ||
               std::find(preferences.overlays.begin(), preferences.overlays.end(), name) != preferences.overlays.end();
    };
    auto overlay = [&](const char *label, const char *name) {
        bool enabled = is_enabled(name);
        ImGui::PushID(name);
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
        ImGui::PopID();
    };
    overlay("Show colliders", "colliders");
    overlay("Show collision points", "collision-points");
    overlay("Show collision normals", "collision-normals");
    overlay("Show grid cells", "grid-cells");
    ImGui::EndDisabled();

    if (preferences.catalogProtocol == "unavailable")
        ImGui::TextWrapped("Automatic Run is unavailable until this project declares common-runner-v1 and exports its linked behaviors through the shared runner catalog. Custom entries remain editable and buildable. Add compatible catalog export integration to the custom runner before declaring support.");
    ImGui::Text("Operation: %s", stage_name(status.stage));
    if (!status.message.empty())
        ImGui::TextWrapped("%s", status.message.c_str());
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Refresh sources"))
        action = RunPanelAction::RefreshSources;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || preferences.target.empty() || preferences.executable.empty());
    if (ImGui::Button("Build"))
        action = RunPanelAction::Build;
    ImGui::SameLine();
    ImGui::BeginDisabled(preferences.catalogProtocol != "common-runner-v1");
    if (ImGui::Button("Build & Run"))
        action = RunPanelAction::BuildAndRun;
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("Stop"))
        action = RunPanelAction::Stop;
    ImGui::EndDisabled();
    if (documentDirty)
        ImGui::TextDisabled("Build & Run will ask you to save the current scene first.");
    return action;
}
} // namespace platformator_editor
