#include "style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

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
    style.FrameBorderSize = 1.0f;
    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    // Neutral dark charcoal palette
    style.Colors[ImGuiCol_Text] = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.157f, 0.157f, 0.157f, 1.00f);       // #282828
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.133f, 0.133f, 0.133f, 1.00f);        // #222222
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.145f, 0.145f, 0.145f, 0.98f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.20f, 0.90f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);        // #1A1A1A
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);        // #1E1E1E
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.145f, 0.145f, 0.145f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.118f, 0.118f, 0.118f, 0.75f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.125f, 0.125f, 0.125f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.75f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.60f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.55f, 0.85f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.68f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.220f, 0.220f, 0.220f, 1.00f);        // #383838
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.34f, 0.34f, 1.00f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.20f, 0.90f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.50f, 0.75f, 0.85f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.35f, 0.60f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.70f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.45f, 0.45f, 0.45f, 0.90f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.145f, 0.145f, 0.145f, 1.00f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.55f, 0.85f, 0.55f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.35f, 0.60f, 0.90f, 1.00f);
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
    case EditorIcon::MoreVertical:
        draw->AddCircleFilled({x + s * 0.50f, y + s * 0.22f}, s * 0.08f, color, 8);
        draw->AddCircleFilled({x + s * 0.50f, y + s * 0.50f}, s * 0.08f, color, 8);
        draw->AddCircleFilled({x + s * 0.50f, y + s * 0.78f}, s * 0.08f, color, 8);
        break;
    case EditorIcon::EyeOpen:
        draw->AddBezierQuadratic({x + s * 0.15f, y + s * 0.50f}, {x + s * 0.50f, y + s * 0.18f}, {x + s * 0.85f, y + s * 0.50f}, color, t);
        draw->AddBezierQuadratic({x + s * 0.15f, y + s * 0.50f}, {x + s * 0.50f, y + s * 0.82f}, {x + s * 0.85f, y + s * 0.50f}, color, t);
        draw->AddCircleFilled({x + s * 0.50f, y + s * 0.50f}, s * 0.13f, color, 8);
        break;
    case EditorIcon::EyeClosed:
        draw->AddBezierQuadratic({x + s * 0.15f, y + s * 0.50f}, {x + s * 0.50f, y + s * 0.22f}, {x + s * 0.85f, y + s * 0.50f}, color, t);
        draw->AddBezierQuadratic({x + s * 0.15f, y + s * 0.50f}, {x + s * 0.50f, y + s * 0.78f}, {x + s * 0.85f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.22f, y + s * 0.78f}, {x + s * 0.78f, y + s * 0.22f}, color, t);
        break;
    case EditorIcon::LockClosed:
        draw->AddRectFilled({x + s * 0.25f, y + s * 0.44f}, {x + s * 0.75f, y + s * 0.85f}, color, 1.5f);
        draw->PathArcTo({x + s * 0.50f, y + s * 0.38f}, s * 0.18f, 3.14159f, 6.28318f, 10);
        draw->PathLineTo({x + s * 0.68f, y + s * 0.44f});
        draw->PathStroke(color, 0, t);
        draw->PathClear();
        draw->AddLine({x + s * 0.32f, y + s * 0.38f}, {x + s * 0.32f, y + s * 0.44f}, color, t);
        break;
    case EditorIcon::LockOpen:
        draw->AddRectFilled({x + s * 0.25f, y + s * 0.44f}, {x + s * 0.75f, y + s * 0.85f}, color, 1.5f);
        draw->PathArcTo({x + s * 0.50f, y + s * 0.28f}, s * 0.18f, 3.14159f, 6.28318f, 10);
        draw->PathLineTo({x + s * 0.68f, y + s * 0.34f});
        draw->PathStroke(color, 0, t);
        draw->PathClear();
        draw->AddLine({x + s * 0.32f, y + s * 0.28f}, {x + s * 0.32f, y + s * 0.44f}, color, t);
        break;
    case EditorIcon::HandTool:
        draw->AddRect({x + s * 0.28f, y + s * 0.44f}, {x + s * 0.72f, y + s * 0.86f}, color, 2.0f, 0, t);
        draw->AddLine({x + s * 0.36f, y + s * 0.44f}, {x + s * 0.36f, y + s * 0.18f}, color, t);
        draw->AddLine({x + s * 0.46f, y + s * 0.44f}, {x + s * 0.46f, y + s * 0.12f}, color, t);
        draw->AddLine({x + s * 0.56f, y + s * 0.44f}, {x + s * 0.56f, y + s * 0.14f}, color, t);
        draw->AddLine({x + s * 0.66f, y + s * 0.44f}, {x + s * 0.66f, y + s * 0.24f}, color, t);
        draw->AddLine({x + s * 0.28f, y + s * 0.62f}, {x + s * 0.16f, y + s * 0.46f}, color, t);
        break;
    case EditorIcon::MoveTool:
        draw->AddLine({x + s * 0.18f, y + s * 0.50f}, {x + s * 0.82f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.18f}, {x + s * 0.50f, y + s * 0.82f}, color, t);
        draw->AddLine({x + s * 0.28f, y + s * 0.40f}, {x + s * 0.18f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.28f, y + s * 0.60f}, {x + s * 0.18f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.72f, y + s * 0.40f}, {x + s * 0.82f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.72f, y + s * 0.60f}, {x + s * 0.82f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.40f, y + s * 0.28f}, {x + s * 0.50f, y + s * 0.18f}, color, t);
        draw->AddLine({x + s * 0.60f, y + s * 0.28f}, {x + s * 0.50f, y + s * 0.18f}, color, t);
        draw->AddLine({x + s * 0.40f, y + s * 0.72f}, {x + s * 0.50f, y + s * 0.82f}, color, t);
        draw->AddLine({x + s * 0.60f, y + s * 0.72f}, {x + s * 0.50f, y + s * 0.82f}, color, t);
        break;
    case EditorIcon::RotateTool:
        draw->PathArcTo({x + s * 0.50f, y + s * 0.50f}, s * 0.30f, 0.6f, 5.8f, 16);
        draw->PathStroke(color, 0, t);
        draw->PathClear();
        draw->AddLine({x + s * 0.75f, y + s * 0.55f}, {x + s * 0.82f, y + s * 0.42f}, color, t);
        draw->AddLine({x + s * 0.68f, y + s * 0.42f}, {x + s * 0.82f, y + s * 0.42f}, color, t);
        break;
    case EditorIcon::ScaleTool:
        draw->AddLine({x + s * 0.25f, y + s * 0.75f}, {x + s * 0.75f, y + s * 0.25f}, color, t);
        draw->AddRectFilled({x + s * 0.65f, y + s * 0.15f}, {x + s * 0.85f, y + s * 0.35f}, color, 1.0f);
        draw->AddRectFilled({x + s * 0.15f, y + s * 0.65f}, {x + s * 0.35f, y + s * 0.85f}, color, 1.0f);
        break;
    case EditorIcon::RectTool:
        draw->AddRect({x + s * 0.20f, y + s * 0.20f}, {x + s * 0.80f, y + s * 0.80f}, color, 0.0f, 0, t);
        draw->AddRectFilled({x + s * 0.14f, y + s * 0.14f}, {x + s * 0.26f, y + s * 0.26f}, color);
        draw->AddRectFilled({x + s * 0.74f, y + s * 0.14f}, {x + s * 0.86f, y + s * 0.26f}, color);
        draw->AddRectFilled({x + s * 0.14f, y + s * 0.74f}, {x + s * 0.26f, y + s * 0.86f}, color);
        draw->AddRectFilled({x + s * 0.74f, y + s * 0.74f}, {x + s * 0.86f, y + s * 0.86f}, color);
        break;
    case EditorIcon::Pause:
        draw->AddRectFilled({x + s * 0.25f, y + s * 0.20f}, {x + s * 0.43f, y + s * 0.80f}, color, 1.0f);
        draw->AddRectFilled({x + s * 0.57f, y + s * 0.20f}, {x + s * 0.75f, y + s * 0.80f}, color, 1.0f);
        break;
    case EditorIcon::StepForward:
        draw->AddTriangleFilled({x + s * 0.20f, y + s * 0.20f}, {x + s * 0.58f, y + s * 0.50f},
                                {x + s * 0.20f, y + s * 0.80f}, color);
        draw->AddRectFilled({x + s * 0.66f, y + s * 0.20f}, {x + s * 0.80f, y + s * 0.80f}, color, 1.0f);
        break;
    case EditorIcon::Plus:
        draw->AddLine({x + s * 0.20f, y + s * 0.50f}, {x + s * 0.80f, y + s * 0.50f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.20f}, {x + s * 0.50f, y + s * 0.80f}, color, t);
        break;
    case EditorIcon::Info:
        draw->AddCircle({x + s * 0.50f, y + s * 0.50f}, s * 0.42f, color, 16, t);
        draw->AddCircleFilled({x + s * 0.50f, y + s * 0.30f}, std::max(1.0f, s * 0.07f), color);
        draw->AddLine({x + s * 0.50f, y + s * 0.44f}, {x + s * 0.50f, y + s * 0.72f}, color, t * 1.2f);
        break;
    case EditorIcon::Warning:
        draw->AddTriangle({x + s * 0.50f, y + s * 0.12f}, {x + s * 0.88f, y + s * 0.88f}, {x + s * 0.12f, y + s * 0.88f}, color, t);
        draw->AddLine({x + s * 0.50f, y + s * 0.38f}, {x + s * 0.50f, y + s * 0.64f}, color, t * 1.2f);
        draw->AddCircleFilled({x + s * 0.50f, y + s * 0.76f}, std::max(1.0f, s * 0.07f), color);
        break;
    case EditorIcon::Error:
        draw->AddCircle({x + s * 0.50f, y + s * 0.50f}, s * 0.42f, color, 16, t);
        draw->AddLine({x + s * 0.33f, y + s * 0.33f}, {x + s * 0.67f, y + s * 0.67f}, color, t * 1.2f);
        draw->AddLine({x + s * 0.67f, y + s * 0.33f}, {x + s * 0.33f, y + s * 0.67f}, color, t * 1.2f);
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

void setup_editor_fonts(ImGuiIO &io, float baseFontSize)
{
    static const char *fontCandidates[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "assets/fonts/Inter-Regular.ttf",
        "assets/fonts/Roboto-Regular.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc"
    };

    std::error_code ec;
    for (const char *path : fontCandidates)
    {
        if (std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec))
        {
            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 2;
            config.PixelSnapH = true;
            if (io.Fonts->AddFontFromFileTTF(path, baseFontSize, &config))
                return;
        }
    }
    io.Fonts->AddFontDefault();
}

float resolve_editor_ui_scale(float userScale, int logicalWidth, int logicalHeight,
                              int pixelWidth, int pixelHeight) noexcept
{
    if (!std::isfinite(userScale) || userScale < 0.5f || userScale > 3.0f)
        userScale = 1.0f;
    const float dpiScale = logicalWidth > 0 && logicalHeight > 0 && pixelWidth > 0 && pixelHeight > 0
        ? std::max(static_cast<float>(pixelWidth) / logicalWidth,
                   static_cast<float>(pixelHeight) / logicalHeight)
        : 1.0f;
    return std::clamp(userScale * dpiScale, 0.5f, 6.0f);
}

void push_editor_toolbar_style(bool playMode)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.Colors[ImGuiCol_WindowBg]);
    ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_Border]);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, style.WindowBorderSize));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(style.ItemSpacing.x * 0.85f, style.ItemSpacing.y * 0.7f));
}

void pop_editor_toolbar_style()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

bool icon_label_button(EditorIcon icon, const char *label, const char *tooltip, float customWidth)
{
    const char *textEnd = (label != nullptr && !(label[0] == '#' && label[1] == '#'))
        ? (std::strstr(label, "##") ? std::strstr(label, "##") : label + std::strlen(label))
        : label;
    const ImVec2 textSize = ImGui::CalcTextSize(label, textEnd);
    const float height = ImGui::GetFrameHeight();
    const float iconSize = height * 0.70f;
    const float iconPadding = ImGui::GetStyle().FramePadding.x;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float textOffset = iconPadding + iconSize + spacing;
    const float btnWidth = (customWidth > 0.0f && textSize.x == 0.0f)
        ? customWidth
        : (textSize.x > 0.0f ? (textOffset + textSize.x + iconPadding) : height);
    const ImVec2 size(btnWidth, height);
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
    const ImVec2 iconPos = {
        position.x + (textSize.x > 0.0f ? iconPadding : (size.x - iconSize) * 0.5f),
        position.y + (height - iconSize) * 0.5f
    };
    draw_icon(draw, icon, iconPos, iconSize, iconColor);
    if (textSize.x > 0.0f)
    {
        const float textY = position.y + (height - textSize.y) * 0.5f;
        draw->AddText({position.x + textOffset, textY},
                      ImGui::GetColorU32(ImGuiCol_Text), label, textEnd);
    }
    if (hovered && tooltip != nullptr && *tooltip != '\0')
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
    (void)id;
    return pressed;
}

bool icon_button(EditorIcon icon, const char *str_id, bool active, const char *tooltip, ImVec2 buttonSize)
{
    const float frameHeight = ImGui::GetFrameHeight();
    const ImVec2 size = (buttonSize.x > 0.0f && buttonSize.y > 0.0f) ? buttonSize : ImVec2(frameHeight, frameHeight);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(str_id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    if (active || hovered || held)
    {
        const ImU32 background = ImGui::GetColorU32(
            held ? ImGuiCol_ButtonActive
            : active ? ImGuiCol_HeaderActive
            : ImGuiCol_ButtonHovered);
        draw->AddRectFilled(position, {position.x + size.x, position.y + size.y}, background,
                            ImGui::GetStyle().FrameRounding);
    }
    const float iconSize = std::min(size.x, size.y) * 0.70f;
    const ImVec2 iconPos(position.x + (size.x - iconSize) * 0.5f,
                         position.y + (size.y - iconSize) * 0.5f);
    const ImU32 iconColor = active ? ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f))
                                   : ImGui::GetColorU32(ImGuiCol_Text);
    draw_icon(draw, icon, iconPos, iconSize, iconColor);
    if (hovered && tooltip != nullptr && *tooltip != '\0')
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
    return pressed;
}

void draw_editor_icon(EditorIcon icon, ImDrawList *draw, ImVec2 origin, float size, ImU32 color)
{
    if (draw != nullptr && size > 0.0f)
        draw_icon(draw, icon, origin, size, color);
}

std::string_view strip_identity_suffix(std::string_view label) noexcept
{
    if (label.size() < 5 || label.back() != ')')
        return label;

    const size_t openParen = label.rfind(" (#");
    if (openParen == std::string_view::npos || openParen == 0)
        return label;

    const size_t digitsStart = openParen + 3;
    const size_t digitsEnd = label.size() - 1;
    if (digitsStart >= digitsEnd)
        return label;

    for (size_t i = digitsStart; i < digitsEnd; ++i)
    {
        if (label[i] < '0' || label[i] > '9')
            return label;
    }

    return label.substr(0, openParen);
}

std::string format_clean_float(double value, int maxDecimals)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return value < 0 ? "-Inf" : "Inf";
    if (std::abs(value) < 1e-12)
        return "0";

    char buffer[64];
    if (std::abs(value) >= 1e9 || (std::abs(value) > 0.0 && std::abs(value) < 1e-5))
    {
        std::snprintf(buffer, sizeof(buffer), "%.*g", std::max(1, maxDecimals), value);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.*f", std::max(0, maxDecimals), value);
    }

    std::string str = buffer;
    if (str.find('.') != std::string::npos && str.find('e') == std::string::npos && str.find('E') == std::string::npos)
    {
        while (!str.empty() && str.back() == '0')
            str.pop_back();
        if (!str.empty() && str.back() == '.')
            str.pop_back();
    }
    if (str == "-0" || str == "-0.")
        return "0";
    return str.empty() ? "0" : str;
}
} // namespace platformator_editor
