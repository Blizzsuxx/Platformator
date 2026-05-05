#include "scene.h"

#include "pathmanager.h"

#include <fstream>

#include "gamemanager.h"
#include "jsonhelpers.h"
#include "gameobject.h"

namespace
{
    bool isAssetsRelativePath(const std::filesystem::path &path)
    {
        const auto component = path.begin();
        return component != path.end() && *component == "assets";
    }

    std::filesystem::path resolveSceneFilePath(const std::string &rawFilePath)
    {
        std::filesystem::path scenePath(rawFilePath);
        if (scenePath.is_absolute())
        {
            return scenePath.lexically_normal();
        }

        scenePath = scenePath.lexically_normal();
        if (isAssetsRelativePath(scenePath))
        {
            const std::filesystem::path runtimeRoot = std::filesystem::path(PathManager::getInstance().getAssetsRootAbsolutePath()).parent_path();
            return (runtimeRoot / scenePath).lexically_normal();
        }

        return (std::filesystem::current_path() / scenePath).lexically_normal();
    }
}

Scene::Scene(std::string filepath) : filePath(std::move(filepath))
{
}

Scene::~Scene() = default;

std::vector<GameObject *> Scene::loadScene()
{
    std::vector<GameObject *> gameObjects;
    const std::filesystem::path resolvedFilePath = resolveSceneFilePath(filePath);

    std::ifstream file(resolvedFilePath);
    if (!file.is_open())
    {
        printf("Failed to load %s\n", resolvedFilePath.string().c_str());
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
    const std::filesystem::path resolvedFilePath = resolveSceneFilePath(filePath);

    for (const GameObject *gameObject : gameObjects)
    {
        if (gameObject == nullptr)
        {
            continue;
        }

        json.push_back(*gameObject);
    }

    std::ofstream file(resolvedFilePath);
    if (!file.is_open())
    {
        printf("Failed to save %s\n", resolvedFilePath.string().c_str());
        return;
    }
    file << json.dump(4);
    file.close();
}