#include "platformator/scene.h"

#include "pathmanager.h"

#include <fstream>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#include "gamemanager.h"
#include "platformator/scriptregistration.h"
#include "platformator/gameobject.h"
#include "platformator/runtime.h"
#include "runtimeaccess.h"
#include "referenceserialization.h"

Scene::Scene(std::string filepath) : filePath(std::move(filepath))
{
}

Scene::~Scene() = default;

std::vector<GameObject *> Scene::loadScene()
{
    platformator::Runtime::current().loadScene(*this);
    std::vector<GameObject *> roots;
    for (auto *object : platformator::Runtime::current().getAllGameObjects())
        if (!object->getParent())
            roots.push_back(object);
    return roots;
}

void Scene::saveScene(const std::vector<GameObject *> &gameObjects) const
{
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    manager.assertCanMutate();
    if (!manager.isIdle())
        throw std::logic_error("Scene save requires an idle runtime");
    if (filePath.empty())
        throw std::invalid_argument("Scene save path is empty");
    GameManager::PhaseGuard saving(manager, GameManager::Phase::Saving);
    platformator_detail::SceneReferenceScope references(gameObjects);
    nlohmann::json document = nlohmann::json::array();
    for (const auto *object : gameObjects)
        document.push_back(*object);
    const auto contents = document.dump(4);
    const auto destination = resolvedFilePath();
    struct TemporaryFile
    {
        std::filesystem::path path;
        std::FILE *file = nullptr;
        ~TemporaryFile()
        {
            if (file)
                std::fclose(file);
            std::error_code ignored;
            if (!path.empty())
                std::filesystem::remove(path, ignored);
        }
    } temporary;
    static std::atomic<uint64_t> sequence{0};
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        auto candidate = destination;
        candidate += ".tmp." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        auto *file = std::fopen(candidate.string().c_str(), "wbx");
        if (file)
        {
            temporary.path = std::move(candidate);
            temporary.file = file;
            break;
        }
        if (errno != EEXIST)
            throw std::runtime_error("Cannot create scene temporary file for '" + destination.string() +
                                     "': " + std::strerror(errno));
    }
    if (!temporary.file)
        throw std::runtime_error("Cannot reserve scene temporary file for '" + destination.string() + "'");
    if (std::fwrite(contents.data(), 1, contents.size(), temporary.file) != contents.size())
        throw std::runtime_error("Cannot write scene temporary file for '" + destination.string() + "'");
    if (std::fclose(std::exchange(temporary.file, nullptr)) != 0)
        throw std::runtime_error("Cannot close scene temporary file for '" + destination.string() + "'");
    std::filesystem::rename(temporary.path, destination);
}

std::filesystem::path Scene::resolvedFilePath() const
{
    return PathManager::getInstance().resolveScenePath(filePath);
}
