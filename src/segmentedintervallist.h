#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "localsortarray.h"
#include "swapcallback.h"

class AABB;
enum Axis : uint8_t;

class SegmentedIntervalList : public SwapCallback
{
public:
    SegmentedIntervalList(AABB *owner, Axis axis, bool isPrimary);
    ~SegmentedIntervalList();

    void clear();
    void sort();
    void add(BoundingRadiusProjectionAxis *axis);
    void remove(BoundingRadiusProjectionAxis *axis);
    void addDirtyChunk(LocalSortArray *chunk);

    bool getIsPrimary() const;
    bool getIsEmpty() const;

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

    void swapBoundaries(LocalSortArray *leftChunk, LocalSortArray *rightChunk);
    void sortChunkFromIndex(LocalSortArray *chunk, size_t arrayIndex);
    void addCollisionsForNewlyAddedProjection(BoundingRadiusProjectionProxy *lowerProjection, size_t lowerIndexInsideChunkWhereItWasInserted, BoundingRadiusProjectionProxy *upperProjection, size_t upperIndexInsideChunkWhereItWasInserted);
    void removeCollisionsForRemovedProjection(BoundingRadiusProjectionProxy *lowerProjection, size_t lowerIndexInsideChunkWhereItWasRemoved, BoundingRadiusProjectionProxy *upperProjection, size_t upperIndexInsideChunkWhereItWasRemoved);

    void emitCollision(Collider *colliderA, Collider *colliderB);
    void removeCollision(Collider *colliderA, Collider *colliderB);
    void swap(BoundingRadiusProjectionProxy *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjectionProxy *rightRadiusProjection, size_t rightRadiusProjectionIndex) override;

    std::vector<LocalSortArray *> chunks;
    std::vector<LocalSortArray *> dirtyChunks;
    AABB *owner;
    Axis axis;

    // we only need to check for collisions in one of the axes when inserting or removing an interval, so we can use this to know if we should emit a collision or not
    bool isPrimary;
};