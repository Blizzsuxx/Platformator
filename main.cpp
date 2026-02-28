#include <stdio.h>
#include <SDL2/SDL_image.h>
#include "gamemanager.h"
#include "boxcollider.h"

int main(int argc, char *args[])
{
    GameManager gameManager = GameManager();
    SDL_Renderer *renderer = gameManager.getWindow()->getRenderer();

    // Load wall texture
    SDL_Texture *wallTexture = IMG_LoadTexture(renderer, "assets/wall.png");
    if (!wallTexture)
    {
        printf("Failed to load wall.png: %s\n", IMG_GetError());
    }

    const float wallThickness = 50.0f;

    // South wall (bottom)
    GameObject *wallSouth = new GameObject();
    wallSouth->addComponent(new BoxCollider(wallSouth, static_cast<float>(SCREEN_WIDTH), wallThickness));
    wallSouth->addComponent(new Rigidbody(wallSouth));
    wallSouth->addComponent(new Sprite(wallSouth, wallTexture, SDL_FLIP_NONE, SCREEN_WIDTH, static_cast<int>(wallThickness)));
    wallSouth->setPosition(Eigen::Vector2f(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT - wallThickness / 2.0f));
    wallSouth->setName("Wall South");
    gameManager.addGameObject(wallSouth);

    // North wall (top)
    GameObject *wallNorth = new GameObject();
    wallNorth->addComponent(new BoxCollider(wallNorth, static_cast<float>(SCREEN_WIDTH), wallThickness));
    wallNorth->addComponent(new Rigidbody(wallNorth));
    wallNorth->addComponent(new Sprite(wallNorth, wallTexture, SDL_FLIP_NONE, SCREEN_WIDTH, static_cast<int>(wallThickness)));
    wallNorth->setPosition(Eigen::Vector2f(SCREEN_WIDTH / 2.0f, wallThickness / 2.0f));
    wallNorth->setName("Wall North");
    gameManager.addGameObject(wallNorth);

    // West wall (left)
    GameObject *wallWest = new GameObject();
    wallWest->addComponent(new BoxCollider(wallWest, wallThickness, static_cast<float>(SCREEN_HEIGHT)));
    wallWest->addComponent(new Rigidbody(wallWest));
    wallWest->addComponent(new Sprite(wallWest, wallTexture, SDL_FLIP_NONE, static_cast<int>(wallThickness), SCREEN_HEIGHT));
    wallWest->setPosition(Eigen::Vector2f(wallThickness / 2.0f, SCREEN_HEIGHT / 2.0f));
    wallWest->setName("Wall West");
    gameManager.addGameObject(wallWest);

    // East wall (right)
    GameObject *wallEast = new GameObject();
    wallEast->addComponent(new BoxCollider(wallEast, wallThickness, static_cast<float>(SCREEN_HEIGHT)));
    wallEast->addComponent(new Rigidbody(wallEast));
    wallEast->addComponent(new Sprite(wallEast, wallTexture, SDL_FLIP_NONE, static_cast<int>(wallThickness), SCREEN_HEIGHT));
    wallEast->setPosition(Eigen::Vector2f(SCREEN_WIDTH - wallThickness / 2.0f, SCREEN_HEIGHT / 2.0f));
    wallEast->setName("Wall East");
    gameManager.addGameObject(wallEast);

    gameManager.loop();

    return 0;
}