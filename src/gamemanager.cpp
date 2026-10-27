#include "gamemanager.h"
#include "simulationclock.h"
#include "workprobe.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "benchmark.h"
#include "constants.h"
#include "pathmanager.h"
#include "platformator/animationclip.h"
#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/scriptcomponent.h"
#include "platformator/texturewrapper.h"
#include "referenceresolver.h"
#include "scenebuilder.h"
#include "scenecomponentio.h"
#include <limits>

GameManager::GameManager(const WindowSettings &windowSettings, const DebugSettings &debugSettings)
    : window(std::make_unique<SDLWindow>(windowSettings, debugSettings)),
      physicsManager(std::make_unique<PhysicsManager>())
{
    PLATFORMATOR_BENCH_RESET();
}

GameManager::~GameManager()
{
    shutdown();
    physicsManager.reset();
    window.reset();
}

void GameManager::loadScene(Scene &scene)
{
    requestScene(scene.filePath);
    flushIfIdle();
}

void GameManager::saveScene(const Scene &scene)
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

    scene.saveScene(rootObjects);
}

void GameManager::createMainCameraIfNoMainCameraExists()
{
    assertCanMutate();
    if (window->getMainCamera())
    {
        return;
    }
    createGameObject()->setName("Main Camera")->addComponent<Camera>();
}

void GameManager::registerGameObjectSubtree(GameObject *gameObject)
{
    if (gameObject == nullptr || gameObject->getIsRegisteredInGameManager())
    {
        return;
    }

    assignFileId(*gameObject);
    for (auto *component : gameObject->components)
    {
        if (component)
        {
            assignFileId(*component);
        }
    }
    gameObject->setGameManagerIteratorIndex(gameObjects.size());
    gameObjects.push_back(gameObject);
    gameObject->setIsRegisteredInGameManager(true);

    registry.add(*gameObject, platformator_detail::ObjectKind::GameObject);
    idToObjectMap[gameObject->getId()] = gameObject;

    gameObject->addComponentsToGameManager();
    if (gameObject->parent || !gameObject->children.empty())
    {
        hierarchyDirty = true;
    }
    else
    {
        gameObject->hierarchyOrder = SIZE_MAX;
    }
    queueTransform(gameObject);

    for (GameObject *child : gameObject->getChildren())
    {
        registerGameObjectSubtree(child);
    }
}

void GameManager::removeGameObject(GameObject *gameObject)
{
    if (gameObject->getIsMarkedForDeletion() || !gameObject->getIsRegisteredInGameManager())
    {
        return;
    }

    while (!gameObject->getChildren().empty())
    {
        removeGameObject(gameObject->getChildren().back());
    }

    if (gameObject->getParent() != nullptr)
    {
        gameObject->detachFromParent();
    }

    size_t lastIndex = gameObjects.size() - 1;
    size_t removeIndex = gameObject->gameManagerIteratorIndex;
    if (removeIndex != lastIndex)
    {
        GameObject *movedGameObject = gameObjects.back();
        gameObjects[removeIndex] = movedGameObject;
        movedGameObject->setGameManagerIteratorIndex(removeIndex);
    }
    gameObjects.pop_back();
    cancelTransform(gameObject);
    idToObjectMap.erase(gameObject->getId());
    gameObject->setGameManagerIteratorIndex(SIZE_MAX);

    startDeletingGameObject(gameObject);
}

void GameManager::startDeletingGameObject(GameObject *gameObject)
{
    gameObject->setIsMarkedForDeletion(true);
    registry.retire(*gameObject);
    for (Component *component : gameObject->components)
    {
        if (!component)
        {
            continue;
        }
        registry.retire(*component);
        idToObjectMap.erase(component->getId());
        queueReconciliation(component);
    }
    gameObject->setIsRegisteredInGameManager(false);
    gameObjectsToDelete.push_back(gameObject);
}

GameObject *GameManager::getGameObject(std::string name)
{
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject->getName() == name)
        {
            return gameObject;
        }
    }

    return nullptr;
}

const std::vector<GameObject *> &GameManager::getGameObjects() const
{
    return gameObjects;
}

PhysicsManager *GameManager::getPhysicsManager() const
{
    return physicsManager.get();
}

SDLWindow *GameManager::getWindow() const
{
    return window.get();
}

void GameManager::runSimulationStep(const double timeDelta)
{
    runSimulationStepWithCustomCallback({}, timeDelta);
}

void GameManager::runSimulationStepWithCustomCallback(const std::function<void(double)> &customCallback,
                                                      double timeDelta)
{
    if (!isIdle() || std::this_thread::get_id() != mainThread)
    {
        throw std::logic_error("Simulation steps require an idle runtime on its main thread");
    }
    if (!std::isfinite(timeDelta) || timeDelta <= 0)
    {
        throw std::invalid_argument("Simulation timestep must be finite and positive");
    }

    auto stage = [&](Phase next, auto &&work) {
        PhaseGuard guard(*this, next);
        work();
    };

    std::exception_ptr error;
    PLATFORMATOR_BENCH_FRAME_BEGIN();
    try
    {
        stage(Phase::Starting, [&] {
            startPendingScripts();
        });
        stage(Phase::StepHook, [&] {
            if (customCallback)
            {
                customCallback(timeDelta);
            }
        });
        stage(Phase::FixedUpdate, [&] {
            fixedUpdateScriptComponents(timeDelta);
        });
        stage(Phase::Physics, [&] {
            PLATFORMATOR_BENCH_SCOPE(Physics);
            synchronizeTransforms();
            physicsManager->checkForCollisions();
            physicsManager->applyPhysics(timeDelta);
            physicsManager->resolveCollisions(timeDelta);
            physicsManager->applyMovement(timeDelta);
            synchronizeTransforms();
        });
        stage(Phase::CollisionDispatch, [&] {
            physicsManager->handlePendingPhysicsEvents(timeDelta);
        });
        stage(Phase::Update, [&] {
            updateScriptComponents(timeDelta);
        });
        stage(Phase::Animation, [&] {
            updateAnimatorComponents(timeDelta);
        });
        stage(Phase::LateUpdate, [&] {
            lateUpdateScriptComponents(timeDelta);
        });
    }
    catch (...)
    {
        error = std::current_exception();
    }

    try
    {
        PLATFORMATOR_BENCH_SCOPE(Cleanup);
        window->updateTransientAudio();
        commitMutations();
    }
    catch (...)
    {
        if (!error)
        {
            error = std::current_exception();
        }
    }

    PLATFORMATOR_BENCH_SET_COUNTER(ObjectCount, gameObjects.size());
    PLATFORMATOR_BENCH_FRAME_END();

    if (error)
    {
        std::rethrow_exception(error);
    }
}

void GameManager::loop()
{
    assertCanMutate();
    if (!isIdle())
    {
        throw std::logic_error("Interactive run requires an idle runtime");
    }
    platformator_detail::SimulationClock clock(FIXED_TIME_STEP, MAXIMUM_CATCH_UP_STEPS);
    auto previous = std::chrono::steady_clock::now();
    while (window->isRunning())
    {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        window->handleSDLEvents(elapsed);
        if (!window->isRunning())
        {
            break;
        }
        const bool paused = window->shouldUseFixedStepDelta();
        const auto steps = clock.advance(elapsed, paused, paused && window->shouldSimulateFrame());
        for (unsigned i = 0; i < steps && window->isRunning(); ++i)
        {
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
            window->clearDebugObjects();
#endif
            runSimulationStep(FIXED_TIME_STEP);
        }
        window->clearAdvanceFrameRequest();
        if (!window->isRunning())
        {
            break;
        }
        {
            PhaseGuard renderPhase(*this, Phase::Rendering);
            window->render();
        }
        std::this_thread::sleep_until(now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                std::chrono::duration<double>(FRAME_TIME)));
    }
}

void GameManager::simulateFrame(double timeDelta)
{
    runSimulationStep(timeDelta);
}

void GameManager::simulateFrameWithCustomCallback(const std::function<void(double)> &customCallback,
                                                  double timeDelta)
{
    runSimulationStepWithCustomCallback(customCallback, timeDelta);
}

bool GameManager::simulateAndRenderFrame(double timeDelta)
{
    return simulateAndRenderFrameWithCustomCallback({}, timeDelta);
}

bool GameManager::simulateAndRenderFrameWithCustomCallback(const std::function<void(double)> &customCallback,
                                                           double timeDelta)
{
    if (!window->isRunning())
    {
        return false;
    }

    window->handleSDLEvents(timeDelta);
    if (!window->isRunning())
    {
        return false;
    }

    if (window->shouldSimulateFrame())
    {
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        window->clearDebugObjects();
#endif
        runSimulationStepWithCustomCallback(customCallback, timeDelta);
    }
    PhaseGuard renderPhase(*this, Phase::Rendering);
    window->render();
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    window->clearAdvanceFrameRequest();
#endif
    return window->isRunning();
}

TextureWrapper *GameManager::loadTexture(const std::string &filePath, bool required)
{
    if (filePath.empty())
    {
        if (required)
        {
            throw std::invalid_argument("Required texture path is empty");
        }
        return nullptr;
    }
    const auto resolved =
        PathManager::getInstance().resolveAssetPath(filePath, AssetPathType::Texture, required);
    if (required && resolved.usedFallback)
    {
        throw std::runtime_error("Required texture not found: " + filePath);
    }
    if (auto found = textureCache.find(resolved.canonicalPath); found != textureCache.end())
    {
        return found->second.get();
    }
    if (!window->getRenderer())
    {
        throw std::runtime_error("Textures require a video renderer: " + filePath);
    }
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
        IMG_LoadTexture(window->getRenderer(), resolved.absolutePath.c_str()), SDL_DestroyTexture);
    if (!texture)
    {
        if (required)
        {
            throw std::runtime_error("Texture '" + filePath + "': " + SDL_GetError());
        }
        return nullptr;
    }
    auto wrapper = std::make_unique<TextureWrapper>(texture.get(), resolved.canonicalPath);
    texture.release();
    wrapper->runtimeSerial = registry.serial();
    auto *result = wrapper.get();
    textureCache.emplace(resolved.canonicalPath, std::move(wrapper));
    return result;
}

void GameManager::freeTexture(TextureWrapper *textureWrapper)
{
    PLATFORMATOR_LOG("Freeing texture: %s\n", textureWrapper->getFilePath().c_str());

    auto it = textureCache.find(textureWrapper->getFilePath());
    if (it != textureCache.end())
    {
        textureCache.erase(it);
    }
}

void GameManager::freeAllTextures()
{
    textureCache.clear();
}

GameObject *GameManager::createGameObject()
{
    assertCanMutate();
    auto cleanup = [](GameObject *object) {
        delete object;
    };
    std::unique_ptr<GameObject, decltype(cleanup)> gameObject(new GameObject(), cleanup);
    registerGameObjectSubtree(gameObject.get());
    auto *result = gameObject.release();
    flushIfIdle();
    return result;
}

void GameManager::destroyGameObject(GameObject *gameObject)
{
    assertCanMutate();
    if (!gameObject)
    {
        throw std::invalid_argument("Cannot destroy a null GameObject");
    }
    if (gameObject->getHandle().runtimeSerial != registry.serial())
    {
        throw std::invalid_argument("GameObject belongs to a different runtime");
    }
    removeGameObject(gameObject);
    flushIfIdle();
}

void GameManager::notifyComponentAdded(Component *component)
{
    if (!component->getHandle())
    {
        using enum platformator_detail::ObjectKind;
        static constexpr platformator_detail::ObjectKind kinds[] = {
            Animator, Audio, Camera, Collider, Rigidbody, ScriptComponent, Sprite};
        auto kind = kinds[component->getType()];
        if (kind == Collider)
        {
            kind = static_cast<::Collider *>(component)->getColliderType() == ColliderType::BoxCollider
                       ? BoxCollider
                       : CircleCollider;
        }
        registry.add(*component, kind);
        idToObjectMap[component->getId()] = component;
    }
    queueReconciliation(component);
}

void GameManager::notifyRuntimeOfComponentAdded(Component *component)
{
    if (component->getType() == ComponentType::ANIMATOR)
    {
        auto *animator = static_cast<Animator *>(component);
        animator->setGameManagerIndex(animatorComponents.size());
        animatorComponents.push_back(animator);
    }
    else if (component->getType() == ComponentType::SCRIPT)
    {
        auto *script = static_cast<ScriptComponent *>(component);
        script->setGameManagerIndex(scriptComponents.size());
        scriptComponents.push_back(script);
        queueScriptStart(script);
    }
    else if (component->getType() == ComponentType::AUDIO)
    {
        auto *audio = static_cast<Audio *>(component);
        if (audio->getAutoPlay())
        {
            audio->play();
        }
    }
}

void GameManager::notifyRuntimeOfComponentRemoved(Component *component)
{
    if (component == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::ANIMATOR)
    {
        Animator *animator = static_cast<Animator *>(component);
        size_t removeIndex = animator->getGameManagerIndex();

        if (removeIndex == SIZE_MAX)
        {
            return;
        }

        size_t lastIndex = animatorComponents.size() - 1;

        if (removeIndex != lastIndex)
        {
            Animator *movedAnimator = animatorComponents.back();
            animatorComponents[removeIndex] = movedAnimator;
            movedAnimator->setGameManagerIndex(removeIndex);
        }

        animatorComponents.pop_back();
        animator->setGameManagerIndex(SIZE_MAX);
    }
    else if (component->getType() == ComponentType::SCRIPT)
    {
        ScriptComponent *scriptComponent = static_cast<ScriptComponent *>(component);
        size_t removeIndex = scriptComponent->getGameManagerIndex();

        if (removeIndex == SIZE_MAX)
        {
            return;
        }

        size_t lastIndex = scriptComponents.size() - 1;

        if (removeIndex != lastIndex)
        {
            ScriptComponent *movedScriptComponent = scriptComponents.back();
            scriptComponents[removeIndex] = movedScriptComponent;
            movedScriptComponent->setGameManagerIndex(removeIndex);
        }

        scriptComponents.pop_back();
        scriptComponent->setGameManagerIndex(SIZE_MAX);
    }
    else if (component->getType() == ComponentType::AUDIO)
    {
        static_cast<Audio *>(component)->stop();
    }
}

void GameManager::notifyPhysicsManagerOfComponentAdded(Component *component)
{
    if (component == nullptr || physicsManager == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::COLLIDER)
    {
        physicsManager->addColliderComponent((Collider *)component);
    }
    else if (component->getType() == ComponentType::RIGID_BODY)
    {
        physicsManager->addRigidBodyComponent((Rigidbody *)component);
    }
}

void GameManager::notifyWindowOfComponentAdded(Component *component)
{
    if (component == nullptr || window == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::SPRITE)
    {
        window->addSpriteComponent((Sprite *)component);
    }

    if (component->getType() == ComponentType::CAMERA)
    {
        window->setMainCamera((Camera *)component);
    }
}

void GameManager::notifyPhysicsManagerOfComponentRemoved(Component *component)
{
    if (component == nullptr || physicsManager == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::COLLIDER)
    {
        physicsManager->removeColliderComponent((Collider *)component);
    }
    else if (component->getType() == ComponentType::RIGID_BODY)
    {
        physicsManager->removeRigidBodyComponent((Rigidbody *)component);
    }
}

void GameManager::notifyWindowOfComponentRemoved(Component *component)
{
    if (component == nullptr || window == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::SPRITE)
    {
        window->removeSpriteComponent((Sprite *)component);
    }

    if (component->getType() == ComponentType::CAMERA && window->getMainCamera() == (Camera *)component)
    {
        window->setMainCamera(nullptr);
    }
}

void GameManager::fixedUpdateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : scriptComponents)
    {
        if (!scriptComponent->canDispatch())
        {
            continue;
        }

        scriptComponent->fixedUpdate(timeDelta);
    }
}

void GameManager::updateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : scriptComponents)
    {
        if (!scriptComponent->canDispatch())
        {
            continue;
        }

        scriptComponent->update(timeDelta);
    }
}

void GameManager::lateUpdateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : scriptComponents)
    {
        if (!scriptComponent->canDispatch())
        {
            continue;
        }

        scriptComponent->lateUpdate(timeDelta);
    }
}

void GameManager::updateAnimatorComponents(double timeDelta)
{
    for (Animator *animator : animatorComponents)
    {
        if (!animator->getIsCallbackEligible() || animator->isRetired() ||
            !animator->getGameObject()->getActive())
        {
            continue;
        }

        animator->update(timeDelta);
    }
}

void GameManager::startPendingScripts()
{
    if (pendingStartScripts.empty())
    {
        return;
    }
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    // Callbacks can append or cancel script entries, so retain no queue iterators.
    for (size_t i = 0; i < pendingStartScripts.size(); ++i)
    {
        auto *script = pendingStartScripts[i];
        if (!script) // Canceled queue entry.
        {
            continue;
        }
        script->startPendingBehaviors(resolver);
        cancelScriptStart(script);
    }
    pendingStartScripts.clear();
}

AudioWrapper *GameManager::loadAudio(const std::string &filePath, bool required)
{
    if (filePath.empty())
    {
        if (required)
        {
            throw std::invalid_argument("Required audio path is empty");
        }
        return nullptr;
    }
    const auto resolved =
        PathManager::getInstance().resolveAssetPath(filePath, AssetPathType::Audio, required);
    if (required && resolved.usedFallback)
    {
        throw std::runtime_error("Required audio not found: " + filePath);
    }
    if (auto found = audioCache.find(resolved.canonicalPath); found != audioCache.end())
    {
        return found->second.get();
    }
    if (!window->getMixer())
    {
        throw std::runtime_error("Audio assets require an audio backend: " + filePath);
    }
    std::unique_ptr<MIX_Audio, decltype(&MIX_DestroyAudio)> audio(
        MIX_LoadAudio(window->getMixer(), resolved.absolutePath.c_str(), true), MIX_DestroyAudio);
    if (!audio)
    {
        if (required)
        {
            throw std::runtime_error("Audio '" + filePath + "': " + SDL_GetError());
        }
        return nullptr;
    }
    auto wrapper = std::make_unique<AudioWrapper>(audio.get(), resolved.canonicalPath);
    audio.release();
    wrapper->runtimeSerial = registry.serial();
    auto *result = wrapper.get();
    audioCache.emplace(resolved.canonicalPath, std::move(wrapper));
    return result;
}

void GameManager::freeAudio(AudioWrapper *audioWrapper)
{
    PLATFORMATOR_LOG("Freeing audio: %s\n", audioWrapper->getFilePath().c_str());
    auto it = audioCache.find(audioWrapper->getFilePath());
    if (it != audioCache.end())
    {
        audioCache.erase(it);
    }
}

void GameManager::freeAllAudio()
{
    audioCache.clear();
}

AnimationClip *GameManager::loadAnimationClip(const std::string &filePath)
{
    platformator_detail::ActiveReferenceResolver resolver(platformator::Runtime::current());
    return loadAnimationClip(filePath, resolver, false);
}

void GameManager::freeAnimationClip(AnimationClip *animationClip)
{
    PLATFORMATOR_LOG("Freeing animation clip: %s\n", animationClip->getFilePath().c_str());

    auto it = animationClipCache.find(animationClip->getFilePath());
    if (it != animationClipCache.end())
    {
        animationClipCache.erase(it);
    }
}

void GameManager::freeAllAnimationClips()
{
    animationClipCache.clear();
}

void GameManager::queueScriptStart(ScriptComponent *script)
{
    if (script->pendingBehaviors.empty() || script->pendingStartIndex != SIZE_MAX)
    {
        return;
    }
    pendingStartScripts.push_back(script);
    script->pendingStartIndex = pendingStartScripts.size() - 1;
}

void GameManager::cancelScriptStart(ScriptComponent *script)
{
    const auto index = script->pendingStartIndex;
    if (index == SIZE_MAX)
    {
        return;
    }
    pendingStartScripts[index] = nullptr;
    script->pendingStartIndex = SIZE_MAX;
}

const Eigen::Vector2f &GameManager::getGravityVector() const
{
    return physicsManager->getGravityVector();
}

const Eigen::Vector2f &GameManager::getGravityVectorNormalized() const
{
    return physicsManager->getGravityVectorNormalized();
}

BaseObject *GameManager::getObjectById(int id) const
{
    auto it = idToObjectMap.find(id);
    if (it != idToObjectMap.end())
    {
        return it->second;
    }

    return nullptr;
}

void GameManager::assertCanMutate() const
{
    if (std::this_thread::get_id() != mainThread)
    {
        throw std::logic_error("Structural mutation is main-thread-only");
    }
    if (shuttingDown || preparingScene || phase == Phase::DestructionCallbacks || phase == Phase::Saving)
    {
        throw std::logic_error(
            "Structural mutation is forbidden during preparation, saving, onDestroy, or shutdown");
    }
}

BaseObject *GameManager::resolveHandle(platformator::ObjectHandle handle,
                                       platformator_detail::ObjectKind kind) const
{
    return registry.get(handle, kind);
}

void GameManager::queueReconciliation(Component *component)
{
    component->callbackEligible = false;
    // Lifecycle reconciliation also updates bodies that are not yet physically registered.
    if (component->getType() == RIGID_BODY)
    {
        physicsManager->queueBodyParticipationChange(static_cast<Rigidbody *>(component));
    }
    if (component->reconcileQueued)
    {
        return;
    }
    if (component->getType() == ComponentType::SCRIPT)
    {
        cancelScriptStart(static_cast<ScriptComponent *>(component));
    }
    component->reconcileQueued = true;
    reconciliations.push_back(component);
}

void GameManager::retireComponent(Component *component)
{
    assert(!component->isRetired());
    registry.retire(*component);
    idToObjectMap.erase(component->getId());
    retiredComponents.push_back(component);
    queueReconciliation(component);
}

void GameManager::flushIfIdle()
{
    if (isIdle())
    {
        commitMutations();
    }
}

void GameManager::queueTransform(GameObject *object)
{
    transformOrderNeeded |= object->parent != nullptr;
    if (object->transformQueueIndex != SIZE_MAX)
    {
        return;
    }
    object->transformQueueIndex = dirtyTransforms.size();
    dirtyTransforms.push_back(object);
}

void GameManager::cancelTransform(GameObject *object)
{
    if (object->transformQueueIndex == SIZE_MAX)
    {
        return;
    }
    const auto index = object->transformQueueIndex;
    auto *moved = dirtyTransforms.back();
    dirtyTransforms[index] = moved;
    moved->transformQueueIndex = index;
    dirtyTransforms.pop_back();
    object->transformQueueIndex = SIZE_MAX;
    if (dirtyTransforms.empty())
    {
        transformOrderNeeded = false;
    }
}

void GameManager::rebuildHierarchyOrder(GameObject *object)
{
    object->hierarchyOrder = nextHierarchyOrder++;
    for (auto *child : object->children)
    {
        rebuildHierarchyOrder(child);
    }
}

void GameManager::synchronizeSubtree(GameObject *object)
{
    if (object->transformStamp == transformStamp)
    {
        return;
    }
    object->transformStamp = transformStamp;

    const auto previous = object->worldPosition;
    object->worldPosition =
        object->parent ? (object->parent->worldPosition + object->position).eval() : object->position;
    if (previous != object->worldPosition)
    {
        if (auto *body = object->getComponent<Rigidbody>())
        {
            body->wakeUp();
        }
    }

    PLATFORMATOR_WORK_ADD(transformVisits, 1);
    object->updateCollider();

    for (auto *child : object->children)
    {
        synchronizeSubtree(child);
    }
}

void GameManager::synchronizeTransforms()
{
    if (hierarchyDirty)
    {
        PLATFORMATOR_WORK_ADD(hierarchyRebuilds, 1);
        nextHierarchyOrder = 0;
        for (auto *object : gameObjects)
        {
            if (!object->parent)
            {
                rebuildHierarchyOrder(object);
            }
        }
        hierarchyDirty = false;
    }

    if (dirtyTransforms.empty())
    {
        return;
    }
    if (transformOrderNeeded)
    {
        std::sort(dirtyTransforms.begin(), dirtyTransforms.end(), [](auto *a, auto *b) {
            return a->hierarchyOrder < b->hierarchyOrder;
        });
    }

    ++transformStamp;
    for (auto *object : dirtyTransforms)
    {
        object->transformQueueIndex = SIZE_MAX;
        synchronizeSubtree(object);
    }

    dirtyTransforms.clear();
    transformOrderNeeded = false;
}

void GameManager::clearObjects()
{
    requestScene(std::nullopt);
    flushIfIdle();
}

void GameManager::runDestructionCallbacks(ScriptComponent *script, std::exception_ptr &error)
{
    if (script->destructionDelivered)
    {
        return;
    }
    script->destructionDelivered = true;
    script->lifecycle = ObjectLifecycle::DestroyCallbacks;
    PhaseGuard callbackPhase(*this, Phase::DestructionCallbacks);
    cancelScriptStart(script);
    const auto destroy = [&](Behavior *behavior) {
        try
        {
            behavior->onDestroy();
        }
        catch (...)
        {
            if (!error)
            {
                error = std::current_exception();
            }
        }
    };
    for (auto *behavior : script->behaviors)
    {
        destroy(behavior);
    }
    for (auto *behavior : script->pendingBehaviors)
    {
        destroy(behavior);
    }
}

void GameManager::reconcilePending(std::exception_ptr &error)
{
    do
    {
        reconciliationDrain.swap(reconciliations);
        for (auto *component : reconciliationDrain)
        {
            PLATFORMATOR_WORK_ADD(reconciliations, 1);
            component->reconcileQueued = false;

            const auto *owner = component->getGameObject();
            const bool desired = !component->isRetired() && owner->getActive() && !owner->isRetired();
            component->callbackEligible = desired;
            if (component->getType() == COLLIDER)
            {
                if (auto *collider = owner->getComponent<Collider>())
                {
                    physicsManager->refreshSolverPairs(collider);
                }
            }

            if (desired == component->systemsRegistered)
            {
                if (desired && component->getType() == ComponentType::SCRIPT)
                {
                    queueScriptStart(static_cast<ScriptComponent *>(component));
                }
                continue;
            }

            if (desired)
            {
                notifyPhysicsManagerOfComponentAdded(component);
                notifyWindowOfComponentAdded(component);
                notifyRuntimeOfComponentAdded(component);
            }
            else
            {
                notifyPhysicsManagerOfComponentRemoved(component);
                notifyWindowOfComponentRemoved(component);
                notifyRuntimeOfComponentRemoved(component);
            }
            component->systemsRegistered = desired;
        }
        reconciliationDrain.clear();

        synchronizeTransforms();

        // Old support recipients remain allocated until this grid/support flush and notification drain.
        physicsManager->flushPendingColliderOperations();
        try
        {
            PhaseGuard exitPhase(*this, Phase::CollisionDispatch);
            physicsManager->handlePendingPhysicsEvents(0.0);
        }
        catch (...)
        {
            if (!error)
            {
                error = std::current_exception();
            }
        }
    } while (!reconciliations.empty());
}

void GameManager::drainRetirements(std::exception_ptr &error)
{
    for (auto *component : retiredComponents)
    {
        component->lifecycle = ObjectLifecycle::DestroyCallbacks;
    }
    for (auto *object : gameObjectsToDelete)
    {
        object->lifecycle = ObjectLifecycle::DestroyCallbacks;
        for (auto *component : object->components)
        {
            if (component)
            {
                component->lifecycle = ObjectLifecycle::DestroyCallbacks;
            }
        }
    }
    for (auto *component : retiredComponents)
    {
        if (component->getType() == ComponentType::SCRIPT)
        {
            runDestructionCallbacks(static_cast<ScriptComponent *>(component), error);
        }
    }
    for (auto *object : gameObjectsToDelete)
    {
        if (auto *script = object->getComponent<ScriptComponent>())
        {
            runDestructionCallbacks(script, error);
        }
    }

    // All callbacks finish before any batch allocation is freed. Hierarchy links
    // have already detached at logical retirement, independent of allocation order.
    for (auto *component : retiredComponents)
    {
        component->lifecycle = ObjectLifecycle::Unregistered;
        registry.erase(*component);
        delete component;
    }
    retiredComponents.clear();

    for (auto *object : gameObjectsToDelete)
    {
        for (auto *component : object->components)
        {
            if (component)
            {
                component->lifecycle = ObjectLifecycle::Unregistered;
                registry.erase(*component);
            }
        }
        object->lifecycle = ObjectLifecycle::Unregistered;
        registry.erase(*object);
        delete object;
    }
    gameObjectsToDelete.clear();
}

void GameManager::commitMutations()
{
    PLATFORMATOR_BENCH_SCOPE_IF(SceneTransition, pendingSceneRequest.has_value());
    PhaseGuard commitPhase(*this, Phase::MutationCommit);
    std::exception_ptr error;
    auto selected = std::move(pendingSceneRequest);
    pendingSceneRequest.reset();

    std::unique_ptr<SceneBuildContext> prepared;
    if (selected && selected->path)
    {
        preparingScene = true;
        try
        {
            auto candidate = std::make_unique<SceneBuildContext>(*this);
            candidate->prepare(Scene(*selected->path));
            reconciliations.reserve(reconciliations.size() + gameObjects.size() * COMPONENT_TYPE_COUNT +
                                    candidate->ids.size());
            gameObjectsToDelete.reserve(gameObjectsToDelete.size() + gameObjects.size());
            scriptComponents.reserve(std::max(scriptComponents.size(), candidate->objects.size()));
            animatorComponents.reserve(std::max(animatorComponents.size(), candidate->objects.size()));
            prepared = std::move(candidate);
        }
        catch (...)
        {
            error = std::current_exception();
        }
        preparingScene = false;
    }

    const bool clearing = selected && !selected->path;
    if (clearing || prepared)
    {
        while (!gameObjects.empty())
        {
            removeGameObject(gameObjects.back());
        }
    }
    reconcilePending(error);
    drainRetirements(error);

    if (clearing)
    {
        nextFileId = 0;
        platformator::Runtime::current().currentSceneFilePath.clear();
    }

    if (prepared)
    {
        gameObjects.swap(prepared->objects);
        idToObjectMap.swap(prepared->ids);
        nextFileId = prepared->nextFileId;
        hierarchyDirty = true;
        for (size_t i = 0; i < gameObjects.size(); ++i)
        {
            auto *object = gameObjects[i];
            registry.publish(*object);
            object->setIsRegisteredInGameManager(true);
            object->setGameManagerIteratorIndex(i);
            queueTransform(object);
            for (auto *component : object->components)
            {
                if (component)
                {
                    registry.publish(*component);
                    queueReconciliation(component);
                }
            }
        }
        platformator::Runtime::current().currentSceneFilePath = std::move(prepared->path);
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
    while (!gameObjects.empty())
    {
        removeGameObject(gameObjects.back());
    }
    try
    {
        commitMutations();
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Runtime shutdown callback failed: %s\n", error.what());
    }
    catch (...)
    {
        std::fprintf(stderr, "Runtime shutdown callback failed\n");
    }
    window->clearTransientAudio();
    freeAllAnimationClips();
    freeAllTextures();
    freeAllAudio();
    shutdownComplete = true;
}

AnimationClip *GameManager::loadAnimationClip(const std::string &filePath,
                                              platformator::ReferenceResolver &resolver, bool required)
{
    if (filePath.empty())
    {
        if (required)
        {
            throw std::invalid_argument("Required animation path is empty");
        }
        return nullptr;
    }
    const auto resolved =
        PathManager::getInstance().resolveAssetPath(filePath, AssetPathType::AnimationClip, required);
    if (required && resolved.usedFallback)
    {
        throw std::runtime_error("Required animation not found: " + filePath);
    }
    if (auto found = animationClipCache.find(resolved.canonicalPath); found != animationClipCache.end())
    {
        return found->second.get();
    }
    std::ifstream file(resolved.absolutePath);
    if (!file)
    {
        if (required)
        {
            throw std::runtime_error("Cannot open animation: " + resolved.absolutePath);
        }
        return nullptr;
    }
    auto clip = std::make_unique<AnimationClip>();
    deserialize(nlohmann::json::parse(file), *clip, resolver);
    clip->setFilePath(resolved.canonicalPath);
    clip->runtimeSerial = registry.serial();
    auto *result = clip.get();
    animationClipCache.emplace(resolved.canonicalPath, std::move(clip));
    return result;
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

void GameManager::assignFileId(BaseObject &object)
{
    if (object.getId() < 0)
    {
        if (nextFileId > std::numeric_limits<int>::max())
        {
            throw std::overflow_error("Scene file IDs exhausted");
        }
        object.setId(static_cast<int>(nextFileId++));
    }
    else
    {
        if (auto found = idToObjectMap.find(object.getId());
            found != idToObjectMap.end() && found->second != &object)
        {
            throw std::invalid_argument("Duplicate registered file ID: " + std::to_string(object.getId()));
        }
        nextFileId = std::max(nextFileId, static_cast<int64_t>(object.getId()) + 1);
    }
}
