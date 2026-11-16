#include "render_culling_fixture.h"

#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <platformator/runtimeoptions.h>

#include "test_support.h"
#include "runtimeaccess.h"
#include "sdlwindow.h"

namespace
{
using render_culling_test::FixtureTexture;
using render_culling_test::RenderCullingFixture;
using render_culling_test::SpriteRecord;
using platformator_test::SpriteIdentity;
constexpr int OUTPUT_WIDTH = 320;
constexpr int OUTPUT_HEIGHT = 240;

struct SpriteSpec
{
    float x;
    float y;
    float width;
    float height;
    float rotationRadians = 0.0f;
    Eigen::Vector2f scale = Eigen::Vector2f::Ones();
    FixtureTexture texture = FixtureTexture::BlackSquare;
    SDL_FlipMode flip = SDL_FLIP_NONE;
    bool active = true;
    bool setSourceRect = false;
    bool uncertain = false;
};

struct CapturedImage
{
    int width = 0;
    int height = 0;
    size_t callbacks = 0;
    std::vector<uint8_t> rgba;
    std::string failure;

    static void beforePresent(SDL_Renderer *renderer, int outputWidth, int outputHeight, void *context)
    {
        auto *capture = static_cast<CapturedImage *>(context);
        if (capture == nullptr)
            return;

        ++capture->callbacks;
        capture->width = outputWidth;
        capture->height = outputHeight;
        const SDL_Rect readbackRect{0, 0, outputWidth, outputHeight};
        SDL_Surface *raw = SDL_RenderReadPixels(renderer, &readbackRect);
        if (raw == nullptr)
        {
            capture->failure = std::string("SDL_RenderReadPixels failed: ") + SDL_GetError();
            return;
        }

        SDL_Surface *converted = raw;
        if (raw->format != SDL_PIXELFORMAT_RGBA32)
        {
            converted = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(raw);
            if (converted == nullptr)
            {
                capture->failure = std::string("SDL_ConvertSurface failed: ") + SDL_GetError();
                return;
            }
        }

        if (converted->w > outputWidth || converted->h > outputHeight)
        {
            capture->failure = "Readback dimensions exceed the reserved output storage";
            SDL_DestroySurface(converted);
            return;
        }
        if (capture->rgba.size() != static_cast<size_t>(outputWidth) * outputHeight * 4)
        {
            capture->failure = "Readback destination storage was not reserved before rendering";
            SDL_DestroySurface(converted);
            return;
        }

        if (SDL_MUSTLOCK(converted) && !SDL_LockSurface(converted))
        {
            capture->failure = std::string("SDL_LockSurface failed: ") + SDL_GetError();
            SDL_DestroySurface(converted);
            return;
        }
        capture->width = converted->w;
        capture->height = converted->h;
        const auto *pixels = static_cast<const uint8_t *>(converted->pixels);
        for (int row = 0; row < converted->h; ++row)
        {
            std::memcpy(capture->rgba.data() + static_cast<size_t>(row) * outputWidth * 4,
                        pixels + static_cast<size_t>(row) * converted->pitch, static_cast<size_t>(converted->w) * 4);
        }
        if (SDL_MUSTLOCK(converted))
            SDL_UnlockSurface(converted);
        SDL_DestroySurface(converted);
    }
};

struct FrameResult
{
    CapturedImage image;
    uint64_t imageHash = 0;
    std::vector<SpriteIdentity> submissions;
    std::vector<SpriteIdentity> expectedVisible;
    platformator_test::WorkCounters work;
    std::string rendererName;
    int vsyncMode = 0;
    bool vsyncKnown = false;
    size_t unresolved = 0;
    size_t overflow = 0;
};

struct FrameDigest
{
    int width = 0;
    int height = 0;
    uint64_t imageHash = 0;
    std::vector<SpriteIdentity> submissions;
    std::vector<SpriteIdentity> expectedVisible;
};

std::vector<SpriteIdentity> expectedVisibleForFixture(const RenderCullingFixture &fixture,
                                                     int outputWidth = OUTPUT_WIDTH, int outputHeight = OUTPUT_HEIGHT);
void requireGoodReadback(const FrameResult &frame, const std::string &name);
uint64_t hashImage(const std::vector<uint8_t> &pixels);
std::string formatTrace(const std::vector<SpriteIdentity> &trace);

template <typename Settings> constexpr bool supportsCullingSetting()
{
    return requires(Settings &value) { value.enableRenderCulling = true; };
}

template <typename Settings> bool cullingSetting(const Settings &settings)
{
    if constexpr (requires { settings.enableRenderCulling; })
        return settings.enableRenderCulling;
    return true; // The pinned pre-feature runtime only has submit-all behavior.
}

std::pair<int, int> currentOutputSize(RenderCullingFixture &fixture)
{
    int outputWidth = 0;
    int outputHeight = 0;
    SDL_Renderer *renderer = fixture.runtime().getRenderer();
    if (renderer == nullptr || !SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight) ||
        outputWidth <= 0 || outputHeight <= 0)
        throw std::runtime_error("Could not query the current render output size");
    return {outputWidth, outputHeight};
}

FrameResult captureFrame(RenderCullingFixture &fixture, size_t submissionCapacity,
                         const std::function<void(double)> &stepHook = {}, CapturedImage *reusableImage = nullptr,
                         bool resetWork = true, bool simulate = true)
{
    const auto [outputWidth, outputHeight] = currentOutputSize(fixture);
    FrameResult result;
    if (reusableImage != nullptr)
        result.image.rgba.swap(reusableImage->rgba);
    const char *rendererName = SDL_GetRendererName(fixture.runtime().getRenderer());
    result.rendererName = rendererName != nullptr ? rendererName : "<unknown>";
    result.vsyncKnown = SDL_GetRenderVSync(fixture.runtime().getRenderer(), &result.vsyncMode);
    result.image.rgba.resize(static_cast<size_t>(outputWidth) * outputHeight * 4);
    result.submissions.reserve(submissionCapacity);
    platformator_test::RenderObservation observation;
    observation.identityContext = &fixture;
    observation.resolveIdentity = &RenderCullingFixture::resolveIdentityCallback;
    observation.submissions = &result.submissions;
    observation.readbackContext = &result.image;
    observation.beforePresent = &CapturedImage::beforePresent;

    {
        platformator_test::ScopedRenderObservation scope(observation);
        if (resetWork)
            platformator_test::work = {};
        const bool rendered = simulate
                                  ? (stepHook
                                         ? fixture.runtime().simulateAndRenderFrameWithCustomCallback(test_support::dt,
                                                                                                       stepHook)
                                         : fixture.runtime().simulateAndRenderFrame(test_support::dt))
                                  : (platformator_detail::RuntimeAccess::window()->render(), true);
        test_support::require(rendered, "Render culling fixture quit before a frame was rendered");
        result.work = platformator_test::work;
    }
    result.unresolved = observation.unresolvedSprites;
    result.overflow = observation.submissionOverflows;
    result.imageHash = hashImage(result.image.rgba);
    if (reusableImage != nullptr)
        reusableImage->rgba = std::move(result.image.rgba);
    return result;
}

using FixtureMutation = std::function<void(RenderCullingFixture &)>;

FrameResult capture(bool enableCulling, const std::vector<SpriteSpec> &sprites, float cameraWidth = 320.0f,
                    float cameraHeight = 240.0f, const FixtureMutation &mutation = {})
{
    constexpr int outputWidth = 320;
    constexpr int outputHeight = 240;
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(outputWidth, outputHeight, cameraWidth, cameraHeight, enableCulling);
    fixture.reserveSprites(sprites.size());

    for (size_t index = 0; index < sprites.size(); ++index)
    {
        const auto &spec = sprites[index];
        auto &record = fixture.addSprite(index, 0, spec.x, spec.y, spec.width, spec.height, spec.texture, spec.flip,
                                         spec.active, spec.rotationRadians, spec.scale);
        if (spec.setSourceRect)
            record.sprite->setSourceRect(SDL_FRect{5.0f, 5.0f, 30.0f, 30.0f});
    }

    if (mutation)
        mutation(fixture);

    FrameResult result = captureFrame(fixture, fixture.records().size());
    result.expectedVisible = expectedVisibleForFixture(fixture);
    return result;
}

struct Bounds
{
    double minX;
    double minY;
    double maxX;
    double maxY;
};

Bounds oracleBounds(const SpriteSpec &sprite, float cameraWidth, float cameraHeight)
{
    const double cameraScaleX = 320.0 / cameraWidth;
    const double cameraScaleY = 240.0 / cameraHeight;
    const double scaledWidth = static_cast<double>(sprite.width) * sprite.scale.x() * cameraScaleX;
    const double scaledHeight = static_cast<double>(sprite.height) * sprite.scale.y() * cameraScaleY;
    const double left = (static_cast<double>(sprite.x) -
                         static_cast<double>(sprite.width) * sprite.scale.x() * 0.5) * cameraScaleX;
    const double top = (static_cast<double>(sprite.y) -
                        static_cast<double>(sprite.height) * sprite.scale.y() * 0.5) * cameraScaleY;
    const double centerX = left + scaledWidth * 0.5;
    const double centerY = top + scaledHeight * 0.5;
    const double angle = static_cast<double>(sprite.rotationRadians) * (180.0 / std::acos(-1.0));
    const double radians = angle * (std::acos(-1.0) / 180.0);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const std::array<std::array<double, 2>, 4> corners = {{{left, top},
                                                           {left + scaledWidth, top},
                                                           {left, top + scaledHeight},
                                                           {left + scaledWidth, top + scaledHeight}}};
    Bounds result{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto &corner : corners)
    {
        const double offsetX = corner[0] - centerX;
        const double offsetY = corner[1] - centerY;
        const double x = centerX + offsetX * cosine - offsetY * sine;
        const double y = centerY + offsetX * sine + offsetY * cosine;
        result.minX = std::min(result.minX, x);
        result.minY = std::min(result.minY, y);
        result.maxX = std::max(result.maxX, x);
        result.maxY = std::max(result.maxY, y);
    }
    return result;
}

Bounds oracleBoundsForRect(double left, double top, double width, double height, double angleDegrees)
{
    const double centerX = left + width * 0.5;
    const double centerY = top + height * 0.5;
    const double radians = angleDegrees * (std::acos(-1.0) / 180.0);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const std::array<std::array<double, 2>, 4> corners = {{{left, top},
                                                           {left + width, top},
                                                           {left, top + height},
                                                           {left + width, top + height}}};
    Bounds result{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto &corner : corners)
    {
        const double offsetX = corner[0] - centerX;
        const double offsetY = corner[1] - centerY;
        const double x = centerX + offsetX * cosine - offsetY * sine;
        const double y = centerY + offsetX * sine + offsetY * cosine;
        result.minX = std::min(result.minX, x);
        result.minY = std::min(result.minY, y);
        result.maxX = std::max(result.maxX, x);
        result.maxY = std::max(result.maxY, y);
    }
    return result;
}

bool oracleRetains(const SpriteRecord &record, const Camera &camera, int outputWidth = 320, int outputHeight = 240)
{
    if (record.sprite == nullptr || record.object == nullptr)
        return false;
    if (record.sprite->getTexture() == nullptr)
        return true;
    const SDL_FRect cameraRect = camera.getCamera();
    const Eigen::Vector2f position = record.object->getPosition();
    const double scaleX = static_cast<double>(outputWidth) / cameraRect.w;
    const double scaleY = static_cast<double>(outputHeight) / cameraRect.h;
    const double width = static_cast<double>(record.sprite->getWidth()) * scaleX;
    const double height = static_cast<double>(record.sprite->getHeight()) * scaleY;
    const double left = (static_cast<double>(position.x()) - record.sprite->getWidth() * 0.5 - cameraRect.x) * scaleX;
    const double top = (static_cast<double>(position.y()) - record.sprite->getHeight() * 0.5 - cameraRect.y) * scaleY;
    const Bounds bounds = oracleBoundsForRect(left, top, width, height, record.object->getRotationInDegrees());
    return !(bounds.maxX + 1.0 < 0.0 || bounds.maxY + 1.0 < 0.0 || bounds.minX - 1.0 > outputWidth ||
             bounds.minY - 1.0 > outputHeight);
}

std::vector<SpriteIdentity> expectedVisibleForFixture(const RenderCullingFixture &fixture, int outputWidth,
                                                     int outputHeight)
{
    std::vector<SpriteIdentity> expected;
    if (fixture.camera() == nullptr)
        return expected;
    for (const SpriteRecord &record : fixture.records())
    {
        if (record.object == nullptr || !record.object->getActive())
            continue;
        if (oracleRetains(record, *fixture.camera(), outputWidth, outputHeight))
            expected.push_back(record.identity);
    }
    return expected;
}

void checkMutationScenario(const std::string &name, const std::vector<SpriteSpec> &sprites,
                           size_t expectedReferenceCount, const FixtureMutation &mutation)
{
    const FrameResult reference = capture(false, sprites, 320.0f, 240.0f, mutation);
    const FrameResult enabled = capture(true, sprites, 320.0f, 240.0f, mutation);
    requireGoodReadback(reference, name + " reference");
    requireGoodReadback(enabled, name + " enabled");
    test_support::require(reference.submissions.size() == expectedReferenceCount,
                          name + ": reference membership mismatch");
    test_support::require(reference.image.rgba == enabled.image.rgba,
                          name + ": lifecycle-enabled and submit-all images differ");
    test_support::require(reference.expectedVisible == enabled.expectedVisible,
                          name + ": modes do not share the same logical visible set");
    std::vector<SpriteIdentity> expected;
    for (const SpriteIdentity identity : reference.submissions)
    {
        if (std::find(reference.expectedVisible.begin(), reference.expectedVisible.end(), identity) !=
            reference.expectedVisible.end())
            expected.push_back(identity);
    }
    test_support::require(enabled.submissions == expected,
                          name + ": actual order does not match the current conservative reference subsequence");
    std::cout << name << " reference_submitted=" << reference.submissions.size()
              << " enabled_submitted=" << enabled.submissions.size()
              << " image_fnv64=0x" << std::hex << hashImage(enabled.image.rgba) << std::dec
              << " reference_order=" << formatTrace(reference.submissions)
              << " enabled_order=" << formatTrace(enabled.submissions) << '\n';
}

FrameResult captureFixtureFrame(RenderCullingFixture &fixture)
{
    FrameResult frame = captureFrame(fixture, fixture.records().size());
    frame.expectedVisible = expectedVisibleForFixture(fixture, frame.image.width, frame.image.height);
    return frame;
}

FrameResult captureRenderOnlyFrame(RenderCullingFixture &fixture)
{
    FrameResult frame = captureFrame(fixture, fixture.records().size(), {}, nullptr, true, false);
    frame.expectedVisible = frame.submissions;
    return frame;
}

FrameDigest captureFrameDigest(RenderCullingFixture &fixture, size_t submissionCapacity,
                               const std::function<void(double)> &stepHook, CapturedImage &reusableImage)
{
    FrameResult frame = captureFrame(fixture, submissionCapacity, stepHook, &reusableImage);
    requireGoodReadback(frame, "Dynamic rendering frame");
    frame.expectedVisible = expectedVisibleForFixture(fixture, frame.image.width, frame.image.height);
    return {frame.image.width, frame.image.height, frame.imageHash, std::move(frame.submissions),
            std::move(frame.expectedVisible)};
}

void compareFixtureFrames(const std::string &name, const FrameResult &reference, const FrameResult &enabled)
{
    requireGoodReadback(reference, name + " reference");
    requireGoodReadback(enabled, name + " enabled");
    test_support::require(reference.image.width == enabled.image.width &&
                              reference.image.height == enabled.image.height,
                          name + ": reference and enabled output sizes differ");
    test_support::require(reference.image.rgba == enabled.image.rgba,
                          name + ": reference and enabled images differ");
    std::vector<SpriteIdentity> expected;
    for (const SpriteIdentity identity : reference.submissions)
    {
        if (std::find(reference.expectedVisible.begin(), reference.expectedVisible.end(), identity) !=
            reference.expectedVisible.end())
            expected.push_back(identity);
    }
    test_support::require(enabled.submissions == expected,
                          name + ": enabled submissions differ from the ordered independent-oracle subsequence");
}

void compareFrameDigests(const std::string &name, const FrameDigest &reference, const FrameDigest &enabled)
{
    test_support::require(reference.width == enabled.width && reference.height == enabled.height,
                          name + ": reference and enabled output sizes differ");
    test_support::require(reference.imageHash == enabled.imageHash,
                          name + ": corresponding rendered frame hashes differ");
    test_support::require(reference.expectedVisible == enabled.expectedVisible,
                          name + ": independent oracle membership differs between modes");
    std::vector<SpriteIdentity> expected;
    for (const SpriteIdentity identity : reference.submissions)
    {
        if (std::find(reference.expectedVisible.begin(), reference.expectedVisible.end(), identity) !=
            reference.expectedVisible.end())
            expected.push_back(identity);
    }
    test_support::require(enabled.submissions == expected,
                          name + ": enabled order differs from the independent-oracle reference subsequence");
}

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
        if (setenv("PLATFORMATOR_RENDER_CULLING_MODE", mode, 1) != 0)
            throw std::runtime_error("Could not select the internal render-culling comparison mode");
    }

    ScopedRenderCullingMode(const ScopedRenderCullingMode &) = delete;
    ScopedRenderCullingMode &operator=(const ScopedRenderCullingMode &) = delete;

    ~ScopedRenderCullingMode()
    {
        if (hadPrevious_)
            setenv("PLATFORMATOR_RENDER_CULLING_MODE", previous_.c_str(), 1);
        else
            unsetenv("PLATFORMATOR_RENDER_CULLING_MODE");
    }

  private:
    std::string previous_;
    bool hadPrevious_ = false;
};

enum class StaticLayout
{
    Visible,
    Sparse,
    Offscreen
};

struct StaticModeCapture
{
    platformator_test::WorkCounters setupWork;
    std::vector<FrameResult> frames;
};

StaticModeCapture captureStaticMode(const char *mode, bool enableCulling, size_t count, StaticLayout layout)
{
    ScopedRenderCullingMode strategy(mode);
    test_support::headlessEnvironment();
    platformator_test::work = {};
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, enableCulling);
    fixture.reserveSprites(count);
    for (size_t index = 0; index < count; ++index)
    {
        const bool visible = layout == StaticLayout::Visible || (layout == StaticLayout::Sparse && index % 10 == 0);
        const float x = visible ? static_cast<float>(10 + (index % 16) * 19)
                                : static_cast<float>(430 + index * 6);
        const float y = visible ? static_cast<float>(10 + (index / 16) * 14) : 400.0f;
        fixture.addSprite(index, 0, x, y, 10.0f, 10.0f);
    }

    StaticModeCapture result;
    result.setupWork = platformator_test::work;
    result.frames.reserve(3);
    for (size_t frameIndex = 0; frameIndex < 3; ++frameIndex)
    {
        FrameResult frame = captureFrame(fixture, count);
        frame.expectedVisible = expectedVisibleForFixture(fixture, frame.image.width, frame.image.height);
        result.frames.push_back(std::move(frame));
    }
    return result;
}

void checkStaticCacheReuse()
{
    struct Case
    {
        size_t count;
        StaticLayout layout;
        const char *name;
    };
    const std::array<Case, 6> cases = {{{0, StaticLayout::Visible, "empty"},
                                        {1, StaticLayout::Visible, "visible-1"},
                                        {32, StaticLayout::Visible, "visible-32"},
                                        {256, StaticLayout::Visible, "visible-256"},
                                        {128, StaticLayout::Sparse, "sparse-128"},
                                        {128, StaticLayout::Offscreen, "offscreen-128"}}};

    for (const auto &testCase : cases)
    {
        const StaticModeCapture linear = captureStaticMode("current-linear", true, testCase.count, testCase.layout);
        const StaticModeCapture submitAll = captureStaticMode("current-linear", false, testCase.count, testCase.layout);
        const StaticModeCapture cached = captureStaticMode("cached-linear", true, testCase.count, testCase.layout);
        const StaticModeCapture indexed = captureStaticMode("indexed", true, testCase.count, testCase.layout);

        test_support::require(linear.setupWork.renderCullingStateCreations == 0 &&
                                  submitAll.setupWork.renderCullingStateCreations == 0,
                              std::string(testCase.name) + ": inactive comparison modes must not create cache state");
        test_support::require(cached.setupWork.renderCullingStateCreations == 1 &&
                                  cached.setupWork.renderCullingRecordsAdded == testCase.count,
                              std::string(testCase.name) + ": cached-linear must own one record per registered sprite");
        test_support::require(indexed.setupWork.renderCullingStateCreations == 1 &&
                                  indexed.setupWork.renderCullingRecordsAdded == testCase.count,
                              std::string(testCase.name) + ": indexed mode must own one record per registered sprite");
        test_support::require(linear.frames.front().image.rgba == submitAll.frames.front().image.rgba,
                              std::string(testCase.name) + ": submit-all and current-linear images differ");
        test_support::require(submitAll.frames.front().submissions.size() == testCase.count,
                              std::string(testCase.name) + ": submit-all must preserve every registered submission");
        for (size_t rank = 0; rank < testCase.count; ++rank)
            test_support::require(submitAll.frames.front().submissions[rank].slot == rank,
                                  std::string(testCase.name) + ": submit-all order differs from registration order");
        std::vector<SpriteIdentity> expectedLinear;
        for (const auto identity : submitAll.frames.front().submissions)
            if (std::find(linear.frames.front().expectedVisible.begin(), linear.frames.front().expectedVisible.end(),
                          identity) != linear.frames.front().expectedVisible.end())
                expectedLinear.push_back(identity);
        test_support::require(linear.frames.front().submissions == expectedLinear,
                              std::string(testCase.name) + ": current-linear differs from the ordered oracle subset");
        for (size_t frameIndex = 0; frameIndex < cached.frames.size(); ++frameIndex)
        {
            compareFixtureFrames(std::string(testCase.name) + " cached frame " + std::to_string(frameIndex),
                                 linear.frames.front(), cached.frames[frameIndex]);
            if (frameIndex == 0)
                continue;
            const auto &work = cached.frames[frameIndex].work;
            test_support::require(work.renderCullingBoundsRefreshes == 0 &&
                                      work.renderCullingQueries == 0 &&
                                      work.renderCullingFullPopulationVisits == 0 &&
                                      work.renderCullingOrderWork == 0 &&
                                      work.renderCullingCacheHits == 1,
                                  std::string(testCase.name) +
                                      ": an unchanged prepared frame must reuse the ordered result without selection work");
        }
        for (size_t frameIndex = 0; frameIndex < indexed.frames.size(); ++frameIndex)
        {
            compareFixtureFrames(std::string(testCase.name) + " indexed frame " + std::to_string(frameIndex),
                                 linear.frames.front(), indexed.frames[frameIndex]);
            const auto &work = indexed.frames[frameIndex].work;
            if (frameIndex == 0)
                test_support::require(work.renderCullingQueries == 1 && work.renderCullingProjectionRebuilds == 1,
                                      std::string(testCase.name) + ": initial indexed preparation must query once");
            else
                test_support::require(work.renderCullingBoundsRefreshes == 0 &&
                                          work.renderCullingQueries == 0 &&
                                          work.renderCullingCellVisits == 0 &&
                                          work.renderCullingCandidateVisits == 0 &&
                                          work.renderCullingFullPopulationVisits == 0 &&
                                          work.renderCullingOrderWork == 0 &&
                                          work.renderCullingCacheHits == 1,
                                      std::string(testCase.name) +
                                          ": an unchanged indexed frame must reuse selection without querying");
        }
    }
}

void checkCachePublicationCoalescing()
{
    ScopedRenderCullingMode strategy("cached-linear");
    test_support::headlessEnvironment();
    platformator_test::work = {};
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, true);
    constexpr size_t population = 16;
    fixture.reserveSprites(population);
    for (size_t index = 0; index < population; ++index)
        fixture.addSprite(index, 0, static_cast<float>(30 + index * 15), 80.0f, 12.0f, 16.0f);
    GameObject *parent = fixture.runtime().createGameObject();
    parent->addChild(fixture.record(0).object);
    parent->addChild(fixture.record(1).object);

    const FrameResult prepared = captureFixtureFrame(fixture);
    const auto originalWidth = fixture.record(0).sprite->getWidthWithoutScale();
    const auto originalHeight = fixture.record(0).sprite->getHeightWithoutScale();
    const auto originalScale = fixture.record(0).object->getScale();
    const auto originalRotation = fixture.record(0).object->getRotation();

    platformator_test::work = {};
    fixture.record(0).sprite->setWidth(originalWidth);
    fixture.record(0).sprite->setHeight(originalHeight);
    fixture.record(0).object->setScale(originalScale);
    fixture.record(0).object->setRotation(originalRotation);
    fixture.runtime().simulateFrame(test_support::dt);
    FrameResult sameValues = captureFrame(fixture, population, {}, nullptr, false);
    sameValues.expectedVisible = expectedVisibleForFixture(fixture, sameValues.image.width, sameValues.image.height);
    compareFixtureFrames("cache same-value setters", prepared, sameValues);
    test_support::require(sameValues.work.renderCullingDirtyEnqueues == 0 &&
                              sameValues.work.renderCullingBoundsRefreshes == 0,
                          "Same-value geometry setters must not enqueue or refresh a cached record");

    platformator_test::work = {};
    for (int step = 0; step < 5; ++step)
    {
        fixture.record(0).sprite->setWidth(originalWidth + 2.0f + step);
        fixture.record(0).sprite->setWidth(originalWidth);
        fixture.record(0).sprite->setHeight(originalHeight + 1.0f);
        fixture.record(0).sprite->setHeight(originalHeight);
        fixture.record(0).object->setScale({1.1f, 0.9f});
        fixture.record(0).object->setScale(originalScale);
        fixture.record(0).object->setRotation(originalRotation + 0.15f);
        fixture.record(0).object->setRotation(originalRotation);
        fixture.runtime().simulateFrame(test_support::dt);
    }
    FrameResult changedBack = captureFrame(fixture, population, {}, nullptr, false);
    changedBack.expectedVisible = expectedVisibleForFixture(fixture, changedBack.image.width, changedBack.image.height);
    compareFixtureFrames("cache changed-back values", prepared, changedBack);
    test_support::require(changedBack.work.renderCullingDirtyEnqueues == 1 &&
                              changedBack.work.renderCullingBoundsRefreshes == 1,
                          "Many changed-back edits and simulation steps must coalesce to one final refresh");

    platformator_test::work = {};
    for (int step = 0; step < 5; ++step)
    {
        parent->setPosition({static_cast<float>(step * 3), static_cast<float>(step * 2)});
        fixture.runtime().simulateFrame(test_support::dt);
    }
    FrameResult movedSubtree = captureFrame(fixture, population, {}, nullptr, false);
    movedSubtree.expectedVisible = expectedVisibleForFixture(fixture, movedSubtree.image.width, movedSubtree.image.height);
    test_support::require(movedSubtree.work.renderCullingDirtyEnqueues == 2 &&
                              movedSubtree.work.renderCullingBoundsRefreshes == 2,
                          "A changed parent subtree must refresh its two affected sprites once, excluding unrelated sprites");
    test_support::require(movedSubtree.submissions == movedSubtree.expectedVisible,
                          "The parent-subtree frame must draw current visible content in authoritative order");
}

std::vector<FrameResult> capturePublishedWorldPositionSequence(const char *mode)
{
    ScopedRenderCullingMode strategy(mode);
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, true);
    fixture.reserveSprites(1);
    fixture.addSprite(0, 0, 82.0f, 86.0f, 48.0f, 32.0f);
    GameObject *parent = fixture.runtime().createGameObject();
    parent->addChild(fixture.record(0).object);

    std::vector<FrameResult> frames;
    frames.reserve(3);
    frames.push_back(captureFixtureFrame(fixture));

    // The renderer sees the last synchronized worldPosition until the normal
    // transform boundary publishes the queued parent movement. Geometry edits
    // force cached paths to refresh during this render-only interval.
    parent->setPosition({900.0f, 0.0f});
    fixture.record(0).sprite->setWidth(62.0f);
    fixture.record(0).object->setRotation(0.47f);
    frames.push_back(captureRenderOnlyFrame(fixture));

    fixture.runtime().simulateFrame(test_support::dt);
    frames.push_back(captureRenderOnlyFrame(fixture));
    return frames;
}

void checkPublishedWorldPositionBounds()
{
    const auto current = capturePublishedWorldPositionSequence("current-linear");
    const auto cached = capturePublishedWorldPositionSequence("cached-linear");
    const auto indexed = capturePublishedWorldPositionSequence("indexed");
    test_support::require(current.size() == 3 && cached.size() == current.size() && indexed.size() == current.size(),
                          "Published-position regression must capture initial, render-only, and synchronized frames");
    test_support::require(current[1].submissions == std::vector<SpriteIdentity>{{0, 0}} &&
                              current[2].submissions.empty(),
                          "The parent move must remain at the prior published position until transform synchronization");
    for (size_t frame = 0; frame < current.size(); ++frame)
    {
        compareFixtureFrames("published world position cached frame " + std::to_string(frame), current[frame],
                             cached[frame]);
        compareFixtureFrames("published world position indexed frame " + std::to_string(frame), current[frame],
                             indexed[frame]);
    }
}

std::vector<FrameResult> captureDistantTexturelessSequence(const char *mode)
{
    ScopedRenderCullingMode strategy(mode);
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, true);
    fixture.reserveSprites(1);
    fixture.addSprite(0, 0, 1000.0f, 80.0f, 24.0f, 24.0f, FixtureTexture::Null);

    std::vector<FrameResult> frames;
    frames.reserve(6);
    frames.push_back(captureFixtureFrame(fixture));
    const auto captureMutation = [&](const std::function<void()> &mutation) {
        platformator_test::work = {};
        FrameResult frame = captureFrame(fixture, fixture.records().size(),
                                          [&](double) { mutation(); }, nullptr, false);
        frame.expectedVisible = expectedVisibleForFixture(fixture, frame.image.width, frame.image.height);
        frames.push_back(std::move(frame));
    };
    captureMutation([&] { fixture.record(0).sprite->setTextureWrapper(fixture.texture(FixtureTexture::BlackSquare)); });
    captureMutation([&] { fixture.record(0).sprite->setTextureWrapper(nullptr); });
    captureMutation([&] { fixture.camera()->getGameObject()->setPosition({1000.0f, 0.0f}); });
    captureMutation([&] { fixture.record(0).sprite->setTextureWrapper(fixture.texture(FixtureTexture::RedCircle)); });
    captureMutation([&] { fixture.camera()->getGameObject()->setPosition({0.0f, 0.0f}); });
    return frames;
}

void checkDistantTexturelessIndexedFallback()
{
    const auto current = captureDistantTexturelessSequence("current-linear");
    const auto indexed = captureDistantTexturelessSequence("indexed");
    test_support::require(current.size() == 6 && indexed.size() == current.size(),
                          "Textureless fallback sequence must cover initial, dirty, and camera-pan frames");
    const std::array<bool, 6> expectedSubmission = {true, false, true, true, true, false};
    for (size_t frame = 0; frame < current.size(); ++frame)
    {
        test_support::require(current[frame].submissions.size() == (expectedSubmission[frame] ? 1u : 0u),
                              "Current-linear textureless fallback schedule did not match its contract");
        compareFixtureFrames("distant textureless indexed frame " + std::to_string(frame), current[frame],
                             indexed[frame]);
    }
}

std::vector<FrameResult> captureSimultaneousCrossing(const char *mode)
{
    ScopedRenderCullingMode strategy(mode);
    test_support::headlessEnvironment();
    constexpr size_t population = 512;
    constexpr size_t half = population / 2;
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, true);
    fixture.reserveSprites(population);
    for (size_t slot = 0; slot < population; ++slot)
    {
        const bool initiallyVisible = slot < half;
        const float x = initiallyVisible ? static_cast<float>(40 + (slot % 16) * 14)
                                         : static_cast<float>(700 + (slot % 16) * 14);
        const float y = static_cast<float>(30 + (slot / 16 % 12) * 15);
        fixture.addSprite(slot, 0, x, y, 12.0f, 12.0f);
    }
    std::vector<FrameResult> frames;
    frames.reserve(2);
    frames.push_back(captureFixtureFrame(fixture));

    platformator_test::work = {};
    for (size_t slot = 0; slot < population; ++slot)
    {
        const bool initiallyVisible = slot < half;
        const float x = initiallyVisible ? static_cast<float>(900 + (slot % 16) * 14)
                                         : static_cast<float>(40 + (slot % 16) * 14);
        fixture.record(slot).object->setPosition({x, static_cast<float>(30 + (slot / 16 % 12) * 15)});
    }
    fixture.runtime().simulateFrame(test_support::dt);
    FrameResult changed = captureFrame(fixture, population, {}, nullptr, false, false);
    changed.expectedVisible = expectedVisibleForFixture(fixture, changed.image.width, changed.image.height);
    frames.push_back(std::move(changed));
    return frames;
}

void checkSimultaneousCrossingOrderWork()
{
    const auto current = captureSimultaneousCrossing("current-linear");
    const auto cached = captureSimultaneousCrossing("cached-linear");
    const auto indexed = captureSimultaneousCrossing("indexed");
    constexpr size_t population = 512;
    constexpr size_t half = population / 2;
    for (const auto *frames : {&cached, &indexed})
    {
        test_support::require(frames->size() == 2 && current.size() == 2,
                              "Simultaneous crossing must capture preparation and the batched membership change");
        compareFixtureFrames("simultaneous crossing current frame", current[1], (*frames)[1]);
        const auto &work = (*frames)[1].work;
        test_support::require(work.renderCullingDirtyEnqueues == population &&
                                  work.renderCullingBoundsRefreshes == population,
                              "Every crossing sprite must publish and refresh exactly once");
        test_support::require(work.renderCullingOrderWork <= population * 2 &&
                                  work.renderCullingOrderWork > 0,
                              "Simultaneous visibility changes must use bounded batch order work");
        test_support::require((*frames)[1].submissions.size() == half,
                              "The simultaneous crossing must replace the visible half without duplicates");
        test_support::require(work.renderCullingOwnedRecordCapacity == population,
                              "The warmed crossing path must not grow record capacity");
    }
}

std::vector<FrameResult> captureIndexedPanSequence(const char *mode)
{
    ScopedRenderCullingMode strategy(mode);
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, true);
    constexpr size_t spreadCount = 400;
    fixture.reserveSprites(spreadCount + 3);
    size_t slot = 0;
    for (int row = 0; row < 20; ++row)
    {
        for (int column = 0; column < 20; ++column)
        {
            const float x = -9500.0f + static_cast<float>(column) * 1000.0f;
            const float y = -9500.0f + static_cast<float>(row) * 1000.0f;
            fixture.addSprite(slot++, 0, x, y, 24.0f, 24.0f);
        }
    }
    // Keep overlapping translucent sprites in one queried cell. The index's
    // linked-list traversal is deliberately independent of submission rank.
    fixture.addSprite(slot++, 0, 0.0f, 0.0f, 120.0f, 90.0f, FixtureTexture::RedCircle);
    fixture.addSprite(slot++, 0, 0.0f, 0.0f, 90.0f, 110.0f, FixtureTexture::BlackSquare, SDL_FLIP_HORIZONTAL);
    fixture.addSprite(slot++, 0, 0.0f, 0.0f, 70.0f, 70.0f, FixtureTexture::RedCircle, SDL_FLIP_VERTICAL);

    const std::array<Eigen::Vector2f, 5> cameraPositions = {
        Eigen::Vector2f{-9500.0f, -9500.0f}, Eigen::Vector2f{-4500.0f, -4500.0f},
        Eigen::Vector2f{0.0f, 0.0f}, Eigen::Vector2f{4500.0f, 4500.0f},
        Eigen::Vector2f{9500.0f, 9500.0f}};
    std::vector<FrameResult> frames;
    frames.reserve(cameraPositions.size());
    for (size_t frameIndex = 0; frameIndex < cameraPositions.size(); ++frameIndex)
    {
        fixture.camera()->getGameObject()->setPosition(cameraPositions[frameIndex]);
        if (frameIndex == cameraPositions.size() - 1)
        {
            fixture.camera()->setWidth(640.0f);
            fixture.camera()->setHeight(480.0f);
        }
        FrameResult frame = captureFrame(fixture, fixture.records().size());
        frame.expectedVisible = expectedVisibleForFixture(fixture, frame.image.width, frame.image.height);
        frames.push_back(std::move(frame));
    }
    return frames;
}

void checkIndexedPanSelection()
{
    const auto reference = captureIndexedPanSequence("current-linear");
    const auto indexed = captureIndexedPanSequence("indexed");
    test_support::require(reference.size() == indexed.size(), "Pan comparison frame counts differ");
    for (size_t frame = 0; frame < reference.size(); ++frame)
        compareFixtureFrames("indexed pan frame " + std::to_string(frame), reference[frame], indexed[frame]);

    constexpr size_t population = 403;
    for (size_t frame = 1; frame + 1 < indexed.size(); ++frame)
    {
        const auto &work = indexed[frame].work;
        test_support::require(work.renderCullingQueries > 0,
                              "A camera pan must execute an actual spatial query");
        test_support::require(work.renderCullingBoundsRefreshes == 0 && work.renderCullingIndexRelinks == 0 &&
                                  work.renderCullingFullPopulationVisits == 0,
                              "Camera-only pans must reuse bounds and avoid a population-wide selection pass");
        test_support::require(work.renderCullingCandidateVisits < population / 4 &&
                                  work.renderCullingOrderWork < population,
                              "Spread-scene pan must visit and order a spatially selective candidate set");
    }

    const auto &projectionWork = indexed.back().work;
    test_support::require(projectionWork.renderCullingProjectionRebuilds == 1 &&
                              projectionWork.renderCullingBoundsRefreshes == population,
                          "A genuine camera projection change must refresh/regrid the dependent records");
    const auto &panWork = indexed[2].work;
    std::cout << "[WORK] indexed-pan queries=" << panWork.renderCullingQueries
              << " cells=" << panWork.renderCullingCellVisits
              << " candidates=" << panWork.renderCullingCandidateVisits
              << " order=" << panWork.renderCullingOrderWork
              << " refresh=" << panWork.renderCullingBoundsRefreshes
              << " relinks=" << panWork.renderCullingIndexRelinks
              << " full=" << panWork.renderCullingFullPopulationVisits
              << " cacheHit=" << panWork.renderCullingCacheHits
              << " ownedCells=" << panWork.renderCullingOwnedCellCount
              << " nodeSupply=" << panWork.renderCullingOwnedCellCapacity << '\n';
    std::cout << "[WORK] indexed-projection refresh=" << projectionWork.renderCullingBoundsRefreshes
              << " relinks=" << projectionWork.renderCullingIndexRelinks
              << " queries=" << projectionWork.renderCullingQueries
              << " order=" << projectionWork.renderCullingOrderWork << '\n';
}

void checkRenderedDynamicSequence()
{
    using render_culling_test::DeterministicFixtureTrajectory;
    using render_culling_test::FixturePose;
    using render_culling_test::FixtureTrajectoryProfile;

    constexpr size_t population = 40;
    constexpr size_t warmupFrames = 120;
    constexpr size_t measuredFrames = 720;
    constexpr size_t totalFrames = warmupFrames + measuredFrames;

    const auto captureMode = [](bool enableCulling) {
        test_support::headlessEnvironment();
        RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, enableCulling);
        fixture.reserveSprites(population + 1);
        DeterministicFixtureTrajectory trajectory(FixtureTrajectoryProfile::Crossing, population, 0);

        GameObject *parent = fixture.runtime().createGameObject();
        parent->setPosition({0.0f, 0.0f});
        for (size_t slot = 0; slot < population; ++slot)
        {
            const FixturePose pose = trajectory.pose(slot, 0);
            auto &record = fixture.addSprite(slot, 0, pose.x, pose.y, 28.0f, 22.0f,
                                             slot % 2 == 0 ? FixtureTexture::BlackSquare : FixtureTexture::RedCircle);
            if (slot == 0)
                parent->addChild(record.object);
        }

        std::vector<FrameDigest> frames;
        frames.reserve(measuredFrames);
        CapturedImage reusableImage;
        size_t simulatedStep = 0;
        const auto stepHook = [&](double) {
            const size_t step = simulatedStep++;
            if (step >= 680)
                return;
            for (size_t slot = 0; slot < population; ++slot)
            {
                if (slot >= fixture.records().size() || fixture.records()[slot].object == nullptr)
                    continue;
                const FixturePose pose = trajectory.pose(slot, step);
                fixture.records()[slot].object->setPosition({pose.x, pose.y});
                fixture.records()[slot].object->setRotation(pose.rotationRadians);
            }

            if (step < 240)
                parent->setPosition({static_cast<float>(step) * 0.12f, std::sin(step * 0.025f) * 8.0f});
            if (step == 170)
            {
                SpriteRecord &sprite = fixture.record(3);
                sprite.sprite->setWidth(46.0f);
                sprite.sprite->setHeight(19.0f);
                sprite.sprite->setSourceRect(SDL_FRect{4.0f, 3.0f, 22.0f, 18.0f});
                sprite.object->setScale({1.2f, 0.85f});
                sprite.object->setRotation(0.61f);
                sprite.sprite->setTextureWrapper(fixture.texture(FixtureTexture::BlackSquare));
            }
            if (step == 250)
                fixture.camera()->setCamera(SDL_FRect{40.0f, 25.0f, 520.0f, 300.0f});
            if (step == 330)
                fixture.replaceCamera(600.0f, 180.0f, -30.0f, 20.0f);
            if (step == 390)
                fixture.record(7).object->setActive(false);
            if (step == 425)
                fixture.record(7).object->setActive(true);
            if (step == 455)
                fixture.removeSprite(9);
            if (step == 490)
                fixture.addSprite(population, 0, 120.0f, 110.0f, 36.0f, 26.0f, FixtureTexture::RedCircle);
            if (step == 530)
                fixture.replaceSprite(12, 1, 150.0f, 100.0f, 34.0f, 40.0f, FixtureTexture::BlackSquare);
            if (step == 570)
                fixture.camera()->setCamera(SDL_FRect{0.0f, 0.0f, 320.0f, 240.0f});
            if (step == 660)
                fixture.removeCamera();
        };

        for (size_t frame = 0; frame < warmupFrames; ++frame)
        {
            const bool rendered = fixture.runtime().simulateAndRenderFrameWithCustomCallback(test_support::dt, stepHook);
            test_support::require(rendered, "Dynamic warmup ended before rendering");
        }

        for (size_t frame = 0; frame < measuredFrames; ++frame)
        {
            if (frame == 1)
            {
                test_support::require(SDL_SetWindowSize(fixture.runtime().getWindowHandle(), 400, 300),
                                      "Could not resize the SDL window during the dynamic sequence");
                SDL_PumpEvents();
            }
            FrameDigest digest = captureFrameDigest(fixture, population + 1, stepHook, reusableImage);
            if (frame == 1)
                test_support::require(digest.width == 400 && digest.height == 300,
                                      "Output resize after the first frame must update readback dimensions");
            frames.push_back(std::move(digest));
        }
        test_support::require(simulatedStep == totalFrames,
                              "Dynamic sequence must render matching warmup and measured frames");
        return frames;
    };

    const std::vector<FrameDigest> reference = captureMode(false);
    const std::vector<FrameDigest> enabled = captureMode(true);
    test_support::require(reference.size() == measuredFrames && enabled.size() == measuredFrames,
                          "Dynamic sequence must retain all 720 measured rendered frames");
    size_t entries = 0;
    size_t exits = 0;
    for (size_t frame = 0; frame < measuredFrames; ++frame)
    {
        compareFrameDigests("dynamic rendered frame " + std::to_string(frame), reference[frame], enabled[frame]);
        if (frame != 0)
        {
            const auto &before = reference[frame - 1].expectedVisible;
            const auto &after = reference[frame].expectedVisible;
            for (const SpriteIdentity identity : after)
                entries += std::find(before.begin(), before.end(), identity) == before.end();
            for (const SpriteIdentity identity : before)
                exits += std::find(after.begin(), after.end(), identity) == after.end();
        }
    }
    test_support::require(entries > 0 && exits > 0,
                          "Rendered moving sequence must include oracle-visible entry and exit transitions");
    for (size_t frame = measuredFrames - 10; frame < measuredFrames; ++frame)
    {
        test_support::require(reference[frame].imageHash == reference[frame - 1].imageHash &&
                                  enabled[frame].imageHash == enabled[frame - 1].imageHash &&
                                  reference[frame].submissions == reference[frame - 1].submissions &&
                                  enabled[frame].submissions == enabled[frame - 1].submissions,
                              "Unchanged repeated frames must preserve image hashes and traces");
    }
    test_support::require(reference.back().expectedVisible.empty() && reference.back().submissions.empty() &&
                              enabled.back().expectedVisible.empty() && enabled.back().submissions.empty(),
                          "Removing the camera must produce no visible sprites or render submissions");
    std::cout << "rendered-dynamic frames=" << measuredFrames << " warmup=" << warmupFrames
              << " oracle_entries=" << entries << " oracle_exits=" << exits
              << " resized_output=" << reference[1].width << 'x' << reference[1].height
              << " no_camera=verified repeated_frames=verified\n";
}

void checkSceneResetFailurePreservationAndEmptyChurn()
{
    ScopedRenderCullingMode strategy("indexed");
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, true);
    constexpr size_t population = 96;
    fixture.reserveSprites(population);
    for (size_t slot = 0; slot < population; ++slot)
        fixture.addSprite(slot, 0, static_cast<float>(slot * 42), static_cast<float>((slot % 9) * 35), 20.0f,
                          18.0f, slot % 2 == 0 ? FixtureTexture::BlackSquare : FixtureTexture::RedCircle);
    const FrameResult prepared = captureFixtureFrame(fixture);
    test_support::require(prepared.work.renderCullingOwnedRecords == population &&
                              prepared.work.renderCullingOwnedBytes > 0,
                          "Prepared scene must expose its live indexed cache storage");

    const auto badScenePath = std::filesystem::temp_directory_path() /
                              "platformator-render-culling-invalid-scene.json";
    const auto goodScenePath = std::filesystem::temp_directory_path() /
                               "platformator-render-culling-reset-scene.json";
    {
        std::ofstream invalid(badScenePath, std::ios::trunc);
        invalid << "{ this is not a scene }";
    }
    test_support::writeScene(goodScenePath, "Render Culling Reset", 9300);
    try
    {
        bool rejected = false;
        try
        {
            fixture.runtime().loadScene(badScenePath.string());
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        test_support::require(rejected, "Malformed scene preparation must fail before replacing live cache state");
        const FrameResult afterFailure = captureFixtureFrame(fixture);
        compareFixtureFrames("failed scene preparation preserves rendered scene", prepared, afterFailure);
        test_support::require(afterFailure.work.renderCullingOwnedBytes == prepared.work.renderCullingOwnedBytes,
                              "Failed scene preparation must preserve the live cache allocation state");

        for (size_t slot = population; slot-- > 0;)
            fixture.removeSprite(slot);
        const FrameResult emptyChurn = captureFixtureFrame(fixture);
        test_support::require(emptyChurn.work.renderCullingOwnedRecords == 0 &&
                                  emptyChurn.work.renderCullingOwnedBytes == prepared.work.renderCullingOwnedBytes,
                              "Ordinary transient empty churn must retain capacity for reuse");

        fixture.clearSceneAndCreateCamera(320.0f, 240.0f);
        const FrameResult afterClear = captureFixtureFrame(fixture);
        test_support::require(afterClear.work.renderCullingOwnedRecords == 0 &&
                                  afterClear.work.renderCullingOwnedBytes == 0 &&
                                  afterClear.work.renderCullingOwnedCellCount == 0 &&
                                  afterClear.work.renderCullingOwnedCellCapacity == 0,
                              "Successful scene clear must release old record and grid high-water capacity");

        fixture.loadSceneAndCreateCamera(goodScenePath.string(), 320.0f, 240.0f);
        fixture.addSprite(0, 1, 85.0f, 75.0f, 28.0f, 24.0f, FixtureTexture::RedCircle);
        const FrameResult replacement = captureFixtureFrame(fixture);
        test_support::require(replacement.work.renderCullingOwnedRecords == 1 &&
                                  replacement.work.renderCullingOwnedBytes > 0 &&
                                  replacement.submissions == std::vector<SpriteIdentity>{{0, 1}},
                              "Successful scene replacement must build fresh cache membership with new identity");

        fixture.clearSceneAndCreateCamera(320.0f, 240.0f);
        const FrameResult secondReset = captureFixtureFrame(fixture);
        test_support::require(secondReset.work.renderCullingOwnedBytes == 0 &&
                                  secondReset.work.renderCullingOwnedCellCount == 0,
                              "Repeated scene reset must not retain keys or handles from a prior scene");
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(badScenePath, ignored);
        std::filesystem::remove(goodScenePath, ignored);
        throw;
    }
    std::error_code ignored;
    std::filesystem::remove(badScenePath, ignored);
    std::filesystem::remove(goodScenePath, ignored);
    std::cout << "scene-reset failure_preserved=verified empty_churn=retained clear_load_reset=released\n";
}

void checkRuntimeCacheReset()
{
    ScopedRenderCullingMode strategy("indexed");
    test_support::headlessEnvironment();
    uint64_t firstSerial = 0;
    uint64_t secondSerial = 0;
    for (size_t instance = 0; instance < 2; ++instance)
    {
        RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, true);
        fixture.addSprite(0, static_cast<uint32_t>(instance), 70.0f, 80.0f, 36.0f, 32.0f,
                          FixtureTexture::RedCircle);
        const FrameResult frame = captureFixtureFrame(fixture);
        test_support::require(frame.work.renderCullingOwnedRecords == 1 &&
                                  frame.work.renderCullingOwnedCellCapacity <= 4 * frame.work.renderCullingOwnedRecordCapacity &&
                                  frame.submissions == std::vector<SpriteIdentity>{{0, static_cast<uint32_t>(instance)}},
                              "A newly constructed runtime must start with only its own render cache handles");
        if (instance == 0)
            firstSerial = fixture.runtime().getRuntimeSerial();
        else
            secondSerial = fixture.runtime().getRuntimeSerial();
    }
    test_support::require(firstSerial != 0 && secondSerial != 0 && firstSerial != secondSerial,
                          "Renderer cache lifetime must end with its Runtime and use the next runtime identity");
    std::cout << "runtime-reset cache_lifetime=per-runtime historical_handles=none\n";
}

template <typename Actions>
void checkTextureLifetimeScenario(const std::string &name, const std::vector<SpriteSpec> &sprites, Actions actions)
{
    constexpr int outputWidth = 320;
    constexpr int outputHeight = 240;
    const auto run = [&](bool enableCulling) {
        test_support::headlessEnvironment();
        RenderCullingFixture fixture(outputWidth, outputHeight, outputWidth, outputHeight, enableCulling);
        fixture.reserveSprites(sprites.size() + 2);
        for (size_t slot = 0; slot < sprites.size(); ++slot)
        {
            const auto &sprite = sprites[slot];
            fixture.addSprite(slot, 0, sprite.x, sprite.y, sprite.width, sprite.height, sprite.texture,
                              sprite.flip, sprite.active, sprite.rotationRadians, sprite.scale);
        }
        std::vector<FrameResult> frames;
        const auto capture = [&] { frames.push_back(captureFixtureFrame(fixture)); };
        actions(fixture, capture);
        return frames;
    };
    const std::vector<FrameResult> reference = run(false);
    const std::vector<FrameResult> enabled = run(true);
    test_support::require(reference.size() == enabled.size(), name + ": mode sequences have different frame counts");
    for (size_t frame = 0; frame < reference.size(); ++frame)
        compareFixtureFrames(name + " corresponding frame " + std::to_string(frame), reference[frame], enabled[frame]);
}

void checkTextureLifetimeRegressions()
{
    const std::vector<SpriteSpec> singleBlack{{80.0f, 70.0f, 48.0f, 44.0f}};
    checkTextureLifetimeScenario(
        "single-user-idle-replacement", singleBlack,
        [](RenderCullingFixture &fixture, const auto &capture) {
            fixture.replaceSprite(0, 1, 80.0f, 70.0f, 48.0f, 44.0f, FixtureTexture::BlackSquare);
            capture();
        });

    checkTextureLifetimeScenario(
        "remove-last-user-render-and-readd", singleBlack,
        [](RenderCullingFixture &fixture, const auto &capture) {
            fixture.removeSprite(0);
            capture();
            fixture.addSprite(1, 1, 80.0f, 70.0f, 48.0f, 44.0f, FixtureTexture::BlackSquare);
            capture();
        });

    checkTextureLifetimeScenario(
        "single-user-texture-switches", singleBlack,
        [](RenderCullingFixture &fixture, const auto &capture) {
            fixture.record(0).sprite->setTextureWrapper(fixture.texture(FixtureTexture::RedCircle));
            capture();
            fixture.record(0).sprite->setTextureWrapper(fixture.texture(FixtureTexture::BlackSquare));
            capture();
        });

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "platformator-render-culling-texture-reload.scene";
    std::error_code ignored;
    std::filesystem::remove(scenePath, ignored);
    test_support::writeScene(scenePath, "Texture Lifetime Reload", 9200);
    try
    {
        checkTextureLifetimeScenario(
            "single-user-scene-reload", singleBlack,
            [&](RenderCullingFixture &fixture, const auto &capture) {
                fixture.loadSceneAndCreateCamera(scenePath.string(), 320.0f, 240.0f);
                fixture.addSprite(0, 1, 80.0f, 70.0f, 48.0f, 44.0f, FixtureTexture::BlackSquare);
                capture();
            });
    }
    catch (...)
    {
        std::filesystem::remove(scenePath, ignored);
        throw;
    }
    std::filesystem::remove(scenePath, ignored);
}

bool oracleRetains(const SpriteSpec &sprite, float cameraWidth, float cameraHeight)
{
    if (sprite.uncertain)
        return true;
    const Bounds bounds = oracleBounds(sprite, cameraWidth, cameraHeight);
    return !(bounds.maxX + 1.0 < 0.0 || bounds.maxY + 1.0 < 0.0 || bounds.minX - 1.0 > 320.0 ||
             bounds.minY - 1.0 > 240.0);
}

uint64_t hashImage(const std::vector<uint8_t> &pixels)
{
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t byte : pixels)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

void saveRepresentativeImage(const std::string &name, const CapturedImage &image)
{
    const char *configuredRoot = std::getenv("PLATFORMATOR_RENDER_CULLING_EVIDENCE_DIR");
    if (configuredRoot == nullptr || *configuredRoot == '\0')
        return;

    const std::filesystem::path root(configuredRoot);
    std::filesystem::create_directories(root);
    const auto path = root / (name + "-enabled.ppm");
    if (std::filesystem::exists(path))
        throw std::runtime_error("Refusing to overwrite render-culling image evidence: " + path.string());
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("Could not create render-culling image evidence: " + path.string());
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel)
    {
        const char rgb[3] = {static_cast<char>(image.rgba[pixel * 4]),
                             static_cast<char>(image.rgba[pixel * 4 + 1]),
                             static_cast<char>(image.rgba[pixel * 4 + 2])};
        output.write(rgb, sizeof(rgb));
    }
    if (!output)
        throw std::runtime_error("Could not write render-culling image evidence: " + path.string());
}

std::string formatTrace(const std::vector<SpriteIdentity> &trace)
{
    std::string formatted = "[";
    for (size_t index = 0; index < trace.size(); ++index)
    {
        if (index != 0)
            formatted += ", ";
        formatted += std::to_string(trace[index].slot) + ":" + std::to_string(trace[index].generation);
    }
    return formatted + "]";
}

void requireGoodReadback(const FrameResult &frame, const std::string &name)
{
    test_support::require(frame.image.failure.empty(), name + ": " + frame.image.failure);
    test_support::require(frame.image.callbacks == 1, name + ": expected exactly one pre-presentation readback");
    test_support::require(frame.unresolved == 0, name + ": an observed sprite has no logical identity");
    test_support::require(frame.overflow == 0, name + ": the pre-reserved submission trace overflowed");
    test_support::require(frame.work.renderSubmittedSprites == frame.submissions.size(),
                          name + ": submission count and ordered trace differ");
}

void checkScenario(const std::string &name, const std::vector<SpriteSpec> &sprites, size_t expectedReferenceCount,
                   float cameraWidth = 320.0f, float cameraHeight = 240.0f)
{
    const FrameResult reference = capture(false, sprites, cameraWidth, cameraHeight);
    const FrameResult enabled = capture(true, sprites, cameraWidth, cameraHeight);
    requireGoodReadback(reference, name + " reference");
    requireGoodReadback(enabled, name + " enabled");
    test_support::require(reference.submissions.size() == expectedReferenceCount,
                          name + ": reference mode membership mismatch");
    test_support::require(reference.rendererName == "software" && enabled.rendererName == "software",
                          name + ": deterministic software renderer was not selected");
    test_support::require(reference.rendererName == enabled.rendererName &&
                              reference.vsyncKnown == enabled.vsyncKnown &&
                              (!reference.vsyncKnown || reference.vsyncMode == enabled.vsyncMode),
                          name + ": reference and enabled renderer/VSync state differ");
    test_support::require(reference.image.rgba == enabled.image.rgba,
                          name + ": enabled and submit-all images differ");

    std::vector<SpriteIdentity> expected;
    for (size_t index = 0; index < sprites.size(); ++index)
    {
        if (sprites[index].active && oracleRetains(sprites[index], cameraWidth, cameraHeight))
            expected.push_back({index, 0});
    }
    test_support::require(enabled.submissions == expected,
                          name + ": actual submissions are not the ordered conservative reference subsequence");
    saveRepresentativeImage(name, enabled.image);
    std::cout << name << " reference_submitted=" << reference.submissions.size()
              << " enabled_submitted=" << enabled.submissions.size() << " rgba_fnv64=0x" << std::hex
              << hashImage(enabled.image.rgba) << std::dec << " reference_order="
              << formatTrace(reference.submissions) << " enabled_order=" << formatTrace(enabled.submissions)
              << " views=" << enabled.work.renderViewPreparations
              << " eligibility_checks=" << enabled.work.renderEligibilityChecks
              << " visibility_checks=" << enabled.work.renderVisibilityChecks << " renderer="
              << enabled.rendererName << " vsync="
              << (enabled.vsyncKnown ? std::to_string(enabled.vsyncMode) : std::string("unavailable")) << '\n';
}

void checkLifecycleRegressions()
{
    checkMutationScenario(
        "parent-movement-size-scale-rotation-source-texture",
        {{105, 78, 40, 32}, {245, 150, 50, 28}, {410, 90, 42, 35}, {0, 0, 20, 20}}, 4,
        [](RenderCullingFixture &fixture) {
            SpriteRecord &child = fixture.record(0);
            GameObject *parent = fixture.runtime().createGameObject();
            parent->setPosition({15.0f, 20.0f});
            parent->addChild(child.object);
            parent->setPosition({47.0f, 39.0f});
            child.sprite->setWidth(58.0f);
            child.sprite->setHeight(24.0f);
            child.sprite->setSourceRect(SDL_FRect{4.0f, 5.0f, 24.0f, 18.0f});
            child.sprite->setFlip(static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
            child.sprite->setTextureWrapper(fixture.texture(FixtureTexture::RedCircle));
            child.object->setScale({1.35f, 0.75f});
            child.object->setRotation(0.83f);
        });

    checkMutationScenario(
        "activation-insertion-swap-removal-component-replacement-destruction",
        {{50, 50, 36, 34, 0.0f, Eigen::Vector2f::Ones(), FixtureTexture::BlackSquare, SDL_FLIP_NONE, false},
         {90, 80, 40, 38}, {410, 95, 46, 30}, {210, 135, 34, 42}},
        3, [](RenderCullingFixture &fixture) {
            fixture.record(0).object->setActive(true);
            fixture.removeSprite(1); // SDLWindow's swap-removal path changes the authoritative order.
            auto &replacement = fixture.replaceSpriteComponent(2, 1, FixtureTexture::RedCircle, 64.0f, 20.0f,
                                                                 SDL_FLIP_VERTICAL);
            replacement.object->setPosition({165.0f, 100.0f});
            replacement.object->setRotation(0.45f);
            fixture.removeSprite(3);
            fixture.addSprite(4, 0, 130.0f, 180.0f, 30.0f, 30.0f, FixtureTexture::Missing);
        });

    checkMutationScenario(
        "camera-component-replacement-and-dimensions",
        {{60, 80, 52, 28}, {250, 130, 38, 48}, {440, 95, 56, 26}, {750, 160, 34, 34}}, 4,
        [](RenderCullingFixture &fixture) {
            fixture.replaceCamera(640.0f, 120.0f, 12.0f, 8.0f);
            fixture.record(2).object->setPosition({600.0f, 110.0f});
            fixture.record(3).object->setPosition({900.0f, 180.0f});
        });

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "platformator-render-culling-lifecycle-load.scene";
    std::error_code ignored;
    std::filesystem::remove(scenePath, ignored);
    test_support::writeScene(scenePath, "Lifecycle Reload", 9100);
    try
    {
        checkMutationScenario("scene-clear-load-and-rebuild",
                              {{60, 70, 40, 40}, {390, 60, 36, 36}}, 2,
                              [&](RenderCullingFixture &fixture) {
                                  fixture.loadSceneAndCreateCamera(scenePath.string(), 320.0f, 240.0f);
                                  fixture.addSprite(0, 1, 90.0f, 90.0f, 52.0f, 44.0f,
                                                    FixtureTexture::RedCircle);
                                  fixture.addSprite(1, 1, 560.0f, 130.0f, 42.0f, 30.0f,
                                                    FixtureTexture::BlackSquare);
                              });
    }
    catch (...)
    {
        std::filesystem::remove(scenePath, ignored);
        throw;
    }
    std::filesystem::remove(scenePath, ignored);
}

bool axisVisible(float center, float extent, float lowerBound, float upperBound)
{
    return center + extent * 0.5f + 1.0f >= lowerBound && center - extent * 0.5f - 1.0f <= upperBound;
}

void checkTrajectoryContracts()
{
    using render_culling_test::DeterministicFixtureTrajectory;
    using render_culling_test::FixtureTrajectoryProfile;

    DeterministicFixtureTrajectory crossing(FixtureTrajectoryProfile::Crossing, 40, 0);
    DeterministicFixtureTrajectory crossingAgain(FixtureTrajectoryProfile::Crossing, 40, 0);
    size_t horizontalEntries = 0;
    size_t horizontalExits = 0;
    size_t verticalEntries = 0;
    size_t verticalExits = 0;
    size_t visibleEntries = 0;
    size_t visibleExits = 0;
    std::vector<bool> previousX(40);
    std::vector<bool> previousY(40);
    std::vector<bool> previousVisible(40);
    std::vector<render_culling_test::FixturePose> previousPoses(40);
    for (size_t slot = 0; slot < 40; ++slot)
        previousPoses[slot] = crossing.pose(slot, 0);

    for (size_t step = 0; step <= 720; ++step)
    {
        for (size_t slot = 0; slot < 40; ++slot)
        {
            const auto pose = crossing.pose(slot, step);
            const auto samePose = crossingAgain.pose(slot, step);
            test_support::require(pose.x == samePose.x && pose.y == samePose.y &&
                                      pose.rotationRadians == samePose.rotationRadians &&
                                      pose.generation == samePose.generation,
                                  "A fixed seed/slot/step must produce the same crossing pose");
            if (step != 0)
            {
                test_support::require(std::abs(pose.x - previousPoses[slot].x) < 10.0f &&
                                          std::abs(pose.y - previousPoses[slot].y) < 8.0f,
                                      "Crossing trajectories move continuously without teleport or wrap updates");
            }
            const bool xVisible = axisVisible(pose.x, 28.0f, 0.0f, 320.0f);
            const bool yVisible = axisVisible(pose.y, 22.0f, 0.0f, 240.0f);
            const bool visible = xVisible && yVisible;
            if (step >= 120)
            {
                horizontalEntries += !previousX[slot] && xVisible;
                horizontalExits += previousX[slot] && !xVisible;
                verticalEntries += !previousY[slot] && yVisible;
                verticalExits += previousY[slot] && !yVisible;
                visibleEntries += !previousVisible[slot] && visible;
                visibleExits += previousVisible[slot] && !visible;
            }
            previousX[slot] = xVisible;
            previousY[slot] = yVisible;
            previousVisible[slot] = visible;
            previousPoses[slot] = pose;
        }
    }
    test_support::require(horizontalEntries > 0 && horizontalExits > 0 && verticalEntries > 0 && verticalExits > 0 &&
                              visibleEntries > 0 && visibleExits > 0,
                          "The crossing profile must produce horizontal/vertical and visible-set entry/exit events");
    test_support::require(crossing.pose(7, 0).x == crossing.pose(7, 240).x &&
                              crossing.pose(7, 0).y == crossing.pose(7, 240).y,
                          "The reference crossing period must be 240 fixed steps");

    DeterministicFixtureTrajectory cameraPan(FixtureTrajectoryProfile::CameraPan, 80, 0);
    size_t panEntries = 0;
    size_t panExits = 0;
    std::vector<bool> panPrevious(80);
    for (size_t slot = 0; slot < 80; ++slot)
    {
        const auto pose = cameraPan.pose(slot, 0);
        test_support::require(pose.x == cameraPan.pose(slot, 500).x && pose.y == cameraPan.pose(slot, 500).y,
                              "Camera-pan sprites remain stationary");
    }
    for (size_t step = 0; step <= 720; ++step)
    {
        const auto [cameraX, cameraY] = cameraPan.cameraPosition(step);
        for (size_t slot = 0; slot < 80; ++slot)
        {
            const auto pose = cameraPan.pose(slot, step);
            const bool visible = axisVisible(pose.x - cameraX, 24.0f, 0.0f, 320.0f) &&
                                 axisVisible(pose.y - cameraY, 24.0f, 0.0f, 240.0f);
            if (step >= 120)
            {
                panEntries += !panPrevious[slot] && visible;
                panExits += panPrevious[slot] && !visible;
            }
            panPrevious[slot] = visible;
        }
    }
    test_support::require(panEntries > 0 && panExits > 0, "Camera pan must continuously cross sprite boundaries");

    DeterministicFixtureTrajectory churn(FixtureTrajectoryProfile::Churn, 1000, 0);
    size_t churnReplacements = 0;
    for (size_t step = 1; step <= 600; ++step)
    {
        size_t thisStep = 0;
        churn.forEachReplacedSlot(step, [&](size_t, uint32_t generation) {
            ++thisStep;
            test_support::require(generation > 0, "A replaced logical slot must receive a new generation");
        });
        test_support::require(thisStep == 10, "A 1,000-slot churn fixture replaces exactly one percent each step");
        churnReplacements += thisStep;
    }
    test_support::require(churnReplacements == 6000, "Churn population remains fixed while slots are replaced");
    DeterministicFixtureTrajectory tinyChurn(FixtureTrajectoryProfile::Churn, 5, 4);
    test_support::require(tinyChurn.replacementsPerStep() == 1 && tinyChurn.generationForSlot(0, 5) == 1,
                          "Tiny churn fixtures replace at least one slot per step");

    DeterministicFixtureTrajectory sparse(FixtureTrajectoryProfile::Sparse, 100, 0);
    DeterministicFixtureTrajectory clustered(FixtureTrajectoryProfile::Clustered, 100, 0);
    DeterministicFixtureTrajectory rotated(FixtureTrajectoryProfile::Rotated, 100, 0);
    size_t sparseVisible = 0;
    size_t clusteredVisible = 0;
    size_t rotatedVisible = 0;
    size_t rotatedOutside = 0;
    for (size_t slot = 0; slot < 100; ++slot)
    {
        const auto sparsePose = sparse.pose(slot, 0);
        const auto clusterPose = clustered.pose(slot, 0);
        const auto rotatedPose = rotated.pose(slot, 0);
        sparseVisible += axisVisible(sparsePose.x, 20.0f, 0.0f, 320.0f) &&
                         axisVisible(sparsePose.y, 20.0f, 0.0f, 240.0f);
        clusteredVisible += axisVisible(clusterPose.x, 20.0f, 0.0f, 320.0f) &&
                            axisVisible(clusterPose.y, 20.0f, 0.0f, 240.0f);
        rotatedVisible += axisVisible(rotatedPose.x, 70.0f, 0.0f, 320.0f) &&
                          axisVisible(rotatedPose.y, 14.0f, 0.0f, 240.0f);
        rotatedOutside += rotatedPose.x > 320.0f;
    }
    test_support::require(sparseVisible == 10 && clusteredVisible == 10 && rotatedVisible == 10 &&
                              rotatedOutside == 90,
                          "Sparse, clustered, and rotated layouts retain the planned mostly-offscreen split");
    std::cout << "trajectory crossings x=" << horizontalEntries << '/' << horizontalExits << " y="
              << verticalEntries << '/' << verticalExits << " camera_pan=" << panEntries << '/' << panExits
              << " churn_replacements=" << churnReplacements << '\n';
}

#if PLATFORMATOR_ENABLE_WORK_PROBES
void restoreDefaultRenderMapping(SDL_Renderer *renderer)
{
    test_support::require(SDL_SetRenderTarget(renderer, nullptr), "Could not reset render target");
    test_support::require(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED),
                          "Could not reset logical presentation");
    test_support::require(SDL_SetRenderScale(renderer, 1.0f, 1.0f), "Could not reset render scale");
    test_support::require(SDL_SetRenderViewport(renderer, nullptr), "Could not reset render viewport");
}

void checkCustomMappingFallbacksAndDirectCamera()
{
    enum class MappingChange
    {
        Viewport,
        Scale,
        LogicalPresentation,
        Target
    };

    for (const auto mapping : {MappingChange::Viewport, MappingChange::Scale, MappingChange::LogicalPresentation,
                               MappingChange::Target})
    {
        test_support::headlessEnvironment();
        RenderCullingFixture fixture(320, 240, 320.0f, 240.0f, true);
        fixture.reserveSprites(3);
        fixture.addSprite(0, 0, -600.0f, 30.0f, 24.0f, 24.0f);
        fixture.addSprite(1, 0, 700.0f, 70.0f, 36.0f, 18.0f, FixtureTexture::RedCircle);
        fixture.addSprite(2, 0, 60.0f, -500.0f, 28.0f, 28.0f);
        SDL_Renderer *renderer = fixture.runtime().getRenderer();
        SDL_Texture *target = nullptr;
        if (mapping == MappingChange::Target)
        {
            target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
            test_support::require(target != nullptr, "Could not create a render target for mapping fallback test");
        }

        platformator_test::RenderObservation observation;
        observation.identityContext = &fixture;
        observation.resolveIdentity = &RenderCullingFixture::resolveIdentityCallback;
        std::vector<SpriteIdentity> submissions;
        submissions.reserve(fixture.records().size());
        observation.submissions = &submissions;

        const auto setCustomMapping = [mapping, target](double) {
            auto *renderer = platformator::Runtime::current().getRenderer();
            bool succeeded = false;
            switch (mapping)
            {
            case MappingChange::Viewport:
            {
                const SDL_Rect viewport{8, 8, 300, 224};
                succeeded = SDL_SetRenderViewport(renderer, &viewport);
                break;
            }
            case MappingChange::Scale:
                succeeded = SDL_SetRenderScale(renderer, 1.25f, 1.0f);
                break;
            case MappingChange::LogicalPresentation:
                succeeded = SDL_SetRenderLogicalPresentation(renderer, 160, 120, SDL_LOGICAL_PRESENTATION_LETTERBOX);
                break;
            case MappingChange::Target:
                succeeded = SDL_SetRenderTarget(renderer, target);
                break;
            }
            test_support::require(succeeded, "Could not install custom render mapping");
        };
        const auto resetMapping = [](double) {
            restoreDefaultRenderMapping(platformator::Runtime::current().getRenderer());
        };

        platformator_test::WorkCounters customWork;
        {
            platformator_test::ScopedRenderObservation scope(observation);
            platformator_test::work = {};
            test_support::require(fixture.runtime().simulateAndRenderFrameWithCustomCallback(test_support::dt,
                                                                                             setCustomMapping),
                                  "Custom mapping fallback frame failed");
            customWork = platformator_test::work;
        }
        const std::vector<SpriteIdentity> expectedAll{{0, 0}, {1, 0}, {2, 0}};
        test_support::require(submissions == expectedAll, "Custom SDL mapping did not fall back to submit-all");
        test_support::require(customWork.renderEligibilityChecks == 1 && customWork.renderVisibilityChecks == 0,
                              "Custom SDL mapping performed culling classification instead of falling back");

        {
            platformator_test::ScopedRenderObservation scope(observation);
            platformator_test::work = {};
            test_support::require(fixture.runtime().simulateAndRenderFrameWithCustomCallback(test_support::dt,
                                                                                             resetMapping),
                                  "Default mapping restoration frame failed");
        }
        test_support::require(submissions.empty(), "Default mapping was not restored for same-frame culling");
        if (target != nullptr)
            SDL_DestroyTexture(target);
    }

    test_support::headlessEnvironment();
    RenderCullingFixture directFixture(320, 240, 320.0f, 240.0f, true);
    auto &offscreen = directFixture.addSprite(0, 0, -800.0f, 20.0f, 20.0f, 20.0f);
    platformator_test::RenderObservation directObservation;
    directObservation.identityContext = &directFixture;
    directObservation.resolveIdentity = &RenderCullingFixture::resolveIdentityCallback;
    std::vector<SpriteIdentity> directSubmissions;
    directSubmissions.reserve(1);
    directObservation.submissions = &directSubmissions;
    {
        platformator_test::ScopedRenderObservation scope(directObservation);
        directFixture.camera()->render(offscreen.sprite, directFixture.runtime().getRenderer(), 320, 240);
    }
    test_support::require(directSubmissions == std::vector<SpriteIdentity>{{0, 0}},
                          "Direct Camera::render stopped submitting an offscreen sprite");
}

std::vector<FrameResult> captureConsecutiveMutationSequence(const char *mode, bool enableCulling)
{
    ScopedRenderCullingMode strategy(mode);
    test_support::headlessEnvironment();
    RenderCullingFixture fixture(OUTPUT_WIDTH, OUTPUT_HEIGHT, 320.0f, 240.0f, enableCulling);
    fixture.reserveSprites(8);
    fixture.addSprite(0, 0, 65.0f, 65.0f, 42.0f, 34.0f, FixtureTexture::BlackSquare);
    fixture.addSprite(1, 0, 155.0f, 85.0f, 36.0f, 30.0f, FixtureTexture::RedCircle);
    fixture.addSprite(2, 0, 270.0f, 170.0f, 30.0f, 28.0f, FixtureTexture::BlackSquare);
    fixture.addSprite(3, 0, 440.0f, 110.0f, 38.0f, 32.0f, FixtureTexture::RedCircle);
    GameObject *parentA = fixture.runtime().createGameObject();
    GameObject *parentB = fixture.runtime().createGameObject();
    parentA->setPosition({4.0f, 6.0f});
    parentB->setPosition({-10.0f, 12.0f});
    parentA->addChild(fixture.record(0).object);

    SDL_Renderer *renderer = fixture.runtime().getRenderer();
    SDL_Texture *transitionTarget = nullptr;
    std::vector<FrameResult> frames;
    frames.reserve(11);
    const auto capture = [&](size_t step) {
        const auto callback = [&](double) {
            switch (step)
            {
            case 1:
                parentA->setPosition({23.0f, 17.0f});
                parentB->addChild(fixture.record(0).object);
                fixture.record(0).sprite->setWidth(56.0f);
                fixture.record(0).sprite->setHeight(22.0f);
                fixture.record(0).sprite->setSourceRect(SDL_FRect{4.0f, 3.0f, 20.0f, 17.0f});
                fixture.record(0).sprite->setFlip(static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
                fixture.record(0).sprite->setTextureWrapper(fixture.texture(FixtureTexture::RedCircle));
                fixture.record(0).object->setScale({1.25f, 0.8f});
                fixture.record(0).object->setRotation(0.72f);
                fixture.camera()->setCamera(SDL_FRect{12.0f, 8.0f, 300.0f, 220.0f});
                break;
            case 2:
            {
                const SDL_Rect viewport{8, 8, 300, 224};
                test_support::require(SDL_SetRenderViewport(renderer, &viewport), "Could not set transition viewport");
                fixture.removeSprite(3);
                fixture.record(1).object->setPosition({90.0f, 145.0f});
                break;
            }
            case 3:
                restoreDefaultRenderMapping(renderer);
                fixture.record(1).sprite->setSourceRect(SDL_FRect{2.0f, 2.0f, 18.0f, 15.0f});
                break;
            case 4:
                test_support::require(SDL_SetRenderScale(renderer, 1.25f, 1.0f), "Could not set transition scale");
                fixture.record(2).sprite->setTextureWrapper(nullptr);
                break;
            case 5:
                restoreDefaultRenderMapping(renderer);
                fixture.addSprite(4, 1, 205.0f, 80.0f, 44.0f, 36.0f, FixtureTexture::BlackSquare);
                break;
            case 6:
                test_support::require(SDL_SetRenderLogicalPresentation(renderer, 160, 120,
                                                                        SDL_LOGICAL_PRESENTATION_LETTERBOX),
                                      "Could not set transition logical presentation");
                parentB->setPosition({48.0f, 31.0f});
                fixture.record(1).sprite->setFlip(SDL_FLIP_VERTICAL);
                break;
            case 7:
                restoreDefaultRenderMapping(renderer);
                fixture.replaceCamera(360.0f, 200.0f, -18.0f, 11.0f);
                fixture.record(4).object->setPosition({235.0f, 115.0f});
                break;
            case 8:
                transitionTarget = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                                     400, 300);
                test_support::require(transitionTarget != nullptr, "Could not create transition target");
                test_support::require(SDL_SetRenderTarget(renderer, transitionTarget), "Could not set transition target");
                fixture.record(4).sprite->clearSourceRect();
                fixture.record(4).object->setScale({0.7f, 1.4f});
                break;
            case 9:
                restoreDefaultRenderMapping(renderer);
                fixture.removeCamera();
                break;
            case 10:
                fixture.replaceCamera(320.0f, 240.0f, 0.0f, 0.0f);
                parentB->removeChild(fixture.record(0).object);
                fixture.record(0).object->setPosition({72.0f, 92.0f});
                break;
            default:
                break;
            }
        };
        FrameResult frame = captureFrame(fixture, fixture.records().size(), callback);
        frame.expectedVisible = expectedVisibleForFixture(fixture, frame.image.width, frame.image.height);
        frames.push_back(std::move(frame));
    };

    capture(0);
    test_support::require(SDL_SetWindowSize(fixture.runtime().getWindowHandle(), 400, 300),
                          "Could not resize the window after its first rendered frame");
    SDL_PumpEvents();
    for (size_t step = 1; step <= 10; ++step)
        capture(step);
    if (transitionTarget != nullptr)
        SDL_DestroyTexture(transitionTarget);
    return frames;
}

void checkConsecutiveMutationStrategies()
{
    const auto current = captureConsecutiveMutationSequence("current-linear", true);
    const auto submitAll = captureConsecutiveMutationSequence("current-linear", false);
    const auto cached = captureConsecutiveMutationSequence("cached-linear", true);
    const auto indexed = captureConsecutiveMutationSequence("indexed", true);
    test_support::require(current.size() == 11 && submitAll.size() == current.size() &&
                              cached.size() == current.size() && indexed.size() == current.size(),
                          "Consecutive mutation strategies produced different frame counts");
    test_support::require(current[1].image.width == 400 && current[1].image.height == 300,
                          "A real output resize after rendering must update the next frame readback");
    for (size_t step = 0; step < current.size(); ++step)
    {
        const bool mappingFallback = step == 2 || step == 4 || step == 6 || step == 8;
        if (mappingFallback)
        {
            for (const auto *mode : {&cached[step], &indexed[step]})
            {
                requireGoodReadback(*mode, "consecutive mapping fallback");
                test_support::require(mode->image.width == current[step].image.width &&
                                          mode->image.height == current[step].image.height &&
                                          mode->image.rgba == current[step].image.rgba &&
                                          mode->submissions == current[step].submissions,
                                      "A custom mapping fallback must submit all current eligible sprites in order");
            }
        }
        else
        {
            compareFixtureFrames("consecutive cached-linear transition " + std::to_string(step), current[step],
                                 cached[step]);
            compareFixtureFrames("consecutive indexed transition " + std::to_string(step), current[step],
                                 indexed[step]);
        }
        requireGoodReadback(submitAll[step], "consecutive submit-all transition " + std::to_string(step));
        test_support::require(submitAll[step].image.rgba == current[step].image.rgba,
                              "Submit-all image changed during a consecutive transition");
        if (mappingFallback)
            continue;
        std::vector<SpriteIdentity> selected;
        for (const auto identity : submitAll[step].submissions)
            if (std::find(submitAll[step].expectedVisible.begin(), submitAll[step].expectedVisible.end(), identity) !=
                submitAll[step].expectedVisible.end())
                selected.push_back(identity);
        test_support::require(selected == current[step].submissions,
                              "Current-linear selection differs from the submit-all independent-oracle subsequence");
    }
    for (const auto *frames : {&cached, &indexed})
    {
        for (const size_t step : {size_t(2), size_t(4), size_t(6), size_t(8)})
            test_support::require((*frames)[step].work.renderCullingMappingFallbacks == 1 &&
                                      (*frames)[step].work.renderCullingBoundsRefreshes == 0 &&
                                      (*frames)[step].work.renderCullingQueries == 0,
                                  "Unsupported mappings must submit all without consuming dirty bounds or rebuilding selection");
        test_support::require((*frames)[3].work.renderCullingBoundsRefreshes == 1 &&
                                  (*frames)[3].work.renderCullingQueries <= 1 &&
                                  (*frames)[3].work.renderCullingProjectionRebuilds == 0,
                              "Returning to default mapping must drain only the affected record without a full rebuild");
        test_support::require((*frames)[5].work.renderCullingBoundsRefreshes == 2 &&
                                  (*frames)[5].work.renderCullingDirtyEnqueues == 1 &&
                                  (*frames)[5].work.renderCullingQueries <= 1 &&
                                  (*frames)[5].work.renderCullingProjectionRebuilds == 0,
                              "Texture eligibility changes during fallback must remain pending until same-view resumption");
    }
    test_support::require(current[9].submissions.empty() && indexed[9].submissions.empty(),
                          "No-camera transition must suspend selection and submit no sprites");
    std::cout << "consecutive-mutations frames=" << current.size()
              << " cached-linear=indexed=current-linear submit-all=verified resize=400x300 fallback-return=verified\n";
}
#endif

void checkPublicOption()
{
    char program[] = "render-culling-test";
    char *defaultArgs[] = {program};
    RuntimeOptions defaults = parseRuntimeOptions(1, defaultArgs, "default.scene");
    test_support::require(cullingSetting(defaults.windowSettings),
                          "The default RuntimeOptions mode must enable render culling");

    char noCulling[] = "--no-render-culling";
    char *disabledArgs[] = {program, noCulling};
    RuntimeOptions disabled = parseRuntimeOptions(2, disabledArgs, "default.scene");
    test_support::require(!cullingSetting(disabled.windowSettings),
                          "--no-render-culling must select submit-all mode");

    char *repeatedArgs[] = {program, noCulling, noCulling};
    RuntimeOptions repeated = parseRuntimeOptions(3, repeatedArgs, "default.scene");
    test_support::require(!cullingSetting(repeated.windowSettings), "Repeated no-value mode must be idempotent");
}

void run()
{
    checkStaticCacheReuse();
    checkCachePublicationCoalescing();
    checkPublishedWorldPositionBounds();
    checkDistantTexturelessIndexedFallback();
    checkSimultaneousCrossingOrderWork();
    checkIndexedPanSelection();
    checkScenario("all-visible",
                  {{70, 60, 40, 40}, {150, 90, 50, 30, 0.0f, Eigen::Vector2f::Ones(), FixtureTexture::RedCircle}},
                  2);
    checkScenario("all-offscreen", {{390, 60, 40, 40}, {-70, 120, 30, 30}, {120, 310, 25, 25}}, 3);
    checkScenario("inactive", {{50, 50, 40, 40, 0, Eigen::Vector2f::Ones(), FixtureTexture::BlackSquare,
                                 SDL_FLIP_NONE, false}},
                  0);
    checkScenario("empty", {}, 0);

    SpriteSpec overlap{78, 62, 74, 50, 0.29f, {1.2f, 0.8f}, FixtureTexture::RedCircle,
                       SDL_FLIP_HORIZONTAL, true, true};
    SpriteSpec partial{-8, 128, 42, 38, 0.0f, Eigen::Vector2f::Ones(), FixtureTexture::BlackSquare,
                       SDL_FLIP_VERTICAL, true, true};
    SpriteSpec rotated{314, 195, 70, 18, 0.7853982f, {0.85f, 1.4f}, FixtureTexture::Missing,
                       SDL_FLIP_NONE, true, false};
    SpriteSpec missing{520, 190, 18, 18, 0.0f, Eigen::Vector2f::Ones(), FixtureTexture::Null,
                       SDL_FLIP_NONE, true, false, true};
    checkScenario("mixed-translucent-transform-source-flip-missing",
                  {{70, 60, 50, 44}, {390, 70, 40, 40}, overlap, partial, rotated, missing}, 6);
    checkScenario("unequal-camera-output-scale",
                  {{110, 72, 54, 26, 0.41f, {1.1f, 0.9f}}, {490, 64, 42, 28}, {800, 110, 32, 24}}, 3,
                  640.0f, 120.0f);
    checkLifecycleRegressions();
    checkTextureLifetimeRegressions();
    checkTrajectoryContracts();
    checkRenderedDynamicSequence();
    checkSceneResetFailurePreservationAndEmptyChurn();
    checkRuntimeCacheReset();
#if PLATFORMATOR_ENABLE_WORK_PROBES
    checkCustomMappingFallbacksAndDirectCamera();
    checkConsecutiveMutationStrategies();
#endif
}
} // namespace

int main()
{
    if constexpr (!supportsCullingSetting<WindowSettings>())
    {
        std::cout << "[SKIP] render_culling_test requires a runtime with WindowSettings::enableRenderCulling\n";
        return 77;
    }
    try
    {
        checkPublicOption();
        std::cout << "[PASS] render culling public option parser\n";
#if !PLATFORMATOR_ENABLE_WORK_PROBES
        std::cout << "[SKIP] render_culling_test requires PLATFORMATOR_ENABLE_WORK_PROBES=ON for actual frame readback and order observation\n";
        return 77;
#else
        run();
        std::cout << "[PASS] render_culling_test\n";
        return 0;
#endif
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] render_culling_test: " << exception.what() << '\n';
        return 1;
    }
}
