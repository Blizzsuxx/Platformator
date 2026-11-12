#include "actions.h"

#include <array>

namespace platformator_editor
{
namespace
{
constexpr std::array kActions{
    EditorActionDescriptor{EditorAction::NewScene, "New Scene", "Ctrl+N", false},
    EditorActionDescriptor{EditorAction::OpenScene, "Open Scene", "Ctrl+O", false},
    EditorActionDescriptor{EditorAction::Save, "Save", "Ctrl+S", false},
    EditorActionDescriptor{EditorAction::SaveAs, "Save As", "Ctrl+Shift+S", false},
    EditorActionDescriptor{EditorAction::Undo, "Undo", "Ctrl+Z", false},
    EditorActionDescriptor{EditorAction::Redo, "Redo", "Ctrl+Y", false},
    EditorActionDescriptor{EditorAction::ZoomIn, "Zoom In", "Ctrl++", false},
    EditorActionDescriptor{EditorAction::ZoomOut, "Zoom Out", "Ctrl+-", false},
    EditorActionDescriptor{EditorAction::Build, "Build", "Ctrl+B", false},
    EditorActionDescriptor{EditorAction::BuildAndRun, "Build and Run", "F5", false},
    EditorActionDescriptor{EditorAction::Stop, "Stop", "Shift+F5", false},
    EditorActionDescriptor{EditorAction::SelectHandTool, "Hand Tool", "Q", true},
    EditorActionDescriptor{EditorAction::FrameSelected, "Frame Selected", "F", true},
    EditorActionDescriptor{EditorAction::FrameAll, "Frame All", "Shift+F", true},
    EditorActionDescriptor{EditorAction::SelectMoveTool, "Move Tool", "W", true},
    EditorActionDescriptor{EditorAction::SelectRotateTool, "Rotate Tool", "E", true},
    EditorActionDescriptor{EditorAction::SelectScaleTool, "Scale Tool", "R", true},
    EditorActionDescriptor{EditorAction::RenameSelection, "Rename", "F2", false, true},
    EditorActionDescriptor{EditorAction::DeleteSelection, "Delete Selection", "Delete", false, true},
    EditorActionDescriptor{EditorAction::DuplicateSelection, "Duplicate Selection", "Ctrl+D", false, true},
    EditorActionDescriptor{EditorAction::Preferences, "Editor Preferences", "Ctrl+,", false},
};
} // namespace

std::span<const EditorActionDescriptor> editor_action_descriptors() noexcept
{
    return kActions;
}

bool editor_action_allowed(EditorAction action, const EditorInputCapture &capture) noexcept
{
    if (!capture.appFocused || !capture.focusedHost || capture.textInput ||
        capture.modalOpen || capture.gestureActive || capture.embeddedGameFocused)
        return false;
    for (const auto &descriptor : kActions)
        if (descriptor.action == action)
        {
            if (action == EditorAction::SelectHandTool || action == EditorAction::SelectMoveTool ||
                action == EditorAction::SelectRotateTool || action == EditorAction::SelectScaleTool ||
                action == EditorAction::FrameSelected || action == EditorAction::FrameAll)
            {
                return capture.sceneFocused || capture.hierarchyFocused;
            }
            return (!descriptor.sceneOnly || capture.sceneFocused) &&
                   (!descriptor.hierarchyOnly || capture.hierarchyFocused);
        }
    return false;
}

std::optional<EditorAction> dispatch_editor_key(EditorKey key, bool shift, bool control,
                                               const EditorInputCapture &capture) noexcept
{
    if (!capture.appFocused || !capture.focusedHost || capture.textInput ||
        capture.modalOpen || capture.gestureActive || capture.embeddedGameFocused)
        return std::nullopt;
    const bool sceneOrHierarchy = capture.sceneFocused || capture.hierarchyFocused;
    switch (key)
    {
    case EditorKey::Q:
        if (!sceneOrHierarchy || shift || control)
            return std::nullopt;
        return EditorAction::SelectHandTool;
    case EditorKey::F:
        if (!sceneOrHierarchy || control)
            return std::nullopt;
        return shift ? EditorAction::FrameAll : EditorAction::FrameSelected;
    case EditorKey::F5:
        if (control)
            return std::nullopt;
        return shift ? EditorAction::Stop : EditorAction::BuildAndRun;
    case EditorKey::W:
        if (!sceneOrHierarchy || shift || control)
            return std::nullopt;
        return EditorAction::SelectMoveTool;
    case EditorKey::E:
        if (!sceneOrHierarchy || shift || control)
            return std::nullopt;
        return EditorAction::SelectRotateTool;
    case EditorKey::R:
        if (!sceneOrHierarchy || shift || control)
            return std::nullopt;
        return EditorAction::SelectScaleTool;
    case EditorKey::F2:
        if (!capture.hierarchyFocused || shift || control)
            return std::nullopt;
        return EditorAction::RenameSelection;
    case EditorKey::Delete:
        if (!capture.hierarchyFocused || shift || control)
            return std::nullopt;
        return EditorAction::DeleteSelection;
    case EditorKey::D:
        if (!capture.hierarchyFocused || !control || shift)
            return std::nullopt;
        return EditorAction::DuplicateSelection;
    case EditorKey::Comma:
        if (!control || shift)
            return std::nullopt;
        return EditorAction::Preferences;
    }
    return std::nullopt;
}
} // namespace platformator_editor
