#pragma once

#include <string>
#include <cstdint>
#include <optional>

#include "platformator/debugsettings.h"
#include "platformator/windowsettings.h"

struct EditorSessionLaunchOptions
{
    std::uintptr_t commandReadHandle = 3;
    std::uintptr_t eventWriteHandle = 4;
    std::uintptr_t frameMappingHandle = 5;
};

struct RuntimeOptions
{
    // Loaded automatically by Runtime(options) when non-empty.
    std::string sceneFilePath;
    WindowSettings windowSettings;
    DebugSettings debugSettings;
    // Must point to the assets/ directory itself; leave empty to auto-discover <exe_dir>/assets or ../assets.
    std::string assetRoot;
    uint64_t smokeSteps = 0;
    std::optional<EditorSessionLaunchOptions> editorSession;
};

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath);
