#pragma once

#include "platformator/rigidbody.h"
#include "platformator/collider.h"
#include "collision.h"
#include "segmentedintervallist.h"
#include <algorithm>
#include <chrono>
#include <oneapi/tbb/concurrent_vector.h>
#include <vector>
#include <span>
#include "helpers.h"
#include "grid.h"

class GameManager;

struct PhysicsEvent
{
    enum EventType
    {
        COLLISION_ENTER,
        COLLISION_EXIT,
        COLLISION_STAY
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
    friend class Rigidbody;

  public:
    PhysicsManager();
    ~PhysicsManager();

    void applyPhysics(double timeDelta);
    void applyMovement(double timeDelta);

    void checkForCollisions();
    void resolveCollisions(double timeDelta);
    const Grid &getGrid() const;
    const std::vector<Collider *> &getColliders() const;
    const std::vector<const ColliderPair *> &getContactPairs() const
    {
        return contactPairs;
    }

    void addRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void addColliderComponent(Collider *colliderComponent);
    // void refreshColliderComponent(Collider *colliderComponent);
    void queueColliderSync(Collider *colliderComponent);

    void removeRigidBodyComponent(Rigidbody *rigidBodyComponent);
    void removeColliderComponent(Collider *colliderComponent);

    void addPendingPhysicsEvent(const PhysicsEvent &event);
    const std::vector<PhysicsEvent> &getPendingPhysicsEvents() const;
    void clearPendingPhysicsEvents();
    void handlePendingPhysicsEvents(double timeDelta);

    const Eigen::Vector2f &getGravityVector() const;
    const Eigen::Vector2f &getGravityVectorNormalized() const;
    void refreshSubscriptions(Collider *collider);
    void refreshResponse(Collider *collider);

  private:
    void flushPendingColliderOperations();

    void updateSleepingStates(double timeDelta);
    void broadPhase();
    void narrowPhase();
    void processNarrowPhasePair(const ColliderPair *pair);
    void refreshPairLists(const ColliderPair *pair);
    void removePair(const ColliderPair *pair);
    void reducePairSupport(const ColliderPair *pair);
    void queueSupportDelta(Rigidbody *body, int delta);
    void flushSupportDeltas();
    void canonicalizePairs();
    void queueBodyChange(Rigidbody *body);
    void cancelBodyChange(Rigidbody *body);
    void reconcileBody(Rigidbody *body);
    void drainBodyChanges();
    Rigidbody *sleepRoot(Rigidbody *body);
    void mergeSleepIslands(Rigidbody *a, Rigidbody *b);
#if PLATFORMATOR_ENABLE_BENCHMARKS
    size_t countAwakeDynamicBodies() const;
#endif

    void satCreateCollision(const ColliderPair &pair);
    void circleCircleCollision(const ColliderPair &pair);
    void circleBoxCollision(const ColliderPair &pair);

    bool checkProjections(std::span<const Eigen::Vector2f> normals, const Collider *referenceCollider,
                          const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal,
                          const Collider *&realIncidentCollider);
    void preStepCollision(Collision *collision, float inverseTimeDelta);
    void resolveCollision(const Collision *collision);
    void resolveKinematicCollision(const Collision *collision);
    void resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal,
                                           float correctionDistance);
    void calculateContactPoint(Collision *collision);
    PhysicsEvent makePhysicsEvent(PhysicsEvent::EventType type, const ColliderPair *pair, Collision *collision);

    std::vector<Rigidbody *> rigidBodyComponents;
    std::vector<Rigidbody *> awakeBodies, kinematicBodies, sleepEligibleBodies, changedBodies;
    std::vector<Collider *> colliderComponents;
    std::vector<Collision *> activeCollisions;
    std::vector<Collision *> dynamicCollisionScratch;
    std::vector<const ColliderPair *> contactPairs;
    std::vector<const ColliderPair *> solverPairs;
    std::vector<const ColliderPair *> stayPairs;
    std::vector<Rigidbody *> supportDeltaBodies;
    bool solverOrderDirty = false, stayOrderDirty = false;
    uint64_t simulationStep = 0;
    std::vector<PhysicsEvent> pendingPhysicsEvents;
    struct MovedObject
    {
        GameObject *object;
        bool translated;
    };
    tbb::enumerable_thread_specific<std::vector<MovedObject>> movedObjects;
    Grid grid;

    Eigen::Vector2f gravityVector;
    Eigen::Vector2f gravityVectorNormalized;
};

struct NarrowPhaseLocalResults
{
    std::vector<Collision *> activeCollisions;
    std::vector<PhysicsEvent> physicsEvents;
};
