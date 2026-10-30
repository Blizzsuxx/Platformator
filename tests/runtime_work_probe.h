#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

class Sprite;

namespace platformator_test
{
struct SpriteIdentity
{
    size_t slot = 0;
    uint32_t generation = 0;

    bool operator==(const SpriteIdentity &) const = default;
};

// Main-thread reductions only. Instrumented test builds opt in explicitly.
struct WorkCounters
{
    size_t positionReadVisits = 0;
    size_t immediatePositionRefreshVisits = 0;
    size_t colliderSyncRequests = 0;
    size_t bodyParticipationEnqueues = 0;
    size_t hierarchyRebuilds = 0;
    size_t transformVisits = 0;
    size_t reconciliations = 0;
    size_t geometryPairs = 0;
    size_t contactTransitionChecks = 0;
    size_t contactEventRecords = 0;
    size_t solverRefreshPairs = 0;
    size_t forceBodies = 0;
    size_t movingBodies = 0;
    size_t sleepBodies = 0;
    size_t renderViewPreparations = 0;
    size_t renderEligibilityChecks = 0;
    size_t renderVisibilityChecks = 0;
    size_t renderSubmittedSprites = 0;
    size_t renderReadbackCallbacks = 0;
    size_t renderCullingStateCreations = 0;
    size_t renderCullingRecordsAdded = 0;
    size_t renderCullingRecordsRemoved = 0;
    size_t renderCullingRecordsRelocated = 0;
    size_t renderCullingDirtyEnqueues = 0;
    size_t renderCullingDirtyCancellations = 0;
    size_t renderCullingBoundsRefreshes = 0;
    size_t renderCullingIndexInsertions = 0;
    size_t renderCullingIndexRemovals = 0;
    size_t renderCullingIndexRelinks = 0;
    size_t renderCullingQueries = 0;
    size_t renderCullingQueryStampResets = 0;
    size_t renderCullingCellVisits = 0;
    size_t renderCullingCandidateVisits = 0;
    size_t renderCullingFullPopulationVisits = 0;
    size_t renderCullingOrderWork = 0;
    size_t renderCullingCacheHits = 0;
    size_t renderCullingProjectionRebuilds = 0;
    size_t renderCullingNumericFallbacks = 0;
    size_t renderCullingMappingFallbacks = 0;
    size_t renderCullingSideListVisits = 0;
    size_t renderCullingOwnedRecords = 0;
    size_t renderCullingOwnedRecordCapacity = 0;
    size_t renderCullingOwnedCellCount = 0;
    size_t renderCullingOwnedCellCapacity = 0;
    size_t renderCullingOwnedBytes = 0;
};
inline WorkCounters work;

struct RenderObservation
{
    using IdentityResolver = bool (*)(const Sprite *, SpriteIdentity &, void *);
    using BeforePresentCallback = void (*)(SDL_Renderer *, int, int, void *);

    void *identityContext = nullptr;
    IdentityResolver resolveIdentity = nullptr;
    std::vector<SpriteIdentity> *submissions = nullptr;
    size_t unresolvedSprites = 0;
    size_t submissionOverflows = 0;
    void *readbackContext = nullptr;
    BeforePresentCallback beforePresent = nullptr;
};

inline RenderObservation *activeRenderObservation = nullptr;

class ScopedRenderObservation
{
  public:
    explicit ScopedRenderObservation(RenderObservation &observation)
        : previous_(activeRenderObservation)
    {
        activeRenderObservation = &observation;
        if (observation.submissions != nullptr)
        {
            observation.submissions->clear();
        }
        observation.unresolvedSprites = 0;
        observation.submissionOverflows = 0;
    }

    ScopedRenderObservation(const ScopedRenderObservation &) = delete;
    ScopedRenderObservation &operator=(const ScopedRenderObservation &) = delete;

    ~ScopedRenderObservation()
    {
        activeRenderObservation = previous_;
    }

  private:
    RenderObservation *previous_;
};

inline void observeSpriteSubmission(const Sprite *sprite)
{
    ++work.renderSubmittedSprites;
    RenderObservation *observation = activeRenderObservation;
    if (observation == nullptr || observation->submissions == nullptr || observation->resolveIdentity == nullptr)
    {
        return;
    }

    SpriteIdentity identity;
    if (!observation->resolveIdentity(sprite, identity, observation->identityContext))
    {
        ++observation->unresolvedSprites;
        return;
    }

    auto &submissions = *observation->submissions;
    if (submissions.size() == submissions.capacity())
    {
        ++observation->submissionOverflows;
        return;
    }
    submissions.push_back(identity);
}

inline void observeBeforePresent(SDL_Renderer *renderer, int width, int height)
{
    RenderObservation *observation = activeRenderObservation;
    if (observation != nullptr && observation->beforePresent != nullptr)
    {
        ++work.renderReadbackCallbacks;
        observation->beforePresent(renderer, width, height, observation->readbackContext);
    }
}
} // namespace platformator_test

#if PLATFORMATOR_ENABLE_WORK_PROBES
#define PLATFORMATOR_WORK_ADD(field, count) (::platformator_test::work.field += (count))
#define PLATFORMATOR_WORK_SET(field, value) (::platformator_test::work.field = (value))
#define PLATFORMATOR_RENDER_VIEW_PREPARED() (++::platformator_test::work.renderViewPreparations)
#define PLATFORMATOR_RENDER_ELIGIBILITY_CHECK() (++::platformator_test::work.renderEligibilityChecks)
#define PLATFORMATOR_RENDER_VISIBILITY_CHECK() (++::platformator_test::work.renderVisibilityChecks)
#define PLATFORMATOR_RENDER_SUBMISSION(sprite) (::platformator_test::observeSpriteSubmission(sprite))
#define PLATFORMATOR_RENDER_BEFORE_PRESENT(renderer, width, height) \
    (::platformator_test::observeBeforePresent(renderer, width, height))
#define PLATFORMATOR_RENDER_CULLING_ADD(field, count) (::platformator_test::work.field += (count))
#define PLATFORMATOR_RENDER_CULLING_SET(field, value) (::platformator_test::work.field = (value))
#else
#define PLATFORMATOR_WORK_ADD(field, count) ((void)0)
#define PLATFORMATOR_WORK_SET(field, value) ((void)0)
#define PLATFORMATOR_RENDER_VIEW_PREPARED() ((void)0)
#define PLATFORMATOR_RENDER_ELIGIBILITY_CHECK() ((void)0)
#define PLATFORMATOR_RENDER_VISIBILITY_CHECK() ((void)0)
#define PLATFORMATOR_RENDER_SUBMISSION(sprite) ((void)0)
#define PLATFORMATOR_RENDER_BEFORE_PRESENT(renderer, width, height) ((void)0)
#define PLATFORMATOR_RENDER_CULLING_ADD(field, count) ((void)0)
#define PLATFORMATOR_RENDER_CULLING_SET(field, value) ((void)0)
#endif
