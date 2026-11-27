#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <type_traits>
#include <vector>

#include "core/flatmap.h"
#include "core/membership.h"
#include "core/slotpool.h"
#include "physics/broadphase/gridcell.h"
#include "physics/broadphase/projectionproxy.h"
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

    template <bool IsConst> class Iterator
    {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::conditional_t<IsConst, const Node, Node>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type *;
        using reference = value_type &;

        explicit Iterator(Node *const *ptr = nullptr) noexcept : ptr(ptr)
        {
        }

        reference operator*() const noexcept
        {
            return **ptr;
        }
        pointer operator->() const noexcept
        {
            return *ptr;
        }
        Iterator &operator++() noexcept
        {
            ++ptr;
            return *this;
        }
        Iterator operator++(int) noexcept
        {
            Iterator copy = *this;
            ++ptr;
            return copy;
        }
        bool operator==(const Iterator &other) const noexcept
        {
            return ptr == other.ptr;
        }
        bool operator!=(const Iterator &other) const noexcept
        {
            return ptr != other.ptr;
        }

      private:
        Node *const *ptr;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

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

            try
            {
                activeNodes.push_back(created);
                created->activeIndex = activeNodes.size() - 1;
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

        membership(activeNodes, node, &Node::activeIndex, false);

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

    ColliderPair *find(Collider *a, Collider *b) const noexcept
    {
        return table.find(PairKey(a, b));
    }

    ColliderPair *findOrEmplace(Collider *origA, Collider *origB)
    {
        return table.findOrEmplace(PairKey(origA, origB), [&]() { return pairPool.create(origA, origB); });
    }

    bool erase(Collider *a, Collider *b) noexcept
    {
        ColliderPair *pair = table.extract(PairKey(a, b));
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
    using PairKey = platformator_detail::CanonicalColliderPairKey;

    platformator_detail::ChunkedSlotPool<ColliderPair, 64, 1024> pairPool;
    FlatHashMap<PairKey, ColliderPair *, PairKey::Hash, std::equal_to<PairKey>, 64> table;
};
