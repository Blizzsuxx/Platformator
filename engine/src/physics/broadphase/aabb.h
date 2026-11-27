#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>
#include "core/flatset.h"
#include "physics/broadphase/segmentedintervallist.h"
#include "platformator/collider.h"
#include "physics/collision.h"
#include "physics/colliderpair.h"

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
            const uintptr_t p1 = reinterpret_cast<uintptr_t>(pair.a);
            const uintptr_t p2 = reinterpret_cast<uintptr_t>(pair.b);
            uint64_t x = static_cast<uint64_t>(p1 >> 3) * 0x9e3779b97f4a7c15ULL;
            x ^= static_cast<uint64_t>(p2 >> 3) + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            return static_cast<size_t>(x);
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
            const float cMinX = collider->getXProjections()->getMin()->getProjectedPosition();
            const float cMaxX = collider->getXProjections()->getMax()->getProjectedPosition();
            const float cMinY = collider->getYProjections()->getMin()->getProjectedPosition();
            const float cMaxY = collider->getYProjections()->getMax()->getProjectedPosition();
            if (cMaxX >= minX && cMinX <= maxX && cMaxY >= minY && cMinY <= maxY)
            {
                visitor(collider, cMinX, cMaxX, cMinY, cMaxY);
            }
        });
    }
    bool getIsEmpty() const;

  private:
    struct alignas(ColliderBinding) BindingSlot
    {
        std::byte data[sizeof(ColliderBinding)];
    };

    struct BindingBlock
    {
        std::unique_ptr<BindingSlot[]> slots;
        size_t capacity = 0;
        size_t used = 0;
    };

    class FlatBindingMap
    {
      public:
        struct Entry
        {
            Collider *key = nullptr;
            ColliderBinding *value = nullptr;
        };

        size_t size() const noexcept
        {
            return count;
        }

        ColliderBinding *find(Collider *key) const noexcept
        {
            if (count == 0)
            {
                return nullptr;
            }
            const Entry &entry = entries[findSlot(key)];
            return entry.key != nullptr ? entry.value : nullptr;
        }

        template <typename Factory> ColliderBinding *emplaceIfAbsent(Collider *key, Factory &&factory)
        {
            if (entries.empty() || exceedsLoadFactor(count + 1))
            {
                rehash(entries.empty() ? INITIAL_CAPACITY : entries.size() * 2);
            }
            const size_t idx = findSlot(key);
            if (entries[idx].key != nullptr)
            {
                return nullptr;
            }
            ColliderBinding *binding = factory();
            entries[idx] = Entry{key, binding};
            ++count;
            return binding;
        }

        void erase(Collider *key) noexcept
        {
            if (count == 0)
            {
                return;
            }
            size_t hole = findSlot(key);
            if (entries[hole].key == nullptr)
            {
                return;
            }

            --count;
            const size_t mask = entries.size() - 1;
            size_t curr = (hole + 1) & mask;
            while (entries[curr].key != nullptr)
            {
                const size_t ideal = FlatPointerHash<Collider>{}(entries[curr].key) & mask;
                if (probeDistance(ideal, curr, mask) >= probeDistance(hole, curr, mask))
                {
                    entries[hole] = entries[curr];
                    hole = curr;
                }
                curr = (curr + 1) & mask;
            }
            entries[hole] = Entry{};
        }

        void clear() noexcept
        {
            if (count > 0)
            {
                std::fill(entries.begin(), entries.end(), Entry{});
                count = 0;
            }
        }

        template <typename Fn> void forEach(Fn &&fn) const
        {
            if (count == 0)
            {
                return;
            }
            for (const Entry &entry : entries)
            {
                if (entry.key != nullptr)
                {
                    fn(entry.key, entry.value);
                }
            }
        }

      private:
        static constexpr size_t INITIAL_CAPACITY = 8;
        static constexpr size_t LOAD_FACTOR_NUM = 7;
        static constexpr size_t LOAD_FACTOR_DEN = 10;

        static size_t probeDistance(size_t from, size_t to, size_t mask) noexcept
        {
            return (to - from) & mask;
        }

        static size_t findEmptySlot(const std::vector<Entry> &table, Collider *key) noexcept
        {
            const size_t mask = table.size() - 1;
            size_t idx = FlatPointerHash<Collider>{}(key) & mask;
            while (table[idx].key != nullptr)
            {
                idx = (idx + 1) & mask;
            }
            return idx;
        }

        bool exceedsLoadFactor(size_t nextCount) const noexcept
        {
            return nextCount * LOAD_FACTOR_DEN >= entries.size() * LOAD_FACTOR_NUM;
        }

        size_t findSlot(Collider *key) const noexcept
        {
            assert(!entries.empty());
            const size_t mask = entries.size() - 1;
            size_t idx = FlatPointerHash<Collider>{}(key) & mask;
            while (entries[idx].key != nullptr && entries[idx].key != key)
            {
                idx = (idx + 1) & mask;
            }
            return idx;
        }

        void rehash(size_t newCapacity)
        {
            std::vector<Entry> next(newCapacity);
            for (const Entry &entry : entries)
            {
                if (entry.key != nullptr)
                {
                    next[findEmptySlot(next, entry.key)] = entry;
                }
            }
            entries.swap(next);
        }

        std::vector<Entry> entries;
        size_t count = 0;
    };

    BindingSlot *allocateBindingSlot();
    void repairAxis(SegmentedIntervalList &list, BoundingRadiusProjectionAxisBinding &axisBinding);
    bool tryClearAllRemoved(const std::vector<Collider *> &colliders);
    void axisOverlapBegin(Collider *colliderA, Collider *colliderB);
    void axisOverlapEnd(Collider *colliderA, Collider *colliderB);
    void overlapBeginCheckpoint(Collider *colliderA, Collider *colliderB);
    void overlapEndCheckpoint(Collider *colliderA, Collider *colliderB);
    bool checkOverlapOnAxis(Collider *colliderA, Collider *colliderB) const;

    // Declare binding storage before intervalLists so proxies outlive interval chunks if implicitly torn down.
    std::vector<BindingBlock> bindingBlocks;
    std::vector<BindingSlot *> freeBindings;
    FlatBindingMap bindingMap;
    SegmentedIntervalList intervalListX;
    SegmentedIntervalList intervalListY;
    FlatHashSet<AABBPair, AABBPair::HashFunction, std::equal_to<AABBPair>, AABBPair::IsEmpty>
        pairsWithBothAxisOverlapping;
    Grid *owner;

    friend class Grid;
    friend class SegmentedIntervalList;
};
