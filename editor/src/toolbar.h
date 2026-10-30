#pragma once

#include <functional>
#include <string>
#include <vector>

namespace platformator_editor
{
struct EditorToolbarState
{
    bool projectLocked = false;
    bool operationActive = false;
    bool openScenePending = false;
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
