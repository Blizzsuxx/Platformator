#include <filesystem>
#include <iostream>
#include "gamemanager.h"
#include "mario_game.h"
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
    GameManager &gameManager = GameManager::getInstance();
    SDLWindow *window = gameManager.getWindow();
    const std::filesystem::path scenePath = argc > 1 ? std::filesystem::path(args[1]) : getDefaultScenePath();

    try
    {
        gameManager.addScene(Scene(scenePath.string()));
        Scene &loadedScene = gameManager.getScenes().back();
        gameManager.loadScene(loadedScene);

        mario::MarioGame marioGame(gameManager, *window, loadedScene);
        gameManager.loop();
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Failed to load Mario example scene '" << scenePath.string() << "': " << exception.what() << '\n';
        return 1;
    }

    return 0;
}