#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "core/flatmap.h"
#include "core/flatset.h"
#include "core/slotpool.h"
#include "physics/broadphase/projectionproxy.h"
#include "physics/broadphase/segmentedintervallist.h"
#include "platformator/collider.h"

class Grid;
struct PairDelta;

class AABB
{
  public:
    struct ColliderBinding
    {
        Collider *collider;
        platformator_detail::ProjectionAxisBinding xBinding;
        platformator_detail::ProjectionAxisBinding yBinding;
        bool pendingRemoval = false;

        explicit ColliderBinding(Collider *collider) noexcept
            : collider(collider), xBinding(collider, false), yBinding(collider, true)
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
    template <typename Fn> void forEachRegisteredCollider(Fn &&fn) const
    {
        bindingMap.forEach([&](Collider *collider, ColliderBinding *) { fn(collider); });
    }
    template <typename Visitor>
    void forEachOverlappingAABB(float minX, float maxX, float minY, float maxY, Visitor &&visitor) const
    {
        intervalListX.forEachOverlappingInterval(minX, maxX, [&](Collider *collider) {
            if (collider->overlapsTightBounds(minX, maxX, minY, maxY))
            {
                visitor(collider, collider->getTightMinX(), collider->getTightMaxX(), collider->getTightMinY(),
                        collider->getTightMaxY());
            }
        });
    }
    void beginDeltaBatch();
    void endDeltaBatch() noexcept
    {
        activeDeltaSink = nullptr;
    }
    bool getIsEmpty() const;

  private:
    using PairKey = platformator_detail::CanonicalColliderPairKey;

    // Run X and Y axis operations concurrently when allowed, temporarily clearing the thread-bound
    // delta sink so TBB task stealing cannot alias two workers onto the same TLS delta buffer.
    template <typename FnX, typename FnY> void runAxesInParallelOrSerial(bool parallel, FnX &&fnX, FnY &&fnY);

    void repairAxis(SegmentedIntervalList &list, platformator_detail::ProjectionAxisBinding &axisBinding);
    bool tryClearAllRemoved(const std::vector<Collider *> &colliders, std::vector<ColliderBinding *> &outMatched);
    void recordDelta(Collider *colliderA, Collider *colliderB, int delta);
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB);

    // Declare binding storage before intervalLists so proxies outlive interval chunks if implicitly torn down.
    platformator_detail::ChunkedSlotPool<ColliderBinding, 4, 64> bindingPool;
    FlatPointerMap<Collider, ColliderBinding, 8> bindingMap;
    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    FlatHashSet<PairKey, PairKey::Hash, std::equal_to<PairKey>, PairKey::IsEmpty> pairsWithBothAxisOverlapping;
    Grid *owner;
    std::vector<PairDelta> *activeDeltaSink = nullptr;

    friend class Grid;
    friend class SegmentedIntervalList;
};
