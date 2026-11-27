#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>

// Open-addressing linear-probing hash set with backward-shift deletion (no tombstones).
// Empty slots are identified by IsEmpty{}(slot) and reset to Key{}.
template <typename Key, typename Hash, typename Equal, typename IsEmpty>
class FlatHashSet
{
  public:
    class const_iterator
    {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Key;
        using difference_type = std::ptrdiff_t;
        using pointer = const Key *;
        using reference = const Key &;

        const_iterator() noexcept = default;
        const_iterator(const Key *cur, const Key *end) noexcept : cur(cur), end(end)
        {
            skipEmpty();
        }

        const Key &operator*() const noexcept
        {
            return *cur;
        }
        const Key *operator->() const noexcept
        {
            return cur;
        }

        const_iterator &operator++() noexcept
        {
            ++cur;
            skipEmpty();
            return *this;
        }

        const_iterator operator++(int) noexcept
        {
            const_iterator copy = *this;
            ++(*this);
            return copy;
        }

        bool operator==(const const_iterator &other) const noexcept
        {
            return cur == other.cur;
        }

      private:
        void skipEmpty() noexcept
        {
            while (cur != end && IsEmpty{}(*cur))
            {
                ++cur;
            }
        }

        const Key *cur = nullptr;
        const Key *end = nullptr;
    };

    using iterator = const_iterator;

    FlatHashSet() = default;
    ~FlatHashSet() = default;
    FlatHashSet(const FlatHashSet &) = default;
    FlatHashSet &operator=(const FlatHashSet &) = default;

    FlatHashSet(FlatHashSet &&other) noexcept
    {
        swap(other);
    }

    FlatHashSet &operator=(FlatHashSet &&other) noexcept
    {
        FlatHashSet moved(std::move(other));
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

    bool contains(const Key &key) const noexcept
    {
        if (count == 0)
        {
            return false;
        }
        return !IsEmpty{}(slots[findSlot(key)]);
    }

    bool insert(const Key &key)
    {
        assert(!IsEmpty{}(key));
        if (slots.empty() || exceedsLoadFactor(count + 1))
        {
            rehash(slots.empty() ? INITIAL_CAPACITY : slots.size() * 2);
        }

        const size_t idx = findSlot(key);
        if (!IsEmpty{}(slots[idx]))
        {
            return false;
        }

        slots[idx] = key;
        ++count;
        return true;
    }

    size_t erase(const Key &key) noexcept
    {
        if (count == 0)
        {
            return 0;
        }

        const size_t idx = findSlot(key);
        if (IsEmpty{}(slots[idx]))
        {
            return 0;
        }

        eraseAt(idx);
        return 1;
    }

    void clear() noexcept
    {
        if (slots.size() > MAX_RETAINED_CLEAR_CAPACITY)
        {
            slots.clear();
            count = 0;
        }
        else if (count > 0)
        {
            std::fill(slots.begin(), slots.end(), Key{});
            count = 0;
        }
    }

    void swap(FlatHashSet &other) noexcept
    {
        slots.swap(other.slots);
        std::swap(count, other.count);
    }

    const_iterator begin() const noexcept
    {
        if (count == 0)
        {
            return end();
        }
        return const_iterator(slots.data(), slots.data() + slots.size());
    }

    const_iterator end() const noexcept
    {
        const Key *finish = slots.data() + slots.size();
        return const_iterator(finish, finish);
    }

  private:
    static constexpr size_t INITIAL_CAPACITY = 8;
    static constexpr size_t MAX_RETAINED_CLEAR_CAPACITY = 64;
    static constexpr size_t LOAD_FACTOR_NUM = 7;
    static constexpr size_t LOAD_FACTOR_DEN = 10;

    static size_t probeDistance(size_t from, size_t to, size_t mask) noexcept
    {
        return (to - from) & mask;
    }

    static size_t findEmptySlot(const std::vector<Key> &table, const Key &key) noexcept
    {
        const size_t mask = table.size() - 1;
        size_t idx = Hash{}(key) & mask;
        while (!IsEmpty{}(table[idx]))
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
        size_t idx = Hash{}(key) & mask;
        while (!IsEmpty{}(slots[idx]) && !Equal{}(slots[idx], key))
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
        while (!IsEmpty{}(slots[curr]))
        {
            const size_t ideal = Hash{}(slots[curr]) & mask;
            if (probeDistance(ideal, curr, mask) >= probeDistance(hole, curr, mask))
            {
                slots[hole] = slots[curr];
                hole = curr;
            }
            curr = (curr + 1) & mask;
        }

        slots[hole] = Key{};
    }

    void rehash(size_t newCapacity)
    {
        std::vector<Key> next(newCapacity);
        for (const Key &slot : slots)
        {
            if (!IsEmpty{}(slot))
            {
                next[findEmptySlot(next, slot)] = slot;
            }
        }
        slots.swap(next);
    }

    std::vector<Key> slots;
    size_t count = 0;
};

template <typename T>
struct FlatPointerHash
{
    size_t operator()(const T *key) const noexcept
    {
        // SplitMix64 finalizer on pointer value (ignoring low alignment bits).
        uint64_t x = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(key) >> 3);
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return static_cast<size_t>(x);
    }
};

template <typename T>
struct FlatPointerIsEmpty
{
    bool operator()(const T *key) const noexcept
    {
        return key == nullptr;
    }
};

template <typename T>
using FlatPointerSet = FlatHashSet<T *, FlatPointerHash<T>, std::equal_to<T *>, FlatPointerIsEmpty<T>>;
