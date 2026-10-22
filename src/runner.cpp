#include "platformator/runner.h"

#include "platformator/runtime.h"
#include <cstdio>
#include <exception>

namespace platformator
{
int run(const RuntimeOptions &runtimeOptions)
{
    try
    {
        Runtime runtime(runtimeOptions);
        if (!runtimeOptions.sceneFilePath.empty())
            runtime.loadScene(runtimeOptions.sceneFilePath);
        runtime.run();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Platformator: %s\n", error.what());
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
        std::fprintf(stderr, "Platformator: %s\n", error.what());
        return 1;
    }
}
} // namespace platformator
