#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace platformator_detail
{
// Pointer-stable block-allocated object pool with an O(1) noexcept intrusive free list.
// Callers own object lifetimes via create()/destroy(); all live objects must be destroyed before clear() or destruction.
template <typename T, size_t InitialBlockCapacity = 16, size_t MaxBlockCapacity = 256>
class ChunkedSlotPool
{
    static_assert(InitialBlockCapacity > 0, "InitialBlockCapacity must be positive");
    static_assert(MaxBlockCapacity >= InitialBlockCapacity, "MaxBlockCapacity must be >= InitialBlockCapacity");

  public:
    ChunkedSlotPool() = default;

    ~ChunkedSlotPool()
    {
        assert(liveCount == 0 && "All objects allocated from ChunkedSlotPool must be destroyed before pool destruction");
    }

    ChunkedSlotPool(const ChunkedSlotPool &) = delete;
    ChunkedSlotPool &operator=(const ChunkedSlotPool &) = delete;

    ChunkedSlotPool(ChunkedSlotPool &&other) noexcept
    {
        swap(other);
    }

    ChunkedSlotPool &operator=(ChunkedSlotPool &&other) noexcept
    {
        ChunkedSlotPool moved(std::move(other));
        swap(moved);
        return *this;
    }

    template <typename... Args>
    T *create(Args &&...args)
    {
        Slot *slot = acquireSlot();
        if constexpr (std::is_nothrow_constructible_v<T, Args...>)
        {
            T *object = std::construct_at(reinterpret_cast<T *>(slot->storage), std::forward<Args>(args)...);
#ifndef NDEBUG
            ++liveCount;
#endif
            return object;
        }
        else
        {
            struct SlotRollback
            {
                ChunkedSlotPool *pool;
                Slot *slot;
                ~SlotRollback()
                {
                    if (slot != nullptr)
                    {
                        pool->releaseSlot(slot);
                    }
                }
            } rollback{this, slot};

            T *object = std::construct_at(reinterpret_cast<T *>(slot->storage), std::forward<Args>(args)...);
            rollback.slot = nullptr;
#ifndef NDEBUG
            ++liveCount;
#endif
            return object;
        }
    }

    void destroy(T *object) noexcept
    {
        assert(object != nullptr);
#ifndef NDEBUG
        assert(liveCount > 0);
        --liveCount;
#endif
        std::destroy_at(object);
        releaseSlot(std::launder(reinterpret_cast<Slot *>(object)));
    }

    void clear() noexcept
    {
        assert(liveCount == 0 && "All objects allocated from ChunkedSlotPool must be destroyed before clear()");
        freeHead = nullptr;
        tailCursor = nullptr;
        tailEnd = nullptr;
        nextBlockCapacity = InitialBlockCapacity;
        blocks.clear();
    }

    void swap(ChunkedSlotPool &other) noexcept
    {
        blocks.swap(other.blocks);
        std::swap(freeHead, other.freeHead);
        std::swap(tailCursor, other.tailCursor);
        std::swap(tailEnd, other.tailEnd);
        std::swap(nextBlockCapacity, other.nextBlockCapacity);
#ifndef NDEBUG
        std::swap(liveCount, other.liveCount);
#endif
    }

  private:
    union Slot
    {
        alignas(T) std::byte storage[sizeof(T)];
        Slot *nextFree;
    };

    static_assert(std::is_standard_layout_v<Slot> && std::is_trivially_destructible_v<Slot>);

    Slot *acquireSlot()
    {
        if (freeHead != nullptr)
        {
            Slot *slot = freeHead;
            freeHead = slot->nextFree;
            return slot;
        }
        if (tailCursor != tailEnd)
        {
            return tailCursor++;
        }
        return allocateFromNewBlock();
    }

    void releaseSlot(Slot *slot) noexcept
    {
        slot->nextFree = freeHead;
        freeHead = slot;
    }

    Slot *allocateFromNewBlock()
    {
        const size_t capacity = nextBlockCapacity;
        auto block = std::make_unique_for_overwrite<Slot[]>(capacity);
        Slot *raw = block.get();
        blocks.push_back(std::move(block));
        nextBlockCapacity =
            (capacity >= MaxBlockCapacity || capacity > MaxBlockCapacity / 2) ? MaxBlockCapacity : capacity * 2;
        tailCursor = raw + 1;
        tailEnd = raw + capacity;
        return raw;
    }

    std::vector<std::unique_ptr<Slot[]>> blocks;
    Slot *freeHead = nullptr;
    Slot *tailCursor = nullptr;
    Slot *tailEnd = nullptr;
    size_t nextBlockCapacity = InitialBlockCapacity;
#ifndef NDEBUG
    size_t liveCount = 0;
#endif
};
} // namespace platformator_detail
