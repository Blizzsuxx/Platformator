#include "rendering/renderculling.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "benchmark/workprobe.h"

namespace platformator_detail
{
RenderCullingState::~RenderCullingState()
{
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedRecords, 0);
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedRecordCapacity, 0);
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedBytes, 0);
}

void RenderCullingState::reserveRecords(RecordIndex capacity)
{
    const RecordIndex target = nextGeometricCapacity(records.capacity(), capacity);
    // Retry every related allocation to the same target. An earlier reserve
    // may have grown records before another vector threw.
    records.reserve(target);
    dirtyQueue.reserve(target);
    dirtyQueuePosition.reserve(target);
    currentVisibleRanks.reserve(target);
    visibleRankMerge.reserve(target);
}

bool RenderCullingState::validateMembershipInvariants() const
{
    for (RecordIndex position = 0; position < currentVisibleRanks.size(); ++position)
    {
        const RecordIndex rank = currentVisibleRanks[position];
        if (rank >= records.size() || records[rank].visiblePosition != position)
            return false;
    }
    for (RecordIndex rank = 0; rank < records.size(); ++rank)
    {
        const RecordIndex position = records[rank].visiblePosition;
        if (position != absentIndex &&
            (position >= currentVisibleRanks.size() || currentVisibleRanks[position] != rank))
            return false;
    }
    return true;
}

void RenderCullingState::appendRecord(platformator::ObjectHandle spriteHandle)
{
    if (!spriteHandle)
        throw std::invalid_argument("A render-culling record requires a live Sprite handle");

    const RecordIndex required = records.size() + 1;
    reserveRecords(required);
    records.push_back(Record{spriteHandle});
    dirtyQueuePosition.push_back(absentIndex);
    visibleSelectionValid = false;
    ++visibleMembershipRevision_;
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingRecordsAdded, 1);
    markDirty(required - 1, spriteHandle);
    publishCapacityObservation();
}

void RenderCullingState::removeVisiblePosition(RecordIndex rank, RecordIndex position) noexcept
{
    const RecordIndex lastPosition = currentVisibleRanks.size() - 1;
    if (position != lastPosition)
    {
        const RecordIndex movedRank = currentVisibleRanks[lastPosition];
        currentVisibleRanks[position] = movedRank;
        records[movedRank].visiblePosition = position;
    }
    currentVisibleRanks.pop_back();
    records[rank].visiblePosition = absentIndex;
}

void RenderCullingState::removeRecord(RecordIndex index)
{
    if (index >= records.size())
        throw std::out_of_range("Render-culling record index is out of range");

    visibleSelectionValid = false;
    ++visibleMembershipRevision_;
    const RecordIndex visiblePos = records[index].visiblePosition;
    if (visiblePos != absentIndex)
        removeVisiblePosition(index, visiblePos);
    cancelDirty(index);
    const RecordIndex lastIndex = records.size() - 1;
    if (index != lastIndex)
    {
        const RecordIndex movedVisiblePos = records[lastIndex].visiblePosition;
        if (movedVisiblePos != absentIndex)
            currentVisibleRanks[movedVisiblePos] = index;
        records[index] = records[lastIndex];
        const RecordIndex movedQueuePosition = dirtyQueuePosition[lastIndex];
        dirtyQueuePosition[index] = movedQueuePosition;
        if (movedQueuePosition != absentIndex)
            dirtyQueue[movedQueuePosition] = index;
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingRecordsRelocated, 1);
    }
    records.pop_back();
    dirtyQueuePosition.pop_back();
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingRecordsRemoved, 1);
    publishCapacityObservation();
}

void RenderCullingState::markDirty(RecordIndex index, platformator::ObjectHandle spriteHandle) noexcept
{
    if (index >= records.size() || records[index].spriteHandle != spriteHandle ||
        dirtyQueuePosition[index] != absentIndex)
        return;

    // appendRecord reserves the queue alongside every record, so publishing a
    // main-thread notification cannot allocate.
    dirtyQueuePosition[index] = dirtyQueue.size();
    dirtyQueue.push_back(index);
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingDirtyEnqueues, 1);
}

void RenderCullingState::cancelDirty(RecordIndex index) noexcept
{
    if (index >= dirtyQueuePosition.size() || dirtyQueuePosition[index] == absentIndex)
        return;

    const RecordIndex queuePosition = dirtyQueuePosition[index];
    const RecordIndex movedIndex = dirtyQueue.back();
    if (queuePosition != dirtyQueue.size() - 1)
    {
        dirtyQueue[queuePosition] = movedIndex;
        dirtyQueuePosition[movedIndex] = queuePosition;
    }
    dirtyQueue.pop_back();
    dirtyQueuePosition[index] = absentIndex;
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingDirtyCancellations, 1);
}

RenderCullingState::RecordIndex RenderCullingState::dirtyCount() const
{
    return dirtyQueue.size();
}

RenderCullingState::RecordIndex RenderCullingState::dirtyAt(RecordIndex index) const
{
    assert(index < dirtyQueue.size());
    return dirtyQueue[index];
}

void RenderCullingState::clearDirtyQueue() noexcept
{
    for (const RecordIndex recordIndex : dirtyQueue)
        dirtyQueuePosition[recordIndex] = absentIndex;
    dirtyQueue.clear();
}

void RenderCullingState::releaseStorage() noexcept
{
    std::vector<Record>().swap(records);
    std::vector<RecordIndex>().swap(dirtyQueue);
    std::vector<RecordIndex>().swap(dirtyQueuePosition);
    std::vector<RecordIndex>().swap(currentVisibleRanks);
    std::vector<RecordIndex>().swap(visibleRankMerge);
    cachedViewKey = {};
    visibleMembershipRevision_ = 0;
    visibleSelectionValid = false;
    deltaMembershipChanged = false;
    publishCapacityObservation();
}

const std::vector<RenderCullingState::RecordIndex> &RenderCullingState::visibleRanks() const
{
    return currentVisibleRanks;
}

bool RenderCullingState::selectionValid() const
{
    return visibleSelectionValid;
}

uint64_t RenderCullingState::visibleMembershipRevision() const noexcept
{
    return visibleMembershipRevision_;
}

bool RenderCullingState::isRankVisible(RecordIndex rank) const noexcept
{
    return rank < records.size() && records[rank].visiblePosition != absentIndex;
}

const SpriteRenderProjectionKey &RenderCullingState::projectionKey() const
{
    return cachedViewKey.projection;
}

const SpriteRenderViewKey &RenderCullingState::viewKey() const
{
    return cachedViewKey;
}

void RenderCullingState::beginVisibleSelectionScratch()
{
    visibleRankMerge.clear();
}

void RenderCullingState::appendVisibleSelectionScratch(RecordIndex rank)
{
    assert(rank < records.size());
    visibleRankMerge.push_back(rank);
}

void RenderCullingState::publishVisibleSelectionScratch()
{
    if (visibleSelectionValid && visibleRankMerge.size() == currentVisibleRanks.size())
    {
        bool identical = true;
        for (const RecordIndex rank : visibleRankMerge)
        {
            PLATFORMATOR_RENDER_CULLING_ADD(visibleMembershipChecks, 1);
            if (records[rank].visiblePosition == absentIndex)
            {
                identical = false;
                break;
            }
        }
        if (identical)
        {
            visibleRankMerge.clear();
            return;
        }
    }

    for (const RecordIndex oldRank : currentVisibleRanks)
        records[oldRank].visiblePosition = absentIndex;
    currentVisibleRanks.swap(visibleRankMerge);
    visibleRankMerge.clear();
    for (RecordIndex position = 0; position < currentVisibleRanks.size(); ++position)
    {
        const RecordIndex rank = currentVisibleRanks[position];
        assert(records[rank].visiblePosition == absentIndex);
        records[rank].visiblePosition = position;
    }
    ++visibleMembershipRevision_;
    visibleSelectionValid = true;
    PLATFORMATOR_RENDER_CULLING_ADD(visibleMembershipChanges, 1);
}

void RenderCullingState::beginVisibleDelta()
{
    assert(visibleSelectionValid);
    deltaMembershipChanged = false;
}

void RenderCullingState::updateVisibleRank(RecordIndex rank, bool visible)
{
    assert(visibleSelectionValid);
    assert(rank < records.size());
    PLATFORMATOR_RENDER_CULLING_ADD(visibleMembershipChecks, 1);
    const RecordIndex position = records[rank].visiblePosition;
    const bool alreadyVisible = position != absentIndex;
    if (visible == alreadyVisible)
        return;

    deltaMembershipChanged = true;
    PLATFORMATOR_RENDER_CULLING_ADD(visibleMembershipChanges, 1);
    if (visible)
    {
        records[rank].visiblePosition = currentVisibleRanks.size();
        currentVisibleRanks.push_back(rank);
    }
    else
    {
        removeVisiblePosition(rank, position);
    }
}

bool RenderCullingState::finishVisibleDelta()
{
    assert(visibleSelectionValid);
    if (!deltaMembershipChanged)
        return false;
    ++visibleMembershipRevision_;
    deltaMembershipChanged = false;
    return true;
}

void RenderCullingState::setPreparedViewKey(const SpriteRenderViewKey &view)
{
    cachedViewKey = view;
}

RenderCullingState::RecordIndex RenderCullingState::recordCount() const
{
    return records.size();
}

RenderCullingState::Record &RenderCullingState::recordAt(RecordIndex index)
{
    assert(index < records.size());
    return records[index];
}

void RenderCullingState::publishCapacityObservation() const
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    const auto boundedBytes = [](RecordIndex capacity, RecordIndex itemSize) {
        if (capacity > std::numeric_limits<RecordIndex>::max() / itemSize)
            return std::numeric_limits<RecordIndex>::max();
        return capacity * itemSize;
    };
    RecordIndex bytes = boundedBytes(records.capacity(), sizeof(Record));
    const RecordIndex queueBytes = boundedBytes(dirtyQueue.capacity(), sizeof(RecordIndex));
    const RecordIndex positionBytes = boundedBytes(dirtyQueuePosition.capacity(), sizeof(RecordIndex));
    const RecordIndex visibleBytes = boundedBytes(currentVisibleRanks.capacity(), sizeof(RecordIndex));
    const RecordIndex visibleMergeBytes = boundedBytes(visibleRankMerge.capacity(), sizeof(RecordIndex));
    for (const RecordIndex amount : {queueBytes, positionBytes, visibleBytes, visibleMergeBytes})
    {
        if (amount > std::numeric_limits<RecordIndex>::max() - bytes)
        {
            bytes = std::numeric_limits<RecordIndex>::max();
            break;
        }
        bytes += amount;
    }
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedRecords, records.size());
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedRecordCapacity, records.capacity());
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedBytes, bytes);
#endif
}
} // namespace platformator_detail
