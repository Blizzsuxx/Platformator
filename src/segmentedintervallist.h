#pragma once

#include <cstdint>
#include <vector>
#include "localsortarray.h"
#include "swapcallback.h"

class AABB;
enum Axis : uint8_t;

class SegmentedIntervalList : public SwapCallback
{
public:
    SegmentedIntervalList(AABB *owner, Axis axis);
    ~SegmentedIntervalList();

    void clear();
    void sort();
    void add(BoundingRadiusProjectionAxis *axis);
    void remove(BoundingRadiusProjectionAxis *axis);
    void addDirtyChunk(LocalSortArray *chunk);

private:
    template <typename EmitFn>
    void processProjectionCollisions(
        BoundingRadiusProjection *lowerProjection,
        size_t lowerIndexInsideChunk,
        BoundingRadiusProjection *upperProjection,
        size_t upperIndexInsideChunk,
        EmitFn &&emit);

    size_t binarySearch(BoundingRadiusProjection *element);
    size_t binarySearch(LocalSortArray *array);

    std::pair<LocalSortArray *, size_t> add(BoundingRadiusProjection *element);
    std::pair<LocalSortArray *, size_t> add(BoundingRadiusProjection *element, size_t chunkIndex);
    std::pair<LocalSortArray *, size_t> remove(BoundingRadiusProjection *element);

    void swapBoundaries(LocalSortArray *leftChunk, LocalSortArray *rightChunk);
    void sortChunkFromIndex(LocalSortArray *chunk, size_t arrayIndex);
    void addCollisionsForNewlyAddedProjection(BoundingRadiusProjection *lowerProjection, size_t lowerIndexInsideChunkWhereItWasInserted, BoundingRadiusProjection *upperProjection, size_t upperIndexInsideChunkWhereItWasInserted);
    void removeCollisionsForRemovedProjection(BoundingRadiusProjection *lowerProjection, size_t lowerIndexInsideChunkWhereItWasRemoved, BoundingRadiusProjection *upperProjection, size_t upperIndexInsideChunkWhereItWasRemoved);

    void emitCollision(Collider *colliderA, Collider *colliderB);
    void removeCollision(Collider *colliderA, Collider *colliderB);
    void swap(BoundingRadiusProjection *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjection *rightRadiusProjection, size_t rightRadiusProjectionIndex) override;

    std::vector<LocalSortArray *> chunks;
    std::vector<LocalSortArray *> dirtyChunks;
    AABB *owner;
    Axis axis;
};