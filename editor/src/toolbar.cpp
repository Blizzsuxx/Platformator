#include "toolbar.h"

#include "style.h"

#include <imgui.h>

namespace platformator_editor
{
EditorToolbarFrame draw_editor_toolbar(const EditorToolbarState &state,
                                       const EditorToolbarActions &actions)
{
    EditorToolbarFrame result;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float availableWidth = viewport == nullptr ? 1280.0f : viewport->WorkSize.x;
    result.compact = availableWidth < 900.0f;
    const float rowHeight = ImGui::GetFrameHeight();
    const float rowCount = result.compact ? 3.0f : 1.0f;
    result.height = rowCount * rowHeight + (rowCount - 1.0f) * ImGui::GetStyle().ItemSpacing.y +
                    ImGui::GetStyle().WindowPadding.y * 2.0f;

    if (viewport != nullptr)
    {
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, result.height), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
    }
    constexpr ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus;
    push_editor_toolbar_style();
    ImGui::Begin("##EditorToolbar", nullptr, toolbarFlags);

    const auto remember = [&](const char *name) {
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        result.items.push_back({name, minimum.x, minimum.y, maximum.x - minimum.x,
                                maximum.y - minimum.y});
    };
    const auto invoke = [](const std::function<void()> &callback) {
        if (callback)
            callback();
    };

    if (!result.compact)
    {
        ImGui::BeginDisabled(state.projectLocked);
        if (icon_label_button(EditorIcon::NewDocument, "New", "Create a scene (Ctrl+N)"))
            invoke(actions.newScene);
        remember("New");
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(state.projectLocked);
    if (icon_label_button(EditorIcon::Open, "Open Scene", "Open a scene (Ctrl+O)"))
        invoke(actions.openScene);
    remember("Open Scene");
    ImGui::SameLine();
    if (icon_label_button(EditorIcon::Save, "Save", "Save the current scene (Ctrl+S)"))
        invoke(actions.save);
    remember("Save");
    ImGui::EndDisabled();

    if (result.compact)
        ImGui::NewLine();
    else
        ImGui::SameLine();

    ImGui::BeginDisabled(state.projectLocked);
    if (icon_label_button(EditorIcon::Build, "Build", "Build the configured target (Ctrl+B)"))
        invoke(actions.build);
    remember("Build");
    ImGui::SameLine();
    if (icon_label_button(EditorIcon::Run, "Build and Run", "Build, verify and launch (F5)"))
        invoke(actions.buildAndRun);
    remember("Build and Run");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.operationActive);
    if (icon_label_button(EditorIcon::Stop, "Stop", "Stop the owned build, query or game (Shift+F5)"))
        invoke(actions.stop);
    remember("Stop");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool moreOpen = ImGui::BeginMenu("More");
    remember("More");
    if (moreOpen)
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !state.projectLocked))
            invoke(actions.newScene);
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, state.canUndo && !state.projectLocked))
            invoke(actions.undo);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, state.canRedo && !state.projectLocked))
            invoke(actions.redo);
        if (ImGui::MenuItem("Validate", "F6", false, !state.projectLocked))
            invoke(actions.validate);
        if (ImGui::MenuItem("Open Project Folder...", nullptr, false, !state.projectLocked))
            invoke(actions.openProject);
        ImGui::EndMenu();
    }

    if (result.compact)
        ImGui::NewLine();
    else
        ImGui::SameLine();
    const std::string sceneLabel = state.openScenePending ? "Opening scene..." :
        (state.sceneLabel.empty() ? "No authored scene" : state.sceneLabel);
    ImGui::TextDisabled("%s", sceneLabel.c_str());
    remember("Scene");
    if (!state.scenePath.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", state.scenePath.c_str());
    ImGui::SameLine();
    const float statusWidth = ImGui::GetContentRegionAvail().x;
    const float statusY = ImGui::GetCursorScreenPos().y;
    ImGui::BeginChild("##toolbar-status", ImVec2(statusWidth, rowHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const char *statusText = state.status.empty() ? "Ready" : state.status.c_str();
    ImGui::TextDisabled("%s", statusText);
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(statusText);
        ImGui::EndTooltip();
    }
    const ImVec2 statusMinimum = ImGui::GetWindowPos();
    const ImVec2 statusMaximum{statusMinimum.x + ImGui::GetWindowSize().x,
                               statusMinimum.y + ImGui::GetWindowSize().y};
    result.items.push_back({"Status", statusMinimum.x, statusY,
                            statusMaximum.x - statusMinimum.x, statusMaximum.y - statusY});
    ImGui::EndChild();
    ImGui::End();
    pop_editor_toolbar_style();
    return result;
}
} // namespace platformator_editor
