#include <stdio.h>
#include <exception>
#include <string>

#include "gamemanager.h"

int main(int argc, char *args[])
{
    GameManager &gameManager = GameManager::getInstance();
    SDLWindow *window = gameManager.getWindow();

    const std::string sceneFilePath = argc > 1 ? args[1] : "assets/scenes/default.scene";

    Scene loadedScene = Scene(sceneFilePath);
    gameManager.loadScene(loadedScene);

    gameManager.loop();

    return 0;
}