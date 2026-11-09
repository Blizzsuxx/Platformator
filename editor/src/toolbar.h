#pragma once

#include <functional>
#include <string>
#include <vector>

#include "accessibility.h"
#include "viewport.h"

namespace platformator_editor
{
struct EditorToolbarState
{
    bool projectLocked = false;
    bool operationActive = false;
    bool openScenePending = false;
    bool sceneAvailable = true;
    SceneTool activeSceneTool = SceneTool::Move;
    std::string projectLabel;
    std::string projectPath;
    std::string sceneLabel;
    std::string scenePath;
    std::string status;
};

struct EditorToolbarActions
{
    std::function<void()> openScene;
    std::function<void()> save;
    std::function<void()> build;
    std::function<void()> buildAndRun;
    std::function<void()> stop;
    std::function<void()> moveTool;
    std::function<void()> rotateTool;
    std::function<void()> scaleTool;
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
std::vector<SemanticNode> editor_toolbar_semantic_nodes(const EditorToolbarState &state,
                                                        const EditorToolbarFrame &frame);
} // namespace platformator_editor
