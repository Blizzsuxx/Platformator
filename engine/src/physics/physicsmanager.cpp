#include "physics/physicsmanager.h"
#include "physics/physicsgeometry.h"

#include "benchmark/benchmark.h"
#include "core/constants.h"
#include "core/membership.h"
#include "rendering/debugdraw.h"
#include "core/gamemanager.h"
#include "oneapi/tbb.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "platformator/sprite.h"
#include "core/runtimeaccess.h"
#include "benchmark/workprobe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <tuple>

PhysicsManager::PhysicsManager()
    : registeredBodies(), colliderComponents(), pendingPhysicsEvents(), grid(this),
      gravityVector(GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y), gravityVectorNormalized(gravityVector.normalized())
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(forceBodies, awakeDynamicBodies.size());
    if (awakeDynamicBodies.empty())
    {
        return;
    }
    const auto integrateBody = [&](Rigidbody *body) { body->integrateForces(timeDelta, gravityVector); };
    if (awakeDynamicBodies.size() < BODY_INTEGRATION_PARALLEL_THRESHOLD)
    {
        for (auto *body : awakeDynamicBodies)
        {
            integrateBody(body);
        }
        return;
    }
    tbb::parallel_for(size_t(0), awakeDynamicBodies.size(), [&](size_t i) { integrateBody(awakeDynamicBodies[i]); });
}

void PhysicsManager::applyMovement(double timeDelta)
{
    commitMovedBodies(timeDelta, false);
}

void PhysicsManager::commitMovedBodies(double timeDelta, bool useSolverDeltas)
{
    const size_t totalMoving = awakeDynamicBodies.size() + movingKinematicBodies.size();
    PLATFORMATOR_WORK_ADD(movingBodies, totalMoving);
    if (totalMoving == 0)
    {
        return;
    }

    constexpr uint8_t FLAG_TRANSLATED = 1 << 0;
    constexpr uint8_t FLAG_ROTATED = 1 << 1;
    constexpr uint8_t FLAG_FAST_ROOT_LEAF = 1 << 2;
    constexpr uint8_t FLAG_NEED_PUBLISH = 1 << 3;
    constexpr uint8_t FLAG_HAS_SPRITE = 1 << 4;

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

    const auto integrateAndClassify = [&](Rigidbody *body, bool isDynamic, Collider *&outFastCollider) -> uint8_t {
        outFastCollider = nullptr;
        uint8_t changes = 0;
        if (useSolverDeltas && isDynamic)
        {
            const size_t regIdx = body->getPhysicsManagerIndex();
            if (regIdx < solverBodyIntegrated.size() && solverBodyIntegrated[regIdx] != 0)
            {
                const SolverBodyDelta &delta = solverBodyDeltas[regIdx];
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
            const bool translated = (changes & FLAG_TRANSLATED) != 0;
            const bool rotated = (changes & FLAG_ROTATED) != 0;
            bool worldChanged = false;
            if (translated)
            {
                worldChanged = object->worldPosition != object->position;
                object->worldPosition = object->position;
                object->immediateWorldPosition = object->position;
            }
            uint8_t flags = static_cast<uint8_t>(changes | FLAG_FAST_ROOT_LEAF);
            if (worldChanged || rotated)
            {
                flags |= FLAG_NEED_PUBLISH;
                if (object->getComponent<Sprite>() != nullptr)
                {
                    flags |= FLAG_HAS_SPRITE;
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

    if (totalMoving < BODY_INTEGRATION_PARALLEL_THRESHOLD)
    {
        const auto processOne = [&](Rigidbody *body, bool isDynamic) {
            Collider *fastCollider = nullptr;
            const uint8_t flags = integrateAndClassify(body, isDynamic, fastCollider);
            if (flags == 0)
            {
                return;
            }
            if ((flags & FLAG_FAST_ROOT_LEAF) != 0)
            {
                if ((flags & FLAG_TRANSLATED) != 0)
                {
                    PLATFORMATOR_WORK_ADD(transformVisits, 1);
                }
                if ((flags & FLAG_NEED_PUBLISH) != 0)
                {
                    if ((flags & FLAG_HAS_SPRITE) != 0)
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
            publishMoved(body->getGameObject(), (flags & FLAG_TRANSLATED) != 0, (flags & FLAG_ROTATED) != 0);
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

    movedBodyChanges.resize(totalMoving);
    movedFastColliders.resize(totalMoving);
    const size_t awakeCount = awakeDynamicBodies.size();
    tbb::parallel_for(size_t(0), totalMoving, [&](size_t i) {
        const bool isDynamic = i < awakeCount;
        auto *body = isDynamic ? awakeDynamicBodies[i] : movingKinematicBodies[i - awakeCount];
        movedBodyChanges[i] = integrateAndClassify(body, isDynamic, movedFastColliders[i]);
    });

    for (size_t i = 0; i < totalMoving; ++i)
    {
        const uint8_t flags = movedBodyChanges[i];
        if (flags == 0)
        {
            continue;
        }
        if ((flags & FLAG_FAST_ROOT_LEAF) != 0)
        {
            if ((flags & FLAG_TRANSLATED) != 0)
            {
                PLATFORMATOR_WORK_ADD(transformVisits, 1);
            }
            if ((flags & FLAG_NEED_PUBLISH) != 0)
            {
                if ((flags & FLAG_HAS_SPRITE) != 0)
                {
                    auto *body = i < awakeCount ? awakeDynamicBodies[i] : movingKinematicBodies[i - awakeCount];
                    manager.publishRenderGeometryChange(body->getGameObject());
                }
                if (Collider *fastCollider = movedFastColliders[i])
                {
                    PLATFORMATOR_WORK_ADD(colliderSyncRequests, 1);
                    grid.queueSyncCollider(fastCollider);
                }
            }
            continue;
        }
        auto *body = i < awakeCount ? awakeDynamicBodies[i] : movingKinematicBodies[i - awakeCount];
        publishMoved(body->getGameObject(), (flags & FLAG_TRANSLATED) != 0, (flags & FLAG_ROTATED) != 0);
    }
}

void PhysicsManager::checkForCollisions()
{
    broadPhase();
    narrowPhase();
}

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
        body->awakeDynamicIndex = SIZE_MAX;
        body->movingKinematicIndex = SIZE_MAX;
        body->sleepCandidateIndex = SIZE_MAX;
        body->participationQueueIndex = SIZE_MAX;
        body->supportDeltaIndex = SIZE_MAX;
        body->supportDelta = 0;
        body->solverIslandId = -1;
        body->solverIslandBodyIndex = SIZE_MAX;
        body->solverColorMask = 0;
    }
    for (Rigidbody *body : pendingBodyChanges)
    {
        body->participationQueueIndex = SIZE_MAX;
    }
    for (Rigidbody *body : supportDeltaBodies)
    {
        body->supportDeltaIndex = SIZE_MAX;
        body->supportDelta = 0;
        body->solverIslandId = -1;
        body->solverIslandBodyIndex = SIZE_MAX;
        body->solverColorMask = 0;
    }
    for (Collider *collider : colliderComponents)
    {
        collider->setIsRegisteredInPhysicsManager(false);
        collider->setPhysicsManagerIndex(SIZE_MAX);
        collider->setIsRegisteredInGrid(false);
        collider->desiredInGrid = false;
        collider->gridQueueIndex = SIZE_MAX;
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
    persistentIslands.clear();
    freeIslandIds.clear();
    activeIslandIds.clear();
    dirtySplitIslandIds.clear();
    kinematicSolverPairs.clear();
    solverPreStepJobs.clear();
    solverConstraints.clear();
    contactPairs.clear();
    supportDeltaBodies.clear();
    pendingPhysicsEvents.clear();
    movedBodyChanges.clear();
    movedFastColliders.clear();
    grid.clearAll();
}

void PhysicsManager::broadPhase()
{
    PLATFORMATOR_BENCH_SCOPE(BroadPhase);

    grid.flushPendingCellUpdates();

    PLATFORMATOR_BENCH_SET_COUNTER(OccupiedCellCount, grid.getCells().size());
    PLATFORMATOR_BENCH_SET_COUNTER(CandidatePairCount, grid.getCandidatePairCount());
}

void PhysicsManager::narrowPhase()
{
    PLATFORMATOR_BENCH_SCOPE(NarrowPhase);

    const auto &pendingPairs = *grid.getPendingNarrowPhasePairs();
    const bool noPendingBodyChanges = pendingBodyChanges.empty();

    const auto needsSerialTransition = [&](const ColliderPair *pair) noexcept -> bool {
        auto *collision = pair->getCollision();
        const bool hasContact = collision != nullptr;
        if (pair->resultHadContact != hasContact)
        {
            return true;
        }
        if (!hasContact && !pair->isInSolver() && !pair->contributedSupportBodyA &&
            !pair->contributedSupportBodyB && pair->contactIndex == SIZE_MAX)
        {
            return false;
        }
        if (hasContact && pair->contactIndex != SIZE_MAX)
        {
            if (!pair->isInSolver() && !pair->contributedSupportBodyA && !pair->contributedSupportBodyB &&
                (pair->getObjectA()->getIsTrigger() || pair->getObjectB()->getIsTrigger()))
            {
                return false;
            }
            if (noPendingBodyChanges && pair->isInSolver())
            {
                const bool referenceIsA = collision->getReferenceObject() == pair->getObjectA();
                const bool supportsA =
                    (collision->supportState &
                     (referenceIsA ? Collision::SUPPORTS_REFERENCE : Collision::SUPPORTS_INCIDENT)) != 0;
                const bool supportsB =
                    (collision->supportState &
                     (referenceIsA ? Collision::SUPPORTS_INCIDENT : Collision::SUPPORTS_REFERENCE)) != 0;
                if ((pair->contributedSupportBodyA != nullptr) == supportsA &&
                    (pair->contributedSupportBodyB != nullptr) == supportsB)
                {
                    return false;
                }
            }
        }
        return true;
    };

    const auto applySerialTransition = [&](const ColliderPair *pair) {
        auto *collision = pair->getCollision();
        const bool hasContact = collision != nullptr;
        if (pair->resultHadContact != hasContact)
        {
            const auto type = hasContact ? PhysicsEvent::COLLISION_ENTER : PhysicsEvent::COLLISION_EXIT;
            addPendingPhysicsEvent(makePhysicsEvent(type, pair, collision));
        }
        reconcilePairSupport(pair);
        refreshContactParticipation(pair);
    };

    if (pendingPairs.size() < NARROW_PHASE_PARALLEL_THRESHOLD)
    {
        for (auto *pair : pendingPairs)
        {
            processNarrowPhasePair(pair);
        }
        for (auto *pair : pendingPairs)
        {
            PLATFORMATOR_WORK_ADD(geometryPairs, pair->resultTestedGeometry ? 1 : 0);
            PLATFORMATOR_WORK_ADD(contactTransitionChecks, 1);
            if (needsSerialTransition(pair))
            {
                applySerialTransition(pair);
            }
        }
    }
    else
    {
        const size_t pairCount = pendingPairs.size();
        const size_t chunkCount = std::min<size_t>(32, (pairCount + 63) / 64);
        if (narrowTransitionChunks.size() < chunkCount)
        {
            narrowTransitionChunks.resize(chunkCount);
        }
#if PLATFORMATOR_ENABLE_WORK_PROBES
        narrowGeometryCounts.assign(chunkCount, 0);
#endif
        const size_t base = pairCount / chunkCount;
        const size_t rem = pairCount % chunkCount;

        tbb::parallel_for(size_t(0), chunkCount, [&](size_t chunkIdx) {
            auto &transitions = narrowTransitionChunks[chunkIdx];
            transitions.clear();
            const size_t start = chunkIdx * base + std::min(chunkIdx, rem);
            const size_t end = start + base + (chunkIdx < rem ? 1 : 0);
#if PLATFORMATOR_ENABLE_WORK_PROBES
            uint64_t tested = 0;
#endif
            for (size_t i = start; i < end; ++i)
            {
                const ColliderPair *pair = pendingPairs[i];
                processNarrowPhasePair(pair);
#if PLATFORMATOR_ENABLE_WORK_PROBES
                if (pair->resultTestedGeometry)
                {
                    ++tested;
                }
#endif
                if (needsSerialTransition(pair))
                {
                    transitions.push_back(pair);
                }
            }
#if PLATFORMATOR_ENABLE_WORK_PROBES
            narrowGeometryCounts[chunkIdx] = tested;
#endif
        });

#if PLATFORMATOR_ENABLE_WORK_PROBES
        uint64_t totalTested = 0;
        for (size_t c = 0; c < chunkCount; ++c)
        {
            totalTested += narrowGeometryCounts[c];
        }
        PLATFORMATOR_WORK_ADD(geometryPairs, totalTested);
        PLATFORMATOR_WORK_ADD(contactTransitionChecks, pairCount);
#endif

        for (size_t c = 0; c < chunkCount; ++c)
        {
            for (const ColliderPair *pair : narrowTransitionChunks[c])
            {
                applySerialTransition(pair);
            }
        }
    }
    flushSupportDeltas();

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    for (int32_t islandId : activeIslandIds)
    {
        for (const ColliderPair *pair : persistentIslands[static_cast<size_t>(islandId)].contacts)
        {
            if (auto *collision = pair->getCollision())
            {
                DebugDraw::getInstance().addCollisionDebugObject(*collision);
            }
        }
    }
    for (const ColliderPair *pair : kinematicSolverPairs)
    {
        if (auto *collision = pair->getCollision())
        {
            DebugDraw::getInstance().addCollisionDebugObject(*collision);
        }
    }
#endif

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(PendingNarrowPhasePairCount, pendingPairs.size());
    PLATFORMATOR_BENCH_SET_COUNTER(ActiveCollisionCount, contactPairs.size());

#endif

    grid.clearPendingNarrowPhasePairs();
}

#if PLATFORMATOR_ENABLE_BENCHMARKS
size_t PhysicsManager::countAwakeDynamicBodies() const
{
    return awakeDynamicBodies.size();
}
#endif

void PhysicsManager::processNarrowPhasePair(const ColliderPair *pair)
{
    // Structural queue mutation/erasure cannot overlap this distinct-pair worker batch.
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);
    pair->resultHadContact = pair->getCollision() != nullptr;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    pair->resultTestedGeometry = false;
#endif
    if (!pair->getObjectA()->getGameObject()->getActive() ||
        !pair->getObjectB()->getGameObject()->getActive() || pair->getObjectA()->isRetired() ||
        pair->getObjectB()->isRetired())
    {
        pair->clearCollisionNoEvent();
        return;
    }
    if (pair->shouldUpdate())
    {
#if PLATFORMATOR_ENABLE_WORK_PROBES
        pair->resultTestedGeometry = true;
#endif
        physics_geometry::updateContactGeometry(*pair, gravityVectorNormalized);
    }
}

void PhysicsManager::updateSleepingStates(double timeDelta)
{
#if PLATFORMATOR_ENABLE_BENCHMARKS
    if (benchmarkSleepingDisabled)
    {
        return;
    }
#endif
    PLATFORMATOR_WORK_ADD(sleepBodies, sleepCandidates.size());
    if (sleepCandidates.empty())
    {
        return;
    }

    constexpr float linearThresholdSq = LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD;
    const auto isQuiet = [](const Rigidbody *body) noexcept {
        return body->getVelocity().squaredNorm() <= linearThresholdSq &&
               std::abs(body->getAngularVelocity()) <= ANGULAR_SLEEP_THRESHOLD;
    };

    const auto evaluateIsland = [&](const PersistentIsland &island, std::vector<Rigidbody *> &toSleep) {
        bool allCandidates = true;
        bool anyCandidate = false;
        for (Rigidbody *body : island.bodies)
        {
            if (body->sleepCandidateIndex != SIZE_MAX)
            {
                anyCandidate = true;
            }
            else
            {
                allCandidates = false;
            }
        }
        if (!anyCandidate)
        {
            return;
        }

        if (allCandidates)
        {
            bool quiet = true;
            double minTimer = std::numeric_limits<double>::infinity();
            for (Rigidbody *body : island.bodies)
            {
                quiet &= isQuiet(body);
                minTimer = std::min(minTimer, body->getSleepTimer());
            }
            const double timer = quiet ? minTimer + timeDelta : 0.0;
            if (timer >= SLEEP_DELAY)
            {
                for (Rigidbody *body : island.bodies)
                {
                    toSleep.push_back(body);
                }
            }
            else
            {
                for (Rigidbody *body : island.bodies)
                {
                    body->setSleepTimer(timer);
                }
            }
            return;
        }

        // Mixed island (some bodies supported, some unsupported): preserve exact sleep-candidate connectivity.
        for (Rigidbody *body : island.bodies)
        {
            if (body->sleepCandidateIndex != SIZE_MAX)
            {
                resetSleepIsland(body);
            }
        }
        for (const ColliderPair *pair : island.contacts)
        {
            auto *rbA = pair->getObjectA()->getGameObject()->getComponent<Rigidbody>();
            auto *rbB = pair->getObjectB()->getGameObject()->getComponent<Rigidbody>();
            if (rbA != nullptr && rbB != nullptr)
            {
                mergeSleepIslands(rbA, rbB);
            }
        }
        for (Rigidbody *body : island.bodies)
        {
            if (body->sleepCandidateIndex == SIZE_MAX)
            {
                continue;
            }
            auto *root = findSleepRoot(body);
            root->islandQuiet &= isQuiet(body);
            root->islandTimer = std::min(root->islandTimer, body->getSleepTimer());
        }
        for (Rigidbody *body : island.bodies)
        {
            if (body->sleepCandidateIndex == SIZE_MAX)
            {
                continue;
            }
            auto *root = findSleepRoot(body);
            const double timer = root->islandQuiet ? root->islandTimer + timeDelta : 0.0;
            if (timer >= SLEEP_DELAY)
            {
                toSleep.push_back(body);
            }
            else
            {
                body->setSleepTimer(timer);
            }
        }
    };

    std::vector<Rigidbody *> bodiesToSleep;
    const bool runParallel = activeIslandIds.size() >= 32 &&
                             tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) > 1 &&
                             tbb::this_task_arena::max_concurrency() > 1;
    if (!runParallel)
    {
        for (int32_t islandId : activeIslandIds)
        {
            evaluateIsland(persistentIslands[static_cast<size_t>(islandId)], bodiesToSleep);
        }
    }
    else
    {
        for (auto &local : parallelBodiesToSleep)
        {
            local.clear();
        }
        tbb::task_group_context context(tbb::task_group_context::isolated);
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, activeIslandIds.size(), 16),
            [&](const auto &range) {
                auto &localToSleep = parallelBodiesToSleep.local();
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    evaluateIsland(persistentIslands[static_cast<size_t>(activeIslandIds[i])], localToSleep);
                }
            },
            context);
        for (auto &local : parallelBodiesToSleep)
        {
            bodiesToSleep.insert(bodiesToSleep.end(), local.begin(), local.end());
            local.clear();
        }
    }

    // Evaluate isolated sleep candidates (0 active dynamic contacts, e.g. zero-gravity bodies).
    for (Rigidbody *body : sleepCandidates)
    {
        if (body->solverIslandId != -1)
        {
            continue;
        }
        const double timer = isQuiet(body) ? body->getSleepTimer() + timeDelta : 0.0;
        if (timer >= SLEEP_DELAY)
        {
            bodiesToSleep.push_back(body);
        }
        else
        {
            body->setSleepTimer(timer);
        }
    }

    if (!bodiesToSleep.empty())
    {
        std::sort(bodiesToSleep.begin(), bodiesToSleep.end(), [](const Rigidbody *a, const Rigidbody *b) {
            return a->getPhysicsManagerIndex() < b->getPhysicsManagerIndex();
        });
        for (Rigidbody *body : bodiesToSleep)
        {
            body->setIsSleeping(true);
        }
    }

    // Sleep decisions queue membership changes; drain only after all traversals finish.
    reconcileChangedBodies();
}

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
    std::sort(pendingPhysicsEvents.begin(), pendingPhysicsEvents.end(), [&](const auto &a, const auto &b)
              { return key(a) < key(b); });
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

