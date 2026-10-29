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
        SDL_Surface *raw = SDL_RenderReadPixels(renderer, nullptr);
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

        if (converted->w != outputWidth || converted->h != outputHeight)
        {
            capture->failure = "Readback dimensions do not match the requested output";
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
        const auto *pixels = static_cast<const uint8_t *>(converted->pixels);
        for (int row = 0; row < outputHeight; ++row)
        {
            std::memcpy(capture->rgba.data() + static_cast<size_t>(row) * outputWidth * 4,
                        pixels + static_cast<size_t>(row) * converted->pitch, static_cast<size_t>(outputWidth) * 4);
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
                         const std::function<void(double)> &stepHook = {}, CapturedImage *reusableImage = nullptr)
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
        platformator_test::work = {};
        const bool rendered = stepHook
                                  ? fixture.runtime().simulateAndRenderFrameWithCustomCallback(test_support::dt,
                                                                                                stepHook)
                                  : fixture.runtime().simulateAndRenderFrame(test_support::dt);
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
#if PLATFORMATOR_ENABLE_WORK_PROBES
    checkCustomMappingFallbacksAndDirectCamera();
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
