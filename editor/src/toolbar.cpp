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
        result.height = ImGui::GetFrameHeight() + style.WindowBorderSize * 2.0f;

        const bool iconOnly = contentWidth < 420.0f * uiScale;
        const float itemSpacing = iconOnly ? (2.0f * uiScale) : (style.ItemSpacing.x * 0.85f);
        const float frameHeight = ImGui::GetFrameHeight();
        const float toolBtnWidth = iconOnly
            ? std::min(frameHeight, std::max(18.0f, (contentWidth - itemSpacing * 3.0f) / 4.0f))
            : frameHeight;
        const float maxBtnWidth = iconOnly ? toolBtnWidth : 0.0f;
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

        const auto sum_widths = [&](const auto &widths)
        {
            float total = 0.0f;
            for (const float w : widths)
                total += (total > 0.0f ? itemSpacing : 0.0f) + w;
            return total;
        };

        result.compact = iconOnly;

        const char *runLabel = iconOnly ? "##Run" : (result.compact ? "Run" : "Build and Run");
        const char *pauseResumeLabel = iconOnly
            ? (state.isPaused ? "##Resume" : "##Pause")
            : (state.isPaused ? "Resume" : "Pause");
        const char *stepLabel = iconOnly ? "##Step" : "Step";
        const char *stopLabel = iconOnly ? "##Stop" : "Stop";
        const EditorIcon pauseResumeIcon = state.isPaused ? EditorIcon::Run : EditorIcon::Pause;

        const std::array playbackWidths{iconButtonWidth(runLabel), iconButtonWidth(pauseResumeLabel),
                                        iconButtonWidth(stepLabel), iconButtonWidth(stopLabel)};

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
        push_editor_toolbar_style(false);
        ImGui::Begin("##EditorToolbar", nullptr, toolbarFlags);

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
        ImGui::SetCursorPosX(style.WindowPadding.x + std::max(0.0f, (contentWidth - playbackTotalWidth) * 0.5f));

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
