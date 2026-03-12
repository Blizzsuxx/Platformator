#include <stdio.h>
#include "gamemanager.h"
#include "boxcollider.h"

int main(int argc, char *args[])
{
    GameManager &gameManager = GameManager::getInstance();

    const float wallThickness = 50.0f;
    const float wallWidth = SCREEN_WIDTH;
    const float wallHeight = SCREEN_HEIGHT;

    GameObject *ball = gameManager
                           .createGameObject()
                           ->addComponent<CircleCollider>(25.0f)
                           ->addComponent<Rigidbody>()
                           ->addComponent<Sprite>("assets/ball.png", SDL_FLIP_NONE, 50, 50)
                           ->setPosition(Eigen::Vector2f(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f))
                           ->setName("Ball");
    ball
        ->getComponent<Rigidbody>()
        ->setVelocity(Eigen::Vector2f(200.0f, 150.0f));

    // South wall (bottom)
    GameObject *wallSouth = gameManager
                                .createGameObject()
                                ->addComponent<BoxCollider>(wallWidth, wallThickness)
                                ->addComponent<Rigidbody>(STATIC, false)
                                ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallWidth, wallThickness)
                                ->setPosition(Eigen::Vector2f(wallWidth / 2.0f, wallHeight - wallThickness / 2.0f))
                                ->setName("Wall South");

    // North wall (top)
    GameObject *wallNorth = gameManager
                                .createGameObject()
                                ->addComponent<BoxCollider>(wallWidth, wallThickness)
                                ->addComponent<Rigidbody>(STATIC, false)
                                ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallWidth, wallThickness)
                                ->setPosition(Eigen::Vector2f(wallWidth / 2.0f, wallThickness / 2.0f))
                                ->setName("Wall North");

    // West wall (left)
    GameObject *wallWest = gameManager
                               .createGameObject()
                               ->addComponent<BoxCollider>(wallThickness, wallHeight)
                               ->addComponent<Rigidbody>(STATIC, false)
                               ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallThickness, wallHeight)
                               ->setPosition(Eigen::Vector2f(wallThickness / 2.0f, wallHeight / 2.0f))
                               ->setName("Wall West");

    // East wall (right)
    GameObject *wallEast = gameManager
                               .createGameObject()
                               ->addComponent<BoxCollider>(wallThickness, wallHeight)
                               ->addComponent<Rigidbody>(STATIC, false)
                               ->addComponent<Sprite>("assets/wall.png", SDL_FLIP_NONE, wallThickness, wallHeight)
                               ->setPosition(Eigen::Vector2f(wallWidth - wallThickness / 2.0f, wallHeight / 2.0f))
                               ->setName("Wall East");

    gameManager.loop();

    return 0;
}