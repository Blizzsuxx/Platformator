#pragma once

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
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    const std::unordered_set<ColliderPair, ColliderPair::HashFunction> *getCandidatePairSet() const;
    // sort the array with insertion sort, sort from lowest to highest
    void sort();

private:
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis);
    void removePair(const ColliderPair &pair);
    void removePairsForCollider(Collider *collider);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<ColliderPair, ColliderPair::HashFunction> candidateCollisions;
    std::unordered_map<Collider *, std::unordered_set<Collider *>> pairAdjacency;

    friend class SegmentedIntervalList;
};