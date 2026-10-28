#include "renderculling.h"
#include "gamemanager.h"
#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "sdlwindow.h"
#include "test_support.h"

#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/runtime.h>
#include <platformator/scriptcomponent.h>
#include <platformator/sprite.h>

#include <oneapi/tbb/global_control.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace test_support;
using ::RenderCullingMode;
using platformator_detail::RenderCullingFrameStats;

namespace
{
std::ofstream lifecycleEvidence;

const char *modeName(RenderCullingMode mode)
{
    switch (mode)
    {
    case RenderCullingMode::None: return "none";
    case RenderCullingMode::Linear: return "linear";
    case RenderCullingMode::DynamicBvh: return "bvh";
    case RenderCullingMode::Segmented: return "segmented";
    }
    return "invalid";
}

const char *broadName(BroadPhaseMode mode)
{
    return mode == BroadPhaseMode::DynamicBvh ? "bvh" : "segmented";
}

void record(const char *fixture, RenderCullingMode mode, BroadPhaseMode broadPhase, size_t workers,
            const std::string &checkpoint, const RenderCullingFrameStats &stats)
{
    if (lifecycleEvidence)
        lifecycleEvidence << fixture << ',' << broadName(broadPhase) << ',' << modeName(mode) << ',' << workers << ','
                          << checkpoint << ',' << stats.candidateCount << ',' << stats.visibleCount << ','
                          << (stats.visibilityKnown ? 1 : 0) << ',' << stats.submittedCount << ','
                          << stats.indexProxyCount << ',' << stats.visibilityQueries << ','
                          << stats.maintenanceOperations << ',' << stats.geometryRevision << ',' << stats.viewRevision
                          << ',' << stats.drawOrderRevision << ',' << stats.outputOrderHash << ','
                          << (stats.resultCacheHit ? 1 : 0) << ',' << stats.fallbackReason << '\n';
}

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

RenderCullingFrameStats render(platformator::Runtime &runtime)
{
    require(runtime.simulateAndRenderFrame(dt), "Lifecycle fixture renders a frame");
    return platformator_detail::RuntimeAccess::gameManager().getWindow()->getRenderCullingStats();
}

RuntimeOptions optionsFor(RenderCullingMode mode, BroadPhaseMode broadPhase)
{
    RuntimeOptions options;
    options.windowSettings.width = 160;
    options.windowSettings.height = 120;
    options.spatialSettings.renderCulling = mode;
    options.spatialSettings.broadPhase = broadPhase;
    return options;
}

class LateParentMutation final : public test_support::Behavior
{
  public:
    void lateUpdate(double) override
    {
        if (!moved)
        {
            getGameObject()->setPosition({24.0f, 20.0f});
            moved = true;
        }
    }

  private:
    bool moved = false;
};

void parentCameraAndLifetimeTransitions(RenderCullingMode mode, BroadPhaseMode broadPhase, size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(optionsFor(mode, broadPhase));
    require(runtime.getRenderer() != nullptr, "Lifecycle fixture uses a real SDL software renderer");
    auto *cameraObject = runtime.createGameObject()->setPosition({0, 0});
    cameraObject->addComponent<Camera>(160.0f, 120.0f);
    auto *camera = cameraObject->getComponent<Camera>();
    auto *parent = runtime.createGameObject()->setPosition({20, 20});
    auto *child = runtime.createGameObject()->setPosition({64, 48});
    parent->addChild(child);
    child->addComponent<Sprite>();
    auto *sprite = child->getComponent<Sprite>();
    sprite->setWidth(16.0f);
    sprite->setHeight(16.0f);
    auto *outside = runtime.createGameObject()->setPosition({900, 48});
    outside->addComponent<Sprite>();
    outside->getComponent<Sprite>()->setWidth(16.0f);
    outside->getComponent<Sprite>()->setHeight(16.0f);

    const auto initial = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "initial", initial);
    require(initial.submittedCount == (mode == RenderCullingMode::None ? 2 : 1) && initial.fallbackReason.empty(),
            "Initial view preserves the direct none path or submits only its visible sprite");
    if (mode != RenderCullingMode::None)
    {
        require(initial.boundsPrepared == 2 && initial.boundsPrepareNanoseconds > 0 &&
                    initial.indexProxyCount == 2 && initial.visibilityQueries == 1 &&
                    initial.indexQueryNanoseconds > 0 && initial.resultReduceNanoseconds > 0,
                "Live visibility records actual bounds, query, reduction and valid-record gauges");
        if (mode == RenderCullingMode::DynamicBvh)
            require(initial.rebuildCalls == 1 && initial.treeNodes == 3,
                    "Live BVH reports its actual initial rebuild and node population");
        const size_t owned = platformator_detail::RuntimeAccess::window()->getRenderCullingOwnedCapacityBytes();
        require(owned > 2 * sizeof(platformator_detail::RenderBounds), "Visibility capacity includes owned record and reusable index payload");
        const auto cached = render(runtime);
        require(cached.resultCacheHit && cached.boundsPrepared == 0 && cached.visibilityQueries == 0 &&
                    cached.indexProxyCount == initial.indexProxyCount && cached.treeNodes == initial.treeNodes,
                "Live cache hits skip work while retaining end-of-frame index gauges");
    }
    const auto initialGeometryRevision = initial.geometryRevision;
    const auto initialViewRevision = initial.viewRevision;
    const auto initialOrderRevision = initial.drawOrderRevision;

    parent->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(new LateParentMutation());
    const auto lateMoved = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "late-update", lateMoved);
    require(lateMoved.geometryRevision > initialGeometryRevision &&
                lateMoved.submittedCount == (mode == RenderCullingMode::None ? 2 : 1) &&
                std::abs(parent->getPosition().x() - 24.0f) < 0.001f,
            "LateUpdate transform mutation publishes current sprite geometry before this frame is submitted");
    if (mode != RenderCullingMode::None)
        require(lateMoved.boundsPrepared > 0 && lateMoved.boundsPrepareNanoseconds > 0 &&
                    lateMoved.indexUpdateNanoseconds > 0 && lateMoved.visibilityQueries == 1,
                "Live moving geometry records the phases it executes");
    parent->removeComponent(SCRIPT);

    runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double) {
        parent->setPosition({28, 24});
        child->setPosition({80, 48});
        auto *newParent = runtime.createGameObject()->setPosition({-20, -20});
        newParent->addChild(child);
        cameraObject->setPosition({12, 8});
    });
    const auto moved = platformator_detail::RuntimeAccess::gameManager().getWindow()->getRenderCullingStats();
    record("lifecycle", mode, broadPhase, workers, "reparent-camera-pan", moved);
    require(moved.submittedCount == (mode == RenderCullingMode::None ? 2 : 1) &&
                moved.geometryRevision > initialGeometryRevision &&
                moved.viewRevision > initialViewRevision,
            "Parent/reparent and late mutations publish current sprite and camera geometry before submission");
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().empty(),
            "Visibility membership and render queries create no gameplay contact pairs");

    camera->setWidth(100.0f);
    camera->setHeight(80.0f);
    const auto resized = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "camera-aspect", resized);
    require(resized.viewRevision > moved.viewRevision && resized.submittedCount <= 2,
            "Camera aspect changes invalidate view-dependent visibility extents");
    cameraObject->setPosition({600.0f, 500.0f});
    const auto jumped = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "camera-jump", jumped);
    require(jumped.viewRevision > resized.viewRevision &&
                jumped.submittedCount == (mode == RenderCullingMode::None ? 2 : 0),
            "A camera jump removes formerly visible sprites using the current view bounds");
    cameraObject->setPosition({0.0f, 0.0f});
    const auto returned = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "camera-return", returned);
    require(returned.viewRevision > jumped.viewRevision &&
                returned.submittedCount == (mode == RenderCullingMode::None ? 2 : 1),
            "Returning the camera restores current visibility without stale query results");

    child->setActive(false);
    const auto deactivated = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "deactivated", deactivated);
    require(deactivated.submittedCount == (mode == RenderCullingMode::None ? 1 : 0) &&
                deactivated.drawOrderRevision > returned.drawOrderRevision,
            "Deactivation removes a sprite from current draw order");
    child->setActive(true);
    const auto reactivated = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "reactivated", reactivated);
    require(reactivated.submittedCount == (mode == RenderCullingMode::None ? 2 : 1) &&
                reactivated.drawOrderRevision > deactivated.drawOrderRevision,
            "Reactivation re-registers the sprite and invalidates order state");

    child->removeComponent(SPRITE);
    const auto replaced = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "sprite-removed", replaced);
    require(replaced.submittedCount == (mode == RenderCullingMode::None ? 1 : 0),
            "Sprite component replacement retires its old visibility proxy before reclamation");
    child->addComponent<Sprite>();
    auto *replacement = child->getComponent<Sprite>();
    replacement->setWidth(20.0f);
    replacement->setHeight(12.0f);
    const auto restored = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "sprite-replaced", restored);
    require(restored.submittedCount == (mode == RenderCullingMode::None ? 2 : 1),
            "Replacement sprite publishes its current destination geometry");

    child->destroy();
    const auto destroyed = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "game-object-destroyed", destroyed);
    require(destroyed.submittedCount == (mode == RenderCullingMode::None ? 1 : 0) && destroyed.fallbackReason.empty(),
            "Destroyed sprites disappear without a stale proxy or forced submit-all path");

    cameraObject->removeComponent(CAMERA);
    const auto noCamera = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "no-camera", noCamera);
    require(noCamera.submittedCount == 0 && noCamera.fallbackReason == "no-camera" &&
                noCamera.visibilityQueries == 0 && noCamera.maintenanceOperations == 0,
            "No-camera frames perform no visibility maintenance or query");
    auto *replacementCameraObject = runtime.createGameObject()->setPosition({0, 0});
    replacementCameraObject->addComponent<Camera>(160.0f, 120.0f);
    const auto cameraRestored = render(runtime);
    record("lifecycle", mode, broadPhase, workers, "camera-replaced", cameraRestored);
    require(cameraRestored.fallbackReason.empty() &&
                cameraRestored.submittedCount == (mode == RenderCullingMode::None ? 1 : 0),
            "Camera replacement resumes culling with current view state");
    (void)outside;
}

void zeroStepPausedAndHeadless(RenderCullingMode mode, BroadPhaseMode broadPhase)
{
    RuntimeOptions options = optionsFor(mode, broadPhase);
    options.debugSettings.startPaused = true;
    {
    platformator::Runtime runtime(options);
    auto *cameraObject = runtime.createGameObject();
    cameraObject->addComponent<Camera>(160.0f, 120.0f);
    auto *camera = cameraObject->getComponent<Camera>();
    auto *visible = runtime.createGameObject()->setPosition({64, 48});
    visible->addComponent<Sprite>();
    auto *sprite = visible->getComponent<Sprite>();
    sprite->setWidth(24);
    sprite->setHeight(24);
    (void)camera;

    auto initial = render(runtime);
    record("paused", mode, broadPhase, 1, "initial", initial);
    require(initial.submittedCount == 1, "Paused render submits current initial visibility");
    visible->setPosition({600, 48});
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    require(runtime.simulateAndRenderFrame(0.0), "Zero-step paused frame still renders");
#else
    require(runtime.simulateAndRenderFrame(dt), "Current frame renders without debug pause support");
#endif
    const auto moved = platformator_detail::RuntimeAccess::gameManager().getWindow()->getRenderCullingStats();
    record("paused", mode, broadPhase, 1, "zero-step-or-current", moved);
    require(moved.submittedCount == (mode == RenderCullingMode::None ? 1 : 0) &&
                moved.geometryRevision > initial.geometryRevision,
            "Paused zero-step rendering publishes queued geometry before querying");
    visible->setPosition({64, 48});
    const auto resumed = render(runtime);
    record("paused", mode, broadPhase, 1, "resume", resumed);
    require(resumed.submittedCount == 1, "Rendering resumes with no stale visibility result");
    }

    RuntimeOptions headlessOptions = options;
    headlessOptions.windowSettings.headless = true;
    headlessOptions.debugSettings.startPaused = false;
    platformator::Runtime headless(headlessOptions);
    headless.createGameObject()->addComponent<Camera>(160.0f, 120.0f);
    for (int i = 0; i < 64; ++i)
        headless.createGameObject()->setPosition({static_cast<float>(i * 400), 0})->addComponent<Sprite>();
    headless.simulateFrame(dt);
    const auto headlessStats = platformator_detail::RuntimeAccess::gameManager().getWindow()->getRenderCullingStats();
    record("headless", mode, broadPhase, 1, "selected-no-renderer", headlessStats);
    require(headless.getRenderer() == nullptr && headlessStats.visibilityQueries == 0 &&
                headlessStats.maintenanceOperations == 0,
            "Headless selected render modes own no active visibility work");
}

void unsupportedSdlStateFallsBack(RenderCullingMode mode, BroadPhaseMode broadPhase)
{
    platformator::Runtime runtime(optionsFor(mode, broadPhase));
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "SDL-state fallback fixture owns a renderer");
    auto *cameraObject = runtime.createGameObject();
    cameraObject->addComponent<Camera>(160.0f, 120.0f);
    auto *visible = runtime.createGameObject()->setPosition({64, 48});
    visible->addComponent<Sprite>();
    visible->getComponent<Sprite>()->setWidth(16.0f);
    visible->getComponent<Sprite>()->setHeight(16.0f);
    auto *offscreen = runtime.createGameObject()->setPosition({900, 48});
    offscreen->addComponent<Sprite>();
    offscreen->getComponent<Sprite>()->setWidth(16.0f);
    offscreen->getComponent<Sprite>()->setHeight(16.0f);

    require(SDL_SetRenderScale(renderer, 2.0f, 2.0f), "Fixture sets a non-default SDL scale");
    const auto scaled = render(runtime);
    record("sdl-fallback", mode, broadPhase, 1, "scale", scaled);
    require(scaled.submittedCount == 2 && !scaled.fallbackReason.empty(),
            "Unsupported SDL scale reports a reason and conservatively submits all sprites");
    require(SDL_SetRenderScale(renderer, 1.0f, 1.0f), "Fixture restores default SDL scale");
    const auto restoredScale = render(runtime);
    record("sdl-fallback", mode, broadPhase, 1, "scale-restored", restoredScale);
    require(restoredScale.submittedCount == 1 && restoredScale.fallbackReason.empty(),
            "Restoring supported SDL state resumes indexed visibility");

    const SDL_Rect viewport{0, 0, 80, 60};
    require(SDL_SetRenderViewport(renderer, &viewport), "Fixture sets a custom SDL viewport");
    const auto customViewport = render(runtime);
    record("sdl-fallback", mode, broadPhase, 1, "viewport", customViewport);
    require(customViewport.submittedCount == 2 && !customViewport.fallbackReason.empty(),
            "Custom viewport conservatively bypasses culling with a recorded reason");
    require(SDL_SetRenderViewport(renderer, nullptr), "Fixture restores the default SDL viewport");
    const auto restoredViewport = render(runtime);
    record("sdl-fallback", mode, broadPhase, 1, "viewport-restored", restoredViewport);
    require(restoredViewport.submittedCount == 1 && restoredViewport.fallbackReason.empty(),
            "Viewport restoration reconciles pending visibility before culling resumes (submitted=" +
                std::to_string(restoredViewport.submittedCount) + ", reason=" + restoredViewport.fallbackReason +
                ", mode=" + std::to_string(static_cast<int>(mode)) + ", candidates=" +
                std::to_string(restoredViewport.candidateCount) + ", proxies=" +
                std::to_string(restoredViewport.indexProxyCount) + ")");

    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 160, 120);
    require(target != nullptr, "Fixture creates an SDL target texture");
    require(SDL_SetRenderTarget(renderer, target), "Fixture selects an external SDL render target");
    const auto customTarget = render(runtime);
    record("sdl-fallback", mode, broadPhase, 1, "target", customTarget);
    require(customTarget.submittedCount == 2 && !customTarget.fallbackReason.empty(),
            "External target rendering bypasses culling conservatively");
    require(SDL_SetRenderTarget(renderer, nullptr), "Fixture restores the window render target");
    SDL_DestroyTexture(target);

    require(SDL_SetRenderLogicalPresentation(renderer, 80, 60, SDL_LOGICAL_PRESENTATION_LETTERBOX),
            "Fixture sets custom logical presentation");
    const auto customLogical = render(runtime);
    record("sdl-fallback", mode, broadPhase, 1, "logical-presentation", customLogical);
    require(customLogical.submittedCount == 2 && !customLogical.fallbackReason.empty(),
            "Custom logical presentation bypasses culling with a reason");
    require(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED),
            "Fixture restores default logical presentation");
    const auto restoredLogical = render(runtime);
    record("sdl-fallback", mode, broadPhase, 1, "logical-presentation-restored", restoredLogical);
    require(restoredLogical.submittedCount == 1 && restoredLogical.fallbackReason.empty(),
            "Logical-presentation restoration reconciles and resumes culling");
    (void)offscreen;
}
} // namespace

int main()
{
    headlessEnvironment();
    if (const char *path = std::getenv("PLATFORMATOR_RENDER_LIFECYCLE_EVIDENCE"); path && *path)
    {
        const std::filesystem::path file(path);
        std::filesystem::create_directories(file.parent_path());
        lifecycleEvidence.open(file);
        lifecycleEvidence << "fixture,physics,render,workers,checkpoint,candidates,visible,visibility_known,submitted,proxies,queries,maintenance,"
                             "geometry_revision,view_revision,draw_order_revision,output_order_hash,cache_hit,fallback\n";
    }
    try
    {
        for (const auto broadPhase : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
            for (const auto mode : {RenderCullingMode::None, RenderCullingMode::Linear,
                                    RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented})
            {
                parentCameraAndLifetimeTransitions(mode, broadPhase, 1);
                parentCameraAndLifetimeTransitions(mode, broadPhase, 6);
                zeroStepPausedAndHeadless(mode, broadPhase);
                if (mode != RenderCullingMode::None)
                    unsupportedSdlStateFallsBack(mode, broadPhase);
            }
        std::cout << "Render culling lifecycle regressions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
