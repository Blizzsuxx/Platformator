#include "rendering/sdlwindow.h"

#include <algorithm>
#include <cassert>
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
#include <chrono>
#endif
#include <cmath>
#include <limits>
#include <vector>

#include "benchmark/workprobe.h"
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "core/vectorcapacity.h"
#include "platformator/sprite.h"
#include "rendering/spriterendergeometry.h"

namespace
{
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
        timing.totalNs += ns;
    }
};
#endif

template <typename Entry>
bool spriteDrawEntryLess(const Entry &left, const Entry &right) noexcept
{
    PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderComparisons, 1);
    if (left.zOrder != right.zOrder)
    {
        return left.zOrder < right.zOrder;
    }
    return left.registrationSequence < right.registrationSequence;
}

class SpriteBatcher
{
  public:
    SpriteBatcher(SDL_Renderer *renderer, bool isSoftwareRenderer, std::vector<SDL_Vertex> &vertices,
                  std::vector<int> &indices)
        : renderer(renderer), isSoftwareRenderer(isSoftwareRenderer), vertices(vertices), indices(indices)
    {
    }

    void prepareTextureState(SDL_Texture *texture, SDL_Color color)
    {
        if (texture != batchTexture)
        {
            flush();
            batchTexture = texture;
            texBlendMode = SDL_BLENDMODE_BLEND;
            SDL_GetTextureBlendMode(batchTexture, &texBlendMode);
            texR = 1.0f;
            texG = 1.0f;
            texB = 1.0f;
            texA = 1.0f;
            SDL_GetTextureColorModFloat(batchTexture, &texR, &texG, &texB);
            SDL_GetTextureAlphaModFloat(batchTexture, &texA);
            promoteAlphaBlend = platformator_detail::shouldPromoteSpriteAlphaBlend(color, texBlendMode);
            texSizeValid = false;
            return;
        }
        const bool promoteAlpha = platformator_detail::shouldPromoteSpriteAlphaBlend(color, texBlendMode);
        if (promoteAlpha != promoteAlphaBlend)
        {
            flush();
            promoteAlphaBlend = promoteAlpha;
        }
    }

    bool resolveUVs(const SDL_FRect *src, SDL_FlipMode flip, float &u0, float &v0, float &u1, float &v1)
    {
        u0 = 0.0f;
        v0 = 0.0f;
        u1 = 1.0f;
        v1 = 1.0f;
        if (src != nullptr)
        {
            if (!texSizeValid)
            {
                texSizeValid = SDL_GetTextureSize(batchTexture, &texW, &texH) && texW > 0.0f && texH > 0.0f;
                if (texSizeValid)
                {
                    invTexW = 1.0f / texW;
                    invTexH = 1.0f / texH;
                }
            }
            if (texSizeValid)
            {
                const SDL_FRect bounds{0, 0, texW, texH};
                SDL_FRect clipped{};
                if (!SDL_GetRectIntersectionFloat(src, &bounds, &clipped))
                {
                    return false;
                }
                u0 = clipped.x * invTexW;
                v0 = clipped.y * invTexH;
                u1 = (clipped.x + clipped.w) * invTexW;
                v1 = (clipped.y + clipped.h) * invTexH;
            }
        }
        if ((flip & SDL_FLIP_HORIZONTAL) != 0)
        {
            std::swap(u0, u1);
        }
        if ((flip & SDL_FLIP_VERTICAL) != 0)
        {
            std::swap(v0, v1);
        }
        return true;
    }

    void appendQuad(const GameObject *object, const SDL_FRect &dst, SDL_Color color, float u0, float v0, float u1,
                    float v1)
    {
        constexpr float inv255 = 1.0f / 255.0f;
        const bool modulated = platformator_detail::isColorModulated(color);
        const SDL_FColor fcolor =
            modulated ? SDL_FColor{static_cast<float>(color.r) * inv255, static_cast<float>(color.g) * inv255,
                                   static_cast<float>(color.b) * inv255, static_cast<float>(color.a) * inv255}
                      : SDL_FColor{texR, texG, texB, texA};

        float x0, y0, x1, y1, x2, y2, x3, y3;
        if (object->getRotation() == 0.0f)
        {
            const float right = dst.x + dst.w;
            const float bottom = dst.y + dst.h;
            x0 = dst.x;
            y0 = dst.y;
            x1 = right;
            y1 = dst.y;
            x2 = right;
            y2 = bottom;
            x3 = dst.x;
            y3 = bottom;
        }
        else
        {
            const float hx = dst.w * 0.5f;
            const float hy = dst.h * 0.5f;
            const float cx = dst.x + hx;
            const float cy = dst.y + hy;
            const float c = object->getCosRotation();
            const float s = object->getSinRotation();
            const float hc = hx * c;
            const float hs = hx * s;
            const float vc = hy * c;
            const float vs = hy * s;
            x0 = cx - hc + vs;
            y0 = cy - hs - vc;
            x1 = cx + hc + vs;
            y1 = cy + hs - vc;
            x2 = cx + hc - vs;
            y2 = cy + hs + vc;
            x3 = cx - hc - vs;
            y3 = cy - hs + vc;
        }

        vertices.push_back(SDL_Vertex{{x0, y0}, fcolor, {u0, v0}});
        vertices.push_back(SDL_Vertex{{x1, y1}, fcolor, {u1, v0}});
        vertices.push_back(SDL_Vertex{{x2, y2}, fcolor, {u1, v1}});
        vertices.push_back(SDL_Vertex{{x3, y3}, fcolor, {u0, v1}});
    }

    void flush()
    {
        if (vertices.empty() || batchTexture == nullptr)
        {
            return;
        }

        const size_t vertexCount = vertices.size();
        const size_t quadCount = vertexCount / 4;
        const size_t requiredIndices = quadCount * 6;
        if (indices.size() < requiredIndices)
        {
            const size_t startQuad = indices.size() / 6;
            indices.resize(requiredIndices);
            for (size_t q = startQuad; q < quadCount; ++q)
            {
                const int base = static_cast<int>(q * 4);
                const size_t dst = q * 6;
                indices[dst + 0] = base + 0;
                indices[dst + 1] = base + 1;
                indices[dst + 2] = base + 2;
                indices[dst + 3] = base + 0;
                indices[dst + 4] = base + 2;
                indices[dst + 5] = base + 3;
            }
        }

        if (promoteAlphaBlend)
        {
            SDL_SetTextureBlendMode(batchTexture, SDL_BLENDMODE_BLEND);
        }

        SDL_RenderGeometryRaw(renderer, batchTexture,
                              &vertices[0].position.x, static_cast<int>(sizeof(SDL_Vertex)),
                              &vertices[0].color, static_cast<int>(sizeof(SDL_Vertex)),
                              &vertices[0].tex_coord.x, static_cast<int>(sizeof(SDL_Vertex)),
                              static_cast<int>(vertexCount),
                              indices.data(), static_cast<int>(requiredIndices),
                              static_cast<int>(sizeof(int)));

        if (isSoftwareRenderer)
        {
            // SDL3's software RenderGeometryRaw fast path mutates texture color/alpha mod internally.
            SDL_SetTextureColorModFloat(batchTexture, texR, texG, texB);
            SDL_SetTextureAlphaModFloat(batchTexture, texA);
        }
        if (promoteAlphaBlend)
        {
            SDL_SetTextureBlendMode(batchTexture, texBlendMode);
        }

        vertices.clear();
    }

  private:
    SDL_Renderer *renderer;
    bool isSoftwareRenderer;
    std::vector<SDL_Vertex> &vertices;
    std::vector<int> &indices;

    SDL_Texture *batchTexture = nullptr;
    SDL_BlendMode texBlendMode = SDL_BLENDMODE_BLEND;
    float texR = 1.0f;
    float texG = 1.0f;
    float texB = 1.0f;
    float texA = 1.0f;
    bool promoteAlphaBlend = false;
    float texW = 0.0f;
    float texH = 0.0f;
    float invTexW = 0.0f;
    float invTexH = 0.0f;
    bool texSizeValid = false;
};
} // namespace

// ============================================================================
// Visible Selection Culling
// ============================================================================

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
    const auto resolveRecordSprite = [&](RenderCullingState::RecordIndex index,
                                         const RenderCullingState::Record &record) {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingHandleResolutions, 1);
        if (index < spriteComponents.size() && spriteComponents[index]->getHandle() == record.spriteHandle)
            return spriteComponents[index];
        return static_cast<Sprite *>(manager.resolveHandle(record.spriteHandle, ObjectKind::Sprite));
    };

    const auto refreshRecord = [&](RenderCullingState::RecordIndex index) -> Sprite * {
        auto &record = state.recordAt(index);
        Sprite *sprite = resolveRecordSprite(index, record);
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
        if (!spriteBoundsOverlap(record.bounds, query))
            return false;

        Sprite *sprite = hasResolvedSprite ? resolvedSprite : resolveRecordSprite(rank, record);
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

// ============================================================================
// Draw Ordering
// ============================================================================

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
bool SDLWindow::trySingleEntryRepair(SpriteDrawOrder &order, size_t population)
{
    if (order.orderMode != BenchmarkSpriteOrderMode::SingleEntryRepair || !order.orderDirty ||
        !order.singleEditCandidateValid || order.singleEditSlot >= population)
        return false;

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
    if (oldPos >= order.entries.size())
        return false;

    const SpriteDrawEntry updated{targetRank, spriteComponents[targetRank]->getZOrder(),
                                  order.sequenceBySlot[targetRank]};
    size_t moves = 0;
    if (oldPos > 0 && spriteDrawEntryLess(updated, order.entries[oldPos - 1]))
    {
        auto it = std::lower_bound(order.entries.begin(),
                                   order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos), updated,
                                   spriteDrawEntryLess<SpriteDrawEntry>);
        const size_t newPos = static_cast<size_t>(it - order.entries.begin());
        std::move_backward(it, order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos),
                           order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1));
        order.entries[newPos] = updated;
        moves = oldPos - newPos;
    }
    else if (oldPos + 1 < order.entries.size() && spriteDrawEntryLess(order.entries[oldPos + 1], updated))
    {
        auto it = std::lower_bound(order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1),
                                   order.entries.end(), updated, spriteDrawEntryLess<SpriteDrawEntry>);
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
    return true;
}
#endif

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

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    if (order.orderMode != BenchmarkSpriteOrderMode::ForcedFullSort && !order.orderDirty && !membershipChanged)
        return;

    if (!membershipChanged && trySingleEntryRepair(order, population))
        return;
#else
    if (!order.orderDirty && !membershipChanged)
        return;
#endif

    PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderPreparations, 1);
    order.entries.clear();
    const auto appendRank = [&](size_t rank) {
        order.entries.push_back({rank, spriteComponents[rank]->getZOrder(), order.sequenceBySlot[rank]});
    };
    if (useCachedSelection)
    {
        for (const size_t rank : renderCullingState->visibleRanks())
            appendRank(rank);
    }
    else
    {
        for (size_t rank = 0; rank < population; ++rank)
            appendRank(rank);
    }
    PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderEntries, order.entries.size());

    if (order.entries.size() > 1)
    {
        PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderSorts, 1);
        std::sort(order.entries.begin(), order.entries.end(), spriteDrawEntryLess<SpriteDrawEntry>);
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

// ============================================================================
// Sprite Registration & Change Notifications
// ============================================================================

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
        const size_t target = platformator_detail::nextGeometricCapacity(spriteComponents.capacity(), required);
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
        spriteDrawOrder->invalidateForPopulationChange();
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

    const size_t removeIndex = spriteComponent->getWindowIndex();
    const size_t lastIndex = spriteComponents.size() - 1;

    if (spriteDrawOrder)
        spriteDrawOrder->invalidateForPopulationChange();
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

// ============================================================================
// Batched Sprite Submission & Storage Lifecycle
// ============================================================================

void SDLWindow::renderSpritesBatched(Camera *camera, const platformator_detail::SpriteRenderView &view,
                                     bool useCachedSelection)
{
    if (!spriteDrawOrder || spriteDrawOrder->entries.empty())
        return;

    spriteBatchVertices.clear();
    if (const size_t expectedVertices = spriteDrawOrder->entries.size() * 4;
        spriteBatchVertices.capacity() < expectedVertices)
    {
        spriteBatchVertices.reserve(expectedVertices);
    }

    SpriteBatcher batcher(renderer, isSoftwareRenderer, spriteBatchVertices, spriteBatchIndices);

    for (const auto &entry : spriteDrawOrder->entries)
    {
        Sprite *sprite = spriteComponents[entry.rank];
        const GameObject *object = sprite->getGameObject();
        assert(object->getActive());

        SDL_Texture *texture = sprite->getTexture();
        if (texture == nullptr)
        {
            PLATFORMATOR_RENDER_SUBMISSION(sprite);
            continue;
        }

        SDL_FRect dst{};
        float angleDegrees = 0.0f;
        if (useCachedSelection)
        {
            const auto &spritePos = camera->getPublishedSpritePosition(sprite);
            dst = platformator_detail::makeSpriteRenderDestination(spritePos.x(), spritePos.y(), sprite->getWidth(),
                                                                   sprite->getHeight(), view);
        }
        else
        {
            camera->prepareSpriteGeometry(sprite, view, dst, angleDegrees);
            if (view.cullingEligible)
            {
                PLATFORMATOR_RENDER_VISIBILITY_CHECK();
                if (platformator_detail::classifySpriteForRender(dst, angleDegrees, view) ==
                    platformator_detail::SpriteRenderClassification::Reject)
                    continue;
            }
        }

        PLATFORMATOR_RENDER_SUBMISSION(sprite);

        if (!platformator_detail::isSafeSpriteRenderDestination(dst))
            continue;

        const SDL_Color color = sprite->getColor();
        batcher.prepareTextureState(texture, color);

        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (!batcher.resolveUVs(sprite->getSourceRect(), sprite->getFlip(), u0, v0, u1, v1))
            continue;

        batcher.appendQuad(object, dst, color, u0, v0, u1, v1);
    }

    batcher.flush();
}

void SDLWindow::releaseRenderingStorage() noexcept
{
    std::vector<SDL_Vertex>().swap(spriteBatchVertices);
    std::vector<int>().swap(spriteBatchIndices);
    if (renderCullingState)
        renderCullingState->releaseStorage();
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    benchmarkSelectionTiming = {};
#endif
    if (spriteDrawOrder)
        spriteDrawOrder->releaseStorage();
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
