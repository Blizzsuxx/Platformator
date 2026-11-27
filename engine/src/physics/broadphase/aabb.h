#pragma once

#include <cstddef>
#include <vector>

#include "core/flathash.h"
#include "core/flatmap.h"
#include "core/flatset.h"
#include "core/slotpool.h"
#include "physics/broadphase/segmentedintervallist.h"
#include "platformator/collider.h"

class Grid;

class AABBPair
{
  public:
    AABBPair() noexcept : a(nullptr), b(nullptr)
    {
    }

    AABBPair(Collider *first, Collider *second) noexcept
        : a(first <= second ? first : second), b(first <= second ? second : first)
    {
    }

    ~AABBPair() = default;

    bool operator==(const AABBPair &other) const noexcept
    {
        return a == other.a && b == other.b;
    }

    struct IsEmpty
    {
        bool operator()(const AABBPair &pair) const noexcept
        {
            return pair.a == nullptr;
        }
    };

    class HashFunction
    {
      public:
        size_t operator()(const AABBPair &pair) const noexcept
        {
            return platformator_detail::hashPointerPair(pair.a, pair.b);
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
        bool pendingRemoval = false;

        explicit ColliderBinding(Collider *collider)
            : collider(collider), xBinding(collider->getXProjections()), yBinding(collider->getYProjections())
        {
        }
    };

    AABB(Grid *owner);
    ~AABB();
    AABB(const AABB &) = delete;
    AABB &operator=(const AABB &) = delete;
    AABB(AABB &&) = delete;
    AABB &operator=(AABB &&) = delete;

    void add(Collider *element);
    void addBatch(const std::vector<Collider *> &colliders);
    void remove(Collider *element);
    void remove(ColliderBinding *binding);
    void removeBatch(const std::vector<Collider *> &colliders);
    void repair(Collider *element);
    void repair(ColliderBinding *binding);
    ColliderBinding *getBinding(Collider *element);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    template <typename Fn> void forEachRegisteredCollider(Fn &&fn) const
    {
        bindingMap.forEach([&](Collider *collider, ColliderBinding *) { fn(collider); });
    }
    template <typename Visitor>
    void forEachOverlappingAABB(float minX, float maxX, float minY, float maxY, Visitor &&visitor) const
    {
        intervalListX.forEachOverlappingInterval(minX, maxX, [&](Collider *collider) {
            const float cMinX = collider->getTightMinX();
            const float cMaxX = collider->getTightMaxX();
            const float cMinY = collider->getTightMinY();
            const float cMaxY = collider->getTightMaxY();
            if (cMaxX >= minX && cMinX <= maxX && cMaxY >= minY && cMinY <= maxY)
            {
                visitor(collider, cMinX, cMaxX, cMinY, cMaxY);
            }
        });
    }
    bool getIsEmpty() const;

  private:
    void repairAxis(SegmentedIntervalList &list, BoundingRadiusProjectionAxisBinding &axisBinding);
    bool tryClearAllRemoved(const std::vector<Collider *> &colliders);
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB);
    void overlapBeginCheckpoint(Collider *colliderA, Collider *colliderB);
    void overlapEndCheckpoint(Collider *colliderA, Collider *colliderB);
    bool checkOverlapOnAxis(Collider *colliderA, Collider *colliderB) const;

    // Declare binding storage before intervalLists so proxies outlive interval chunks if implicitly torn down.
    platformator_detail::ChunkedSlotPool<ColliderBinding, 4, 64> bindingPool;
    FlatPointerMap<Collider, ColliderBinding, 8> bindingMap;
    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    FlatHashSet<AABBPair, AABBPair::HashFunction, std::equal_to<AABBPair>, AABBPair::IsEmpty>
        pairsWithBothAxisOverlapping;
    Grid *owner;

    friend class Grid;
    friend class SegmentedIntervalList;
};
