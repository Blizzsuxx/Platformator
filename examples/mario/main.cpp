#include <filesystem>
#include <iostream>
#include "gamemanager.h"
#include "mario_game.h"
#include "runtimeoptions.h"
#include "scene.h"

namespace
{
    std::filesystem::path getExampleRootPath()
    {
        return std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path();
    }

    std::filesystem::path getDefaultScenePath()
    {
        return getExampleRootPath() / "level1.scene";
    }
} // namespace

int main(int argc, char *args[])
{
    std::filesystem::path scenePath = getDefaultScenePath();

    try
    {
        RuntimeOptions runtimeOptions = parseRuntimeOptions(argc, args, getDefaultScenePath().string());
        GameManager::setStartupWindowSettings(runtimeOptions.windowSettings);

        GameManager &gameManager = GameManager::getInstance();
        SDLWindow *window = gameManager.getWindow();
        scenePath = runtimeOptions.sceneFilePath;
        Scene loadedScene(scenePath.string());

        mario::MarioGame marioGame(gameManager, *window, loadedScene);
        gameManager.loadScene(loadedScene);
        marioGame.initializeScene();
        gameManager.loop();
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Failed to load Mario example scene '" << scenePath.string() << "': " << exception.what() << '\n';
        return 1;
    }

    return 0;
}