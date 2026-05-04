#pragma once

#include <string>

#include "windowsettings.h"

struct RuntimeOptions
{
    std::string sceneFilePath;
    WindowSettings windowSettings;
};

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath);