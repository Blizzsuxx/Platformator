#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "platformator/objecthandle.h"
#include "rendering/spriterendergeometry.h"

namespace platformator_detail
{
enum class RenderCullingMode
{
    SubmitAll,
    CachedLinear
};

// Optional renderer-owned state. SDLWindow is the sole owner and creates it
// only for enabled cached-linear culling with a live renderer.
class RenderCullingState
{
  public:
    using RecordIndex = std::size_t;
    static constexpr RecordIndex absentIndex = std::numeric_limits<RecordIndex>::max();

    struct Record
    {
        // Records are dense and rank-aligned with SDLWindow::spriteComponents.
        // They contain identity, never an owning or potentially stale pointer.
        platformator::ObjectHandle spriteHandle{};
        SpriteRenderBounds bounds{};
        bool boundsValid = false;
        RecordIndex visiblePosition = absentIndex;
    };

    RenderCullingState() = default;
    ~RenderCullingState();

    void reserveRecords(RecordIndex capacity);
    void appendRecord(platformator::ObjectHandle spriteHandle);
    void removeRecord(RecordIndex index);
    RecordIndex recordCount() const;
    Record &recordAt(RecordIndex index);
    void releaseStorage() noexcept;

    void markDirty(RecordIndex index, platformator::ObjectHandle spriteHandle) noexcept;
    RecordIndex dirtyCount() const;
    RecordIndex dirtyAt(RecordIndex index) const;
    void clearDirtyQueue() noexcept;

    // Cached projection and visible membership
    const std::vector<RecordIndex> &visibleRanks() const;
    bool selectionValid() const;
    uint64_t visibleMembershipRevision() const noexcept;
    bool isRankVisible(RecordIndex rank) const noexcept;
    const SpriteRenderProjectionKey &projectionKey() const;
    const SpriteRenderViewKey &viewKey() const;
    void beginVisibleSelectionScratch();
    void appendVisibleSelectionScratch(RecordIndex rank);
    void publishVisibleSelectionScratch();
    void beginVisibleDelta();
    void updateVisibleRank(RecordIndex rank, bool visible);
    bool finishVisibleDelta();
    void setPreparedViewKey(const SpriteRenderViewKey &view);

    bool validateMembershipInvariants() const;
    void publishCapacityObservation() const;

  private:
    void cancelDirty(RecordIndex index) noexcept;
    void removeVisiblePosition(RecordIndex rank, RecordIndex position) noexcept;
    // One record and queue-position sentinel per registered Sprite slot. Rank
    // is the vector index, matching Sprite::windowIndex; no second membership
    // registry or per-Sprite cache state is maintained.
    std::vector<Record> records;
    std::vector<RecordIndex> dirtyQueue;
    std::vector<RecordIndex> dirtyQueuePosition;
    std::vector<RecordIndex> currentVisibleRanks;
    std::vector<RecordIndex> visibleRankMerge;
    SpriteRenderViewKey cachedViewKey{};
    uint64_t visibleMembershipRevision_ = 0;
    bool visibleSelectionValid = false;
    bool deltaMembershipChanged = false;
};
} // namespace platformator_detail
