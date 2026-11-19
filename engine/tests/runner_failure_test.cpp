#include "test_support.h"
#include <iostream>
#include <platformator/boxcollider.h>
#include <platformator/runner.h>

using namespace test_support;
int main(int argc, char **argv)
{
    headlessEnvironment();
    try
    {
        const std::string mode = argc > 1 ? argv[1] : "runner";
        if (mode == "startup")
        {
            test_support::setEnvironment("SDL_VIDEODRIVER", "platformator-invalid-driver");
            bool failed = false;
            try
            {
                platformator::Runtime runtime;
            }
            catch (const std::exception &)
            {
                failed = true;
            }
            require(failed && !platformator::Runtime::tryGetCurrent(),
                    "Backend failure must throw without publishing a runtime");
            return 0;
        }
        if (mode == "physics")
        {
            test_support::setEnvironment("SDL_VIDEODRIVER", "platformator-invalid-driver");
            test_support::setEnvironment("SDL_AUDIODRIVER", "platformator-invalid-driver");
            platformator::Runtime runtime(physicsOnly());
            require(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0,
                    "Physics-only startup cannot acquire video/audio subsystems");
            runtime.createGameObject()->addComponent<BoxCollider>(10, 10);
            runtime.simulateFrame(dt);
            return 0;
        }
        const auto path = std::filesystem::absolute("runner-failure.scene");
        RuntimeOptions options = physicsOnly();
        options.smokeSteps = 3;
        options.sceneFilePath = "missing-runner.scene";
        require(platformator::run(options) != 0, "Missing CLI scene must return nonzero");
        {
            std::ofstream file(path);
            file << "malformed";
        }
        options.sceneFilePath = path.string();
        require(platformator::run(options) != 0, "Malformed CLI scene must return nonzero");
        writeScene(path, "smoke");
        require(platformator::run(options) == 0, "Valid finite smoke must complete successfully");
        char app[] = "test", flag[] = "--smoke-steps", invalid[] = "0";
        char *arguments[] = {app, flag, invalid};
        require(platformator::run(3, arguments, path.string()) != 0, "Invalid CLI values must return nonzero");
        std::filesystem::remove(path);
        std::cout << "[PASS] runner failure statuses\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
