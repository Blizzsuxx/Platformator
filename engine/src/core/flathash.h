#pragma once

#include <cstddef>
#include <cstdint>

namespace platformator_detail
{
inline uint64_t splitMix64(uint64_t x) noexcept
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

inline size_t hashInt32(int key) noexcept
{
    // Low-bias 32-bit integer finalizer.
    uint32_t x = static_cast<uint32_t>(key);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<size_t>(x);
}

inline size_t hashPointer(const void *ptr) noexcept
{
    // SplitMix64 finalizer on pointer value (ignoring low alignment bits).
    return static_cast<size_t>(splitMix64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr) >> 3)));
}

inline size_t hashPointerPair(const void *first, const void *second) noexcept
{
    const uintptr_t p1 = reinterpret_cast<uintptr_t>(first);
    const uintptr_t p2 = reinterpret_cast<uintptr_t>(second);
    uint64_t x = static_cast<uint64_t>(p1 >> 3) * 0x9e3779b97f4a7c15ULL;
    x ^= static_cast<uint64_t>(p2 >> 3) + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
    return static_cast<size_t>(splitMix64(x));
}

struct FlatInt32Hash
{
    size_t operator()(int key) const noexcept
    {
        return hashInt32(key);
    }
};
} // namespace platformator_detail

template <typename T>
struct FlatPointerHash
{
    size_t operator()(const T *key) const noexcept
    {
        return platformator_detail::hashPointer(key);
    }
};

template <typename T>
struct FlatPointerIsEmpty
{
    bool operator()(const T *key) const noexcept
    {
        return key == nullptr;
    }
};
