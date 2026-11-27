#include "core/objectpool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

#include <oneapi/tbb/spin_mutex.h>

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
constexpr size_t SLOT_ALIGNMENT = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
constexpr size_t INITIAL_SLAB_SLOTS = 64;
constexpr size_t MAX_SLAB_SLOTS = 256;

static_assert((SLOT_ALIGNMENT & (SLOT_ALIGNMENT - 1)) == 0);
static_assert(alignof(std::max_align_t) <= SLOT_ALIGNMENT);
static_assert(alignof(GameObject) <= SLOT_ALIGNMENT);
static_assert(alignof(BoxCollider) <= SLOT_ALIGNMENT);
static_assert(alignof(CircleCollider) <= SLOT_ALIGNMENT);
static_assert(alignof(Rigidbody) <= SLOT_ALIGNMENT);
static_assert(alignof(Sprite) <= SLOT_ALIGNMENT);

constexpr size_t alignUp(size_t size, size_t alignment) noexcept
{
    return (size + alignment - 1) & ~(alignment - 1);
}

struct FreeNode
{
    FreeNode *next;
};

struct Slab
{
    std::unique_ptr<std::byte[]> storage;
    size_t byteSize = 0;
    FreeNode *freeHead = nullptr;
    size_t liveSlots = 0;
    bool reclaimWhenEmpty = false;

    // Only slabs with free slots are on the intrusive available list.
    Slab *next = nullptr;
    Slab **previousLink = nullptr;

    Slab(size_t slotCount, size_t slotSize)
        : storage(std::make_unique_for_overwrite<std::byte[]>(slotCount * slotSize)),
          byteSize(slotCount * slotSize)
    {
        assert(firstAddress() % SLOT_ALIGNMENT == 0);
        for (size_t i = slotCount; i > 0; --i)
        {
            auto *slot = reinterpret_cast<FreeNode *>(storage.get() + (i - 1) * slotSize);
            freeHead = std::construct_at(slot, FreeNode{freeHead});
        }
    }

    std::uintptr_t firstAddress() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(storage.get());
    }

    std::uintptr_t endAddress() const noexcept
    {
        return firstAddress() + byteSize;
    }

    bool contains(std::uintptr_t address) const noexcept
    {
        return address >= firstAddress() && address < endAddress();
    }
};

using SlabMap = std::map<std::uintptr_t, Slab>;

class SlabPool
{
  public:
    explicit SlabPool(size_t objectSize) noexcept
        : objectSize(objectSize),
          slotSize(alignUp(std::max(objectSize, sizeof(FreeNode)), SLOT_ALIGNMENT))
    {
    }

    void *allocate(size_t size)
    {
        if (size != objectSize)
            return ::operator new(size);

        // Declare before the lock so any unused slab is freed after unlocking.
        SlabMap::node_type candidate;
        std::unique_lock lock(mutex);
        if (!available)
        {
            const size_t count = nextSlabSlots;
            lock.unlock();
            try
            {
                candidate = makeSlab(count);
            }
            catch (const std::bad_alloc &)
            {
                // Another thread may have freed a slot or published a slab while unlocked.
                lock.lock();
                if (!available)
                    throw;
                return takeSlot();
            }
            lock.lock();
            if (!available)
            {
                const auto result = slabs.insert(std::move(candidate));
                assert(result.inserted);
                Slab *slab = &result.position->second;
                slab->reclaimWhenEmpty = exiting;
                link(slab);
                nextSlabSlots = std::min(count * 2, MAX_SLAB_SLOTS);
            }
        }
        return takeSlot();
    }

    bool deallocate(void *ptr, size_t size) noexcept
    {
        if (size != 0 && size != objectSize)
            return false;

        SlabMap::node_type retired;
        std::lock_guard lock(mutex);
        Slab *slab = findSlab(reinterpret_cast<std::uintptr_t>(ptr));
        if (!slab)
            return false;

        const bool wasFull = slab->freeHead == nullptr;
        slab->freeHead = std::construct_at(static_cast<FreeNode *>(ptr), FreeNode{slab->freeHead});
        if (wasFull)
            link(slab);

        if (--slab->liveSlots == 0 && slab->reclaimWhenEmpty)
        {
            unlink(slab);
            lastFreed = nullptr;
            retired = slabs.extract(slab->firstAddress());
        }
        return true;
    }

    void trim(bool atExit = false) noexcept
    {
        SlabMap retired;
        std::lock_guard lock(mutex);
        exiting = atExit || exiting;
        lastFreed = nullptr;
        for (auto it = slabs.begin(); it != slabs.end();)
        {
            Slab &slab = it->second;
            if (slab.liveSlots == 0)
            {
                unlink(&slab);
                retired.insert(retired.end(), slabs.extract(it++));
            }
            else
            {
                slab.reclaimWhenEmpty = true;
                ++it;
            }
        }
        nextSlabSlots = INITIAL_SLAB_SLOTS;
    }

  private:
    Slab *findSlab(std::uintptr_t address) noexcept
    {
        // Consecutive deletes commonly come from the same slab.
        Slab *slab = lastFreed;
        if (!slab || !slab->contains(address))
        {
            const auto next = slabs.upper_bound(address);
            if (next == slabs.begin())
                return nullptr;
            slab = &std::prev(next)->second;
            if (!slab->contains(address))
                return nullptr;
            lastFreed = slab;
        }
        assert((address - slab->firstAddress()) % slotSize == 0);
        return slab;
    }

    FreeNode *takeSlot() noexcept
    {
        Slab *slab = available;
        FreeNode *slot = slab->freeHead;
        slab->freeHead = slot->next;
        ++slab->liveSlots;
        if (!slab->freeHead)
            unlink(slab);
        return slot;
    }

    SlabMap::node_type makeSlab(size_t count) const
    {
        Slab slab(count, slotSize);
        const auto address = slab.firstAddress();

        // Prepare both allocations before acquiring the pool lock.
        SlabMap pending;
        return pending.extract(pending.emplace(address, std::move(slab)).first);
    }

    void link(Slab *slab) noexcept
    {
        assert(slab->previousLink == nullptr);
        slab->next = available;
        slab->previousLink = &available;
        if (available)
            available->previousLink = &slab->next;
        available = slab;
    }

    void unlink(Slab *slab) noexcept
    {
        assert(slab->previousLink != nullptr);
        *slab->previousLink = slab->next;
        if (slab->next)
            slab->next->previousLink = slab->previousLink;
        slab->next = nullptr;
        slab->previousLink = nullptr;
    }

    const size_t objectSize;
    const size_t slotSize;
    tbb::spin_mutex mutex;
    SlabMap slabs;
    Slab *available = nullptr;
    Slab *lastFreed = nullptr;
    size_t nextSlabSlots = INITIAL_SLAB_SLOTS;
    bool exiting = false;
};

using Pools = std::array<SlabPool, static_cast<size_t>(PooledObjectKind::Count)>;
constinit std::atomic<Pools *> publishedPools{nullptr};

size_t poolIndex(PooledObjectKind kind) noexcept
{
    const size_t index = static_cast<size_t>(kind);
    assert(index < static_cast<size_t>(PooledObjectKind::Count));
    return index;
}

Pools &getPools()
{
    // The registry outlives static objects that may still return a slot after exit trimming.
    static Pools *const pools = [] {
        std::unique_ptr<Pools> instance(new Pools{
            SlabPool(sizeof(GameObject)),
            SlabPool(sizeof(BoxCollider)),
            SlabPool(sizeof(CircleCollider)),
            SlabPool(sizeof(Rigidbody)),
            SlabPool(sizeof(Sprite)),
        });
        if (std::atexit([] {
                if (auto *pools = publishedPools.load(std::memory_order_acquire))
                {
                    for (auto &pool : *pools)
                        pool.trim(true);
                }
            }) != 0)
            throw std::bad_alloc();
        Pools *ready = instance.release();
        publishedPools.store(ready, std::memory_order_release);
        return ready;
    }();
    return *pools;
}
} // namespace

void *allocatePooledObject(PooledObjectKind kind, size_t size)
{
    return getPools()[poolIndex(kind)].allocate(size);
}

void deallocatePooledObject(PooledObjectKind kind, void *ptr, size_t size) noexcept
{
    if (!ptr)
        return;

    const size_t index = poolIndex(kind);
    auto *pools = publishedPools.load(std::memory_order_acquire);
    if (!pools || !(*pools)[index].deallocate(ptr, size))
        ::operator delete(ptr);
}

void releaseEmptyObjectPools() noexcept
{
    if (auto *pools = publishedPools.load(std::memory_order_acquire))
    {
        for (auto &pool : *pools)
            pool.trim();
    }
}
#endif

void *allocatePooledObjectNothrow(PooledObjectKind kind, size_t size) noexcept
{
    try
    {
        return allocatePooledObject(kind, size);
    }
    catch (...)
    {
        return nullptr;
    }
}
} // namespace platformator_detail
