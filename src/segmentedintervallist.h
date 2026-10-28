#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "localsortarray.h"

class AABB;

class SegmentedIntervalList
{
    friend class LocalSortArray;

public:
    SegmentedIntervalList(AABB *owner, bool isPrimary);
    ~SegmentedIntervalList();

    void clear();
    void add(BoundingRadiusProjectionAxisBinding *binding);
    void remove(BoundingRadiusProjectionAxisBinding *binding);
    void repairProjection(BoundingRadiusProjectionProxy *projection);

    bool getIsPrimary() const;
    bool getIsEmpty() const;
    size_t getChunkCount() const;
    // Owned dynamic payload; excludes allocator headers and the inline owner object.
    size_t getOwnedCapacityBytes() const;

private:
    template <typename EmitFn>
    void processProjectionCollisions(
        BoundingRadiusProjectionProxy *lowerProjection,
        size_t lowerIndexInsideChunk,
        BoundingRadiusProjectionProxy *upperProjection,
        size_t upperIndexInsideChunk,
        EmitFn &&emit);

    size_t binarySearch(BoundingRadiusProjectionProxy *element);
    size_t binarySearch(LocalSortArray *array);
    std::pair<LocalSortArray *, size_t> find(BoundingRadiusProjectionProxy *element);

    std::pair<LocalSortArray *, size_t> add(BoundingRadiusProjectionProxy *element);
    std::pair<LocalSortArray *, size_t> add(BoundingRadiusProjectionProxy *element, size_t chunkIndex);
    std::pair<LocalSortArray *, size_t> remove(BoundingRadiusProjectionProxy *element);
    std::pair<LocalSortArray *, size_t> remove(LocalSortArray *array, size_t index);
    std::pair<LocalSortArray *, size_t> getPreviousIndex(LocalSortArray *chunk, size_t index) const;
    std::pair<LocalSortArray *, size_t> getNextIndex(LocalSortArray *chunk, size_t index) const;

    void repairProjectionFromIndexInternal(BoundingRadiusProjectionProxy *projection, LocalSortArray *chunk, size_t arrayIndex);
    void addCollisionsForNewlyAddedProjection(BoundingRadiusProjectionProxy *lowerProjection, size_t lowerIndexInsideChunkWhereItWasInserted, BoundingRadiusProjectionProxy *upperProjection, size_t upperIndexInsideChunkWhereItWasInserted);
    void removeCollisionsForRemovedProjection(BoundingRadiusProjectionProxy *lowerProjection, size_t lowerIndexInsideChunkWhereItWasRemoved, BoundingRadiusProjectionProxy *upperProjection, size_t upperIndexInsideChunkWhereItWasRemoved);

    void emitCollision(ProjectionOwner endpointA, ProjectionOwner endpointB);
    void removeCollision(ProjectionOwner endpointA, ProjectionOwner endpointB);
    void swap(BoundingRadiusProjectionProxy *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjectionProxy *rightRadiusProjection, size_t rightRadiusProjectionIndex);

    std::vector<LocalSortArray *> chunks;
    AABB *owner;

    // we only need to check for collisions in one of the axes when inserting or removing an interval, so we can use this to know if we should emit a collision or not
    bool isPrimary;
};
