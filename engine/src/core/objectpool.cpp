#include "core/objectpool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>

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
constexpr size_t SLOT_ALIGNMENT = std::max<size_t>(alignof(std::max_align_t), __STDCPP_DEFAULT_NEW_ALIGNMENT__);
constexpr size_t INITIAL_SLAB_SLOTS = 64;
constexpr size_t MAX_SLAB_SLOTS = 256;

static_assert(alignof(GameObject) <= SLOT_ALIGNMENT);
static_assert(alignof(BoxCollider) <= SLOT_ALIGNMENT);
static_assert(alignof(CircleCollider) <= SLOT_ALIGNMENT);
static_assert(alignof(Rigidbody) <= SLOT_ALIGNMENT);
static_assert(alignof(Sprite) <= SLOT_ALIGNMENT);

struct FreeNode
{
    FreeNode *next;
};

struct alignas(SLOT_ALIGNMENT) Slab
{
    FreeNode *freeHead = nullptr;
    size_t liveSlots = 0;
    std::uintptr_t endAddress = 0;
    bool reclaimWhenEmpty = false;

    // Only slabs with free slots are on this list.
    Slab *next = nullptr;
    Slab **previousLink = nullptr;
};

struct SlabDeleter
{
    void operator()(Slab *slab) const noexcept
    {
        std::destroy_at(slab);
        ::operator delete(slab, std::align_val_t{SLOT_ALIGNMENT});
    }
};

using SlabMap = std::map<std::uintptr_t, std::unique_ptr<Slab, SlabDeleter>>;

class SlabPool
{
  public:
    explicit SlabPool(size_t objectSize) noexcept
        : objectSize(objectSize),
          slotSize((std::max(objectSize, sizeof(FreeNode)) + SLOT_ALIGNMENT - 1) & ~(SLOT_ALIGNMENT - 1))
    {
    }

    void *allocate(size_t size)
    {
        if (size != objectSize)
            return ::operator new(size);

        // Declare the node first so any unused backing storage is freed after unlocking.
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
                lock.lock();
                if (!available)
                    throw;
                return takeSlot();
            }
            lock.lock();
            if (!available)
            {
                Slab *slab = candidate.mapped().get();
                slab->reclaimWhenEmpty = exiting;
                slabs.insert(std::move(candidate));
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
            retired = slabs.extract(firstAddress(slab));
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
            Slab *slab = it->second.get();
            if (slab->liveSlots == 0)
            {
                unlink(slab);
                retired.insert(retired.end(), slabs.extract(it++));
            }
            else
            {
                slab->reclaimWhenEmpty = true;
                ++it;
            }
        }
        nextSlabSlots = INITIAL_SLAB_SLOTS;
    }

  private:
    static std::uintptr_t firstAddress(const Slab *slab) noexcept
    {
        return reinterpret_cast<std::uintptr_t>(slab + 1);
    }

    Slab *findSlab(std::uintptr_t address) noexcept
    {
        // Consecutive deletes commonly come from the same slab.
        Slab *slab = lastFreed;
        if (!slab || address < firstAddress(slab) || address >= slab->endAddress)
        {
            auto next = slabs.upper_bound(address);
            if (next == slabs.begin())
                return nullptr;
            slab = std::prev(next)->second.get();
            if (address >= slab->endAddress)
                return nullptr;
            lastFreed = slab;
        }
        assert((address - firstAddress(slab)) % slotSize == 0);
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
        auto *storage = static_cast<std::byte *>(
            ::operator new(sizeof(Slab) + count * slotSize, std::align_val_t{SLOT_ALIGNMENT}));
        std::unique_ptr<Slab, SlabDeleter> slab(std::construct_at(reinterpret_cast<Slab *>(storage)));
        storage += sizeof(Slab);
        slab->endAddress = reinterpret_cast<std::uintptr_t>(storage + count * slotSize);
        for (size_t i = count; i > 0; --i)
        {
            auto *slot = reinterpret_cast<FreeNode *>(storage + (i - 1) * slotSize);
            slab->freeHead = std::construct_at(slot, FreeNode{slab->freeHead});
        }

        // Prepare both allocations before acquiring the pool lock.
        SlabMap pending;
        const auto address = firstAddress(slab.get());
        return pending.extract(pending.emplace(address, std::move(slab)).first);
    }

    void link(Slab *slab) noexcept
    {
        slab->next = available;
        slab->previousLink = &available;
        if (available)
            available->previousLink = &slab->next;
        available = slab;
    }

    void unlink(Slab *slab) noexcept
    {
        *slab->previousLink = slab->next;
        if (slab->next)
            slab->next->previousLink = slab->previousLink;
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
    const size_t index = static_cast<size_t>(kind);
    assert(index < static_cast<size_t>(PooledObjectKind::Count));
    return getPools()[index].allocate(size);
}

void deallocatePooledObject(PooledObjectKind kind, void *ptr, size_t size) noexcept
{
    if (!ptr)
        return;

    const size_t index = static_cast<size_t>(kind);
    assert(index < static_cast<size_t>(PooledObjectKind::Count));
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
