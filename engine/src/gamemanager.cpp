#include "gamemanager.h"
#include "simulationclock.h"
#include "workprobe.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "benchmark.h"
#include "constants.h"
#include "objectpool.h"
#include "physicstaskcontext.h"
#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/camera.h"
#include "platformator/collider.h"
#include "platformator/rigidbody.h"
#include "platformator/runtime.h"
#include "platformator/scriptcomponent.h"
#include "platformator/sprite.h"
#include "referenceresolver.h"
#include "scenebuilder.h"

namespace
{
template <typename T> void swapRemoveIndexed(std::vector<T *> &list, T *item)
{
    const size_t removeIndex = item->getGameManagerIndex();
    if (removeIndex == SIZE_MAX)
    {
        return;
    }
    const size_t lastIndex = list.size() - 1;
    if (removeIndex != lastIndex)
    {
        T *moved = list.back();
        list[removeIndex] = moved;
        moved->setGameManagerIndex(removeIndex);
    }
    list.pop_back();
    item->setGameManagerIndex(SIZE_MAX);
}
} // namespace

GameManager::GameManager(const WindowSettings &windowSettings, const DebugSettings &debugSettings)
    : window(std::make_unique<SDLWindow>(windowSettings, debugSettings)),
      physicsManager(std::make_unique<PhysicsManager>()), assets(*this)
{
    PLATFORMATOR_BENCH_RESET();
}

GameManager::~GameManager()
{
    shutdown();
    physicsManager.reset();
    window.reset();
}

void GameManager::tagAssetRuntime(Asset &asset) const
{
    asset.runtimeSerial = registry.serial();
}

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

void GameManager::saveScene(const Scene &scene)
{
    assertCanMutate();
    if (!isIdle())
    {
        throw std::logic_error("Scene save requires an idle runtime");
    }

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

void GameManager::createMainCameraIfNoMainCameraExists()
{
    assertCanMutate();
    if (window->getMainCamera())
    {
        return;
    }
    createGameObject()->setName("Main Camera")->addComponent<Camera>();
}

GameObject *GameManager::createGameObject()
{
    assertCanMutate();
    auto cleanup = [](GameObject *object) { delete object; };
    std::unique_ptr<GameObject, decltype(cleanup)> gameObject(new GameObject(), cleanup);
    assignFileId(*gameObject);
    gameObject->setGameManagerIteratorIndex(gameObjects.size());
    gameObjects.push_back(gameObject.get());
    registry.add(*gameObject, platformator_detail::ObjectKind::GameObject);
    idToObjectMap.insertOrAssign(gameObject->getId(), gameObject.get());
    return gameObject.release();
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

    registry.add(*gameObject, platformator_detail::ObjectKind::GameObject);
    idToObjectMap.insertOrAssign(gameObject->getId(), gameObject);

    gameObject->addComponentsToGameManager();

    for (GameObject *child : gameObject->getChildren())
    {
        registerGameObjectSubtree(child);
    }
}

void GameManager::removeGameObject(GameObject *gameObject)
{
    if (!gameObject->getIsRegisteredInGameManager())
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

    const size_t lastIndex = gameObjects.size() - 1;
    const size_t removeIndex = gameObject->gameManagerIteratorIndex;
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
    gameObjectsToDelete.push_back(gameObject);
}

GameObject *GameManager::getGameObject(const std::string &name)
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

BaseObject *GameManager::getObjectById(int id) const
{
    return idToObjectMap.find(id);
}

BaseObject *GameManager::resolveHandle(platformator::ObjectHandle handle, platformator_detail::ObjectKind kind) const
{
    return registry.get(handle, kind);
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
        if (BaseObject *found = idToObjectMap.find(object.getId()); found != nullptr && found != &object)
        {
            throw std::invalid_argument("Duplicate registered file ID: " + std::to_string(object.getId()));
        }
        nextFileId = std::max(nextFileId, static_cast<int64_t>(object.getId()) + 1);
    }
}

[[noreturn]] void GameManager::throwCannotMutate() const
{
    if (std::this_thread::get_id() != mainThread)
    {
        throw std::logic_error("Structural mutation is main-thread-only");
    }
    throw std::logic_error("Structural mutation is forbidden during preparation, saving, onDestroy, or shutdown");
}

// ============================================================================
// Simulation Loop & Frame Stepping
// ============================================================================

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
    if (simulationStepNumber == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Simulation step number is exhausted");
    }

    auto stage = [&](Phase next, auto &&work) {
        PhaseGuard guard(*this, next);
        work();
    };

    std::exception_ptr error;
    PLATFORMATOR_BENCH_FRAME_BEGIN();
    try
    {
        stage(Phase::Starting, [&] { startPendingScripts(); });
        stage(Phase::StepHook, [&] {
            if (customCallback)
            {
                PLATFORMATOR_BENCH_SCOPE(StepHook);
                customCallback(timeDelta);
            }
        });
        stage(Phase::FixedUpdate, [&] { fixedUpdateScriptComponents(timeDelta); });
        stage(Phase::Physics, [&] {
            PLATFORMATOR_BENCH_SCOPE(Physics);
            platformator_detail::runPhysicsWork([&] {
                synchronizeTransforms();
                physicsManager->checkForCollisions();
                physicsManager->reconcileChangedBodies();
                physicsManager->applyPhysics(timeDelta);
                physicsManager->resolveCollisions(timeDelta);
                physicsManager->applyMovement(timeDelta);
                synchronizeTransforms();
            });
        });
        stage(Phase::CollisionDispatch, [&] { physicsManager->handlePendingPhysicsEvents(timeDelta); });
        stage(Phase::Update, [&] { updateScriptComponents(timeDelta); });
        stage(Phase::Animation, [&] { updateAnimatorComponents(timeDelta); });
        stage(Phase::LateUpdate, [&] { lateUpdateScriptComponents(timeDelta); });
    }
    catch (...)
    {
        error = std::current_exception();
    }

    try
    {
        PLATFORMATOR_BENCH_SCOPE(Cleanup);
        window->updateAudio();
        commitMutations();
    }
    catch (...)
    {
        if (!error)
        {
            error = std::current_exception();
        }
    }

    if (!error)
    {
        ++simulationStepNumber;
    }

    PLATFORMATOR_BENCH_SET_COUNTER(ObjectCount, gameObjects.size());
    PLATFORMATOR_BENCH_FRAME_END();

    if (error)
    {
        std::rethrow_exception(error);
    }
}

void GameManager::loop(const std::function<bool(SDLWindow &)> &ownerThreadService,
                       const std::function<void(std::uint64_t)> &afterSimulationStep)
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
        if (ownerThreadService && !ownerThreadService(*window))
        {
            break;
        }
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
            window->clearDebugObjects();
            runSimulationStep(FIXED_TIME_STEP);
            if (afterSimulationStep)
            {
                afterSimulationStep(simulationStepNumber);
            }
        }
        if (!window->isRunning())
        {
            window->clearAdvanceFrameRequest();
            break;
        }
        // Simulation steps update audio themselves; paused and zero-step frames still need it.
        if (steps == 0)
        {
            window->updateAudio();
        }
        {
            PhaseGuard renderPhase(*this, Phase::Rendering);
            window->render();
        }
        window->clearAdvanceFrameRequest();
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
    if (!isIdle() || std::this_thread::get_id() != mainThread)
    {
        throw std::logic_error("Simulation steps require an idle runtime on its main thread");
    }
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
        window->clearDebugObjects();
        runSimulationStepWithCustomCallback(customCallback, timeDelta);
    }
    else
    {
        window->updateAudio();
    }
    PhaseGuard renderPhase(*this, Phase::Rendering);
    window->render();
    window->clearAdvanceFrameRequest();
    return window->isRunning();
}

// ============================================================================
// Component Subsystem Registration & Script Dispatch
// ============================================================================

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
        idToObjectMap.insertOrAssign(component->getId(), component);
    }
    queueReconciliation(component);
}

void GameManager::registerComponentInSystems(Component *component)
{
    switch (component->getType())
    {
    case ComponentType::ANIMATOR: {
        auto *animator = static_cast<Animator *>(component);
        animator->setGameManagerIndex(animatorComponents.size());
        animatorComponents.push_back(animator);
        break;
    }
    case ComponentType::SCRIPT: {
        auto *script = static_cast<ScriptComponent *>(component);
        script->setGameManagerIndex(scriptComponents.size());
        scriptComponents.push_back(script);
        syncScriptUpdateLists(script);
        queueScriptStart(script);
        break;
    }
    case ComponentType::AUDIO: {
        auto *audio = static_cast<Audio *>(component);
        audio->ensureTrack();
        if (audio->getAutoPlay())
        {
            audio->play();
        }
        break;
    }
    case ComponentType::COLLIDER:
        physicsManager->addColliderComponent(static_cast<Collider *>(component));
        break;
    case ComponentType::RIGID_BODY:
        physicsManager->addRigidBodyComponent(static_cast<Rigidbody *>(component));
        break;
    case ComponentType::SPRITE:
        window->addSpriteComponent(static_cast<Sprite *>(component));
        break;
    case ComponentType::CAMERA:
        window->setMainCamera(static_cast<Camera *>(component));
        break;
    default:
        break;
    }
}

void GameManager::unregisterComponentFromSystems(Component *component)
{
    switch (component->getType())
    {
    case ComponentType::ANIMATOR:
        swapRemoveIndexed(animatorComponents, static_cast<Animator *>(component));
        break;
    case ComponentType::SCRIPT: {
        auto *script = static_cast<ScriptComponent *>(component);
        removeScriptFromUpdateLists(script);
        swapRemoveIndexed(scriptComponents, script);
        break;
    }
    case ComponentType::AUDIO:
        static_cast<Audio *>(component)->stop();
        break;
    case ComponentType::COLLIDER:
        physicsManager->removeColliderComponent(static_cast<Collider *>(component));
        break;
    case ComponentType::RIGID_BODY:
        physicsManager->removeRigidBodyComponent(static_cast<Rigidbody *>(component));
        break;
    case ComponentType::SPRITE:
        window->removeSpriteComponent(static_cast<Sprite *>(component));
        break;
    case ComponentType::CAMERA:
        if (window->getMainCamera() == static_cast<Camera *>(component))
        {
            window->setMainCamera(nullptr);
        }
        break;
    default:
        break;
    }
}

void GameManager::syncScriptUpdateLists(ScriptComponent *script)
{
    auto syncPhase = [script](std::vector<ScriptComponent *> &list, size_t &indexField,
                              size_t ScriptComponent::*member, bool want) {
        if (want)
        {
            if (indexField == SIZE_MAX)
            {
                indexField = list.size();
                list.push_back(script);
            }
        }
        else if (indexField != SIZE_MAX)
        {
            const size_t removeIndex = indexField;
            const size_t lastIndex = list.size() - 1;
            if (removeIndex != lastIndex)
            {
                ScriptComponent *moved = list[lastIndex];
                list[removeIndex] = moved;
                moved->*member = removeIndex;
            }
            list.pop_back();
            indexField = SIZE_MAX;
        }
    };

    const uint8_t mask = script->activeUpdateMask;
    syncPhase(fixedUpdateScripts, script->fixedUpdateListIndex, &ScriptComponent::fixedUpdateListIndex,
              (mask & BEHAVIOR_UPDATE_FIXED) != 0);
    syncPhase(updateScripts, script->updateListIndex, &ScriptComponent::updateListIndex,
              (mask & BEHAVIOR_UPDATE_FRAME) != 0);
    syncPhase(lateUpdateScripts, script->lateUpdateListIndex, &ScriptComponent::lateUpdateListIndex,
              (mask & BEHAVIOR_UPDATE_LATE) != 0);
}

void GameManager::removeScriptFromUpdateLists(ScriptComponent *script)
{
    auto removePhase = [](std::vector<ScriptComponent *> &list, size_t &indexField, size_t ScriptComponent::*member) {
        if (indexField == SIZE_MAX)
            return;
        const size_t removeIndex = indexField;
        const size_t lastIndex = list.size() - 1;
        if (removeIndex != lastIndex)
        {
            ScriptComponent *moved = list[lastIndex];
            list[removeIndex] = moved;
            moved->*member = removeIndex;
        }
        list.pop_back();
        indexField = SIZE_MAX;
    };

    removePhase(fixedUpdateScripts, script->fixedUpdateListIndex, &ScriptComponent::fixedUpdateListIndex);
    removePhase(updateScripts, script->updateListIndex, &ScriptComponent::updateListIndex);
    removePhase(lateUpdateScripts, script->lateUpdateListIndex, &ScriptComponent::lateUpdateListIndex);
}

void GameManager::fixedUpdateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : fixedUpdateScripts)
    {
        if (scriptComponent->canDispatch())
        {
            scriptComponent->fixedUpdate(timeDelta);
        }
    }
}

void GameManager::updateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : updateScripts)
    {
        if (scriptComponent->canDispatch())
        {
            scriptComponent->update(timeDelta);
        }
    }
}

void GameManager::lateUpdateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : lateUpdateScripts)
    {
        if (scriptComponent->canDispatch())
        {
            scriptComponent->lateUpdate(timeDelta);
        }
    }
}

void GameManager::updateAnimatorComponents(double timeDelta)
{
    for (Animator *animator : animatorComponents)
    {
        if (!animator->getIsCallbackEligible())
        {
            continue;
        }
        assert(!animator->isRetired() && animator->getGameObject()->getActive());
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
        if (script->getGameManagerIndex() != SIZE_MAX)
        {
            syncScriptUpdateLists(script);
        }
        cancelScriptStart(script);
    }
    pendingStartScripts.clear();
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
    // Do not swap-remove: this can be called while startPendingScripts() iterates by index.
    const auto index = script->pendingStartIndex;
    if (index == SIZE_MAX)
    {
        return;
    }
    pendingStartScripts[index] = nullptr;
    script->pendingStartIndex = SIZE_MAX;
}

// ============================================================================
// Transform Synchronization
// ============================================================================

void GameManager::queueTransform(GameObject *object, bool refreshImmediate)
{
    if (refreshImmediate)
    {
        object->refreshImmediatePositions();
    }
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
}

void GameManager::synchronizeSubtree(GameObject *object)
{
    const auto previous = object->worldPosition;
    object->worldPosition =
        object->parent ? (object->parent->worldPosition + object->position).eval() : object->position;
    object->immediateWorldPosition = object->worldPosition;
    if (previous != object->worldPosition)
    {
        publishRenderGeometryChange(object);
        if (auto *body = object->getComponent<Rigidbody>())
        {
            body->wakeUp();
        }
        object->updateCollider();
    }

    PLATFORMATOR_WORK_ADD(transformVisits, 1);

    for (auto *child : object->children)
    {
        synchronizeSubtree(child);
    }
}

void GameManager::publishRenderGeometryChange(GameObject *object)
{
    if (object == nullptr || !object->getIsRegisteredInGameManager())
    {
        return;
    }
    if (auto *sprite = object->getComponent<Sprite>())
    {
        window->notifySpriteGeometryChanged(sprite);
    }
}

void GameManager::synchronizeTransforms()
{
    if (dirtyTransforms.empty())
    {
        return;
    }

    const uint64_t cleanStamp = ++transformStamp;
    auto hasDirtyAncestor = [cleanStamp](GameObject *object) -> bool {
        GameObject *ancestor = object->parent;
        while (ancestor != nullptr)
        {
            if (ancestor->transformQueueIndex != SIZE_MAX)
            {
                return true;
            }
            if (ancestor->transformStamp == cleanStamp)
            {
                return false;
            }
            ancestor = ancestor->parent;
        }
        for (ancestor = object->parent; ancestor != nullptr && ancestor->transformStamp != cleanStamp;
             ancestor = ancestor->parent)
        {
            ancestor->transformStamp = cleanStamp;
        }
        return false;
    };

    // Keep transformQueueIndex intact during filtering so ancestor checks see every dirty node.
    // Top-most dirty roots have strictly disjoint subtrees, so every affected node is visited once.
    for (auto *object : dirtyTransforms)
    {
        if (!hasDirtyAncestor(object))
        {
            synchronizeSubtree(object);
        }
    }
    for (auto *object : dirtyTransforms)
    {
        object->transformQueueIndex = SIZE_MAX;
    }
    dirtyTransforms.clear();
}

// ============================================================================
// Mutation Reconciliation, Retirement & Scene Transitions
// ============================================================================

void GameManager::queueReconciliation(Component *component)
{
    component->callbackEligible = false;
    // Notify PhysicsManager immediately so bodies retired or deactivated during
    // StepHook/FixedUpdate are ejected by reconcileChangedBodies() before applyPhysics().
    if (component->getType() == RIGID_BODY && static_cast<Rigidbody *>(component)->getIsRegisteredInPhysicsManager())
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
        commitLocalMutations();
    }
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
        if (!reconciliations.empty())
        {
            reconciliationDrain.swap(reconciliations);
            for (auto *component : reconciliationDrain)
            {
                PLATFORMATOR_WORK_ADD(reconciliations, 1);
                component->reconcileQueued = false;

                const auto *owner = component->getGameObject();
                const bool desired = !component->isRetired() && owner->getActive() && !owner->isRetired();
                component->callbackEligible = desired;

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
                    registerComponentInSystems(component);
                }
                else
                {
                    unregisterComponentFromSystems(component);
                }
                component->systemsRegistered = desired;
            }
            reconciliationDrain.clear();
        }

        if (!dirtyTransforms.empty())
        {
            synchronizeTransforms();
        }

        // Old support recipients remain allocated until this grid/support flush and notification drain.
        physicsManager->flushPendingColliderOperations();
        if (!physicsManager->getPendingPhysicsEvents().empty())
        {
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
        }
    } while (!reconciliations.empty() || !dirtyTransforms.empty());
}

void GameManager::drainRetirements(std::exception_ptr &error)
{
    if (retiredComponents.empty() && gameObjectsToDelete.empty())
    {
        return;
    }

    PLATFORMATOR_BENCH_SCOPE(Retirement);
    bool hasScriptDestruction = false;
    for (auto *component : retiredComponents)
    {
        if (component->getType() == ComponentType::SCRIPT)
        {
            hasScriptDestruction = true;
            break;
        }
    }
    if (!hasScriptDestruction)
    {
        for (auto *object : gameObjectsToDelete)
        {
            if (object->components[ComponentType::SCRIPT] != nullptr)
            {
                hasScriptDestruction = true;
                break;
            }
        }
    }

    if (hasScriptDestruction)
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
    }

    // All callbacks finish before any batch allocation is freed.
    for (auto *component : retiredComponents)
    {
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
                registry.erase(*component);
            }
        }
        registry.erase(*object);
        delete object;
    }
    gameObjectsToDelete.clear();
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
        }
        gameObjectsToDelete.push_back(object);
    }
    gameObjects.clear();
    idToObjectMap.clear();
    dirtyTransforms.clear();
    reconciliations.clear();
    reconciliationDrain.clear();
    for (ScriptComponent *script : scriptComponents)
    {
        script->setGameManagerIndex(SIZE_MAX);
        script->fixedUpdateListIndex = SIZE_MAX;
        script->updateListIndex = SIZE_MAX;
        script->lateUpdateListIndex = SIZE_MAX;
    }
    scriptComponents.clear();
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

void GameManager::commitLocalMutations()
{
    PhaseGuard commitPhase(*this, Phase::MutationCommit);
    std::exception_ptr error;
    reconcilePending(error);
    drainRetirements(error);
    if (error)
    {
        std::rethrow_exception(error);
    }
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
            scriptComponents.reserve(std::max(scriptComponents.size(), candidate->scriptCount));
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
        platformator::Runtime::current().currentSceneFilePath.clear();
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
        platformator::Runtime::current().currentSceneFilePath = std::move(prepared->path);
        reconcilePending(error);
    }
    if (error)
    {
        std::rethrow_exception(error);
    }
}

void GameManager::commitMutations()
{
    if (pendingSceneRequest)
    {
        auto selected = std::move(*pendingSceneRequest);
        pendingSceneRequest.reset();
        commitSceneTransition(std::move(selected));
        return;
    }
    commitLocalMutations();
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
    window->clearTransientAudio();
    assets.freeAll();
    platformator_detail::releaseEmptyObjectPools();
    shutdownComplete = true;
}
