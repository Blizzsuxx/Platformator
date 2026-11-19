#include "objectpool.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "platformator/gameobject.h"
#include "platformator/rigidbody.h"
#include "platformator/sprite.h"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define PLATFORMATOR_POOL_SANITIZER_ACTIVE 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define PLATFORMATOR_POOL_SANITIZER_ACTIVE 1
#else
#define PLATFORMATOR_POOL_SANITIZER_ACTIVE 0
#endif
#else
#define PLATFORMATOR_POOL_SANITIZER_ACTIVE 0
#endif

namespace platformator_detail
{
#if PLATFORMATOR_POOL_SANITIZER_ACTIVE

void *allocatePooledObject(PooledObjectKind, size_t size)
{
    return ::operator new(size);
}

void deallocatePooledObject(PooledObjectKind, void *ptr, size_t) noexcept
{
    ::operator delete(ptr);
}

void releaseEmptyObjectPools() noexcept
{
}

#else

namespace
{
static_assert(alignof(GameObject) <= alignof(std::max_align_t));
static_assert(alignof(BoxCollider) <= alignof(std::max_align_t));
static_assert(alignof(CircleCollider) <= alignof(std::max_align_t));
static_assert(alignof(Rigidbody) <= alignof(std::max_align_t));
static_assert(alignof(Sprite) <= alignof(std::max_align_t));

constexpr size_t POOL_COUNT = static_cast<size_t>(PooledObjectKind::Count);

struct FreeNode
{
    FreeNode *next;
};

constexpr size_t alignUp(size_t value, size_t alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

class CentralPool
{
  public:
    explicit CentralPool(size_t rawSize) noexcept
        : slotSize(alignUp(std::max(rawSize, sizeof(FreeNode)), alignof(std::max_align_t)))
    {
    }

    ~CentralPool()
    {
        std::lock_guard<std::mutex> lock(mutex);
        releaseAllSlabsLocked();
    }

    FreeNode *acquireOne()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (freeHead == nullptr)
        {
            growLocked();
        }
        FreeNode *node = freeHead;
        freeHead = node->next;
        --freeCount;
        return node;
    }

    size_t acquireBatch(size_t requested, FreeNode *&outHead)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (freeHead == nullptr)
        {
            growLocked();
        }
        FreeNode *first = freeHead;
        FreeNode *last = first;
        size_t taken = 1;
        while (taken < requested && last->next != nullptr)
        {
            last = last->next;
            ++taken;
        }
        freeHead = last->next;
        last->next = nullptr;
        freeCount -= taken;
        outHead = first;
        return taken;
    }

    void returnBatch(FreeNode *head, FreeNode *tail, size_t count) noexcept
    {
        if (head == nullptr || count == 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex);
        tail->next = freeHead;
        freeHead = head;
        freeCount += count;
    }

    void releaseIfEmpty() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (freeCount == totalSlots && !slabs.empty())
        {
            releaseAllSlabsLocked();
        }
    }

  private:
    void growLocked()
    {
        const size_t count = nextSlabSlots;
        nextSlabSlots = std::min<size_t>(nextSlabSlots * 2, MAX_SLAB_SLOTS);

        const size_t bytes = count * slotSize;
        auto *mem = static_cast<std::byte *>(::operator new(bytes, std::align_val_t{alignof(std::max_align_t)}));
        try
        {
            slabs.push_back(mem);
        }
        catch (...)
        {
            ::operator delete(mem, std::align_val_t{alignof(std::max_align_t)});
            throw;
        }

        // Link slots in reverse so popping yields ascending contiguous addresses.
        FreeNode *head = freeHead;
        for (size_t i = count; i > 0; --i)
        {
            auto *node = reinterpret_cast<FreeNode *>(mem + (i - 1) * slotSize);
            node->next = head;
            head = node;
        }
        freeHead = head;
        freeCount += count;
        totalSlots += count;
    }

    void releaseAllSlabsLocked() noexcept
    {
        for (std::byte *slab : slabs)
        {
            ::operator delete(slab, std::align_val_t{alignof(std::max_align_t)});
        }
        slabs.clear();
        freeHead = nullptr;
        freeCount = 0;
        totalSlots = 0;
        nextSlabSlots = INITIAL_SLAB_SLOTS;
    }

    static constexpr size_t INITIAL_SLAB_SLOTS = 64;
    static constexpr size_t MAX_SLAB_SLOTS = 256;

    const size_t slotSize;
    std::mutex mutex;
    std::vector<std::byte *> slabs;
    FreeNode *freeHead = nullptr;
    size_t freeCount = 0;
    size_t totalSlots = 0;
    size_t nextSlabSlots = INITIAL_SLAB_SLOTS;
};

using CentralPoolArray = std::array<std::shared_ptr<CentralPool>, POOL_COUNT>;

CentralPoolArray &centralPools()
{
    static CentralPoolArray pools{
        std::make_shared<CentralPool>(sizeof(GameObject)),
        std::make_shared<CentralPool>(sizeof(BoxCollider)),
        std::make_shared<CentralPool>(sizeof(CircleCollider)),
        std::make_shared<CentralPool>(sizeof(Rigidbody)),
        std::make_shared<CentralPool>(sizeof(Sprite)),
    };
    return pools;
}

inline thread_local bool threadCacheDestroyed = false;

class ThreadPoolCache
{
  public:
    explicit ThreadPoolCache(const CentralPoolArray &pools) noexcept : centrals(pools)
    {
    }

    ~ThreadPoolCache()
    {
        flushAll();
        threadCacheDestroyed = true;
    }

    void *allocate(size_t idx)
    {
        SlotCache &lane = lanes[idx];
        if (lane.head == nullptr)
        {
            lane.count = centrals[idx]->acquireBatch(REFILL_BATCH, lane.head);
        }
        FreeNode *node = lane.head;
        lane.head = node->next;
        --lane.count;
        return node;
    }

    void deallocate(size_t idx, void *ptr) noexcept
    {
        SlotCache &lane = lanes[idx];
        auto *node = static_cast<FreeNode *>(ptr);
        node->next = lane.head;
        lane.head = node;
        ++lane.count;

        if (lane.count >= MAX_LOCAL_NODES)
        {
            FreeNode *head = lane.head;
            FreeNode *tail = head;
            size_t spilled = 1;
            while (spilled < SPILL_BATCH && tail->next != nullptr)
            {
                tail = tail->next;
                ++spilled;
            }
            lane.head = tail->next;
            tail->next = nullptr;
            lane.count -= spilled;
            centrals[idx]->returnBatch(head, tail, spilled);
        }
    }

    void flushAll() noexcept
    {
        for (size_t idx = 0; idx < POOL_COUNT; ++idx)
        {
            SlotCache &lane = lanes[idx];
            if (lane.head == nullptr || lane.count == 0)
            {
                continue;
            }
            FreeNode *head = lane.head;
            FreeNode *tail = head;
            size_t walked = 1;
            while (tail->next != nullptr)
            {
                tail = tail->next;
                ++walked;
            }
            assert(walked == lane.count);
            lane.head = nullptr;
            const size_t count = lane.count;
            lane.count = 0;
            centrals[idx]->returnBatch(head, tail, count);
        }
    }

  private:
    struct SlotCache
    {
        FreeNode *head = nullptr;
        size_t count = 0;
    };

    static constexpr size_t REFILL_BATCH = 64;
    static constexpr size_t MAX_LOCAL_NODES = 2048;
    static constexpr size_t SPILL_BATCH = 1024;

    CentralPoolArray centrals;
    std::array<SlotCache, POOL_COUNT> lanes{};
};

ThreadPoolCache *tryGetThreadCache()
{
    if (threadCacheDestroyed)
    {
        return nullptr;
    }
    thread_local ThreadPoolCache cache(centralPools());
    return &cache;
}
} // namespace

void *allocatePooledObject(PooledObjectKind kind, size_t)
{
    const size_t idx = static_cast<size_t>(kind);
    assert(idx < POOL_COUNT);
    if (ThreadPoolCache *cache = tryGetThreadCache())
    {
        return cache->allocate(idx);
    }
    return centralPools()[idx]->acquireOne();
}

void deallocatePooledObject(PooledObjectKind kind, void *ptr, size_t) noexcept
{
    if (ptr == nullptr)
    {
        return;
    }
    const size_t idx = static_cast<size_t>(kind);
    assert(idx < POOL_COUNT);
    if (ThreadPoolCache *cache = tryGetThreadCache())
    {
        cache->deallocate(idx, ptr);
        return;
    }
    auto *node = static_cast<FreeNode *>(ptr);
    node->next = nullptr;
    centralPools()[idx]->returnBatch(node, node, 1);
}

void releaseEmptyObjectPools() noexcept
{
    if (!threadCacheDestroyed)
    {
        if (ThreadPoolCache *cache = tryGetThreadCache())
        {
            cache->flushAll();
        }
    }
    for (auto &pool : centralPools())
    {
        pool->releaseIfEmpty();
    }
}

#endif
} // namespace platformator_detail
