#pragma once

#include <cstddef>

namespace platformator_detail
{
enum class PooledObjectKind : size_t
{
    GameObject = 0,
    BoxCollider,
    CircleCollider,
    Rigidbody,
    Sprite,
    Count
};

void *allocatePooledObject(PooledObjectKind kind, size_t size);
void deallocatePooledObject(PooledObjectKind kind, void *ptr, size_t size) noexcept;
void releaseEmptyObjectPools() noexcept;
} // namespace platformator_detail
