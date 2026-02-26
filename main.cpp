#include <stdio.h>
#include "gamemanager.h"
#include "boxcollider.h"

int main(int argc, char *args[])
{
    GameManager gameManager = GameManager();

    GameObject *wallSouth = new GameObject();
    BoxCollider *wallSouthCollider = new BoxCollider(wallSouth, 800.0f, 50.0f);
    Rigidbody *wallSouthRigidbody = new Rigidbody(wallSouth);
    Sprite *wallSouthSprite = new Sprite(wallSouth, SDL_Texture(""));

    wallSouth->addComponent(wallSouthCollider);
    wallSouth->addComponent(wallSouthRigidbody);
    wallSouth->addComponent(wallSouthSprite);
    wallSouth->setPosition(Eigen::Vector2f(400.0f, 575.0f));
    wallSouth->setName("Wall South");

    gameManager.addGameObject(wallSouth);

    gameManager.loop();

    return 0;
}