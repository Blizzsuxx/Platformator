#include "physics/physicsmanager.h"

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"

#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

// ============================================================================
// Soft-Step Parameters & Body Integration Helpers
// ============================================================================

PhysicsManager::SoftStepParams PhysicsManager::makeSoftStepParams(double timeDelta) const noexcept
{
    SoftStepParams params;
    params.subSteps = COLLISION_SOLVER_SUBSTEPS;
    const float dt = timeDelta > 0.0 ? static_cast<float>(timeDelta) : 0.0f;
    params.h = dt > 0.0f ? dt / static_cast<float>(params.subSteps) : 0.0f;
    params.invH = params.h > 0.0f ? 1.0f / params.h : 0.0f;

    if (params.h > 0.0f)
    {
        const float contactHertz = std::min(CONTACT_HERTZ, 0.25f * params.invH);
        const float omega = 2.0f * static_cast<float>(std::numbers::pi) * contactHertz;
        const float a1 = 2.0f * CONTACT_DAMPING_RATIO + params.h * omega;
        const float a2 = params.h * omega * a1;
        params.biasRate = a1 > 0.0f ? (omega / a1) : 0.0f;
        params.impulseScale = 1.0f / (1.0f + a2);
        params.massScale = a2 / (1.0f + a2);
    }
    else
    {
        params.biasRate = 0.0f;
        params.impulseScale = 0.0f;
        params.massScale = 1.0f;
    }

    params.negMaxPushSpeed = -std::max(30.0f, gravityVector.norm() * 0.35f);
    params.slop = COLLISION_ALLOWED_PENETRATION;
    params.restitutionThreshold = RESTITUTION_VELOCITY_THRESHOLD;
    return params;
}

PhysicsManager::SolverBodyForce PhysicsManager::makeBodyForce(const Rigidbody *rb) const noexcept
{
    const Eigen::Vector2f accel =
        rb->getForce() * rb->getInverseMass() + (rb->getGravity() ? gravityVector : Eigen::Vector2f::Zero());
    const float angAccel = rb->getTorque() * rb->getInverseMomentOfInertia();
    return SolverBodyForce{accel.x(), accel.y(), angAccel, 0.0f};
}

void PhysicsManager::commitWritableBodies(const std::vector<WritableSolverBody> &writable,
                                          const std::vector<SolverBodyState> &states) noexcept
{
    for (const WritableSolverBody &entry : writable)
    {
        const SolverBodyState &state = states[entry.localIndex];
        entry.body->velocity = Eigen::Vector2f(state.vx, state.vy);
        entry.body->angularVelocity = state.w;
        const uint32_t awakeIndex = entry.body->awakeDynamicIndex;
        if (awakeIndex != Rigidbody::INVALID_INDEX)
        {
            solverBodyDeltas[awakeIndex] = SolverBodyDelta{state.dpX, state.dpY, state.dAngle, solverStep};
        }
    }
}

void PhysicsManager::integrateSolverVelocities(SolverBodyState *states, const SolverBodyForce *forces,
                                               size_t begin, size_t end, float h) noexcept
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    const __m128 hVec = _mm_setr_ps(h, h, h, 0.0f);
    for (size_t i = begin; i < end; ++i)
    {
        const __m128 v = _mm_load_ps(&states[i].vx);
        const __m128 a = _mm_load_ps(&forces[i].accelX);
        _mm_store_ps(&states[i].vx, _mm_add_ps(v, _mm_mul_ps(a, hVec)));
    }
#else
    for (size_t i = begin; i < end; ++i)
    {
        states[i].vx += forces[i].accelX * h;
        states[i].vy += forces[i].accelY * h;
        states[i].w += forces[i].angAccel * h;
    }
#endif
}

void PhysicsManager::integrateSolverPositions(SolverBodyState *states, size_t begin, size_t end, float h) noexcept
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    const __m128 hVec = _mm_setr_ps(h, h, h, 0.0f);
    for (size_t i = begin; i < end; ++i)
    {
        const __m128 v = _mm_load_ps(&states[i].vx);
        const __m128 dp = _mm_load_ps(&states[i].dpX);
        _mm_store_ps(&states[i].dpX, _mm_add_ps(dp, _mm_mul_ps(v, hVec)));
    }
#else
    for (size_t i = begin; i < end; ++i)
    {
        states[i].dpX += states[i].vx * h;
        states[i].dpY += states[i].vy * h;
        states[i].dAngle += states[i].w * h;
    }
#endif
}

// ============================================================================
// Dynamics Entry Points
// ============================================================================

void PhysicsManager::stepDynamics(double timeDelta)
{
    solveDynamics(timeDelta);
    commitMovedBodies(timeDelta, /*useSolverDeltas=*/true);
    reconcileChangedBodies();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::solveDynamics(double timeDelta)
{
    PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);
    resolveKinematicPairs();
    splitDirtyIslands();
    wakePersistentIslandEndpoints();
    reconcileChangedBodies();

    PLATFORMATOR_WORK_ADD(forceBodies, awakeDynamicBodies.size());
    // Only deltas written during this solve may be committed by the movement pass.
    // Contact loss and reused registration slots both fall back to fresh integration.
    if (++solverStep == 0)
    {
        for (auto &delta : solverBodyDeltas)
        {
            delta.step = 0;
        }
        ++solverStep;
    }
    if (!activeIslandIds.empty())
    {
        if (solverBodyDeltas.size() < awakeDynamicBodies.size())
        {
            PLATFORMATOR_WORK_ADD(solverDeltaSlotsInitialized, awakeDynamicBodies.size() - solverBodyDeltas.size());
            solverBodyDeltas.resize(awakeDynamicBodies.size());
        }
        solveConstraintIterations(makeSoftStepParams(timeDelta));
    }
}

// ============================================================================
// Island Work-Unit Bucketing & Dispatch
// ============================================================================

void PhysicsManager::buildIslandWorkUnits()
{
    auto &bucketed = scheduleScratch.bucketedIslandIds;
    auto &workUnits = scheduleScratch.islandWorkUnits;
    const size_t n = activeIslandIds.size();
    bucketed.resize(n);
    workUnits.clear();
    if (n == 0)
    {
        return;
    }

    // O(N) stable counting sort by contact count (clamped to 128) so 8-wide SIMD island groups
    // have nearly identical lengths while preserving exact deterministic order within each bucket.
    constexpr size_t MAX_BUCKET = 128;
    uint32_t counts[MAX_BUCKET + 1] = {};
    for (int32_t islandId : activeIslandIds)
    {
        const size_t c = std::min<size_t>(persistentIslands[static_cast<size_t>(islandId)].contacts.size(), MAX_BUCKET);
        ++counts[c];
    }
    uint32_t offsets[MAX_BUCKET + 1];
    uint32_t running = 0;
    for (size_t b = 0; b <= MAX_BUCKET; ++b)
    {
        offsets[b] = running;
        running += counts[b];
    }
    for (int32_t islandId : activeIslandIds)
    {
        const size_t c = std::min<size_t>(persistentIslands[static_cast<size_t>(islandId)].contacts.size(), MAX_BUCKET);
        bucketed[offsets[c]++] = islandId;
    }

    const bool useAVX2 = platformator_solver_simd::cpuSupportsAVX2();
    workUnits.reserve(n);
    size_t idx = 0;
    while (idx < n)
    {
        const size_t remaining = n - idx;
        if (useAVX2 && remaining >= 4)
        {
            const size_t groupSize = std::min<size_t>(8, remaining);
            size_t minCount = SIZE_MAX;
            size_t maxCount = 0;
            size_t sumCount = 0;
            for (size_t k = 0; k < groupSize; ++k)
            {
                const size_t c = persistentIslands[static_cast<size_t>(bucketed[idx + k])].contacts.size();
                minCount = std::min(minCount, c);
                maxCount = std::max(maxCount, c);
                sumCount += c;
            }
            if (minCount >= 2 && maxCount <= minCount * 2 + 12)
            {
                workUnits.push_back(IslandWorkUnit{static_cast<uint32_t>(idx), static_cast<uint8_t>(groupSize),
                                                   static_cast<uint32_t>(sumCount)});
                idx += groupSize;
                continue;
            }
        }
        const size_t c = persistentIslands[static_cast<size_t>(bucketed[idx])].contacts.size();
        workUnits.push_back(IslandWorkUnit{static_cast<uint32_t>(idx), 1, static_cast<uint32_t>(c)});
        ++idx;
    }
}

void PhysicsManager::executeIslandWorkUnits(size_t beginUnit, size_t endUnit, const SoftStepParams &params,
                                            IslandSolverScratch &scratch)
{
    const auto &bucketed = scheduleScratch.bucketedIslandIds;
    const auto &workUnits = scheduleScratch.islandWorkUnits;
    for (size_t u = beginUnit; u < endUnit; ++u)
    {
        const IslandWorkUnit &unit = workUnits[u];
        if (unit.count > 1)
        {
            if (solvePersistentIslandGroup8(bucketed.data() + unit.begin, unit.count, params, scratch))
            {
                continue;
            }
        }
        for (size_t k = 0; k < unit.count; ++k)
        {
            const int32_t islandId = bucketed[unit.begin + k];
            solvePersistentIsland(persistentIslands[static_cast<size_t>(islandId)], params, scratch);
        }
    }
}

void PhysicsManager::solveConstraintIterations(const SoftStepParams &params)
{
    if (activeIslandIds.empty())
    {
        return;
    }

    size_t totalContacts = 0;
    size_t largestIslandContacts = 0;
    for (int32_t islandId : activeIslandIds)
    {
        const size_t count = persistentIslands[static_cast<size_t>(islandId)].contacts.size();
        totalContacts += count;
        largestIslandContacts = std::max(largestIslandContacts, count);
    }

    constexpr size_t parallelConstraintThreshold = 256;
    constexpr size_t orderedConstraintThreshold = 256;
    const bool allowParallel = totalContacts >= parallelConstraintThreshold && hasParallelPhysicsWorkers();

    if (largestIslandContacts >= orderedConstraintThreshold && largestIslandContacts > totalContacts / 2)
    {
        bool anyColored = false;
        for (int32_t islandId : activeIslandIds)
        {
            PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
            if (island.contacts.size() >= orderedConstraintThreshold &&
                solveColoredIsland(island, params, allowParallel))
            {
                anyColored = true;
                continue;
            }
            solvePersistentIsland(island, params, serialSolverScratch);
        }
        if (anyColored && allowParallel)
        {
            PLATFORMATOR_WORK_ADD(solverOrderedBatchDispatches, 1);
        }
        return;
    }

    if (allowParallel)
    {
        solveIslands(params, totalContacts);
        return;
    }

    solveConstraintsSerial(params);
}

void PhysicsManager::solveConstraintsSerial(const SoftStepParams &params)
{
    buildIslandWorkUnits();
    executeIslandWorkUnits(0, scheduleScratch.islandWorkUnits.size(), params, serialSolverScratch);
}

void PhysicsManager::solveIslands(const SoftStepParams &params, size_t totalContacts)
{
    buildIslandWorkUnits();
    const auto workers = std::min(tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism),
                                  static_cast<size_t>(tbb::this_task_arena::max_concurrency()));
    const size_t target = std::max(size_t{1}, totalContacts / workers / 4);

    auto &jobEnds = scheduleScratch.jobEnds;
    const auto &workUnits = scheduleScratch.islandWorkUnits;
    jobEnds.clear();
    size_t jobSize = 0;
    const size_t unitCount = workUnits.size();
    for (size_t i = 0; i < unitCount; ++i)
    {
        const size_t count = workUnits[i].contactWeight;
        if (jobSize != 0 && jobSize + count > target)
        {
            jobEnds.push_back(i);
            jobSize = 0;
        }
        jobSize += count;
        PLATFORMATOR_WORK_SET(solverLargestIslandJob,
                              std::max(platformator_test::work.solverLargestIslandJob, jobSize));
    }
    jobEnds.push_back(unitCount);
    PLATFORMATOR_WORK_ADD(solverIslandJobs, jobEnds.size());

    // A simulation step must finish applying impulses even if the caller's task group is cancelled.
    tbb::task_group_context context(tbb::task_group_context::isolated);
    tbb::parallel_for(
        size_t{0}, jobEnds.size(),
        [&](size_t job) {
            auto &scratch = solverIslandScratch.local();
            const size_t begin = job == 0 ? 0 : jobEnds[job - 1];
            const size_t end = jobEnds[job];
            executeIslandWorkUnits(begin, end, params, scratch);
        },
        context);
}

// ============================================================================
// Kinematic Collision Resolution
// ============================================================================

void PhysicsManager::resolveKinematicPairs()
{
    for (ColliderPair *pair : kinematicSolverPairs)
    {
        SolverContactPair contact;
        if (extractSolverContactPair(pair, contact))
        {
            resolveKinematicCollision(contact.collision, contact.refBody, contact.incBody);
        }
    }
}

void PhysicsManager::resolveKinematicCollision(const Collision *collision, Rigidbody *refBody, Rigidbody *incBody)
{
    const bool referenceIsKinematic = refBody->getBodyType() == BodyType::KINEMATIC;
    const bool incidentIsKinematic = incBody->getBodyType() == BodyType::KINEMATIC;

    float maxPenetrationDepth = 0.0f;
    const ClipPointsWithData &contactPoints = collision->getContactPoints();
    for (size_t i = 0; i < contactPoints.count; ++i)
    {
        maxPenetrationDepth = std::max(maxPenetrationDepth, -contactPoints.points[i].separation);
    }

    const float correctionDistance = std::max(0.0f, maxPenetrationDepth - COLLISION_ALLOWED_PENETRATION);
    float perBodyCorrection = correctionDistance;
    if (referenceIsKinematic && incidentIsKinematic)
    {
        perBodyCorrection *= 0.5f;
    }

    const Eigen::Vector2f &normal = collision->getNormal();
    if (referenceIsKinematic)
    {
        resolveKinematicBodyAgainstNormal(refBody, normal, perBodyCorrection);
    }
    if (incidentIsKinematic)
    {
        resolveKinematicBodyAgainstNormal(incBody, -normal, perBodyCorrection);
    }
}

void PhysicsManager::resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal,
                                                       float correctionDistance)
{
    const Eigen::Vector2f velocity = rigidBody->getVelocity();
    const float normalSpeed = velocity.dot(approachNormal);
    if (normalSpeed > 0.0f)
    {
        rigidBody->setVelocity(velocity - normalSpeed * approachNormal);
    }

    if (correctionDistance > 0.0f)
    {
        GameObject *gameObject = rigidBody->getGameObject();
        gameObject->position -= correctionDistance * approachNormal;
        platformator_detail::RuntimeAccess::gameManager().queueTransform(gameObject, false);
    }
}
