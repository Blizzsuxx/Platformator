#include "console.h"

#include <string_view>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
const char *stage_name(PipelineStage stage)
{
    switch (stage)
    {
    case PipelineStage::Idle: return "Idle";
    case PipelineStage::Configuring: return "Configuring";
    case PipelineStage::Building: return "Building";
    case PipelineStage::QueryingCatalog: return "Catalog query";
    case PipelineStage::Launching: return "Launching";
    case PipelineStage::Running: return "Running";
    }
    return "Unknown";
}
} // namespace

void ConsolePanel::draw(const PipelineStatus &status, const BoundedConsole &console)
{
    ImGui::Text("Stage: %s", stage_name(status.stage));
    if (!status.command.empty())
        ImGui::TextWrapped("Command: %s", status.command.c_str());
    if (!status.message.empty())
        ImGui::TextWrapped("Result: %s", status.message.c_str());
    if (console.truncated())
        ImGui::TextDisabled("Older console output was removed after reaching the 1 MiB limit.");
    ImGui::BeginChild("Pipeline console output", ImVec2(0.0f, 190.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const auto &text = console.text();
    constexpr std::size_t maximumDisplayedBytes = 64U * 1024U;
    const auto offset = text.size() > maximumDisplayedBytes ? text.size() - maximumDisplayedBytes : 0;
    const auto visible = std::string_view(text).substr(offset);
    ImGui::TextUnformatted(visible.data(), visible.data() + visible.size());
    ImGui::EndChild();
}
} // namespace platformator_editor
