#include "render_culling_fixture.h"

#include <SDL3/SDL_surface.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

#include "core/runtimeaccess.h"
#include "rendering/sdlwindow.h"
#include "test_support.h"

namespace
{
std::atomic<bool> gCountAllocations{false};
std::atomic<size_t> gObservedAllocations{0};
} // namespace

void *operator new(std::size_t bytes)
{
    if (gCountAllocations.load(std::memory_order_relaxed))
        gObservedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void *memory = std::malloc(bytes == 0 ? 1 : bytes))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
class ScopedAllocationCounter
{
  public:
    ScopedAllocationCounter()
    {
        gObservedAllocations.store(0, std::memory_order_relaxed);
        gCountAllocations.store(true, std::memory_order_release);
    }

    ~ScopedAllocationCounter() { gCountAllocations.store(false, std::memory_order_release); }

    size_t allocations() const { return gObservedAllocations.load(std::memory_order_acquire); }
};
} // namespace

namespace
{
using platformator_test::SpriteIdentity;
using render_culling_test::FixtureTexture;
using render_culling_test::RenderCullingFixture;

constexpr int kOutputWidth = 320;
constexpr int kOutputHeight = 240;

class ScopedRenderCullingMode
{
  public:
    explicit ScopedRenderCullingMode(const char *mode)
    {
        if (const char *previous = std::getenv("PLATFORMATOR_RENDER_CULLING_MODE"))
        {
            hadPrevious_ = true;
            previous_ = previous;
        }
        if (mode == nullptr)
        {
            test_support::clearEnvironment("PLATFORMATOR_RENDER_CULLING_MODE");
        }
        else if (!test_support::setEnvironment("PLATFORMATOR_RENDER_CULLING_MODE", mode))
        {
            throw std::runtime_error("Could not set PLATFORMATOR_RENDER_CULLING_MODE");
        }
    }

    ~ScopedRenderCullingMode()
    {
        if (hadPrevious_)
            test_support::setEnvironment("PLATFORMATOR_RENDER_CULLING_MODE", previous_.c_str());
        else
            test_support::clearEnvironment("PLATFORMATOR_RENDER_CULLING_MODE");
    }

  private:
    bool hadPrevious_ = false;
    std::string previous_;
};

struct CapturedFrame
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    std::vector<SpriteIdentity> submissions;
    platformator_test::WorkCounters work;
    std::string readbackError;

    static void beforePresent(SDL_Renderer *renderer, int outputWidth, int outputHeight, void *context)
    {
        auto *capture = static_cast<CapturedFrame *>(context);
        if (capture == nullptr)
            return;

        capture->width = outputWidth;
        capture->height = outputHeight;
        const SDL_Rect rect{0, 0, outputWidth, outputHeight};
        SDL_Surface *raw = SDL_RenderReadPixels(renderer, &rect);
        if (raw == nullptr)
        {
            capture->readbackError = std::string("SDL_RenderReadPixels failed: ") + SDL_GetError();
            return;
        }

        SDL_Surface *converted = raw;
        if (raw->format != SDL_PIXELFORMAT_RGBA32)
        {
            converted = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(raw);
            if (converted == nullptr)
            {
                capture->readbackError = std::string("SDL_ConvertSurface failed: ") + SDL_GetError();
                return;
            }
        }

        if (capture->rgba.size() != static_cast<size_t>(outputWidth) * outputHeight * 4)
        {
            capture->readbackError = "Readback destination buffer was not sized before render";
            SDL_DestroySurface(converted);
            return;
        }

        if (SDL_MUSTLOCK(converted) && !SDL_LockSurface(converted))
        {
            capture->readbackError = std::string("SDL_LockSurface failed: ") + SDL_GetError();
            SDL_DestroySurface(converted);
            return;
        }

        const auto *pixels = static_cast<const uint8_t *>(converted->pixels);
        for (int row = 0; row < converted->h; ++row)
        {
            std::memcpy(capture->rgba.data() + static_cast<size_t>(row) * outputWidth * 4,
                        pixels + static_cast<size_t>(row) * converted->pitch,
                        static_cast<size_t>(converted->w) * 4);
        }

        if (SDL_MUSTLOCK(converted))
            SDL_UnlockSurface(converted);
        SDL_DestroySurface(converted);
    }
};

void captureIntoFrame(RenderCullingFixture &fixture, CapturedFrame &frame)
{
    frame.readbackError.clear();
    frame.submissions.clear();
    frame.rgba.resize(static_cast<size_t>(kOutputWidth) * kOutputHeight * 4);

    platformator_test::RenderObservation observation;
    observation.identityContext = &fixture;
    observation.resolveIdentity = &RenderCullingFixture::resolveIdentityCallback;
    observation.submissions = &frame.submissions;
    observation.readbackContext = &frame;
    observation.beforePresent = &CapturedFrame::beforePresent;

    {
        platformator_test::ScopedRenderObservation scope(observation);
        platformator_test::work = {};
        const bool ok = fixture.runtime().simulateAndRenderFrame(test_support::dt);
        test_support::require(ok, "Sprite ordering fixture quit unexpectedly during frame capture");
        frame.work = platformator_test::work;
    }

    test_support::require(frame.readbackError.empty(), frame.readbackError);
    test_support::require(observation.unresolvedSprites == 0 && observation.submissionOverflows == 0,
                          "Sprite ordering observation had unresolved=" +
                              std::to_string(observation.unresolvedSprites) + " overflow=" +
                              std::to_string(observation.submissionOverflows) + " cap=" +
                              std::to_string(frame.submissions.capacity()));
}

CapturedFrame captureRenderedFrame(RenderCullingFixture &fixture, size_t submissionCapacity)
{
    CapturedFrame frame;
    frame.submissions.reserve(submissionCapacity);
    captureIntoFrame(fixture, frame);
    return frame;
}

std::array<uint8_t, 4> pixelAt(const CapturedFrame &frame, int x, int y)
{
    test_support::require(x >= 0 && x < frame.width && y >= 0 && y < frame.height,
                          "Requested pixel coordinate is out of bounds");
    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)) * 4;
    return {frame.rgba[offset + 0], frame.rgba[offset + 1], frame.rgba[offset + 2], frame.rgba[offset + 3]};
}

void checkInitialOverlappingRegistrationOrder()
{
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(kOutputWidth, kOutputHeight, 320.0f, 240.0f, true);
    fixture.reserveSprites(2);
    // Register black square first (slot 0), then semi-transparent red circle second (slot 1).
    fixture.addSprite(0, 0, 160.0f, 120.0f, 40.0f, 40.0f, FixtureTexture::BlackSquare);
    fixture.addSprite(1, 0, 160.0f, 120.0f, 40.0f, 40.0f, FixtureTexture::RedCircle);

    SDL_Texture *redTex = fixture.texture(FixtureTexture::RedCircle)->getTexture();
    test_support::require(redTex != nullptr, "Missing red circle texture");
    test_support::require(SDL_SetTextureBlendMode(redTex, SDL_BLENDMODE_BLEND), "Could not set blend mode");
    test_support::require(SDL_SetTextureAlphaMod(redTex, 128), "Could not set alpha mod");

    const CapturedFrame frame = captureRenderedFrame(fixture, 2);
    const std::vector<SpriteIdentity> expectedOrder = {{0, 0}, {1, 0}};
    test_support::require(frame.submissions == expectedOrder,
                          "Initial equal-depth overlapping pair did not draw in registration order");

    const auto bg = pixelAt(frame, 10, 10);
    test_support::require(bg == (std::array<uint8_t, 4>{255, 255, 255, 255}),
                          "Background pixel did not match expected clear color");

    const auto corner = pixelAt(frame, 142, 102);
    test_support::require(corner == (std::array<uint8_t, 4>{0, 0, 0, 255}),
                          "Overlapping corner did not expose the earlier opaque black square through alpha=0");

    const auto center = pixelAt(frame, 160, 120);
    test_support::require(center[0] >= 127 && center[0] <= 129 && center[1] == 0 && center[2] == 0 &&
                              center[3] == 255,
                          "Blended center pixel did not match 50% red over black compositing");
}

void checkOrderingAcrossModes(const char *modeName, const char *envMode, bool enableCulling)
{
    ScopedRenderCullingMode modeScope(envMode);
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(kOutputWidth, kOutputHeight, 320.0f, 240.0f, enableCulling);
    fixture.reserveSprites(6);

    // 0: BlackSquare, 1: RedCircle, 2: BlackSquare, 3: RedCircle, 4: BlackSquare, 5: RedCircle
    // Alternating textures verify equal-Z registration order is never reordered to batch textures.
    fixture.addSprite(0, 0, 160.0f, 120.0f, 40.0f, 40.0f, FixtureTexture::BlackSquare);
    fixture.addSprite(1, 0, 160.0f, 120.0f, 40.0f, 40.0f, FixtureTexture::RedCircle);
    fixture.addSprite(2, 0, 160.0f, 120.0f, 40.0f, 40.0f, FixtureTexture::BlackSquare);
    fixture.addSprite(3, 0, 160.0f, 120.0f, 40.0f, 40.0f, FixtureTexture::RedCircle);
    fixture.addSprite(4, 0, 80.0f, 60.0f, 20.0f, 20.0f, FixtureTexture::BlackSquare);
    fixture.addSprite(5, 0, 80.0f, 60.0f, 20.0f, 20.0f, FixtureTexture::RedCircle);

    // Default value is 0 on all constructors.
    for (size_t slot = 0; slot < 6; ++slot)
    {
        test_support::require(fixture.record(slot).sprite->getZOrder() == 0,
                              std::string(modeName) + ": default zOrder must be 0");
    }

    // Negative/positive Z, signed 32-bit extremes, and equal-Z ties across distinct textures.
    constexpr int32_t kMinZ = std::numeric_limits<int32_t>::min();
    constexpr int32_t kMaxZ = std::numeric_limits<int32_t>::max();
    fixture.record(0).sprite->setZOrder(10);
    fixture.record(1).sprite->setZOrder(-1);
    fixture.record(2).sprite->setZOrder(kMaxZ);
    fixture.record(3).sprite->setZOrder(kMinZ);
    fixture.record(4).sprite->setZOrder(0);
    fixture.record(5).sprite->setZOrder(0);

    // Expected: 3 (min), 1 (-1), 4 (0, reg 4), 5 (0, reg 5), 0 (10), 2 (max).
    const CapturedFrame frame1 = captureRenderedFrame(fixture, 6);
    const std::vector<SpriteIdentity> expected1 = {{3, 0}, {1, 0}, {4, 0}, {5, 0}, {0, 0}, {2, 0}};
    test_support::require(frame1.submissions == expected1,
                          std::string(modeName) + ": mixed negative/positive/extreme Z order mismatch");
    test_support::require(frame1.work.renderCullingNumericRankSorts == 0,
                          std::string(modeName) + ": numeric rank sorts must be zero");
    test_support::require(frame1.work.spriteOrderPreparations == 1 && frame1.work.spriteOrderSorts == 1,
                          std::string(modeName) + ": first prepared frame must sort draw entries once");
    // Topmost at (160,120) is slot 2 (BlackSquare, maxZ) -> black (0,0,0,255).
    test_support::require(pixelAt(frame1, 160, 120) == (std::array<uint8_t, 4>{0, 0, 0, 255}),
                          std::string(modeName) + ": highest Z black square should occlude lower red circles");
    // Topmost at (80,60) is slot 5 (RedCircle, Z=0 over slot 4 BlackSquare Z=0) -> red (255,0,0,255).
    test_support::require(pixelAt(frame1, 80, 60) == (std::array<uint8_t, 4>{255, 0, 0, 255}),
                          std::string(modeName) + ": tied Z=0 red circle registered after black square should draw on top");

    // Identical assignments must cause zero ordering invalidation or preparation.
    fixture.record(0).sprite->setZOrder(10);
    fixture.record(2).sprite->setZOrder(kMaxZ);
    fixture.record(3).sprite->setZOrder(kMinZ);
    const CapturedFrame unchanged = captureRenderedFrame(fixture, 6);
    test_support::require(unchanged.submissions == expected1,
                          std::string(modeName) + ": identical assignments altered order");
    test_support::require(unchanged.work.spriteOrderPreparations == 0 && unchanged.work.spriteOrderSorts == 0 &&
                              unchanged.work.spriteOrderComparisons == 0 &&
                              unchanged.work.renderCullingBoundsRefreshes == 0,
                          std::string(modeName) + ": identical assignments triggered unnecessary work");

    // Several edits before preparation must coalesce to final values and return to original tie order.
    fixture.record(0).sprite->setZOrder(50);
    fixture.record(0).sprite->setZOrder(-50);
    fixture.record(0).sprite->setZOrder(0);
    fixture.record(1).sprite->setZOrder(99);
    fixture.record(1).sprite->setZOrder(0);
    fixture.record(2).sprite->setZOrder(0);
    fixture.record(3).sprite->setZOrder(0);
    fixture.record(4).sprite->setZOrder(-2);
    fixture.record(5).sprite->setZOrder(2);

    // Expected: 4 (-2), 0 (0,reg 0), 1 (0,reg 1), 2 (0,reg 2), 3 (0,reg 3), 5 (2).
    const CapturedFrame frame2 = captureRenderedFrame(fixture, 6);
    const std::vector<SpriteIdentity> expected2 = {{4, 0}, {0, 0}, {1, 0}, {2, 0}, {3, 0}, {5, 0}};
    test_support::require(frame2.submissions == expected2,
                          std::string(modeName) + ": coalesced edits and tie restoration mismatch");
    test_support::require(frame2.work.spriteOrderPreparations == 1 && frame2.work.spriteOrderSorts == 1,
                          std::string(modeName) + ": multiple edits before frame must prepare/sort once");
    // Topmost at (160,120) is now slot 3 (RedCircle) -> red (255,0,0,255).
    test_support::require(pixelAt(frame2, 160, 120) == (std::array<uint8_t, 4>{255, 0, 0, 255}),
                          std::string(modeName) + ": restored tie should put slot 3 red circle on top");

    // Custom SDL mapping fallback must preserve Z order and resume configured policy cleanly.
    if (enableCulling)
    {
        SDL_Renderer *renderer = fixture.runtime().getRenderer();
        test_support::require(SDL_SetRenderScale(renderer, 1.25f, 1.0f), "Could not set custom render scale");
        fixture.record(3).sprite->setZOrder(-10);
        const CapturedFrame fallbackFrame = captureRenderedFrame(fixture, 6);
        const std::vector<SpriteIdentity> expectedFallback = {{3, 0}, {4, 0}, {0, 0}, {1, 0}, {2, 0}, {5, 0}};
        test_support::require(fallbackFrame.submissions == expectedFallback,
                              std::string(modeName) + ": custom SDL mapping fallback broke Z order");
        test_support::require(SDL_SetRenderScale(renderer, 1.0f, 1.0f), "Could not restore render scale");

        // Restore scale and verify configured mode resumes with latest Z order.
        fixture.record(2).sprite->setZOrder(-20);
        const CapturedFrame resumedFrame = captureRenderedFrame(fixture, 6);
        const std::vector<SpriteIdentity> expectedResumed = {{2, 0}, {3, 0}, {4, 0}, {0, 0}, {1, 0}, {5, 0}};
        test_support::require(resumedFrame.submissions == expectedResumed,
                              std::string(modeName) + ": resumed frame after fallback broke Z order");
    }
}

void checkLifecycleStabilityAndChurn(const char *envMode)
{
    ScopedRenderCullingMode modeScope(envMode);
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(kOutputWidth, kOutputHeight, 320.0f, 240.0f, true);
    fixture.reserveSprites(16);

    // 1. Register A(0), B(1), C(2), D(3) at equal Z=0.
    for (size_t i = 0; i < 4; ++i)
        fixture.addSprite(i, 0, 160.0f, 120.0f, 20.0f, 20.0f,
                          (i % 2 == 0) ? FixtureTexture::BlackSquare : FixtureTexture::RedCircle);

    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{0, 0}, {1, 0}, {2, 0}, {3, 0}}),
                          "Initial equal-Z sequence must be A, B, C, D");

    // 2. Remove B(1) so D(3) moves into slot 1 in dense storage. Survivors must stay A, C, D.
    fixture.removeSprite(1);
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{0, 0}, {2, 0}, {3, 0}}),
                          "Removing B must keep surviving ties A, C, D in registration order");

    // 3. Real unregister and re-register of C(2) -> A, D, C(gen 1).
    fixture.replaceSprite(2, 1, 160.0f, 120.0f, 20.0f, 20.0f, FixtureTexture::BlackSquare);
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{0, 0}, {3, 0}, {2, 1}}),
                          "Re-registering C must renew its precedence to A, D, C'");

    // 4. Visibility loss/recovery and texture loss/recovery preserve surviving ties.
    fixture.record(0).object->setPosition({-900.0f, -900.0f});
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{3, 0}, {2, 1}}),
                          "Moving A offscreen must omit A without altering D, C'");
    fixture.record(0).object->setPosition({160.0f, 120.0f});
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{0, 0}, {3, 0}, {2, 1}}),
                          "Returning A to view must restore A, D, C'");

    fixture.record(3).sprite->setTextureWrapper(nullptr);
    const auto withoutTex = captureRenderedFrame(fixture, 4);
    test_support::require(withoutTex.submissions == (std::vector<SpriteIdentity>{{0, 0}, {3, 0}, {2, 1}}) ||
                              withoutTex.submissions == (std::vector<SpriteIdentity>{{0, 0}, {2, 1}}),
                          "Texture loss frame must keep surviving registration order");
    fixture.record(3).sprite->setTextureWrapper(fixture.texture(FixtureTexture::RedCircle));
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{0, 0}, {3, 0}, {2, 1}}),
                          "Restoring D's texture must preserve A, D, C'");

    // 5. Coalesced activation toggle inside a step preserves precedence; real deactivation/reactivation renews it.
    {
        CapturedFrame coalescedFrame;
        coalescedFrame.rgba.resize(static_cast<size_t>(kOutputWidth) * kOutputHeight * 4);
        coalescedFrame.submissions.reserve(4);
        platformator_test::RenderObservation obs;
        obs.identityContext = &fixture;
        obs.resolveIdentity = &RenderCullingFixture::resolveIdentityCallback;
        obs.submissions = &coalescedFrame.submissions;
        platformator_test::ScopedRenderObservation scope(obs);
        test_support::require(
            fixture.runtime().simulateAndRenderFrameWithCustomCallback(
                test_support::dt,
                [&](double) {
                    fixture.record(0).object->setActive(false);
                    fixture.record(0).object->setActive(true);
                }),
            "Coalesced activation toggle frame failed");
        test_support::require(coalescedFrame.submissions ==
                                  (std::vector<SpriteIdentity>{{0, 0}, {3, 0}, {2, 1}}),
                              "Coalesced activation toggle before reconciliation must preserve A's precedence");
    }

    fixture.record(0).object->setActive(false);
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{3, 0}, {2, 1}}),
                          "Real deactivation must remove A");
    fixture.record(0).object->setActive(true);
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{3, 0}, {2, 1}, {0, 0}}),
                          "Real reactivation must renew A's precedence to the end");

    // 6. Replacement in a reused slot (D at slot 3 -> gen 1) and missing-camera pending edit consumption.
    fixture.replaceSprite(3, 1, 160.0f, 120.0f, 20.0f, 20.0f, FixtureTexture::RedCircle);
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{2, 1}, {0, 0}, {3, 1}}),
                          "Slot replacement must draw new identity once at the end");

    fixture.removeCamera();
    fixture.record(3).sprite->setZOrder(-5);
    const auto noCameraFrame = captureRenderedFrame(fixture, 4);
    test_support::require(noCameraFrame.submissions.empty(), "Missing camera frame must submit nothing");
    fixture.replaceCamera(320.0f, 240.0f, 0.0f, 0.0f);
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{3, 1}, {2, 1}, {0, 0}}),
                          "Returning from missing camera must consume pending Z edit on next drawn frame");

    // 7. Failed scene load keeps live scene and ordering intact.
    const auto tempDir = std::filesystem::temp_directory_path() / "platformator_sprite_order_us3";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir);
    const auto badScene = tempDir / "bad.scene";
    {
        std::ofstream out(badScene);
        out << "[{\"id\": 0, \"name\": \"Bad\", \"tag\": \"\", \"active\": true, "
               "\"position\": {\"x\": 0, \"y\": 0}, \"scale\": {\"x\": 1, \"y\": 1}, \"rotation\": 0, "
               "\"children\": [], \"components\": [{\"id\": 1, \"type\": 6, \"textureFilePath\": \"\", "
               "\"flip\": 0, \"width\": 10, \"height\": 10, \"zOrder\": \"bad\", \"sourceRectEnabled\": false}]}]";
    }
    bool threw = false;
    try
    {
        fixture.runtime().loadScene(badScene.string());
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    test_support::require(threw, "Malformed candidate scene must throw");
    test_support::require(captureRenderedFrame(fixture, 4).submissions ==
                              (std::vector<SpriteIdentity>{{3, 1}, {2, 1}, {0, 0}}),
                          "Failed scene load must preserve live scene and its ordering state");

    // 8. Scene clear releases ordering & culling storage with zero lingering bytes or references.
    fixture.clearSceneAndCreateCamera(320.0f, 240.0f);
    const auto clearedFrame = captureRenderedFrame(fixture, 4);
    test_support::require(clearedFrame.submissions.empty() &&
                              clearedFrame.work.spriteOrderOwnedRecords == 0 &&
                              clearedFrame.work.spriteOrderOwnedBytes == 0 &&
                              clearedFrame.work.renderCullingOwnedRecords == 0 &&
                              clearedFrame.work.renderCullingOwnedBytes == 0,
                          "Scene clear must release ordering and culling storage completely");

    // 9. Stable-capacity churn does not grow capacity indefinitely or emit duplicates/stale refs.
    fixture.reserveSprites(128);
    constexpr size_t kChurnCount = 16;
    for (size_t i = 0; i < kChurnCount; ++i)
        fixture.addSprite(i, 0, 160.0f, 120.0f, 12.0f, 12.0f, FixtureTexture::BlackSquare);
    const auto warmChurn = captureRenderedFrame(fixture, kChurnCount);
    const size_t warmedOrderCap = warmChurn.work.spriteOrderOwnedEntryCapacity;
    const size_t warmedOrderBytes = warmChurn.work.spriteOrderOwnedBytes;
    const size_t warmedCullingBytes = warmChurn.work.renderCullingOwnedBytes;
    for (uint32_t gen = 1; gen <= 20; ++gen)
    {
        const size_t slot = gen % kChurnCount;
        fixture.replaceSprite(slot, gen, 160.0f, 120.0f, 12.0f, 12.0f, FixtureTexture::RedCircle);
        const auto churnFrame = captureRenderedFrame(fixture, kChurnCount);
        test_support::require(churnFrame.submissions.size() == kChurnCount &&
                                  churnFrame.work.spriteOrderOwnedEntryCapacity == warmedOrderCap &&
                                  churnFrame.work.spriteOrderOwnedBytes == warmedOrderBytes &&
                                  churnFrame.work.renderCullingOwnedBytes == warmedCullingBytes,
                              "Stable-capacity churn must not grow retained ordering or culling bytes");
    }

    // 10. Scene reload reproduces deterministic serialized equal-Z ties.
    const auto tieScene = tempDir / "ties.scene";
    {
        std::ofstream out(tieScene);
        out << nlohmann::json::array({
            nlohmann::json{{"id", 10}, {"name", "First"}, {"tag", ""}, {"active", true},
                           {"position", {{"x", 160.0}, {"y", 120.0}}},
                           {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", 0.0},
                           {"children", nlohmann::json::array()},
                           {"components", nlohmann::json::array({nlohmann::json{
                               {"id", 11}, {"type", 6}, {"textureFilePath", ""}, {"flip", 0},
                               {"width", 16.0}, {"height", 16.0}, {"zOrder", 0}, {"sourceRectEnabled", false}}})}},
            nlohmann::json{{"id", 20}, {"name", "Second"}, {"tag", ""}, {"active", true},
                           {"position", {{"x", 160.0}, {"y", 120.0}}},
                           {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", 0.0},
                           {"children", nlohmann::json::array()},
                           {"components", nlohmann::json::array({nlohmann::json{
                               {"id", 21}, {"type", 6}, {"textureFilePath", ""}, {"flip", 0},
                               {"width", 16.0}, {"height", 16.0}, {"zOrder", 0}, {"sourceRectEnabled", false}}})}}})
                   .dump(2);
    }
    fixture.loadSceneAndCreateCamera(tieScene.string(), 320.0f, 240.0f);
    const auto tieFrame1 = captureRenderedFrame(fixture, 2);
    test_support::require(tieFrame1.submissions == (std::vector<SpriteIdentity>{{0, 0}, {1, 0}}),
                          "Loaded scene must register equal-Z sprites in serialized traversal order");
    fixture.loadSceneAndCreateCamera(tieScene.string(), 320.0f, 240.0f);
    const auto tieFrame2 = captureRenderedFrame(fixture, 2);
    test_support::require(tieFrame2.submissions == (std::vector<SpriteIdentity>{{0, 0}, {1, 0}}),
                          "Repeated scene reload must reproduce equal-Z tie order deterministically");

    std::filesystem::remove_all(tempDir, ec);
}

struct ScaleRunSummary
{
    size_t singleEditEntries = 0;
    size_t singleEditComparisons = 0;
    size_t bulkEditEntries = 0;
    size_t bulkEditComparisons = 0;
};

ScaleRunSummary checkCachedWorkScaling(size_t population)
{
    ScopedRenderCullingMode selection("cached-linear");
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(kOutputWidth, kOutputHeight, 320.0f, 240.0f, true);
    fixture.reserveSprites(population);

    constexpr size_t kVisibleCount = 32;
    for (size_t i = 0; i < population; ++i)
    {
        // First 32 sprites sit well inside [0, 320] x [0, 240] even with +/-1px pan or movement.
        // Remaining sprites sit in a finite offscreen 2D grid well within certified bounds (>= 500, <= 8500).
        const size_t offscreenIndex = i >= kVisibleCount ? (i - kVisibleCount) : 0;
        const float x = i < kVisibleCount ? 40.0f + static_cast<float>(i % 8) * 30.0f
                                          : 500.0f + static_cast<float>(offscreenIndex % 100) * 80.0f;
        const float y = i < kVisibleCount ? 40.0f + static_cast<float>(i / 8) * 36.0f
                                          : 500.0f + static_cast<float>(offscreenIndex / 100) * 80.0f;
        auto &rec = fixture.addSprite(i, 0, x, y, 8.0f, 8.0f, FixtureTexture::BlackSquare);
        rec.sprite->setZOrder(static_cast<std::int32_t>(i));
    }

    CapturedFrame frame;
    frame.rgba.resize(static_cast<size_t>(kOutputWidth) * kOutputHeight * 4);
    frame.submissions.reserve(population);

    // Two warmup frames so submission/order/culling scratch vectors reach steady capacity.
    captureIntoFrame(fixture, frame);
    captureIntoFrame(fixture, frame);

    // Helper: run simulateFrame then measure allocations across SDLWindow::render() + Z edits,
    // verifying zero warmed allocations in culling/ordering/render submission.
    const auto renderAndCheckNoAlloc = [&](const char *label, auto &&prepBeforeRender, bool includePrepInAllocScope = true) {
        frame.submissions.clear();
        frame.rgba.resize(static_cast<size_t>(kOutputWidth) * kOutputHeight * 4);
        frame.readbackError.clear();
        if (!includePrepInAllocScope)
            prepBeforeRender();
        fixture.runtime().simulateFrame(test_support::dt);
        platformator_test::RenderObservation observation;
        observation.identityContext = &fixture;
        observation.resolveIdentity = &RenderCullingFixture::resolveIdentityCallback;
        observation.submissions = &frame.submissions;
        observation.readbackContext = &frame;
        observation.beforePresent = &CapturedFrame::beforePresent;
        {
            platformator_test::ScopedRenderObservation scope(observation);
            platformator_test::work = {};
            size_t warmedAllocations = 0;
            {
                ScopedAllocationCounter alloc;
                if (includePrepInAllocScope)
                    prepBeforeRender();
                platformator_detail::RuntimeAccess::window()->render();
                warmedAllocations = alloc.allocations();
            }
            test_support::require(warmedAllocations == 0,
                                  std::string(label) + ": warmed Z/render path performed temporary heap allocations (" +
                                      std::to_string(warmedAllocations) + ")");
            frame.work = platformator_test::work;
        }
        test_support::require(frame.readbackError.empty(), frame.readbackError);
        test_support::require(observation.unresolvedSprites == 0 && observation.submissionOverflows == 0,
                              "Sprite ordering observation had unresolved or overflowed submissions");
    };

    // 1. Identical writes across entire population must be a pure no-op.
    renderAndCheckNoAlloc("identical-writes", [&]() {
        for (size_t i = 0; i < population; ++i)
            fixture.record(i).sprite->setZOrder(fixture.record(i).sprite->getZOrder());
    });
    test_support::require(frame.work.spriteOrderPreparations == 0 &&
                              frame.work.spriteOrderSorts == 0 && frame.work.spriteOrderComparisons == 0 &&
                              frame.work.renderCullingBoundsRefreshes == 0 &&
                              frame.work.renderCullingFullPopulationVisits == 0 &&
                              frame.work.renderCullingCacheHits == 1,
                          "Identical Z writes must do zero ordering or culling work");

    // 2. Repeated edits to one visible sprite before preparation sort at most once on V entries.
    ScaleRunSummary summary{};
    renderAndCheckNoAlloc("repeated-edits", [&]() {
        for (int z : {999, -50, 42, -100, 0})
            fixture.record(0).sprite->setZOrder(z);
    });
    test_support::require(frame.work.spriteOrderPreparations == 1 &&
                              frame.work.spriteOrderSorts == 1 &&
                              frame.work.spriteOrderEntries == kVisibleCount &&
                              frame.work.renderCullingBoundsRefreshes == 0 &&
                              frame.work.renderCullingFullPopulationVisits == 0 &&
                              frame.submissions.front() == (SpriteIdentity{0, 0}),
                          "Repeated visible Z edit failed: prep=" + std::to_string(frame.work.spriteOrderPreparations) +
                              " sorts=" + std::to_string(frame.work.spriteOrderSorts) +
                              " entries=" + std::to_string(frame.work.spriteOrderEntries) +
                              " refresh=" + std::to_string(frame.work.renderCullingBoundsRefreshes) +
                              " frontSlot=" +
                              (frame.submissions.empty() ? std::string("empty")
                                                         : std::to_string(frame.submissions.front().slot)));
    summary.singleEditEntries = frame.work.spriteOrderEntries;
    summary.singleEditComparisons = frame.work.spriteOrderComparisons;

    // 3. Rejected-only Z edits do zero order/culling work, then surface on admission and fallback.
    renderAndCheckNoAlloc("rejected-only-edits", [&]() {
        for (size_t i = kVisibleCount; i < std::min(population, kVisibleCount + 100); ++i)
            fixture.record(i).sprite->setZOrder(-500);
    });
    test_support::require(frame.work.spriteOrderPreparations == 0 &&
                              frame.work.spriteOrderSorts == 0 && frame.work.spriteOrderEntries == 0 &&
                              frame.work.renderCullingBoundsRefreshes == 0 &&
                              frame.work.renderCullingFullPopulationVisits == 0 &&
                              frame.work.renderCullingCacheHits == 1,
                          "Rejected-only Z edits must leave visible draw order and culling state untouched");

    // Admission: move sprite (kVisibleCount) inside view with edited Z = -500.
    fixture.record(kVisibleCount).object->setPosition({160.0f, 120.0f});
    {
        captureIntoFrame(fixture, frame);
        test_support::require(frame.submissions.size() == kVisibleCount + 1 &&
                                  frame.submissions.front() == (SpriteIdentity{kVisibleCount, 0}),
                              "Admitting a rejected sprite with an earlier Z edit must immediately use its new Z");
    }
    // Move it back offscreen and stabilize.
    fixture.record(kVisibleCount).object->setPosition({500.0f, 500.0f});
    captureIntoFrame(fixture, frame);

    // Custom mapping fallback after rejected edit (set Z = -600 while offscreen).
    fixture.record(kVisibleCount + 1).sprite->setZOrder(-600);
    {
        SDL_Renderer *renderer = fixture.runtime().getRenderer();
        test_support::require(SDL_SetRenderScale(renderer, 1.25f, 1.0f), "Could not enable custom scale");
        captureIntoFrame(fixture, frame);
        test_support::require(frame.submissions.size() == population &&
                                  frame.submissions.front() == (SpriteIdentity{kVisibleCount + 1, 0}),
                              "Mapping fallback must submit rejected edited sprites using their latest Z");
        test_support::require(SDL_SetRenderScale(renderer, 1.0f, 1.0f), "Could not restore default scale");
    }
    fixture.record(kVisibleCount + 1).sprite->setZOrder(static_cast<std::int32_t>(kVisibleCount + 1));
    captureIntoFrame(fixture, frame);

    // 4. Within-view animation/movement of visible sprites keeps same visible set & zero order sorts.
    renderAndCheckNoAlloc(
        "within-view-motion",
        [&]() {
            for (size_t i = 0; i < 8; ++i)
                fixture.record(i).object->setPosition({41.0f + static_cast<float>(i % 8) * 30.0f, 41.0f});
        },
        false);
    test_support::require(frame.work.renderCullingBoundsRefreshes == 8 &&
                              frame.work.renderCullingHandleResolutions == 8 &&
                              frame.work.visibleMembershipChanges == 0 &&
                              frame.work.spriteOrderPreparations == 0 && frame.work.spriteOrderSorts == 0 &&
                              frame.work.spriteOrderComparisons == 0,
                          "Within-view movement keeping visible membership must resolve once per dirty slot and reuse prepared draw order");

    // 5. Same-set camera pan scans registered sprites in O(N) without resorting unchanged membership.
    renderAndCheckNoAlloc(
        "same-set-camera-pan", [&]() { fixture.camera()->getGameObject()->setPosition({1.0f, 0.0f}); }, false);
    test_support::require(frame.work.renderCullingFullPopulationVisits == population &&
                              frame.work.renderCullingCandidateVisits == population &&
                              frame.work.renderCullingBoundsRefreshes == 0 &&
                              frame.work.visibleMembershipChanges == 0 &&
                              frame.work.spriteOrderPreparations == 0 && frame.work.spriteOrderSorts == 0,
                          "Same-set camera pan must scan registered sprites without re-sorting unchanged visible membership");
    fixture.camera()->getGameObject()->setPosition({0.0f, 0.0f});
    captureIntoFrame(fixture, frame);

    // 6. Bulk Z edits on all visible sprites touch only V entries in one sort without full-population scans.
    renderAndCheckNoAlloc("bulk-visible-z-edits", [&]() {
        for (size_t i = 0; i < kVisibleCount; ++i)
            fixture.record(i).sprite->setZOrder(-static_cast<std::int32_t>(i));
    });
    test_support::require(frame.work.spriteOrderPreparations == 1 &&
                              frame.work.spriteOrderSorts == 1 &&
                              frame.work.spriteOrderEntries == kVisibleCount &&
                              frame.work.renderCullingBoundsRefreshes == 0 &&
                              frame.work.renderCullingFullPopulationVisits == 0 &&
                              frame.submissions.front() == (SpriteIdentity{kVisibleCount - 1, 0}) &&
                              frame.submissions.back() == (SpriteIdentity{0, 0}),
                          "Bulk visible Z edit must visit/sort only V entries without full-population scans");
    summary.bulkEditEntries = frame.work.spriteOrderEntries;
    summary.bulkEditComparisons = frame.work.spriteOrderComparisons;

    return summary;
}

void checkWorkOnlyScalingContracts()
{
    const ScaleRunSummary small = checkCachedWorkScaling(1000);
    const ScaleRunSummary large = checkCachedWorkScaling(10000);
    test_support::require(small.singleEditEntries == large.singleEditEntries &&
                              small.bulkEditEntries == large.bulkEditEntries &&
                              large.singleEditComparisons == small.singleEditComparisons &&
                              large.bulkEditComparisons == small.bulkEditComparisons,
                          "Fixed-V ordering work under cached-linear culling scaled with background population N");

    // 7. Headless execution with 1,000 sprites retains zero culling or ordering state.
    {
        ScopedRenderCullingMode selection("cached-linear");
        test_support::headlessEnvironment();
        RuntimeOptions options;
        options.windowSettings.width = kOutputWidth;
        options.windowSettings.height = kOutputHeight;
        options.windowSettings.headless = true;
        options.windowSettings.enableRenderCulling = true;
        platformator_test::work = {};
        platformator::Runtime runtime(options);
        runtime.createMainCameraIfNoMainCameraExists();
        std::vector<Sprite *> sprites;
        sprites.reserve(1000);
        for (size_t i = 0; i < 1000; ++i)
        {
            auto *obj = runtime.createGameObject()->setPosition({static_cast<float>(i), 50.0f});
            auto *sprite =
                obj->addComponent<Sprite>(static_cast<TextureWrapper *>(nullptr), SDL_FLIP_NONE, 8.0f, 8.0f)
                    ->getComponent<Sprite>();
            sprite->setZOrder(static_cast<std::int32_t>(i));
            sprites.push_back(sprite);
        }
        runtime.simulateAndRenderFrame(test_support::dt);
        platformator_test::work = {};
        size_t headlessAllocations = 0;
        {
            ScopedAllocationCounter alloc;
            for (size_t i = 0; i < sprites.size(); ++i)
                sprites[i]->setZOrder(-static_cast<std::int32_t>(i));
            platformator_detail::RuntimeAccess::window()->render();
            headlessAllocations = alloc.allocations();
        }
        test_support::require(headlessAllocations == 0 && platformator_test::work.renderCullingStateCreations == 0 &&
                                  platformator_test::work.spriteOrderOwnedRecords == 0 &&
                                  platformator_test::work.spriteOrderOwnedBytes == 0 &&
                                  platformator_test::work.spriteOrderPreparations == 0 &&
                                  platformator_test::work.spriteOrderSorts == 0,
                              "Headless simulation must maintain zero culling or ordering state");
    }
}

void checkBenchmarkOrderingVariants()
{
    ScopedRenderCullingMode selection("cached-linear");
    test_support::headlessEnvironment();

    struct StepOutcome
    {
        std::vector<SpriteIdentity> submissions;
        std::vector<uint8_t> rgba;
        platformator_test::WorkCounters work;
    };
    const auto runScript = [](SDLWindow::BenchmarkSpriteOrderMode orderMode) {
        RenderCullingFixture fixture(kOutputWidth, kOutputHeight, 320.0f, 240.0f, true);
        platformator_detail::RuntimeAccess::window()->setBenchmarkSpriteOrderMode(orderMode);
        fixture.reserveSprites(20);
        for (size_t i = 0; i < 16; ++i)
        {
            auto &rec = fixture.addSprite(i, 0, 80.0f + static_cast<float>(i % 4) * 40.0f,
                                          60.0f + static_cast<float>(i / 4) * 30.0f, 36.0f, 36.0f,
                                          i % 2 == 0 ? FixtureTexture::BlackSquare : FixtureTexture::RedCircle);
            rec.sprite->setZOrder(static_cast<std::int32_t>(i * 2));
        }
        for (size_t i = 16; i < 20; ++i)
        {
            auto &rec = fixture.addSprite(i, 0, 600.0f + static_cast<float>(i - 16) * 50.0f, 60.0f, 20.0f, 20.0f,
                                          FixtureTexture::BlackSquare);
            rec.sprite->setZOrder(static_cast<std::int32_t>(i * 2));
        }

        std::vector<StepOutcome> outcomes;
        const auto record = [&] {
            const auto frame = captureRenderedFrame(fixture, 20);
            outcomes.push_back({frame.submissions, frame.rgba, frame.work});
        };

        // Step 0: Initial preparation (16 visible).
        record();
        // Step 1: Clean frame (ForcedFullSort sorts; Rebuild/Repair skip).
        record();
        // Step 2: Single retained edit moving earlier (slot 12: 24 -> -5).
        fixture.record(12).sprite->setZOrder(-5);
        record();
        // Step 3: Repeated edit on single retained slot moving later (slot 12: -5 -> 100 -> 15).
        fixture.record(12).sprite->setZOrder(100);
        fixture.record(12).sprite->setZOrder(15);
        record();
        // Step 4: Rejected offscreen edit (slot 17) + single retained edit (slot 4) -> still 1 retained edit!
        fixture.record(17).sprite->setZOrder(-999);
        fixture.record(4).sprite->setZOrder(50);
        record();
        // Step 5: Two distinct retained edits (slot 0 & slot 1) -> repair must fall back to full rebuild.
        fixture.record(0).sprite->setZOrder(90);
        fixture.record(1).sprite->setZOrder(-90);
        record();
        // Step 6: Membership change (admit slot 17) + single retained edit (slot 2) -> repair falls back cleanly.
        fixture.record(17).object->setPosition({120.0f, 120.0f});
        fixture.record(2).sprite->setZOrder(-10);
        record();
        // Step 7: Slot retirement (remove slot 3) + single retained edit (slot 5) -> repair falls back cleanly.
        fixture.removeSprite(3);
        fixture.record(5).sprite->setZOrder(-20);
        record();

        // Step 8: Late-position single-entry repair: move the last visible entry (slot 0, highest Z=90)
        // to the front (Z=-1000), forcing slot-lookup to scan to the tail while rewriting only 1 entry.
        fixture.record(0).sprite->setZOrder(-1000);
        record();

        // Step 9: Warmed static frame after late-position repair must do zero lookup, entry, or sort work.
        record();

        const auto orderTiming = platformator_detail::RuntimeAccess::window()->consumeBenchmarkOrderTiming();
        test_support::require(orderTiming.calls == outcomes.size(),
                              "Benchmark order timing must measure every rendered frame's prepareDrawOrder call");
        const auto zeroTiming = platformator_detail::RuntimeAccess::window()->consumeBenchmarkOrderTiming();
        test_support::require(zeroTiming.calls == 0 && zeroTiming.totalNs == 0,
                              "Consuming benchmark order timing must reset counters");

        return outcomes;
    };

    const auto rebuild = runScript(SDLWindow::BenchmarkSpriteOrderMode::DefaultRebuild);
    const auto forcedSort = runScript(SDLWindow::BenchmarkSpriteOrderMode::ForcedFullSort);
    const auto repair = runScript(SDLWindow::BenchmarkSpriteOrderMode::SingleEntryRepair);

    test_support::require(rebuild.size() == 10 && forcedSort.size() == 10 && repair.size() == 10,
                          "Ordering variant script length mismatch");
    for (size_t step = 0; step < rebuild.size(); ++step)
    {
        test_support::require(forcedSort[step].submissions == rebuild[step].submissions &&
                                  forcedSort[step].rgba == rebuild[step].rgba &&
                                  repair[step].submissions == rebuild[step].submissions &&
                                  repair[step].rgba == rebuild[step].rgba,
                              "Benchmark ordering variant produced different submissions or pixels at step " +
                                  std::to_string(step));
    }

    // Step 1 clean frame: ForcedFullSort still sorts; Rebuild/Repair do zero work and zero lookup visits.
    test_support::require(rebuild[1].work.spriteOrderSorts == 0 && rebuild[1].work.spriteOrderEntries == 0 &&
                              rebuild[1].work.spriteOrderLookupVisits == 0 && repair[1].work.spriteOrderSorts == 0 &&
                              repair[1].work.spriteOrderRepairs == 0 && repair[1].work.spriteOrderLookupVisits == 0 &&
                              forcedSort[1].work.spriteOrderSorts == 1,
                          "Clean frame sort/repair/lookup counts mismatch");
    // Steps 2, 3, 4: Single retained edit uses SingleEntryRepair without full sort, separating lookup vs entries.
    for (const size_t step : {size_t(2), size_t(3), size_t(4)})
    {
        test_support::require(repair[step].work.spriteOrderRepairs == 1 &&
                                  repair[step].work.spriteOrderSorts == 0 &&
                                  repair[step].work.spriteOrderEntries == 1 &&
                                  repair[step].work.spriteOrderLookupVisits >= 1 &&
                                  repair[step].work.spriteOrderMoves > 0,
                              "Expected single-entry repair at step " + std::to_string(step));
    }
    // Steps 5, 6, 7: Second retained edit / membership change / slot retirement fall back to rebuild.
    for (const size_t step : {size_t(5), size_t(6), size_t(7)})
    {
        test_support::require(repair[step].work.spriteOrderRepairs == 0 &&
                                  repair[step].work.spriteOrderLookupVisits == 0 &&
                                  repair[step].work.spriteOrderSorts == 1,
                              "Single-entry repair must fall back to full rebuild at step " + std::to_string(step));
    }
    // Step 8: Late-position repair must visit the full table to find the slot while rewriting 1 entry.
    test_support::require(repair[8].work.spriteOrderRepairs == 1 &&
                              repair[8].work.spriteOrderSorts == 0 &&
                              repair[8].work.spriteOrderEntries == 1 &&
                              repair[8].work.spriteOrderLookupVisits == repair[8].submissions.size() &&
                              repair[8].work.spriteOrderMoves == repair[8].submissions.size() - 1 &&
                              rebuild[8].work.spriteOrderSorts == 1 &&
                              rebuild[8].work.spriteOrderEntries == rebuild[8].submissions.size() &&
                              rebuild[8].work.spriteOrderLookupVisits == 0,
                          "Late-position repair must account slot lookup separately from rewritten entries");
    // Step 9: Warmed static frame after late-position repair must hit cache cleanly.
    test_support::require(repair[9].work.renderCullingCacheHits == 1 &&
                              repair[9].work.spriteOrderPreparations == 0 &&
                              repair[9].work.spriteOrderLookupVisits == 0 &&
                              repair[9].work.spriteOrderEntries == 0 &&
                              repair[9].work.spriteOrderSorts == 0 &&
                              rebuild[9].work.renderCullingCacheHits == 1 &&
                              rebuild[9].work.spriteOrderPreparations == 0 &&
                              forcedSort[9].work.spriteOrderSorts == 1,
                          "Warmed static frame after repair must reuse cache cleanly");
}
} // namespace

int main()
{
#if !PLATFORMATOR_ENABLE_WORK_PROBES
    std::cout << "[SKIP] platformator_sprite_ordering_test requires PLATFORMATOR_ENABLE_WORK_PROBES=ON\n";
    return 0;
#else
    try
    {
        checkInitialOverlappingRegistrationOrder();
        checkOrderingAcrossModes("default", nullptr, true);
        checkOrderingAcrossModes("cached-linear", "cached-linear", true);
        checkOrderingAcrossModes("submit-all", "cached-linear", false);
        checkLifecycleStabilityAndChurn("cached-linear");
        checkWorkOnlyScalingContracts();
        checkBenchmarkOrderingVariants();
        std::cout << "[PASS] platformator_sprite_ordering_test\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] platformator_sprite_ordering_test: " << error.what() << '\n';
        return 1;
    }
#endif
}
