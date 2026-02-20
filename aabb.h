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
    // SegmentedIntervalList *getIntervalListY();
    void updateCandidateList();
    std::list<Collision> *getCandidateCollisions();

private:
    void checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk);
    void checkForCollisionsWithCheckpoint(LocalSortArray *chunk);

    SegmentedIntervalList intervalListX;
    // SegmentedIntervalList intervalListY;
    std::list<Collision> candidateCollisions;
};