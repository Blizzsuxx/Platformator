#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "segmentedintervallist.h"
#include "collider.h"
#include "collision.h"
#include "colliderpair.h"

class Grid;

enum Axis : uint8_t
{
    X = 1,
    Y = 2,
    ALL_AXES = 3
};

class AABB
{
public:
    AABB(Grid *owner);
    ~AABB();

    void add(Collider *element);
    void remove(Collider *element);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    // sort the array with insertion sort, sort from lowest to highest
    void sort();

private:
    const std::vector<const ColliderPair *> *getTouchingPairs(Collider *collider) const;
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis);
    void removePair(const ColliderPair &pair);
    void removePairsForCollider(Collider *collider);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    Grid *owner;

    friend class SegmentedIntervalList;
};