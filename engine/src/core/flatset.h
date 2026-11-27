#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>

#include "core/flathash.h"
#include "core/flattable.h"

// Open-addressing linear-probing hash set with backward-shift deletion (no tombstones).
// Empty slots are identified by IsEmpty{}(slot) and reset to Key{}.
template <typename Key, typename Hash, typename Equal, typename IsEmpty, size_t InitialCapacity = 8,
          size_t MaxRetainedClearCapacity = 64>
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
    FlatHashSet(FlatHashSet &&) noexcept = default;
    FlatHashSet &operator=(FlatHashSet &&) noexcept = default;

    bool empty() const noexcept
    {
        return table.empty();
    }

    size_t size() const noexcept
    {
        return table.size();
    }

    bool contains(const Key &key) const noexcept
    {
        return table.find(key) != nullptr;
    }

    bool insert(const Key &key)
    {
        assert(!IsEmpty{}(key));
        return table.tryInsert(key, [&]() -> const Key & { return key; });
    }

    size_t erase(const Key &key) noexcept
    {
        return table.erase(key) ? 1 : 0;
    }

    void clear() noexcept
    {
        table.clear();
    }

    const_iterator begin() const noexcept
    {
        if (table.empty())
        {
            return end();
        }
        return const_iterator(table.data(), table.data() + table.slotCount());
    }

    const_iterator end() const noexcept
    {
        const Key *finish = table.data() + table.slotCount();
        return const_iterator(finish, finish);
    }

  private:
    struct Traits
    {
        using KeyType = Key;

        static const Key &key(const Key &slot) noexcept
        {
            return slot;
        }

        static bool isEmpty(const Key &slot) noexcept
        {
            return IsEmpty{}(slot);
        }

        static size_t hash(const Key &k) noexcept
        {
            return Hash{}(k);
        }

        static bool equals(const Key &lhs, const Key &rhs) noexcept
        {
            return Equal{}(lhs, rhs);
        }
    };

    platformator_detail::FlatHashTable<Key, Traits, InitialCapacity, MaxRetainedClearCapacity> table;
};

template <typename T>
using FlatPointerSet = FlatHashSet<T *, FlatPointerHash<T>, std::equal_to<T *>, FlatPointerIsEmpty<T>>;
