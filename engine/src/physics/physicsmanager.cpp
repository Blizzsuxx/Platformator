#include "physics/physicsmanager.h"

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/membership.h"
#include "core/runtimeaccess.h"
#include "platformator/sprite.h"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>

#include <algorithm>
#include <exception>
#include <tuple>

// ============================================================================
// Lifecycle & Worker Detection
// ============================================================================

bool PhysicsManager::hasParallelPhysicsWorkers() noexcept
{
    return tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) > 1 &&
           tbb::this_task_arena::max_concurrency() > 1;
}

PhysicsManager::PhysicsManager()
    : registeredBodies(), colliderComponents(), pendingPhysicsEvents(), grid(this),
      gravityVector(GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y), gravityVectorNormalized(gravityVector.normalized())
{
}

PhysicsManager::~PhysicsManager() = default;

// ============================================================================
// Body Movement Commit & Transform Publication
// ============================================================================

#if PLATFORMATOR_ENABLE_BENCHMARKS
void PhysicsManager::integrateMovementForBenchmark(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(forceBodies, awakeDynamicBodies.size());
    const auto integrateBody = [&](Rigidbody *body) { body->integrateForces(timeDelta, gravityVector); };
    if (awakeDynamicBodies.size() < BODY_INTEGRATION_PARALLEL_THRESHOLD)
    {
        for (auto *body : awakeDynamicBodies)
        {
            integrateBody(body);
        }
    }
    else
    {
        tbb::parallel_for(size_t(0), awakeDynamicBodies.size(), [&](size_t i) { integrateBody(awakeDynamicBodies[i]); });
    }
    commitMovedBodies(timeDelta, false);
}

size_t PhysicsManager::countAwakeDynamicBodies() const
{
    return awakeDynamicBodies.size();
}
#endif

void PhysicsManager::commitMovedBodies(double timeDelta, bool useSolverDeltas)
{
    const size_t totalMoving = awakeDynamicBodies.size() + movingKinematicBodies.size();
    PLATFORMATOR_WORK_ADD(movingBodies, totalMoving);
    if (totalMoving == 0)
    {
        return;
    }

    enum MovedBodyFlags : uint8_t
    {
        MOVED_TRANSLATED = Rigidbody::INTEGRATE_TRANSLATED,
        MOVED_ROTATED = Rigidbody::INTEGRATE_ROTATED,
        MOVED_FAST_ROOT_LEAF = 1u << 2,
        MOVED_NEED_PUBLISH = 1u << 3,
        MOVED_HAS_SPRITE = 1u << 4,
    };

    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    const auto publishMoved = [&](GameObject *object, bool translated, bool rotated) {
        const bool handledBySubtreeSync =
            translated && object->parent == nullptr && object->position != object->worldPosition;
        if (rotated && !handledBySubtreeSync)
        {
            manager.publishRenderGeometryChange(object);
            object->updateCollider();
        }
        if (translated)
        {
            // The manager synchronizes this batch before any callbacks or reads.
            manager.queueTransform(object, false);
        }
    };

    const auto finishMoved = [&](Rigidbody *body, uint8_t flags, Collider *fastCollider) {
        if (flags == 0)
        {
            return;
        }
        if ((flags & MOVED_FAST_ROOT_LEAF) != 0)
        {
            if ((flags & MOVED_TRANSLATED) != 0)
            {
                PLATFORMATOR_WORK_ADD(transformVisits, 1);
            }
            if ((flags & MOVED_NEED_PUBLISH) != 0)
            {
                if ((flags & MOVED_HAS_SPRITE) != 0)
                {
                    manager.publishRenderGeometryChange(body->getGameObject());
                }
                if (fastCollider != nullptr)
                {
                    PLATFORMATOR_WORK_ADD(colliderSyncRequests, 1);
                    grid.queueSyncCollider(fastCollider);
                }
            }
            return;
        }
        publishMoved(body->getGameObject(), (flags & MOVED_TRANSLATED) != 0, (flags & MOVED_ROTATED) != 0);
    };

    const bool hasSolverDeltas = useSolverDeltas && !activeIslandIds.empty();
    const auto integrateAndClassify = [&](Rigidbody *body, bool isDynamic, Collider *&outFastCollider) -> uint8_t {
        outFastCollider = nullptr;
        uint8_t changes = 0;
        if (useSolverDeltas && isDynamic)
        {
            const uint32_t awakeIndex = body->awakeDynamicIndex;
            if (hasSolverDeltas && awakeIndex < solverBodyDeltas.size() &&
                solverBodyDeltas[awakeIndex].step == solverStep)
            {
                const SolverBodyDelta &delta = solverBodyDeltas[awakeIndex];
                changes = body->integrateDelta(delta.dx, delta.dy, delta.dAngle);
            }
            else
            {
                body->integrateForces(timeDelta, gravityVector);
                changes = body->integrateLocal(timeDelta);
            }
        }
        else
        {
            changes = body->integrateLocal(timeDelta);
        }
        if (changes == 0)
        {
            return 0;
        }

        GameObject *object = body->getGameObject();
        if (object->parent == nullptr && object->children.empty() && object->transformQueueIndex == SIZE_MAX)
        {
            const bool translated = (changes & MOVED_TRANSLATED) != 0;
            const bool rotated = (changes & MOVED_ROTATED) != 0;
            bool worldChanged = false;
            if (translated)
            {
                worldChanged = object->worldPosition != object->position;
                object->worldPosition = object->position;
                object->immediateWorldPosition = object->position;
            }
            uint8_t flags = static_cast<uint8_t>(changes | MOVED_FAST_ROOT_LEAF);
            if (worldChanged || rotated)
            {
                flags |= MOVED_NEED_PUBLISH;
                if (object->getComponent<Sprite>() != nullptr)
                {
                    flags |= MOVED_HAS_SPRITE;
                }
                if (auto *collider = object->getComponent<Collider>())
                {
                    collider->dirty |= Collider::Geometry;
                    if (collider->getIsRegisteredInPhysicsManager())
                    {
                        outFastCollider = collider;
                    }
                }
            }
            return flags;
        }
        return changes;
    };

    if (totalMoving < BODY_INTEGRATION_PARALLEL_THRESHOLD || !hasParallelPhysicsWorkers())
    {
        const auto processOne = [&](Rigidbody *body, bool isDynamic) {
            Collider *fastCollider = nullptr;
            const uint8_t flags = integrateAndClassify(body, isDynamic, fastCollider);
            finishMoved(body, flags, fastCollider);
        };
        for (auto *body : awakeDynamicBodies)
        {
            processOne(body, true);
        }
        for (auto *body : movingKinematicBodies)
        {
            processOne(body, false);
        }
        return;
    }

    moveScratch.bodyChanges.resize(totalMoving);
    moveScratch.fastColliders.resize(totalMoving);
    const size_t awakeCount = awakeDynamicBodies.size();
    tbb::task_group_context context(tbb::task_group_context::isolated);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, totalMoving, 64),
        [&](const auto &range) {
            for (size_t i = range.begin(); i != range.end(); ++i)
            {
                const bool isDynamic = i < awakeCount;
                auto *body = isDynamic ? awakeDynamicBodies[i] : movingKinematicBodies[i - awakeCount];
                moveScratch.bodyChanges[i] = integrateAndClassify(body, isDynamic, moveScratch.fastColliders[i]);
            }
        },
        context);

    for (size_t i = 0; i < totalMoving; ++i)
    {
        if (moveScratch.bodyChanges[i] == 0)
        {
            continue;
        }
        auto *body = i < awakeCount ? awakeDynamicBodies[i] : movingKinematicBodies[i - awakeCount];
        finishMoved(body, moveScratch.bodyChanges[i], moveScratch.fastColliders[i]);
    }
}

// ============================================================================
// Component Registration, Grid Access & World Reset
// ============================================================================

const Grid &PhysicsManager::getGrid() const
{
    return grid;
}

const std::vector<Collider *> &PhysicsManager::getColliders() const
{
    return colliderComponents;
}

void PhysicsManager::addRigidBodyComponent(Rigidbody *rigidBodyComponent)
{
    if (rigidBodyComponent == nullptr || rigidBodyComponent->getIsRegisteredInPhysicsManager() ||
        !rigidBodyComponent->getGameObject()->getActive())
    {
        return;
    }

    rigidBodyComponent->setPhysicsManagerIndex(registeredBodies.size());
    rigidBodyComponent->setIsRegisteredInPhysicsManager(true);
    registeredBodies.push_back(rigidBodyComponent);
    queueBodyParticipationChange(rigidBodyComponent);
}

void PhysicsManager::addColliderComponent(Collider *colliderComponent)
{
    if (colliderComponent == nullptr || colliderComponent->getIsRegisteredInPhysicsManager() ||
        !colliderComponent->getGameObject()->getActive())
    {
        return;
    }

    colliderComponent->setPhysicsManagerIndex(colliderComponents.size());
    colliderComponent->setIsRegisteredInPhysicsManager(true);
    colliderComponents.push_back(colliderComponent);

    grid.queueAddCollider(colliderComponent);
}

void PhysicsManager::queueColliderSync(Collider *colliderComponent)
{
    grid.queueSyncCollider(colliderComponent);
}

void PhysicsManager::removeRigidBodyComponent(Rigidbody *rigidBodyComponent)
{
    if (rigidBodyComponent == nullptr || !rigidBodyComponent->getIsRegisteredInPhysicsManager())
    {
        return;
    }

    swapRemoveIndexed(
        registeredBodies, rigidBodyComponent, [](const Rigidbody *rb) { return rb->getPhysicsManagerIndex(); },
        [](Rigidbody *rb, size_t idx) { rb->setPhysicsManagerIndex(idx); });
    rigidBodyComponent->setIsRegisteredInPhysicsManager(false);
    cancelBodyParticipationChange(rigidBodyComponent);
    reconcileBodyParticipation(rigidBodyComponent);
}

void PhysicsManager::removeColliderComponent(Collider *colliderComponent)
{
    if (colliderComponent == nullptr || !colliderComponent->getIsRegisteredInPhysicsManager())
    {
        return;
    }

    swapRemoveIndexed(
        colliderComponents, colliderComponent, [](const Collider *c) { return c->getPhysicsManagerIndex(); },
        [](Collider *c, size_t idx) { c->setPhysicsManagerIndex(idx); });
    colliderComponent->setIsRegisteredInPhysicsManager(false);

    grid.queueRemoveCollider(colliderComponent);
}

void PhysicsManager::flushPendingColliderOperations()
{
    {
        PLATFORMATOR_BENCH_SCOPE(DeferredGridFlush);
        grid.flushPendingCellUpdates();
    }
    flushSupportDeltas();
}

void PhysicsManager::clearAll()
{
    for (Rigidbody *body : registeredBodies)
    {
        body->setIsRegisteredInPhysicsManager(false);
        body->setPhysicsManagerIndex(SIZE_MAX);
        body->awakeDynamicIndex = Rigidbody::INVALID_INDEX;
        body->movingKinematicIndex = Rigidbody::INVALID_INDEX;
        body->sleepCandidateIndex = Rigidbody::INVALID_INDEX;
        body->participationQueueIndex = Rigidbody::INVALID_INDEX;
        body->supportDeltaIndex = Rigidbody::INVALID_INDEX;
        body->supportDelta = 0;
        body->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
        body->solverIslandBodyIndex = Rigidbody::INVALID_INDEX;
        body->solverColorMask = 0;
    }
    for (Rigidbody *body : pendingBodyChanges)
    {
        body->participationQueueIndex = Rigidbody::INVALID_INDEX;
    }
    for (Rigidbody *body : supportDeltaBodies)
    {
        body->supportDeltaIndex = Rigidbody::INVALID_INDEX;
        body->supportDelta = 0;
        body->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
        body->solverIslandBodyIndex = Rigidbody::INVALID_INDEX;
        body->solverColorMask = 0;
    }
    for (Collider *collider : colliderComponents)
    {
        collider->setIsRegisteredInPhysicsManager(false);
        collider->setPhysicsManagerIndex(SIZE_MAX);
        collider->setIsRegisteredInGrid(false);
        collider->desiredInGrid = false;
        collider->gridQueueIndex = Collider::INVALID_INDEX;
        collider->cachedCellCount = 0;
        collider->invalidateFatBounds();
        collider->adjacentPairs.clear();
    }

    registeredBodies.clear();
    awakeDynamicBodies.clear();
    movingKinematicBodies.clear();
    sleepCandidates.clear();
    pendingBodyChanges.clear();
    colliderComponents.clear();
    contactPairs.clear();
    supportDeltaBodies.clear();
    pendingPhysicsEvents.clear();

    persistentIslands.clear();
    freeIslandIds.clear();
    activeIslandIds.clear();
    dirtySplitIslandIds.clear();
    kinematicSolverPairs.clear();

    splitScratch.clear();
    coloredScratch.clear();
    scheduleScratch.clear();
    narrowScratch.clear();
    moveScratch.clear();
    solverBodyDeltas.clear();
    solverStep = 0;

    serialSolverScratch.clear();
    for (auto &scratch : solverIslandScratch)
    {
        scratch.clear();
    }
    serialSleepScratch.clear();
    for (auto &scratch : parallelSleepScratch)
    {
        scratch.clear();
    }
    bodiesToSleepScratch.clear();
    grid.clearAll();
}

// ============================================================================
// Physics Events & Gravity
// ============================================================================

void PhysicsManager::addPendingPhysicsEvent(const PhysicsEvent &event)
{
    pendingPhysicsEvents.push_back(event);
    PLATFORMATOR_WORK_ADD(contactEventRecords, 1);
    if (event.type == PhysicsEvent::COLLISION_ENTER)
    {
        PLATFORMATOR_BENCH_ADD_COUNTER(CollisionEnterEventCount, 1);
    }
    else
    {
        PLATFORMATOR_BENCH_ADD_COUNTER(CollisionExitEventCount, 1);
    }
}

const std::vector<PhysicsEvent> &PhysicsManager::getPendingPhysicsEvents() const
{
    return pendingPhysicsEvents;
}

void PhysicsManager::clearPendingPhysicsEvents()
{
    pendingPhysicsEvents.clear();
}

void PhysicsManager::handlePendingPhysicsEvents(double timeDelta)
{
    const auto key = [](const PhysicsEvent &event)
    {
        const int family = event.type == PhysicsEvent::COLLISION_EXIT ? 0 : 1;
        return std::tuple{family, event.colliderA->getHandle(), event.colliderB->getHandle()};
    };
    std::sort(pendingPhysicsEvents.begin(), pendingPhysicsEvents.end(),
              [&](const auto &a, const auto &b) { return key(a) < key(b); });

    std::exception_ptr error;
    for (const PhysicsEvent &event : pendingPhysicsEvents)
    {
        auto deliver = [&](Collider *receiver, Collider *other)
        {
            try
            {
                if (event.type == PhysicsEvent::COLLISION_ENTER)
                {
                    receiver->triggerCollisionEnter(event.collision, other, timeDelta);
                }
                else
                {
                    receiver->triggerCollisionExit(other, timeDelta);
                }
            }
            catch (...)
            {
                if (!error)
                {
                    error = std::current_exception();
                }
            }
        };
        deliver(event.colliderA, event.colliderB);
        deliver(event.colliderB, event.colliderA);
    }
    clearPendingPhysicsEvents();
    if (error)
    {
        std::rethrow_exception(error);
    }
}

const Eigen::Vector2f &PhysicsManager::getGravityVector() const
{
    return gravityVector;
}

const Eigen::Vector2f &PhysicsManager::getGravityVectorNormalized() const
{
    return gravityVectorNormalized;
}

PhysicsEvent PhysicsManager::makePhysicsEvent(PhysicsEvent::EventType type, const ColliderPair *pair,
                                              Collision *collision)
{
    return PhysicsEvent{type, collision, pair->getObjectA(), pair->getObjectB()};
}
