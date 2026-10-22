#pragma once

#include <string>
#include <cstdint>

#include "platformator/debugsettings.h"
#include "platformator/windowsettings.h"

struct RuntimeOptions
{
    std::string sceneFilePath;
    WindowSettings windowSettings;
    DebugSettings debugSettings;
    std::string assetRoot;
    uint64_t smokeSteps = 0;
};

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath);
