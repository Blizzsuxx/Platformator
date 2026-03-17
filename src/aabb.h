#pragma once

#include "segmentedintervallist.h"
#include "collider.h"
#include "collision.h"

class AABB
{
public:
    AABB();
    ~AABB();

    void add(Collider *element);
    void remove(Collider *element);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    const std::unordered_set<Collision, Collision::HashFunction> *getCandidateCollisions() const;
    // sort the array with insertion sort, sort from lowest to highest
    void sort();

private:
    // void checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk);
    // void checkForCollisionsWithCheckpoint(LocalSortArray *chunk);
    void addCandidateCollision(Collider *colliderA, Collider *colliderB);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<Collision, Collision::HashFunction> candidateCollisions;

    friend class SegmentedIntervalList;
};