#pragma once

#include <vector>
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

class AABBPair
{
public:
    AABBPair(Collider *a, Collider *b) : a(a), b(b), axisOverlap(0)
    {
    }

    ~AABBPair()
    {
    }
    bool operator==(const AABBPair &other) const
    {
        return (a == other.a && b == other.b) || (a == other.b && b == other.a);
    }

    class HashFunction
    {
    public:
        size_t operator()(const AABBPair &pair) const
        {
            // sort them so that the order of the objects doesn't matter
            const Collider *objectA = pair.a;
            const Collider *objectB = pair.b;

            if (objectA > objectB)
            {
                std::swap(objectA, objectB);
            }

            size_t hash1 = std::hash<const Collider *>()(objectA);
            size_t hash2 = std::hash<const Collider *>()(objectB);
            return hash1 ^ (hash2 << 1); // Combine the two hashes
        }
    };

    Collider *a;
    Collider *b;
    mutable uint8_t axisOverlap; // bitfield of Axis values
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
    bool getIsEmpty() const;

private:
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<AABBPair, AABBPair::HashFunction> pairsWithAtLeastOneAxisOverlapping;
    Grid *owner;

    friend class SegmentedIntervalList;
};