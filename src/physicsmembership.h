#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

// Pair lists expose const pairs; only their mutable bookkeeping index changes.
template <typename T>
void membership(std::vector<T *> &list, T *item, size_t std::remove_const_t<T>::*index, bool desired)
{
    using Mutable = std::remove_const_t<T>;
    auto &position = const_cast<Mutable *>(item)->*index;
    if (desired == (position != SIZE_MAX)) return;
    if (desired)
    {
        const auto indexToInsert = list.size();
        list.push_back(item);
        position = indexToInsert;
    }
    else
    {
        auto *moved = list.back();
        list[position] = moved;
        const_cast<Mutable *>(moved)->*index = position;
        list.pop_back();
        position = SIZE_MAX;
    }
}
