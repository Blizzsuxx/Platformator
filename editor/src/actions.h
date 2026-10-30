#pragma once

#include <span>
#include <string_view>

namespace platformator_editor
{
enum class EditorAction
{
    NewScene,
    OpenScene,
    Save,
    SaveAs,
    Undo,
    Redo,
    ZoomIn,
    ZoomOut,
    Validate,
    Build,
    BuildAndRun,
    Stop,
    Settings,
    FrameSelected,
    FrameAll
};

struct EditorActionDescriptor
{
    EditorAction action;
    std::string_view label;
    std::string_view shortcut;
    bool sceneOnly;
};

struct EditorInputCapture
{
    bool appFocused = true;
    bool textInput = false;
    bool modalOpen = false;
    bool gestureActive = false;
    bool sceneFocused = false;
};

std::span<const EditorActionDescriptor> editor_action_descriptors() noexcept;
bool editor_action_allowed(EditorAction action, const EditorInputCapture &capture) noexcept;
} // namespace platformator_editor
