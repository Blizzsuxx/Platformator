#pragma once

#include <string>
#include <vector>

class GameObject;

class SceneFileWriter
{
public:
    explicit SceneFileWriter(std::string scenePath);

    void write(const std::vector<GameObject *> &gameObjects) const;

private:
    std::string scenePath;
};