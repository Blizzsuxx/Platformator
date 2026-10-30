#include "style.h"

#include <algorithm>
#include <cmath>

namespace platformator_editor
{
namespace
{
ImGuiStyle make_base_style()
{
    ImGuiStyle style;
    style.WindowPadding = {10.0f, 9.0f};
    style.FramePadding = {7.0f, 4.0f};
    style.CellPadding = {6.0f, 4.0f};
    style.ItemSpacing = {7.0f, 6.0f};
    style.ItemInnerSpacing = {5.0f, 4.0f};
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 7.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.87f, 0.89f, 0.92f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.49f, 0.53f, 0.59f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.115f, 0.135f, 1.00f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.115f, 0.125f, 0.145f, 1.00f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.145f, 0.17f, 0.98f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.24f, 0.27f, 0.32f, 0.78f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.21f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.24f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.28f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.17f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.145f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.105f, 0.12f, 0.145f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.10f, 0.12f, 0.75f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.29f, 0.32f, 0.37f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.41f, 0.47f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.36f, 0.63f, 0.94f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.55f, 0.84f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.39f, 0.67f, 0.96f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.19f, 0.24f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.34f, 0.44f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.43f, 0.70f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.25f, 0.33f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.32f, 0.43f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.39f, 0.57f, 1.00f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.27f, 0.30f, 0.35f, 0.75f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.31f, 0.51f, 0.73f, 0.85f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.30f, 0.55f, 0.84f, 1.00f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.35f, 0.47f, 1.00f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.29f, 0.39f, 1.00f);
    style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.27f, 0.55f, 0.82f, 0.55f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.34f, 0.62f, 0.93f, 1.00f);
    return style;
}

void draw_icon(ImDrawList *draw, EditorIcon icon, ImVec2 origin, float size, ImU32 color)
{
    const float x = origin.x;
    const float y = origin.y;
    const float s = size;
    const float t = std::max(1.2f, s * 0.095f);
    switch (icon)
    {
    case EditorIcon::NewDocument:
        draw->AddRect({x + s * 0.22f, y + s * 0.16f}, {x + s * 0.78f, y + s * 0.84f}, color, 2.0f, 0, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.31f}, {x + s * 0.50f, y + s * 0.69f}, color, t);
        draw->AddLine({x + s * 0.31f, y + s * 0.50f}, {x + s * 0.69f, y + s * 0.50f}, color, t);
        break;
    case EditorIcon::Open:
        draw->AddLine({x + s * 0.16f, y + s * 0.35f}, {x + s * 0.43f, y + s * 0.35f}, color, t);
        draw->AddLine({x + s * 0.43f, y + s * 0.35f}, {x + s * 0.55f, y + s * 0.48f}, color, t);
        draw->AddRect({x + s * 0.16f, y + s * 0.35f}, {x + s * 0.84f, y + s * 0.80f}, color, 2.0f, 0, t);
        draw->AddLine({x + s * 0.22f, y + s * 0.68f}, {x + s * 0.78f, y + s * 0.68f}, color, t);
        break;
    case EditorIcon::Save:
        draw->AddRect({x + s * 0.18f, y + s * 0.15f}, {x + s * 0.82f, y + s * 0.85f}, color, 2.0f, 0, t);
        draw->AddRect({x + s * 0.30f, y + s * 0.18f}, {x + s * 0.68f, y + s * 0.40f}, color, 1.0f, 0, t);
        draw->AddRect({x + s * 0.31f, y + s * 0.57f}, {x + s * 0.69f, y + s * 0.82f}, color, 1.0f, 0, t);
        break;
    case EditorIcon::Undo:
    case EditorIcon::Redo:
    {
        const bool reverse = icon == EditorIcon::Redo;
        const float left = reverse ? x + s * 0.84f : x + s * 0.16f;
        const float right = reverse ? x + s * 0.16f : x + s * 0.84f;
        draw->PathLineTo({left, y + s * 0.35f});
        draw->PathLineTo({left + (reverse ? -1.0f : 1.0f) * s * 0.25f, y + s * 0.15f});
        draw->PathLineTo({left + (reverse ? -1.0f : 1.0f) * s * 0.25f, y + s * 0.55f});
        draw->PathStroke(color, 0, t);
        draw->PathClear();
        draw->PathArcTo({x + s * 0.49f, y + s * 0.55f}, s * 0.28f, reverse ? 0.0f : 3.14159f,
                        reverse ? 3.14159f : 6.28318f, 12);
        draw->PathLineTo({right, y + s * 0.78f});
        draw->PathStroke(color, 0, t);
        break;
    }
    case EditorIcon::Validate:
        draw->AddCircle({x + s * 0.48f, y + s * 0.49f}, s * 0.31f, color, 0, t);
        draw->AddLine({x + s * 0.31f, y + s * 0.49f}, {x + s * 0.44f, y + s * 0.62f}, color, t);
        draw->AddLine({x + s * 0.44f, y + s * 0.62f}, {x + s * 0.68f, y + s * 0.34f}, color, t);
        break;
    case EditorIcon::Build:
        draw->AddRect({x + s * 0.17f, y + s * 0.19f}, {x + s * 0.81f, y + s * 0.81f}, color, 2.0f, 0, t);
        draw->AddLine({x + s * 0.28f, y + s * 0.37f}, {x + s * 0.69f, y + s * 0.37f}, color, t);
        draw->AddLine({x + s * 0.28f, y + s * 0.53f}, {x + s * 0.69f, y + s * 0.53f}, color, t);
        draw->AddLine({x + s * 0.28f, y + s * 0.69f}, {x + s * 0.58f, y + s * 0.69f}, color, t);
        break;
    case EditorIcon::Run:
        draw->AddTriangleFilled({x + s * 0.28f, y + s * 0.17f}, {x + s * 0.78f, y + s * 0.50f},
                                {x + s * 0.28f, y + s * 0.83f}, color);
        break;
    case EditorIcon::Stop:
        draw->AddRectFilled({x + s * 0.23f, y + s * 0.23f}, {x + s * 0.77f, y + s * 0.77f}, color, 2.0f);
        break;
    case EditorIcon::GameObject:
        draw->AddRect({x + s * 0.18f, y + s * 0.25f}, {x + s * 0.82f, y + s * 0.78f}, color, 1.5f, 0, t);
        draw->AddLine({x + s * 0.18f, y + s * 0.25f}, {x + s * 0.50f, y + s * 0.10f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.10f}, {x + s * 0.82f, y + s * 0.25f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.10f}, {x + s * 0.50f, y + s * 0.62f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.62f}, {x + s * 0.18f, y + s * 0.78f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.62f}, {x + s * 0.82f, y + s * 0.78f}, color, t);
        break;
    case EditorIcon::Animator:
        draw->AddCircle({x + s * 0.50f, y + s * 0.53f}, s * 0.34f, color, 0, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.53f}, {x + s * 0.50f, y + s * 0.30f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.53f}, {x + s * 0.68f, y + s * 0.62f}, color, t);
        draw->AddLine({x + s * 0.36f, y + s * 0.08f}, {x + s * 0.64f, y + s * 0.08f}, color, t);
        break;
    case EditorIcon::Audio:
        draw->AddRectFilled({x + s * 0.16f, y + s * 0.39f}, {x + s * 0.36f, y + s * 0.62f}, color, 1.0f);
        draw->AddTriangleFilled({x + s * 0.34f, y + s * 0.39f}, {x + s * 0.62f, y + s * 0.18f},
                                {x + s * 0.62f, y + s * 0.82f}, color);
        draw->PathArcTo({x + s * 0.57f, y + s * 0.50f}, s * 0.26f, -0.85f, 0.85f, 12);
        draw->PathStroke(color, 0, t);
        draw->PathClear();
        break;
    case EditorIcon::Camera:
        draw->AddRect({x + s * 0.12f, y + s * 0.28f}, {x + s * 0.88f, y + s * 0.78f}, color, 2.0f, 0, t);
        draw->AddRect({x + s * 0.25f, y + s * 0.17f}, {x + s * 0.48f, y + s * 0.29f}, color, 1.0f, 0, t);
        draw->AddCircle({x + s * 0.60f, y + s * 0.53f}, s * 0.17f, color, 0, t);
        break;
    case EditorIcon::Collider:
        draw->AddRect({x + s * 0.19f, y + s * 0.19f}, {x + s * 0.81f, y + s * 0.81f}, color, 0.0f, 0, t);
        draw->AddLine({x + s * 0.19f, y + s * 0.50f}, {x + s * 0.81f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.19f}, {x + s * 0.50f, y + s * 0.81f}, color, t);
        break;
    case EditorIcon::Rigidbody:
        draw->AddCircleFilled({x + s * 0.50f, y + s * 0.51f}, s * 0.21f, color, 12);
        draw->AddLine({x + s * 0.50f, y + s * 0.12f}, {x + s * 0.50f, y + s * 0.28f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.74f}, {x + s * 0.50f, y + s * 0.90f}, color, t);
        draw->AddLine({x + s * 0.11f, y + s * 0.51f}, {x + s * 0.28f, y + s * 0.51f}, color, t);
        draw->AddLine({x + s * 0.72f, y + s * 0.51f}, {x + s * 0.89f, y + s * 0.51f}, color, t);
        break;
    case EditorIcon::Script:
        draw->AddRect({x + s * 0.20f, y + s * 0.12f}, {x + s * 0.80f, y + s * 0.88f}, color, 1.5f, 0, t);
        draw->AddLine({x + s * 0.35f, y + s * 0.39f}, {x + s * 0.27f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.27f, y + s * 0.50f}, {x + s * 0.35f, y + s * 0.61f}, color, t);
        draw->AddLine({x + s * 0.65f, y + s * 0.39f}, {x + s * 0.73f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.73f, y + s * 0.50f}, {x + s * 0.65f, y + s * 0.61f}, color, t);
        draw->AddLine({x + s * 0.56f, y + s * 0.35f}, {x + s * 0.44f, y + s * 0.65f}, color, t);
        break;
    case EditorIcon::Sprite:
        draw->AddRect({x + s * 0.12f, y + s * 0.14f}, {x + s * 0.88f, y + s * 0.86f}, color, 1.5f, 0, t);
        draw->AddCircleFilled({x + s * 0.68f, y + s * 0.34f}, s * 0.07f, color, 8);
        draw->PathLineTo({x + s * 0.19f, y + s * 0.74f});
        draw->PathLineTo({x + s * 0.43f, y + s * 0.47f});
        draw->PathLineTo({x + s * 0.56f, y + s * 0.61f});
        draw->PathLineTo({x + s * 0.67f, y + s * 0.51f});
        draw->PathLineTo({x + s * 0.81f, y + s * 0.73f});
        draw->PathStroke(color, 0, t);
        draw->PathClear();
        break;
    case EditorIcon::UnknownComponent:
        draw->AddRect({x + s * 0.16f, y + s * 0.16f}, {x + s * 0.84f, y + s * 0.84f}, color, 2.0f, 0, t);
        draw->AddText({x + s * 0.34f, y + s * 0.18f}, color, "?");
        break;
    case EditorIcon::Folder:
        draw->AddRectFilled({x + s * 0.10f, y + s * 0.25f}, {x + s * 0.90f, y + s * 0.82f}, color, 2.0f);
        draw->AddRectFilled({x + s * 0.16f, y + s * 0.12f}, {x + s * 0.52f, y + s * 0.32f}, color, 2.0f);
        break;
    }
}
} // namespace

void apply_editor_style(float scale)
{
    if (!std::isfinite(scale) || scale <= 0.0f)
        scale = 1.0f;
    ImGui::GetStyle() = make_base_style();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
}

void push_editor_toolbar_style()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.145f, 0.17f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(style.WindowPadding.x, style.WindowPadding.y * 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(style.ItemSpacing.x * 0.85f, style.ItemSpacing.y * 0.7f));
}

void pop_editor_toolbar_style()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

bool icon_label_button(EditorIcon icon, const char *label, const char *tooltip)
{
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float height = ImGui::GetFrameHeight();
    const ImVec2 size(textSize.x + height + ImGui::GetStyle().FramePadding.x * 2.0f, height);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, size);
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive
                                         : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y}, background,
                        ImGui::GetStyle().FrameRounding);
    const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
    const float iconSize = height * 0.70f;
    draw_icon(draw, icon, {position.x + ImGui::GetStyle().FramePadding.x,
                           position.y + (height - iconSize) * 0.5f}, iconSize, iconColor);
    draw->AddText({position.x + height, position.y + ImGui::GetStyle().FramePadding.y},
                  ImGui::GetColorU32(ImGuiCol_Text), label);
    if (hovered && tooltip != nullptr && *tooltip != '\0')
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
    (void)id;
    return pressed;
}

void draw_editor_icon(EditorIcon icon, ImDrawList *draw, ImVec2 origin, float size, ImU32 color)
{
    if (draw != nullptr && size > 0.0f)
        draw_icon(draw, icon, origin, size, color);
}
} // namespace platformator_editor
