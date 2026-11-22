#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

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
        bool operator!=(const const_iterator &other) const noexcept
        {
            return cur != other.cur;
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
    FlatHashSet(FlatHashSet &&) noexcept = default;
    FlatHashSet &operator=(FlatHashSet &&) noexcept = default;

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
        const size_t mask = slots.size() - 1;
        size_t idx = Hash{}(key) & mask;
        while (!IsEmpty{}(slots[idx]))
        {
            if (Equal{}(slots[idx], key))
            {
                return true;
            }
            idx = (idx + 1) & mask;
        }
        return false;
    }

    bool insert(const Key &key)
    {
        if (slots.empty() || (count + 1) * 10 >= slots.size() * 7)
        {
            rehash(slots.empty() ? 8 : slots.size() * 2);
        }
        const size_t mask = slots.size() - 1;
        size_t idx = Hash{}(key) & mask;
        while (!IsEmpty{}(slots[idx]))
        {
            if (Equal{}(slots[idx], key))
            {
                return false;
            }
            idx = (idx + 1) & mask;
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
        const size_t mask = slots.size() - 1;
        size_t hole = Hash{}(key) & mask;
        while (!IsEmpty{}(slots[hole]))
        {
            if (Equal{}(slots[hole], key))
            {
                break;
            }
            hole = (hole + 1) & mask;
        }
        if (IsEmpty{}(slots[hole]))
        {
            return 0;
        }

        --count;
        size_t curr = (hole + 1) & mask;
        while (!IsEmpty{}(slots[curr]))
        {
            const size_t ideal = Hash{}(slots[curr]) & mask;
            if (((curr - ideal) & mask) >= ((curr - hole) & mask))
            {
                slots[hole] = slots[curr];
                hole = curr;
            }
            curr = (curr + 1) & mask;
        }
        slots[hole] = Key{};
        return 1;
    }

    void clear() noexcept
    {
        if (slots.size() > 64)
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
    void rehash(size_t newCapacity)
    {
        std::vector<Key> next(newCapacity);
        const size_t mask = newCapacity - 1;
        for (const Key &slot : slots)
        {
            if (IsEmpty{}(slot))
            {
                continue;
            }
            size_t idx = Hash{}(slot) & mask;
            while (!IsEmpty{}(next[idx]))
            {
                idx = (idx + 1) & mask;
            }
            next[idx] = slot;
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
