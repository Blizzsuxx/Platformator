#include "physicspairstate.h"
#include "benchmark.h"
#include "workprobe.h"
#include "spatialresourcehooks.h"

#include <algorithm>
#include <stdexcept>

#include "physicsmanager.h"

PhysicsPairState::PhysicsPairState(PhysicsManager *owner)
    : owner(owner), pairDeltaScratch(), candidateCollisions(), pendingNarrowPhasePairs(), pairAdjacency(),
      activePairDeltaCollector()
{
}

void PhysicsPairState::dequeuePairFromNarrowPhase(const ColliderPair *pair)
{
    if (pair == nullptr || !pair->getIsQueuedForNarrowPhase())
        return;

    const size_t removeIndex = pair->getNarrowPhaseQueueIndex();
    const size_t lastIndex = pendingNarrowPhasePairs.size() - 1;
    if (removeIndex != lastIndex)
    {
        const ColliderPair *movedPair = pendingNarrowPhasePairs[lastIndex];
        pendingNarrowPhasePairs[removeIndex] = movedPair;
        movedPair->setNarrowPhaseQueueIndex(removeIndex);
    }
    pendingNarrowPhasePairs.pop_back();
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);
}

void PhysicsPairState::queuePairsForCollider(Collider *collider)
{
    const auto *touchingPairs = getTouchingPairs(collider);
    if (touchingPairs == nullptr)
        return;
    PLATFORMATOR_WORK_ADD(pairAdjacencyQueueVisits, touchingPairs->size());
    for (const ColliderPair *pair : *touchingPairs)
        queuePairForNarrowPhase(pair);
}

void PhysicsPairState::queuePairForNarrowPhase(const ColliderPair *pair)
{
    if (pair == nullptr || pair->getIsQueuedForNarrowPhase())
        return;
    pair->setNarrowPhaseQueueIndex(pendingNarrowPhasePairs.size());
    pendingNarrowPhasePairs.push_back(pair);
}

const std::vector<const ColliderPair *> *PhysicsPairState::getPendingNarrowPhasePairs() const
{
    return &pendingNarrowPhasePairs;
}

const std::vector<const ColliderPair *> *PhysicsPairState::getTouchingPairs(Collider *collider) const
{
    const auto iterator = pairAdjacency.find(collider);
    return iterator == pairAdjacency.end() ? nullptr : &iterator->second;
}

void PhysicsPairState::clearPendingNarrowPhasePairs()
{
    // Workers consumed their own indices. The fixed pointer batch lives through serial reduction.
    pendingNarrowPhasePairs.clear();
}

void PhysicsPairState::recordPairDelta(Collider *colliderA, Collider *colliderB, int delta)
{
    if (delta != 0)
        activePairDeltaCollector.record(colliderA, colliderB, delta);
}

int PhysicsPairState::applyPairWitnessDelta(Collider *colliderA, Collider *colliderB, int delta)
{
    platformator_detail::SpatialResourceTagScope resourceTag(
        platformator_detail::SpatialResourceTag::PhysicsCommonPairs);
    if (delta == 0)
        return 0;

    auto iteratorPair = candidateCollisions.find(ColliderPair(colliderA, colliderB));
    if (iteratorPair == candidateCollisions.end() && delta < 0)
        return 0;

    const ColliderPair *pair;
    if (iteratorPair == candidateCollisions.end())
    {
        auto [iterator, inserted] = candidateCollisions.emplace(colliderA, colliderB);
        (void)inserted;
        pair = &(*iterator);
    }
    else
    {
        pair = &(*iteratorPair);
    }

    const int previousWitnessCount = pair->getWitnessCount();
    pair->addToWitnessCount(delta);
    if (delta > 0)
    {
        if (previousWitnessCount != 0)
            return 0;

        PLATFORMATOR_BENCH_ADD_COUNTER(PairAddCount, 1);

        Collider *objectA = pair->getObjectA();
        Collider *objectB = pair->getObjectB();
        auto &adjacencyA = pairAdjacency[objectA];
        pair->setAdjacencyIndexA(adjacencyA.size());
        adjacencyA.push_back(pair);
        auto &adjacencyB = pairAdjacency[objectB];
        pair->setAdjacencyIndexB(adjacencyB.size());
        adjacencyB.push_back(pair);
        queuePairForNarrowPhase(pair);
        return 1;
    }

    if (pair->getWitnessCount() > 0)
        return 0;

    PLATFORMATOR_BENCH_ADD_COUNTER(PairRemoveCount, 1);

    Collider *objectA = pair->getObjectA();
    Collider *objectB = pair->getObjectB();
    dequeuePairFromNarrowPhase(pair);

    auto adjacencyA = pairAdjacency.find(objectA);
    if (adjacencyA != pairAdjacency.end())
    {
        auto &pairsA = adjacencyA->second;
        const size_t removeIndex = pair->getAdjacencyIndexA();
        const size_t lastIndex = pairsA.size() - 1;
        if (removeIndex != lastIndex)
        {
            const ColliderPair *movedPair = pairsA[lastIndex];
            pairsA[removeIndex] = movedPair;
            if (movedPair->getObjectA() == objectA)
                movedPair->setAdjacencyIndexA(removeIndex);
            else
                movedPair->setAdjacencyIndexB(removeIndex);
        }
        pairsA.pop_back();
        if (pairsA.empty())
            pairAdjacency.erase(adjacencyA);
    }

    auto adjacencyB = pairAdjacency.find(objectB);
    if (adjacencyB != pairAdjacency.end())
    {
        auto &pairsB = adjacencyB->second;
        const size_t removeIndex = pair->getAdjacencyIndexB();
        const size_t lastIndex = pairsB.size() - 1;
        if (removeIndex != lastIndex)
        {
            const ColliderPair *movedPair = pairsB[lastIndex];
            pairsB[removeIndex] = movedPair;
            if (movedPair->getObjectA() == objectB)
                movedPair->setAdjacencyIndexA(removeIndex);
            else
                movedPair->setAdjacencyIndexB(removeIndex);
        }
        pairsB.pop_back();
        if (pairsB.empty())
            pairAdjacency.erase(adjacencyB);
    }

    // Final contact/support removal is completed while the stable pair record still exists.
    if (owner)
        owner->removePair(pair);
    candidateCollisions.erase(candidateCollisions.find(ColliderPair(colliderA, colliderB)));
    return -1;
}

PairPublicationStats PhysicsPairState::applyCollectedPairDeltas()
{
    platformator_detail::SpatialResourceTagScope resourceTag(platformator_detail::SpatialResourceTag::Scratch);
    PairPublicationStats stats;
    pairDeltaScratch.clear();
    for (auto &local : activePairDeltaCollector.localPairDeltas)
    {
        for (auto delta : local)
        {
            if (delta.colliderB->getHandle() < delta.colliderA->getHandle())
                std::swap(delta.colliderA, delta.colliderB);
            pairDeltaScratch.push_back(delta);
        }
        local.clear();
    }

    for (const auto &delta : pairDeltaScratch)
        if (delta.delta > 0)
        {
            stats.rawDiscoveries += delta.delta;
            PLATFORMATOR_BENCH_ADD_COUNTER(RawDiscoveryCount, delta.delta);
        }

    // EFFICIENCY: Every nonempty changed-pair batch pays an O(D log D) canonical sort, followed by
    // O(D) serial witness reduction and pair/adjacency mutation; high-migration frames can make this
    // a visible tail cost. It remains serial because the pair map, both adjacency lists, pending
    // narrow-phase queue, and callback retirement order are shared mutable state. A better design
    // partitions canonical pair keys into stable owner shards, reduces each shard in parallel, and
    // serializes only callbacks/reclamation. Concrete replacement work: add sharded pair-state
    // ownership and benchmark it against this path at 0/1/10/100% movement before switching.
    std::sort(pairDeltaScratch.begin(), pairDeltaScratch.end(), [](const auto &a, const auto &b) {
        if (a.colliderA->getHandle() != b.colliderA->getHandle())
            return a.colliderA->getHandle() < b.colliderA->getHandle();
        return a.colliderB->getHandle() < b.colliderB->getHandle();
    });

    for (size_t i = 0; i < pairDeltaScratch.size();)
    {
        const auto first = pairDeltaScratch[i];
        int delta = 0;
        do
        {
            delta += pairDeltaScratch[i++].delta;
        } while (i < pairDeltaScratch.size() && pairDeltaScratch[i].colliderA == first.colliderA &&
                 pairDeltaScratch[i].colliderB == first.colliderB);
        if (delta > 0)
        {
            ++stats.exactChecks;
            PLATFORMATOR_BENCH_ADD_COUNTER(ExactOverlapCount, 1);
        }
        if (delta != 0)
        {
            const int transition = applyPairWitnessDelta(first.colliderA, first.colliderB, delta);
            stats.pairAdds += transition > 0;
            stats.pairRemoves += transition < 0;
        }
    }
    return stats;
}

size_t PhysicsPairState::getCandidatePairCount() const
{
    return candidateCollisions.size();
}

size_t PhysicsPairState::getOwnedCapacityBytes() const
{
    size_t bytes = sizeof(*this) + (candidateCollisions.size() + pairAdjacency.size()) * 2 * sizeof(void *) +
                   candidateCollisions.bucket_count() * sizeof(void *) +
                   candidateCollisions.size() * sizeof(ColliderPair) +
                   pairAdjacency.bucket_count() * sizeof(void *) +
                   pairAdjacency.size() * sizeof(std::pair<Collider *const, std::vector<const ColliderPair *>>);
    for (const auto &[_, pairs] : pairAdjacency)
        bytes += pairs.capacity() * sizeof(const ColliderPair *);
    bytes += pendingNarrowPhasePairs.capacity() * sizeof(const ColliderPair *);
    return bytes;
}

size_t PhysicsPairState::getScratchCapacityBytes() const
{
    size_t bytes = pairDeltaScratch.capacity() * sizeof(PairDelta);
    for (const auto &local : activePairDeltaCollector.localPairDeltas)
        bytes += local.capacity() * sizeof(PairDelta);
    return bytes;
}

bool PhysicsPairState::containsPair(Collider *colliderA, Collider *colliderB) const
{
    return candidateCollisions.find(ColliderPair(colliderA, colliderB)) != candidateCollisions.end();
}

void PhysicsPairState::setReplayEndpointIdentity(Collider *collider, platformator::ObjectHandle handle)
{
    if (collider == nullptr || !handle)
        throw std::invalid_argument("Spatial replay collider identity must be valid");
    if (pairAdjacency.contains(collider))
        throw std::logic_error("Spatial replay identity cannot change while its candidate pairs are retained");
    collider->handle = handle;
}
