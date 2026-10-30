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
    Stop
};

void apply_editor_style(float scale);
bool icon_label_button(EditorIcon icon, const char *label, const char *tooltip = nullptr);
} // namespace platformator_editor
