#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

class BaseObject;

namespace platformator_detail
{
// Open-addressing linear-probing ID table with backward-shift deletion (no tombstones).
// Uses value == nullptr as the empty sentinel so all 32-bit signed file IDs are valid keys.
class FlatIdMap
{
  public:
    FlatIdMap() = default;
    FlatIdMap(const FlatIdMap &) = default;
    FlatIdMap &operator=(const FlatIdMap &) = default;

    FlatIdMap(FlatIdMap &&other) noexcept
    {
        swap(other);
    }

    FlatIdMap &operator=(FlatIdMap &&other) noexcept
    {
        FlatIdMap moved(std::move(other));
        swap(moved);
        return *this;
    }

    BaseObject *find(int key) const noexcept
    {
        if (count == 0)
        {
            return nullptr;
        }
        return entries[findSlot(key)].value;
    }

    void insertOrAssign(int key, BaseObject *value)
    {
        assert(value != nullptr);
        if (value == nullptr)
        {
            erase(key);
            return;
        }

        if (!entries.empty())
        {
            const size_t idx = findSlot(key);
            if (entries[idx].value != nullptr)
            {
                entries[idx].value = value;
                return;
            }
            if (!exceedsLoadFactor(count + 1))
            {
                entries[idx] = Entry{key, value};
                ++count;
                return;
            }
        }

        rehash(entries.empty() ? INITIAL_CAPACITY : entries.size() * 2);
        entries[findEmptySlot(entries, key)] = Entry{key, value};
        ++count;
    }

    void erase(int key) noexcept
    {
        if (count == 0)
        {
            return;
        }

        const size_t idx = findSlot(key);
        if (entries[idx].value == nullptr)
        {
            return;
        }

        eraseAt(idx);
    }

    void reserve(size_t expected)
    {
        const size_t minSlots = (expected * LOAD_FACTOR_DEN) / LOAD_FACTOR_NUM + 2;
        const size_t capacity = std::max(INITIAL_CAPACITY, std::bit_ceil(minSlots));
        if (capacity > entries.size())
        {
            rehash(capacity);
        }
    }

    void clear() noexcept
    {
        if (count > 0)
        {
            std::fill(entries.begin(), entries.end(), Entry{});
            count = 0;
        }
    }

    void swap(FlatIdMap &other) noexcept
    {
        entries.swap(other.entries);
        std::swap(count, other.count);
    }

    size_t size() const noexcept
    {
        return count;
    }

    bool empty() const noexcept
    {
        return count == 0;
    }

  private:
    static constexpr size_t INITIAL_CAPACITY = 16;
    static constexpr size_t LOAD_FACTOR_NUM = 7;
    static constexpr size_t LOAD_FACTOR_DEN = 10;

    struct Entry
    {
        int key = 0;
        BaseObject *value = nullptr;
    };

    static size_t hashKey(int key) noexcept
    {
        // Low-bias 32-bit integer finalizer.
        uint32_t x = static_cast<uint32_t>(key);
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return static_cast<size_t>(x);
    }

    static size_t probeDistance(size_t from, size_t to, size_t mask) noexcept
    {
        return (to - from) & mask;
    }

    static size_t findEmptySlot(const std::vector<Entry> &table, int key) noexcept
    {
        const size_t mask = table.size() - 1;
        size_t idx = hashKey(key) & mask;
        while (table[idx].value != nullptr)
        {
            idx = (idx + 1) & mask;
        }
        return idx;
    }

    bool exceedsLoadFactor(size_t nextCount) const noexcept
    {
        return nextCount * LOAD_FACTOR_DEN >= entries.size() * LOAD_FACTOR_NUM;
    }

    size_t findSlot(int key) const noexcept
    {
        assert(!entries.empty());
        const size_t mask = entries.size() - 1;
        size_t idx = hashKey(key) & mask;
        while (entries[idx].value != nullptr && entries[idx].key != key)
        {
            idx = (idx + 1) & mask;
        }
        return idx;
    }

    void eraseAt(size_t hole) noexcept
    {
        --count;
        const size_t mask = entries.size() - 1;
        size_t curr = (hole + 1) & mask;

        // Backward-shift any following entry whose probe chain crosses the hole.
        while (entries[curr].value != nullptr)
        {
            const size_t ideal = hashKey(entries[curr].key) & mask;
            if (probeDistance(ideal, curr, mask) >= probeDistance(hole, curr, mask))
            {
                entries[hole] = entries[curr];
                hole = curr;
            }
            curr = (curr + 1) & mask;
        }

        entries[hole] = Entry{};
    }

    void rehash(size_t newCapacity)
    {
        std::vector<Entry> next(newCapacity);
        for (const Entry &entry : entries)
        {
            if (entry.value != nullptr)
            {
                next[findEmptySlot(next, entry.key)] = entry;
            }
        }
        entries.swap(next);
    }

    std::vector<Entry> entries;
    size_t count = 0;
};
} // namespace platformator_detail
