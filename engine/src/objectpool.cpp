#include "objectpool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
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

void deallocatePooledObject(void *ptr) noexcept
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

class SlabPool;

struct alignas(SLOT_ALIGNMENT) Slab
{
    SlabPool *pool;
    FreeNode *freeHead = nullptr;
    size_t liveSlots = 0;

    // Only slabs with free slots are on this list.
    Slab *next = nullptr;
    Slab **previousLink = nullptr;
};

struct SlotHeader
{
    Slab *slab;
};

constexpr size_t alignSlotSize(size_t size) noexcept
{
    return (size + SLOT_ALIGNMENT - 1) & ~(SLOT_ALIGNMENT - 1);
}

constexpr size_t OBJECT_OFFSET = alignSlotSize(sizeof(SlotHeader));
constexpr size_t SLAB_OBJECT_OFFSET = alignSlotSize(sizeof(Slab) + sizeof(SlotHeader));

SlotHeader *slotHeader(void *ptr) noexcept
{
    return reinterpret_cast<SlotHeader *>(static_cast<std::byte *>(ptr) - sizeof(SlotHeader));
}

class SlabPool
{
  public:
    explicit SlabPool(size_t objectSize) noexcept
        : objectSize(objectSize), slotSize(alignSlotSize(sizeof(SlotHeader) + std::max(objectSize, sizeof(FreeNode))))
    {
    }

    void *allocate(size_t size)
    {
        if (size != objectSize)
        {
            // A derived type can inherit this allocator. Its allocation still needs a header
            // so the corresponding nothrow placement delete can free it without a size.
            if (size > SIZE_MAX - OBJECT_OFFSET)
                throw std::bad_alloc();
            auto *storage = static_cast<std::byte *>(
                ::operator new(OBJECT_OFFSET + size, std::align_val_t{SLOT_ALIGNMENT}));
            void *ptr = storage + OBJECT_OFFSET;
            std::construct_at(slotHeader(ptr), SlotHeader{nullptr});
            return ptr;
        }

        std::lock_guard lock(mutex);
        if (available == nullptr)
            grow();

        Slab *slab = available;
        FreeNode *slot = slab->freeHead;
        slab->freeHead = slot->next;
        ++slab->liveSlots;
        if (slab->freeHead == nullptr)
            unlink(slab);
        return slot;
    }

    void deallocate(Slab *slab, void *ptr) noexcept
    {
        std::lock_guard lock(mutex);
        const bool wasFull = slab->freeHead == nullptr;
        slab->freeHead = std::construct_at(static_cast<FreeNode *>(ptr), FreeNode{slab->freeHead});
        if (wasFull)
            link(slab);

        --slab->liveSlots;
        if (exiting && slab->liveSlots == 0)
            release(slab);
    }

    void trim(bool atExit = false) noexcept
    {
        std::lock_guard lock(mutex);
        exiting = atExit || exiting;
        Slab *slab = available;
        while (slab != nullptr)
        {
            Slab *next = slab->next;
            if (slab->liveSlots == 0)
                release(slab);
            slab = next;
        }
        nextSlabSlots = INITIAL_SLAB_SLOTS;
    }

  private:
    void grow()
    {
        const size_t count = nextSlabSlots;
        auto *storage = static_cast<std::byte *>(
            ::operator new(SLAB_OBJECT_OFFSET - sizeof(SlotHeader) + count * slotSize,
                           std::align_val_t{SLOT_ALIGNMENT}));
        auto *slab = std::construct_at(reinterpret_cast<Slab *>(storage), Slab{this});
        storage += SLAB_OBJECT_OFFSET;
        for (size_t i = count; i > 0; --i)
        {
            auto *slot = reinterpret_cast<FreeNode *>(storage + (i - 1) * slotSize);
            std::construct_at(slotHeader(slot), SlotHeader{slab});
            slab->freeHead = std::construct_at(slot, FreeNode{slab->freeHead});
        }
        link(slab);
        nextSlabSlots = std::min(count * 2, MAX_SLAB_SLOTS);
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

    void release(Slab *slab) noexcept
    {
        unlink(slab);
        std::destroy_at(slab);
        ::operator delete(slab, std::align_val_t{SLOT_ALIGNMENT});
    }

    const size_t objectSize;
    const size_t slotSize;
    tbb::spin_mutex mutex;
    Slab *available = nullptr;
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

void deallocatePooledObject(void *ptr) noexcept
{
    if (ptr == nullptr)
        return;

    if (Slab *slab = slotHeader(ptr)->slab)
        slab->pool->deallocate(slab, ptr);
    else
        ::operator delete(static_cast<std::byte *>(ptr) - OBJECT_OFFSET, std::align_val_t{SLOT_ALIGNMENT});
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
