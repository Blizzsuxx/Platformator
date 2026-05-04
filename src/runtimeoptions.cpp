#include "runtimeoptions.h"

#include <stdexcept>

namespace
{
    int parsePositiveInteger(const std::string &value, const std::string &argumentName)
    {
        const int parsedValue = std::stoi(value);
        if (parsedValue <= 0)
        {
            throw std::runtime_error(argumentName + " must be greater than zero.");
        }

        return parsedValue;
    }

    std::string requireArgumentValue(int argc, char *args[], int &index, const std::string &argumentName)
    {
        if (index + 1 >= argc)
        {
            throw std::runtime_error("Missing value for " + argumentName + ".");
        }

        index++;
        return args[index];
    }
}

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath)
{
    RuntimeOptions runtimeOptions{defaultScenePath, {}};
    bool scenePathSpecified = false;

    for (int index = 1; index < argc; index++)
    {
        const std::string argument = args[index];

        if (argument == "--scene")
        {
            runtimeOptions.sceneFilePath = requireArgumentValue(argc, args, index, argument);
            scenePathSpecified = true;
            continue;
        }

        if (argument == "--window-width")
        {
            runtimeOptions.windowSettings.width = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--window-height")
        {
            runtimeOptions.windowSettings.height = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--fullscreen")
        {
            runtimeOptions.windowSettings.fullscreen = true;
            continue;
        }

        if (argument == "--maximized")
        {
            runtimeOptions.windowSettings.maximizeOnStartup = true;
            continue;
        }

        if (argument == "--keep-aspect-ratio")
        {
            runtimeOptions.windowSettings.keepAspectRatio = true;
            continue;
        }

        if (!argument.starts_with("--") && !scenePathSpecified)
        {
            runtimeOptions.sceneFilePath = argument;
            scenePathSpecified = true;
            continue;
        }

        throw std::runtime_error("Unknown runtime argument: " + argument);
    }

    return runtimeOptions;
}