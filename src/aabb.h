#pragma once

#include <cstdint>
#include "segmentedintervallist.h"
#include "collider.h"
#include "collision.h"

enum Axis : uint8_t
{
    X = 1,
    Y = 2,
    ALL_AXES = 3
};

struct ColliderPair
{
    Collider *objectA;
    Collider *objectB;

    ColliderPair(Collider *a, Collider *b)
    {
        if (a < b)
        {
            objectA = a;
            objectB = b;
        }
        else
        {
            objectA = b;
            objectB = a;
        }
    }

    bool operator==(const ColliderPair &other) const
    {
        return (objectA == other.objectA && objectB == other.objectB) || (objectA == other.objectB && objectB == other.objectA);
    }

    class HashFunction
    {
    public:
        size_t operator()(const ColliderPair &collision) const
        {
            // sort them so that the order of the objects doesn't matter
            const Collider *objectA = collision.objectA;
            const Collider *objectB = collision.objectB;

            if (objectA > objectB)
            {
                std::swap(objectA, objectB);
            }

            size_t hash1 = std::hash<const Collider *>()(objectA);
            size_t hash2 = std::hash<const Collider *>()(objectB);
            return hash1 ^ (hash2 << 1); // Combine the two hashes
        }
    };
};

struct PairState
{
    uint8_t axisMask = 0; // bit 0 = X, bit 1 = Y
    bool isInCandidateSet = false;
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
    // void checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk);
    // void checkForCollisionsWithCheckpoint(LocalSortArray *chunk);
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis);
    // void removePairsForCollider(Collider *collider);

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<ColliderPair, ColliderPair::HashFunction> candidateCollisions;
    std::unordered_map<ColliderPair, PairState, ColliderPair::HashFunction> pairStates;

    friend class SegmentedIntervalList;
};