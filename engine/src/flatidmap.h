#pragma once

#include <algorithm>
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
        if (entries.empty())
        {
            return nullptr;
        }
        const size_t mask = entries.size() - 1;
        size_t idx = hashKey(key) & mask;
        while (entries[idx].value != nullptr)
        {
            if (entries[idx].key == key)
            {
                return entries[idx].value;
            }
            idx = (idx + 1) & mask;
        }
        return nullptr;
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
            const size_t mask = entries.size() - 1;
            size_t idx = hashKey(key) & mask;
            while (entries[idx].value != nullptr)
            {
                if (entries[idx].key == key)
                {
                    entries[idx].value = value;
                    return;
                }
                idx = (idx + 1) & mask;
            }
            if ((count + 1) * 10 < entries.size() * 7)
            {
                entries[idx] = Entry{key, value};
                ++count;
                return;
            }
        }
        rehash(entries.empty() ? 16 : entries.size() * 2);
        const size_t mask = entries.size() - 1;
        size_t idx = hashKey(key) & mask;
        while (entries[idx].value != nullptr)
        {
            idx = (idx + 1) & mask;
        }
        entries[idx] = Entry{key, value};
        ++count;
    }

    void erase(int key) noexcept
    {
        if (entries.empty())
        {
            return;
        }
        const size_t mask = entries.size() - 1;
        size_t hole = hashKey(key) & mask;
        while (entries[hole].value != nullptr)
        {
            if (entries[hole].key == key)
            {
                break;
            }
            hole = (hole + 1) & mask;
        }
        if (entries[hole].value == nullptr)
        {
            return;
        }

        --count;
        size_t curr = (hole + 1) & mask;
        while (entries[curr].value != nullptr)
        {
            const size_t ideal = hashKey(entries[curr].key) & mask;
            if (((curr - ideal) & mask) >= ((curr - hole) & mask))
            {
                entries[hole] = entries[curr];
                hole = curr;
            }
            curr = (curr + 1) & mask;
        }
        entries[hole] = Entry{};
    }

    void reserve(size_t expected)
    {
        const size_t needed = (expected * 10) / 7 + 2;
        size_t cap = 16;
        while (cap < needed)
        {
            cap *= 2;
        }
        if (cap > entries.size())
        {
            rehash(cap);
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
    struct Entry
    {
        int key = 0;
        BaseObject *value = nullptr;
    };

    static size_t hashKey(int key) noexcept
    {
        uint32_t x = static_cast<uint32_t>(key);
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return static_cast<size_t>(x);
    }

    void rehash(size_t newCapacity)
    {
        std::vector<Entry> next(newCapacity);
        const size_t mask = newCapacity - 1;
        for (const Entry &entry : entries)
        {
            if (entry.value == nullptr)
            {
                continue;
            }
            size_t idx = hashKey(entry.key) & mask;
            while (next[idx].value != nullptr)
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
} // namespace platformator_detail
