#pragma once

#include <span>
#include <string_view>
#include <optional>

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
    Build,
    BuildAndRun,
    Stop,
    FrameSelected,
    FrameAll,
    SelectMoveTool,
    SelectRotateTool,
    SelectScaleTool,
    RenameSelection,
    DeleteSelection,
    DuplicateSelection,
    Preferences
};

struct EditorActionDescriptor
{
    EditorAction action;
    std::string_view label;
    std::string_view shortcut;
    bool sceneOnly;
    bool hierarchyOnly = false;
};

struct EditorInputCapture
{
    bool appFocused = true;
    bool textInput = false;
    bool modalOpen = false;
    bool gestureActive = false;
    bool sceneFocused = false;
    bool hierarchyFocused = false;
    bool focusedHost = true;
};

enum class EditorKey
{
    F,
    F5,
    W,
    E,
    R,
    F2,
    Delete,
    D,
    Comma
};

std::span<const EditorActionDescriptor> editor_action_descriptors() noexcept;
bool editor_action_allowed(EditorAction action, const EditorInputCapture &capture) noexcept;
std::optional<EditorAction> dispatch_editor_key(EditorKey key, bool shift, bool control,
                                               const EditorInputCapture &capture) noexcept;
} // namespace platformator_editor
