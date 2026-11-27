#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <vector>

#include "core/flathash.h"
#include "core/flatmap.h"
#include "core/slotpool.h"
#include "physics/broadphase/gridcell.h"
#include "physics/colliderpair.h"

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
        using iterator_category = std::forward_iterator_tag;
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
        using iterator_category = std::forward_iterator_tag;
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
        Node *node = table.find(key);
        return node != nullptr ? &node->second : nullptr;
    }

    const GridCell *find(const GridCellKey &key) const noexcept
    {
        const Node *node = table.find(key);
        return node != nullptr ? &node->second : nullptr;
    }

    GridCell &getOrCreate(const GridCellKey &key, Grid *owner)
    {
        Node *node = table.findOrEmplace(key, [&]() {
            Node *created = nullptr;
            if (!recycledNodes.empty())
            {
                created = recycledNodes.back();
                recycledNodes.pop_back();
                created->first = key;
                created->second.resetKey(key);
            }
            else
            {
                created = nodePool.create(key, owner);
            }

            created->activeIndex = activeNodes.size();
            try
            {
                activeNodes.push_back(created);
            }
            catch (...)
            {
                created->activeIndex = SIZE_MAX;
                nodePool.destroy(created);
                throw;
            }
            return created;
        });
        return node->second;
    }

    bool eraseAndMaybeRecycle(const GridCellKey &key, size_t maxRecycled) noexcept
    {
        Node *node = table.extract(key);
        if (node == nullptr)
        {
            return false;
        }

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
            try
            {
                recycledNodes.push_back(node);
                return true;
            }
            catch (...)
            {
                // Fall back to releasing the node to the slot pool if recycling storage cannot grow.
            }
        }

        nodePool.destroy(node);
        return true;
    }

    void clear() noexcept
    {
        for (Node *node : activeNodes)
        {
            nodePool.destroy(node);
        }
        for (Node *node : recycledNodes)
        {
            nodePool.destroy(node);
        }
        activeNodes.clear();
        recycledNodes.clear();
        table.clearAndRelease();
        nodePool.clear();
    }

  private:
    platformator_detail::ChunkedSlotPool<Node, 16, 256> nodePool;
    std::vector<Node *> recycledNodes;
    std::vector<Node *> activeNodes;
    FlatHashMap<GridCellKey, Node *, GridCellKey::Hash, std::equal_to<GridCellKey>, 32> table;
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
        return table.size();
    }

    bool empty() const noexcept
    {
        return table.empty();
    }

    const ColliderPair *find(Collider *a, Collider *b) const noexcept
    {
        return table.find(canonicalKey(a, b));
    }

    const ColliderPair *findOrEmplace(Collider *origA, Collider *origB)
    {
        return table.findOrEmplace(canonicalKey(origA, origB), [&]() { return pairPool.create(origA, origB); });
    }

    bool erase(Collider *a, Collider *b) noexcept
    {
        ColliderPair *pair = table.extract(canonicalKey(a, b));
        if (pair == nullptr)
        {
            return false;
        }
        pairPool.destroy(pair);
        return true;
    }

    template <typename Fn>
    void forEach(Fn &&fn) const
    {
        table.forEach([&](const PairKey &, ColliderPair *pair) { fn(*pair); });
    }

    void clear() noexcept
    {
        table.forEach([this](const PairKey &, ColliderPair *pair) { pairPool.destroy(pair); });
        table.clearAndRelease();
        pairPool.clear();
    }

  private:
    struct PairKey
    {
        Collider *a = nullptr;
        Collider *b = nullptr;

        bool operator==(const PairKey &) const noexcept = default;
    };

    struct PairKeyHash
    {
        size_t operator()(const PairKey &key) const noexcept
        {
            return platformator_detail::hashPointerPair(key.a, key.b);
        }
    };

    static PairKey canonicalKey(Collider *a, Collider *b) noexcept
    {
        return a <= b ? PairKey{a, b} : PairKey{b, a};
    }

    platformator_detail::ChunkedSlotPool<ColliderPair, 64, 1024> pairPool;
    FlatHashMap<PairKey, ColliderPair *, PairKeyHash, std::equal_to<PairKey>, 64> table;
};
