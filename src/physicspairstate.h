#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "colliderpair.h"
#include "oneapi/tbb.h"
#include "spatialresourcehooks.h"

class Collider;
class PhysicsManager;

struct PairDelta
{
    Collider *colliderA;
    Collider *colliderB;
    int delta;
};

struct PairDeltaCollector
{
    tbb::enumerable_thread_specific<std::vector<PairDelta>> localPairDeltas;

    void record(Collider *colliderA, Collider *colliderB, int delta)
    {
        platformator_detail::SpatialResourceTagScope resourceTag(platformator_detail::SpatialResourceTag::Scratch);
        localPairDeltas.local().emplace_back(colliderA, colliderB, delta);
    }
};

// PhysicsManager-owned canonical candidate, adjacency and narrow-phase queue state. Spatial
// backends contribute net support deltas but never own contact or solver records.
struct PairPublicationStats
{
    size_t rawDiscoveries=0, exactChecks=0, pairAdds=0, pairRemoves=0;
};

class PhysicsPairState
{
    friend class Grid;

  public:
    explicit PhysicsPairState(PhysicsManager *owner = nullptr);
    PhysicsPairState(const PhysicsPairState &) = delete;
    PhysicsPairState &operator=(const PhysicsPairState &) = delete;

    void recordPairDelta(Collider *colliderA, Collider *colliderB, int delta);
    PairPublicationStats applyCollectedPairDeltas();
    int applyPairWitnessDelta(Collider *colliderA, Collider *colliderB, int delta);
    void queuePairForNarrowPhase(const ColliderPair *pair);
    void queuePairsForCollider(Collider *collider);
    void dequeuePairFromNarrowPhase(const ColliderPair *pair);
    const std::vector<const ColliderPair *> *getTouchingPairs(Collider *collider) const;
    const std::vector<const ColliderPair *> *getPendingNarrowPhasePairs() const;
    void clearPendingNarrowPhasePairs();
    size_t getCandidatePairCount() const;
    size_t getOwnedCapacityBytes() const;
    size_t getScratchCapacityBytes() const;
    bool containsPair(Collider *colliderA, Collider *colliderB) const;
    // Replay endpoints have stable trace identities but are not registered GameManager objects.
    // Give their private Collider records generation-aware handles so they exercise the same
    // production candidate/adjacency store without creating a Runtime or solver feedback.
    void setReplayEndpointIdentity(Collider *collider, platformator::ObjectHandle handle);

  private:
    PhysicsManager *owner;
    std::vector<PairDelta> pairDeltaScratch;
    std::unordered_set<ColliderPair, ColliderPair::HashFunction> candidateCollisions;
    std::vector<const ColliderPair *> pendingNarrowPhasePairs;
    std::unordered_map<Collider *, std::vector<const ColliderPair *>> pairAdjacency;
    PairDeltaCollector activePairDeltaCollector;
};
