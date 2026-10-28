#include "renderculling.h"
#include "spatialresourcehooks.h"

#include <utility>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL_render.h>

#include "dynamicaabbtree.h"
#include "platformator/collider.h"
#include "platformator/camera.h"
#include "platformator/sprite.h"
#include "spatialindex.h"
#include "workprobe.h"

namespace platformator_detail
{
bool RenderBounds::isFiniteOrdered() const
{
    return std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) && std::isfinite(maxY) && minX <= maxX &&
           minY <= maxY;
}

bool RenderBounds::overlapsInclusive(const RenderBounds &other) const
{
    return minX <= other.maxX && maxX >= other.minX && minY <= other.maxY && maxY >= other.minY;
}

bool RenderCulling::computeSpriteWorldBounds(const Sprite &sprite, const Camera &camera, int outputWidth,
                                             int outputHeight, RenderBounds &bounds)
{
    bounds = {};
    if (sprite.getGameObject() == nullptr || camera.getGameObject() == nullptr)
        return false;
    const auto spritePosition = sprite.getGameObject()->getPosition();
    const auto cameraPosition = camera.getGameObject()->getPosition();
    return computeSpriteWorldBounds(spritePosition.x(), spritePosition.y(), sprite.getWidth(), sprite.getHeight(),
                                    sprite.getGameObject()->getRotationInDegrees(), cameraPosition.x(),
                                    cameraPosition.y(), camera.getWidth(), camera.getHeight(), outputWidth, outputHeight,
                                    bounds);
}

bool RenderCulling::computeSpriteWorldBounds(float spriteCenterX, float spriteCenterY, float spriteWidth,
                                             float spriteHeight, float angleDegrees, float cameraX, float cameraY,
                                             float cameraWidth, float cameraHeight, int outputWidth, int outputHeight,
                                             RenderBounds &bounds)
{
    bounds = {};
    if (outputWidth <= 0 || outputHeight <= 0)
        return false;
    const float scaleX = static_cast<float>(outputWidth) / cameraWidth;
    const float scaleY = static_cast<float>(outputHeight) / cameraHeight;
    const double magnitude = std::max({std::abs(static_cast<double>(spriteCenterX)),
                                       std::abs(static_cast<double>(spriteCenterY)),
                                       std::abs(static_cast<double>(cameraX)), std::abs(static_cast<double>(cameraY)),
                                       std::abs(static_cast<double>(spriteWidth)),
                                       std::abs(static_cast<double>(spriteHeight)),
                                       std::abs(static_cast<double>(cameraWidth)),
                                       std::abs(static_cast<double>(cameraHeight))});
    if (!std::isfinite(cameraWidth) || !std::isfinite(cameraHeight) || cameraWidth <= 0.0f || cameraHeight <= 0.0f ||
        !std::isfinite(spriteWidth) || !std::isfinite(spriteHeight) || spriteWidth == 0.0f || spriteHeight == 0.0f ||
        !std::isfinite(angleDegrees) || !std::isfinite(spriteCenterX) || !std::isfinite(spriteCenterY) ||
        !std::isfinite(cameraX) || !std::isfinite(cameraY) || !std::isfinite(scaleX) || !std::isfinite(scaleY) ||
        scaleX <= 0.0f || scaleY <= 0.0f || !std::isfinite(magnitude) || magnitude > 1.0e15)
        return false;

    const double ratio = static_cast<double>(scaleY) / scaleX;
    const double radians = static_cast<double>(angleDegrees) * std::numbers::pi / 180.0;
    const double cosine = std::abs(std::cos(radians)), sine = std::abs(std::sin(radians));
    const double w = std::abs(static_cast<double>(spriteWidth)), h = std::abs(static_cast<double>(spriteHeight));
    const double halfX = (cosine*w + sine*h*ratio) * 0.5;
    const double halfY = (sine*w/ratio + cosine*h) * 0.5;
    const double geometryMagnitude = std::max(std::abs(double(spriteCenterX)), std::abs(double(spriteCenterY))) +
        (w+h)*(1.0 + std::max(ratio, 1.0/ratio));
    const double error = 32.0 * std::numeric_limits<float>::epsilon() * (geometryMagnitude + 1.0);
    bounds = {double(spriteCenterX)-halfX-error, double(spriteCenterY)-halfY-error,
              double(spriteCenterX)+halfX+error, double(spriteCenterY)+halfY+error};
    return bounds.isFiniteOrdered();
}

bool RenderCulling::computeCameraWorldBounds(const Camera &camera, int outputWidth, int outputHeight,
                                             RenderBounds &bounds)
{
    bounds = {};
    if (outputWidth <= 0 || outputHeight <= 0 || camera.getGameObject() == nullptr)
        return false;
    const Eigen::Vector2f position = camera.getGameObject()->getPosition();
    return computeCameraWorldBounds(position.x(), position.y(), camera.getWidth(), camera.getHeight(), outputWidth,
                                    outputHeight, bounds);
}

bool RenderCulling::computeCameraWorldBounds(float cameraX, float cameraY, float width, float height, int outputWidth,
                                             int outputHeight, RenderBounds &bounds)
{
    bounds = {};
    if (outputWidth <= 0 || outputHeight <= 0)
        return false;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f ||
        !std::isfinite(cameraX) || !std::isfinite(cameraY))
        return false;
    const double magnitude = std::max({std::abs(static_cast<double>(cameraX)),
                                       std::abs(static_cast<double>(cameraY)),
                                       std::abs(static_cast<double>(width)),
                                       std::abs(static_cast<double>(height))});
    if (!std::isfinite(magnitude) || magnitude > 1.0e15)
        return false;
    const double guardX = static_cast<double>(width) / outputWidth +
                          16.0 * std::numeric_limits<float>::epsilon() * (magnitude + 1.0);
    const double guardY = static_cast<double>(height) / outputHeight +
                          16.0 * std::numeric_limits<float>::epsilon() * (magnitude + 1.0);
    bounds = {static_cast<double>(cameraX) - guardX, static_cast<double>(cameraY) - guardY,
              static_cast<double>(cameraX) + width + guardX, static_cast<double>(cameraY) + height + guardY};
    return bounds.isFiniteOrdered();
}

void RenderCulling::sortByDrawOrder(std::vector<VisibilityCandidate> &candidates)
{
    PLATFORMATOR_WORK_ADD(renderCandidateSortCalls, 1);
    std::sort(candidates.begin(), candidates.end(), [](const VisibilityCandidate &a, const VisibilityCandidate &b) {
        if (a.windowIndex != b.windowIndex)
            return a.windowIndex < b.windowIndex;
        return a.recordIndex < b.recordIndex;
    });
}

namespace
{
constexpr float visibilityCellSize = 200.0f;
constexpr size_t maximumSegmentMemberships = 4096;
constexpr uint64_t visibilityCategory = 1;

bool toSpatialBounds(const RenderBounds &bounds, float padding, SpatialBounds &result)
{
    if (!bounds.isFiniteOrdered() || !std::isfinite(padding) || padding < 0.0f)
        return false;
    const auto outwardLow = [](double value) {
        float converted = static_cast<float>(value);
        if (!std::isfinite(converted))
            return converted;
        if (static_cast<double>(converted) > value)
            converted = std::nextafter(converted, -std::numeric_limits<float>::infinity());
        return converted;
    };
    const auto outwardHigh = [](double value) {
        float converted = static_cast<float>(value);
        if (!std::isfinite(converted))
            return converted;
        if (static_cast<double>(converted) < value)
            converted = std::nextafter(converted, std::numeric_limits<float>::infinity());
        return converted;
    };
    const double lowX = bounds.minX - padding;
    const double lowY = bounds.minY - padding;
    const double highX = bounds.maxX + padding;
    const double highY = bounds.maxY + padding;
    result = {outwardLow(lowX), outwardLow(lowY), outwardHigh(highX), outwardHigh(highY)};
    return result.isFiniteOrdered();
}

DynamicAabbTree::Bounds toTreeBounds(const SpatialBounds &bounds)
{
    return {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY};
}

bool externalRendererState(SDL_Renderer *renderer, std::string &reason)
{
    if (SDL_GetRenderTarget(renderer) != nullptr)
    {
        reason = "external-render-target";
        return true;
    }
    float scaleX = 1.0f, scaleY = 1.0f;
    if (!SDL_GetRenderScale(renderer, &scaleX, &scaleY) || scaleX != 1.0f || scaleY != 1.0f)
    {
        reason = "render-scale";
        return true;
    }
    SDL_Rect viewport{};
    int outputWidth = 0, outputHeight = 0;
    if (!SDL_GetRenderViewport(renderer, &viewport) || !SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight) ||
        viewport.x != 0 || viewport.y != 0 || viewport.w != outputWidth || viewport.h != outputHeight)
    {
        reason = "render-viewport";
        return true;
    }
    int logicalWidth = 0, logicalHeight = 0;
    SDL_RendererLogicalPresentation presentation = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (!SDL_GetRenderLogicalPresentation(renderer, &logicalWidth, &logicalHeight, &presentation) || logicalWidth != 0 ||
        logicalHeight != 0 || presentation != SDL_LOGICAL_PRESENTATION_DISABLED)
    {
        reason = "logical-presentation";
        return true;
    }
    return false;
}
} // namespace

struct RenderCulling::Impl
{
    struct Record
    {
        Sprite *sprite = nullptr;
        RenderBounds exactBounds{};
        SpatialBounds indexedBounds{};
        DynamicAabbTree::ProxyId proxy = -1;
        bool geometryValid = false;
        bool indexed = false;
        bool uncullable = true;
        bool dirty = true;
        std::unique_ptr<BoundingRadiusProjectionAxis> xAxis;
        std::unique_ptr<BoundingRadiusProjectionAxis> yAxis;
    };

    SpatialSettings settings;
    uint64_t runtimeSerial;
    std::vector<std::unique_ptr<Record>> records;
    std::vector<size_t> freeRecordSlots;
    std::unordered_map<Sprite *, size_t> recordBySprite;
    std::unordered_map<Sprite *, RenderCullingSnapshotSprite> snapshotGeometryBySprite;
    Camera *lastSnapshotCamera = nullptr;
    std::vector<size_t> dirtyRecords;
    std::unordered_set<size_t> uncullableSlots;
    std::unordered_set<Sprite *> segmentedHits;
    std::unique_ptr<DynamicAabbTree> tree;
    std::unique_ptr<SegmentedSpatialContext> segmented;
    std::unique_ptr<BoundingRadiusProjectionAxis> cameraXAxis;
    std::unique_ptr<BoundingRadiusProjectionAxis> cameraYAxis;
    Camera *indexedCamera = nullptr;
    bool cameraIndexed = false;
    RenderBounds currentView{};
    SpatialBounds indexedView{};
    std::vector<Sprite *> result;
    std::vector<VisibilityCandidate> candidates;
    RenderCullingFrameStats frameStats;
    uint64_t lastGeometryRevision = UINT64_MAX;
    uint64_t lastViewRevision = UINT64_MAX;
    uint64_t lastDrawOrderRevision = UINT64_MAX;
    int lastOutputWidth = 0;
    int lastOutputHeight = 0;
    float lastCameraWidth = 0.0f;
    float lastCameraHeight = 0.0f;
    size_t pendingMaintenance = 0;
    size_t retiredProxyRemoves = 0;
    size_t indexedRecordCount = 0;
    size_t cachedCandidateCount = 0;
    size_t cachedVisibleCount = 0;
    bool cachedVisibilityKnown = false;
    bool treeDirty = false;
    bool treeBuilt = false;
    double lastScaleRatio = -1;
    uint64_t cachedOrderHash = 0;

    void invalidateResultCache()
    {
        lastGeometryRevision = UINT64_MAX;
        lastViewRevision = UINT64_MAX;
        lastDrawOrderRevision = UINT64_MAX;
    }

    Impl(SpatialSettings settings, uint64_t runtimeSerial)
        : settings(settings), runtimeSerial(runtimeSerial), snapshotGeometryBySprite(), lastSnapshotCamera(nullptr)
    {
        SpatialResourceTagScope resourceTag(SpatialResourceTag::VisibilityIndex);
        if (settings.renderCulling == RenderCullingMode::DynamicBvh)
            tree = std::make_unique<DynamicAabbTree>();
        if (settings.renderCulling == RenderCullingMode::Segmented)
        {
            SpatialPairPolicy policy{this, ProjectionOwnerKind::Visibility, &Impl::segmentEligible,
                                     &Impl::segmentPairChanged};
            segmented = std::make_unique<SegmentedSpatialContext>(visibilityCellSize, policy);
        }
    }

    ~Impl()
    {
        // Segmented bindings retain pointers to endpoint axes; retire them before records/axes.
        segmented.reset();
        tree.reset();
    }

    static bool segmentEligible(void *context, ProjectionOwner a, ProjectionOwner b)
    {
        auto &self = *static_cast<Impl *>(context);
        const void *other = nullptr;
        if (a.pointer == self.indexedCamera)
            other = b.pointer;
        else if (b.pointer == self.indexedCamera)
            other = a.pointer;
        else
            return false;
        return self.recordBySprite.find(static_cast<Sprite *>(const_cast<void *>(other))) != self.recordBySprite.end();
    }

    static void segmentPairChanged(void *context, ProjectionOwner a, ProjectionOwner b, int delta)
    {
        auto &self = *static_cast<Impl *>(context);
        Sprite *sprite = nullptr;
        if (a.pointer == self.indexedCamera)
            sprite = static_cast<Sprite *>(b.pointer);
        else if (b.pointer == self.indexedCamera)
            sprite = static_cast<Sprite *>(a.pointer);
        if (!sprite)
            return;
        if (delta > 0)
            self.segmentedHits.insert(sprite);
        else
            self.segmentedHits.erase(sprite);
    }

    Record *find(Sprite *sprite)
    {
        auto found = recordBySprite.find(sprite);
        return found == recordBySprite.end() ? nullptr : records[found->second].get();
    }

    bool makeSpatialBounds(Record &record, SpatialBounds &out, float padding) const
    {
        if (!record.geometryValid || !toSpatialBounds(record.exactBounds, padding, out))
            return false;
        if (settings.renderCulling == RenderCullingMode::Segmented)
        {
            const double minCellX = std::floor(static_cast<double>(out.minX) / visibilityCellSize);
            const double maxCellX = std::floor(static_cast<double>(out.maxX) / visibilityCellSize);
            const double minCellY = std::floor(static_cast<double>(out.minY) / visibilityCellSize);
            const double maxCellY = std::floor(static_cast<double>(out.maxY) / visibilityCellSize);
            if (minCellX < std::numeric_limits<int>::min() || maxCellX > std::numeric_limits<int>::max() ||
                minCellY < std::numeric_limits<int>::min() || maxCellY > std::numeric_limits<int>::max())
                return false;
            const double cellsX = std::floor(static_cast<double>(out.maxX) / visibilityCellSize) -
                                  std::floor(static_cast<double>(out.minX) / visibilityCellSize) + 1.0;
            const double cellsY = std::floor(static_cast<double>(out.maxY) / visibilityCellSize) -
                                  std::floor(static_cast<double>(out.minY) / visibilityCellSize) + 1.0;
            if (cellsX <= 0 || cellsY <= 0 || cellsX * cellsY > maximumSegmentMemberships)
                return false;
        }
        if (settings.renderCulling == RenderCullingMode::DynamicBvh &&
            (static_cast<double>(out.maxX) - out.minX >= 100000.0 ||
             static_cast<double>(out.maxY) - out.minY >= 100000.0))
            return false;
        return true;
    }

    void removeFromIndex(size_t slot, Record &record)
    {
        if (!record.indexed)
            return;
        if (tree && record.proxy >= 0)
        {
            tree->remove(record.proxy);
            record.proxy = -1;
            treeDirty = true;
            ++frameStats.proxyRemoves;
        }
        else if (segmented)
        {
            segmented->remove({record.sprite, ProjectionOwnerKind::Visibility});
            ++frameStats.proxyRemoves;
        }
        record.indexed = false;
        if (indexedRecordCount > 0)
            --indexedRecordCount;
        PLATFORMATOR_WORK_ADD(renderIndexRemoves, 1);
        uncullableSlots.insert(slot);
        segmentedHits.erase(record.sprite);
        ++pendingMaintenance;
    }

    void indexRecord(size_t slot, Record &record)
    {
        SpatialBounds next{};
        const float padding = settings.renderCulling == RenderCullingMode::DynamicBvh ? settings.bvhPadding : 0.0f;
        const bool representable = makeSpatialBounds(record, next, padding);
        if (!representable)
        {
            removeFromIndex(slot, record);
            record.indexedBounds = {};
            record.uncullable = true;
            uncullableSlots.insert(slot);
            return;
        }

        bool indexChanged = false;
        if (tree)
        {
            if (record.indexed)
            {
                if (record.indexedBounds.minX != next.minX || record.indexedBounds.minY != next.minY ||
                    record.indexedBounds.maxX != next.maxX || record.indexedBounds.maxY != next.maxY)
                {
                    tree->updateMarked(record.proxy, toTreeBounds(next));
                    treeDirty = true;
                    PLATFORMATOR_WORK_ADD(renderIndexMoves, 1);
                    ++frameStats.proxyMoves;
                    indexChanged = true;
                }
            }
            else
            {
                record.proxy = tree->addMarked(toTreeBounds(next), visibilityCategory, slot);
                record.indexed = true;
                ++indexedRecordCount;
                treeDirty = true;
                PLATFORMATOR_WORK_ADD(renderIndexAdds, 1);
                ++frameStats.proxyInserts;
                indexChanged = true;
            }
        }
        else if (segmented)
        {
            const ProjectionOwner endpoint{record.sprite, ProjectionOwnerKind::Visibility};
            if (record.indexed)
            {
                if (record.indexedBounds.minX != next.minX || record.indexedBounds.minY != next.minY ||
                    record.indexedBounds.maxX != next.maxX || record.indexedBounds.maxY != next.maxY)
                {
                    segmented->update(endpoint, next);
                    PLATFORMATOR_WORK_ADD(renderIndexMoves, 1);
                    ++frameStats.proxyMoves;
                    indexChanged = true;
                }
            }
            else
            {
                record.xAxis = std::make_unique<BoundingRadiusProjectionAxis>(record.sprite,
                                                                               ProjectionOwnerKind::Visibility,
                                                                               next.minX, next.maxX);
                record.yAxis = std::make_unique<BoundingRadiusProjectionAxis>(record.sprite,
                                                                               ProjectionOwnerKind::Visibility,
                                                                               next.minY, next.maxY);
                segmented->insert(endpoint, record.xAxis.get(), record.yAxis.get(), next);
                record.indexed = true;
                ++indexedRecordCount;
                PLATFORMATOR_WORK_ADD(renderIndexAdds, 1);
                ++frameStats.proxyInserts;
                indexChanged = true;
            }
        }
        record.uncullable = false;
        uncullableSlots.erase(slot);
        record.indexedBounds = next;
        if (indexChanged)
            ++pendingMaintenance;
    }

    void setCameraEndpoint(Camera *camera, const SpatialBounds &bounds)
    {
        if (!segmented)
            return;
        if (indexedCamera != camera)
        {
            if (indexedCamera)
                segmented->remove({indexedCamera, ProjectionOwnerKind::Visibility});
            indexedCamera = camera;
            cameraIndexed = false;
            cameraXAxis.reset();
            cameraYAxis.reset();
            segmentedHits.clear();
            if (!camera)
                return;
            cameraXAxis = std::make_unique<BoundingRadiusProjectionAxis>(camera, ProjectionOwnerKind::Visibility,
                                                                          bounds.minX, bounds.maxX);
            cameraYAxis = std::make_unique<BoundingRadiusProjectionAxis>(camera, ProjectionOwnerKind::Visibility,
                                                                          bounds.minY, bounds.maxY);
            segmented->insert({camera, ProjectionOwnerKind::Visibility}, cameraXAxis.get(), cameraYAxis.get(), bounds);
            cameraIndexed = true;
            ++pendingMaintenance;
            return;
        }
        if (!cameraIndexed)
        {
            if (!camera)
                return;
            cameraXAxis = std::make_unique<BoundingRadiusProjectionAxis>(camera, ProjectionOwnerKind::Visibility,
                                                                          bounds.minX, bounds.maxX);
            cameraYAxis = std::make_unique<BoundingRadiusProjectionAxis>(camera, ProjectionOwnerKind::Visibility,
                                                                          bounds.minY, bounds.maxY);
            segmented->insert({camera, ProjectionOwnerKind::Visibility}, cameraXAxis.get(), cameraYAxis.get(), bounds);
            cameraIndexed = true;
            ++pendingMaintenance;
            return;
        }
        if (indexedCamera)
        {
            segmented->update({camera, ProjectionOwnerKind::Visibility}, bounds);
            ++pendingMaintenance;
        }
    }

    static bool treeCollect(DynamicAabbTree::ProxyId, uint64_t userData, void *context)
    {
        auto &self = *static_cast<Impl *>(context);
        const size_t slot = static_cast<size_t>(userData);
        if (slot < self.records.size() && self.records[slot] && self.records[slot]->sprite)
            self.candidates.push_back({slot, self.records[slot]->sprite->getWindowIndex()});
        return true;
    }

    uint64_t hashOrder(const std::vector<Sprite *> &sprites) const
    {
        uint64_t hash = 1469598103934665603ull;
        for (const Sprite *sprite : sprites)
        {
            const uint64_t index = sprite->getWindowIndex();
            for (unsigned byte = 0; byte < sizeof(index); ++byte)
            {
                hash ^= (index >> (byte * 8)) & 0xffu;
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    void gauges()
    {
        frameStats.indexProxyCount = settings.renderCulling == RenderCullingMode::Linear
            ? recordBySprite.size() - uncullableSlots.size() : indexedRecordCount;
        frameStats.uncullableCount = uncullableSlots.size();
        if (tree)
        {
            const auto metrics = tree->getMetrics();
            frameStats.treeHeight = static_cast<size_t>(std::max(0, metrics.height));
            frameStats.treeNodes = indexedRecordCount == 0 ? 0 : indexedRecordCount * 2 - 1;
        }
        if (segmented) frameStats.visibilityCells = segmented->getCellCount();
    }

    const std::vector<Sprite *> &prepareView(Camera *camera, const RenderCullingSnapshotView &view,
                                              const std::vector<Sprite *> &drawOrder, uint64_t geometryRevision,
                                              uint64_t viewRevision, uint64_t drawOrderRevision, bool snapshot)
    {
        SpatialResourceTagScope resourceTag(SpatialResourceTag::VisibilityIndex);
        frameStats = {};
        frameStats.proxyRemoves = std::exchange(retiredProxyRemoves, 0);
        const auto previousView = currentView;
        const auto viewStarted = std::chrono::steady_clock::now();
        if (!RenderCulling::computeCameraWorldBounds(static_cast<float>(view.positionX), static_cast<float>(view.positionY),
                static_cast<float>(view.width), static_cast<float>(view.height), view.outputWidth, view.outputHeight, currentView))
            return bypass(drawOrder, "invalid-camera-view");
        SpatialBounds viewIndexBounds{};
        if (!toSpatialBounds(currentView, 0.0f, viewIndexBounds))
            return bypass(drawOrder, "unrepresentable-camera-view");
        const double ratio = (static_cast<double>(view.outputHeight) * view.width) /
                             (static_cast<double>(view.outputWidth) * view.height);
        const bool ratioChanged = ratio != lastScaleRatio;
        const bool viewChanged = viewRevision != lastViewRevision || camera != lastSnapshotCamera ||
            previousView.minX != currentView.minX || previousView.minY != currentView.minY ||
            previousView.maxX != currentView.maxX || previousView.maxY != currentView.maxY;
        frameStats.boundsPrepareNanoseconds = elapsed(viewStarted);
        if (!ratioChanged && !viewChanged && geometryRevision == lastGeometryRevision &&
            drawOrderRevision == lastDrawOrderRevision && dirtyRecords.empty())
        {
            frameStats.resultCacheHit = true;
            frameStats.candidateCount = cachedCandidateCount;
            frameStats.visibleCount = cachedVisibleCount;
            frameStats.visibilityKnown = cachedVisibilityKnown;
            frameStats.outputOrderHash = cachedOrderHash;
            if (!uncullableSlots.empty()) frameStats.fallbackReason = "uncullable-sprites";
            gauges();
            PLATFORMATOR_WORK_ADD(renderResultCacheHits, 1);
            return result;
        }
        if (ratioChanged)
        {
            dirtyRecords.clear();
            for (const auto &[_, slot] : recordBySprite)
            {
                records[slot]->dirty = true;
                dirtyRecords.push_back(slot);
            }
        }
        const auto boundsStarted = std::chrono::steady_clock::now();
        for (const size_t slot : dirtyRecords)
        {
            auto &record = *records[slot];
            if (snapshot)
            {
                const auto &input = snapshotGeometryBySprite.at(record.sprite);
                record.geometryValid = RenderCulling::computeSpriteWorldBounds(
                    static_cast<float>(input.centerX), static_cast<float>(input.centerY),
                    static_cast<float>(input.width) * static_cast<float>(input.scaleX),
                    static_cast<float>(input.height) * static_cast<float>(input.scaleY),
                    static_cast<float>(input.angleDegrees), static_cast<float>(view.positionX),
                    static_cast<float>(view.positionY), static_cast<float>(view.width), static_cast<float>(view.height),
                    view.outputWidth, view.outputHeight, record.exactBounds);
            }
            else record.geometryValid = RenderCulling::computeSpriteWorldBounds(*record.sprite, *camera,
                                                         view.outputWidth, view.outputHeight, record.exactBounds);
            record.uncullable = !record.geometryValid;
            ++frameStats.boundsPrepared;
            PLATFORMATOR_WORK_ADD(renderBoundsPrepared, 1);
        }
        frameStats.boundsPrepareNanoseconds += elapsed(boundsStarted);
        const auto updateStarted = std::chrono::steady_clock::now();
        for (const size_t slot : dirtyRecords)
        {
            indexRecord(slot, *records[slot]);
            records[slot]->dirty = false;
        }
        dirtyRecords.clear();
        if (segmented && (viewChanged || !cameraIndexed))
        {
            const double cellsX = std::floor(double(viewIndexBounds.maxX) / visibilityCellSize) -
                                  std::floor(double(viewIndexBounds.minX) / visibilityCellSize) + 1;
            const double cellsY = std::floor(double(viewIndexBounds.maxY) / visibilityCellSize) -
                                  std::floor(double(viewIndexBounds.minY) / visibilityCellSize) + 1;
            if (cellsX <= 0 || cellsY <= 0 || cellsX*cellsY > maximumSegmentMemberships ||
                double(viewIndexBounds.minX)/visibilityCellSize < std::numeric_limits<int>::min() ||
                double(viewIndexBounds.maxX)/visibilityCellSize > std::numeric_limits<int>::max() ||
                double(viewIndexBounds.minY)/visibilityCellSize < std::numeric_limits<int>::min() ||
                double(viewIndexBounds.maxY)/visibilityCellSize > std::numeric_limits<int>::max())
                return bypass(drawOrder, "unrepresentable-segmented-view");
            setCameraEndpoint(camera, viewIndexBounds);
        }
        frameStats.indexUpdateNanoseconds = elapsed(updateStarted);
        if (treeDirty && tree)
        {
            const auto started = std::chrono::steady_clock::now();
            const auto rebuilt = tree->rebuild(!treeBuilt || settings.bvhMaintenance == BvhMaintenance::FullRebuild
                ? DynamicAabbTree::MaintenancePolicy::FullRebuild : DynamicAabbTree::MaintenancePolicy::NativePartial);
            treeBuilt = true;
            treeDirty = false;
            frameStats.rebuildCalls = 1;
            frameStats.rebuildItems = static_cast<size_t>(std::max(0, rebuilt.rebuildItemCount));
            frameStats.indexMaintenanceNanoseconds = elapsed(started);
        }
        const auto queryStarted = std::chrono::steady_clock::now();
        candidates.clear();
        frameStats.visibilityQueries = 1;
        PLATFORMATOR_WORK_ADD(renderVisibilityQueries, 1);
        const bool linear = settings.renderCulling == RenderCullingMode::Linear;
        if (linear)
        {
            // drawOrder is the authoritative, unique current window vector. Refine directly in
            // that order so the linear baseline stays O(N) and transparent submission order is
            // preserved without allocating, sorting, or deduplicating candidate records.
            frameStats.rawDiscoveries = drawOrder.size();
            frameStats.candidateCount = drawOrder.size();
            result.clear();
            const auto reduceStarted = std::chrono::steady_clock::now();
            for (Sprite *sprite : drawOrder)
            {
                const auto &record = *records[recordBySprite.at(sprite)];
                ++frameStats.exactChecks;
                if (record.uncullable || record.exactBounds.overlapsInclusive(currentView))
                    result.push_back(record.sprite);
            }
            PLATFORMATOR_WORK_ADD(renderFullOrderWalks, drawOrder.size());
            frameStats.outputOrderHash = hashOrder(result);
            frameStats.resultReduceNanoseconds = elapsed(reduceStarted);
        }
        else
        {
            if (tree || segmented)
                for (size_t slot : uncullableSlots)
                    candidates.push_back({slot, records[slot]->sprite->getWindowIndex()});
            if (tree)
                tree->query(toTreeBounds(viewIndexBounds), visibilityCategory, &Impl::treeCollect, this);
            else
                for (Sprite *sprite : segmentedHits)
                    candidates.push_back({recordBySprite.at(sprite), sprite->getWindowIndex()});
            frameStats.rawDiscoveries = candidates.size();
            frameStats.indexQueryNanoseconds = elapsed(queryStarted);
            const auto reduceStarted = std::chrono::steady_clock::now();
            RenderCulling::sortByDrawOrder(candidates);
            candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
                return a.recordIndex == b.recordIndex;
            }), candidates.end());
            frameStats.candidateCount = candidates.size();
            result.clear();
            for (const auto &candidate : candidates)
            {
                const auto &record = *records[candidate.recordIndex];
                ++frameStats.exactChecks;
                if (record.uncullable || record.exactBounds.overlapsInclusive(currentView))
                    result.push_back(record.sprite);
            }
            frameStats.outputOrderHash = hashOrder(result);
            frameStats.resultReduceNanoseconds = elapsed(reduceStarted);
        }
        frameStats.visibleCount = result.size();
        frameStats.visibilityKnown = uncullableSlots.empty();
        frameStats.maintenanceOperations = pendingMaintenance;
        if (pendingMaintenance) PLATFORMATOR_WORK_ADD(renderMaintenancePasses, 1);
        pendingMaintenance = 0;
        frameStats.geometryRevision = geometryRevision;
        frameStats.viewRevision = viewRevision;
        frameStats.drawOrderRevision = drawOrderRevision;
        if (!uncullableSlots.empty()) frameStats.fallbackReason = "uncullable-sprites";
        gauges();
        lastScaleRatio = ratio;
        lastSnapshotCamera = camera;
        lastGeometryRevision = geometryRevision;
        lastViewRevision = viewRevision;
        lastDrawOrderRevision = drawOrderRevision;
        cachedCandidateCount = frameStats.candidateCount;
        cachedVisibleCount = frameStats.visibleCount;
        cachedVisibilityKnown = frameStats.visibilityKnown;
        cachedOrderHash = frameStats.outputOrderHash;
        return result;
    }

    static uint64_t elapsed(std::chrono::steady_clock::time_point started)
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count());
    }

    const std::vector<Sprite *> &bypass(const std::vector<Sprite *> &drawOrder, const std::string &reason)
    {
        frameStats.proxyRemoves += std::exchange(retiredProxyRemoves, 0);
        frameStats.fallbackReason = reason;
        frameStats.candidateCount = drawOrder.size();
        result = drawOrder;
        frameStats.outputOrderHash = hashOrder(result);
        gauges();
        invalidateResultCache();
        return result;
    }

    const std::vector<Sprite *> &prepare(Camera *camera, int outputWidth, int outputHeight, SDL_Renderer *renderer,
        const std::vector<Sprite *> &drawOrder, uint64_t geometryRevision, uint64_t viewRevision, uint64_t orderRevision)
    {
        frameStats = {};
        if (!camera || !renderer) { result.clear(); invalidateResultCache(); return result; }
        std::string reason;
        if (externalRendererState(renderer, reason)) return bypass(drawOrder, reason);
        const auto position = camera->getGameObject()->getPosition();
        const RenderCullingSnapshotView view{position.x(), position.y(), camera->getWidth(), camera->getHeight(),
                                              outputWidth, outputHeight};
        return prepareView(camera, view, drawOrder, geometryRevision, viewRevision, orderRevision, false);
    }

    const std::vector<Sprite *> &prepareSnapshot(Camera *camera, const RenderCullingSnapshotView &view,
        const std::vector<Sprite *> &drawOrder, uint64_t geometryRevision, uint64_t viewRevision, uint64_t orderRevision)
    {
        if (settings.renderCulling == RenderCullingMode::None) return drawOrder;
        return prepareView(camera, view, drawOrder, geometryRevision, viewRevision, orderRevision, true);
    }

    bool isSnapshotSpriteUncullable(Sprite *sprite) const
    {
        const auto found = recordBySprite.find(sprite);
        return found == recordBySprite.end() || !records[found->second] || records[found->second]->uncullable;
    }
};

RenderCulling::RenderCulling(SDLWindow *, SpatialSettings settings, uint64_t runtimeSerial)
    : impl(std::make_unique<Impl>(settings, runtimeSerial))
{
}

RenderCulling::~RenderCulling() = default;

void RenderCulling::addSprite(Sprite *sprite)
{
    if (!sprite || impl->recordBySprite.find(sprite) != impl->recordBySprite.end())
        return;
    if (impl->runtimeSerial != 0 && sprite->getHandle().runtimeSerial != impl->runtimeSerial)
        throw std::logic_error("Render visibility sprite belongs to a different runtime");
    size_t slot;
    if (impl->freeRecordSlots.empty())
    {
        slot = impl->records.size();
        impl->records.push_back(std::make_unique<Impl::Record>());
    }
    else
    {
        slot = impl->freeRecordSlots.back();
        impl->freeRecordSlots.pop_back();
        impl->records[slot] = std::make_unique<Impl::Record>();
    }
    auto &record = *impl->records[slot];
    record.sprite = sprite;
    impl->recordBySprite.emplace(sprite, slot);
    impl->dirtyRecords.push_back(slot);
}

void RenderCulling::removeSprite(Sprite *sprite)
{
    if (!sprite)
        return;
    const auto found = impl->recordBySprite.find(sprite);
    if (found == impl->recordBySprite.end())
        return;
    const size_t slot = found->second;
    auto &record = *impl->records[slot];
    const auto previousRemoves = impl->frameStats.proxyRemoves;
    impl->removeFromIndex(slot, record);
    // Retirement runs in lifecycle cleanup; its time belongs to that enclosing step.
    // Carry its operation count to the next rendered publication without charging it twice.
    impl->retiredProxyRemoves += impl->frameStats.proxyRemoves - previousRemoves;
    impl->frameStats.proxyRemoves = previousRemoves;
    impl->uncullableSlots.erase(slot);
    impl->recordBySprite.erase(found);
    impl->snapshotGeometryBySprite.erase(sprite);
    impl->segmentedHits.erase(sprite);
    record.sprite = nullptr;
    impl->records[slot].reset();
    impl->freeRecordSlots.push_back(slot);
    impl->dirtyRecords.erase(std::remove(impl->dirtyRecords.begin(), impl->dirtyRecords.end(), slot),
                             impl->dirtyRecords.end());
    ++impl->pendingMaintenance;
}

void RenderCulling::markSpriteDirty(Sprite *sprite)
{
    if (auto *record = impl->find(sprite); record && !record->dirty)
    {
        record->dirty = true;
        const size_t slot = impl->recordBySprite.at(sprite);
        impl->dirtyRecords.push_back(slot);
    }
}

void RenderCulling::setCamera(Camera *camera)
{
    if (!impl->segmented)
        return;
    if (impl->indexedCamera == camera)
        return;
    if (impl->segmented && impl->cameraIndexed && impl->indexedCamera)
    {
        impl->segmented->remove({impl->indexedCamera, ProjectionOwnerKind::Visibility});
        ++impl->pendingMaintenance;
    }
    impl->indexedCamera = camera;
    impl->cameraIndexed = false;
    impl->cameraXAxis.reset();
    impl->cameraYAxis.reset();
    impl->segmentedHits.clear();
}

const std::vector<Sprite *> &RenderCulling::prepare(Camera *camera, int outputWidth, int outputHeight,
                                                    SDL_Renderer *renderer, const std::vector<Sprite *> &drawOrder,
                                                    uint64_t geometryRevision, uint64_t viewRevision,
                                                    uint64_t drawOrderRevision)
{
    return impl->prepare(camera, outputWidth, outputHeight, renderer, drawOrder, geometryRevision, viewRevision,
                         drawOrderRevision);
}

const std::vector<Sprite *> &RenderCulling::prepareSnapshot(
    Camera *cameraIdentity, const RenderCullingSnapshotView &view,
    const std::vector<Sprite *> &drawOrder, uint64_t geometryRevision, uint64_t viewRevision,
    uint64_t drawOrderRevision)
{
    return impl->prepareSnapshot(cameraIdentity, view, drawOrder, geometryRevision, viewRevision, drawOrderRevision);
}

void RenderCulling::updateSnapshotSprite(const RenderCullingSnapshotSprite &input)
{
    if (impl->settings.renderCulling == RenderCullingMode::None) return;
    if (!input.sprite) throw std::invalid_argument("Snapshot sprite must not be null");
    impl->snapshotGeometryBySprite[input.sprite] = input;
    addSprite(input.sprite);
    markSpriteDirty(input.sprite);
    PLATFORMATOR_WORK_ADD(renderSnapshotVisits, 1);
}

bool RenderCulling::isSnapshotSpriteUncullable(Sprite *sprite) const
{
    return impl->isSnapshotSpriteUncullable(sprite);
}

const RenderCullingFrameStats &RenderCulling::getFrameStats() const
{
    return impl->frameStats;
}

size_t RenderCulling::getOwnedCapacityBytes() const
{
    const size_t treeBytes = impl->tree ? impl->tree->getMetrics().memoryBytes : 0;
    size_t bytes = sizeof(*this) + sizeof(Impl) + treeBytes +
                   (impl->tree ? sizeof(DynamicAabbTree) : 0) + impl->records.capacity() * sizeof(std::unique_ptr<Impl::Record>) +
                   impl->freeRecordSlots.capacity() * sizeof(size_t) +
                   impl->dirtyRecords.capacity() * sizeof(size_t) + impl->result.capacity() * sizeof(Sprite *) +
                   impl->candidates.capacity() * sizeof(VisibilityCandidate) +
                   impl->recordBySprite.bucket_count() * sizeof(void *) +
                   impl->recordBySprite.size() * sizeof(std::pair<Sprite *const, size_t>) +
                   impl->snapshotGeometryBySprite.bucket_count() * sizeof(void *) +
                   impl->snapshotGeometryBySprite.size() *
                       sizeof(std::pair<Sprite *const, RenderCullingSnapshotSprite>);
    bytes += (impl->recordBySprite.size() + impl->snapshotGeometryBySprite.size() +
              impl->uncullableSlots.size() + impl->segmentedHits.size()) * 2 * sizeof(void *);
    for (const auto &record : impl->records)
        if (record)
            bytes += sizeof(Impl::Record) + (bool(record->xAxis) + bool(record->yAxis)) * sizeof(BoundingRadiusProjectionAxis);
    bytes += (bool(impl->cameraXAxis) + bool(impl->cameraYAxis)) * sizeof(BoundingRadiusProjectionAxis) +
             (impl->uncullableSlots.bucket_count() + impl->segmentedHits.bucket_count()) * sizeof(void *) +
             impl->uncullableSlots.size() * sizeof(size_t) + impl->segmentedHits.size() * sizeof(Sprite *);
    if (impl->segmented)
        bytes += impl->segmented->getOwnedCapacityBytes();
    return bytes;
}
} // namespace platformator_detail
