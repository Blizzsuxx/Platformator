#include "rendering/sdlwindow.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>
#include <vector>

#include "benchmark/workprobe.h"
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "platformator/sprite.h"
#include "rendering/spriterendergeometry.h"

namespace
{
bool overlaps(const platformator_detail::SpriteRenderBounds &a,
              const platformator_detail::SpriteRenderBounds &b)
{
    return a.maxX >= b.minX && a.minX <= b.maxX && a.maxY >= b.minY && a.minY <= b.maxY;
}

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
struct ScopedBenchmarkTiming
{
    SDLWindow::BenchmarkOrderTiming &timing;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    ~ScopedBenchmarkTiming() noexcept
    {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        const std::uint64_t ns = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
        ++timing.calls;
        timing.lastNs = ns;
        timing.totalNs += ns;
    }
};
#endif
} // namespace

bool SDLWindow::prepareVisibleSelection(Camera *camera, const platformator_detail::SpriteRenderView &view)
{
    using namespace platformator_detail;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    ScopedBenchmarkTiming scopedSelectionTiming{benchmarkSelectionTiming};
#endif
    if (!renderCullingState || camera == nullptr)
        return false;
    if (!view.cullingEligible)
    {
        ++observedFallbackCounts.mappingFallbacks;
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingMappingFallbacks, 1);
        return false;
    }

    auto &state = *renderCullingState;
    const SpriteRenderViewKey currentView = makeSpriteRenderViewKey(camera->getHandle(), view);
    if (state.selectionValid() && state.dirtyCount() == 0 && sameSpriteRenderView(state.viewKey(), currentView))
    {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingCacheHits, 1);
        return true;
    }

    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingViewQueryBuilds, 1);
    SpriteRenderBounds query;
    if (!currentView.valid || !makeCertifiedRenderViewQuery(view, query))
    {
        ++observedFallbackCounts.numericFallbacks;
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingNumericFallbacks, 1);
        return false;
    }

    const bool projectionChanged = !sameSpriteRenderProjection(state.projectionKey(), currentView.projection);
    const bool viewChanged = !sameSpriteRenderView(state.viewKey(), currentView);
    const bool rebuildSelection = projectionChanged || viewChanged || !state.selectionValid();
    if (projectionChanged)
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingProjectionRebuilds, 1);

    GameManager &manager = RuntimeAccess::gameManager();
    const auto resolveRecordSprite = [&](const RenderCullingState::Record &record) {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingHandleResolutions, 1);
        return static_cast<Sprite *>(manager.resolveHandle(record.spriteHandle, ObjectKind::Sprite));
    };

    const auto refreshRecord = [&](RenderCullingState::RecordIndex index) -> Sprite * {
        auto &record = state.recordAt(index);
        Sprite *sprite = resolveRecordSprite(record);
        if (sprite == nullptr || sprite->getTexture() == nullptr)
        {
            record.boundsValid = false;
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingBoundsRefreshes, 1);
            return sprite;
        }

        const Eigen::Vector2f &position = camera->getPublishedSpritePosition(sprite);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingProxyCertifications, 1);
        record.boundsValid = makeCertifiedSpriteRenderProxy(position.x(), position.y(), sprite->getWidth(),
                                                            sprite->getHeight(),
                                                            sprite->getGameObject()->getRotationInDegrees(),
                                                            view.scaleX, view.scaleY, record.bounds);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingBoundsRefreshes, 1);
        return sprite;
    };

    const auto visibleWithResolvedSprite = [&](RenderCullingState::RecordIndex rank, Sprite *resolvedSprite,
                                               bool hasResolvedSprite) {
        const auto &record = state.recordAt(rank);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingCandidateVisits, 1);
        if (!record.boundsValid)
            return true;
        if (!overlaps(record.bounds, query))
            return false;

        Sprite *sprite = hasResolvedSprite ? resolvedSprite : resolveRecordSprite(record);
        if (sprite == nullptr)
            return true;
        SDL_FRect destination{};
        float angleDegrees = 0.0f;
        camera->prepareSpriteGeometry(sprite, view, destination, angleDegrees);
        PLATFORMATOR_RENDER_VISIBILITY_CHECK();
        return classifySpriteForRender(destination, angleDegrees, view) != SpriteRenderClassification::Reject;
    };

    if (rebuildSelection)
    {
        if (projectionChanged)
        {
            for (RenderCullingState::RecordIndex rank = 0; rank < state.recordCount(); ++rank)
                refreshRecord(rank);
        }
        else
        {
            const auto queuedCount = state.dirtyCount();
            for (RenderCullingState::RecordIndex queued = 0; queued < queuedCount; ++queued)
                refreshRecord(state.dirtyAt(queued));
        }

        state.beginVisibleSelectionScratch();
        for (RenderCullingState::RecordIndex rank = 0; rank < state.recordCount(); ++rank)
        {
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingFullPopulationVisits, 1);
            if (visibleWithResolvedSprite(rank, nullptr, false))
                state.appendVisibleSelectionScratch(rank);
        }
        state.publishVisibleSelectionScratch();
    }
    else
    {
        const auto queuedCount = state.dirtyCount();
        assert(queuedCount > 0);
        state.beginVisibleDelta();
        for (RenderCullingState::RecordIndex queued = 0; queued < queuedCount; ++queued)
        {
            const auto rank = state.dirtyAt(queued);
            Sprite *sprite = refreshRecord(rank);
            state.updateVisibleRank(rank, visibleWithResolvedSprite(rank, sprite, true));
        }
        state.finishVisibleDelta();
    }

    state.setPreparedViewKey(currentView);
    state.clearDirtyQueue();
    return true;
}

void SDLWindow::prepareDrawOrder(bool useCachedSelection)
{
    if (!spriteDrawOrder)
        return;

    auto &order = *spriteDrawOrder;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    ScopedBenchmarkTiming scopedTiming{order.benchmarkTiming};
#endif

    const size_t population = spriteComponents.size();
    const bool currentAll = !useCachedSelection || (renderCullingState->visibleRanks().size() == population);
    const uint64_t currentRevision = useCachedSelection ? renderCullingState->visibleMembershipRevision() : 0;
    const bool membershipChanged = !order.preparedOnce || (currentAll != order.preparedAll) ||
                                   (!currentAll && order.preparedMembershipRevision != currentRevision);

    const auto entryLess = [](const SpriteDrawEntry &left, const SpriteDrawEntry &right) {
        PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderComparisons, 1);
        if (left.zOrder != right.zOrder)
            return left.zOrder < right.zOrder;
        return left.registrationSequence < right.registrationSequence;
    };

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    if (order.orderMode != BenchmarkSpriteOrderMode::ForcedFullSort && !order.orderDirty && !membershipChanged)
        return;

    if (order.orderMode == BenchmarkSpriteOrderMode::SingleEntryRepair && order.orderDirty && !membershipChanged &&
        order.singleEditCandidateValid && order.singleEditSlot < population)
    {
        const size_t targetRank = order.singleEditSlot;
        size_t oldPos = order.entries.size();
        size_t lookupVisits = 0;
        for (size_t idx = 0; idx < order.entries.size(); ++idx)
        {
            ++lookupVisits;
            if (order.entries[idx].rank == targetRank)
            {
                oldPos = idx;
                break;
            }
        }
        PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderLookupVisits, lookupVisits);
        if (oldPos < order.entries.size())
        {
            const SpriteDrawEntry updated{targetRank, spriteComponents[targetRank]->getZOrder(),
                                          order.sequenceBySlot[targetRank]};
            size_t moves = 0;
            if (oldPos > 0 && entryLess(updated, order.entries[oldPos - 1]))
            {
                auto it = std::lower_bound(order.entries.begin(),
                                           order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos), updated,
                                           entryLess);
                const size_t newPos = static_cast<size_t>(it - order.entries.begin());
                std::move_backward(it, order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos),
                                   order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1));
                order.entries[newPos] = updated;
                moves = oldPos - newPos;
            }
            else if (oldPos + 1 < order.entries.size() && entryLess(order.entries[oldPos + 1], updated))
            {
                auto it = std::lower_bound(order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1),
                                           order.entries.end(), updated, entryLess);
                const size_t newPos = static_cast<size_t>(it - order.entries.begin()) - 1;
                std::move(order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1), it,
                          order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos));
                order.entries[newPos] = updated;
                moves = newPos - oldPos;
            }
            else
            {
                order.entries[oldPos] = updated;
            }
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderPreparations, 1);
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderEntries, 1);
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderRepairs, 1);
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderMoves, moves);
            order.orderDirty = false;
            order.singleEditCandidateValid = false;
            return;
        }
    }
#else
    if (!order.orderDirty && !membershipChanged)
        return;
#endif

    PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderPreparations, 1);
    order.entries.clear();
    if (useCachedSelection)
    {
        for (const size_t rank : renderCullingState->visibleRanks())
        {
            Sprite *sprite = spriteComponents[rank];
            order.entries.push_back({rank, sprite->getZOrder(), order.sequenceBySlot[rank]});
        }
    }
    else
    {
        for (size_t rank = 0; rank < population; ++rank)
        {
            Sprite *sprite = spriteComponents[rank];
            order.entries.push_back({rank, sprite->getZOrder(), order.sequenceBySlot[rank]});
        }
    }
    PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderEntries, order.entries.size());

    if (order.entries.size() > 1)
    {
        // EFFICIENCY: Retained Z edits and membership changes perform a full O(V log V)
        // rebuild-and-sort over active visible slots. SingleEntryRepair exists behind
        // BenchmarkSpriteOrderMode for bounded comparison, but pilot timing under
        // software vsync=1 was inconclusive within run variation (median -0.001 ms,
        // stdev 0.065 ms vs ~7.4k shifted entries per edit), so the simpler full
        // rebuild remains the production default.
        PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderSorts, 1);
        std::sort(order.entries.begin(), order.entries.end(), entryLess);
    }

    order.preparedAll = currentAll;
    order.preparedMembershipRevision = currentAll ? 0 : currentRevision;
    order.preparedOnce = true;
    order.orderDirty = false;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    order.singleEditCandidateValid = false;
#endif
}

void SDLWindow::publishOrderCapacityObservation() const noexcept
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    if (spriteDrawOrder)
    {
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedRecords, spriteDrawOrder->sequenceBySlot.size());
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedEntryCapacity, spriteDrawOrder->entries.capacity());
        const size_t bytes =
            spriteDrawOrder->sequenceBySlot.capacity() * sizeof(uint64_t) +
            spriteDrawOrder->entries.capacity() * sizeof(SpriteDrawEntry);
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedBytes, bytes);
    }
    else
    {
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedRecords, 0);
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedEntryCapacity, 0);
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedBytes, 0);
    }
#endif
}

void SDLWindow::addSpriteComponent(Sprite *spriteComponent)
{
    if (spriteComponent == nullptr || spriteComponent->getIsRegisteredInWindow() ||
        !spriteComponent->getGameObject()->getActive())
    {
        return;
    }

    const size_t newIndex = spriteComponents.size();
    const size_t required = newIndex + 1;
    if (required > spriteComponents.capacity())
    {
        const size_t doubled = spriteComponents.capacity() > std::numeric_limits<size_t>::max() / 2
                                   ? std::numeric_limits<size_t>::max()
                                   : spriteComponents.capacity() * 2;
        const size_t target = std::max(required, std::max<size_t>(4, doubled));
        spriteComponents.reserve(target);
        if (spriteDrawOrder)
        {
            spriteDrawOrder->sequenceBySlot.reserve(target);
            spriteDrawOrder->entries.reserve(target);
        }
    }
    else if (spriteDrawOrder)
    {
        if (spriteDrawOrder->sequenceBySlot.capacity() < spriteComponents.capacity())
            spriteDrawOrder->sequenceBySlot.reserve(spriteComponents.capacity());
        if (spriteDrawOrder->entries.capacity() < spriteComponents.capacity())
            spriteDrawOrder->entries.reserve(spriteComponents.capacity());
    }
    if (renderCullingState)
        renderCullingState->appendRecord(spriteComponent->getHandle());
    if (spriteDrawOrder)
    {
        spriteDrawOrder->sequenceBySlot.push_back(spriteDrawOrder->nextSequence++);
        spriteDrawOrder->entries.clear();
        spriteDrawOrder->orderDirty = true;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        spriteDrawOrder->singleEditCandidateValid = false;
#endif
        spriteDrawOrder->preparedAll = false;
    }
    spriteComponents.push_back(spriteComponent);
    spriteComponent->setWindowIndex(newIndex);
    spriteComponent->setIsRegisteredInWindow(true);
    publishOrderCapacityObservation();
}

void SDLWindow::removeSpriteComponent(Sprite *spriteComponent)
{
    if (spriteComponent == nullptr || !spriteComponent->getIsRegisteredInWindow())
    {
        return;
    }

    size_t removeIndex = spriteComponent->getWindowIndex();
    size_t lastIndex = spriteComponents.size() - 1;

    if (spriteDrawOrder)
    {
        spriteDrawOrder->entries.clear();
        spriteDrawOrder->orderDirty = true;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        spriteDrawOrder->singleEditCandidateValid = false;
#endif
        spriteDrawOrder->preparedAll = false;
    }

    if (renderCullingState)
        renderCullingState->removeRecord(removeIndex);

    if (removeIndex != lastIndex)
    {
        Sprite *movedSprite = spriteComponents[lastIndex];
        spriteComponents[removeIndex] = movedSprite;
        movedSprite->setWindowIndex(removeIndex);
        if (spriteDrawOrder)
            spriteDrawOrder->sequenceBySlot[removeIndex] = spriteDrawOrder->sequenceBySlot[lastIndex];
    }

    if (spriteDrawOrder)
        spriteDrawOrder->sequenceBySlot.pop_back();

    spriteComponents.pop_back();
    spriteComponent->setWindowIndex(SIZE_MAX);
    spriteComponent->setIsRegisteredInWindow(false);
    publishOrderCapacityObservation();
}

void SDLWindow::notifySpriteGeometryChanged(Sprite *spriteComponent) noexcept
{
    if (!renderCullingState || spriteComponent == nullptr || !spriteComponent->getIsRegisteredInWindow())
        return;

    renderCullingState->markDirty(spriteComponent->getWindowIndex(), spriteComponent->getHandle());
}

void SDLWindow::notifySpriteOrderChanged(size_t slot) noexcept
{
    if (!spriteDrawOrder)
        return;
    auto &order = *spriteDrawOrder;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    if (order.orderDirty && order.orderMode != BenchmarkSpriteOrderMode::SingleEntryRepair)
        return;
#else
    if (order.orderDirty)
        return;
#endif
    const bool stale =
        slot >= spriteComponents.size() || !order.preparedOnce ||
        (!order.preparedAll && (!renderCullingState || !renderCullingState->selectionValid() ||
                                order.preparedMembershipRevision != renderCullingState->visibleMembershipRevision()));
    if (stale)
    {
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        order.singleEditCandidateValid = false;
#endif
        order.orderDirty = true;
        return;
    }
    const bool affected = order.preparedAll || renderCullingState->isRankVisible(slot);
    if (!affected)
        return;

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    if (order.orderMode == BenchmarkSpriteOrderMode::SingleEntryRepair)
    {
        if (!order.orderDirty && !order.entries.empty())
        {
            order.singleEditCandidateValid = true;
            order.singleEditSlot = slot;
        }
        else if (!order.singleEditCandidateValid || order.singleEditSlot != slot)
        {
            order.singleEditCandidateValid = false;
        }
    }
#endif
    order.orderDirty = true;
}

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
void SDLWindow::setBenchmarkSpriteOrderMode(BenchmarkSpriteOrderMode mode) noexcept
{
    if (!spriteDrawOrder || spriteDrawOrder->orderMode == mode)
        return;
    spriteDrawOrder->orderMode = mode;
    spriteDrawOrder->singleEditCandidateValid = false;
    spriteDrawOrder->orderDirty = true;
}

SDLWindow::BenchmarkOrderTiming SDLWindow::consumeBenchmarkOrderTiming() noexcept
{
    if (!spriteDrawOrder)
        return {};
    const BenchmarkOrderTiming current = spriteDrawOrder->benchmarkTiming;
    spriteDrawOrder->benchmarkTiming = {};
    return current;
}

SDLWindow::BenchmarkOrderTiming SDLWindow::consumeBenchmarkSelectionTiming() noexcept
{
    const BenchmarkOrderTiming current = benchmarkSelectionTiming;
    benchmarkSelectionTiming = {};
    return current;
}
#endif

void SDLWindow::releaseRenderingStorage() noexcept
{
    if (renderCullingState)
        renderCullingState->releaseStorage();
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    benchmarkSelectionTiming = {};
#endif
    if (spriteDrawOrder)
    {
        std::vector<uint64_t>().swap(spriteDrawOrder->sequenceBySlot);
        std::vector<SpriteDrawEntry>().swap(spriteDrawOrder->entries);
        spriteDrawOrder->nextSequence = 1;
        spriteDrawOrder->preparedMembershipRevision = 0;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        spriteDrawOrder->singleEditSlot = 0;
        spriteDrawOrder->benchmarkTiming = {};
        spriteDrawOrder->singleEditCandidateValid = false;
#endif
        spriteDrawOrder->preparedOnce = false;
        spriteDrawOrder->orderDirty = true;
        spriteDrawOrder->preparedAll = false;
    }
    publishOrderCapacityObservation();
}

void SDLWindow::clearSceneComponents() noexcept
{
    for (Sprite *sprite : spriteComponents)
    {
        sprite->setIsRegisteredInWindow(false);
        sprite->setWindowIndex(SIZE_MAX);
    }
    spriteComponents.clear();
    mainCamera = nullptr;
    releaseRenderingStorage();
}

