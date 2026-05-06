#pragma once

#include <string>

#include <debugsettings.h>
#include <windowsettings.h>

struct RuntimeOptions
{
    std::string sceneFilePath;
    WindowSettings windowSettings;
    DebugSettings debugSettings;
};

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath);