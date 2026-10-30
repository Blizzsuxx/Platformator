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
    Folder
};

void apply_editor_style(float scale);
void push_editor_toolbar_style();
void pop_editor_toolbar_style();
bool icon_label_button(EditorIcon icon, const char *label, const char *tooltip = nullptr);
void draw_editor_icon(EditorIcon icon, ImDrawList *draw, ImVec2 origin, float size, ImU32 color);
} // namespace platformator_editor
