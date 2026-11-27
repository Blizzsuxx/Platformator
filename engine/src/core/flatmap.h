#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include "core/flathash.h"
#include "core/flattable.h"

// Open-addressing linear-probing hash map for non-null pointer values.
// Uses value == nullptr as the empty sentinel so all key values (including 0 / default keys) are valid.
template <typename Key, typename ValuePtr, typename Hash, typename Equal = std::equal_to<Key>,
          size_t InitialCapacity = 8, size_t MaxRetainedClearCapacity = SIZE_MAX>
class FlatHashMap
{
    static_assert(std::is_pointer_v<ValuePtr>,
                  "FlatHashMap requires a pointer value type because nullptr marks empty slots.");

  public:
    struct Entry
    {
        Key key{};
        ValuePtr value = nullptr;
    };

    FlatHashMap() = default;
    ~FlatHashMap() = default;
    FlatHashMap(const FlatHashMap &) = default;
    FlatHashMap &operator=(const FlatHashMap &) = default;
    FlatHashMap(FlatHashMap &&) noexcept = default;
    FlatHashMap &operator=(FlatHashMap &&) noexcept = default;

    bool empty() const noexcept
    {
        return table.empty();
    }

    size_t size() const noexcept
    {
        return table.size();
    }

    ValuePtr find(const Key &key) const noexcept
    {
        const Entry *entry = table.find(key);
        return entry != nullptr ? entry->value : nullptr;
    }

    bool contains(const Key &key) const noexcept
    {
        return find(key) != nullptr;
    }

    void insertOrAssign(const Key &key, ValuePtr value)
    {
        assert(value != nullptr);
        table.insertOrAssign(
            key, [&]() { return Entry{key, value}; }, [&](Entry &existing) { existing.value = value; });
    }

    template <typename Factory>
    ValuePtr emplaceIfAbsent(const Key &key, Factory &&factory)
    {
        Entry *entry = table.emplaceIfAbsent(key, [&]() {
            ValuePtr value = factory();
            assert(value != nullptr);
            return Entry{key, value};
        });
        return entry != nullptr ? entry->value : nullptr;
    }

    template <typename Factory>
    ValuePtr findOrEmplace(const Key &key, Factory &&factory)
    {
        Entry &entry = table.findOrEmplace(key, [&]() {
            ValuePtr value = factory();
            assert(value != nullptr);
            return Entry{key, value};
        });
        return entry.value;
    }

    bool erase(const Key &key) noexcept
    {
        return table.erase(key);
    }

    ValuePtr extract(const Key &key) noexcept
    {
        ValuePtr extracted = nullptr;
        table.erase(key, [&](const Entry &entry) { extracted = entry.value; });
        return extracted;
    }

    void reserve(size_t expected)
    {
        table.reserve(expected);
    }

    void clear() noexcept
    {
        table.clear();
    }

    void clearAndRelease() noexcept
    {
        table.clearAndRelease();
    }

    void swap(FlatHashMap &other) noexcept
    {
        table.swap(other.table);
    }

    template <typename Fn>
    void forEach(Fn &&fn) const
    {
        table.forEach([&](const Entry &entry) { fn(entry.key, entry.value); });
    }

  private:
    struct Traits
    {
        using KeyType = Key;

        static const Key &key(const Entry &entry) noexcept
        {
            return entry.key;
        }

        static bool isEmpty(const Entry &entry) noexcept
        {
            return entry.value == nullptr;
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

    platformator_detail::FlatHashTable<Entry, Traits, InitialCapacity, MaxRetainedClearCapacity> table;
};

template <typename Key, typename Value, size_t InitialCapacity = 8, size_t MaxRetainedClearCapacity = SIZE_MAX>
using FlatPointerMap =
    FlatHashMap<Key *, Value *, FlatPointerHash<Key>, std::equal_to<Key *>, InitialCapacity, MaxRetainedClearCapacity>;
