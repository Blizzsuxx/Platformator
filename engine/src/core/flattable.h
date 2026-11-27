#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace platformator_detail
{
// Open-addressing linear-probing hash table with backward-shift deletion (no tombstones).
// Empty slots are identified by Traits::isEmpty(slot) and reset to Slot{}.
template <typename Slot, typename Traits, size_t InitialCapacity = 8, size_t MaxRetainedClearCapacity = SIZE_MAX>
class FlatHashTable
{
    static_assert(InitialCapacity >= 2 && std::has_single_bit(InitialCapacity),
                  "InitialCapacity must be a power of two >= 2");

  public:
    using Key = typename Traits::KeyType;

    FlatHashTable() = default;
    ~FlatHashTable() = default;
    FlatHashTable(const FlatHashTable &) = default;
    FlatHashTable &operator=(const FlatHashTable &) = default;

    FlatHashTable(FlatHashTable &&other) noexcept
    {
        swap(other);
    }

    FlatHashTable &operator=(FlatHashTable &&other) noexcept
    {
        FlatHashTable moved(std::move(other));
        swap(moved);
        return *this;
    }

    bool empty() const noexcept
    {
        return count == 0;
    }

    size_t size() const noexcept
    {
        return count;
    }

    const Slot *data() const noexcept
    {
        return slots.data();
    }

    size_t slotCount() const noexcept
    {
        return slots.size();
    }

    const Slot *find(const Key &key) const noexcept
    {
        if (count == 0)
        {
            return nullptr;
        }
        const Slot &slot = slots[findSlot(key)];
        return Traits::isEmpty(slot) ? nullptr : &slot;
    }

    template <typename MakeSlot>
    bool tryInsert(const Key &key, MakeSlot &&makeSlot)
    {
        return emplaceIfAbsent(key, std::forward<MakeSlot>(makeSlot)) != nullptr;
    }

    template <typename MakeSlot, typename OnExisting>
    void insertOrAssign(const Key &key, MakeSlot &&makeSlot, OnExisting &&onExisting)
    {
        auto [slot, inserted] = probeOrInsert(key, std::forward<MakeSlot>(makeSlot));
        if (!inserted)
        {
            onExisting(*slot);
        }
    }

    template <typename MakeSlot>
    Slot *emplaceIfAbsent(const Key &key, MakeSlot &&makeSlot)
    {
        auto [slot, inserted] = probeOrInsert(key, std::forward<MakeSlot>(makeSlot));
        return inserted ? slot : nullptr;
    }

    template <typename MakeSlot>
    Slot &findOrEmplace(const Key &key, MakeSlot &&makeSlot)
    {
        return *probeOrInsert(key, std::forward<MakeSlot>(makeSlot)).first;
    }

    bool erase(const Key &key) noexcept
    {
        return erase(key, [](const Slot &) {});
    }

    template <typename OnFound>
    bool erase(const Key &key, OnFound &&onFound) noexcept
    {
        if (count == 0)
        {
            return false;
        }
        const size_t idx = findSlot(key);
        if (Traits::isEmpty(slots[idx]))
        {
            return false;
        }
        onFound(slots[idx]);
        eraseAt(idx);
        return true;
    }

    void reserve(size_t expected)
    {
        if (expected == 0)
        {
            return;
        }
        const size_t minSlots = (expected * LOAD_FACTOR_DEN) / LOAD_FACTOR_NUM + 2;
        const size_t targetCapacity = std::max(InitialCapacity, std::bit_ceil(minSlots));
        if (targetCapacity > slots.size())
        {
            rehash(targetCapacity);
        }
    }

    void clear() noexcept
    {
        if constexpr (MaxRetainedClearCapacity != SIZE_MAX)
        {
            if (slots.size() > MaxRetainedClearCapacity)
            {
                slots.clear();
                count = 0;
                return;
            }
        }
        if (count > 0)
        {
            std::fill(slots.begin(), slots.end(), Slot{});
            count = 0;
        }
    }

    void clearAndRelease() noexcept
    {
        slots.clear();
        count = 0;
    }

    void swap(FlatHashTable &other) noexcept
    {
        slots.swap(other.slots);
        std::swap(count, other.count);
    }

    template <typename Fn>
    void forEach(Fn &&fn) const
    {
        if (count == 0)
        {
            return;
        }
        for (const Slot &slot : slots)
        {
            if (!Traits::isEmpty(slot))
            {
                fn(slot);
            }
        }
    }

  private:
    template <typename MakeSlot>
    std::pair<Slot *, bool> probeOrInsert(const Key &key, MakeSlot &&makeSlot)
    {
        if (!slots.empty())
        {
            const size_t idx = findSlot(key);
            if (!Traits::isEmpty(slots[idx]))
            {
                return {&slots[idx], false};
            }
            if (!exceedsLoadFactor(count + 1))
            {
                Slot slot = makeSlot();
                assert(!Traits::isEmpty(slot));
                slots[idx] = std::move(slot);
                ++count;
                return {&slots[idx], true};
            }
        }

        rehash(slots.empty() ? InitialCapacity : slots.size() * 2);
        const size_t idx = findEmptySlot(slots, key);
        Slot slot = makeSlot();
        assert(!Traits::isEmpty(slot));
        slots[idx] = std::move(slot);
        ++count;
        return {&slots[idx], true};
    }

    static constexpr size_t LOAD_FACTOR_NUM = 7;
    static constexpr size_t LOAD_FACTOR_DEN = 10;

    static size_t probeDistance(size_t from, size_t to, size_t mask) noexcept
    {
        return (to - from) & mask;
    }

    static size_t findEmptySlot(const std::vector<Slot> &table, const Key &key) noexcept
    {
        const size_t mask = table.size() - 1;
        size_t idx = Traits::hash(key) & mask;
        while (!Traits::isEmpty(table[idx]))
        {
            idx = (idx + 1) & mask;
        }
        return idx;
    }

    bool exceedsLoadFactor(size_t nextCount) const noexcept
    {
        return nextCount * LOAD_FACTOR_DEN >= slots.size() * LOAD_FACTOR_NUM;
    }

    size_t findSlot(const Key &key) const noexcept
    {
        assert(!slots.empty());
        const size_t mask = slots.size() - 1;
        size_t idx = Traits::hash(key) & mask;
        while (!Traits::isEmpty(slots[idx]) && !Traits::equals(Traits::key(slots[idx]), key))
        {
            idx = (idx + 1) & mask;
        }
        return idx;
    }

    void eraseAt(size_t hole) noexcept
    {
        --count;
        const size_t mask = slots.size() - 1;
        size_t curr = (hole + 1) & mask;

        // Backward-shift any following entry whose probe chain crosses the hole.
        while (!Traits::isEmpty(slots[curr]))
        {
            const size_t ideal = Traits::hash(Traits::key(slots[curr])) & mask;
            if (probeDistance(ideal, curr, mask) >= probeDistance(hole, curr, mask))
            {
                slots[hole] = std::move(slots[curr]);
                hole = curr;
            }
            curr = (curr + 1) & mask;
        }

        slots[hole] = Slot{};
    }

    void rehash(size_t newCapacity)
    {
        std::vector<Slot> next(newCapacity);
        for (Slot &slot : slots)
        {
            if (!Traits::isEmpty(slot))
            {
                const size_t idx = findEmptySlot(next, Traits::key(slot));
                next[idx] = std::move(slot);
            }
        }
        slots.swap(next);
    }

    std::vector<Slot> slots;
    size_t count = 0;
};
} // namespace platformator_detail
