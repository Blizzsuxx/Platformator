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
void deallocatePooledObject(PooledObjectKind kind, void *ptr, size_t size = 0) noexcept;
void releaseEmptyObjectPools() noexcept;
} // namespace platformator_detail

#define PLATFORMATOR_DEFINE_POOLED_ALLOCATORS(Type, Kind)                                                   \
    void *Type::operator new(size_t size)                                                                   \
    {                                                                                                       \
        return platformator_detail::allocatePooledObject(platformator_detail::PooledObjectKind::Kind, size); \
    }                                                                                                       \
    void Type::operator delete(void *ptr, size_t size) noexcept                                             \
    {                                                                                                       \
        platformator_detail::deallocatePooledObject(platformator_detail::PooledObjectKind::Kind, ptr, size); \
    }                                                                                                       \
    void *Type::operator new(size_t size, const std::nothrow_t &) noexcept                                  \
    {                                                                                                       \
        return platformator_detail::allocatePooledObjectNothrow(platformator_detail::PooledObjectKind::Kind, size); \
    }                                                                                                       \
    void Type::operator delete(void *ptr, const std::nothrow_t &) noexcept                                  \
    {                                                                                                       \
        platformator_detail::deallocatePooledObject(platformator_detail::PooledObjectKind::Kind, ptr);      \
    }
