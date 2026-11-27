#pragma once

#include "buildconfig.h"
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
    void stepDynamics(double timeDelta);

    void checkForCollisions();
    void resolveCollisions(double timeDelta);
    const Grid &getGrid() const;
    float getGridCellSize() const noexcept
    {
        return grid.getCellSize();
    }
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
#if PLATFORMATOR_ENABLE_BENCHMARKS
    void disableSleepingForBenchmark()
    {
        benchmarkSleepingDisabled = true;
    }
#endif
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
#if PLATFORMATOR_ENABLE_BENCHMARKS
    bool benchmarkSleepingDisabled = false;
#endif
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

    void commitMovedBodies(double timeDelta, bool useSolverDeltas);

    struct SoftStepParams
    {
        float dt = 0.0f;
        float h = 0.0f;
        float invH = 0.0f;
        float biasRate = 0.0f;
        float massScale = 1.0f;
        float impulseScale = 0.0f;
        float negMaxPushSpeed = -35.0f;
        float slop = 0.01f;
        float restitutionThreshold = 1.0f;
        size_t subSteps = 4;
        bool integrateForces = true;
        bool commitPositions = true;
    };

    struct SolverBodyForce
    {
        float accelX = 0.0f;
        float accelY = 0.0f;
        float angAccel = 0.0f;
        uint8_t isDynamic = 0;
    };

    struct SolverBodyDelta
    {
        float dx = 0.0f;
        float dy = 0.0f;
        float dAngle = 0.0f;
    };

    struct SolverBodyState
    {
        Eigen::Vector2f velocity = Eigen::Vector2f::Zero();
        float angularVelocity = 0.0f;
        Eigen::Vector2f deltaPosition = Eigen::Vector2f::Zero();
        float deltaAngle = 0.0f;
        float inverseMass = 0.0f;
        float inverseMomentOfInertia = 0.0f;
    };

    struct alignas(16) WideBodyState
    {
        float vx = 0.0f;
        float vy = 0.0f;
        float w = 0.0f;
        float pad0 = 0.0f;
        float dpX = 0.0f;
        float dpY = 0.0f;
        float dAngle = 0.0f;
        float pad1 = 0.0f;
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
        float baseSeparation[2] = {0.0f, 0.0f};
        float relativeVelocity[2] = {0.0f, 0.0f};
        float accumulatedNormalImpulse[2] = {0.0f, 0.0f};
        float accumulatedTangentImpulse[2] = {0.0f, 0.0f};
        float solverFriction = 0.0f;
        float solverRestitution = 0.0f;
        uint8_t pointCount = 0;
    };

    struct IslandSolverScratch
    {
        std::vector<SolverConstraint> constraints;
        std::vector<SolverBodyState> bodies;
        std::vector<WideBodyState> wideBodies;
        std::vector<SolverBodyForce> bodyForces;
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
        uint16_t splitCooldown = 0;
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
    SoftStepParams makeSoftStepParams(double timeDelta, bool integrateForces, bool commitPositions) const noexcept;
    SolverBodyForce makeBodyForce(const Rigidbody *rb) const noexcept;
    bool solveColoredIsland(PersistentIsland &island, const SoftStepParams &params, bool allowParallel);

    void prepareSolverConstraint(const SolverPreStepJob &job, const SoftStepParams &params,
                                 SolverConstraint &outConstraint) const;
    static void applyWarmStart(const SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB) noexcept;
    static void solveConstraintSoft(SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB,
                                    const SoftStepParams &params, bool useBias) noexcept;
    static void applyRestitutionSoft(SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB,
                                     const SoftStepParams &params) noexcept;
    static void storeConstraintImpulses(const SolverConstraint &constraint) noexcept;
    void solvePersistentIsland(const PersistentIsland &island, const SoftStepParams &params,
                               IslandSolverScratch &scratch);
    bool solvePersistentIslandGroup8(const int32_t *islandIds, size_t groupSize, const SoftStepParams &params,
                                     IslandSolverScratch &scratch);
    void buildIslandWorkUnits();
    void executeIslandWorkUnits(size_t beginUnit, size_t endUnit, const SoftStepParams &params,
                                IslandSolverScratch &scratch);
    void solveConstraintIterations(const SoftStepParams &params);
    void solveConstraintsSerial(const SoftStepParams &params);
    void solveIslands(const SoftStepParams &params, size_t totalContacts);
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
    std::vector<WideBodyState> solverWideBodyStates;
    std::vector<SolverBodyForce> solverBodyForces;
    std::vector<SolverBodyDelta> solverBodyDeltas;
    std::vector<uint8_t> solverBodyIntegrated;
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
