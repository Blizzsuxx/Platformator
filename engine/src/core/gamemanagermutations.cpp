#include "core/gamemanager.h"

#include <cassert>
#include <exception>

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/membership.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptcomponent.h"
#include "platformator/sprite.h"

// ============================================================================
// Transform Synchronization
// ============================================================================

void GameManager::queueTransform(GameObject *object, bool refreshImmediate)
{
    if (refreshImmediate)
    {
        object->refreshImmediatePositions();
    }
    membership(dirtyTransforms, object, &GameObject::transformQueueIndex, true);
}

void GameManager::cancelTransform(GameObject *object)
{
    membership(dirtyTransforms, object, &GameObject::transformQueueIndex, false);
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
    // StepHook/FixedUpdate are ejected by reconcileChangedBodies() before stepDynamics().
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

bool GameManager::hasRetiredScripts() const
{
    for (auto *component : retiredComponents)
    {
        if (component->getType() == ComponentType::SCRIPT)
        {
            return true;
        }
    }
    for (auto *object : gameObjectsToDelete)
    {
        if (object->components[ComponentType::SCRIPT] != nullptr)
        {
            return true;
        }
    }
    return false;
}

void GameManager::deliverRetirementDestructionCallbacks(std::exception_ptr &error)
{
    // Move the whole batch into the callback phase before invoking any script.
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

void GameManager::deleteRetiredEntities()
{
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

void GameManager::drainRetirements(std::exception_ptr &error)
{
    if (retiredComponents.empty() && gameObjectsToDelete.empty())
    {
        return;
    }

    PLATFORMATOR_BENCH_SCOPE(Retirement);
    if (hasRetiredScripts())
    {
        deliverRetirementDestructionCallbacks(error);
    }
    // All callbacks finish before any batch allocation is freed.
    deleteRetiredEntities();
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

