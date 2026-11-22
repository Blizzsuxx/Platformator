#include "physics/physicsmanager.h"
#include "physics/physicsgeometry.h"

#include "benchmark/benchmark.h"
#include "core/constants.h"
#include "rendering/debugdraw.h"
#include "core/gamemanager.h"
#include "oneapi/tbb.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
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
    const auto integrateBody = [&](Rigidbody *body) {
        resetSleepIsland(body);
        body->integrateForces(timeDelta, gravityVector);
    };
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
    const size_t totalMoving = awakeDynamicBodies.size() + movingKinematicBodies.size();
    PLATFORMATOR_WORK_ADD(movingBodies, totalMoving);
    if (totalMoving == 0)
    {
        return;
    }
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
    if (totalMoving < BODY_INTEGRATION_PARALLEL_THRESHOLD)
    {
        for (auto *body : awakeDynamicBodies)
        {
            if (const auto changes = body->integrateLocal(timeDelta))
            {
                publishMoved(body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0);
            }
        }
        for (auto *body : movingKinematicBodies)
        {
            if (const auto changes = body->integrateLocal(timeDelta))
            {
                publishMoved(body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0);
            }
        }
        return;
    }
    movedBodyChanges.resize(totalMoving);
    tbb::parallel_for(size_t(0), totalMoving, [&](size_t i) {
        auto *body =
            i < awakeDynamicBodies.size() ? awakeDynamicBodies[i] : movingKinematicBodies[i - awakeDynamicBodies.size()];
        movedBodyChanges[i] = static_cast<uint8_t>(body->integrateLocal(timeDelta));
    });
    for (size_t i = 0; i < totalMoving; ++i)
    {
        const uint8_t changes = movedBodyChanges[i];
        if (changes != 0)
        {
            auto *body = i < awakeDynamicBodies.size() ? awakeDynamicBodies[i]
                                                       : movingKinematicBodies[i - awakeDynamicBodies.size()];
            publishMoved(body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0);
        }
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

    size_t removeIndex = rigidBodyComponent->getPhysicsManagerIndex();
    size_t lastIndex = registeredBodies.size() - 1;

    if (removeIndex != lastIndex)
    {
        Rigidbody *movedRigidBody = registeredBodies[lastIndex];
        registeredBodies[removeIndex] = movedRigidBody;
        movedRigidBody->setPhysicsManagerIndex(removeIndex);
    }

    registeredBodies.pop_back();
    rigidBodyComponent->setPhysicsManagerIndex(SIZE_MAX);
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

    size_t removeIndex = colliderComponent->getPhysicsManagerIndex();
    size_t lastIndex = colliderComponents.size() - 1;

    if (removeIndex != lastIndex)
    {
        Collider *movedCollider = colliderComponents[lastIndex];
        colliderComponents[removeIndex] = movedCollider;
        movedCollider->setPhysicsManagerIndex(removeIndex);
    }

    colliderComponents.pop_back();
    colliderComponent->setPhysicsManagerIndex(SIZE_MAX);
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
    }
    for (Rigidbody *body : pendingBodyChanges)
    {
        body->participationQueueIndex = SIZE_MAX;
    }
    for (Rigidbody *body : supportDeltaBodies)
    {
        body->supportDeltaIndex = SIZE_MAX;
        body->supportDelta = 0;
    }
    for (Collider *collider : colliderComponents)
    {
        collider->setIsRegisteredInPhysicsManager(false);
        collider->setPhysicsManagerIndex(SIZE_MAX);
        collider->setIsRegisteredInGrid(false);
        collider->desiredInGrid = false;
        collider->gridQueueIndex = SIZE_MAX;
        collider->cachedCellCount = 0;
        collider->adjacentPairs.clear();
    }

    registeredBodies.clear();
    awakeDynamicBodies.clear();
    movingKinematicBodies.clear();
    sleepCandidates.clear();
    pendingBodyChanges.clear();
    colliderComponents.clear();
    solverConstraints.clear();
    contactPairs.clear();
    solverPairs.clear();
    supportDeltaBodies.clear();
    pendingPhysicsEvents.clear();
    movedBodyChanges.clear();
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
    if (pendingPairs.size() < NARROW_PHASE_PARALLEL_THRESHOLD)
    {
        for (auto *pair : pendingPairs)
        {
            processNarrowPhasePair(pair);
        }
    }
    else
    {
        tbb::parallel_for(size_t(0), pendingPairs.size(), [&](size_t i)
                          { processNarrowPhasePair(pendingPairs[i]); });
    }

    for (auto *pair : pendingPairs)
    {
        PLATFORMATOR_WORK_ADD(geometryPairs, pair->resultTestedGeometry ? 1 : 0);
        PLATFORMATOR_WORK_ADD(contactTransitionChecks, 1);
        auto *collision = pair->getCollision();
        const bool hasContact = collision != nullptr;
        if (pair->resultHadContact != hasContact)
        {
            const auto type = hasContact ? PhysicsEvent::COLLISION_ENTER : PhysicsEvent::COLLISION_EXIT;
            addPendingPhysicsEvent(makePhysicsEvent(type, pair, collision));
        }
        else if (pair->solverIndex == SIZE_MAX && !pair->contributedSupportBodyA && !pair->contributedSupportBodyB &&
                 (pair->contactIndex != SIZE_MAX) == hasContact)
        {
            // A surviving trigger is already live after the geometry pass and can neither support nor solve.
            if (!hasContact || pair->getObjectA()->getIsTrigger() || pair->getObjectB()->getIsTrigger())
                continue;
        }

        reconcilePairSupport(pair);
        refreshContactParticipation(pair);
    }
    flushSupportDeltas();

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    for (auto *pair : solverPairs)
    {
        auto *collision = pair->getCollision();
        DebugDraw::getInstance().addCollisionDebugObject(*collision);
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
    PLATFORMATOR_WORK_ADD(sleepBodies, sleepCandidates.size());

    // Reduce all member motion and timers before applying any group decision.
    for (auto *body : sleepCandidates)
    {
        auto *root = findSleepRoot(body);
        root->islandQuiet &=
            body->getVelocity().squaredNorm() <= LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD &&
            std::abs(body->getAngularVelocity()) <= ANGULAR_SLEEP_THRESHOLD;
        root->islandTimer = std::min(root->islandTimer, body->getSleepTimer());
    }

    for (auto *body : sleepCandidates)
    {
        auto *root = findSleepRoot(body);
        const auto timer = root->islandQuiet ? root->islandTimer + timeDelta : 0;
        if (timer >= SLEEP_DELAY)
        {
            body->setIsSleeping(true);
        }
        else
        {
            body->setSleepTimer(timer);
        }
    }

    // Sleep decisions queue membership changes; drain only after both fixed-list traversals finish.
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

