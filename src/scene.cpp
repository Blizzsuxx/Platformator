#include "scene.h"
#include "camera.h"

Scene::Scene(std::string filepath) : filePath(filepath)
{
}

Scene::~Scene()
{
}

std::vector<GameObject *> Scene::loadScene()
{
    std::vector<GameObject *> objects;
    return objects;
}