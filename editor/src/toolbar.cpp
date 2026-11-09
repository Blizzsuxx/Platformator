#include "toolbar.h"

#include "style.h"

#include <algorithm>
#include <array>
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
    const float itemSpacing = style.ItemSpacing.x * 0.85f;
    const float frameHeight = ImGui::GetFrameHeight();
    const auto textWidth = [](const char *label) {
        return ImGui::CalcTextSize(label).x;
    };
    const auto iconButtonWidth = [&](const char *label) {
        return textWidth(label) + frameHeight + style.FramePadding.x * 2.0f;
    };
    const auto selectableWidth = [&](const char *label) {
        return textWidth(label) + style.FramePadding.x * 2.0f;
    };
    const std::string identity =
        (state.projectLabel.empty() ? std::string("No Project") : state.projectLabel) +
        " / " + (state.sceneLabel.empty() ? std::string("Untitled Scene") : state.sceneLabel);
    const float identityWidth = state.projectLabel.empty() && state.sceneLabel.empty() ? 0.0f
        : std::min(textWidth(identity.c_str()), contentWidth * 0.48f);
    const std::array fullRowWidths{
        iconButtonWidth("Open Scene"), iconButtonWidth("Save"),
        selectableWidth("Move (W)"), selectableWidth("Rotate (E)"),
        selectableWidth("Scale (R)"), iconButtonWidth("Build"),
        iconButtonWidth("Build and Run"), iconButtonWidth("Stop")};
    float fullRowWidth = 0.0f;
    for (const float width : fullRowWidths)
        fullRowWidth += width;
    fullRowWidth += itemSpacing * static_cast<float>(fullRowWidths.size() - 1);
    const float statusWidth = state.status.empty() ? 0.0f
        : std::min(textWidth(state.status.c_str()), contentWidth * 0.48f);
    result.compact = fullRowWidth > contentWidth;

    const char *openLabel = result.compact ? "Open" : "Open Scene";
    const char *moveLabel = result.compact ? "Move" : "Move (W)";
    const char *rotateLabel = result.compact ? "Rotate" : "Rotate (E)";
    const char *scaleLabel = result.compact ? "Scale" : "Scale (R)";
    const char *runLabel = result.compact ? "Run" : "Build and Run";
    const std::array fileWidths{iconButtonWidth(openLabel), iconButtonWidth("Save")};
    const std::array toolWidths{selectableWidth(moveLabel), selectableWidth(rotateLabel),
                                selectableWidth(scaleLabel)};
    const std::array operationWidths{iconButtonWidth("Build"), iconButtonWidth(runLabel),
                                     iconButtonWidth("Stop")};
    const std::array infoWidths{identityWidth, statusWidth};
    const auto groupRows = [&](const auto &widths) {
        std::size_t rows = 1;
        float rowWidth = 0.0f;
        for (const float width : widths)
        {
            if (rowWidth > 0.0f && rowWidth + itemSpacing + width > contentWidth)
            {
                ++rows;
                rowWidth = width;
            }
            else
                rowWidth = rowWidth > 0.0f ? rowWidth + itemSpacing + width : width;
        }
        return rows;
    };
    const std::size_t infoRowCount = identityWidth > 0.0f || statusWidth > 0.0f
        ? groupRows(infoWidths) : 0U;
    const std::size_t rowCount = result.compact
        ? groupRows(fileWidths) + groupRows(toolWidths) + groupRows(operationWidths) + infoRowCount
        : 1U + infoRowCount;
    const float rowHeight = ImGui::GetFrameHeight();
    const float rowCountFloat = static_cast<float>(rowCount);
    result.height = rowCountFloat * rowHeight + (rowCountFloat - 1.0f) * style.ItemSpacing.y * 0.7f +
                    style.WindowPadding.y * 1.1f;

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
    const float firstRowX = ImGui::GetCursorPosX();
    float currentRowY = ImGui::GetCursorPosY();
    float usedWidth = 0.0f;
    const float compactRowAdvance = rowHeight + style.ItemSpacing.y * 0.7f;
    const auto next_row = [&] {
        currentRowY += compactRowAdvance;
        ImGui::SetCursorPos(ImVec2(firstRowX, currentRowY));
        usedWidth = 0.0f;
    };

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
    const auto place = [&](float width) {
        if (!result.compact)
        {
            if (usedWidth > 0.0f)
                ImGui::SameLine();
            usedWidth += (usedWidth > 0.0f ? itemSpacing : 0.0f) + width;
            return;
        }
        if (usedWidth > 0.0f && usedWidth + itemSpacing + width <= contentWidth)
        {
            ImGui::SameLine();
            usedWidth += itemSpacing + width;
        }
        else
        {
            if (usedWidth > 0.0f)
                next_row();
            usedWidth = width;
        }
    };
    const auto next_group = [&] {
        if (result.compact)
            next_row();
    };

    ImGui::BeginDisabled(state.projectLocked);
    place(fileWidths[0]);
    if (icon_label_button(EditorIcon::Open, openLabel, "Open a scene (Ctrl+O)"))
        invoke(actions.openScene);
    remember("Open Scene");
    place(fileWidths[1]);
    if (icon_label_button(EditorIcon::Save, "Save", "Save the current scene (Ctrl+S)"))
        invoke(actions.save);
    remember("Save");
    ImGui::EndDisabled();

    next_group();
    ImGui::BeginDisabled(!state.sceneAvailable || state.projectLocked);
    place(toolWidths[0]);
    if (ImGui::Selectable(moveLabel, state.activeSceneTool == SceneTool::Move,
                          0, ImVec2(toolWidths[0], 0.0f)))
        invoke(actions.moveTool);
    remember("Move Tool");
    place(toolWidths[1]);
    if (ImGui::Selectable(rotateLabel, state.activeSceneTool == SceneTool::Rotate,
                          0, ImVec2(toolWidths[1], 0.0f)))
        invoke(actions.rotateTool);
    remember("Rotate Tool");
    place(toolWidths[2]);
    if (ImGui::Selectable(scaleLabel, state.activeSceneTool == SceneTool::Scale,
                          0, ImVec2(toolWidths[2], 0.0f)))
        invoke(actions.scaleTool);
    remember("Scale Tool");
    ImGui::EndDisabled();

    next_group();

    ImGui::BeginDisabled(state.projectLocked);
    place(operationWidths[0]);
    if (icon_label_button(EditorIcon::Build, "Build", "Build the configured target (Ctrl+B)"))
        invoke(actions.build);
    remember("Build");
    place(operationWidths[1]);
    if (icon_label_button(EditorIcon::Run, runLabel, "Build, verify and launch (F5)"))
        invoke(actions.buildAndRun);
    remember("Build and Run");
    ImGui::EndDisabled();
    place(operationWidths[2]);
    ImGui::BeginDisabled(!state.operationActive);
    if (icon_label_button(EditorIcon::Stop, "Stop", "Stop the owned build, query or game (Shift+F5)"))
        invoke(actions.stop);
    remember("Stop");
    ImGui::EndDisabled();
    if (identityWidth > 0.0f || statusWidth > 0.0f)
    {
        if (result.compact)
            next_group();
        else
            next_row();
    }
    if (identityWidth > 0.0f)
    {
        place(identityWidth);
        ImGui::Dummy(ImVec2(identityWidth, frameHeight));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(minimum, maximum, true);
        const float textY = minimum.y + std::max(0.0f, (frameHeight - ImGui::GetTextLineHeight()) * 0.5f);
        draw->AddText(ImVec2(minimum.x, textY), ImGui::GetColorU32(ImGuiCol_Text), identity.c_str());
        draw->PopClipRect();
        if (ImGui::IsItemHovered())
        {
            if (!state.projectPath.empty() || !state.scenePath.empty())
                ImGui::SetTooltip("Project: %s\nScene: %s",
                    state.projectPath.empty() ? state.projectLabel.c_str() : state.projectPath.c_str(),
                    state.scenePath.empty() ? state.sceneLabel.c_str() : state.scenePath.c_str());
            else if (textWidth(identity.c_str()) > identityWidth)
                ImGui::SetTooltip("%s", identity.c_str());
        }
        remember("Project and Scene");
    }
    if (statusWidth > 0.0f)
    {
        place(statusWidth);
        ImGui::Dummy(ImVec2(statusWidth, frameHeight));
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(minimum, maximum, true);
        const float textY = minimum.y + std::max(0.0f, (frameHeight - ImGui::GetTextLineHeight()) * 0.5f);
        draw->AddText(ImVec2(minimum.x, textY), ImGui::GetColorU32(ImGuiCol_Text), state.status.c_str());
        draw->PopClipRect();
        if (ImGui::IsItemHovered() && textWidth(state.status.c_str()) > statusWidth)
            ImGui::SetTooltip("%s", state.status.c_str());
        remember("Document Status");
    }
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

    const auto bounds_for = [&](std::string_view name) -> std::optional<SemanticBounds> {
        const auto found = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto &item) {
            return item.name == name;
        });
        if (found == frame.items.end())
            return std::nullopt;
        return SemanticBounds{found->x, found->y, found->width, found->height};
    };
    const auto bounded_text = [](std::string text) {
        constexpr std::size_t maximum = 512;
        if (text.size() > maximum)
        {
            text.resize(maximum - 3);
            text += "...";
        }
        return text;
    };

    if (const auto bounds = bounds_for("Project and Scene"))
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
        identity.bounds = bounds;
        identity.visible = true;
        nodes.push_back(std::move(identity));
    }
    if (const auto bounds = bounds_for("Document Status"))
    {
        SemanticNode status;
        status.id = "main/toolbar/status";
        status.parentId = "main/toolbar";
        status.role = SemanticRole::Text;
        status.name = "Document and operation status";
        status.value = bounded_text(state.status);
        status.error = state.status.find("Failed") != std::string::npos;
        status.bounds = bounds;
        status.visible = true;
        nodes.push_back(std::move(status));
    }
    return nodes;
}
} // namespace platformator_editor
