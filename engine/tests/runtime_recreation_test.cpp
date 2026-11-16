#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <json.hpp>

#include "runtime_work_probe.h"
#include "platformator/camera.h"
#include "platformator/runtime.h"
#include "platformator/sprite.h"

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void configureHeadlessEnvironment()
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
}

void writeSceneFile(const std::filesystem::path &path, const std::string &name, int objectId)
{
    nlohmann::json gameObject = {
        {"id", objectId},
        {"name", name},
        {"tag", ""},
        {"rotation", 0.0f},
        {"active", true},
        {"position", {{"x", 10.0f}, {"y", 20.0f}}},
        {"scale", {{"x", 1.0f}, {"y", 1.0f}}},
        {"components", nlohmann::json::array()},
        {"children", nlohmann::json::array()},
    };

    std::ofstream file(path);
    require(file.is_open(), "Runtime recreation test failed to open the temporary scene file.");
    file << nlohmann::json::array({gameObject}).dump(4);
}

uint64_t runRuntimePass(const std::filesystem::path &scenePath, const std::filesystem::path &savedScenePath,
                        const std::string &name, int objectId)
{
    platformator::Runtime runtime;
    require(&platformator::Runtime::current() == &runtime,
            "Current access must bind to the newly constructed runtime");
    require(runtime.getWindowHandle() != nullptr,
            "Runtime recreation test expected a live SDL window handle.");

    runtime.loadScene(scenePath.string());

    GameObject *gameObject = runtime.getGameObject(name);
    require(gameObject != nullptr,
            "Runtime recreation test failed to load the named GameObject through Runtime.");
    require(runtime.getObjectById(objectId) != nullptr,
            "Runtime recreation test failed to resolve the loaded GameObject by id through Runtime.");

    runtime.saveScene(savedScenePath.string());
    require(std::filesystem::exists(savedScenePath),
            "Runtime recreation test failed to save the scene through Runtime.");
    return runtime.getRuntimeSerial();
}
} // namespace

int main()
{
    configureHeadlessEnvironment();

    const std::filesystem::path scenePathA = std::filesystem::current_path() / "runtime_recreation_a.scene";
    const std::filesystem::path scenePathB = std::filesystem::current_path() / "runtime_recreation_b.scene";
    const std::filesystem::path savedScenePathA =
        std::filesystem::current_path() / "runtime_recreation_saved_a.scene";
    const std::filesystem::path savedScenePathB =
        std::filesystem::current_path() / "runtime_recreation_saved_b.scene";

    auto cleanup = [&]() {
        std::error_code errorCode;
        std::filesystem::remove(scenePathA, errorCode);
        std::filesystem::remove(scenePathB, errorCode);
        std::filesystem::remove(savedScenePathA, errorCode);
        std::filesystem::remove(savedScenePathB, errorCode);
    };

    cleanup();

    try
    {
        writeSceneFile(scenePathA, "Runtime A", 4100);
        writeSceneFile(scenePathB, "Runtime B", 4200);

        require(platformator::Runtime::tryGetCurrent() == nullptr, "No runtime is published before creation");
        const auto firstSerial = runRuntimePass(scenePathA, savedScenePathA, "Runtime A", 4100);
        require(platformator::Runtime::tryGetCurrent() == nullptr, "Shutdown clears current access");
        const auto secondSerial = runRuntimePass(scenePathB, savedScenePathB, "Runtime B", 4200);
        require(firstSerial != secondSerial && platformator::Runtime::tryGetCurrent() == nullptr,
                "Recreation publishes a distinct identity lifetime and clears access again");

        const auto assetRoot = std::filesystem::current_path() / "runtime_recreation_assets";
        std::filesystem::create_directories(assetRoot / "textures");
        std::filesystem::copy_file(std::filesystem::path(SDL_GetBasePath()) / "assets/textures/missing.png",
                                   assetRoot / "textures/recreated.png", std::filesystem::copy_options::overwrite_existing);
        RuntimeOptions assetOptions;
        assetOptions.assetRoot = assetRoot.string();
        {
            platformator::Runtime first(assetOptions);
            require(first.loadTexture("assets/textures/recreated.png") != nullptr, "Initial asset must load");
            std::filesystem::remove(assetRoot / "textures/recreated.png");
            require(first.loadTexture("assets/textures/recreated.png") != nullptr,
                    "Retained cached request survives file deletion");
        }
        {
            platformator::Runtime second(assetOptions);
            bool rejected = false;
            try { second.loadTexture("assets/textures/recreated.png"); }
            catch (const std::exception &) { rejected = true; }
            require(rejected, "A new runtime cannot reuse a previous runtime's asset alias");
        }
        std::filesystem::remove_all(assetRoot);

        RuntimeOptions renderOptions;
        renderOptions.windowSettings.width = 320;
        renderOptions.windowSettings.height = 240;
        uint64_t renderRuntimeSerial = 0;
        {
            platformator_test::work = {};
            platformator::Runtime runtime(renderOptions);
            renderRuntimeSerial = runtime.getRuntimeSerial();
            auto *cameraObject = runtime.createGameObject();
            cameraObject->addComponent<Camera>(320.0f, 240.0f);
            auto *spriteObject = runtime.createGameObject()->setPosition({70.0f, 80.0f});
            auto *texture = runtime.loadTexture("assets/textures/black_square.png");
            spriteObject->addComponent<Sprite>(texture, SDL_FLIP_NONE, 32.0f, 28.0f);
            require(runtime.simulateAndRenderFrame(1.0 / 120.0),
                    "Render-culling runtime recreation fixture did not render");
#if PLATFORMATOR_ENABLE_WORK_PROBES
            require(platformator_test::work.renderCullingOwnedRecords == 1 &&
                        platformator_test::work.renderCullingOwnedBytes > 0,
                    "First runtime did not prepare its renderer-owned culling cache");
#endif
            runtime.clearScene();
#if PLATFORMATOR_ENABLE_WORK_PROBES
            require(platformator_test::work.renderCullingOwnedRecords == 0 &&
                        platformator_test::work.renderCullingOwnedBytes == 0,
                    "Clearing a runtime scene must release renderer cache capacity");
#endif
        }
        {
            platformator_test::work = {};
            platformator::Runtime runtime(renderOptions);
            require(runtime.getRuntimeSerial() != renderRuntimeSerial,
                    "A recreated runtime must have a new identity for renderer cache handles");
            auto *cameraObject = runtime.createGameObject();
            cameraObject->addComponent<Camera>(320.0f, 240.0f);
            auto *spriteObject = runtime.createGameObject()->setPosition({70.0f, 80.0f});
            auto *texture = runtime.loadTexture("assets/textures/black_square.png");
            spriteObject->addComponent<Sprite>(texture, SDL_FLIP_NONE, 32.0f, 28.0f);
            require(runtime.simulateAndRenderFrame(1.0 / 120.0),
                    "Recreated render-culling runtime did not render");
#if PLATFORMATOR_ENABLE_WORK_PROBES
            require(platformator_test::work.renderCullingOwnedRecords == 1 &&
                        platformator_test::work.renderCullingOwnedBytes > 0,
                    "Recreated runtime inherited an empty or retired render cache");
#endif
        }

        cleanup();
        std::cout << "[PASS] runtime_recreation_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        cleanup();
        std::cerr << "[FAIL] runtime_recreation_test: " << exception.what() << '\n';
        return 1;
    }
}
