#include "physics/physicsmanager.h"

#include "benchmark/workprobe.h"
#include "core/constants.h"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_group.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
bool isBodyQuiet(const Rigidbody *body) noexcept
{
    constexpr float linearThresholdSq = LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD;
    return body->getVelocity().squaredNorm() <= linearThresholdSq &&
           std::abs(body->getAngularVelocity()) <= ANGULAR_SLEEP_THRESHOLD;
}
} // namespace

// ============================================================================
// Island & Isolated Body Sleep Evaluation
// ============================================================================

void PhysicsManager::evaluateMixedIslandSleep(const PersistentIsland &island, double timeDelta, SleepScratch &scratch,
                                              std::vector<Rigidbody *> &toSleep)
{
    // Mixed island (some bodies supported, some unsupported): preserve exact sleep-candidate connectivity.
    const size_t bodyCount = island.bodies.size();
    scratch.dsu.reset(bodyCount);
    scratch.compQuiet.assign(bodyCount, 1);
    scratch.compTimer.resize(bodyCount);
    for (size_t i = 0; i < bodyCount; ++i)
    {
        scratch.compTimer[i] = island.bodies[i]->getSleepTimer();
    }

    for (const ColliderPair *pair : island.contacts)
    {
        const auto [rbA, rbB] = pairRigidbodies(pair);
        if (rbA == nullptr || rbB == nullptr ||
            rbA->sleepCandidateIndex == Rigidbody::INVALID_INDEX ||
            rbB->sleepCandidateIndex == Rigidbody::INVALID_INDEX ||
            rbA->solverIslandId != island.id || rbB->solverIslandId != island.id)
        {
            continue;
        }
        scratch.dsu.unite(rbA->solverIslandBodyIndex, rbB->solverIslandBodyIndex);
    }

    for (size_t i = 0; i < bodyCount; ++i)
    {
        Rigidbody *body = island.bodies[i];
        if (body->sleepCandidateIndex == Rigidbody::INVALID_INDEX)
        {
            continue;
        }
        const uint32_t root = scratch.dsu.find(static_cast<uint32_t>(i));
        scratch.compQuiet[root] = static_cast<uint8_t>(scratch.compQuiet[root] && isBodyQuiet(body));
        scratch.compTimer[root] = std::min(scratch.compTimer[root], body->getSleepTimer());
    }

    for (size_t i = 0; i < bodyCount; ++i)
    {
        Rigidbody *body = island.bodies[i];
        if (body->sleepCandidateIndex == Rigidbody::INVALID_INDEX)
        {
            continue;
        }
        const uint32_t root = scratch.dsu.find(static_cast<uint32_t>(i));
        const double timer = scratch.compQuiet[root] ? scratch.compTimer[root] + timeDelta : 0.0;
        if (timer >= SLEEP_DELAY)
        {
            toSleep.push_back(body);
        }
        else
        {
            body->setSleepTimer(timer);
        }
    }
}

size_t PhysicsManager::evaluateIslandSleep(const PersistentIsland &island, double timeDelta, SleepScratch &scratch,
                                           std::vector<Rigidbody *> &toSleep)
{
    size_t candidateCount = 0;
    bool quiet = true;
    double minTimer = std::numeric_limits<double>::infinity();
    for (Rigidbody *body : island.bodies)
    {
        if (body->sleepCandidateIndex != Rigidbody::INVALID_INDEX)
        {
            ++candidateCount;
            quiet &= isBodyQuiet(body);
            minTimer = std::min(minTimer, body->getSleepTimer());
        }
    }
    if (candidateCount == 0)
    {
        return 0;
    }

    if (candidateCount == island.bodies.size())
    {
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
        return candidateCount;
    }

    evaluateMixedIslandSleep(island, timeDelta, scratch, toSleep);
    return candidateCount;
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

    auto &bodiesToSleep = bodiesToSleepScratch;
    bodiesToSleep.clear();
    size_t visitedCandidates = 0;
    const bool runParallel = activeIslandIds.size() >= 32 && hasParallelPhysicsWorkers();

    if (!runParallel)
    {
        for (int32_t islandId : activeIslandIds)
        {
            visitedCandidates += evaluateIslandSleep(persistentIslands[static_cast<size_t>(islandId)], timeDelta,
                                                     serialSleepScratch, bodiesToSleep);
        }
    }
    else
    {
        for (auto &local : parallelSleepScratch)
        {
            local.toSleep.clear();
        }
        std::atomic<size_t> parallelVisited{0};
        tbb::task_group_context context(tbb::task_group_context::isolated);
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, activeIslandIds.size(), 16),
            [&](const auto &range) {
                auto &localScratch = parallelSleepScratch.local();
                size_t localCount = 0;
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    localCount += evaluateIslandSleep(persistentIslands[static_cast<size_t>(activeIslandIds[i])],
                                                      timeDelta, localScratch, localScratch.toSleep);
                }
                if (localCount > 0)
                {
                    parallelVisited.fetch_add(localCount, std::memory_order_relaxed);
                }
            },
            context);
        visitedCandidates = parallelVisited.load(std::memory_order_relaxed);
        for (auto &local : parallelSleepScratch)
        {
            bodiesToSleep.insert(bodiesToSleep.end(), local.toSleep.begin(), local.toSleep.end());
            local.toSleep.clear();
        }
    }

    // Evaluate isolated sleep candidates (0 active dynamic contacts, e.g. zero-gravity bodies).
    if (visitedCandidates < sleepCandidates.size())
    {
        for (Rigidbody *body : sleepCandidates)
        {
            if (body->solverIslandId != ColliderPair::INVALID_ISLAND_ID)
            {
                continue;
            }
            const double timer = isBodyQuiet(body) ? body->getSleepTimer() + timeDelta : 0.0;
            if (timer >= SLEEP_DELAY)
            {
                bodiesToSleep.push_back(body);
            }
            else
            {
                body->setSleepTimer(timer);
            }
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
