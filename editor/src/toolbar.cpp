#include "toolbar.h"

#include "style.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <imgui.h>

namespace platformator_editor
{
    EditorToolbarFrame draw_editor_toolbar(const EditorToolbarState &state,
                                           const EditorToolbarActions &actions)
    {
        EditorToolbarFrame result;
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        const float availableWidth = viewport == nullptr ? 1280.0f : viewport->WorkSize.x;
        const ImGuiStyle &style = ImGui::GetStyle();
        const float contentWidth = std::max(1.0f, availableWidth - style.WindowPadding.x * 2.0f);
        const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
        result.height = 40.0f * uiScale;

        const bool iconOnly = contentWidth < 680.0f * uiScale;
        const float itemSpacing = iconOnly ? (2.0f * uiScale) : (style.ItemSpacing.x * 0.85f);
        const float frameHeight = ImGui::GetFrameHeight();
        const float toolBtnWidth = iconOnly
            ? std::min(frameHeight, std::max(18.0f, (contentWidth - itemSpacing * 11.0f) / 12.0f))
            : frameHeight;
        const float maxBtnWidth = iconOnly ? toolBtnWidth : 0.0f;
        const float groupGap = iconOnly ? itemSpacing : std::max(itemSpacing, 8.0f * uiScale);
        const auto textWidth = [](const char *label)
        {
            const char *textEnd = (label != nullptr && !(label[0] == '#' && label[1] == '#'))
                ? (std::strstr(label, "##") ? std::strstr(label, "##") : label + std::strlen(label))
                : label;
            return ImGui::CalcTextSize(label, textEnd).x;
        };
        const auto iconButtonWidth = [&](const char *label)
        {
            if (iconOnly)
                return maxBtnWidth;
            const float w = textWidth(label);
            const float iconSize = frameHeight * 0.70f;
            const float iconPadding = style.FramePadding.x;
            const float spacing = style.ItemInnerSpacing.x;
            const float textOffset = iconPadding + iconSize + spacing;
            return w > 0.0f ? (textOffset + w + iconPadding) : frameHeight;
        };

        const float toolClusterWidth = 5.0f * toolBtnWidth + 4.0f * itemSpacing;
        const auto sum_widths = [&](const auto &widths)
        {
            float total = 0.0f;
            for (const float w : widths)
                total += (total > 0.0f ? itemSpacing : 0.0f) + w;
            return total;
        };

        if (!iconOnly)
        {
            const std::array fullLeft{iconButtonWidth("Open Scene"), iconButtonWidth("Save"), iconButtonWidth("Build")};
            const std::array fullPlayback{iconButtonWidth("Build and Run"),
                                          iconButtonWidth(state.isPaused ? "Resume" : "Pause"),
                                          iconButtonWidth("Step"), iconButtonWidth("Stop")};
            const float fullTotal = toolClusterWidth + groupGap + sum_widths(fullLeft) + groupGap + sum_widths(fullPlayback);
            result.compact = fullTotal > contentWidth;
        }
        else
        {
            result.compact = true;
        }

        const char *openLabel = iconOnly ? "##Open" : (result.compact ? "Open" : "Open Scene");
        const char *saveLabel = iconOnly ? "##Save" : "Save";
        const char *buildLabel = iconOnly ? "##Build" : "Build";
        const char *runLabel = iconOnly ? "##Run" : (result.compact ? "Run" : "Build and Run");
        const char *pauseResumeLabel = iconOnly
            ? (state.isPaused ? "##Resume" : "##Pause")
            : (state.isPaused ? "Resume" : "Pause");
        const char *stepLabel = iconOnly ? "##Step" : "Step";
        const char *stopLabel = iconOnly ? "##Stop" : "Stop";
        const EditorIcon pauseResumeIcon = state.isPaused ? EditorIcon::Run : EditorIcon::Pause;

        const std::array leftWidths{iconButtonWidth(openLabel), iconButtonWidth(saveLabel), iconButtonWidth(buildLabel)};
        const std::array playbackWidths{iconButtonWidth(runLabel), iconButtonWidth(pauseResumeLabel),
                                        iconButtonWidth(stepLabel), iconButtonWidth(stopLabel)};

        const float leftTotalWidth = toolClusterWidth + groupGap + sum_widths(leftWidths);
        const float playbackTotalWidth = sum_widths(playbackWidths);

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
        push_editor_toolbar_style(state.isRunning || state.isPaused);
        ImGui::Begin("##EditorToolbar", nullptr, toolbarFlags);
        float usedWidth = 0.0f;

        const auto remember = [&](const char *name)
        {
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            result.items.push_back({name, minimum.x, minimum.y, maximum.x - minimum.x,
                                    maximum.y - minimum.y});
        };
        const auto invoke = [](const std::function<void()> &callback)
        {
            if (callback)
                callback();
        };
        const std::array<std::tuple<SceneTool, EditorIcon, const char *, const char *, const char *>, 5> toolButtons{{
            {SceneTool::Hand, EditorIcon::HandTool, "##ToolHand", "Hand Tool (Q)", "Hand Tool"},
            {SceneTool::Move, EditorIcon::MoveTool, "##ToolMove", "Move Tool (W)", "Move Tool"},
            {SceneTool::Rotate, EditorIcon::RotateTool, "##ToolRotate", "Rotate Tool (E)", "Rotate Tool"},
            {SceneTool::Scale, EditorIcon::ScaleTool, "##ToolScale", "Scale Tool (R)", "Scale Tool"},
            {SceneTool::Rectangle, EditorIcon::RectTool, "##ToolRect", "Rect Tool (T)", "Rect Tool"},
        }};
        for (std::size_t i = 0; i < toolButtons.size(); ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.0f, itemSpacing);
            const auto &[tool, icon, strId, tooltip, itemName] = toolButtons[i];
            if (icon_button(icon, strId, state.activeTool == tool, tooltip, ImVec2(toolBtnWidth, frameHeight)))
            {
                if (actions.selectTool)
                    actions.selectTool(tool);
            }
            remember(itemName);
        }

        ImGui::SameLine(0.0f, groupGap);
        ImGui::BeginDisabled(state.projectLocked);
        if (icon_label_button(EditorIcon::Open, openLabel, "Open a scene (Ctrl+O)", maxBtnWidth))
            invoke(actions.openScene);
        remember("Open Scene");
        ImGui::EndDisabled();

        ImGui::BeginDisabled(state.projectLocked || !state.saveEnabled);
        ImGui::SameLine(0.0f, itemSpacing);
        if (icon_label_button(EditorIcon::Save, saveLabel, "Save the current scene (Ctrl+S)", maxBtnWidth))
            invoke(actions.save);
        remember("Save");
        ImGui::EndDisabled();

        ImGui::BeginDisabled(state.projectLocked);
        ImGui::SameLine(0.0f, itemSpacing);
        if (icon_label_button(EditorIcon::Build, buildLabel, "Build the configured target (Ctrl+B)", maxBtnWidth))
            invoke(actions.build);
        remember("Build");
        ImGui::EndDisabled();

        const float targetCenterX = style.WindowPadding.x +
                                    std::max(leftTotalWidth + groupGap, (contentWidth - playbackTotalWidth) * 0.5f);
        if (leftTotalWidth + groupGap + playbackTotalWidth <= contentWidth)
            ImGui::SameLine(targetCenterX);
        else
            ImGui::SameLine(0.0f, itemSpacing);

        ImGui::BeginDisabled(state.projectLocked || state.isRunning);
        if (icon_label_button(EditorIcon::Run, runLabel, "Build, verify and launch (F5)", maxBtnWidth))
            invoke(actions.buildAndRun);
        remember("Build and Run");
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!state.pauseResumeEnabled);
        ImGui::SameLine(0.0f, itemSpacing);
        if (icon_label_button(pauseResumeIcon, pauseResumeLabel, state.isPaused ? "Resume playback" : "Pause playback", maxBtnWidth))
            invoke(state.isPaused ? actions.resume : actions.pause);
        remember(state.isPaused ? "Resume" : "Pause");
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!state.stepEnabled);
        ImGui::SameLine(0.0f, itemSpacing);
        if (icon_label_button(EditorIcon::StepForward, stepLabel, "Advance one frame", maxBtnWidth))
            invoke(actions.step);
        remember("Step");
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!state.operationActive);
        ImGui::SameLine(0.0f, itemSpacing);
        if (icon_label_button(EditorIcon::Stop, stopLabel, "Stop the owned build, query or game (Shift+F5)", maxBtnWidth))
            invoke(actions.stop);
        remember("Stop");
        ImGui::EndDisabled();

        ImGui::End();
        pop_editor_toolbar_style();
        return result;
    }

    std::vector<SemanticNode> editor_toolbar_semantic_nodes(const EditorToolbarState &state,
                                                            const EditorToolbarFrame &frame)
    {
        std::vector<SemanticNode> nodes;
        SemanticNode root;
        root.id = "main/toolbar";
        root.role = SemanticRole::Group;
        root.name = "Editor toolbar";
        root.visible = true;
        nodes.push_back(std::move(root));

        const auto bounds_for = [&](std::string_view name) -> std::optional<SemanticBounds>
        {
            const auto found = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto &item)
                                            { return item.name == name; });
            if (found == frame.items.end())
                return std::nullopt;
            return SemanticBounds{found->x, found->y, found->width, found->height};
        };
        const auto bounded_text = [](std::string text)
        {
            constexpr std::size_t maximum = 512;
            if (text.size() > maximum)
            {
                text.resize(maximum - 3);
                text += "...";
            }
            return text;
        };

        {
            SemanticNode identity;
            identity.id = "main/toolbar/identity";
            identity.parentId = "main/toolbar";
            identity.role = SemanticRole::Text;
            identity.name = "Current project and scene";
            identity.value = bounded_text(
                (state.projectLabel.empty() ? std::string("No Project") : state.projectLabel) +
                " / " + (state.sceneLabel.empty() ? std::string("Untitled Scene") : state.sceneLabel));
            identity.help = bounded_text("Project path: " + state.projectPath +
                                         "\nScene path: " + state.scenePath);
            const auto bounds = bounds_for("Project and Scene");
            identity.bounds = bounds ? bounds : SemanticBounds{0.0f, 0.0f, 0.0f, frame.height};
            identity.visible = true;
            nodes.push_back(std::move(identity));
        }
        {
            SemanticNode status;
            status.id = "main/toolbar/status";
            status.parentId = "main/toolbar";
            status.role = SemanticRole::Text;
            status.name = "Document and operation status";
            status.value = bounded_text(state.status);
            status.error = state.status.find("Failed") != std::string::npos;
            const auto bounds = bounds_for("Document Status");
            status.bounds = bounds ? bounds : SemanticBounds{0.0f, 0.0f, 0.0f, frame.height};
            status.visible = true;
            nodes.push_back(std::move(status));
        }
        return nodes;
    }
} // namespace platformator_editor
