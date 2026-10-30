#include <platformator/camera.h>
#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>
#include <platformator/sprite.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "runtime_work_probe.h"
#include "test_support.h"

namespace
{
class ScopedMode
{
  public:
    explicit ScopedMode(const char *mode)
    {
        if (const char *previous = std::getenv("PLATFORMATOR_RENDER_CULLING_MODE"))
        {
            hadPrevious_ = true;
            previous_ = previous;
        }
        if (setenv("PLATFORMATOR_RENDER_CULLING_MODE", mode, 1) != 0)
            throw std::runtime_error("Could not set the internal comparison mode");
    }

    ScopedMode(const ScopedMode &) = delete;
    ScopedMode &operator=(const ScopedMode &) = delete;

    ~ScopedMode()
    {
        if (hadPrevious_)
            setenv("PLATFORMATOR_RENDER_CULLING_MODE", previous_.c_str(), 1);
        else
            unsetenv("PLATFORMATOR_RENDER_CULLING_MODE");
    }

  private:
    std::string previous_;
    bool hadPrevious_ = false;
};

RuntimeOptions makeOptions(bool enableCulling, bool headless)
{
    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.windowSettings.enableRenderCulling = enableCulling;
    options.windowSettings.headless = headless;
    return options;
}

void checkInactiveModesDoNotCreateState()
{
    test_support::headlessEnvironment();
    for (const auto mode : {"current-linear", "cached-linear", "indexed"})
    {
        ScopedMode selection(mode);
        for (const auto [enabled, headless] : {std::pair{true, false}, std::pair{false, false},
                                                std::pair{true, true}})
        {
            platformator_test::work = {};
            platformator::Runtime runtime(makeOptions(enabled, headless));
            const size_t expectedCreations = enabled && !headless && std::string(mode) != "current-linear" ? 1 : 0;
            test_support::require(platformator_test::work.renderCullingStateCreations == expectedCreations,
                                  "State creation must be limited to renderer-backed cached modes");

            if (!headless)
            {
                runtime.createMainCameraIfNoMainCameraExists();
                auto *object = runtime.createGameObject()->setPosition({700.0f, 500.0f});
                auto *sprite = object->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 32.0f, 24.0f)
                                   ->getComponent<Sprite>();
                test_support::require(sprite != nullptr, "Could not create a public Sprite consumer");
                runtime.simulateAndRenderFrame(test_support::dt);
                if (expectedCreations == 0)
                    test_support::require(platformator_test::work.renderCullingDirtyEnqueues == 0,
                                          "Inactive modes must not enqueue geometry changes");
            }
            else
            {
                test_support::require(runtime.getRenderer() == nullptr,
                                      "Headless mode unexpectedly created an SDL renderer");
                runtime.createMainCameraIfNoMainCameraExists();
                auto *object = runtime.createGameObject();
                object->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 32.0f, 24.0f);
                runtime.simulateAndRenderFrame(test_support::dt);
                test_support::require(platformator_test::work.renderCullingStateCreations == 0 &&
                                          platformator_test::work.renderCullingDirtyEnqueues == 0,
                                      "Headless mode must not retain or publish culling state");
            }
        }
    }
}

void countPresent(SDL_Renderer *, int, int, void *context)
{
    ++*static_cast<size_t *>(context);
}

void checkNoCameraAndDirectCameraContracts()
{
    test_support::headlessEnvironment();
    {
        ScopedMode selection("indexed");
        platformator_test::work = {};
        platformator::Runtime runtime(makeOptions(true, false));
        size_t presents = 0;
        platformator_test::RenderObservation observation;
        observation.readbackContext = &presents;
        observation.beforePresent = &countPresent;
        {
            platformator_test::ScopedRenderObservation scope(observation);
            test_support::require(runtime.simulateAndRenderFrame(test_support::dt),
                                  "A no-camera frame failed to clear and present");
        }
        test_support::require(presents == 1 && platformator_test::work.renderSubmittedSprites == 0,
                              "A no-camera frame must clear/present without sprite submissions");
    }

    {
        ScopedMode selection("indexed");
        platformator_test::work = {};
        platformator::Runtime runtime(makeOptions(true, false));
        runtime.createMainCameraIfNoMainCameraExists();
        auto *object = runtime.createGameObject()->setPosition({-800.0f, 30.0f});
        auto *sprite = object->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 20.0f, 20.0f)->getComponent<Sprite>();
        test_support::require(sprite != nullptr, "Could not create a direct Camera render fixture");
        platformator_test::work = {};
        runtime.getMainCamera()->render(sprite, runtime.getRenderer(), 320, 240);
        test_support::require(platformator_test::work.renderSubmittedSprites == 1,
                              "The public four-argument Camera::render must preserve submit-all behavior");
    }
}

void checkUnknownModeRejected()
{
    test_support::headlessEnvironment();
    ScopedMode selection("not-a-render-strategy");
    bool rejected = false;
    try
    {
        platformator::Runtime runtime(makeOptions(true, true));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    test_support::require(rejected, "Unknown internal comparison modes must fail before scene population");
}

void run()
{
    checkInactiveModesDoNotCreateState();
    checkNoCameraAndDirectCameraContracts();
    checkUnknownModeRejected();
}
} // namespace

int main()
{
    try
    {
        run();
        std::cout << "[PASS] render_culling_contract_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] render_culling_contract_test: " << exception.what() << '\n';
        return 1;
    }
}
