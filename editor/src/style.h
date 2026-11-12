#pragma once

#include <string_view>

#include <imgui.h>

namespace platformator_editor
{
enum class EditorIcon
{
    NewDocument,
    Open,
    Save,
    Undo,
    Redo,
    Validate,
    Build,
    Run,
    Stop,
    GameObject,
    Animator,
    Audio,
    Camera,
    Collider,
    Rigidbody,
    Script,
    Sprite,
    UnknownComponent,
    Folder,
    MoreVertical,
    EyeOpen,
    EyeClosed,
    LockClosed,
    LockOpen,
    HandTool,
    MoveTool,
    RotateTool,
    ScaleTool,
    RectTool,
    Pause,
    StepForward,
    Plus
};

void apply_editor_style(float scale);
void setup_editor_fonts(ImGuiIO &io, float baseFontSize = 14.0f);
float resolve_editor_ui_scale(float userScale, int logicalWidth, int logicalHeight,
                              int pixelWidth, int pixelHeight) noexcept;
void push_editor_toolbar_style(bool playMode = false);
void pop_editor_toolbar_style();
bool icon_label_button(EditorIcon icon, const char *label, const char *tooltip = nullptr);
bool icon_button(EditorIcon icon, const char *str_id, bool active = false, const char *tooltip = nullptr, ImVec2 size = {});
void draw_editor_icon(EditorIcon icon, ImDrawList *draw, ImVec2 origin, float size, ImU32 color);
} // namespace platformator_editor
