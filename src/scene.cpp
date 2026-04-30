#include "scene.h"
#include <fstream>
#include "gamemanager.h"
#include "jsonhelpers.h"
#include "gameobject.h"

Scene::Scene(std::string filepath) : filePath(std::move(filepath))
{
}

Scene::~Scene() = default;

std::vector<GameObject *> Scene::loadScene()
{
    std::vector<GameObject *> gameObjects;

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        printf("Failed to load %s\n", filePath.c_str());
        return gameObjects;
    }
    nlohmann::json json = nlohmann::json::parse(file);
    file.close();

    for (const auto &gameObjectJson : json)
    {
        if (gameObjectJson.is_null())
        {
            continue;
        }

        GameObject *gameObject = new GameObject();
        gameObjectJson.get_to(*gameObject);
        gameObjects.push_back(gameObject);
    }

    return gameObjects;
}

void Scene::saveScene(const std::vector<GameObject *> &gameObjects) const
{
    nlohmann::json json = nlohmann::json::array();

    for (const GameObject *gameObject : gameObjects)
    {
        if (gameObject == nullptr)
        {
            continue;
        }

        json.push_back(*gameObject);
    }

    std::ofstream file(filePath);
    if (!file.is_open())
    {
        printf("Failed to save %s\n", filePath.c_str());
        return;
    }
    file << json.dump(4);
    file.close();
}