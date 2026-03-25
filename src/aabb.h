#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "segmentedintervallist.h"
#include "collider.h"
#include "collision.h"
#include "colliderpair.h"

enum Axis : uint8_t
{
    X = 1,
    Y = 2,
    ALL_AXES = 3
};

class AABB
{
public:
    AABB();
    ~AABB();

    void add(Collider *element);
    void remove(Collider *element);
    void queuePairsForCollider(Collider *collider);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    const std::unordered_set<ColliderPair, ColliderPair::HashFunction> *getCandidatePairSet() const;
    const std::vector<const ColliderPair *> *getPendingNarrowPhasePairs() const;
    const std::vector<const ColliderPair *> *getTouchingPairs(Collider *collider) const;
    void clearPendingNarrowPhasePairs();
    // sort the array with insertion sort, sort from lowest to highest
    void sort();

private:
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis);
    void queuePairForNarrowPhase(const ColliderPair *pair);
    void dequeuePairFromNarrowPhase(const ColliderPair *pair);
    void removePair(const ColliderPair &pair);
    void removePairsForCollider(Collider *collider);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<ColliderPair, ColliderPair::HashFunction> candidateCollisions;
    std::vector<const ColliderPair *> pendingNarrowPhasePairs;
    std::unordered_map<Collider *, std::vector<const ColliderPair *>> pairAdjacency;

    friend class SegmentedIntervalList;
};