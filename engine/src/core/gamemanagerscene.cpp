#include "core/gamemanager.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <stdexcept>

#include "benchmark/benchmark.h"
#include "core/objectpool.h"
#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/scene.h"
#include "platformator/scriptcomponent.h"
#include "platformator/template.h"
#include "scene/scenebuilder.h"

void GameManager::loadTemplateDocument(Template &tpl, const nlohmann::json &document)
{
    tpl.loadFromJsonDocument(document, *this);
}

// ============================================================================
// Scene & Object Management
// ============================================================================

void GameManager::loadScene(const Scene &scene)
{
    requestScene(scene.filePath);
    if (isIdle())
    {
        commitMutations();
    }
}

std::vector<GameObject *> GameManager::collectRootObjects() const
{
    std::vector<GameObject *> rootObjects;
    rootObjects.reserve(gameObjects.size());
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject->getParent() == nullptr)
        {
            rootObjects.push_back(gameObject);
        }
    }
    return rootObjects;
}

void GameManager::saveScene(const Scene &scene)
{
    assertCanMutate();
    if (!isIdle())
    {
        throw std::logic_error("Scene save requires an idle runtime");
    }
    scene.saveScene(collectRootObjects());
}

void GameManager::clearObjects()
{
    requestScene(std::nullopt);
    if (isIdle())
    {
        commitMutations();
    }
}

void GameManager::requestScene(std::optional<std::string> path)
{
    assertCanMutate();
    if (path && path->empty())
    {
        throw std::invalid_argument("Scene path is empty");
    }
    pendingSceneRequest = SceneRequest{std::move(path)};
}

void GameManager::clearAllSceneObjects(std::exception_ptr &error)
{
    // Bulk whole-scene teardown: zero live survivors exist, so skip per-collider
    // broadphase removal, exit-event sorting, and per-sprite culling updates.
    auto silenceComponent = [](Component *component) {
        component->callbackEligible = false;
        component->reconcileQueued = false;
        component->systemsRegistered = false;
        if (component->getType() == ComponentType::AUDIO)
        {
            static_cast<Audio *>(component)->stop();
        }
    };

    for (Component *component : retiredComponents)
    {
        silenceComponent(component);
    }
    for (GameObject *object : gameObjectsToDelete)
    {
        for (Component *component : object->components)
        {
            if (component)
            {
                silenceComponent(component);
            }
        }
    }

    gameObjectsToDelete.reserve(gameObjectsToDelete.size() + gameObjects.size());
    for (GameObject *object : gameObjects)
    {
        registry.retire(*object);
        object->setGameManagerIteratorIndex(SIZE_MAX);
        object->transformQueueIndex = SIZE_MAX;
        for (Component *component : object->components)
        {
            if (!component)
            {
                continue;
            }
            registry.retire(*component);
            silenceComponent(component);
            if (component->getType() == ComponentType::SCRIPT)
            {
                auto *script = static_cast<ScriptComponent *>(component);
                script->setGameManagerIndex(SIZE_MAX);
                script->fixedUpdateListIndex = SIZE_MAX;
                script->updateListIndex = SIZE_MAX;
                script->lateUpdateListIndex = SIZE_MAX;
            }
        }
        gameObjectsToDelete.push_back(object);
    }
    gameObjects.clear();
    idToObjectMap.clear();
    dirtyTransforms.clear();
    reconciliations.clear();
    reconciliationDrain.clear();
    fixedUpdateScripts.clear();
    updateScripts.clear();
    lateUpdateScripts.clear();
    for (Animator *animator : animatorComponents)
    {
        animator->setGameManagerIndex(SIZE_MAX);
    }
    animatorComponents.clear();
    for (ScriptComponent *script : pendingStartScripts)
    {
        if (script)
        {
            script->pendingStartIndex = SIZE_MAX;
        }
    }
    pendingStartScripts.clear();

    physicsManager->clearAll();
    window->clearSceneComponents();
    drainRetirements(error);
}

void GameManager::commitSceneTransition(SceneRequest selected)
{
    PLATFORMATOR_BENCH_SCOPE(SceneTransition);
    PhaseGuard commitPhase(*this, Phase::MutationCommit);
    std::exception_ptr error;

    std::unique_ptr<SceneBuildContext> prepared;
    if (selected.path)
    {
        preparingScene = true;
        try
        {
            auto candidate = std::make_unique<SceneBuildContext>(*this);
            candidate->prepare(*selected.path);
            reconciliations.reserve(std::max(reconciliations.size(), candidate->componentCount));
            gameObjectsToDelete.reserve(gameObjectsToDelete.size() + gameObjects.size());
            animatorComponents.reserve(std::max(animatorComponents.size(), candidate->animatorCount));
            prepared = std::move(candidate);
        }
        catch (...)
        {
            error = std::current_exception();
        }
        preparingScene = false;
    }

    const bool clearing = !selected.path;
    if (!clearing && !prepared)
    {
        // Preparation failed: preserve the current scene, but still drain ordinary mutations/retirements.
        reconcilePending(error);
        drainRetirements(error);
        platformator_detail::releaseEmptyObjectPools();
        if (error)
        {
            std::rethrow_exception(error);
        }
        return;
    }

    clearAllSceneObjects(error);
    platformator_detail::releaseEmptyObjectPools();

    if (clearing)
    {
        nextFileId = 0;
        currentSceneFilePath.clear();
    }

    if (prepared)
    {
        gameObjects.swap(prepared->objects);
        idToObjectMap.swap(prepared->ids);
        nextFileId = prepared->nextFileId;
        for (size_t i = 0; i < gameObjects.size(); ++i)
        {
            auto *object = gameObjects[i];
            registry.publish(*object);
            object->setGameManagerIteratorIndex(i);
            for (auto *component : object->components)
            {
                if (component)
                {
                    registry.publish(*component);
                    queueReconciliation(component);
                }
            }
        }
        currentSceneFilePath = std::move(prepared->path);
        reconcilePending(error);
    }
    if (error)
    {
        std::rethrow_exception(error);
    }
}

void GameManager::shutdown()
{
    if (shutdownComplete)
    {
        return;
    }
    shuttingDown = true;
    pendingSceneRequest.reset();
    PhaseGuard shutdownPhase(*this, Phase::Shutdown);
    std::exception_ptr error;
    clearAllSceneObjects(error);
    if (error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception &caught)
        {
            std::fprintf(stderr, "Runtime shutdown callback failed: %s\n", caught.what());
        }
        catch (...)
        {
            std::fprintf(stderr, "Runtime shutdown callback failed\n");
        }
    }
    audioManager->clearTransientAudio();
    assets.freeAll();
    platformator_detail::releaseEmptyObjectPools();
    shutdownComplete = true;
}
