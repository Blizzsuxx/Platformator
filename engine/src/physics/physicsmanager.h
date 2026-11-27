#pragma once

#include "buildconfig.h"
#include "physics/broadphase/grid.h"
#include "physics/physicssolver_simd.h"
#include "platformator/collider.h"
#include "platformator/collision.h"
#include "platformator/collisiontypes.h"
#include "platformator/rigidbody.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <optional>
#include <type_traits>
#include <utility>
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
    friend class Rigidbody;

public:
    PhysicsManager();
    ~PhysicsManager();

#if PLATFORMATOR_ENABLE_BENCHMARKS
    void integrateMovementForBenchmark(double timeDelta);
#endif
    void stepDynamics(double timeDelta);

    void checkForCollisions();
    void solveDynamics(double timeDelta);
    const Grid &getGrid() const;
    float getGridCellSize() const noexcept
    {
        return grid.getCellSize();
    }
    const std::vector<Collider *> &getColliders() const;

    const std::vector<ColliderPair *> &getContactPairs() const
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
    static constexpr uint32_t UNMAPPED_SOLVER_BODY = std::numeric_limits<uint32_t>::max();
    static constexpr size_t PARALLEL_SOLVER_BATCH_SIZE = 128;
    static constexpr size_t PARALLEL_PRESTEP_THRESHOLD = 256;
    static constexpr size_t PARALLEL_BODY_SUBSTEP_THRESHOLD = 512;

#if PLATFORMATOR_ENABLE_BENCHMARKS
    bool benchmarkSleepingDisabled = false;
#endif
    template <typename HitCallback>
    void traverseRayCandidates(const Eigen::Vector2f &origin, const Eigen::Vector2d &dir, float maxDistance,
                               const double &limit, uint64_t collisionMask, bool includeTriggers,
                               HitCallback &&onHit) const;
    void flushPendingColliderOperations();
    void clearAll();

    static bool hasParallelPhysicsWorkers() noexcept;
    static std::pair<bool, bool> pairSupportFlags(const ColliderPair *pair, const Collision *collision) noexcept
    {
        if (collision == nullptr)
        {
            return {false, false};
        }
        const bool referenceIsA = collision->getReferenceObject() == pair->getObjectA();
        const bool supportsA =
            (collision->supportState &
             (referenceIsA ? Collision::SUPPORTS_REFERENCE : Collision::SUPPORTS_INCIDENT)) != 0;
        const bool supportsB =
            (collision->supportState &
             (referenceIsA ? Collision::SUPPORTS_INCIDENT : Collision::SUPPORTS_REFERENCE)) != 0;
        return {supportsA, supportsB};
    }
    static bool extractSolverPair(ColliderPair *pair, Collision *&outCollision, Rigidbody *&outRbA,
                                  Rigidbody *&outRbB) noexcept
    {
        outCollision = pair->getCollision();
        if (outCollision == nullptr)
        {
            return false;
        }
        Rigidbody *rbA = pair->cachedRbA;
        Rigidbody *rbB = pair->cachedRbB;
        if (rbA != nullptr && rbB != nullptr && !rbA->isRetired() && !rbB->isRetired())
        {
            const bool refIsA = outCollision->getReferenceObject() == pair->getObjectA();
            outRbA = refIsA ? rbA : rbB;
            outRbB = refIsA ? rbB : rbA;
            return true;
        }
        outRbA = outCollision->getReferenceObject()->getAttachedRigidbody();
        outRbB = outCollision->getIncidentObject()->getAttachedRigidbody();
        return outRbA != nullptr && outRbB != nullptr;
    }

    void updateSleepingStates(double timeDelta);
    void broadPhase();
    void narrowPhase();
    void processNarrowPhasePair(ColliderPair *pair);
    void refreshContactParticipation(ColliderPair *pair);
    void refreshSolverParticipation(ColliderPair *pair, bool live);
    void removePair(ColliderPair *pair);
    void reconcilePairSupport(ColliderPair *pair);
    void reconcileSupportContribution(Rigidbody *&contributor, Rigidbody *desired);
    void queueSupportDelta(Rigidbody *body, int delta);
    void flushSupportDeltas();
    void queueBodyParticipationChange(Rigidbody *body);
    void reserveAttachmentWork(Rigidbody *old, Collider *collider);
    void cancelBodyParticipationChange(Rigidbody *body);
    void reconcileBodyParticipation(Rigidbody *body);
    void reconcileChangedBodies();

#if PLATFORMATOR_ENABLE_BENCHMARKS
    size_t countAwakeDynamicBodies() const;
#endif

    void commitMovedBodies(double timeDelta, bool useSolverDeltas);

    struct SoftStepParams
    {
        float h = 0.0f;
        float invH = 0.0f;
        float biasRate = 0.0f;
        float massScale = 1.0f;
        float impulseScale = 0.0f;
        float negMaxPushSpeed = -35.0f;
        float slop = 0.01f;
        float restitutionThreshold = 1.0f;
        size_t subSteps = 4;
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
        uint32_t step = 0;
    };

    struct alignas(16) SolverBodyState
    {
        float vx = 0.0f;
        float vy = 0.0f;
        float w = 0.0f;
        float inverseMass = 0.0f;
        float dpX = 0.0f;
        float dpY = 0.0f;
        float dAngle = 0.0f;
        float inverseMomentOfInertia = 0.0f;
    };
    static_assert(sizeof(SolverBodyState) == 32 && alignof(SolverBodyState) >= 16);
    static_assert(offsetof(SolverBodyState, dpX) == 16);

    static void integrateSolverVelocities(SolverBodyState *states, const SolverBodyForce *forces,
                                          size_t begin, size_t end, float h) noexcept;
    static void integrateSolverPositions(SolverBodyState *states, size_t begin, size_t end, float h) noexcept;
    static void runSoftSteps(const SoftStepParams &params, bool hasRestitution, auto integrateVelocities,
                             auto warmStart, auto solve, auto integratePositions, auto restitution)
    {
        for (size_t substep = 0; substep < params.subSteps; ++substep)
        {
            if (params.h > 0.0f)
                integrateVelocities();
            if (substep == 0)
                warmStart();
            solve(std::true_type{});
            if (params.h > 0.0f)
                integratePositions();
            solve(std::false_type{});
        }
        if (hasRestitution)
            restitution();
    }

    struct WritableSolverBody
    {
        size_t regIndex = SIZE_MAX;
        uint32_t localIndex = 0;
        Rigidbody *body = nullptr;
    };

    struct ScopedSolverBodyMapGuard
    {
        std::vector<uint32_t> &localIndices;
        const std::vector<WritableSolverBody> &writable;

        ~ScopedSolverBodyMapGuard() noexcept
        {
            for (const WritableSolverBody &entry : writable)
            {
                if (entry.regIndex != SIZE_MAX)
                {
                    localIndices[entry.regIndex] = UNMAPPED_SOLVER_BODY;
                }
            }
        }
    };

    struct IslandWorkUnit
    {
        uint32_t begin = 0;
        uint8_t count = 0;
        uint32_t contactWeight = 0;
    };

    struct SolverConstraint
    {
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

    struct BodyPrepCache
    {
        float posX = 0.0f;
        float posY = 0.0f;
        float friction = 0.0f;
        float restitution = 0.0f;
    };

    struct IslandSolverScratch
    {
        std::vector<SolverConstraint> constraints;
        std::vector<SolverBodyState> bodies;
        std::vector<SolverBodyForce> bodyForces;
        std::vector<BodyPrepCache> bodyPrep;
        std::vector<WritableSolverBody> writableBodies;
        std::vector<uint32_t> bodyLocalIndices;
        std::vector<std::pair<Rigidbody *, uint32_t>> kinematicBodies;
        std::vector<platformator_solver_simd::WideConstraint8> wideSteps;
        std::vector<platformator_solver_simd::WideConstraint8Cold> wideCold;
    };

    struct SleepScratch
    {
        std::vector<uint32_t> parent;
        std::vector<uint8_t> rank;
        std::vector<uint8_t> compQuiet;
        std::vector<double> compTimer;
        std::vector<Rigidbody *> toSleep;
    };

    struct IslandColorGraph
    {
        static constexpr uint8_t MAX_PARALLEL_COLORS = 16;
        static constexpr uint8_t OVERFLOW_COLOR = 16;
        static constexpr uint8_t TOTAL_BUCKETS = 17;

        std::array<std::vector<ColliderPair *>, TOTAL_BUCKETS> buckets;
    };

    struct PersistentIsland
    {
        int32_t id = -1;
        size_t activeListIndex = SIZE_MAX;
        std::vector<Rigidbody *> bodies;
        std::vector<ColliderPair *> contacts;
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
        uint32_t bodyIndexA = 0;
        uint32_t bodyIndexB = 0;
    };

    int32_t acquirePersistentIsland();
    void recyclePersistentIsland(int32_t islandId);
    void setPersistentIslandActive(PersistentIsland &island, bool active);
    void queuePersistentIslandSplit(PersistentIsland &island);
    void addBodyToPersistentIsland(PersistentIsland &island, Rigidbody *body);
    void detachBodyFromPersistentIsland(Rigidbody *body);
    int32_t mergePersistentIslands(int32_t islandIdA, int32_t islandIdB);
    void removeContactFromPersistentIsland(ColliderPair *pair);
    void updatePersistentIslandContact(ColliderPair *pair, bool solve, Collision::CollisionType response,
                                       Rigidbody *rbA, Rigidbody *rbB);
    void splitPersistentIsland(int32_t islandId);
    void splitDirtyIslands();
    void wakePersistentIslandEndpoints();
    static void clearIslandColorGraph(PersistentIsland &island);
    static void insertContactIntoColorGraph(IslandColorGraph &graph, ColliderPair *pair);
    static void removeContactFromColorGraph(IslandColorGraph &graph, ColliderPair *pair);
    static void ensureIslandColorGraph(PersistentIsland &island);
    SoftStepParams makeSoftStepParams(double timeDelta) const noexcept;
    SolverBodyForce makeBodyForce(const Rigidbody *rb) const noexcept;
    void commitWritableBodies(const std::vector<WritableSolverBody> &writable,
                              const std::vector<SolverBodyState> &states) noexcept;
    bool solveColoredIsland(PersistentIsland &island, const SoftStepParams &params, bool allowParallel);

    static bool constraintHasActiveRestitution(const SolverConstraint &constraint, float restitutionThreshold) noexcept
    {
        if (constraint.solverRestitution <= 0.0f)
        {
            return false;
        }
        for (uint8_t i = 0; i < constraint.pointCount; ++i)
        {
            if (constraint.relativeVelocity[i] < -restitutionThreshold)
            {
                return true;
            }
        }
        return false;
    }

    void prepareSolverConstraint(const SolverPreStepJob &job, const SoftStepParams &params,
                                 SolverConstraint &outConstraint) const;
    static void applyWarmStart(const SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB) noexcept;
    template <bool UseBias>
    static void solveConstraintSoft(SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB,
                                    const SoftStepParams &params) noexcept;
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
    void resolveKinematicPairs();
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
    std::vector<ColliderPair *> kinematicSolverPairs;
    std::vector<int32_t> splitAdjHead;
    std::vector<int32_t> splitAdjNext;
    std::vector<uint32_t> splitAdjTo;
    std::vector<int32_t> splitComponentOfBody;
    std::vector<uint32_t> splitQueue;
    std::vector<ColliderPair *> splitRemainingContacts;
    std::vector<Rigidbody *> splitOldBodies;
    std::vector<size_t> splitComponentContactCounts;
    std::vector<int32_t> splitContactComponents;
    std::vector<int32_t> splitComponentIslandIds;
    std::vector<SolverPreStepJob> solverPreStepJobs;
    std::vector<SolverConstraint> solverConstraints;
    std::vector<uint32_t> solverBodyLocalIndices;
    std::vector<SolverBodyState> solverBodyStates;
    std::vector<SolverBodyForce> solverBodyForces;
    std::vector<SolverBodyDelta> solverBodyDeltas;
    uint32_t solverStep = 0;
    std::vector<WritableSolverBody> solverTouchedWritableBodies;
    std::array<std::vector<platformator_solver_simd::WideConstraint8>, IslandColorGraph::MAX_PARALLEL_COLORS>
        colorWideBlocks;
    std::array<std::vector<platformator_solver_simd::WideConstraint8Cold>, IslandColorGraph::MAX_PARALLEL_COLORS>
        colorWideColdBlocks;
    std::array<std::vector<SolverConstraint>, IslandColorGraph::MAX_PARALLEL_COLORS> colorTailConstraints;
    std::vector<SolverConstraint> overflowConstraints;
    std::vector<int32_t> bucketedIslandIds;
    std::vector<IslandWorkUnit> islandWorkUnits;
    std::vector<std::vector<ColliderPair *>> narrowTransitionChunks;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    std::vector<uint64_t> narrowGeometryCounts;
#endif
    IslandSolverScratch serialSolverScratch;
    tbb::enumerable_thread_specific<IslandSolverScratch> solverIslandScratch;
    SleepScratch serialSleepScratch;
    tbb::enumerable_thread_specific<SleepScratch> parallelSleepScratch;
    std::vector<Rigidbody *> bodiesToSleepScratch;

    std::vector<size_t> solverIslandJobEnds;

    std::vector<ColliderPair *> contactPairs;
    std::vector<Rigidbody *> supportDeltaBodies;
    std::vector<PhysicsEvent> pendingPhysicsEvents;

    std::vector<uint8_t> movedBodyChanges;
    std::vector<Collider *> movedFastColliders;
    Grid grid;

    Eigen::Vector2f gravityVector;
    Eigen::Vector2f gravityVectorNormalized;
};
