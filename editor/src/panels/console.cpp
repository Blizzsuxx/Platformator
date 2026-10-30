#include "console.h"

#include <string_view>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
} // namespace

void ConsolePanel::draw(const PipelineStatus &status, const BoundedConsole &console)
{
    if (!status.command.empty())
        ImGui::TextWrapped("Command: %s", status.command.c_str());
    if (!status.message.empty())
        ImGui::TextWrapped("Result: %s", status.message.c_str());
    if (console.truncated())
        ImGui::TextDisabled("Older console output was removed after reaching the 1 MiB limit.");
    const auto &text = console.text();
    constexpr std::size_t maximumDisplayedBytes = 64U * 1024U;
    const auto offset = text.size() > maximumDisplayedBytes ? text.size() - maximumDisplayedBytes : 0;
    const auto visible = std::string_view(text).substr(offset);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(visible.data(), visible.data() + visible.size());
    ImGui::PopTextWrapPos();
}
} // namespace platformator_editor
