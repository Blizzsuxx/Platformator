#include "spatialresourcehooks.h"

#include <box2d/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <sys/resource.h>

namespace
{
using platformator_detail::SpatialResourceTag;
constexpr size_t DOMAIN_COUNT = static_cast<size_t>(SpatialResourceTag::Count);

struct DomainCounters
{
    std::atomic<uint64_t> allocationCount{0};
    std::atomic<uint64_t> freeCount{0};
    std::atomic<uint64_t> allocatedBytes{0};
    std::atomic<uint64_t> liveBytes{0};
    std::atomic<uint64_t> peakLiveBytes{0};
    std::atomic<uint64_t> liveAllocations{0};
};

struct alignas(std::max_align_t) AllocationHeader
{
    void *base;
    size_t size;
    uint8_t domain;
    bool tracked;
};

std::array<DomainCounters, DOMAIN_COUNT> counters;
std::array<uint64_t, DOMAIN_COUNT> initialLiveBytes{};
std::array<std::atomic<uint64_t>, DOMAIN_COUNT> currentOwnedCapacity{};
std::array<uint64_t, DOMAIN_COUNT> initialOwnedCapacity{};
std::array<std::atomic<uint64_t>, DOMAIN_COUNT> peakOwnedCapacity{};

const char *domainName(const size_t domain)
{
    static constexpr const char *names[] = {"unclassified", "physics_index", "physics_common_pairs",
                                            "visibility_index", "scratch", "box2d"};
    return domain < DOMAIN_COUNT ? names[domain] : "unknown";
}

void updatePeak(std::atomic<uint64_t> &peak, uint64_t value)
{
    uint64_t old = peak.load(std::memory_order_relaxed);
    while (old < value && !peak.compare_exchange_weak(old, value, std::memory_order_relaxed))
    {
    }
}

void *allocateBlock(size_t size, size_t alignment, bool forceBox2d = false)
{
    if (size == 0) size = 1;
    alignment = std::max(alignment, alignof(AllocationHeader));
    const size_t total = size + sizeof(AllocationHeader) + alignment - 1;
    void *base = std::malloc(total);
    if (!base) throw std::bad_alloc();
    const uintptr_t first = reinterpret_cast<uintptr_t>(base) + sizeof(AllocationHeader);
    const uintptr_t userAddress = (first + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    auto *header = reinterpret_cast<AllocationHeader *>(userAddress - sizeof(AllocationHeader));
    const bool tracked = platformator_detail::spatialResourceCaptureActive();
    const size_t domain = forceBox2d ? static_cast<size_t>(SpatialResourceTag::Box2D)
                                     : static_cast<size_t>(platformator_detail::spatialResourceCurrentTag());
    *header = {base, size, static_cast<uint8_t>(domain), tracked};
    if (tracked)
    {
        auto &counter = counters[domain];
        counter.allocationCount.fetch_add(1, std::memory_order_relaxed);
        counter.allocatedBytes.fetch_add(size, std::memory_order_relaxed);
        const uint64_t live = counter.liveBytes.fetch_add(size, std::memory_order_relaxed) + size;
        counter.liveAllocations.fetch_add(1, std::memory_order_relaxed);
        updatePeak(counter.peakLiveBytes, live);
    }
    return reinterpret_cast<void *>(userAddress);
}

void freeBlock(void *ptr)
{
    if (!ptr) return;
    auto *header = reinterpret_cast<AllocationHeader *>(reinterpret_cast<uintptr_t>(ptr) - sizeof(AllocationHeader));
    void *base = header->base;
    if (header->tracked)
    {
        auto &counter = counters[header->domain];
        counter.freeCount.fetch_add(1, std::memory_order_relaxed);
        counter.liveBytes.fetch_sub(header->size, std::memory_order_relaxed);
        counter.liveAllocations.fetch_sub(1, std::memory_order_relaxed);
    }
    std::free(base);
}

void *box2dAllocate(size_t size, int alignment)
{
    return allocateBlock(size, static_cast<size_t>(std::max(1, alignment)), true);
}

void box2dFree(void *memory, size_t)
{
    freeBlock(memory);
}

uint64_t processPeakRssBytes()
{
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
}
} // namespace

void *operator new(size_t size) { return allocateBlock(size, alignof(std::max_align_t)); }
void *operator new[](size_t size) { return allocateBlock(size, alignof(std::max_align_t)); }
void operator delete(void *ptr) noexcept { freeBlock(ptr); }
void operator delete[](void *ptr) noexcept { freeBlock(ptr); }
void operator delete(void *ptr, size_t) noexcept { freeBlock(ptr); }
void operator delete[](void *ptr, size_t) noexcept { freeBlock(ptr); }
void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    try { return allocateBlock(size, alignof(std::max_align_t)); } catch (...) { return nullptr; }
}
void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
    try { return allocateBlock(size, alignof(std::max_align_t)); } catch (...) { return nullptr; }
}
void operator delete(void *ptr, const std::nothrow_t &) noexcept { freeBlock(ptr); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { freeBlock(ptr); }
void *operator new(size_t size, std::align_val_t alignment)
{
    return allocateBlock(size, static_cast<size_t>(alignment));
}
void *operator new[](size_t size, std::align_val_t alignment)
{
    return allocateBlock(size, static_cast<size_t>(alignment));
}
void operator delete(void *ptr, std::align_val_t) noexcept { freeBlock(ptr); }
void operator delete[](void *ptr, std::align_val_t) noexcept { freeBlock(ptr); }
void operator delete(void *ptr, size_t, std::align_val_t) noexcept { freeBlock(ptr); }
void operator delete[](void *ptr, size_t, std::align_val_t) noexcept { freeBlock(ptr); }
void *operator new(size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    try { return allocateBlock(size, static_cast<size_t>(alignment)); } catch (...) { return nullptr; }
}
void *operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    try { return allocateBlock(size, static_cast<size_t>(alignment)); } catch (...) { return nullptr; }
}
void operator delete(void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { freeBlock(ptr); }
void operator delete[](void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { freeBlock(ptr); }

namespace platformator_detail
{
void spatialResourceStart()
{
    for (size_t i = 0; i < DOMAIN_COUNT; ++i)
    {
        auto &counter = counters[i];
        counter.allocationCount.store(0, std::memory_order_relaxed);
        counter.freeCount.store(0, std::memory_order_relaxed);
        counter.allocatedBytes.store(0, std::memory_order_relaxed);
        counter.peakLiveBytes.store(0, std::memory_order_relaxed);
        counter.liveAllocations.store(0, std::memory_order_relaxed);
        currentOwnedCapacity[i].store(0, std::memory_order_relaxed);
        peakOwnedCapacity[i].store(0, std::memory_order_relaxed);
    }
    initialLiveBytes.fill(0);
    initialOwnedCapacity.fill(0);
    setSpatialResourceCaptureActive(true);
    b2SetAllocator(box2dAllocate, box2dFree);
}

void spatialResourceMarkInitial()
{
    for (size_t i = 0; i < DOMAIN_COUNT; ++i)
    {
        initialLiveBytes[i] = counters[i].liveBytes.load(std::memory_order_relaxed);
        initialOwnedCapacity[i] = currentOwnedCapacity[i].load(std::memory_order_relaxed);
    }
}

void spatialResourceSetOwnedCapacity(SpatialResourceTag tag, uint64_t bytes)
{
    const size_t domain = static_cast<size_t>(tag);
    if (domain >= DOMAIN_COUNT) return;
    currentOwnedCapacity[domain].store(bytes, std::memory_order_relaxed);
    updatePeak(peakOwnedCapacity[domain], bytes);
}

nlohmann::json spatialResourceFinish()
{
    std::array<uint64_t, DOMAIN_COUNT> finalLive{}, peakLive{}, allocatedBytes{}, allocationCount{}, freeCount{}, liveAllocations{};
    std::array<uint64_t, DOMAIN_COUNT> initialCapacity{}, finalCapacity{}, peakCapacity{};
    for (size_t i = 0; i < DOMAIN_COUNT; ++i)
    {
        finalLive[i] = counters[i].liveBytes.load(std::memory_order_relaxed);
        peakLive[i] = counters[i].peakLiveBytes.load(std::memory_order_relaxed);
        allocatedBytes[i] = counters[i].allocatedBytes.load(std::memory_order_relaxed);
        allocationCount[i] = counters[i].allocationCount.load(std::memory_order_relaxed);
        freeCount[i] = counters[i].freeCount.load(std::memory_order_relaxed);
        liveAllocations[i] = counters[i].liveAllocations.load(std::memory_order_relaxed);
        initialCapacity[i] = initialOwnedCapacity[i];
        finalCapacity[i] = currentOwnedCapacity[i].load(std::memory_order_relaxed);
        peakCapacity[i] = peakOwnedCapacity[i].load(std::memory_order_relaxed);
    }
    const uint64_t rss = processPeakRssBytes();
    setSpatialResourceCaptureActive(false);
    nlohmann::json domains = nlohmann::json::object();
    for (size_t i = 0; i < DOMAIN_COUNT; ++i)
        domains[domainName(i)] = {{"initial_live_bytes", initialLiveBytes[i]}, {"final_live_bytes", finalLive[i]},
                                  {"peak_live_bytes", peakLive[i]}, {"allocated_bytes", allocatedBytes[i]},
                                  {"initial_owned_capacity_bytes", initialCapacity[i]},
                                  {"final_owned_capacity_bytes", finalCapacity[i]},
                                  {"peak_owned_capacity_bytes", peakCapacity[i]},
                                  {"allocation_count", allocationCount[i]}, {"free_count", freeCount[i]},
                                  {"live_allocation_count", liveAllocations[i]}};
    return {{"status", "complete"}, {"timing_eligible", false},
            {"method", "global C++ allocation hooks plus Box2D b2SetAllocator callbacks"},
            {"coverage", {{"cpp_new_delete_families", "scalar,array,sized,aligned,nothrow"},
                           {"box2d_allocator_callbacks", true},
                           {"owned_capacity_estimates", "tree API plus owner-recursive record/projection/cell/binding/chunk/checkpoint/query-hit/container capacity snapshots; hash nodes include two pointer-sized link/hash bookkeeping words per value (estimate)"},
                           {"tracked_domains", {"physics_index", "physics_common_pairs", "visibility_index",
                                                "scratch", "box2d", "unclassified"}}}},
            {"exclusions", {"C malloc/calloc/realloc not routed through C++ new", "SDL direct C allocations",
                            "oneTBB internal allocator calls not routed through C++ new",
                            "allocator metadata, implementation-private container segment/control tables beyond declared node estimates, and global allocations before capture start"}},
            {"allocation_header_bytes", sizeof(AllocationHeader)}, {"domains", std::move(domains)},
            {"process_peak_rss_bytes", rss}, {"rss_status", rss ? "available" : "unavailable"}};
}
} // namespace platformator_detail
