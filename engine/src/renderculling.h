#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <array>
#include <vector>

#include "platformator/objecthandle.h"
#include "spriterendergeometry.h"

namespace platformator_detail
{
enum class RenderCullingMode
{
    SubmitAll,
    CurrentLinear,
    CachedLinear,
    Indexed
};

enum class RenderCullingSpatialKind
{
    Indexed,
    Oversized,
    Uncertifiable
};

// Optional renderer-owned state. SDLWindow is the sole owner and creates it
// only for an enabled cached comparison mode with a live renderer.
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
        bool hasTexture = false;
        // IDs and backlinks are active only in indexed mode; cached-linear
        // state never allocates or traverses the spatial pools.
        std::array<RecordIndex, 4> cellLinks{absentIndex, absentIndex, absentIndex, absentIndex};
        RecordIndex cellLinkCount = 0;
        RecordIndex sideListPosition = absentIndex;
        uint32_t lastQueryStamp = 0;
        RenderCullingSpatialKind spatialKind = RenderCullingSpatialKind::Uncertifiable;
    };

    struct VisibleRankChange
    {
        RecordIndex rank = absentIndex;
        bool visible = false;
    };

    explicit RenderCullingState(RenderCullingMode mode);
    ~RenderCullingState();

    RenderCullingMode mode() const;
    void reserveRecords(RecordIndex capacity);
    void appendRecord(platformator::ObjectHandle spriteHandle);
    void removeRecord(RecordIndex index);
    void markDirty(RecordIndex index, platformator::ObjectHandle spriteHandle) noexcept;
    void cancelDirty(RecordIndex index) noexcept;
    RecordIndex dirtyCount() const;
    RecordIndex dirtyAt(RecordIndex index) const;
    void clearDirtyQueue() noexcept;
    void releaseStorage() noexcept;
    const std::vector<RecordIndex> &visibleRanks() const;
    bool selectionValid() const;
    const SpriteRenderProjectionKey &projectionKey() const;
    const SpriteRenderViewKey &viewKey() const;
    void beginVisibleSelection();
    void appendVisibleRank(RecordIndex rank);
    void sortVisibleRanks();
    void beginVisibleRankChanges();
    void queueVisibleRankChange(RecordIndex rank, bool visible);
    void applyVisibleRankChanges();
    void setPreparedKeys(const SpriteRenderProjectionKey &projection, const SpriteRenderViewKey &view);
    void setSpatialCellSize(double width, double height);
    RenderCullingSpatialKind updateSpatialBounds(RecordIndex index, const SpriteRenderBounds &bounds,
                                                bool certifiable);
    const std::vector<RecordIndex> &querySpatial(const SpriteRenderBounds &query);
    bool validateSpatialInvariants() const;
    bool hasSpatialIndex() const;
    RecordIndex spatialCellCount() const;
    RecordIndex spatialCellCapacity() const;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    void forceQueryStampForTesting(uint64_t stamp);
    static size_t spatialHashForTesting(int64_t x, int64_t y);
#endif
    RecordIndex recordCount() const;
    Record &recordAt(RecordIndex index);
    const Record &recordAt(RecordIndex index) const;
    void publishCapacityObservation() const;

  private:
    struct SpatialIndex;
    void reserveSpatialCapacity(RecordIndex capacity);
    void removeSpatialMembership(RecordIndex index);
    void insertSpatialMembership(RecordIndex index, const std::array<int64_t, 4> &cellRange);
    void updateSpatialCapacityObservation() const;

    RenderCullingMode currentMode;
    // One record and queue-position sentinel per registered Sprite slot. Rank
    // is the vector index, matching Sprite::windowIndex; no second membership
    // registry or per-Sprite cache state is maintained.
    std::vector<Record> records;
    std::vector<RecordIndex> dirtyQueue;
    std::vector<RecordIndex> dirtyQueuePosition;
    std::vector<RecordIndex> currentVisibleRanks;
    std::vector<RecordIndex> visibleRankMerge;
    std::vector<VisibleRankChange> visibleRankChanges;
    SpriteRenderProjectionKey currentProjectionKey{};
    SpriteRenderViewKey cachedViewKey{};
    bool visibleSelectionValid = false;
    std::unique_ptr<SpatialIndex> spatialIndex;
};
} // namespace platformator_detail
