#pragma once

#include "buildconfig.h"
#include "physics/broadphase/grid.h"
#include "physics/disjointset.h"
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
    static bool hasParallelPhysicsWorkers() noexcept;

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

    // Pair-ordered (handle order) endpoint rigidbodies using cached island pointers when valid.
    static std::pair<Rigidbody *, Rigidbody *> pairRigidbodies(const ColliderPair *pair) noexcept
    {
        Rigidbody *rbA = pair->cachedRbA;
        Rigidbody *rbB = pair->cachedRbB;
        if (rbA != nullptr && rbB != nullptr && !rbA->isRetired() && !rbB->isRetired())
        {
            return {rbA, rbB};
        }
        return {pair->getObjectA()->getAttachedRigidbody(), pair->getObjectB()->getAttachedRigidbody()};
    }

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

    // Reference/incident-ordered contact endpoints (collision->getNormal() points refBody -> incBody).
    struct SolverContactPair
    {
        Collision *collision = nullptr;
        Rigidbody *refBody = nullptr;
        Rigidbody *incBody = nullptr;
    };

    static bool extractSolverContactPair(ColliderPair *pair, SolverContactPair &out) noexcept
    {
        out.collision = pair->getCollision();
        if (out.collision == nullptr)
        {
            return false;
        }
        const auto [rbA, rbB] = pairRigidbodies(pair);
        if (rbA == nullptr || rbB == nullptr)
        {
            return false;
        }
        const bool refIsA = out.collision->getReferenceObject() == pair->getObjectA();
        out.refBody = refIsA ? rbA : rbB;
        out.incBody = refIsA ? rbB : rbA;
        return true;
    }

    // ------------------------------------------------------------------------
    // Narrow-Phase, Support & Body Participation
    // ------------------------------------------------------------------------
    void broadPhase();
    void narrowPhase();
    void processNarrowPhasePair(ColliderPair *pair);
    bool needsNarrowPhaseTransition(const ColliderPair *pair, bool noPendingBodyChanges) const noexcept;
    void applyNarrowPhaseTransition(ColliderPair *pair);
    void emitNarrowPhaseDebugDraw() const;
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
    void updateSleepingStates(double timeDelta);

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

    struct alignas(16) SolverBodyForce
    {
        float accelX = 0.0f;
        float accelY = 0.0f;
        float angAccel = 0.0f;
        float pad = 0.0f;
    };
    static_assert(sizeof(SolverBodyForce) == 16 && alignof(SolverBodyForce) >= 16);

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
        uint32_t localIndex = 0;
        Rigidbody *body = nullptr;
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
        float rnA[2] = {0.0f, 0.0f};
        float rnB[2] = {0.0f, 0.0f};
        float rtA[2] = {0.0f, 0.0f};
        float rtB[2] = {0.0f, 0.0f};
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

    static SolverBodyState makeDynamicBodyState(const Rigidbody *rb) noexcept
    {
        const auto &v = rb->getVelocity();
        return SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), rb->getInverseMass(), 0.0f, 0.0f, 0.0f,
                               rb->getInverseMomentOfInertia()};
    }
    static SolverBodyState makeKinematicBodyState(const Rigidbody *rb) noexcept
    {
        const auto &v = rb->getVelocity();
        return SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    }
    static BodyPrepCache makeBodyPrepCache(const Rigidbody *rb) noexcept
    {
        const auto &pos = rb->getGameObject()->worldPosition;
        return BodyPrepCache{pos.x(), pos.y(), rb->getFriction(), rb->getRestitution()};
    }
    static uint32_t findOrAddKinematicBody(Rigidbody *rb, std::vector<SolverBodyState> &bodies,
                                           std::vector<SolverBodyForce> &forces,
                                           std::vector<std::pair<Rigidbody *, uint32_t>> &kinematics,
                                           std::vector<BodyPrepCache> *prep = nullptr)
    {
        for (const auto &[kinBody, kinSlot] : kinematics)
        {
            if (kinBody == rb)
            {
                return kinSlot;
            }
        }
        const uint32_t localIdx = static_cast<uint32_t>(bodies.size());
        bodies.push_back(makeKinematicBodyState(rb));
        forces.push_back(SolverBodyForce{});
        if (prep != nullptr)
        {
            prep->push_back(makeBodyPrepCache(rb));
        }
        kinematics.emplace_back(rb, localIdx);
        return localIdx;
    }
    static bool packWideContactLane(platformator_solver_simd::WideConstraint8 &wide,
                                    platformator_solver_simd::WideConstraint8Cold &cold, size_t lane,
                                    Collision *collision, const Rigidbody *rbA, const Rigidbody *rbB, uint32_t idxA,
                                    uint32_t idxB, const SolverBodyState *bodies, const SolverBodyForce *bodyForces,
                                    const BodyPrepCache *bodyPrep, const SoftStepParams &params) noexcept;

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

        void clear() noexcept
        {
            constraints.clear();
            bodies.clear();
            bodyForces.clear();
            bodyPrep.clear();
            writableBodies.clear();
            bodyLocalIndices.clear();
            kinematicBodies.clear();
            wideSteps.clear();
            wideCold.clear();
        }
    };

    struct SleepScratch
    {
        platformator_detail::DisjointSet dsu;
        std::vector<uint8_t> compQuiet;
        std::vector<double> compTimer;
        std::vector<Rigidbody *> toSleep;

        void clear() noexcept
        {
            dsu.clear();
            compQuiet.clear();
            compTimer.clear();
            toSleep.clear();
        }
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
        int32_t id = ColliderPair::INVALID_ISLAND_ID;
        size_t activeListIndex = SIZE_MAX;
        std::vector<Rigidbody *> bodies;
        std::vector<ColliderPair *> contacts;
        std::unique_ptr<IslandColorGraph> colorGraph;
        uint16_t splitCooldown = 0;
        bool maySplit = false;
        bool splitQueued = false;
        bool hasSleepingEndpoint = false;
    };

    struct IslandSplitScratch
    {
        platformator_detail::DisjointSet dsu;
        std::vector<int32_t> componentOfBody;
        std::vector<ColliderPair *> remainingContacts;
        std::vector<Rigidbody *> oldBodies;
        std::vector<size_t> componentContactCounts;
        std::vector<int32_t> contactComponents;
        std::vector<int32_t> componentIslandIds;

        void clear() noexcept
        {
            dsu.clear();
            componentOfBody.clear();
            remainingContacts.clear();
            oldBodies.clear();
            componentContactCounts.clear();
            contactComponents.clear();
            componentIslandIds.clear();
        }
    };

    struct SolverPreStepJob
    {
        Collision *collision = nullptr;
        Rigidbody *rbA = nullptr;
        Rigidbody *rbB = nullptr;
        uint32_t bodyIndexA = 0;
        uint32_t bodyIndexB = 0;
    };

    struct ColoredSolverScratch
    {
        std::vector<SolverPreStepJob> preStepJobs;
        std::vector<SolverConstraint> constraints;
        std::vector<uint32_t> bodyLocalIndices;
        std::vector<SolverBodyState> bodyStates;
        std::vector<SolverBodyForce> bodyForces;
        std::vector<BodyPrepCache> bodyPrep;
        std::vector<std::pair<Rigidbody *, uint32_t>> kinematicBodies;
        std::vector<WritableSolverBody> writableBodies;
        std::array<std::vector<platformator_solver_simd::WideConstraint8>, IslandColorGraph::MAX_PARALLEL_COLORS>
            colorWideBlocks;
        std::array<std::vector<platformator_solver_simd::WideConstraint8Cold>, IslandColorGraph::MAX_PARALLEL_COLORS>
            colorWideColdBlocks;
        std::array<std::vector<SolverConstraint>, IslandColorGraph::MAX_PARALLEL_COLORS> colorTailConstraints;
        std::vector<SolverConstraint> overflowConstraints;

        void clear() noexcept
        {
            preStepJobs.clear();
            constraints.clear();
            bodyLocalIndices.clear();
            bodyStates.clear();
            bodyForces.clear();
            bodyPrep.clear();
            kinematicBodies.clear();
            writableBodies.clear();
            for (auto &blocks : colorWideBlocks)
                blocks.clear();
            for (auto &coldBlocks : colorWideColdBlocks)
                coldBlocks.clear();
            for (auto &tails : colorTailConstraints)
                tails.clear();
            overflowConstraints.clear();
        }
    };

    struct IslandScheduleScratch
    {
        std::vector<int32_t> bucketedIslandIds;
        std::vector<IslandWorkUnit> islandWorkUnits;
        std::vector<size_t> jobEnds;

        void clear() noexcept
        {
            bucketedIslandIds.clear();
            islandWorkUnits.clear();
            jobEnds.clear();
        }
    };

    struct NarrowPhaseScratch
    {
        std::vector<std::vector<ColliderPair *>> transitionChunks;
#if PLATFORMATOR_ENABLE_WORK_PROBES
        std::vector<uint64_t> geometryCounts;
#endif
        void clear() noexcept
        {
            for (auto &chunk : transitionChunks)
                chunk.clear();
#if PLATFORMATOR_ENABLE_WORK_PROBES
            geometryCounts.clear();
#endif
        }
    };

    struct MoveCommitScratch
    {
        std::vector<uint8_t> bodyChanges;
        std::vector<Collider *> fastColliders;

        void clear() noexcept
        {
            bodyChanges.clear();
            fastColliders.clear();
        }
    };

    // ------------------------------------------------------------------------
    // Persistent Island & Color-Graph Management
    // ------------------------------------------------------------------------
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
    int32_t findIslandComponents(PersistentIsland &island);
    void partitionIslandByComponents(int32_t islandId, int32_t componentCount);
    void splitPersistentIsland(int32_t islandId);
    void splitDirtyIslands();
    void wakePersistentIslandEndpoints();
    static void clearIslandColorGraph(PersistentIsland &island);
    static void insertContactIntoColorGraph(IslandColorGraph &graph, ColliderPair *pair);
    static void removeContactFromColorGraph(IslandColorGraph &graph, ColliderPair *pair);
    static void ensureIslandColorGraph(PersistentIsland &island);

    // ------------------------------------------------------------------------
    // Sleeping Evaluation
    // ------------------------------------------------------------------------
    static size_t evaluateIslandSleep(const PersistentIsland &island, double timeDelta, SleepScratch &scratch,
                                      std::vector<Rigidbody *> &toSleep);
    static void evaluateMixedIslandSleep(const PersistentIsland &island, double timeDelta, SleepScratch &scratch,
                                         std::vector<Rigidbody *> &toSleep);

    // ------------------------------------------------------------------------
    // Soft-Contact Solver (Scalar, 8-Wide Island Group, & Graph-Colored)
    // ------------------------------------------------------------------------
    SoftStepParams makeSoftStepParams(double timeDelta) const noexcept;
    SolverBodyForce makeBodyForce(const Rigidbody *rb) const noexcept;
    void commitWritableBodies(const std::vector<WritableSolverBody> &writable,
                              const std::vector<SolverBodyState> &states) noexcept;
    void populateColoredIslandBodies(const PersistentIsland &island);
    bool solveColoredIslandWideAVX2(const PersistentIsland &island, const SoftStepParams &params, bool allowParallel);
    bool solveColoredIslandScalar(const PersistentIsland &island, const SoftStepParams &params, bool allowParallel,
                                  bool hasMovingKinematic);
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
    static void applyRestitutionSoft(const SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB,
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
    void resolveKinematicCollision(const Collision *collision, Rigidbody *refBody, Rigidbody *incBody);
    void resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal,
                                           float correctionDistance);
    PhysicsEvent makePhysicsEvent(PhysicsEvent::EventType type, const ColliderPair *pair,
                                  Collision *collision);

    // ------------------------------------------------------------------------
    // Registered Entities & Persistent World State
    // ------------------------------------------------------------------------
    std::vector<Rigidbody *> registeredBodies;
    std::vector<Rigidbody *> awakeDynamicBodies;
    std::vector<Rigidbody *> movingKinematicBodies;
    std::vector<Rigidbody *> sleepCandidates;
    std::vector<Rigidbody *> pendingBodyChanges;
    std::vector<Collider *> colliderComponents;
    std::vector<ColliderPair *> contactPairs;
    std::vector<Rigidbody *> supportDeltaBodies;
    std::vector<PhysicsEvent> pendingPhysicsEvents;

    // Persistent island & kinematic solver membership
    std::vector<PersistentIsland> persistentIslands;
    std::vector<int32_t> freeIslandIds;
    std::vector<int32_t> activeIslandIds;
    std::vector<int32_t> dirtySplitIslandIds;
    std::vector<ColliderPair *> kinematicSolverPairs;

    // Reusable per-step scratch storage (retains capacity across frames)
    IslandSplitScratch splitScratch;
    ColoredSolverScratch coloredScratch;
    IslandScheduleScratch scheduleScratch;
    NarrowPhaseScratch narrowScratch;
    MoveCommitScratch moveScratch;
    std::vector<SolverBodyDelta> solverBodyDeltas;
    uint32_t solverStep = 0;
    IslandSolverScratch serialSolverScratch;
    tbb::enumerable_thread_specific<IslandSolverScratch> solverIslandScratch;
    SleepScratch serialSleepScratch;
    tbb::enumerable_thread_specific<SleepScratch> parallelSleepScratch;
    std::vector<Rigidbody *> bodiesToSleepScratch;

    Grid grid;

    Eigen::Vector2f gravityVector;
    Eigen::Vector2f gravityVectorNormalized;
};
