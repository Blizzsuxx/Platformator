#pragma once

#include <string>
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
    Plus,
    Info,
    Warning,
    Error
};

void apply_editor_style(float scale);
void setup_editor_fonts(ImGuiIO &io, float baseFontSize = 14.0f);
float resolve_editor_ui_scale(float userScale, int logicalWidth, int logicalHeight,
                              int pixelWidth, int pixelHeight) noexcept;
void push_editor_toolbar_style(bool playMode = false);
void pop_editor_toolbar_style();
bool icon_label_button(EditorIcon icon, const char *label, const char *tooltip = nullptr, float customWidth = 0.0f);
bool icon_button(EditorIcon icon, const char *str_id, bool active = false, const char *tooltip = nullptr, ImVec2 size = {});
void draw_editor_icon(EditorIcon icon, ImDrawList *draw, ImVec2 origin, float size, ImU32 color);
std::string_view strip_identity_suffix(std::string_view label) noexcept;
std::string format_clean_float(double value, int maxDecimals = 3);
} // namespace platformator_editor
