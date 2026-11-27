#pragma once

#include "platformator/collision.h"
#include "physics/broadphase/grid.h"
#include "core/helpers.h"
#include "platformator/collider.h"
#include "platformator/rigidbody.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <optional>
#include <utility>
#include <vector>

#include "platformator/collisiontypes.h"

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

    std::optional<platformator::RaycastHit> raycast(const Eigen::Vector2f &origin,
                                                    const Eigen::Vector2f &direction, float maxDistance,
                                                    uint64_t collisionMask = ~uint64_t{0},
                                                    bool includeTriggers = false) const;
    bool raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance,
                 platformator::RaycastHit &outHit, uint64_t collisionMask = ~uint64_t{0},
                 bool includeTriggers = false) const;
    std::vector<platformator::RaycastHit> raycastAll(const Eigen::Vector2f &origin,
                                                     const Eigen::Vector2f &direction, float maxDistance,
                                                     uint64_t collisionMask = ~uint64_t{0},
                                                     bool includeTriggers = false) const;

private:
    template <typename HitCallback>
    bool traverseRayCandidates(const Eigen::Vector2f &origin, const Eigen::Vector2d &dir, float maxDistance,
                               const double &limit, uint64_t collisionMask, bool includeTriggers,
                               HitCallback &&onHit) const;
    void flushPendingColliderOperations();
    void clearAll();

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

    struct SolverBodyState
    {
        Eigen::Vector2f velocity = Eigen::Vector2f::Zero();
        float angularVelocity = 0.0f;
        float inverseMass = 0.0f;
        float inverseMomentOfInertia = 0.0f;
    };

    struct alignas(16) WideBodyVel
    {
        float vx = 0.0f;
        float vy = 0.0f;
        float w = 0.0f;
        float pad = 0.0f;
    };

    struct IslandWorkUnit
    {
        uint32_t begin = 0;
        uint8_t count = 0;
        uint32_t contactWeight = 0;
    };

    struct SolverConstraint
    {
        Rigidbody *rbA = nullptr;
        Rigidbody *rbB = nullptr;
        ClipPointsWithData *contactPoints = nullptr;
        uint32_t bodyIndexA = 0;
        uint32_t bodyIndexB = 0;
        Eigen::Vector2f normal = Eigen::Vector2f::Zero();
        Eigen::Vector2f tangent = Eigen::Vector2f::Zero();
        Eigen::Vector2f rA[2] = {Eigen::Vector2f::Zero(), Eigen::Vector2f::Zero()};
        Eigen::Vector2f rB[2] = {Eigen::Vector2f::Zero(), Eigen::Vector2f::Zero()};
        float massNormal[2] = {0.0f, 0.0f};
        float massTangent[2] = {0.0f, 0.0f};
        float bias[2] = {0.0f, 0.0f};
        float accumulatedNormalImpulse[2] = {0.0f, 0.0f};
        float accumulatedTangentImpulse[2] = {0.0f, 0.0f};
        Eigen::Matrix2f normalK = Eigen::Matrix2f::Zero();
        Eigen::Matrix2f normalMass = Eigen::Matrix2f::Zero();
        float solverFriction = 0.0f;
        uint8_t pointCount = 0;
        bool blockSolve = false;
    };

    struct IslandSolverScratch
    {
        std::vector<SolverConstraint> constraints;
        std::vector<SolverBodyState> bodies;
        std::vector<WideBodyVel> wideBodies;
        std::vector<std::pair<size_t, Rigidbody *>> writableBodies;
    };

    struct IslandColorGraph
    {
        static constexpr uint8_t MAX_PARALLEL_COLORS = 16;
        static constexpr uint8_t OVERFLOW_COLOR = 16;
        static constexpr uint8_t TOTAL_BUCKETS = 17;

        std::array<std::vector<const ColliderPair *>, TOTAL_BUCKETS> buckets;
    };

    struct PersistentIsland
    {
        int32_t id = -1;
        size_t activeListIndex = SIZE_MAX;
        std::vector<Rigidbody *> bodies;
        std::vector<const ColliderPair *> contacts;
        std::unique_ptr<IslandColorGraph> colorGraph;
        bool maySplit = false;
        bool splitQueued = false;
        bool hasSleepingEndpoint = false;
    };

    struct SolverPreStepJob
    {
        Collision *collision = nullptr;
        Rigidbody *rbA = nullptr;
        Rigidbody *rbB = nullptr;
    };

    int32_t acquirePersistentIsland();
    void recyclePersistentIsland(int32_t islandId);
    void setPersistentIslandActive(PersistentIsland &island, bool active);
    void queuePersistentIslandSplit(PersistentIsland &island);
    void addBodyToPersistentIsland(PersistentIsland &island, Rigidbody *body);
    void detachBodyFromPersistentIsland(Rigidbody *body);
    int32_t mergePersistentIslands(int32_t islandIdA, int32_t islandIdB);
    void removeContactFromPersistentIsland(const ColliderPair *pair);
    void updatePersistentIslandContact(const ColliderPair *pair, bool solve, Collision::CollisionType response,
                                       Rigidbody *rbA, Rigidbody *rbB);
    void splitPersistentIsland(int32_t islandId);
    void splitDirtyIslands();
    void wakePersistentIslandEndpoints();
    static void clearIslandColorGraph(PersistentIsland &island);
    static void insertContactIntoColorGraph(IslandColorGraph &graph, const ColliderPair *pair);
    static void removeContactFromColorGraph(IslandColorGraph &graph, const ColliderPair *pair);
    static void ensureIslandColorGraph(PersistentIsland &island);
    bool solveColoredIsland(PersistentIsland &island, float inverseTimeDelta, bool allowParallel);

    void prepareSolverConstraint(const SolverPreStepJob &job, float inverseTimeDelta,
                                 SolverConstraint &outConstraint) const;
    static void applyWarmStart(const SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB) noexcept;
    static void resolveCollision(SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB) noexcept;
    static void storeConstraintImpulses(const SolverConstraint &constraint) noexcept;
    void solvePersistentIsland(const PersistentIsland &island, float inverseTimeDelta,
                               IslandSolverScratch &scratch);
    bool solvePersistentIslandGroup8(const int32_t *islandIds, size_t groupSize, float inverseTimeDelta,
                                     IslandSolverScratch &scratch);
    void buildIslandWorkUnits();
    void executeIslandWorkUnits(size_t beginUnit, size_t endUnit, float inverseTimeDelta,
                                IslandSolverScratch &scratch);
    void solveConstraintIterations(float inverseTimeDelta);
    void solveConstraintsSerial(float inverseTimeDelta);
    void solveIslands(float inverseTimeDelta, size_t totalContacts);
    void resolveKinematicCollision(const Collision *collision, Rigidbody *rbA, Rigidbody *rbB);
    void resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal,
                                           float correctionDistance);
    PhysicsEvent makePhysicsEvent(PhysicsEvent::EventType type, const ColliderPair *pair,
                                  Collision *collision);

    std::vector<Rigidbody *> registeredBodies;
    std::vector<Rigidbody *> awakeDynamicBodies, movingKinematicBodies, sleepCandidates, pendingBodyChanges;
    std::vector<Collider *> colliderComponents;
    std::vector<PersistentIsland> persistentIslands;
    std::vector<int32_t> freeIslandIds;
    std::vector<int32_t> activeIslandIds;
    std::vector<int32_t> dirtySplitIslandIds;
    std::vector<const ColliderPair *> kinematicSolverPairs;
    std::vector<int32_t> splitAdjHead;
    std::vector<int32_t> splitAdjNext;
    std::vector<uint32_t> splitAdjTo;
    std::vector<int32_t> splitComponentOfBody;
    std::vector<uint32_t> splitQueue;
    std::vector<const ColliderPair *> splitRemainingContacts;
    std::vector<int32_t> splitComponentIslandIds;
    std::vector<SolverPreStepJob> solverPreStepJobs;
    std::vector<SolverConstraint> solverConstraints;
    std::vector<uint32_t> solverBodyLocalIndices;
    std::vector<SolverBodyState> solverBodyStates;
    std::vector<WideBodyVel> solverWideBodyVels;
    std::vector<std::pair<size_t, Rigidbody *>> solverTouchedWritableBodies;
    std::vector<int32_t> bucketedIslandIds;
    std::vector<IslandWorkUnit> islandWorkUnits;
    std::vector<std::vector<const ColliderPair *>> narrowTransitionChunks;
    std::vector<uint64_t> narrowGeometryCounts;
    IslandSolverScratch serialSolverScratch;
    tbb::enumerable_thread_specific<IslandSolverScratch> solverIslandScratch;
    tbb::enumerable_thread_specific<std::vector<Rigidbody *>> parallelBodiesToSleep;

    std::vector<std::pair<size_t, size_t>> solverEndpoints;
    std::vector<size_t> solverIslandJobEnds;

    std::vector<const ColliderPair *> contactPairs;
    std::vector<Rigidbody *> supportDeltaBodies;
    std::vector<PhysicsEvent> pendingPhysicsEvents;

    std::vector<uint8_t> movedBodyChanges;
    std::vector<Collider *> movedFastColliders;
    Grid grid;

    Eigen::Vector2f gravityVector;
    Eigen::Vector2f gravityVectorNormalized;
};
