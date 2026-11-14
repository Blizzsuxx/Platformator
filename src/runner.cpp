#include "platformator/runner.h"

#include "platformator/runtime.h"
#include "platformator/scriptregistration.h"
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
#include "editorsession.h"
#endif
#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace platformator
{
int run(const RuntimeOptions &runtimeOptions)
{
    try
    {
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        std::unique_ptr<EditorSessionService> editorSession;
        if (runtimeOptions.editorSession)
            editorSession = std::make_unique<EditorSessionService>(*runtimeOptions.editorSession);
#else
        if (runtimeOptions.editorSession)
            throw std::runtime_error("this runner was built without editor-session support.");
#endif
        RuntimeOptions effectiveOptions = runtimeOptions;
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (editorSession)
        {
            effectiveOptions.windowSettings.hidden = true;
            if (effectiveOptions.windowSettings.width != 640 ||
                effectiveOptions.windowSettings.height != 360)
                throw std::runtime_error("embedded editor-session rendering requires a 640x360 output.");
        }
#endif
        Runtime runtime(effectiveOptions);
        if (!runtimeOptions.sceneFilePath.empty())
            runtime.loadScene(runtimeOptions.sceneFilePath);
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
        if (editorSession)
        {
            runtime.run_with_owner_service(
                [&](SDLWindow &window) {
                    return editorSession->pump(runtime.getRuntimeSerial(), window);
                },
                [&](std::uint64_t stepNumber) {
                    editorSession->simulation_step_completed(runtime.getRuntimeSerial(), stepNumber);
                });
            editorSession->finish(runtime.getRuntimeSerial());
        }
        else
#endif
        runtime.run();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Platformator: " << error.what() << '\n';
        return 1;
    }
}

int run(int argc, char *args[], const std::string &defaultScenePath)
{
    try
    {
        return run(parseRuntimeOptions(argc, args, defaultScenePath));
    }
    catch (const std::exception &error)
    {
        std::cerr << "Platformator: " << error.what() << '\n';
        return 1;
    }
}
} // namespace platformator
