#include <stdio.h>
#include "gamemanager.h"
#include "boxcollider.h"

int main(int argc, char *args[])
{
    GameManager &gameManager = GameManager::getInstance();

    const float wallThickness = 50.0f;
    const float wallWidth = SCREEN_WIDTH;
    const float wallHeight = SCREEN_HEIGHT;

    GameObject *ball = new GameObject();
    ball->addComponent(new CircleCollider(ball, 25.0f));
    ball->addComponent(new Rigidbody(ball));
    ball->addComponent(new Sprite(ball, "assets/ball.png", SDL_FLIP_NONE, 50, 50));
    ball->setPosition(Eigen::Vector2f(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f));
    ball->setName("Ball");
    ball->getComponent<Rigidbody>()->setVelocity(Eigen::Vector2f(200.0f, 150.0f));
    gameManager.addGameObject(ball);

    // South wall (bottom)
    GameObject *wallSouth = new GameObject();
    wallSouth->addComponent(new BoxCollider(wallSouth, wallWidth, wallThickness));
    wallSouth->addComponent(new Rigidbody(wallSouth, STATIC, false));
    wallSouth->addComponent(new Sprite(wallSouth, "assets/wall.png", SDL_FLIP_NONE, static_cast<int>(wallWidth), static_cast<int>(wallThickness)));
    wallSouth->setPosition(Eigen::Vector2f(wallWidth / 2.0f, wallHeight - wallThickness / 2.0f));
    wallSouth->setName("Wall South");
    gameManager.addGameObject(wallSouth);

    // North wall (top)
    GameObject *wallNorth = new GameObject();
    wallNorth->addComponent(new BoxCollider(wallNorth, wallWidth, wallThickness));
    wallNorth->addComponent(new Rigidbody(wallNorth, STATIC, false));
    wallNorth->addComponent(new Sprite(wallNorth, "assets/wall.png", SDL_FLIP_NONE, static_cast<int>(wallWidth), static_cast<int>(wallThickness)));
    wallNorth->setPosition(Eigen::Vector2f(wallWidth / 2.0f, wallThickness / 2.0f));
    wallNorth->setName("Wall North");
    gameManager.addGameObject(wallNorth);

    // West wall (left)
    GameObject *wallWest = new GameObject();
    wallWest->addComponent(new BoxCollider(wallWest, wallThickness, wallHeight));
    wallWest->addComponent(new Rigidbody(wallWest, STATIC, false));
    wallWest->addComponent(new Sprite(wallWest, "assets/wall.png", SDL_FLIP_NONE, static_cast<int>(wallThickness), static_cast<int>(wallHeight)));
    wallWest->setPosition(Eigen::Vector2f(wallThickness / 2.0f, wallHeight / 2.0f));
    wallWest->setName("Wall West");
    gameManager.addGameObject(wallWest);

    // East wall (right)
    GameObject *wallEast = new GameObject();
    wallEast->addComponent(new BoxCollider(wallEast, wallThickness, wallHeight));
    wallEast->addComponent(new Rigidbody(wallEast, STATIC, false));
    wallEast->addComponent(new Sprite(wallEast, "assets/wall.png", SDL_FLIP_NONE, static_cast<int>(wallThickness), static_cast<int>(wallHeight)));
    wallEast->setPosition(Eigen::Vector2f(wallWidth - wallThickness / 2.0f, wallHeight / 2.0f));
    wallEast->setName("Wall East");
    gameManager.addGameObject(wallEast);

    gameManager.loop();

    return 0;
}