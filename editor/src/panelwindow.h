#pragma once

#include <imgui.h>

namespace platformator_editor
{
struct PanelWindowFrame
{
    bool submitted = false;
    bool contentsVisible = false;
};

inline PanelWindowFrame begin_panel_window(bool visible, const char *title, bool *open)
{
    if (!visible)
        return {};
    return {true, ImGui::Begin(title, open)};
}

inline void end_panel_window(PanelWindowFrame frame)
{
    if (frame.submitted)
        ImGui::End();
}

inline void submit_editor_dockspace(ImGuiID dockspaceId, float topReservation)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
        return;
    ImVec2 position = viewport->WorkPos;
    position.y += topReservation;
    ImVec2 size = viewport->WorkSize;
    size.y -= topReservation;
    if (size.y < 0.0f)
        size.y = 0.0f;
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##EditorDockHost", nullptr, hostFlags);
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f));
    ImGui::End();
    ImGui::PopStyleVar(3);
}
} // namespace platformator_editor
