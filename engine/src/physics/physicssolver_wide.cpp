#include "physics/physicsmanager.h"

#include "physics/physicstaskcontext.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// ============================================================================
// 8-Wide AVX2 Independent Island-Group Soft-Step Solver
// ============================================================================

bool PhysicsManager::packWideContactLane(platformator_solver_simd::WideConstraint8 &wide,
                                         platformator_solver_simd::WideConstraint8Cold &cold, size_t lane,
                                         Collision *collision, const Rigidbody *rbA, const Rigidbody *rbB,
                                         uint32_t idxA, uint32_t idxB, const SolverBodyState *bodies,
                                         const SolverBodyForce *bodyForces, const BodyPrepCache *bodyPrep,
                                         const SoftStepParams &params) noexcept
{
    ClipPointsWithData &contactPoints = collision->getContactPoints();
    const uint8_t pointCount = static_cast<uint8_t>(std::min<size_t>(contactPoints.count, 2));
    if (pointCount == 0)
    {
        platformator_solver_simd::clearWideLane(wide, cold, lane);
        return false;
    }

    const uint8_t laneBit = static_cast<uint8_t>(1u << lane);
    const SolverBodyState &bA = bodies[idxA];
    const SolverBodyState &bB = bodies[idxB];
    const float posAx = idxA != 0 ? bodyPrep[idxA].posX : 0.0f;
    const float posAy = idxA != 0 ? bodyPrep[idxA].posY : 0.0f;
    const float posBx = idxB != 0 ? bodyPrep[idxB].posX : 0.0f;
    const float posBy = idxB != 0 ? bodyPrep[idxB].posY : 0.0f;
    const float frictionA = idxA != 0 ? bodyPrep[idxA].friction : rbA->getFriction();
    const float frictionB = idxB != 0 ? bodyPrep[idxB].friction : rbB->getFriction();
    const float restitutionA = idxA != 0 ? bodyPrep[idxA].restitution : rbA->getRestitution();
    const float restitutionB = idxB != 0 ? bodyPrep[idxB].restitution : rbB->getRestitution();
    const float combinedFriction = (frictionA == frictionB) ? frictionA : std::sqrt(frictionA * frictionB);
    const float combinedRestitution = std::max(restitutionA, restitutionB);

    const Eigen::Vector2f &normal = collision->getNormal();
    const float nX = normal.x();
    const float nY = normal.y();

    wide.bodyIndexA[lane] = idxA;
    wide.bodyIndexB[lane] = idxB;
    wide.normalX[lane] = nX;
    wide.normalY[lane] = nY;
    wide.friction[lane] = combinedFriction;
    wide.invIA[lane] = bA.inverseMomentOfInertia;
    wide.invIB[lane] = bB.inverseMomentOfInertia;
    if (idxA != 0)
    {
        wide.readMaskA |= laneBit;
    }
    if (idxB != 0)
    {
        wide.readMaskB |= laneBit;
    }
    if (bA.inverseMass != 0.0f || bA.inverseMomentOfInertia != 0.0f)
    {
        wide.writeMaskA |= laneBit;
    }
    if (bB.inverseMass != 0.0f || bB.inverseMomentOfInertia != 0.0f)
    {
        wide.writeMaskB |= laneBit;
    }

    cold.contactPoints[lane] = &contactPoints;
    cold.pointCount[lane] = pointCount;
    cold.restitution[lane] = combinedRestitution;

    const ClipPointWithData &cp0 = contactPoints.points[0];
    const float p0x = cp0.point.point.x();
    const float p0y = cp0.point.point.y();
    const float rAx0 = idxA != 0 ? (p0x - posAx) : 0.0f;
    const float rAy0 = idxA != 0 ? (p0y - posAy) : 0.0f;
    const float rBx0 = idxB != 0 ? (p0x - posBx) : 0.0f;
    const float rBy0 = idxB != 0 ? (p0y - posBy) : 0.0f;
    const float rnA0 = rAx0 * nY - rAy0 * nX;
    const float rnB0 = rBx0 * nY - rBy0 * nX;
    wide.rnA0[lane] = rnA0;
    wide.rnB0[lane] = rnB0;
    wide.rtA0[lane] = rAx0 * nX + rAy0 * nY;
    wide.rtB0[lane] = rBx0 * nX + rBy0 * nY;
    wide.baseSep0[lane] = cp0.separation;
    wide.accumNormal0[lane] = cp0.accumulatedNormalImpulse;
    wide.accumTangent0[lane] = cp0.accumulatedTangentImpulse;

    float rnA1 = 0.0f;
    float rnB1 = 0.0f;
    if (pointCount == 2)
    {
        const ClipPointWithData &cp1 = contactPoints.points[1];
        const float p1x = cp1.point.point.x();
        const float p1y = cp1.point.point.y();
        const float rAx1 = idxA != 0 ? (p1x - posAx) : 0.0f;
        const float rAy1 = idxA != 0 ? (p1y - posAy) : 0.0f;
        const float rBx1 = idxB != 0 ? (p1x - posBx) : 0.0f;
        const float rBy1 = idxB != 0 ? (p1y - posBy) : 0.0f;
        rnA1 = rAx1 * nY - rAy1 * nX;
        rnB1 = rBx1 * nY - rBy1 * nX;
        wide.rnA1[lane] = rnA1;
        wide.rnB1[lane] = rnB1;
        wide.rtA1[lane] = rAx1 * nX + rAy1 * nY;
        wide.rtB1[lane] = rBx1 * nX + rBy1 * nY;
        wide.baseSep1[lane] = cp1.separation;
        wide.accumNormal1[lane] = cp1.accumulatedNormalImpulse;
        wide.accumTangent1[lane] = cp1.accumulatedTangentImpulse;
        wide.twoPointMask |= laneBit;
    }
    else
    {
        wide.rnA1[lane] = 0.0f;
        wide.rnB1[lane] = 0.0f;
        wide.rtA1[lane] = 0.0f;
        wide.rtB1[lane] = 0.0f;
        wide.massNormal1[lane] = 0.0f;
        wide.massTangent1[lane] = 0.0f;
        wide.baseSep1[lane] = 0.0f;
        wide.accumNormal1[lane] = 0.0f;
        wide.accumTangent1[lane] = 0.0f;
    }

    if (combinedRestitution > 0.0f)
    {
        float vAx = bA.vx;
        float vAy = bA.vy;
        float wA = bA.w;
        float vBx = bB.vx;
        float vBy = bB.vy;
        float wB = bB.w;
        const float h = params.h;
        if (h > 0.0f)
        {
            vAx += bodyForces[idxA].accelX * h;
            vAy += bodyForces[idxA].accelY * h;
            wA += bodyForces[idxA].angAccel * h;
            vBx += bodyForces[idxB].accelX * h;
            vBy += bodyForces[idxB].accelY * h;
            wB += bodyForces[idxB].angAccel * h;
        }
        const float dvN = (vBx - vAx) * nX + (vBy - vAy) * nY;
        const float rv0 = dvN + wB * rnB0 - wA * rnA0;
        cold.relVel0[lane] = rv0;
        const float rv1 = pointCount == 2 ? (dvN + wB * rnB1 - wA * rnA1) : 0.0f;
        cold.relVel1[lane] = rv1;
        if (rv0 < -params.restitutionThreshold || (pointCount == 2 && rv1 < -params.restitutionThreshold))
        {
            wide.restitutionMask |= laneBit;
            return true;
        }
    }
    else
    {
        cold.relVel0[lane] = 0.0f;
        cold.relVel1[lane] = 0.0f;
    }
    return false;
}

bool PhysicsManager::solvePersistentIslandGroup8(const int32_t *islandIds, size_t groupSize,
                                                 const SoftStepParams &params, IslandSolverScratch &scratch)
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    using namespace platformator_solver_simd;
    auto &bodies = scratch.bodies;
    auto &bodyForces = scratch.bodyForces;
    auto &bodyPrep = scratch.bodyPrep;
    auto &writable = scratch.writableBodies;
    auto &localIndices = scratch.bodyLocalIndices;
    auto &kinematics = scratch.kinematicBodies;
    auto &wideSteps = scratch.wideSteps;
    auto &wideCold = scratch.wideCold;

    size_t maxPossibleSteps = 0;
    size_t laneBodyOffset[8] = {};
    size_t totalLaneBodies = 0;
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        const PersistentIsland &island = persistentIslands[static_cast<size_t>(islandIds[lane])];
        maxPossibleSteps = std::max(maxPossibleSteps, island.contacts.size());
        laneBodyOffset[lane] = totalLaneBodies;
        totalLaneBodies += island.bodies.size();
    }
    if (maxPossibleSteps == 0)
    {
        return true;
    }

    bodies.clear();
    bodyForces.clear();
    bodyPrep.clear();
    if (bodies.capacity() < totalLaneBodies + 1)
    {
        bodies.reserve(totalLaneBodies + 1);
        bodyForces.reserve(totalLaneBodies + 1);
        bodyPrep.reserve(totalLaneBodies + 1);
        writable.reserve(totalLaneBodies);
    }
    bodies.push_back(SolverBodyState{}); // slot 0: shared static/sleeping zero-velocity body
    bodyForces.push_back(SolverBodyForce{});
    bodyPrep.push_back(BodyPrepCache{});
    writable.clear();
    kinematics.clear();
    localIndices.assign(totalLaneBodies, UNMAPPED_SOLVER_BODY);

    // Pre-populate dynamic body slots once per island to eliminate per-contact GameObject pointer chasing.
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        const PersistentIsland &island = persistentIslands[static_cast<size_t>(islandIds[lane])];
        const size_t bodyBase = laneBodyOffset[lane];
        for (size_t bi = 0; bi < island.bodies.size(); ++bi)
        {
            Rigidbody *rb = island.bodies[bi];
            if (!rb->isAwakeDynamic())
            {
                continue;
            }
            const uint32_t mapped = static_cast<uint32_t>(bodies.size());
            localIndices[bodyBase + bi] = mapped;
            bodies.push_back(makeDynamicBodyState(rb));
            bodyForces.push_back(makeBodyForce(rb));
            bodyPrep.push_back(makeBodyPrepCache(rb));
            writable.push_back(WritableSolverBody{mapped, rb});
        }
    }

    bool allIndexed = true;
    const auto mapBody = [&](const PersistentIsland &island, size_t bodyBase, Rigidbody *rb) -> uint32_t {
        if (!rb->isAwakeDynamic())
        {
            return rb->isMovingKinematic() ? findOrAddKinematicBody(rb, bodies, bodyForces, kinematics, &bodyPrep) : 0u;
        }
        if (rb->solverIslandId != island.id || rb->solverIslandBodyIndex >= island.bodies.size())
        {
            allIndexed = false;
            return 0;
        }
        const uint32_t mapped = localIndices[bodyBase + rb->solverIslandBodyIndex];
        if (mapped == UNMAPPED_SOLVER_BODY)
        {
            allIndexed = false;
            return 0;
        }
        return mapped;
    };

    wideSteps.resize(maxPossibleSteps);
    wideCold.resize(maxPossibleSteps);

    const PersistentIsland *laneIslands[8] = {};
    ColliderPair *const *laneContacts[8] = {};
    size_t laneContactSizes[8] = {};
    size_t laneCursors[8] = {};
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        const PersistentIsland &island = persistentIslands[static_cast<size_t>(islandIds[lane])];
        laneIslands[lane] = &island;
        laneContacts[lane] = island.contacts.data();
        laneContactSizes[lane] = island.contacts.size();
    }

    size_t stepCount = 0;
    bool anyRestitution = false;
    const float h = params.h;

    // Step-major packing keeps WideConstraint8 (832 B) hot in L1D across all 8 lanes and fuses AVX2 mass finalization.
    while (true)
    {
        WideConstraint8 &wide = wideSteps[stepCount];
        WideConstraint8Cold &cold = wideCold[stepCount];
        wide.resetMasks();
        bool anyLaneActive = false;

        for (size_t lane = 0; lane < 8; ++lane)
        {
            if (lane >= groupSize)
            {
                clearWideLane(wide, cold, lane);
                continue;
            }
            const PersistentIsland &island = *laneIslands[lane];
            ColliderPair *const *contacts = laneContacts[lane];
            const size_t totalContacts = laneContactSizes[lane];
            size_t &cursor = laneCursors[lane];

            SolverContactPair contact;
            bool found = false;

            while (cursor < totalContacts)
            {
                if (cursor + 4 < totalContacts)
                {
                    platformator_detail::prefetchColliderPair(contacts[cursor + 4]);
                }
                ColliderPair *pair = contacts[cursor++];
                if (extractSolverContactPair(pair, contact) && contact.collision->getContactPoints().count > 0)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                clearWideLane(wide, cold, lane);
                continue;
            }

            anyLaneActive = true;
            const size_t bodyBase = laneBodyOffset[lane];
            const uint32_t idxA = mapBody(island, bodyBase, contact.refBody);
            const uint32_t idxB = mapBody(island, bodyBase, contact.incBody);
            if (!allIndexed)
            {
                return false;
            }

            anyRestitution |= packWideContactLane(wide, cold, lane, contact.collision, contact.refBody, contact.incBody,
                                                  idxA, idxB, bodies.data(), bodyForces.data(), bodyPrep.data(),
                                                  params);
        }

        if (!anyLaneActive)
        {
            break;
        }
        finalizeWideEffectiveMasses8AVX2(wide, bodies.data());
        ++stepCount;
        if (stepCount == maxPossibleSteps)
        {
            break;
        }
    }

    if (stepCount == 0)
    {
        return true;
    }

    wideSteps.resize(stepCount);
    wideCold.resize(stepCount);
    SolverBodyState *bodyData = bodies.data();
    const size_t bodyCount = bodies.size();

    runSoftSteps(
        params, anyRestitution,
        [&] { integrateSolverVelocities(bodyData, bodyForces.data(), 1, bodyCount, h); },
        [&] {
            for (auto &wide : wideSteps)
            {
                applyWarmStartWide8AVX2(wide, bodyData);
            }
        },
        [&](auto useBias) {
            for (auto &wide : wideSteps)
            {
                solveWide8SoftAVX2<decltype(useBias)::value>(wide, bodyData, params);
            }
        },
        [&] { integrateSolverPositions(bodyData, 1, bodyCount, h); },
        [&] {
            for (size_t step = 0; step < stepCount; ++step)
            {
                applyRestitutionWide8AVX2(wideSteps[step], wideCold[step], bodyData, params);
            }
        });

    for (size_t step = 0; step < stepCount; ++step)
    {
        storeWideImpulses(wideSteps[step], wideCold[step]);
    }

    commitWritableBodies(writable, bodies);
    return true;
#else
    (void)islandIds;
    (void)groupSize;
    (void)params;
    (void)scratch;
    return false;
#endif
}
