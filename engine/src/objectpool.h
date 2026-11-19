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
void *allocatePooledObjectNothrow(PooledObjectKind kind, size_t size) noexcept;
void deallocatePooledObject(void *ptr) noexcept;
void releaseEmptyObjectPools() noexcept;
} // namespace platformator_detail
