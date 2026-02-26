#pragma once

#include "segmentedintervallist.h"
#include "collider.h"
#include "collision.h"
#include <set>

class AABB : public OnSwapCallback
{
public:
    AABB();
    ~AABB();

    void add(Collider *element);
    void remove(Collider *element);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    // void updateCandidateList();
    const std::unordered_set<Collision, Collision::HashFunction> *getCandidateCollisions() const;
    void onSwap(BoundingRadiusProjection *movedLeft, BoundingRadiusProjection *movedRight) override;
    void sort();

private:
    // void checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk);
    // void checkForCollisionsWithCheckpoint(LocalSortArray *chunk);
    void addCandidateCollision(Collider *colliderA, Collider *colliderB);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<Collision, Collision::HashFunction> candidateCollisions;
};