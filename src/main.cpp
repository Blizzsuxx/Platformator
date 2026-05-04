#include <exception>
#include <iostream>
#include <string>

#include "gamemanager.h"
#include "runtimeoptions.h"

int main(int argc, char *args[])
{
    RuntimeOptions runtimeOptions = parseRuntimeOptions(argc, args, "assets/scenes/default.scene");
    GameManager::setStartupWindowSettings(runtimeOptions.windowSettings);

    GameManager &gameManager = GameManager::getInstance();
    Scene loadedScene(runtimeOptions.sceneFilePath);
    gameManager.loadScene(loadedScene);
    gameManager.loop();

    return 0;
}