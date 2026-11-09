#include "platformator/runtimeoptions.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <charconv>

namespace
{
int parsePositiveInteger(const std::string &value, const std::string &argumentName)
{
    int parsedValue = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || parsedValue <= 0)
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

std::uintptr_t parseSessionDescriptor(const std::string &value, const std::string &argumentName)
{
    std::uint64_t parsedValue = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || parsedValue == 0 ||
        parsedValue > std::numeric_limits<std::uintptr_t>::max())
        throw std::runtime_error(argumentName + " must be a positive native descriptor value.");
    return static_cast<std::uintptr_t>(parsedValue);
}

std::string normalizeArgumentValue(std::string value)
{
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [](unsigned char character) { return !std::isspace(character); }));

    value.erase(
        std::find_if(value.rbegin(), value.rend(), [](unsigned char character) { return !std::isspace(character); })
            .base(),
        value.end());

    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

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
        return {false, false, false, false, false};
    }

    DebugSettings debugSettings{false, false, false, false, false};
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
} // namespace

RuntimeOptions parseRuntimeOptions(int argc, char *args[], const std::string &defaultScenePath)
{
    RuntimeOptions runtimeOptions{defaultScenePath, {}, {}};
    bool scenePathSpecified = false;
    bool editorSessionRequested = false;
    std::optional<std::uintptr_t> commandReadHandle;
    std::optional<std::uintptr_t> eventWriteHandle;
    std::optional<std::uintptr_t> frameMappingHandle;

    for (int index = 1; index < argc; index++)
    {
        const std::string argument = args[index];

        if (argument == "--scene")
        {
            runtimeOptions.sceneFilePath = requireArgumentValue(argc, args, index, argument);
            scenePathSpecified = true;
            continue;
        }

        if (argument == "--asset-root")
        {
            runtimeOptions.assetRoot = requireArgumentValue(argc, args, index, argument);
            continue;
        }
        if (argument == "--editor-session-v1")
        {
            if (editorSessionRequested)
                throw std::runtime_error("--editor-session-v1 may be specified only once.");
            editorSessionRequested = true;
            continue;
        }
        if (argument == "--editor-session-command-fd")
        {
            if (commandReadHandle)
                throw std::runtime_error("--editor-session-command-fd may be specified only once.");
            commandReadHandle = parseSessionDescriptor(
                requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--editor-session-event-fd")
        {
            if (eventWriteHandle)
                throw std::runtime_error("--editor-session-event-fd may be specified only once.");
            eventWriteHandle = parseSessionDescriptor(
                requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--editor-session-frame-fd")
        {
            if (frameMappingHandle)
                throw std::runtime_error("--editor-session-frame-fd may be specified only once.");
            frameMappingHandle = parseSessionDescriptor(
                requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--smoke-steps")
        {
            const auto value = requireArgumentValue(argc, args, index, argument);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), runtimeOptions.smokeSteps);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !runtimeOptions.smokeSteps)
                throw std::runtime_error("--smoke-steps must be a positive integer");
            continue;
        }
        if (argument == "--headless")
        {
            runtimeOptions.windowSettings.headless = true;
            continue;
        }

        if (argument == "--no-render-culling")
        {
            runtimeOptions.windowSettings.enableRenderCulling = false;
            continue;
        }

        if (argument == "--window-width")
        {
            runtimeOptions.windowSettings.width =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--window-height")
        {
            runtimeOptions.windowSettings.height =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
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

        if (argument == "--start-paused")
        {
            runtimeOptions.debugSettings.startPaused = true;
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

    const bool anySessionDescriptor = commandReadHandle || eventWriteHandle || frameMappingHandle;
    if (editorSessionRequested || anySessionDescriptor)
    {
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (!editorSessionRequested || !commandReadHandle || !eventWriteHandle || !frameMappingHandle)
            throw std::runtime_error("editor-session v1 requires its command, event, and frame descriptors.");
        if (*commandReadHandle != 3 || *eventWriteHandle != 4 || *frameMappingHandle != 5)
            throw std::runtime_error("editor-session v1 descriptors must be mapped to child descriptors 3, 4, and 5.");
        if (runtimeOptions.smokeSteps != 0 || runtimeOptions.windowSettings.headless)
            throw std::runtime_error("editor-session v1 cannot be combined with smoke or headless runtime modes.");
        runtimeOptions.editorSession = EditorSessionLaunchOptions{
            *commandReadHandle, *eventWriteHandle, *frameMappingHandle};
#else
        throw std::runtime_error("this runner was built without editor-session support.");
#endif
    }

    return runtimeOptions;
}
