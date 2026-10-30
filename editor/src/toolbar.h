#pragma once

#include <functional>
#include <string>
#include <vector>

namespace platformator_editor
{
struct EditorToolbarState
{
    bool projectLocked = false;
    bool canUndo = false;
    bool canRedo = false;
    bool operationActive = false;
    bool openScenePending = false;
    std::string sceneLabel;
    std::string scenePath;
    std::string status;
};

struct EditorToolbarActions
{
    std::function<void()> newScene;
    std::function<void()> openScene;
    std::function<void()> save;
    std::function<void()> build;
    std::function<void()> buildAndRun;
    std::function<void()> stop;
    std::function<void()> undo;
    std::function<void()> redo;
    std::function<void()> validate;
    std::function<void()> openProject;
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
} // namespace platformator_editor
