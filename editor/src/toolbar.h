#pragma once

#include <functional>
#include <string>
#include <vector>

#include "accessibility.h"

namespace platformator_editor
{
struct EditorToolbarState
{
    bool projectLocked = false;
    bool operationActive = false;
    bool isRunning = false;
    bool isPaused = false;
    bool pauseResumeEnabled = false;
    bool stepEnabled = false;
};

struct EditorToolbarActions
{
    std::function<void()> buildAndRun;
    std::function<void()> pause;
    std::function<void()> resume;
    std::function<void()> step;
    std::function<void()> stop;
};

struct EditorToolbarItemBounds
{
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct EditorToolbarFrame
{
    float height = 0.0f;
    bool compact = false;
    std::vector<EditorToolbarItemBounds> items;
};

EditorToolbarFrame draw_editor_toolbar(const EditorToolbarState &state,
                                       const EditorToolbarActions &actions);
std::vector<SemanticNode> editor_toolbar_semantic_nodes(const EditorToolbarFrame &frame);
} // namespace platformator_editor
