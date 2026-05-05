#include <iostream>
#include "gamemanager.h"
#include "mario_game.h"
#include "runtimeoptions.h"
#include "scene.h"

int main(int argc, char *args[])
{
    RuntimeOptions runtimeOptions = parseRuntimeOptions(argc, args, "assets/scenes/mario_example.scene");
    GameManager::setStartupWindowSettings(runtimeOptions.windowSettings);
    GameManager::setStartupDebugSettings(runtimeOptions.debugSettings);

    GameManager &gameManager = GameManager::getInstance();
    SDLWindow *window = gameManager.getWindow();
    Scene loadedScene(runtimeOptions.sceneFilePath);

    gameManager.loadScene(loadedScene);
    gameManager.loop();

    return 0;
}