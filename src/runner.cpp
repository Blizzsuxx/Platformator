#include "platformator/runner.h"

#include "platformator/runtime.h"
#include "platformator/scriptregistration.h"
#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

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
        std::cerr << "Platformator: " << error.what() << '\n';
        return 1;
    }
}

int run(int argc, char *args[], const std::string &defaultScenePath)
{
    try
    {
        const bool catalogRequested =
            std::any_of(args + 1, args + argc, [](const char *argument) {
                return argument != nullptr && std::string_view(argument) == "--editor-catalog";
            });
        if (catalogRequested)
        {
            if (argc != 2 || args[1] == nullptr || std::string_view(args[1]) != "--editor-catalog")
                throw std::runtime_error("--editor-catalog cannot be combined with runtime arguments.");
            std::cout << exportRegisteredBehaviorCatalog().dump() << '\n';
            return 0;
        }
        return run(parseRuntimeOptions(argc, args, defaultScenePath));
    }
    catch (const std::exception &error)
    {
        std::cerr << "Platformator: " << error.what() << '\n';
        return 1;
    }
}
} // namespace platformator
