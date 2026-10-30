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
    EditorActionDescriptor{EditorAction::FrameAll, "Frame All", "F", true},
    EditorActionDescriptor{EditorAction::FrameSelected, "Frame Selected", "Shift+F", true},
};
} // namespace

std::span<const EditorActionDescriptor> editor_action_descriptors() noexcept
{
    return kActions;
}

bool editor_action_allowed(EditorAction action, const EditorInputCapture &capture) noexcept
{
    if (!capture.appFocused || capture.textInput || capture.modalOpen || capture.gestureActive)
        return false;
    for (const auto &descriptor : kActions)
        if (descriptor.action == action)
            return !descriptor.sceneOnly || capture.sceneFocused;
    return false;
}

std::optional<EditorAction> dispatch_editor_key(EditorKey key, bool shift, bool control,
                                               const EditorInputCapture &capture) noexcept
{
    if (!capture.appFocused || capture.textInput || capture.modalOpen || capture.gestureActive)
        return std::nullopt;
    switch (key)
    {
    case EditorKey::F:
        if (!capture.sceneFocused || control)
            return std::nullopt;
        return shift ? EditorAction::FrameSelected : EditorAction::FrameAll;
    case EditorKey::F5:
        if (control)
            return std::nullopt;
        return shift ? EditorAction::Stop : EditorAction::BuildAndRun;
    }
    return std::nullopt;
}
} // namespace platformator_editor
