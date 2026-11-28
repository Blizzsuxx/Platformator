#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace platformator_detail
{
// Reusable path-compressing, union-by-rank disjoint-set forest (retains capacity across frames).
class DisjointSet
{
  public:
    void reset(size_t count)
    {
        parent.resize(count);
        rank.assign(count, 0);
        for (size_t i = 0; i < count; ++i)
        {
            parent[i] = static_cast<uint32_t>(i);
        }
    }

    uint32_t find(uint32_t x) noexcept
    {
        uint32_t root = x;
        while (parent[root] != root)
        {
            root = parent[root];
        }
        while (parent[x] != x)
        {
            const uint32_t next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    }

    bool unite(uint32_t a, uint32_t b) noexcept
    {
        uint32_t rootA = find(a);
        uint32_t rootB = find(b);
        if (rootA == rootB)
        {
            return false;
        }
        if (rank[rootA] < rank[rootB])
        {
            std::swap(rootA, rootB);
        }
        parent[rootB] = rootA;
        if (rank[rootA] == rank[rootB])
        {
            ++rank[rootA];
        }
        return true;
    }

    void clear() noexcept
    {
        parent.clear();
        rank.clear();
    }

  private:
    std::vector<uint32_t> parent;
    std::vector<uint8_t> rank;
};
} // namespace platformator_detail
