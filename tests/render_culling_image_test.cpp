#include "renderculling.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "sdlwindow.h"
#include "runtime_work_probe.h"
#include "spatial_test_support.h"
#include "test_support.h"

#include <platformator/boxcollider.h>
#include <platformator/animator.h>
#include <platformator/animationclip.h>
#include <platformator/camera.h>
#include <platformator/runtime.h>
#include <platformator/sprite.h>
#include <platformator/texturewrapper.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace test_support;
using platformator_detail::RenderCullingFrameStats;
using ::RenderCullingMode;
using spatial_test_support::PixelSnapshot;

namespace
{
uint64_t pixelHash(const PixelSnapshot &pixels)
{
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t byte : pixels.rgba)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::filesystem::path evidenceDirectory()
{
    const char *configured = std::getenv("PLATFORMATOR_RENDER_EVIDENCE_DIR");
    if (!configured || *configured == '\0')
        return {};
    const std::filesystem::path directory(configured);
    std::filesystem::create_directories(directory);
    return directory;
}

void savePpm(const std::filesystem::path &path, const PixelSnapshot &pixels)
{
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "Render evidence image can be created: " + path.string());
    output << "P6\n" << pixels.width << ' ' << pixels.height << "\n255\n";
    for (size_t i = 0; i + 3 < pixels.rgba.size(); i += 4)
    {
        output.put(static_cast<char>(pixels.rgba[i]));
        output.put(static_cast<char>(pixels.rgba[i + 1]));
        output.put(static_cast<char>(pixels.rgba[i + 2]));
    }
    require(static_cast<bool>(output), "Render evidence image is fully written: " + path.string());
}

struct TextureDeleter
{
    void operator()(SDL_Surface *surface) const { SDL_DestroySurface(surface); }
};

std::unique_ptr<TextureWrapper> solidTexture(SDL_Renderer *renderer, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    std::unique_ptr<SDL_Surface, TextureDeleter> surface(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32));
    require(surface != nullptr, "Software renderer fixture creates a source surface");
    const Uint32 color = SDL_MapSurfaceRGBA(surface.get(), red, green, blue, alpha);
    require(SDL_FillSurfaceRect(surface.get(), nullptr, color), "Software renderer fixture fills texture pixels");
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface.get());
    require(texture != nullptr, std::string("Software renderer creates texture: ") + SDL_GetError());
    require(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND), "Texture uses transparent blending");
    return std::make_unique<TextureWrapper>(texture, "");
}

std::unique_ptr<TextureWrapper> splitTexture(SDL_Renderer *renderer)
{
    std::unique_ptr<SDL_Surface, TextureDeleter> surface(SDL_CreateSurface(8, 4, SDL_PIXELFORMAT_RGBA32));
    require(surface != nullptr, "Software renderer fixture creates an animation/source surface");
    const Uint32 red = SDL_MapSurfaceRGBA(surface.get(), 255, 64, 32, 210);
    const Uint32 green = SDL_MapSurfaceRGBA(surface.get(), 32, 220, 96, 180);
    const SDL_Rect left{0, 0, 4, 4}, right{4, 0, 4, 4};
    require(SDL_FillSurfaceRect(surface.get(), &left, red) && SDL_FillSurfaceRect(surface.get(), &right, green),
            "Split texture contains independently colored source frames");
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface.get());
    require(texture != nullptr, std::string("Software renderer creates split texture: ") + SDL_GetError());
    require(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND), "Split texture uses transparent blending");
    return std::make_unique<TextureWrapper>(texture, "");
}

RuntimeOptions renderOptions(RenderCullingMode mode, int width = 128, int height = 96)
{
    RuntimeOptions options;
    options.windowSettings.width = width;
    options.windowSettings.height = height;
    options.spatialSettings.renderCulling = mode;
    return options;
}

struct VisualCapture
{
    std::array<PixelSnapshot, 5> frames;
    std::array<size_t, 5> submitted{};
    std::array<size_t, 5> candidates{};
    std::array<size_t, 5> visible{};
    std::array<bool, 5> visibilityKnown{};
    std::string renderer;
    int outputWidth = 0;
    int outputHeight = 0;
    std::vector<size_t> lastSubmittedOrder;
};

VisualCapture captureTransparentOrderAndMutation(RenderCullingMode mode)
{
    VisualCapture result;
    platformator::Runtime runtime(renderOptions(mode));
    platformator_detail::RuntimeAccess::window()->setImageValidationEnabled(true);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "Image parity fixture owns the software renderer");
    auto red = solidTexture(renderer, 255, 0, 0, 128);
    auto green = solidTexture(renderer, 0, 255, 0, 128);
    auto blue = splitTexture(renderer);
    auto *cameraObject = runtime.createGameObject()->setPosition({0, 0});
    cameraObject->addComponent<Camera>(128.0f, 96.0f);

    auto *first = runtime.createGameObject()->setPosition({64, 48})->setRotation(0.12f);
    first->addComponent<Sprite>(red.get(), SDL_FLIP_NONE, 54.0f, 40.0f);
    auto *firstSprite = first->getComponent<Sprite>();
    first->addComponent<BoxCollider>(8.0f, 8.0f);
    first->getComponent<BoxCollider>()->setOffset({500.0f, 0.0f});
    auto *removed = runtime.createGameObject()->setPosition({64, 48})->setRotation(-0.19f);
    removed->addComponent<Sprite>(green.get(), SDL_FLIP_HORIZONTAL, 44.0f, 36.0f);
    auto *removedSprite = removed->getComponent<Sprite>();
    removedSprite->setSourceRect(SDL_FRect{0.0f, 0.0f, 4.0f, 4.0f});
    auto *last = runtime.createGameObject()->setPosition({64, 48})->setScale({1.25f, 0.8f})->setRotation(0.33f);
    last->addComponent<Sprite>(blue.get(), SDL_FLIP_HORIZONTAL, 36.0f, 28.0f);
    auto *lastSprite = last->getComponent<Sprite>();
    lastSprite->setSourceRect(SDL_FRect{4.0f, 0.0f, 4.0f, 4.0f});

    auto *edge = runtime.createGameObject()->setPosition({126, 48})->setRotation(0.7853982f);
    edge->addComponent<Sprite>(red.get(), SDL_FLIP_NONE, 24.0f, 12.0f);

    auto frame = [&](size_t index) {
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::renderSubmittedWindowIndices.clear();
#endif
        require(runtime.simulateAndRenderFrame(dt), "Image fixture renders the current frame");
        result.frames[index] = spatial_test_support::readRendererPixels(renderer);
        const auto &stats = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
        result.submitted[index] = stats.submittedCount;
        result.candidates[index] = stats.candidateCount;
        result.visible[index] = stats.visibleCount;
        result.visibilityKnown[index] = stats.visibilityKnown;
#if PLATFORMATOR_ENABLE_WORK_PROBES
        if (index == 3)
            result.lastSubmittedOrder = platformator_test::renderSubmittedWindowIndices;
#endif
    };
    frame(0);
    const AnimationClip resizedClip({AnimationFrame(red.get(), 0.0f)}, 120.0, true, 64.0f, 32.0f,
                                    "render-culling-size-change");
    first->addComponent<Animator>();
    require(first->getComponent<Animator>()->play(&resizedClip),
            "Animator applies the resized destination dimensions to the existing Sprite");
    frame(1);
    removed->removeComponent(SPRITE); // Window membership uses swap-with-last removal.
    frame(2);
    first->setActive(false);
    frame(3);
    first->setActive(true);
    frame(4);

    result.renderer = SDL_GetRendererName(renderer) ? SDL_GetRendererName(renderer) : "unknown";
    auto *window = platformator_detail::RuntimeAccess::gameManager().getWindow();
    result.outputWidth = window->getRenderWidth();
    result.outputHeight = window->getRenderHeight();
    require(runtime.getMainCamera() != nullptr, "Image parity records a real camera and output renderer");
    runtime.clearScene(); // Release Sprite references before the local TextureWrappers destruct.
    return result;
}

struct SparseCapture
{
    PixelSnapshot pixels;
    RenderCullingFrameStats stats;
    std::vector<size_t> submittedWindowIndices;
    std::string renderer;
};

SparseCapture captureSparseThousand(RenderCullingMode mode)
{
    SparseCapture result;
    platformator::Runtime runtime(renderOptions(mode));
    platformator_detail::RuntimeAccess::window()->setImageValidationEnabled(true);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "Sparse fixture owns the software renderer");
    auto texture = solidTexture(renderer, 48, 96, 240, 255);
    runtime.createGameObject()->setPosition({0, 0})->addComponent<Camera>(128.0f, 96.0f);
    for (int i = 0; i < 1000; ++i)
    {
        const bool visible = i < 10;
        const float x = visible ? 8.0f + static_cast<float>(i) * 11.0f : 1000.0f + static_cast<float>(i) * 12.0f;
        auto *object = runtime.createGameObject()->setPosition({x, 48.0f});
        if (i == 0)
        {
            object->addComponent<BoxCollider>(4.0f, 4.0f);
            object->getComponent<BoxCollider>()->setOffset({500.0f, 0.0f});
        }
        object->addComponent<Sprite>(texture.get(), SDL_FLIP_NONE, 8.0f, 8.0f);
    }
#if PLATFORMATOR_ENABLE_WORK_PROBES
    platformator_test::renderSubmittedWindowIndices.clear();
#endif
    require(runtime.simulateAndRenderFrame(dt), "Sparse 1,000-sprite fixture renders one frame");
    const auto proof = platformator_detail::RuntimeAccess::window()->getImageValidationProof();
    require(proof.at("status") == "passed" && proof.at("order_status") == "passed" &&
                proof.at("reference_hash") == proof.at("result_hash") && proof.at("frames") == 1,
            "Same-frame submit-all qualification retains matching pixel hashes and ordered submission proof");
    result.pixels = spatial_test_support::readRendererPixels(renderer);
    result.renderer = SDL_GetRendererName(renderer) ? SDL_GetRendererName(renderer) : "unknown";
    result.stats = platformator_detail::RuntimeAccess::gameManager()
                       .getWindow()
                       ->getRenderCullingStats();
#if PLATFORMATOR_ENABLE_WORK_PROBES
    result.submittedWindowIndices = platformator_test::renderSubmittedWindowIndices;
#endif
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().empty(),
            "Sprite visibility, including sprite/collider bounds mismatch, creates no gameplay contacts");
    runtime.clearScene();
    return result;
}

void imagesAreModeIndependent()
{
    const std::array modes{RenderCullingMode::None, RenderCullingMode::Linear,
                           RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented};
    std::array<VisualCapture, modes.size()> captures;
    for (size_t i = 0; i < modes.size(); ++i)
        captures[i] = captureTransparentOrderAndMutation(modes[i]);

    for (size_t i = 1; i < captures.size(); ++i)
    {
        require(captures[i].frames == captures[0].frames,
                "All render modes produce identical software pixels for transparent overlap, source/flip, "
                "scaling/rotation, animation-size, swap removal and reactivation fixtures");
        require(captures[i].submitted == captures[0].submitted,
                "Each mode submits the same active sprite count for each mutation checkpoint");
        require(captures[i].renderer == captures[0].renderer && captures[i].outputWidth == captures[0].outputWidth &&
                    captures[i].outputHeight == captures[0].outputHeight,
                "Pixel comparisons use the same recorded software renderer and output dimensions");
    }
    const auto evidence = evidenceDirectory();
    if (!evidence.empty())
    {
        std::ofstream summary(evidence / "image-captures.csv");
        require(static_cast<bool>(summary), "Render image summary can be created");
        summary << "mode,renderer,width,height,frame,candidates,visible,visibility_known,submitted,fnv1a64\n";
        std::ofstream diffs(evidence / "pixel-diffs.csv");
        require(static_cast<bool>(diffs), "Render pixel diff summary can be created");
        diffs << "mode,reference,different_bytes\n";
        const std::array<std::string, modes.size()> names{"none", "linear", "bvh", "segmented"};
        for (size_t mode = 0; mode < captures.size(); ++mode)
            for (size_t frameIndex = 0; frameIndex < captures[mode].frames.size(); ++frameIndex)
            {
                const auto filename = names[mode] + "-frame-" + std::to_string(frameIndex) + ".ppm";
                savePpm(evidence / filename, captures[mode].frames[frameIndex]);
                summary << names[mode] << ',' << captures[mode].renderer << ',' << captures[mode].outputWidth << ','
                        << captures[mode].outputHeight << ',' << frameIndex << ','
                        << captures[mode].candidates[frameIndex] << ',' << captures[mode].visible[frameIndex] << ','
                        << (captures[mode].visibilityKnown[frameIndex] ? 1 : 0) << ','
                        << captures[mode].submitted[frameIndex] << ',' << std::hex
                        << pixelHash(captures[mode].frames[frameIndex]) << std::dec << '\n';
            }
        for (size_t mode = 1; mode < captures.size(); ++mode)
            diffs << names[mode] << ",none,0\n";
    }
#if PLATFORMATOR_ENABLE_WORK_PROBES
    for (size_t i = 1; i < captures.size(); ++i)
        require(captures[i].lastSubmittedOrder == captures[0].lastSubmittedOrder,
                "Active sprites preserve draw order after swap removal and reactivation");
#endif
}

void sparseFixtureSubmitsTenOnce()
{
    const std::array modes{RenderCullingMode::None, RenderCullingMode::Linear,
                           RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented};
    std::array<SparseCapture, modes.size()> captures;
    for (size_t i = 0; i < modes.size(); ++i)
        captures[i] = captureSparseThousand(modes[i]);
    for (size_t i = 1; i < captures.size(); ++i)
        require(captures[i].pixels == captures[0].pixels,
                "Sparse 10-of-1,000 scene has identical software output in all four modes");
    const auto evidence = evidenceDirectory();
    if (!evidence.empty())
    {
        std::ofstream summary(evidence / "sparse-captures.csv");
        require(static_cast<bool>(summary), "Sparse render summary can be created");
        summary << "mode,renderer,width,height,candidates,visible,visibility_known,submitted,pixel_hash,submitted_indices\n";
        const std::array<std::string, modes.size()> names{"none", "linear", "bvh", "segmented"};
        for (size_t mode = 0; mode < captures.size(); ++mode)
        {
            savePpm(evidence / (names[mode] + "-sparse.ppm"), captures[mode].pixels);
            summary << names[mode] << ',' << captures[mode].renderer << ',' << captures[mode].pixels.width << ','
                    << captures[mode].pixels.height << ',' << captures[mode].stats.candidateCount << ','
                    << captures[mode].stats.visibleCount << ',' << (captures[mode].stats.visibilityKnown ? 1 : 0) << ','
                    << captures[mode].stats.submittedCount << ',' << std::hex << pixelHash(captures[mode].pixels)
                    << std::dec << ",\"";
            for (size_t i = 0; i < captures[mode].submittedWindowIndices.size(); ++i)
            {
                if (i)
                    summary << ';';
                summary << captures[mode].submittedWindowIndices[i];
            }
            summary << '"' << '\n';
        }
    }

    require(captures[0].stats.submittedCount == 1000,
            "The none control path submits its complete active sprite vector");
    for (size_t i = 1; i < captures.size(); ++i)
        require(captures[i].stats.submittedCount == 10,
                "Each selected visibility index submits exactly ten visible sprite-only objects once");
#if PLATFORMATOR_ENABLE_WORK_PROBES
    require(captures[0].submittedWindowIndices.size() == 1000,
            "The none control path preserves all 1,000 active sprite indices");
    for (size_t i = 0; i < captures[0].submittedWindowIndices.size(); ++i)
        require(captures[0].submittedWindowIndices[i] == i, "The none path retains vector order");
    for (size_t mode = 1; mode < captures.size(); ++mode)
        require(captures[mode].submittedWindowIndices == std::vector<size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
                "Sparse selected submissions retain exactly the visible IDs in reference order");
#endif
    require(captures[0].stats.submittedCount == 1000 && captures[0].stats.candidateCount == 1000,
            "Submit-all baseline reports all candidates while preserving the direct submission control");
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        imagesAreModeIndependent();
        sparseFixtureSubmitsTenOnce();
        std::cout << "Render culling software image regressions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
