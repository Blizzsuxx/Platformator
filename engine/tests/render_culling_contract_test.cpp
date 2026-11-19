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
#include "runtimeaccess.h"
#include "sdlwindow.h"
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
        if (mode == nullptr)
        {
            if (!test_support::clearEnvironment("PLATFORMATOR_RENDER_CULLING_MODE"))
                throw std::runtime_error("Could not unset the internal comparison mode");
        }
        else if (!test_support::setEnvironment("PLATFORMATOR_RENDER_CULLING_MODE", mode))
        {
            throw std::runtime_error("Could not set the internal comparison mode");
        }
    }

    ScopedMode(const ScopedMode &) = delete;
    ScopedMode &operator=(const ScopedMode &) = delete;

    ~ScopedMode()
    {
        if (hadPrevious_)
            test_support::setEnvironment("PLATFORMATOR_RENDER_CULLING_MODE", previous_.c_str());
        else
            test_support::clearEnvironment("PLATFORMATOR_RENDER_CULLING_MODE");
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
    struct SelectorCase
    {
        const char *envValue;
        const char *expectedRequested;
    };
    const SelectorCase cases[] = {
        {nullptr, "cached-linear"},
        {"", "cached-linear"},
        {"cached-linear", "cached-linear"},
    };
    for (const auto &selector : cases)
    {
        ScopedMode selection(selector.envValue);
        for (const auto [enabled, headless] :
             {std::pair{true, false}, std::pair{false, false}, std::pair{true, true}, std::pair{false, true}})
        {
            platformator_test::work = {};
            const auto options = makeOptions(enabled, headless);
            const auto expectedPolicy = SDLWindow::resolveRenderCullingPolicy(options.windowSettings);
            test_support::require(std::string(SDLWindow::renderCullingModeName(expectedPolicy.requestedMode)) ==
                                      selector.expectedRequested,
                                  "Shared resolver returned unexpected requested culling mode");

            platformator::Runtime runtime(options);
            const auto *window = platformator_detail::RuntimeAccess::window();
            test_support::require(window != nullptr, "Runtime missing SDLWindow");
            const auto windowPolicy = window->getRenderCullingPolicy();
            test_support::require(windowPolicy.requestedMode == expectedPolicy.requestedMode &&
                                      windowPolicy.effectiveMode == expectedPolicy.effectiveMode &&
                                      windowPolicy.cullingEnabled == expectedPolicy.cullingEnabled &&
                                      windowPolicy.headless == expectedPolicy.headless,
                                  "Window policy must match shared resolver policy");

            const std::string expectedEffectiveName =
                headless ? "headless-no-rendering" : (!enabled ? "submit-all" : selector.expectedRequested);
            test_support::require(std::string(SDLWindow::effectiveRenderPolicyName(windowPolicy)) ==
                                      expectedEffectiveName,
                                  "Effective policy name mismatch");

            const size_t expectedCreations = enabled && !headless ? 1 : 0;
            test_support::require(platformator_test::work.renderCullingStateCreations == expectedCreations,
                                  "State creation must be limited to renderer-backed enabled culling");

            if (!headless)
            {
                runtime.createMainCameraIfNoMainCameraExists();
                auto *object = runtime.createGameObject()->setPosition({700.0f, 500.0f});
                auto *sprite = object->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 32.0f, 24.0f)
                                   ->getComponent<Sprite>();
                test_support::require(sprite != nullptr, "Could not create a public Sprite consumer");
                runtime.simulateAndRenderFrame(test_support::dt);
                if (expectedCreations == 0)
                {
                    test_support::require(platformator_test::work.renderCullingDirtyEnqueues == 0 &&
                                              platformator_test::work.renderCullingOwnedBytes == 0,
                                          "Submit-all mode must not enqueue geometry changes or allocate culling state");
                }
                else
                {
                    test_support::require(platformator_test::work.renderCullingOwnedRecords == 1 &&
                                              platformator_test::work.renderCullingOwnedBytes > 0,
                                          "Cached-linear mode must retain common culling state for live sprites");
                }
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
                                          platformator_test::work.renderCullingDirtyEnqueues == 0 &&
                                          platformator_test::work.renderCullingOwnedBytes == 0 &&
                                          platformator_test::work.spriteOrderOwnedRecords == 0 &&
                                          platformator_test::work.spriteOrderOwnedBytes == 0,
                                      "Headless mode must not retain or publish culling or ordering state");
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
    for (const char *selector : {static_cast<const char *>(nullptr), "cached-linear"})
    {
        {
            ScopedMode selection(selector);
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
            ScopedMode selection(selector);
            platformator_test::work = {};
            platformator::Runtime runtime(makeOptions(true, false));
            runtime.createMainCameraIfNoMainCameraExists();
            auto *object = runtime.createGameObject()->setPosition({-800.0f, 30.0f});
            auto *sprite = object->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 20.0f, 20.0f)
                               ->getComponent<Sprite>();
            test_support::require(sprite != nullptr, "Could not create a direct Camera render fixture");
            platformator_test::work = {};
            runtime.getMainCamera()->render(sprite, runtime.getRenderer(), 320, 240);
            test_support::require(platformator_test::work.renderSubmittedSprites == 1,
                                  "The public four-argument Camera::render must preserve submit-all behavior");
        }
    }
}

void checkUnknownAndRetiredModesRejected()
{
    test_support::headlessEnvironment();
    for (const char *badSelector : {"indexed", "current-linear", "not-a-render-strategy"})
    {
        ScopedMode selection(badSelector);
        for (const auto [enabled, headless] :
             {std::pair{true, false}, std::pair{true, true}, std::pair{false, false}, std::pair{false, true}})
        {
            bool rejected = false;
            std::string message;
            try
            {
                platformator::Runtime runtime(makeOptions(enabled, headless));
            }
            catch (const std::invalid_argument &error)
            {
                rejected = true;
                message = error.what();
            }
            test_support::require(rejected && message.find("cached-linear") != std::string::npos,
                                  "Unknown or retired culling selectors must fail at startup with actionable guidance");
            if (std::string(badSelector) == "indexed" || std::string(badSelector) == "current-linear")
            {
                test_support::require(
                    message.find("unset PLATFORMATOR_RENDER_CULLING_MODE") != std::string::npos &&
                        message.find("or disable culling") == std::string::npos,
                    "Retired culling diagnostic must direct opt-out/headless and enabled callers to clear or select cached-linear before disabling culling");
            }
        }
    }
}

void run()
{
    checkInactiveModesDoNotCreateState();
    checkNoCameraAndDirectCameraContracts();
    checkUnknownAndRetiredModesRejected();
}
} // namespace

int main()
{
#if !PLATFORMATOR_ENABLE_WORK_PROBES
    std::cout << "[SKIP] render_culling_contract_test requires "
                 "PLATFORMATOR_ENABLE_WORK_PROBES=ON for culling state and submission observation\n";
    return 77;
#else
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
#endif
}
