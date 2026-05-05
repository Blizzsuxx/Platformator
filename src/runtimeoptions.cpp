#include "runtimeoptions.h"

#include <algorithm>
#include <cctype>
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

    std::string normalizeArgumentValue(std::string value)
    {
        value.erase(
            value.begin(),
            std::find_if(
                value.begin(),
                value.end(),
                [](unsigned char character)
                {
                    return !std::isspace(character);
                }));

        value.erase(
            std::find_if(
                value.rbegin(),
                value.rend(),
                [](unsigned char character)
                {
                    return !std::isspace(character);
                })
                .base(),
            value.end());

        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });

        return value;
    }

    void applyDebugDrawToken(DebugSettings &debugSettings, const std::string &token)
    {
        if (token == "colliders")
        {
            debugSettings.showColliders = true;
            return;
        }

        if (token == "collision-points" || token == "points")
        {
            debugSettings.showCollisionPoints = true;
            return;
        }

        if (token == "collision-normals" || token == "normals")
        {
            debugSettings.showCollisionNormals = true;
            return;
        }

        if (token == "collisions")
        {
            debugSettings.showCollisionPoints = true;
            debugSettings.showCollisionNormals = true;
            return;
        }

        if (token == "grid-cells" || token == "gridcells")
        {
            debugSettings.showGridCells = true;
            return;
        }

        throw std::runtime_error("Unknown debug draw category: " + token);
    }

    DebugSettings parseDebugDrawSettings(const std::string &value)
    {
        const std::string normalizedValue = normalizeArgumentValue(value);
        if (normalizedValue == "all" || normalizedValue == "default")
        {
            return {};
        }

        if (normalizedValue == "none")
        {
            return {false, false, false, false};
        }

        DebugSettings debugSettings{false, false, false, false};
        size_t tokenStart = 0;

        while (tokenStart <= value.size())
        {
            const size_t tokenEnd = value.find(',', tokenStart);
            const std::string token = normalizeArgumentValue(value.substr(tokenStart, tokenEnd - tokenStart));
            if (token.empty())
            {
                throw std::runtime_error("Debug draw categories cannot be empty.");
            }

            applyDebugDrawToken(debugSettings, token);

            if (tokenEnd == std::string::npos)
            {
                break;
            }

            tokenStart = tokenEnd + 1;
        }

        return debugSettings;
    }
}

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath)
{
    RuntimeOptions runtimeOptions{defaultScenePath, {}, {}};
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

        if (argument == "--debug-draw")
        {
            runtimeOptions.debugSettings = parseDebugDrawSettings(requireArgumentValue(argc, args, index, argument));
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