#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

#include "colliderpair.h"
#include "gridcell.h"

class Grid;

class FlatCellMap
{
  public:
    struct Node
    {
        GridCellKey first;
        GridCell second;
        size_t activeIndex = SIZE_MAX;

        Node(GridCellKey key, Grid *owner) : first(key), second(key, owner), activeIndex(SIZE_MAX)
        {
        }
    };

    class iterator
    {
      public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = Node;
        using difference_type = std::ptrdiff_t;
        using pointer = Node *;
        using reference = Node &;

        explicit iterator(Node *const *ptr = nullptr) noexcept : ptr(ptr)
        {
        }

        Node &operator*() const noexcept
        {
            return **ptr;
        }
        Node *operator->() const noexcept
        {
            return *ptr;
        }
        iterator &operator++() noexcept
        {
            ++ptr;
            return *this;
        }
        iterator operator++(int) noexcept
        {
            iterator copy = *this;
            ++ptr;
            return copy;
        }
        bool operator==(const iterator &other) const noexcept
        {
            return ptr == other.ptr;
        }
        bool operator!=(const iterator &other) const noexcept
        {
            return ptr != other.ptr;
        }

      private:
        Node *const *ptr;
    };

    class const_iterator
    {
      public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = const Node;
        using difference_type = std::ptrdiff_t;
        using pointer = const Node *;
        using reference = const Node &;

        explicit const_iterator(Node *const *ptr = nullptr) noexcept : ptr(ptr)
        {
        }

        const Node &operator*() const noexcept
        {
            return **ptr;
        }
        const Node *operator->() const noexcept
        {
            return *ptr;
        }
        const_iterator &operator++() noexcept
        {
            ++ptr;
            return *this;
        }
        const_iterator operator++(int) noexcept
        {
            const_iterator copy = *this;
            ++ptr;
            return copy;
        }
        bool operator==(const const_iterator &other) const noexcept
        {
            return ptr == other.ptr;
        }
        bool operator!=(const const_iterator &other) const noexcept
        {
            return ptr != other.ptr;
        }

      private:
        Node *const *ptr;
    };

    FlatCellMap() = default;
    ~FlatCellMap()
    {
        clear();
    }
    FlatCellMap(const FlatCellMap &) = delete;
    FlatCellMap &operator=(const FlatCellMap &) = delete;
    FlatCellMap(FlatCellMap &&) = delete;
    FlatCellMap &operator=(FlatCellMap &&) = delete;

    size_t size() const noexcept
    {
        return activeNodes.size();
    }

    bool empty() const noexcept
    {
        return activeNodes.empty();
    }

    iterator begin() noexcept
    {
        return iterator(activeNodes.data());
    }
    iterator end() noexcept
    {
        return iterator(activeNodes.data() + activeNodes.size());
    }
    const_iterator begin() const noexcept
    {
        return const_iterator(activeNodes.data());
    }
    const_iterator end() const noexcept
    {
        return const_iterator(activeNodes.data() + activeNodes.size());
    }

    GridCell *find(const GridCellKey &key) noexcept
    {
        if (activeNodes.empty())
        {
            return nullptr;
        }
        const size_t mask = table.size() - 1;
        size_t idx = hashKey(key) & mask;
        while (table[idx].node != nullptr)
        {
            if (table[idx].key == key)
            {
                return &table[idx].node->second;
            }
            idx = (idx + 1) & mask;
        }
        return nullptr;
    }

    const GridCell *find(const GridCellKey &key) const noexcept
    {
        if (activeNodes.empty())
        {
            return nullptr;
        }
        const size_t mask = table.size() - 1;
        size_t idx = hashKey(key) & mask;
        while (table[idx].node != nullptr)
        {
            if (table[idx].key == key)
            {
                return &table[idx].node->second;
            }
            idx = (idx + 1) & mask;
        }
        return nullptr;
    }

    GridCell &getOrCreate(const GridCellKey &key, Grid *owner)
    {
        if (!table.empty())
        {
            const size_t mask = table.size() - 1;
            size_t idx = hashKey(key) & mask;
            while (table[idx].node != nullptr)
            {
                if (table[idx].key == key)
                {
                    return table[idx].node->second;
                }
                idx = (idx + 1) & mask;
            }
        }

        if (table.empty() || (activeNodes.size() + 1) * 10 >= table.size() * 7)
        {
            rehash(table.empty() ? 32 : table.size() * 2);
        }

        const size_t mask = table.size() - 1;
        size_t idx = hashKey(key) & mask;
        while (table[idx].node != nullptr)
        {
            idx = (idx + 1) & mask;
        }

        Node *node = nullptr;
        if (!recycledNodes.empty())
        {
            node = recycledNodes.back();
            recycledNodes.pop_back();
            node->first = key;
            node->second.resetKey(key);
        }
        else
        {
            RawSlot *slot = allocateSlot();
            node = std::construct_at(reinterpret_cast<Node *>(slot), key, owner);
        }

        node->activeIndex = activeNodes.size();
        activeNodes.push_back(node);
        table[idx] = Entry{key, node};
        return node->second;
    }

    bool eraseAndMaybeRecycle(const GridCellKey &key, size_t maxRecycled) noexcept
    {
        if (activeNodes.empty())
        {
            return false;
        }
        const size_t mask = table.size() - 1;
        size_t hole = hashKey(key) & mask;
        while (table[hole].node != nullptr)
        {
            if (table[hole].key == key)
            {
                break;
            }
            hole = (hole + 1) & mask;
        }
        if (table[hole].node == nullptr)
        {
            return false;
        }

        Node *node = table[hole].node;

        // Backward-shift deletion preserves probe chains without tombstones.
        size_t curr = (hole + 1) & mask;
        while (table[curr].node != nullptr)
        {
            const size_t ideal = hashKey(table[curr].key) & mask;
            if (((curr - ideal) & mask) >= ((curr - hole) & mask))
            {
                table[hole] = table[curr];
                hole = curr;
            }
            curr = (curr + 1) & mask;
        }
        table[hole] = Entry{};

        const size_t removeIdx = node->activeIndex;
        const size_t lastIdx = activeNodes.size() - 1;
        if (removeIdx != lastIdx)
        {
            Node *moved = activeNodes[lastIdx];
            activeNodes[removeIdx] = moved;
            moved->activeIndex = removeIdx;
        }
        activeNodes.pop_back();
        node->activeIndex = SIZE_MAX;

        if (recycledNodes.size() < maxRecycled)
        {
            recycledNodes.push_back(node);
        }
        else
        {
            std::destroy_at(node);
            freeRawSlots.push_back(reinterpret_cast<RawSlot *>(node));
        }
        return true;
    }

    void clear() noexcept
    {
        for (Node *node : activeNodes)
        {
            std::destroy_at(node);
        }
        for (Node *node : recycledNodes)
        {
            std::destroy_at(node);
        }
        activeNodes.clear();
        recycledNodes.clear();
        freeRawSlots.clear();
        table.clear();
        blocks.clear();
    }

  private:
    struct alignas(Node) RawSlot
    {
        std::byte data[sizeof(Node)];
    };

    struct Block
    {
        std::unique_ptr<RawSlot[]> slots;
        size_t capacity = 0;
        size_t used = 0;
    };

    struct Entry
    {
        GridCellKey key{0, 0};
        Node *node = nullptr;
    };

    static size_t hashKey(const GridCellKey &key) noexcept
    {
        uint64_t x = (static_cast<uint64_t>(static_cast<uint32_t>(key.x)) << 32) |
                     static_cast<uint64_t>(static_cast<uint32_t>(key.y));
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return static_cast<size_t>(x);
    }

    RawSlot *allocateSlot()
    {
        if (!freeRawSlots.empty())
        {
            RawSlot *slot = freeRawSlots.back();
            freeRawSlots.pop_back();
            return slot;
        }
        if (blocks.empty() || blocks.back().used == blocks.back().capacity)
        {
            const size_t prevCap = blocks.empty() ? 0 : blocks.back().capacity;
            const size_t nextCap = prevCap == 0 ? 16 : std::min<size_t>(prevCap * 2, 256);
            Block block;
            block.slots = std::unique_ptr<RawSlot[]>(new RawSlot[nextCap]);
            block.capacity = nextCap;
            block.used = 0;
            blocks.push_back(std::move(block));
        }
        Block &block = blocks.back();
        return &block.slots[block.used++];
    }

    void rehash(size_t newCapacity)
    {
        std::vector<Entry> next(newCapacity);
        const size_t mask = newCapacity - 1;
        for (const Entry &entry : table)
        {
            if (entry.node == nullptr)
            {
                continue;
            }
            size_t idx = hashKey(entry.key) & mask;
            while (next[idx].node != nullptr)
            {
                idx = (idx + 1) & mask;
            }
            next[idx] = entry;
        }
        table.swap(next);
    }

    std::vector<Block> blocks;
    std::vector<RawSlot *> freeRawSlots;
    std::vector<Node *> recycledNodes;
    std::vector<Node *> activeNodes;
    std::vector<Entry> table;
};

class FlatCandidatePairPool
{
  public:
    FlatCandidatePairPool() = default;
    ~FlatCandidatePairPool()
    {
        clear();
    }
    FlatCandidatePairPool(const FlatCandidatePairPool &) = delete;
    FlatCandidatePairPool &operator=(const FlatCandidatePairPool &) = delete;
    FlatCandidatePairPool(FlatCandidatePairPool &&) = delete;
    FlatCandidatePairPool &operator=(FlatCandidatePairPool &&) = delete;

    size_t size() const noexcept
    {
        return count;
    }

    bool empty() const noexcept
    {
        return count == 0;
    }

    const ColliderPair *find(Collider *a, Collider *b) const noexcept
    {
        if (count == 0)
        {
            return nullptr;
        }
        if (a > b)
        {
            std::swap(a, b);
        }
        const size_t mask = table.size() - 1;
        size_t idx = hashPair(a, b) & mask;
        while (table[idx].pair != nullptr)
        {
            if (table[idx].a == a && table[idx].b == b)
            {
                return table[idx].pair;
            }
            idx = (idx + 1) & mask;
        }
        return nullptr;
    }

    const ColliderPair *findOrEmplace(Collider *origA, Collider *origB)
    {
        Collider *a = origA <= origB ? origA : origB;
        Collider *b = origA <= origB ? origB : origA;
        if (!table.empty())
        {
            const size_t mask = table.size() - 1;
            size_t idx = hashPair(a, b) & mask;
            while (table[idx].pair != nullptr)
            {
                if (table[idx].a == a && table[idx].b == b)
                {
                    return table[idx].pair;
                }
                idx = (idx + 1) & mask;
            }
        }

        if (table.empty() || (count + 1) * 10 >= table.size() * 7)
        {
            rehash(table.empty() ? 64 : table.size() * 2);
        }

        const size_t mask = table.size() - 1;
        size_t idx = hashPair(a, b) & mask;
        while (table[idx].pair != nullptr)
        {
            idx = (idx + 1) & mask;
        }

        RawSlot *slot = allocateSlot();
        ColliderPair *pair = std::construct_at(reinterpret_cast<ColliderPair *>(slot), origA, origB);
        table[idx] = Entry{a, b, pair};
        ++count;
        return pair;
    }

    bool erase(Collider *a, Collider *b) noexcept
    {
        if (count == 0)
        {
            return false;
        }
        if (a > b)
        {
            std::swap(a, b);
        }
        const size_t mask = table.size() - 1;
        size_t hole = hashPair(a, b) & mask;
        while (table[hole].pair != nullptr)
        {
            if (table[hole].a == a && table[hole].b == b)
            {
                break;
            }
            hole = (hole + 1) & mask;
        }
        if (table[hole].pair == nullptr)
        {
            return false;
        }

        ColliderPair *pair = table[hole].pair;
        --count;

        size_t curr = (hole + 1) & mask;
        while (table[curr].pair != nullptr)
        {
            const size_t ideal = hashPair(table[curr].a, table[curr].b) & mask;
            if (((curr - ideal) & mask) >= ((curr - hole) & mask))
            {
                table[hole] = table[curr];
                hole = curr;
            }
            curr = (curr + 1) & mask;
        }
        table[hole] = Entry{};

        std::destroy_at(pair);
        freeSlots.push_back(reinterpret_cast<RawSlot *>(pair));
        return true;
    }

    template <typename Fn>
    void forEach(Fn &&fn) const
    {
        if (count == 0)
        {
            return;
        }
        for (const Entry &entry : table)
        {
            if (entry.pair != nullptr)
            {
                fn(*entry.pair);
            }
        }
    }

    void clear() noexcept
    {
        if (count > 0)
        {
            for (const Entry &entry : table)
            {
                if (entry.pair != nullptr)
                {
                    std::destroy_at(entry.pair);
                }
            }
            count = 0;
        }
        table.clear();
        freeSlots.clear();
        blocks.clear();
    }

  private:
    struct alignas(ColliderPair) RawSlot
    {
        std::byte data[sizeof(ColliderPair)];
    };

    struct Block
    {
        std::unique_ptr<RawSlot[]> slots;
        size_t capacity = 0;
        size_t used = 0;
    };

    struct Entry
    {
        Collider *a = nullptr;
        Collider *b = nullptr;
        ColliderPair *pair = nullptr;
    };

    static size_t hashPair(const Collider *a, const Collider *b) noexcept
    {
        const uintptr_t p1 = reinterpret_cast<uintptr_t>(a);
        const uintptr_t p2 = reinterpret_cast<uintptr_t>(b);
        uint64_t x = static_cast<uint64_t>(p1 >> 3) * 0x9e3779b97f4a7c15ULL;
        x ^= static_cast<uint64_t>(p2 >> 3) + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return static_cast<size_t>(x);
    }

    RawSlot *allocateSlot()
    {
        if (!freeSlots.empty())
        {
            RawSlot *slot = freeSlots.back();
            freeSlots.pop_back();
            return slot;
        }
        if (blocks.empty() || blocks.back().used == blocks.back().capacity)
        {
            const size_t prevCap = blocks.empty() ? 0 : blocks.back().capacity;
            const size_t nextCap = prevCap == 0 ? 64 : std::min<size_t>(prevCap * 2, 1024);
            Block block;
            block.slots = std::unique_ptr<RawSlot[]>(new RawSlot[nextCap]);
            block.capacity = nextCap;
            block.used = 0;
            blocks.push_back(std::move(block));
        }
        Block &block = blocks.back();
        return &block.slots[block.used++];
    }

    void rehash(size_t newCapacity)
    {
        std::vector<Entry> next(newCapacity);
        const size_t mask = newCapacity - 1;
        for (const Entry &entry : table)
        {
            if (entry.pair == nullptr)
            {
                continue;
            }
            size_t idx = hashPair(entry.a, entry.b) & mask;
            while (next[idx].pair != nullptr)
            {
                idx = (idx + 1) & mask;
            }
            next[idx] = entry;
        }
        table.swap(next);
    }

    std::vector<Block> blocks;
    std::vector<RawSlot *> freeSlots;
    std::vector<Entry> table;
    size_t count = 0;
};
