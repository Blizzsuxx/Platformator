#pragma once

#include <list>
#include <string>
#include <vector>
#include "gameobject.h"

struct Scene
{
public:
    Scene(std::string filepath);
    ~Scene();
    std::string filePath;

    // Loads a simple block-based scene format.
    // Example:
    // object {
    //   name "Ball"
    //   position 100 200
    //   rigidbody { bodyType dynamic gravity true mass 10 }
    //   circleCollider { radius 25 }
    //   sprite { path "assets/ball.png" flip none size 50 50 }
    // }
    std::vector<GameObject *> loadScene();

    // Saves the current GameObjects back into the same block-based scene format
    // supported by loadScene().
    void saveScene(const std::list<GameObject *> &gameObjects) const;
};