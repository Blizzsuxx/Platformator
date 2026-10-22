#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include "platformator/gameobject.h"

class Scene
{
  public:
    explicit Scene(std::string filepath);
    ~Scene();

    std::string filePath;

    std::vector<GameObject *> loadScene();
    std::filesystem::path resolvedFilePath() const;
    void saveScene(const std::vector<GameObject *> &gameObjects) const;
};
