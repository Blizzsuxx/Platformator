#include "platformator/runner.h"

#include "platformator/runtime.h"
#include "editor/editorsession.h"
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace platformator
{
namespace
{
int runWithExceptionHandling(auto &&fn)
{
    try
    {
        return fn();
    }
    catch (const std::exception &error)
    {
        std::cerr << "Platformator: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

int run(const RuntimeOptions &runtimeOptions)
{
    return runWithExceptionHandling([&] {
        std::unique_ptr<EditorSessionService> editorSession;
        if (runtimeOptions.editorSession)
        {
            editorSession = std::make_unique<EditorSessionService>(*runtimeOptions.editorSession);
            if (runtimeOptions.windowSettings.width != 640 || runtimeOptions.windowSettings.height != 360)
                throw std::runtime_error("embedded editor-session rendering requires a 640x360 output.");
        }
        Runtime runtime(runtimeOptions);
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
        {
            runtime.run();
        }
        return 0;
    });
}

int run(int argc, char *args[], const std::string &defaultScenePath)
{
    return runWithExceptionHandling([&] {
        return run(parseRuntimeOptions(argc, args, defaultScenePath));
    });
}
} // namespace platformator
