#pragma once

#include <deque>
#include <unordered_map>
#include <vector>
#include "segmentedintervallist.h"
#include "collider.h"
#include "collision.h"
#include "colliderpair.h"

class Grid;

enum Axis : uint8_t
{
    NONE = 0,
    X = 1,
    Y = 2,
    ALL_AXES = 3
};

class AABBPair
{
public:
    AABBPair(Collider *a, Collider *b) : a(a), b(b)
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
};

class AABB
{
public:
    struct ColliderBinding
    {
        Collider *collider;
        BoundingRadiusProjectionAxisBinding xBinding;
        BoundingRadiusProjectionAxisBinding yBinding;

        explicit ColliderBinding(Collider *collider)
            : collider(collider), xBinding(collider->getXProjections()), yBinding(collider->getYProjections())
        {
        }

        void bind(Collider *newCollider)
        {
            collider = newCollider;
            xBinding.bind(newCollider->getXProjections());
            yBinding.bind(newCollider->getYProjections());
        }
    };

    AABB(Grid *owner);
    ~AABB();

    void add(Collider *element);
    void remove(Collider *element);
    void remove(ColliderBinding *binding);
    void repair(Collider *element);
    void repair(ColliderBinding *binding);
    ColliderBinding *getBinding(Collider *element);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    // sort the array with insertion sort, sort from lowest to highest
    bool getIsEmpty() const;

private:
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis);
    void overlapBeginCheckpoint(Collider *colliderA, Collider *colliderB);
    void overlapEndCheckpoint(Collider *colliderA, Collider *colliderB);
    bool checkOverlapOnAxis(Collider *colliderA, Collider *colliderB) const;

    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<AABBPair, AABBPair::HashFunction> pairsWithBothAxisOverlapping;
    std::unordered_map<Collider *, ColliderBinding> colliderBindings;
    Grid *owner;

    friend class SegmentedIntervalList;
};