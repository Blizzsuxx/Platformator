#pragma once

#include "collision.h"
#include "grid.h"
#include "helpers.h"
#include "physicspairstate.h"
#include "platformator/collider.h"
#include "platformator/rigidbody.h"
#include "spatialindex.h"
#include <algorithm>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <vector>

class GameManager;

struct PhysicsEvent
{
    enum EventType
    {
        COLLISION_ENTER,
        COLLISION_EXIT
    };

    EventType type;
    Collision *collision;
    Collider *colliderA;
    Collider *colliderB;
};

class PhysicsManager
{
    friend class GameManager;
    friend class Grid;
    friend class PhysicsPairState;
    friend class Rigidbody;
    friend class platformator_detail::PhysicsSpatialIndex;

  public:
    explicit PhysicsManager(const SpatialSettings &settings = {});
    ~PhysicsManager();

    void applyPhysics(double timeDelta);
    void applyMovement(double timeDelta);

    void checkForCollisions();
    void resolveCollisions(double timeDelta);
    const Grid &getGrid() const;
    const Grid *getGridIfActive() const;
    const platformator_detail::PhysicsSpatialIndex *getSpatialIndexIfActive() const;
    size_t getSpatialIndexOwnedCapacityBytes() const;
    size_t getSpatialCommonPairCapacityBytes() const;
    size_t getSpatialScratchCapacityBytes() const;
    const std::vector<Collider *> &getColliders() const;

    const std::vector<const ColliderPair *> &getContactPairs() const
    {
        return contactPairs;
    }

    void addRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void addColliderComponent(Collider *colliderComponent);
    void queueColliderSync(Collider *colliderComponent);

    void removeRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void removeColliderComponent(Collider *colliderComponent);

    void addPendingPhysicsEvent(const PhysicsEvent &event);
    const std::vector<PhysicsEvent> &getPendingPhysicsEvents() const;
    void clearPendingPhysicsEvents();
    void handlePendingPhysicsEvents(double timeDelta);

    const Eigen::Vector2f &getGravityVector() const;
    const Eigen::Vector2f &getGravityVectorNormalized() const;
    void refreshSolverPairs(Collider *collider);
    void refreshResponse(Collider *collider);

  private:
    void flushPendingColliderOperations();

    void updateSleepingStates(double timeDelta);
    void broadPhase();
    void narrowPhase();
    void processNarrowPhasePair(const ColliderPair *pair);
    void refreshContactParticipation(const ColliderPair *pair);
    void refreshSolverParticipation(const ColliderPair *pair, bool live);
    void removePair(const ColliderPair *pair);
    void reconcilePairSupport(const ColliderPair *pair);
    void reconcileSupportContribution(Rigidbody *&contributor, Rigidbody *desired);
    void queueSupportDelta(Rigidbody *body, int delta);
    void flushSupportDeltas();
    void queueBodyParticipationChange(Rigidbody *body);
    void cancelBodyParticipationChange(Rigidbody *body);
    void reconcileBodyParticipation(Rigidbody *body);
    void reconcileChangedBodies();

    static void resetSleepIsland(Rigidbody *body)
    {
        // Step-local aggregation: the solver merges these roots before the sleep decision.
        body->sleepParent = body;
        body->sleepRank = 0;
        body->islandQuiet = true;
        body->islandTimer = body->getSleepTimer();
    }

    Rigidbody *findSleepRoot(Rigidbody *body);
    void mergeSleepIslands(Rigidbody *a, Rigidbody *b);
#if PLATFORMATOR_ENABLE_BENCHMARKS
    size_t countAwakeDynamicBodies() const;
#endif

    void preStepCollision(Collision *collision, float inverseTimeDelta);
    void resolveCollision(const Collision *collision);
    void resolveKinematicCollision(const Collision *collision);
    void resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal,
                                           float correctionDistance);
    PhysicsEvent makePhysicsEvent(PhysicsEvent::EventType type, const ColliderPair *pair,
                                  Collision *collision);

    std::vector<Rigidbody *> registeredBodies;
    std::vector<Rigidbody *> awakeDynamicBodies, movingKinematicBodies, sleepCandidates, pendingBodyChanges;
    std::vector<Collider *> colliderComponents;
    std::vector<Collision *> dynamicCollisionScratch;
    std::vector<const ColliderPair *> contactPairs;
    // Append/swap-remove order; physical solving does not require canonical pair order.
    std::vector<const ColliderPair *> solverPairs;
    std::vector<Rigidbody *> supportDeltaBodies;
    std::vector<PhysicsEvent> pendingPhysicsEvents;

    struct MovedObject
    {
        GameObject *object;
        bool translated;
    };

    tbb::enumerable_thread_specific<std::vector<MovedObject>> movedObjects;
    PhysicsPairState pairState;
    std::unique_ptr<Grid> grid;
    std::unique_ptr<platformator_detail::PhysicsSpatialIndex> spatialIndex;

    Eigen::Vector2f gravityVector;
    Eigen::Vector2f gravityVectorNormalized;
};
