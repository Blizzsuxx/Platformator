#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

// Indexed dense-list membership helper supporting const pointers with mutable index fields.
template <typename T>
inline void membership(std::vector<T *> &list, T *item, size_t std::remove_const_t<T>::*index, bool desired)
{
    using Mutable = std::remove_const_t<T>;
    auto &position = const_cast<Mutable *>(item)->*index;
    if (desired == (position != SIZE_MAX))
    {
        return;
    }
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

template <typename T, typename GetIndex, typename SetIndex>
inline void swapRemoveIndexed(std::vector<T> &list, T item, GetIndex &&getIndex, SetIndex &&setIndex)
{
    const size_t removeIndex = getIndex(item);
    if (removeIndex == SIZE_MAX)
    {
        return;
    }
    const size_t lastIndex = list.size() - 1;
    if (removeIndex != lastIndex)
    {
        T moved = list.back();
        list[removeIndex] = moved;
        setIndex(moved, removeIndex);
    }
    list.pop_back();
    setIndex(item, SIZE_MAX);
}
