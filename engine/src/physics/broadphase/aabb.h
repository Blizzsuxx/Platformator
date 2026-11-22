#pragma once

#include <algorithm>
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
    void remove(Collider *element);
    void remove(ColliderBinding *binding);
    void repair(Collider *element);
    void repair(ColliderBinding *binding);
    ColliderBinding *getBinding(Collider *element);
    SegmentedIntervalList *getIntervalListX();
    SegmentedIntervalList *getIntervalListY();
    template <typename Fn>
    void forEachRegisteredCollider(Fn &&fn) const
    {
        bindingMap.forEach([&](Collider *collider, ColliderBinding *) {
            fn(collider);
        });
    }
    template <typename Visitor>
    void forEachOverlappingAABB(float minX, float maxX, float minY, float maxY, Visitor &&visitor) const
    {
        intervalListX.forEachOverlappingInterval(minX, maxX, [&](Collider *collider)
                                                 {
            const float cMinX = collider->getXProjections()->getMin()->getProjectedPosition();
            const float cMaxX = collider->getXProjections()->getMax()->getProjectedPosition();
            const float cMinY = collider->getYProjections()->getMin()->getProjectedPosition();
            const float cMaxY = collider->getYProjections()->getMax()->getProjectedPosition();
            if (cMaxX >= minX && cMinX <= maxX && cMaxY >= minY && cMinY <= maxY)
            {
                visitor(collider, cMinX, cMaxX, cMinY, cMaxY);
            } });
    }
    // sort the array with insertion sort, sort from lowest to highest
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

        ColliderBinding *find(Collider *key) const noexcept
        {
            if (entries.empty())
            {
                return nullptr;
            }
            const size_t mask = entries.size() - 1;
            size_t idx = hashPtr(key) & mask;
            while (entries[idx].key != nullptr)
            {
                if (entries[idx].key == key)
                {
                    return entries[idx].value;
                }
                idx = (idx + 1) & mask;
            }
            return nullptr;
        }

        template <typename Factory>
        ColliderBinding *emplaceIfAbsent(Collider *key, Factory &&factory)
        {
            if (entries.empty() || (count + 1) * 10 >= entries.size() * 7)
            {
                rehash(entries.empty() ? 8 : entries.size() * 2);
            }
            const size_t mask = entries.size() - 1;
            size_t idx = hashPtr(key) & mask;
            while (entries[idx].key != nullptr)
            {
                if (entries[idx].key == key)
                {
                    return nullptr;
                }
                idx = (idx + 1) & mask;
            }
            ColliderBinding *binding = factory();
            entries[idx] = Entry{key, binding};
            ++count;
            return binding;
        }

        void erase(Collider *key) noexcept
        {
            if (entries.empty())
            {
                return;
            }
            const size_t mask = entries.size() - 1;
            size_t hole = hashPtr(key) & mask;
            while (entries[hole].key != nullptr)
            {
                if (entries[hole].key == key)
                {
                    break;
                }
                hole = (hole + 1) & mask;
            }
            if (entries[hole].key == nullptr)
            {
                return;
            }

            --count;
            size_t curr = (hole + 1) & mask;
            while (entries[curr].key != nullptr)
            {
                const size_t ideal = hashPtr(entries[curr].key) & mask;
                if (((curr - ideal) & mask) >= ((curr - hole) & mask))
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

        template <typename Fn>
        void forEach(Fn &&fn) const
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
        static size_t hashPtr(const Collider *key) noexcept
        {
            uint64_t x = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(key) >> 3);
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            return static_cast<size_t>(x);
        }

        void rehash(size_t newCapacity)
        {
            std::vector<Entry> next(newCapacity);
            const size_t mask = newCapacity - 1;
            for (const Entry &entry : entries)
            {
                if (entry.key == nullptr)
                {
                    continue;
                }
                size_t idx = hashPtr(entry.key) & mask;
                while (next[idx].key != nullptr)
                {
                    idx = (idx + 1) & mask;
                }
                next[idx] = entry;
            }
            entries.swap(next);
        }

        std::vector<Entry> entries;
        size_t count = 0;
    };

    BindingSlot *allocateBindingSlot();
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
