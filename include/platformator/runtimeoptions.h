#pragma once

#include <string>

#include "platformator/debugsettings.h"
#include "platformator/windowsettings.h"

struct RuntimeOptions
{
    std::string sceneFilePath;
    WindowSettings windowSettings;
    DebugSettings debugSettings;
};

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath);