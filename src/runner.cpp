#include "platformator/runner.h"

#include "platformator/runtime.h"

namespace platformator
{
    int run(const RuntimeOptions &runtimeOptions)
    {
        Runtime runtime(runtimeOptions);
        runtime.loadScene(runtimeOptions.sceneFilePath);
        runtime.run();

        return 0;
    }

    int run(int argc, char *args[], const std::string &defaultScenePath)
    {
        return run(parseRuntimeOptions(argc, args, defaultScenePath));
    }
} // namespace platformator