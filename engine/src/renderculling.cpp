#include "renderculling.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "workprobe.h"

namespace platformator_detail
{
struct RenderCullingState::SpatialIndex
{
    struct CellKey
    {
        int64_t x = 0;
        int64_t y = 0;
        bool operator==(const CellKey &) const = default;
    };

    struct CellKeyHash
    {
        size_t operator()(const CellKey &key) const noexcept
        {
            uint64_t value = static_cast<uint64_t>(key.x) * 0x9E3779B185EBCA87ULL;
            const uint64_t other = static_cast<uint64_t>(key.y) * 0xC2B2AE3D27D4EB4FULL;
            value ^= (other << 29) | (other >> 35);
            value ^= value >> 33;
            value *= 0xFF51AFD7ED558CCDULL;
            value ^= value >> 33;
            value *= 0xC4CEB9FE1A85EC53ULL;
            value ^= value >> 33;
            return static_cast<uint32_t>(value ^ (value >> 32));
        }
    };

    struct Cell
    {
        RecordIndex head = absentIndex;
    };
    using CellMap = std::unordered_map<CellKey, Cell, CellKeyHash>;
    struct Link
    {
        RecordIndex record = absentIndex;
        RecordIndex previous = absentIndex;
        RecordIndex next = absentIndex;
        CellKey key{};
    };

    CellMap cells;
    std::vector<CellMap::node_type> freeCellNodes;
    std::vector<Link> links;
    std::vector<RecordIndex> freeLinks;
    std::vector<RecordIndex> sideList;
    std::vector<RecordIndex> candidates;
    RecordIndex nodeSupply = 0;
    double cellWidth = 0.0;
    double cellHeight = 0.0;
    uint32_t queryStamp = 0;
    uint64_t temporaryKey = 0;
};

namespace
{
bool checkedCellCoordinate(double coordinate, double extent, int64_t &cell)
{
    if (!std::isfinite(coordinate) || !std::isfinite(extent) || extent <= 0.0)
        return false;
    const double floored = std::floor(coordinate / extent);
    constexpr double minimum = -9223372036854775808.0;
    constexpr double maximumExclusive = 9223372036854775808.0;
    if (!std::isfinite(floored) || floored < minimum || floored >= maximumExclusive)
        return false;
    cell = static_cast<int64_t>(floored);
    return true;
}

bool finiteBounds(const SpriteRenderBounds &bounds)
{
    return std::isfinite(bounds.minX) && std::isfinite(bounds.minY) && std::isfinite(bounds.maxX) &&
           std::isfinite(bounds.maxY) && bounds.maxX >= bounds.minX && bounds.maxY >= bounds.minY;
}

bool makeCellRange(const SpriteRenderBounds &bounds, double cellWidth, double cellHeight,
                   std::array<int64_t, 4> &range)
{
    return checkedCellCoordinate(bounds.minX, cellWidth, range[0]) &&
           checkedCellCoordinate(bounds.maxX, cellWidth, range[1]) &&
           checkedCellCoordinate(bounds.minY, cellHeight, range[2]) &&
           checkedCellCoordinate(bounds.maxY, cellHeight, range[3]);
}

bool sameCellRange(const std::array<int64_t, 4> &left, const std::array<int64_t, 4> &right)
{
    return left == right;
}
} // namespace

RenderCullingState::RenderCullingState(RenderCullingMode mode) : currentMode(mode)
{
    if (mode == RenderCullingMode::Indexed)
        spatialIndex = std::make_unique<SpatialIndex>();
}

RenderCullingState::~RenderCullingState()
{
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedRecords, 0);
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedRecordCapacity, 0);
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedBytes, 0);
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedCellCount, 0);
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedCellCapacity, 0);
}

RenderCullingMode RenderCullingState::mode() const
{
    return currentMode;
}

void RenderCullingState::reserveRecords(RecordIndex capacity)
{
    const RecordIndex maxIndexedCapacity = std::numeric_limits<RecordIndex>::max() / 4;
    if (spatialIndex && capacity > maxIndexedCapacity)
        throw std::length_error("Render-culling spatial capacity is too large");

    RecordIndex target = records.capacity();
    if (capacity > target)
    {
        const RecordIndex doubled = target > std::numeric_limits<RecordIndex>::max() / 2
                                        ? std::numeric_limits<RecordIndex>::max()
                                        : target * 2;
        target = std::max(capacity, std::max<RecordIndex>(4, doubled));
    }
    if (spatialIndex)
        target = std::min(target, maxIndexedCapacity);
    // Retry every related allocation to the same target. An earlier reserve
    // may have grown records before another vector or index pool threw.
    records.reserve(target);
    dirtyQueue.reserve(target);
    dirtyQueuePosition.reserve(target);
    currentVisibleRanks.reserve(target);
    visibleRankMerge.reserve(target);
    visibleRankChanges.reserve(target);
    reserveSpatialCapacity(target);
}

void RenderCullingState::reserveSpatialCapacity(RecordIndex capacity)
{
    if (!spatialIndex)
        return;
    if (capacity > std::numeric_limits<RecordIndex>::max() / 4)
        throw std::length_error("Render-culling spatial capacity is too large");

    auto &grid = *spatialIndex;
    const RecordIndex target = capacity * 4;
    grid.cells.reserve(target);
    grid.freeCellNodes.reserve(target);
    grid.links.reserve(target);
    grid.freeLinks.reserve(target);
    grid.sideList.reserve(capacity);
    grid.candidates.reserve(capacity);
    while (grid.links.size() < target)
    {
        const RecordIndex linkId = grid.links.size();
        grid.links.push_back({});
        grid.freeLinks.push_back(linkId);
    }
    while (grid.nodeSupply < target)
    {
        int64_t keyY = 0;
        if (grid.temporaryKey > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            throw std::length_error("Render-culling cell-node pool key space is exhausted");
        keyY = static_cast<int64_t>(grid.temporaryKey++);
        typename SpatialIndex::CellMap::iterator inserted;
        for (;;)
        {
            const SpatialIndex::CellKey temporaryKey{std::numeric_limits<int64_t>::min(), keyY};
            if (!grid.cells.contains(temporaryKey))
            {
                inserted = grid.cells.emplace(temporaryKey, SpatialIndex::Cell{}).first;
                break;
            }
            if (grid.temporaryKey > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                throw std::length_error("Render-culling cell-node pool key space is exhausted");
            keyY = static_cast<int64_t>(grid.temporaryKey++);
        }
        grid.freeCellNodes.push_back(grid.cells.extract(inserted));
        ++grid.nodeSupply;
    }
    updateSpatialCapacityObservation();
}

void RenderCullingState::removeSpatialMembership(RecordIndex index)
{
    if (!spatialIndex)
        return;
    auto &grid = *spatialIndex;
    auto &record = records[index];
    for (RecordIndex item = 0; item < record.cellLinkCount; ++item)
    {
        const RecordIndex linkId = record.cellLinks[item];
        auto &link = grid.links[linkId];
        auto cell = grid.cells.find(link.key);
        assert(cell != grid.cells.end());
        if (link.previous == absentIndex)
            cell->second.head = link.next;
        else
            grid.links[link.previous].next = link.next;
        if (link.next != absentIndex)
            grid.links[link.next].previous = link.previous;
        link = {};
        grid.freeLinks.push_back(linkId);
        if (cell->second.head == absentIndex)
            grid.freeCellNodes.push_back(grid.cells.extract(cell));
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingIndexRemovals, 1);
    }
    record.cellLinks.fill(absentIndex);
    record.cellLinkCount = 0;

    if (record.sideListPosition != absentIndex)
    {
        const RecordIndex position = record.sideListPosition;
        const RecordIndex movedRecord = grid.sideList.back();
        if (position != grid.sideList.size() - 1)
        {
            grid.sideList[position] = movedRecord;
            records[movedRecord].sideListPosition = position;
        }
        grid.sideList.pop_back();
        record.sideListPosition = absentIndex;
    }
    record.spatialKind = RenderCullingSpatialKind::Uncertifiable;
}

void RenderCullingState::insertSpatialMembership(RecordIndex index, const std::array<int64_t, 4> &cellRange)
{
    assert(spatialIndex != nullptr);
    auto &grid = *spatialIndex;
    auto &record = records[index];
    for (int64_t y = cellRange[2];; ++y)
    {
        for (int64_t x = cellRange[0];; ++x)
        {
            const SpatialIndex::CellKey key{x, y};
            auto cell = grid.cells.find(key);
            if (cell == grid.cells.end())
            {
                if (grid.freeCellNodes.empty())
                    throw std::length_error("Render-culling cell-node pool is exhausted");
                auto node = std::move(grid.freeCellNodes.back());
                grid.freeCellNodes.pop_back();
                node.key() = key;
                node.mapped() = SpatialIndex::Cell{};
                auto insertion = grid.cells.insert(std::move(node));
                if (!insertion.inserted)
                {
                    grid.freeCellNodes.push_back(std::move(insertion.node));
                    cell = insertion.position;
                }
                else
                {
                    cell = insertion.position;
                }
            }

            if (grid.freeLinks.empty())
                throw std::length_error("Render-culling membership-link pool is exhausted");
            const RecordIndex linkId = grid.freeLinks.back();
            grid.freeLinks.pop_back();
            auto &link = grid.links[linkId];
            link.record = index;
            link.previous = absentIndex;
            link.next = cell->second.head;
            link.key = key;
            if (link.next != absentIndex)
                grid.links[link.next].previous = linkId;
            cell->second.head = linkId;
            record.cellLinks[record.cellLinkCount++] = linkId;
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingIndexInsertions, 1);

            if (x == cellRange[1])
                break;
        }
        if (y == cellRange[3])
            break;
    }
    record.spatialKind = RenderCullingSpatialKind::Indexed;
}

void RenderCullingState::updateSpatialCapacityObservation() const
{
    if (!spatialIndex)
    {
        PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedCellCount, 0);
        PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedCellCapacity, 0);
        return;
    }
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedCellCount, spatialIndex->cells.size());
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedCellCapacity, spatialIndex->nodeSupply);
}

void RenderCullingState::setSpatialCellSize(double width, double height)
{
    if (!spatialIndex)
        return;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0)
        throw std::invalid_argument("Render-culling cell dimensions must be finite and positive");
    if (spatialIndex->cellWidth == width && spatialIndex->cellHeight == height)
        return;

    for (RecordIndex index = 0; index < records.size(); ++index)
        removeSpatialMembership(index);
    spatialIndex->cellWidth = width;
    spatialIndex->cellHeight = height;
    updateSpatialCapacityObservation();
}

RenderCullingSpatialKind RenderCullingState::updateSpatialBounds(RecordIndex index,
                                                                  const SpriteRenderBounds &bounds,
                                                                  bool certifiable)
{
    if (!spatialIndex)
        return RenderCullingSpatialKind::Uncertifiable;
    if (index >= records.size())
        throw std::out_of_range("Render-culling spatial record index is out of range");

    auto &grid = *spatialIndex;
    auto &record = records[index];
    std::array<int64_t, 4> newRange{};
    RenderCullingSpatialKind newKind = RenderCullingSpatialKind::Indexed;
    const double width = bounds.maxX - bounds.minX;
    const double height = bounds.maxY - bounds.minY;
    if (!certifiable || !finiteBounds(bounds) || grid.cellWidth <= 0.0 || grid.cellHeight <= 0.0 ||
        !std::isfinite(width) || !std::isfinite(height))
    {
        newKind = RenderCullingSpatialKind::Uncertifiable;
    }
    else if (width > grid.cellWidth || height > grid.cellHeight)
    {
        newKind = RenderCullingSpatialKind::Oversized;
    }
    else if (!makeCellRange(bounds, grid.cellWidth, grid.cellHeight, newRange))
    {
        newKind = RenderCullingSpatialKind::Uncertifiable;
    }
    else if (static_cast<double>(newRange[1]) - static_cast<double>(newRange[0]) > 1.0 ||
             static_cast<double>(newRange[3]) - static_cast<double>(newRange[2]) > 1.0)
    {
        newKind = RenderCullingSpatialKind::Oversized;
    }

    const auto cellSpan = [](int64_t minimum, int64_t maximum) {
        if (maximum < minimum)
            return std::numeric_limits<uint64_t>::max();
        return static_cast<uint64_t>(maximum) - static_cast<uint64_t>(minimum);
    };
    const uint64_t spanX = cellSpan(newRange[0], newRange[1]);
    const uint64_t spanY = cellSpan(newRange[2], newRange[3]);
    bool sameIndexedCoverage = false;
    if (record.spatialKind == RenderCullingSpatialKind::Indexed && newKind == RenderCullingSpatialKind::Indexed &&
        spanX <= 1 && spanY <= 1 &&
        record.cellLinkCount == static_cast<RecordIndex>((spanX + 1) * (spanY + 1)))
    {
        std::array<int64_t, 4> oldRange{};
        bool first = true;
        for (RecordIndex item = 0; item < record.cellLinkCount; ++item)
        {
            const auto &key = grid.links[record.cellLinks[item]].key;
            if (first)
            {
                oldRange = {key.x, key.x, key.y, key.y};
                first = false;
            }
            else
            {
                oldRange[0] = std::min(oldRange[0], key.x);
                oldRange[1] = std::max(oldRange[1], key.x);
                oldRange[2] = std::min(oldRange[2], key.y);
                oldRange[3] = std::max(oldRange[3], key.y);
            }
        }
        sameIndexedCoverage = oldRange == newRange;
    }

    record.bounds = bounds;
    record.boundsValid = certifiable && finiteBounds(bounds);
    if (sameIndexedCoverage)
    {
        record.spatialKind = RenderCullingSpatialKind::Indexed;
        return record.spatialKind;
    }

    if ((record.spatialKind == RenderCullingSpatialKind::Indexed && newKind == RenderCullingSpatialKind::Indexed) ||
        record.spatialKind != newKind)
    {
        removeSpatialMembership(index);
        if (record.spatialKind == RenderCullingSpatialKind::Uncertifiable && newKind == RenderCullingSpatialKind::Indexed)
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingIndexRelinks, 1);
    }

    if (newKind == RenderCullingSpatialKind::Indexed)
    {
        if (record.cellLinkCount == 0)
            insertSpatialMembership(index, newRange);
    }
    else if (record.sideListPosition == absentIndex)
    {
        record.sideListPosition = grid.sideList.size();
        grid.sideList.push_back(index);
    }
    record.spatialKind = newKind;
    updateSpatialCapacityObservation();
    return newKind;
}

const std::vector<RenderCullingState::RecordIndex> &
RenderCullingState::querySpatial(const SpriteRenderBounds &query)
{
    if (!spatialIndex)
        return currentVisibleRanks;

    auto &grid = *spatialIndex;
    grid.candidates.clear();
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingQueries, 1);

    const auto appendCandidate = [&](RecordIndex index, uint32_t stamp) {
        auto &record = records[index];
        if (record.lastQueryStamp == stamp)
            return;
        record.lastQueryStamp = stamp;
        grid.candidates.push_back(index);
    };

    uint32_t nextStamp = grid.queryStamp + 1;
    if (nextStamp == 0)
    {
        for (auto &record : records)
            record.lastQueryStamp = 0;
        nextStamp = 1;
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingQueryStampResets, 1);
    }
    grid.queryStamp = nextStamp;

    std::array<int64_t, 4> range{};
    const bool validRange = finiteBounds(query) && grid.cellWidth > 0.0 && grid.cellHeight > 0.0 &&
                            makeCellRange(query, grid.cellWidth, grid.cellHeight, range) &&
                            static_cast<double>(range[1]) - static_cast<double>(range[0]) <= 3.0 &&
                            static_cast<double>(range[3]) - static_cast<double>(range[2]) <= 3.0;
    if (!validRange)
    {
        for (RecordIndex index = 0; index < records.size(); ++index)
        {
            appendCandidate(index, nextStamp);
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingFullPopulationVisits, 1);
        }
        return grid.candidates;
    }

    for (int64_t y = range[2];; ++y)
    {
        for (int64_t x = range[0];; ++x)
        {
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingCellVisits, 1);
            const auto cell = grid.cells.find({x, y});
            if (cell != grid.cells.end())
            {
                for (RecordIndex linkId = cell->second.head; linkId != absentIndex;
                     linkId = grid.links[linkId].next)
                    appendCandidate(grid.links[linkId].record, nextStamp);
            }
            if (x == range[1])
                break;
        }
        if (y == range[3])
            break;
    }
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingSideListVisits, grid.sideList.size());
    for (const RecordIndex rank : grid.sideList)
        appendCandidate(rank, nextStamp);
    return grid.candidates;
}

bool RenderCullingState::validateSpatialInvariants() const
{
    if (!spatialIndex)
        return currentMode != RenderCullingMode::Indexed;
    const auto &grid = *spatialIndex;
    std::vector<uint8_t> visited(grid.links.size(), 0);
    RecordIndex linkedCount = 0;
    for (const auto &[key, cell] : grid.cells)
    {
        RecordIndex previous = absentIndex;
        RecordIndex steps = 0;
        for (RecordIndex linkId = cell.head; linkId != absentIndex; linkId = grid.links[linkId].next)
        {
            if (linkId >= grid.links.size() || ++steps > grid.links.size() || visited[linkId])
                return false;
            const auto &link = grid.links[linkId];
            if (link.key != key || link.previous != previous || link.record >= records.size() ||
                records[link.record].spatialKind != RenderCullingSpatialKind::Indexed)
                return false;
            const auto &record = records[link.record];
            if (std::find(record.cellLinks.begin(), record.cellLinks.begin() + record.cellLinkCount, linkId) ==
                record.cellLinks.begin() + record.cellLinkCount)
                return false;
            visited[linkId] = 1;
            ++linkedCount;
            previous = linkId;
        }
    }

    std::vector<uint8_t> sideSeen(records.size(), 0);
    for (RecordIndex position = 0; position < grid.sideList.size(); ++position)
    {
        const RecordIndex rank = grid.sideList[position];
        if (rank >= records.size() || sideSeen[rank] || records[rank].sideListPosition != position ||
            records[rank].spatialKind == RenderCullingSpatialKind::Indexed)
            return false;
        sideSeen[rank] = 1;
    }

    RecordIndex expectedLinks = 0;
    for (RecordIndex rank = 0; rank < records.size(); ++rank)
    {
        const auto &record = records[rank];
        if (record.spatialKind == RenderCullingSpatialKind::Indexed)
        {
            if (record.cellLinkCount == 0 || record.cellLinkCount > 4 ||
                record.sideListPosition != absentIndex || sideSeen[rank])
                return false;
            expectedLinks += record.cellLinkCount;
            for (RecordIndex index = 0; index < record.cellLinkCount; ++index)
            {
                const RecordIndex linkId = record.cellLinks[index];
                if (linkId >= grid.links.size() || !visited[linkId] || grid.links[linkId].record != rank)
                    return false;
            }
        }
        else if (record.cellLinkCount != 0 || record.sideListPosition == absentIndex || !sideSeen[rank])
        {
            return false;
        }
    }
    const bool indexedCapacityBounded = records.capacity() <= std::numeric_limits<RecordIndex>::max() / 4;
    return linkedCount == expectedLinks && grid.nodeSupply == grid.cells.size() + grid.freeCellNodes.size() &&
           grid.links.size() == grid.freeLinks.size() + linkedCount && indexedCapacityBounded &&
           grid.nodeSupply <= 4 * records.capacity();
}

bool RenderCullingState::hasSpatialIndex() const
{
    return spatialIndex != nullptr;
}

RenderCullingState::RecordIndex RenderCullingState::spatialCellCount() const
{
    return spatialIndex ? spatialIndex->cells.size() : 0;
}

RenderCullingState::RecordIndex RenderCullingState::spatialCellCapacity() const
{
    return spatialIndex ? spatialIndex->nodeSupply : 0;
}

#if PLATFORMATOR_ENABLE_WORK_PROBES
void RenderCullingState::forceQueryStampForTesting(uint64_t stamp)
{
    if (!spatialIndex)
        return;
    spatialIndex->queryStamp = static_cast<uint32_t>(stamp);
}

size_t RenderCullingState::spatialHashForTesting(int64_t x, int64_t y)
{
    return SpatialIndex::CellKeyHash{}({x, y});
}
#endif

void RenderCullingState::appendRecord(platformator::ObjectHandle spriteHandle)
{
    if (!spriteHandle)
        throw std::invalid_argument("A render-culling record requires a live Sprite handle");

    const RecordIndex required = records.size() + 1;
    reserveRecords(required);
    records.push_back(Record{spriteHandle});
    dirtyQueuePosition.push_back(absentIndex);
    visibleSelectionValid = false;
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingRecordsAdded, 1);
    markDirty(required - 1, spriteHandle);
    if (spatialIndex)
    {
        auto &record = records.back();
        record.sideListPosition = spatialIndex->sideList.size();
        spatialIndex->sideList.push_back(required - 1);
    }
    publishCapacityObservation();
}

void RenderCullingState::removeRecord(RecordIndex index)
{
    if (index >= records.size())
        throw std::out_of_range("Render-culling record index is out of range");

    visibleSelectionValid = false;
    currentVisibleRanks.clear();
    removeSpatialMembership(index);
    cancelDirty(index);
    const RecordIndex lastIndex = records.size() - 1;
    if (index != lastIndex)
    {
        records[index] = records[lastIndex];
        if (spatialIndex)
        {
            auto &movedRecord = records[index];
            for (RecordIndex item = 0; item < movedRecord.cellLinkCount; ++item)
                spatialIndex->links[movedRecord.cellLinks[item]].record = index;
            if (movedRecord.sideListPosition != absentIndex)
                spatialIndex->sideList[movedRecord.sideListPosition] = index;
        }
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
    updateSpatialCapacityObservation();
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
    std::vector<VisibleRankChange>().swap(visibleRankChanges);
    if (spatialIndex)
    {
        auto &grid = *spatialIndex;
        SpatialIndex::CellMap emptyCells;
        grid.cells.swap(emptyCells);
        std::vector<SpatialIndex::CellMap::node_type>().swap(grid.freeCellNodes);
        std::vector<SpatialIndex::Link>().swap(grid.links);
        std::vector<RecordIndex>().swap(grid.freeLinks);
        std::vector<RecordIndex>().swap(grid.sideList);
        std::vector<RecordIndex>().swap(grid.candidates);
        grid.nodeSupply = 0;
        grid.cellWidth = 0.0;
        grid.cellHeight = 0.0;
        grid.queryStamp = 0;
        grid.temporaryKey = 0;
    }
    currentProjectionKey = {};
    cachedViewKey = {};
    visibleSelectionValid = false;
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

const SpriteRenderProjectionKey &RenderCullingState::projectionKey() const
{
    return currentProjectionKey;
}

const SpriteRenderViewKey &RenderCullingState::viewKey() const
{
    return cachedViewKey;
}

void RenderCullingState::beginVisibleSelection()
{
    currentVisibleRanks.clear();
    visibleSelectionValid = true;
}

void RenderCullingState::appendVisibleRank(RecordIndex rank)
{
    assert(visibleSelectionValid);
    currentVisibleRanks.push_back(rank);
}

void RenderCullingState::sortVisibleRanks()
{
    assert(visibleSelectionValid);
    std::sort(currentVisibleRanks.begin(), currentVisibleRanks.end());
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingOrderWork, currentVisibleRanks.size());
}

void RenderCullingState::beginVisibleRankChanges()
{
    assert(visibleSelectionValid);
    visibleRankChanges.clear();
}

void RenderCullingState::queueVisibleRankChange(RecordIndex rank, bool visible)
{
    assert(visibleSelectionValid);
    assert(rank < records.size());
    const auto found = std::lower_bound(currentVisibleRanks.begin(), currentVisibleRanks.end(), rank);
    const bool alreadyVisible = found != currentVisibleRanks.end() && *found == rank;
    if (visible == alreadyVisible)
        return;

    visibleRankChanges.push_back({rank, visible});
}

void RenderCullingState::applyVisibleRankChanges()
{
    assert(visibleSelectionValid);
    if (visibleRankChanges.empty())
        return;

    std::sort(visibleRankChanges.begin(), visibleRankChanges.end(),
              [](const VisibleRankChange &left, const VisibleRankChange &right) { return left.rank < right.rank; });
    visibleRankMerge.clear();
    RecordIndex visibleIndex = 0;
    RecordIndex changeIndex = 0;
    while (visibleIndex < currentVisibleRanks.size() || changeIndex < visibleRankChanges.size())
    {
        if (changeIndex == visibleRankChanges.size() ||
            (visibleIndex < currentVisibleRanks.size() &&
             currentVisibleRanks[visibleIndex] < visibleRankChanges[changeIndex].rank))
        {
            visibleRankMerge.push_back(currentVisibleRanks[visibleIndex++]);
            continue;
        }

        const auto &change = visibleRankChanges[changeIndex++];
        if (visibleIndex < currentVisibleRanks.size() && currentVisibleRanks[visibleIndex] == change.rank)
            ++visibleIndex;
        if (change.visible)
            visibleRankMerge.push_back(change.rank);
    }

    const RecordIndex processed = currentVisibleRanks.size() + visibleRankChanges.size();
    currentVisibleRanks.swap(visibleRankMerge);
    visibleRankMerge.clear();
    visibleRankChanges.clear();
    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingOrderWork, processed);
}

void RenderCullingState::setPreparedKeys(const SpriteRenderProjectionKey &projection,
                                        const SpriteRenderViewKey &view)
{
    currentProjectionKey = projection;
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

const RenderCullingState::Record &RenderCullingState::recordAt(RecordIndex index) const
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
    const RecordIndex visibleChangesBytes =
        boundedBytes(visibleRankChanges.capacity(), sizeof(VisibleRankChange));
    for (const RecordIndex amount : {queueBytes, positionBytes, visibleBytes, visibleMergeBytes,
                                     visibleChangesBytes})
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
    if (spatialIndex)
    {
        const auto &grid = *spatialIndex;
        const auto addBytes = [&](RecordIndex amount) {
            if (amount > std::numeric_limits<RecordIndex>::max() - bytes)
                bytes = std::numeric_limits<RecordIndex>::max();
            else
                bytes += amount;
        };
        addBytes(boundedBytes(grid.links.capacity(), sizeof(SpatialIndex::Link)));
        addBytes(boundedBytes(grid.freeLinks.capacity(), sizeof(RecordIndex)));
        addBytes(boundedBytes(grid.sideList.capacity(), sizeof(RecordIndex)));
        addBytes(boundedBytes(grid.candidates.capacity(), sizeof(RecordIndex)));
        addBytes(boundedBytes(grid.freeCellNodes.capacity(), sizeof(SpatialIndex::CellMap::node_type)));
        addBytes(boundedBytes(grid.nodeSupply,
                              sizeof(SpatialIndex::CellMap::value_type) + 2 * sizeof(void *)));
        if (grid.nodeSupply != 0 || !grid.cells.empty())
            addBytes(boundedBytes(grid.cells.bucket_count(), sizeof(void *)));
    }
    PLATFORMATOR_RENDER_CULLING_SET(renderCullingOwnedBytes, bytes);
    updateSpatialCapacityObservation();
#endif
}
} // namespace platformator_detail
