#include <stdio.h>
#include "gamemanager.h"
#include "boxcollider.h"

int main(int argc, char *args[])
{
    GameManager &gameManager = GameManager::getInstance();
    SDLWindow *window = gameManager.getWindow();

    const float wallThickness = 50.0f;
    const float wallWidth = SCREEN_WIDTH;
    const float wallHeight = SCREEN_HEIGHT;

    GameObject *ball = gameManager
                           .createGameObject()
                           ->setName("Ball")
                           ->addComponent<Rigidbody>()
                           ->setPosition(Eigen::Vector2f(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f))
                           ->addComponent<BoxCollider>(50.0f, 50.0f)
                           ->addComponent<Sprite>("assets/ball.png", SDL_FLIP_NONE, 50, 50);

    window->addSdlListener([ball](SDL_Event event)
                           {
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            switch (event.key.key)
            {
            case SDLK_UP:
                ball->getComponent<Rigidbody>()->setVelocity(Eigen::Vector2f(0.0f, -200.0f));
                break;
            case SDLK_DOWN:
                ball->getComponent<Rigidbody>()->setVelocity(Eigen::Vector2f(0.0f, 200.0f));
                break;
            case SDLK_LEFT:
                ball->getComponent<Rigidbody>()->setVelocity(Eigen::Vector2f(-200.0f, 0.0f));
                break;
            case SDLK_RIGHT:
                ball->getComponent<Rigidbody>()->setVelocity(Eigen::Vector2f(200.0f, 0.0f));
                break;
            default:
                break;
            }
        } });

    ball
        ->getComponent<Rigidbody>()
        ->setVelocity(Eigen::Vector2f(200.0f, 150.0f))
        ->setMass(10.0f)
        ->setFriction(1.0f)
        ->setRestitution(0.0f);

    // South wall (bottom)
    GameObject *wallSouth = gameManager
                                .createGameObject()
                                ->setName("Wall South")
                                ->addComponent<Rigidbody>(STATIC, false)
                                ->addComponent<BoxCollider>(wallWidth, wallThickness)
                                ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallWidth, wallThickness)
                                ->setPosition(Eigen::Vector2f(wallWidth / 2.0f, wallHeight - wallThickness / 2.0f));

    // North wall (top)
    GameObject *wallNorth = gameManager
                                .createGameObject()
                                ->setName("Wall North")
                                ->addComponent<Rigidbody>(STATIC, false)
                                ->addComponent<BoxCollider>(wallWidth, wallThickness)
                                ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallWidth, wallThickness)
                                ->setPosition(Eigen::Vector2f(wallWidth / 2.0f, wallThickness / 2.0f));

    // West wall (left)
    GameObject *wallWest = gameManager
                               .createGameObject()
                               ->setName("Wall West")
                               ->addComponent<Rigidbody>(STATIC, false)
                               ->addComponent<BoxCollider>(wallThickness, wallHeight)
                               ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallThickness, wallHeight)
                               ->setPosition(Eigen::Vector2f(wallThickness / 2.0f, wallHeight / 2.0f));

    // East wall (right)
    GameObject *wallEast = gameManager
                               .createGameObject()
                               ->setName("Wall East")
                               ->addComponent<Rigidbody>(STATIC, false)
                               ->addComponent<BoxCollider>(wallThickness, wallHeight)
                               ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallThickness, wallHeight)
                               ->setPosition(Eigen::Vector2f(wallWidth - wallThickness / 2.0f, wallHeight / 2.0f));

    gameManager.loop();

    return 0;
}