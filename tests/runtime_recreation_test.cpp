#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <json.hpp>

#include "platformator/runtime.h"

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

    void runRuntimePass(const std::filesystem::path &scenePath, const std::filesystem::path &savedScenePath, const std::string &name, int objectId)
    {
        platformator::Runtime runtime;
        require(runtime.getWindowHandle() != nullptr, "Runtime recreation test expected a live SDL window handle.");

        runtime.loadScene(scenePath.string());

        GameObject *gameObject = runtime.getGameObject(name);
        require(gameObject != nullptr, "Runtime recreation test failed to load the named GameObject through Runtime.");
        require(runtime.getObjectById(objectId) != nullptr,
                "Runtime recreation test failed to resolve the loaded GameObject by id through Runtime.");

        runtime.saveScene(savedScenePath.string());
        require(std::filesystem::exists(savedScenePath), "Runtime recreation test failed to save the scene through Runtime.");
    }
} // namespace

int main()
{
    configureHeadlessEnvironment();

    const std::filesystem::path scenePathA = std::filesystem::current_path() / "runtime_recreation_a.scene";
    const std::filesystem::path scenePathB = std::filesystem::current_path() / "runtime_recreation_b.scene";
    const std::filesystem::path savedScenePathA = std::filesystem::current_path() / "runtime_recreation_saved_a.scene";
    const std::filesystem::path savedScenePathB = std::filesystem::current_path() / "runtime_recreation_saved_b.scene";

    auto cleanup = [&]()
    {
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

        runRuntimePass(scenePathA, savedScenePathA, "Runtime A", 4100);
        runRuntimePass(scenePathB, savedScenePathB, "Runtime B", 4200);

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