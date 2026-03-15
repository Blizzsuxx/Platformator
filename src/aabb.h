#pragma once

#include "segmentedintervallist.h"
#include "collider.h"
#include "collision.h"
#include <set>
#include "onswapcallback.h"

class AABB : public OnSwapCallback
{
public:
    AABB();
    ~AABB();

    void add(Collider *element);
    void remove(Collider *element);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    const std::vector<Collision> *getCandidateCollisions() const;
    void onSwap(BoundingRadiusProjection *movedLeft, BoundingRadiusProjection *movedRight) override;
    void sort();

private:
    void checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk);
    void checkForCollisionsWithCheckpoint(LocalSortArray *chunk);
    void addCandidateCollision(Collider *colliderA, Collider *colliderB);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::vector<Collision> candidateCollisions;
};