#pragma once

#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>
#include "segmentedintervallist.h"
#include "platformator/collider.h"
#include "collision.h"
#include "colliderpair.h"

class Grid;

class AABBPair
{
  public:
    AABBPair(Collider *a, Collider *b)
        : AABBPair(ProjectionOwner{a, ProjectionOwnerKind::Collider},
                   ProjectionOwner{b, ProjectionOwnerKind::Collider})
    {
    }

    AABBPair(ProjectionOwner a, ProjectionOwner b);

    ~AABBPair()
    {
    }
    bool operator==(const AABBPair &other) const
    {
        return a == other.a && b == other.b;
    }

    class HashFunction
    {
      public:
        size_t operator()(const AABBPair &pair) const
        {
            const size_t hash1 = std::hash<void *>{}(pair.a);
            const size_t hash2 = std::hash<void *>{}(pair.b);
            return hash1 ^ (hash2 << 1);
        }
    };

    void *a;
    void *b;
};

class AABB
{
  public:
    using PairEligibility = bool (*)(void *, ProjectionOwner, ProjectionOwner);
    using PairChange = void (*)(void *, ProjectionOwner, ProjectionOwner, int);

    struct PairPolicy
    {
        void *context;
        ProjectionOwnerKind endpointKind;
        PairEligibility eligible;
        PairChange changed;
        bool directPhysics = false;
    };

    struct ColliderBinding
    {
        void *endpoint;
        BoundingRadiusProjectionAxisBinding xBinding;
        BoundingRadiusProjectionAxisBinding yBinding;

        explicit ColliderBinding(Collider *collider)
            : ColliderBinding(ProjectionOwner{collider, ProjectionOwnerKind::Collider},
                              collider->getXProjections(), collider->getYProjections())
        {
        }

        ColliderBinding(ProjectionOwner endpoint, BoundingRadiusProjectionAxis *xAxis,
                        BoundingRadiusProjectionAxis *yAxis)
            : endpoint(endpoint.pointer), xBinding(xAxis), yBinding(yAxis)
        {
        }
    };

    AABB(Grid *owner);
    explicit AABB(PairPolicy pairPolicy);
    ~AABB();

    void add(Collider *element);
    void add(ProjectionOwner endpoint, BoundingRadiusProjectionAxis *xAxis,
             BoundingRadiusProjectionAxis *yAxis);
    void remove(Collider *element);
    void remove(ProjectionOwner endpoint);
    void remove(ColliderBinding *binding);
    void repair(Collider *element);
    void repair(ProjectionOwner endpoint);
    void repair(ColliderBinding *binding);
    ColliderBinding *getBinding(Collider *element);
    ColliderBinding *getBinding(ProjectionOwner endpoint);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    // sort the array with insertion sort, sort from lowest to highest
    bool getIsEmpty() const;
    // Owned dynamic payload; excludes allocator headers and the inline owner object.
    size_t getOwnedCapacityBytes() const;

  private:
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB);
    void axisOverlapBegin(ProjectionOwner endpointA, ProjectionOwner endpointB);
    void axisOverlapEnd(ProjectionOwner endpointA, ProjectionOwner endpointB);
    void overlapBeginCheckpoint(Collider *colliderA, Collider *colliderB);
    void overlapEndCheckpoint(Collider *colliderA, Collider *colliderB);
    void overlapBeginCheckpoint(ProjectionOwner endpointA, ProjectionOwner endpointB);
    void overlapEndCheckpoint(ProjectionOwner endpointA, ProjectionOwner endpointB);
    bool checkOverlapOnAxis(ProjectionOwner endpointA, ProjectionOwner endpointB) const;
    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    std::unordered_set<AABBPair, AABBPair::HashFunction> pairsWithBothAxisOverlapping;
    std::unordered_map<void *, ColliderBinding> colliderBindings;
    PairPolicy pairPolicy;

    friend class Grid;
    friend class GridCell;
    friend class SegmentedIntervalList;
};
