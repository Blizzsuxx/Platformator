#include "core/gamemanager.h"
#include "core/simulationclock.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "benchmark/benchmark.h"
#include "core/constants.h"
#include "core/membership.h"
#include "physics/physicstaskcontext.h"
#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/camera.h"
#include "platformator/collider.h"
#include "platformator/rigidbody.h"
#include "platformator/runtime.h"
#include "platformator/scriptcomponent.h"
#include "platformator/sprite.h"
#include "scene/referenceresolver.h"

namespace
{
template <typename T> void swapRemoveIndexed(std::vector<T *> &list, T *item)
{
    ::swapRemoveIndexed(
        list, item, [](const T *entry) { return entry->getGameManagerIndex(); },
        [](T *entry, size_t index) { entry->setGameManagerIndex(index); });
}

template <typename MemberFn>
void dispatchScriptUpdate(const std::vector<ScriptComponent *> &scripts, MemberFn fn, double timeDelta)
{
    for (ScriptComponent *scriptComponent : scripts)
    {
        if (scriptComponent->canDispatch())
        {
            (scriptComponent->*fn)(timeDelta);
        }
    }
}
} // namespace

GameManager::GameManager(const WindowSettings &windowSettings, const DebugSettings &debugSettings)
    : window(std::make_unique<SDLWindow>(windowSettings, debugSettings)),
      audioManager(std::make_unique<AudioManager>(windowSettings.headless)),
      physicsManager(std::make_unique<PhysicsManager>()), assets(*this)
{
    if (!window->attachAudioManager(audioManager.get()))
    {
        window->attachAudioManager(nullptr);
        audioManager.reset();
        window.reset();
        throw std::runtime_error("Failed to attach audio manager to SDLWindow");
    }
    PLATFORMATOR_BENCH_RESET();
}

GameManager::~GameManager()
{
    shutdown();
    physicsManager.reset();
    if (window)
    {
        window->attachAudioManager(nullptr);
    }
    audioManager.reset();
    window.reset();
}

void GameManager::tagAssetRuntime(Asset &asset) const
{
    asset.runtimeSerial = registry.serial();
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

void GameManager::registerIdentity(BaseObject &object, platformator_detail::ObjectKind kind)
{
    registry.add(object, kind);
    try
    {
        idToObjectMap.insertOrAssign(object.getId(), &object);
    }
    catch (...)
    {
        registry.erase(object);
        throw;
    }
}

void GameManager::assertIdleMainThread() const
{
    if (!isIdle() || std::this_thread::get_id() != mainThread)
    {
        throw std::logic_error("Simulation steps require an idle runtime on its main thread");
    }
}

GameObject *GameManager::createGameObject()
{
    assertCanMutate();
    auto cleanup = [](GameObject *object) { delete object; };
    std::unique_ptr<GameObject, decltype(cleanup)> gameObject(new GameObject(), cleanup);
    assignFileId(*gameObject);
    gameObjects.reserve(gameObjects.size() + 1);
    registerIdentity(*gameObject, platformator_detail::ObjectKind::GameObject);
    gameObjects.push_back(gameObject.get());
    gameObject->setGameManagerIteratorIndex(gameObjects.size() - 1);
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

    const size_t initialGameObjectCount = gameObjects.size();
    const size_t initialReconciliationCount = reconciliations.size();
    const int64_t initialNextFileId = nextFileId;
    try
    {
        std::vector<GameObject *> subtree;
        subtree.push_back(gameObject);
        for (size_t i = 0; i < subtree.size(); ++i)
        {
            for (GameObject *child : subtree[i]->getChildren())
            {
                if (child != nullptr && !child->getIsRegisteredInGameManager())
                {
                    subtree.push_back(child);
                }
            }
        }

        size_t totalComponents = 0;
        for (GameObject *obj : subtree)
        {
            for (Component *component : obj->components)
            {
                if (component != nullptr)
                {
                    ++totalComponents;
                }
            }
        }

        gameObjects.reserve(gameObjects.size() + subtree.size());
        reconciliations.reserve(reconciliations.size() + totalComponents);
        idToObjectMap.reserve(idToObjectMap.size() + subtree.size() + totalComponents);

        for (GameObject *obj : subtree)
        {
            assignFileId(*obj);
            for (Component *component : obj->components)
            {
                if (component != nullptr)
                {
                    assignFileId(*component);
                }
            }
            gameObjects.push_back(obj);
            obj->setGameManagerIteratorIndex(gameObjects.size() - 1);
            registry.add(*obj, platformator_detail::ObjectKind::GameObject);
            idToObjectMap.insertOrAssign(obj->getId(), obj);
            for (Component *component : obj->components)
            {
                if (component != nullptr)
                {
                    notifyComponentAdded(component);
                }
            }
        }
    }
    catch (...)
    {
        while (reconciliations.size() > initialReconciliationCount)
        {
            reconciliations.back()->reconcileQueued = false;
            reconciliations.pop_back();
        }
        while (gameObjects.size() > initialGameObjectCount)
        {
            GameObject *obj = gameObjects.back();
            gameObjects.pop_back();
            obj->setGameManagerIteratorIndex(SIZE_MAX);
            for (Component *component : obj->components)
            {
                if (component != nullptr && component->getHandle())
                {
                    if (idToObjectMap.find(component->getId()) == component)
                    {
                        idToObjectMap.erase(component->getId());
                    }
                    registry.erase(*component);
                    component->lifecycle = ObjectLifecycle::Staged;
                }
            }
            if (obj->getHandle())
            {
                if (idToObjectMap.find(obj->getId()) == obj)
                {
                    idToObjectMap.erase(obj->getId());
                }
                registry.erase(*obj);
                obj->lifecycle = ObjectLifecycle::Staged;
            }
        }
        nextFileId = initialNextFileId;
        throw;
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

    membership(gameObjects, gameObject, &GameObject::gameManagerIteratorIndex, false);
    cancelTransform(gameObject);
    idToObjectMap.erase(gameObject->getId());

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

AudioManager *GameManager::getAudioManager() const
{
    return audioManager.get();
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

void GameManager::runSimulationStep(double timeDelta, const std::function<void(double)> &customCallback)
{
    assertIdleMainThread();
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
                physicsManager->stepDynamics(timeDelta);
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
        audioManager->update(window->shouldUseFixedStepDelta());
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
        window->handleSDLEvents();
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
            audioManager->update(window->shouldUseFixedStepDelta());
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

bool GameManager::simulateAndRenderFrame(double timeDelta, const std::function<void(double)> &customCallback)
{
    assertIdleMainThread();
    if (!window->isRunning())
    {
        return false;
    }

    window->handleSDLEvents();
    if (!window->isRunning())
    {
        return false;
    }

    if (window->shouldSimulateFrame())
    {
        window->clearDebugObjects();
        runSimulationStep(timeDelta, customCallback);
    }
    else
    {
        audioManager->update(window->shouldUseFixedStepDelta());
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
        reconciliations.reserve(reconciliations.size() + 1);
        registerIdentity(*component, kind);
    }
    queueReconciliation(component);
}

void GameManager::registerComponentInSystems(Component *component)
{
    switch (component->getType())
    {
    case ComponentType::ANIMATOR: {
        auto *animator = static_cast<Animator *>(component);
        animatorComponents.push_back(animator);
        animator->setGameManagerIndex(animatorComponents.size() - 1);
        break;
    }
    case ComponentType::SCRIPT: {
        auto *script = static_cast<ScriptComponent *>(component);
        scriptComponents.push_back(script);
        script->setGameManagerIndex(scriptComponents.size() - 1);
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
    const uint8_t mask = script->activeUpdateMask;
    membership(fixedUpdateScripts, script, &ScriptComponent::fixedUpdateListIndex,
               (mask & BEHAVIOR_UPDATE_FIXED) != 0);
    membership(updateScripts, script, &ScriptComponent::updateListIndex, (mask & BEHAVIOR_UPDATE_FRAME) != 0);
    membership(lateUpdateScripts, script, &ScriptComponent::lateUpdateListIndex, (mask & BEHAVIOR_UPDATE_LATE) != 0);
}

void GameManager::removeScriptFromUpdateLists(ScriptComponent *script)
{
    membership(fixedUpdateScripts, script, &ScriptComponent::fixedUpdateListIndex, false);
    membership(updateScripts, script, &ScriptComponent::updateListIndex, false);
    membership(lateUpdateScripts, script, &ScriptComponent::lateUpdateListIndex, false);
}

void GameManager::fixedUpdateScriptComponents(double timeDelta)
{
    dispatchScriptUpdate(fixedUpdateScripts, &ScriptComponent::fixedUpdate, timeDelta);
}

void GameManager::updateScriptComponents(double timeDelta)
{
    dispatchScriptUpdate(updateScripts, &ScriptComponent::update, timeDelta);
}

void GameManager::lateUpdateScriptComponents(double timeDelta)
{
    dispatchScriptUpdate(lateUpdateScripts, &ScriptComponent::lateUpdate, timeDelta);
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

