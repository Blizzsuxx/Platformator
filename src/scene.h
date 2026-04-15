#pragma once

#include <string>
#include <vector>
#include "gameobject.h"

struct Scene
{
public:
    Scene(std::string filepath);
    ~Scene();
    std::string filePath;

    std::vector<GameObject *> loadScene();
};