#include <stdio.h>
#include <exception>
#include <string>

#include "gamemanager.h"

int main(int argc, char *args[])
{
    GameManager &gameManager = GameManager::getInstance();
    SDLWindow *window = gameManager.getWindow();

    const std::string sceneFilePath = argc > 1 ? args[1] : "assets/scenes/default.scene";

    try
    {
        gameManager.addScene(Scene(sceneFilePath));
        Scene &loadedScene = gameManager.getScenes().back();
        gameManager.loadScene(loadedScene);

        GameObject *ball = gameManager.getGameObject("Ball");

        window->addSdlListener([&gameManager, &loadedScene, ball](SDL_Event event)
                               {
            if (event.type != SDL_EVENT_KEY_DOWN)
            {
                return;
            }

            if (event.key.key == SDLK_F7)
            {
                try
                {
                    gameManager.saveScene(loadedScene);
                    printf("Saved scene to %s\n", loadedScene.filePath.c_str());
                }
                catch (const std::exception &exception)
                {
                    printf("Failed to save scene to %s: %s\n", loadedScene.filePath.c_str(), exception.what());
                }
                return;
            }

            if (ball == nullptr)
            {
                return;
            }

            Rigidbody *rigidbody = ball->getComponent<Rigidbody>();
            if (rigidbody == nullptr)
            {
                return;
            }

            switch (event.key.key)
            {
            case SDLK_UP:
                rigidbody->setVelocity(Eigen::Vector2f(0.0f, -200.0f));
                break;
            case SDLK_DOWN:
                rigidbody->setVelocity(Eigen::Vector2f(0.0f, 200.0f));
                break;
            case SDLK_LEFT:
                rigidbody->setVelocity(Eigen::Vector2f(-200.0f, 0.0f));
                break;
            case SDLK_RIGHT:
                rigidbody->setVelocity(Eigen::Vector2f(200.0f, 0.0f));
                break;
            default:
                break;
            } });
    }
    catch (const std::exception &exception)
    {
        printf("Failed to load scene %s: %s\n", sceneFilePath.c_str(), exception.what());
        return 1;
    }

    gameManager.loop();

    return 0;
}