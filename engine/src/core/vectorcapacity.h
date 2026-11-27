#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace platformator_detail
{
inline std::size_t nextGeometricCapacity(std::size_t currentCapacity, std::size_t required) noexcept
{
    if (required <= currentCapacity)
        return currentCapacity;
    const auto limit = std::numeric_limits<std::size_t>::max();
    const auto doubled = currentCapacity > limit / 2 ? limit : currentCapacity * 2;
    return std::max(required, std::max<std::size_t>(4, doubled));
}

template <typename T> void reserveForAppend(std::vector<T> &items, std::size_t count)
{
    if (count > items.max_size() - items.size())
        throw std::length_error("Vector capacity exhausted");
    const auto required = items.size() + count;
    if (required > items.capacity())
        items.reserve(std::min(items.max_size(), nextGeometricCapacity(items.capacity(), required)));
}
}
