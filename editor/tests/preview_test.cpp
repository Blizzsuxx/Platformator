#include "document.h"
#include "editorapp.h"
#include "editorwork.h"
#include "preview.h"
#include "test_support.h"
#include "validation.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>
#include <platformator/camera.h>
#include <platformator/animationclip.h>
#include <platformator/texturewrapper.h>

#include <cmath>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace platformator_editor;

namespace platformator_editor
{
void run_viewport_cases();
}

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Json object_record(int id, std::string name, float x, float y, Json components = Json::array(),
                   Json children = Json::array(), bool active = true,
                   float scaleX = 1.0f, float scaleY = 1.0f, float rotation = 0.0f)
{
    return Json{{"id", id}, {"name", std::move(name)}, {"tag", ""}, {"active", active},
                {"position", {{"x", x}, {"y", y}}}, {"scale", {{"x", scaleX}, {"y", scaleY}}},
                {"rotation", rotation}, {"children", std::move(children)},
                {"components", std::move(components)}};
}

Json component_record(int id, int type, Json fields)
{
    fields["id"] = id;
    fields["type"] = type;
    return fields;
}

Json sprite_component(int componentId, std::string texture, int flip = SDL_FLIP_NONE)
{
    return component_record(componentId, 6,
        Json{{"textureFilePath", std::move(texture)}, {"flip", flip}, {"width", 50.0},
             {"height", 50.0}, {"sourceRectEnabled", false}, {"sourceRect", nullptr}});
}

class TemporaryAssets
{
  public:
    TemporaryAssets()
    {
        root = std::filesystem::temp_directory_path() /
               ("platformator-editor-preview-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto sourceRoot = std::filesystem::path(PLATFORMATOR_EDITOR_TEST_SOURCE_DIR);
        std::filesystem::create_directories(root / "assets" / "textures");
        std::filesystem::create_directories(root / "assets" / "animations");
        for (const char *file : {"black_square.png", "red_circle.png"})
            std::filesystem::copy_file(sourceRoot / "assets" / "textures" / file,
                                       root / "assets" / "textures" / file);
        SDL_Surface *asymmetric = SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_RGBA32);
        if (asymmetric == nullptr)
            throw std::runtime_error(std::string("could not create asymmetric texture fixture: ") + SDL_GetError());
        const SDL_Rect left{0, 0, 1, 1};
        const SDL_Rect right{1, 0, 1, 1};
        const bool pixelsFilled = SDL_FillSurfaceRect(asymmetric, &left, SDL_MapSurfaceRGBA(asymmetric, 255, 0, 0, 255)) &&
                                  SDL_FillSurfaceRect(asymmetric, &right, SDL_MapSurfaceRGBA(asymmetric, 0, 0, 255, 255));
        const bool imageSaved = pixelsFilled && IMG_SavePNG(asymmetric,
            (root / "assets" / "textures" / "asymmetric.png").string().c_str());
        SDL_DestroySurface(asymmetric);
        if (!imageSaved)
            throw std::runtime_error(std::string("could not save asymmetric texture fixture: ") + SDL_GetError());
        const Json clip{{"frames", Json::array({
                            Json{{"duration", 0.1}, {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}},
                                 {"hasSourceRect", false}, {"textureWrapperFilePath", "assets/textures/red_circle.png"}},
                            Json{{"duration", 0.1}, {"sourceRect", {{"x", 2.0}, {"y", 3.0}, {"w", 20.0}, {"h", 18.0}}},
                                 {"hasSourceRect", true}, {"textureWrapperFilePath", "assets/textures/black_square.png"}}})},
                          {"loop", true}, {"filePath", "assets/animations/preview.animset"}};
        std::ofstream output(root / "assets" / "animations" / "preview.animset");
        output << clip.dump(2);
        if (!output)
            throw std::runtime_error("could not write the temporary preview animation fixture");
    }

    ~TemporaryAssets()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
};

SDL_Surface *read_target(SDL_Renderer *renderer, SDL_Texture *target)
{
    require(SDL_SetRenderTarget(renderer, target), std::string("set preview readback target: ") + SDL_GetError());
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    require(surface != nullptr, std::string("read preview target: ") + SDL_GetError());
    if (surface->format != SDL_PIXELFORMAT_RGBA32)
    {
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        require(converted != nullptr, std::string("convert preview readback: ") + SDL_GetError());
        surface = converted;
    }
    return surface;
}

std::array<std::uint8_t, 4> pixel(SDL_Surface *surface, int x, int y);

void write_solid_png(const std::filesystem::path &path, std::uint8_t red,
                     std::uint8_t green, std::uint8_t blue)
{
    SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, std::string("create solid texture fixture: ") + SDL_GetError());
    const bool filled = SDL_FillSurfaceRect(surface, nullptr,
        SDL_MapSurfaceRGBA(surface, red, green, blue, 255));
    const bool saved = filled && IMG_SavePNG(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    require(saved, std::string("save solid texture fixture: ") + SDL_GetError());
}

Json animation_asset(std::string frameTexture, std::string filePath, std::string /*name*/)
{
    return Json{{"frames", Json::array({Json{{"duration", 0.1},
                                             {"sourceRect", { {"x", 0.0}, {"y", 0.0},
                                                               {"w", 0.0}, {"h", 0.0} }},
                                             {"hasSourceRect", false},
                                             {"textureWrapperFilePath", std::move(frameTexture)}}})},
                {"loop", true}, {"filePath", std::move(filePath)}};
}

void write_animation(const std::filesystem::path &path, const Json &animation)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << animation.dump(2);
    require(static_cast<bool>(output), "animation fixture must be written completely");
}

void wait_for_asset_preparation(EditorPreview &preview, const DocumentSession &document,
                                std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        preview.poll_asset_reloads(document);
        if (!preview.asset_preparation_pending())
        {
            preview.poll_asset_reloads(document);
            if (!preview.asset_preparation_pending())
                return;
        }
        SDL_Delay(1);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("timed out waiting for asynchronous preview asset preparation");
}

Json animator_component(int componentId, std::string animationPath)
{
    return component_record(componentId, 0,
        Json{{"currentFrameIndex", 0}, {"playing", false}, {"playbackSpeed", 1.0},
             {"animationClipFilePath", std::move(animationPath)}});
}

void check_preview_versioned_asset_reload()
{
    TemporaryAssets assets;
    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = (assets.root / "assets").string();
    platformator::Runtime runtime(options);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "asset reload fixture requires the SDL software renderer");

    DocumentSession document = make_default_document(assets.root.parent_path(), assets.root / "assets", 91);
    document.tabId = TabId{4};
    document.root.push_back(object_record(10, "Reloaded texture", 160.0f, 120.0f,
        Json::array({sprite_component(11, "assets/textures/asymmetric.png")})));
    document.structureRevision++;
    rebuild_document_indexes(document);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, document.assetRoot);
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_asset_preparation(preview, document);
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "initial preview texture must render");
    SDL_Surface *initial = read_target(renderer, preview.target());
    const auto initialPixel = pixel(initial, 80, 60);
    SDL_DestroySurface(initial);
    require(initialPixel[1] < 30,
            "initial asymmetric texture must have low green at its center");
    const Json sceneBeforeEditorHide = document.root;
    const auto revisionBeforeEditorHide = document.valueRevision;
    const auto structureBeforeEditorHide = document.structureRevision;
    const std::unordered_set<int> hiddenObjectIds{10};
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120, &hiddenObjectIds),
            "editor-only visibility changes must redraw the scene preview");
    SDL_Surface *hidden = read_target(renderer, preview.target());
    const auto hiddenPixel = pixel(hidden, 80, 60);
    SDL_DestroySurface(hidden);
    require(hiddenPixel[0] == 30 && hiddenPixel[1] == 32 && hiddenPixel[2] == 36 &&
                document.root == sceneBeforeEditorHide &&
                document.valueRevision == revisionBeforeEditorHide &&
                document.structureRevision == structureBeforeEditorHide,
            "a hidden editor object must disappear from Scene pixels without changing authored scene data");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120, &hiddenObjectIds),
            "an unchanged editor visibility filter must remain renderable");
    SDL_Surface *cachedHidden = read_target(renderer, preview.target());
    require(pixel(cachedHidden, 80, 60) == hiddenPixel,
            "the Scene render cache must include editor-only visibility state");
    SDL_DestroySurface(cachedHidden);
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "clearing editor-only visibility must restore the authored Scene render");
    SDL_Surface *restored = read_target(renderer, preview.target());
    require(pixel(restored, 80, 60) == initialPixel,
            "clearing editor-only visibility must restore the prior Scene pixels");
    SDL_DestroySurface(restored);
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto beforeStaticReuse = work::snapshot();
#endif
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "an unchanged Scene view must reuse the cached render target");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto afterStaticReuse = work::snapshot();
    require(afterStaticReuse[static_cast<std::size_t>(work::Counter::SceneCaptures)] ==
                beforeStaticReuse[static_cast<std::size_t>(work::Counter::SceneCaptures)] &&
                afterStaticReuse[static_cast<std::size_t>(work::Counter::SceneSubmissions)] ==
                beforeStaticReuse[static_cast<std::size_t>(work::Counter::SceneSubmissions)] &&
                afterStaticReuse[static_cast<std::size_t>(work::Counter::SceneCacheHits)] ==
                beforeStaticReuse[static_cast<std::size_t>(work::Counter::SceneCacheHits)] + 1,
            "an unchanged static Scene view must capture and submit zero additional sprites");
    work::reset();
#endif
    require(preview.render(0.0f, 0.0f, 100.0f, 80.0f, 160, 120),
            "a changed Scene view must redraw through conservative visible-bounds rejection");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto smallViewWork = work::snapshot();
    require(smallViewWork[static_cast<std::size_t>(work::Counter::SceneCaptures)] == 1 &&
                smallViewWork[static_cast<std::size_t>(work::Counter::SceneCulledSprites)] > 0 &&
                smallViewWork[static_cast<std::size_t>(work::Counter::SceneSubmissions)] <
                    smallViewWork[static_cast<std::size_t>(work::Counter::SceneCullTests)],
            "a dirty mostly-offscreen view must preserve order while rejecting offscreen sprites");
    work::reset();
#endif

    const auto texturePath = assets.root / "assets" / "textures" / "asymmetric.png";
    write_solid_png(texturePath, 230, 16, 8);
    auto consumers = resources.affected_consumers(texturePath);
    require(consumers.size() == 1, "one sprite proxy must consume the changed texture");
    preview.refresh_asset(document, texturePath, false, consumers);
    write_solid_png(texturePath, 8, 230, 16);
    resources.invalidate(texturePath);
    consumers = resources.affected_consumers(texturePath);
    preview.refresh_asset(document, texturePath, false, consumers);
    const auto wait_for_state = [&](ResourcePreparationState expected, const std::string &message) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            preview.poll_asset_reloads(document);
            const auto state = resources.resolve(texturePath, ResourceKind::Texture).preparation;
            if (state == expected)
                return;
            SDL_Delay(1);
        }
        const auto finalState = resources.resolve(texturePath, ResourceKind::Texture);
        throw std::runtime_error(message + " (state=" +
            std::to_string(static_cast<int>(finalState.preparation)) + ", error=" +
            finalState.preparationError + ", preview=" + preview.last_error() + ")");
    };
    wait_for_state(ResourcePreparationState::Ready,
                   "asynchronously prepared texture must reach its owner-thread commit");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the prepared texture version must render after owner-thread adoption");
    SDL_Surface *reloaded = read_target(renderer, preview.target());
    const auto reloadedPixel = pixel(reloaded, 80, 60);
    SDL_DestroySurface(reloaded);
    require(reloadedPixel[1] > reloadedPixel[0] * 4 && reloadedPixel[1] > reloadedPixel[2] * 4,
            "a successful version change must rebind only the dependent proxy to the new pixels");

    write_solid_png(texturePath, 8, 16, 230);
    consumers = resources.affected_consumers(texturePath);
    preview.refresh_asset(document, texturePath, false, consumers);
    const auto siblingOne = assets.root / "assets" / "textures" / "sibling-one.png";
    const auto siblingTwo = assets.root / "assets" / "textures" / "sibling-two.png";
    write_solid_png(siblingOne, 40, 50, 60);
    write_solid_png(siblingTwo, 60, 50, 40);
    resources.invalidate(siblingOne);
    resources.invalidate(siblingTwo);
    wait_for_state(ResourcePreparationState::Ready,
                   "a decode rejected after a sibling revision must reschedule its live consumers");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "a rescheduled texture version must render after owner-thread adoption");
    SDL_Surface *rescheduled = read_target(renderer, preview.target());
    const auto rescheduledPixel = pixel(rescheduled, 80, 60);
    SDL_DestroySurface(rescheduled);
    require(rescheduledPixel[2] > rescheduledPixel[0] * 4 &&
                rescheduledPixel[2] > rescheduledPixel[1] * 4,
            "rejecting stale prepared bytes must eventually adopt the latest on-disk texture");

    std::ofstream(texturePath, std::ios::binary | std::ios::trunc) << "invalid image";
    resources.invalidate(texturePath);
    consumers = resources.affected_consumers(texturePath);
    preview.refresh_asset(document, texturePath, false, consumers);
    wait_for_state(ResourcePreparationState::Failed,
                   "failed texture preparation must exhaust bounded retries");
    require(preview.object_info(10) != nullptr && preview.object_info(10)->textureAvailable &&
                !preview.object_info(10)->assetError.empty(),
            "a failed replacement must preserve the last-good texture and expose its load error");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the last-good texture must remain renderable after a failed replacement");
    SDL_Surface *lastGood = read_target(renderer, preview.target());
    const auto lastGoodPixel = pixel(lastGood, 80, 60);
    SDL_DestroySurface(lastGood);
    require(lastGoodPixel[2] > lastGoodPixel[0] * 4 && lastGoodPixel[2] > lastGoodPixel[1] * 4,
            "a failed replacement must leave the prior adopted pixels visible");

    write_solid_png(texturePath, 230, 8, 8);
    resources.invalidate(texturePath);
    consumers = resources.affected_consumers(texturePath);
    platformator_editor_detail::EditorPreviewAccess::set_proxy_texture_budget_for_testing(preview, 1);
    preview.refresh_asset(document, texturePath, false, consumers);
    wait_for_state(ResourcePreparationState::Failed,
                   "a texture version that exceeds the retained Scene budget must be rejected");
    require(preview.object_info(10) != nullptr && preview.object_info(10)->textureAvailable &&
                preview.object_info(10)->assetError.find("256 MiB") != std::string::npos,
            "an over-budget Scene texture must report an error and keep its last-good version");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the last-good texture must remain renderable after a memory-budget rejection");
    SDL_Surface *budgetLastGood = read_target(renderer, preview.target());
    const auto budgetPixel = pixel(budgetLastGood, 80, 60);
    SDL_DestroySurface(budgetLastGood);
    require(budgetPixel[2] > budgetPixel[0] * 4 && budgetPixel[2] > budgetPixel[1] * 4,
            "a rejected over-budget replacement must not change the presented pixels");

    preview.release();
    const auto unrelatedTexturePath = assets.root / "assets" / "textures" / "unrelated.png";
    write_solid_png(unrelatedTexturePath, 35, 140, 190);
    DocumentSession animationDocument = make_default_document(assets.root.parent_path(),
                                                               assets.root / "assets", 92);
    animationDocument.tabId = TabId{5};
    animationDocument.root.push_back(object_record(10, "Unrelated texture owner", 60.0f, 80.0f,
        Json::array({sprite_component(11, "assets/textures/unrelated.png")})));
    animationDocument.root.push_back(object_record(20, "Animation owner", 160.0f, 120.0f,
        Json::array({sprite_component(21, ""),
                     component_record(22, 0, Json{{"currentFrameIndex", 0}, {"playing", false},
                                                 {"playbackSpeed", 1.0},
                                                 {"animationClipFilePath", "assets/animations/preview.animset"}})})));
    animationDocument.structureRevision++;
    rebuild_document_indexes(animationDocument);
    ResourceIndex animationResources;
    animationResources.set_project(ProjectGeneration{animationDocument.projectGeneration},
                                   animationDocument.assetRoot);
    EditorPreview animationPreview(runtime);
    animationPreview.set_resource_index(&animationResources);
    animationPreview.reconcile(animationDocument, true);
    const auto unrelatedTextureVersion = animationResources.resolve(
        "assets/textures/unrelated.png", ResourceKind::Texture).logicalVersion;

    const auto clipPath = assets.root / "assets" / "animations" / "preview.animset";
    const auto clipLogicalPath = std::string("assets/animations/preview.animset");
    auto adoptedClip = runtime.loadAnimationClip(clipLogicalPath);
    require(adoptedClip != nullptr, "the preview fixture animation must be loaded before hot reload");
    const auto stagedTexturePath = assets.root / "assets" / "textures" / "staged-frame.png";
    write_solid_png(stagedTexturePath, 12, 220, 24);
    Json replacementClip;
    {
        std::ifstream input(clipPath);
        replacementClip = Json::parse(input);
    }
    replacementClip["frames"][0]["textureWrapperFilePath"] = "assets/textures/staged-frame.png";
    std::ofstream(clipPath, std::ios::trunc) << replacementClip.dump(2);
    animationResources.invalidate(clipPath);
    auto clipConsumers = animationResources.affected_consumers(clipPath);
    require(!clipConsumers.empty(), "the animation clip must retain its proxy consumers");
    animationPreview.refresh_asset(animationDocument, clipPath, false, clipConsumers);
    bool observedTextureStaging = false;
    const auto clipDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < clipDeadline)
    {
        animationPreview.poll_asset_reloads(animationDocument);
        const auto stagedState = animationResources.resolve("assets/textures/staged-frame.png",
                                                   ResourceKind::Texture).preparation;
        const auto clipState = animationResources.resolve(clipPath, ResourceKind::Animation).preparation;
        if (stagedState == ResourcePreparationState::Preparing &&
            clipState != ResourcePreparationState::Ready)
            observedTextureStaging = true;
        if (clipState == ResourcePreparationState::Ready)
            break;
        SDL_Delay(1);
    }
    require(observedTextureStaging &&
                animationResources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                animationResources.resolve("assets/textures/staged-frame.png", ResourceKind::Texture).preparation ==
                    ResourcePreparationState::Ready,
            "all referenced frame textures must be prepared before the replacement clip is committed");
    const auto refreshedClip = runtime.loadAnimationClip(clipLogicalPath);
    require(refreshedClip != nullptr && !refreshedClip->getFrames().empty() &&
                refreshedClip->getFrames().front().getTextureWrapper() != nullptr &&
                refreshedClip->getFrames().front().getTextureWrapper()->getFilePath() ==
                    "assets/textures/staged-frame.png",
            "the committed clip must bind its staged canonical frame texture");

    replacementClip["frames"][0]["textureWrapperFilePath"] = "assets/textures/missing-staged-frame.png";
    std::ofstream(clipPath, std::ios::trunc) << replacementClip.dump(2);
    animationResources.invalidate(clipPath);
    clipConsumers = animationResources.affected_consumers(clipPath);
    animationPreview.refresh_asset(animationDocument, clipPath, false, clipConsumers);
    const auto failedClipDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < failedClipDeadline &&
           animationResources.resolve(clipPath, ResourceKind::Animation).preparation != ResourcePreparationState::Failed)
    {
        animationPreview.poll_asset_reloads(animationDocument);
        SDL_Delay(1);
    }
    require(animationResources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Failed &&
                runtime.loadAnimationClip(clipLogicalPath) == refreshedClip,
            "a missing staged frame must expose failure while preserving the last-good animation wrapper");

    write_solid_png(assets.root / "assets" / "textures" / "missing-staged-frame.png", 90, 40, 180);
    require(animationPreview.retry_failed_assets(animationDocument) == 1,
            "Refresh Assets must schedule a failed referenced Scene clip without another filesystem event");
    const auto explicitRetryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < explicitRetryDeadline &&
           animationResources.resolve(clipPath, ResourceKind::Animation).preparation != ResourcePreparationState::Ready)
    {
        animationPreview.poll_asset_reloads(animationDocument);
        SDL_Delay(1);
    }
    const auto retriedClip = runtime.loadAnimationClip(clipLogicalPath);
    require(animationResources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                retriedClip != refreshedClip && retriedClip != nullptr &&
                retriedClip->getFrames().front().getTextureWrapper() != nullptr &&
                    retriedClip->getFrames().front().getTextureWrapper()->getFilePath() ==
                    "assets/textures/missing-staged-frame.png" &&
                animationResources.resolve("assets/textures/unrelated.png", ResourceKind::Texture).logicalVersion ==
                    unrelatedTextureVersion,
            "an explicit retry must refresh the repaired frame and leave unrelated Scene textures untouched");
    animationPreview.release();
}

void check_scene_directory_alias_event_reload()
{
    TemporaryAssets assets;
    const auto assetRoot = assets.root / "assets";
    const auto firstTarget = assetRoot / "scene-target-a";
    const auto secondTarget = assetRoot / "scene-target-b";
    const auto linksRoot = assetRoot / "scene-links";
    const auto middleAlias = linksRoot / "middle";
    const auto alias = assetRoot / "scene-alias";
    std::filesystem::create_directories(firstTarget);
    std::filesystem::create_directories(secondTarget);
    std::filesystem::create_directories(linksRoot);
    write_solid_png(firstTarget / "sprite.png", 230, 16, 8);
    write_solid_png(secondTarget / "sprite.png", 8, 230, 16);
    write_solid_png(firstTarget / "frame.png", 230, 180, 12);
    write_solid_png(secondTarget / "frame.png", 12, 45, 230);
    const std::string logicalClip = "assets/scene-alias/clip.animset";
    write_animation(firstTarget / "clip.animset",
                    animation_asset("assets/scene-alias/frame.png", logicalClip, "Alias A"));
    write_animation(secondTarget / "clip.animset",
                    animation_asset("assets/scene-alias/frame.png", logicalClip, "Alias B"));
    std::filesystem::create_directory_symlink("../scene-target-a", middleAlias);
    std::filesystem::create_directory_symlink("scene-links/middle", alias);

    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = assetRoot.string();
    platformator::Runtime runtime(options);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "directory alias Scene reload fixture requires an SDL renderer");

    DocumentSession document = make_default_document(assets.root.parent_path(), assetRoot, 96);
    document.tabId = TabId{19};
    document.root.push_back(object_record(10, "Aliased sprite", 160.0f, 120.0f,
        Json::array({sprite_component(11, "assets/scene-alias/sprite.png")})));
    document.root.push_back(object_record(20, "Aliased animation", 230.0f, 120.0f,
        Json::array({sprite_component(21, ""), animator_component(22, logicalClip)})));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, assetRoot);
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_asset_preparation(preview, document);
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "initial directory alias Scene content must render");
    SDL_Surface *initial = read_target(renderer, preview.target());
    const auto initialSprite = pixel(initial, 80, 60);
    const auto initialFrame = pixel(initial, 115, 60);
    SDL_DestroySurface(initial);
    require(initialSprite[0] > initialSprite[1] * 4 && initialFrame[0] > initialFrame[2] * 4,
            "the initial aliased sprite and animation frame must display target A pixels");

    const std::filesystem::path logicalSprite("assets/scene-alias/sprite.png");
    const std::filesystem::path logicalAnimation(logicalClip);
    const std::filesystem::path logicalFrame("assets/scene-alias/frame.png");
    const auto priorSprite = resources.resolve(logicalSprite, ResourceKind::Texture);
    const auto priorAnimation = resources.resolve(logicalAnimation, ResourceKind::Animation);
    const auto priorFrame = resources.resolve(logicalFrame, ResourceKind::Texture);
    const auto staleSpriteRequest = resources.begin_request(logicalSprite);
    const auto staleAnimationRequest = resources.begin_request(logicalAnimation);
    const auto staleFrameRequest = resources.begin_request(logicalFrame);
    const auto authoredDocumentBefore = document.root;
    const auto authoredRevisionBefore = document.valueRevision;
    require(priorSprite.status == ResourceStatus::Resolved &&
                priorAnimation.status == ResourceStatus::Resolved &&
                priorFrame.status == ResourceStatus::Resolved,
            "the Scene must retain logical alias dependencies for sprite, clip and frame assets");
    const auto unrelatedPath = assetRoot / "unrelated.png";
    write_solid_png(unrelatedPath, 40, 50, 60);
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    resources.register_consumer(unrelatedPath, ResourceKind::Texture,
                                {identity, 90, 91, "sprite.texture"});
    const auto unrelatedVersion = resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion;

    const auto settle_target = [&](const std::filesystem::path &target,
                                   std::chrono::seconds timeout, const std::string &message) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            preview.poll_asset_reloads(document);
            const auto sprite = resources.resolve(logicalSprite, ResourceKind::Texture);
            const auto animation = resources.resolve(logicalAnimation, ResourceKind::Animation);
            const auto frame = resources.resolve(logicalFrame, ResourceKind::Texture);
            if (sprite.preparation == ResourcePreparationState::Ready &&
                animation.preparation == ResourcePreparationState::Ready &&
                frame.preparation == ResourcePreparationState::Ready &&
                sprite.canonicalPath == std::filesystem::canonical(target / "sprite.png") &&
                animation.canonicalPath == std::filesystem::canonical(target / "clip.animset") &&
                frame.canonicalPath == std::filesystem::canonical(target / "frame.png"))
                return;
            SDL_Delay(1);
        }
        throw std::runtime_error(message);
    };
    const auto handle_parent_directory_event = [&] {
        // This is the same owner-thread notification dispatch used by the app
        // frame. The changed directory is the parent of a retained intermediate
        // link, while the Scene reaches that link through a separate outer alias.
        apply_project_resource_change(resources, preview, document, linksRoot, true);
    };

    const auto movedLinks = assetRoot / "scene-links-moved";
    std::filesystem::rename(linksRoot, movedLinks);
    handle_parent_directory_event();
    require(!resources.accepts(staleSpriteRequest) && !resources.accepts(staleAnimationRequest) &&
                !resources.accepts(staleFrameRequest),
            "a parent-directory rename must reject prepared Scene results through the old link chain");
    require(resources.resolve(logicalSprite, ResourceKind::Texture).status == ResourceStatus::BrokenSymlink &&
                resources.resolve(logicalAnimation, ResourceKind::Animation).status == ResourceStatus::BrokenSymlink &&
                resources.resolve(logicalFrame, ResourceKind::Texture).status == ResourceStatus::BrokenSymlink,
            "a missing intermediate parent must publish unresolved Scene resource status");

    std::filesystem::create_directories(linksRoot);
    std::filesystem::create_directory_symlink("../scene-target-b", middleAlias);
    handle_parent_directory_event();
    settle_target(secondTarget, std::chrono::seconds(3),
                  "parent-directory recreation must reload all Scene resources within three seconds");
    require(resources.resolve(logicalSprite, ResourceKind::Texture).logicalVersion > priorSprite.logicalVersion &&
                resources.resolve(logicalAnimation, ResourceKind::Animation).logicalVersion > priorAnimation.logicalVersion &&
                resources.resolve(logicalFrame, ResourceKind::Texture).logicalVersion > priorFrame.logicalVersion &&
                resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion == unrelatedVersion,
            "parent recreation must revise every descendant and leave unrelated Scene resources unchanged");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "retargeted directory alias Scene must render after owner-thread adoption");
    SDL_Surface *retargeted = read_target(renderer, preview.target());
    const auto retargetedSprite = pixel(retargeted, 80, 60);
    const auto retargetedFrame = pixel(retargeted, 115, 60);
    SDL_DestroySurface(retargeted);
    require(retargetedSprite[1] > retargetedSprite[0] * 4 &&
                retargetedFrame[2] > retargetedFrame[0] * 4,
            "the recreated-parent Scene must display the new sprite and animation frame pixels (sprite=" +
                std::to_string(retargetedSprite[0]) + "," + std::to_string(retargetedSprite[1]) + "," +
                std::to_string(retargetedSprite[2]) + "; frame=" +
                std::to_string(retargetedFrame[0]) + "," + std::to_string(retargetedFrame[1]) + "," +
                std::to_string(retargetedFrame[2]) + ")");

    std::filesystem::remove(middleAlias);
    handle_parent_directory_event();
    const auto brokenSprite = resources.resolve(logicalSprite, ResourceKind::Texture);
    const auto brokenAnimation = resources.resolve(logicalAnimation, ResourceKind::Animation);
    const auto brokenFrame = resources.resolve(logicalFrame, ResourceKind::Texture);
    require(brokenSprite.lastGoodCanonicalPath == std::filesystem::canonical(secondTarget / "sprite.png") &&
                brokenAnimation.lastGoodCanonicalPath == std::filesystem::canonical(secondTarget / "clip.animset") &&
                brokenFrame.lastGoodCanonicalPath == std::filesystem::canonical(secondTarget / "frame.png"),
            "unlinking the alias must retain last-good Scene resource bindings");

    std::filesystem::create_directory_symlink("../missing-target", middleAlias);
    handle_parent_directory_event();
    require(resources.resolve(logicalSprite, ResourceKind::Texture).status == ResourceStatus::BrokenSymlink &&
                resources.resolve(logicalAnimation, ResourceKind::Animation).status == ResourceStatus::BrokenSymlink &&
                resources.resolve(logicalFrame, ResourceKind::Texture).status == ResourceStatus::BrokenSymlink,
            "a broken directory alias must keep every referenced Scene resource visibly unresolved");
    std::filesystem::remove(middleAlias);
    std::filesystem::create_directory_symlink("../scene-target-a", middleAlias);
    handle_parent_directory_event();
    settle_target(firstTarget, std::chrono::seconds(3),
                  "repair after a broken directory alias must reload without a child write");
    require(resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion == unrelatedVersion,
            "broken-link repair must not revise an unrelated Scene resource");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "repaired directory alias Scene must render after owner-thread adoption");
    SDL_Surface *repaired = read_target(renderer, preview.target());
    const auto repairedSprite = pixel(repaired, 80, 60);
    const auto repairedFrame = pixel(repaired, 115, 60);
    SDL_DestroySurface(repaired);
    require(repairedSprite[0] > repairedSprite[1] * 4 && repairedFrame[0] > repairedFrame[2] * 4,
            "repair must restore target A sprite and frame pixels without refreshing unrelated content");

    const auto stagedLinks = assetRoot / "scene-links-next";
    const auto displacedLinks = assetRoot / "scene-links-old";
    std::filesystem::create_directories(stagedLinks);
    std::filesystem::create_directory_symlink("../scene-target-b", stagedLinks / "middle");
    std::filesystem::rename(linksRoot, displacedLinks);
    std::filesystem::rename(stagedLinks, linksRoot);
    handle_parent_directory_event();
    settle_target(secondTarget, std::chrono::seconds(3),
                  "replacing the intermediate parent must refresh Scene resources within three seconds");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "Scene content must render after intermediate parent replacement");
    SDL_Surface *parentReplacement = read_target(renderer, preview.target());
    const auto replacementSprite = pixel(parentReplacement, 80, 60);
    const auto replacementFrame = pixel(parentReplacement, 115, 60);
    SDL_DestroySurface(parentReplacement);
    require(replacementSprite[1] > replacementSprite[0] * 4 &&
                replacementFrame[2] > replacementFrame[0] * 4 &&
                resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion == unrelatedVersion,
            "parent replacement must adopt current sprite and clip-frame pixels without unrelated resource work "
            "(sprite=" + std::to_string(replacementSprite[0]) + "," +
                std::to_string(replacementSprite[1]) + "," + std::to_string(replacementSprite[2]) +
                "; frame=" + std::to_string(replacementFrame[0]) + "," +
                std::to_string(replacementFrame[1]) + "," + std::to_string(replacementFrame[2]) +
                "; unrelated=" + std::to_string(resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion) +
                "/" + std::to_string(unrelatedVersion) + ")");

    require(document.root == authoredDocumentBefore && document.valueRevision == authoredRevisionBefore,
            "parent replacement, rename/recreation and broken-link repair must not mutate authored Scene references");
    preview.release();
}

void check_scene_dispatch_repairs_first_demand_broken_alias()
{
    TemporaryAssets assets;
    const auto assetRoot = assets.root / "assets";
    const auto links = assetRoot / "links";
    const auto middle = links / "middle";
    const auto alias = assetRoot / "scene-alias";
    const auto target = assetRoot / "target";
    std::filesystem::create_directories(links);
    std::filesystem::create_directories(target);
    write_solid_png(target / "sprite.png", 230, 20, 12);
    write_solid_png(target / "frame.png", 14, 50, 230);
    const std::string logicalClip = "assets/scene-alias/clip.animset";
    write_animation(target / "clip.animset",
                    animation_asset("assets/scene-alias/frame.png", logicalClip, "Repaired alias"));
    std::filesystem::create_directory_symlink("links/middle", alias);

    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = assetRoot.string();
    platformator::Runtime runtime(options);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "first-demand alias repair requires an SDL renderer");

    DocumentSession document = make_default_document(assets.root.parent_path(), assetRoot, 97);
    document.tabId = TabId{20};
    document.root.push_back(object_record(10, "Initially broken sprite", 160.0f, 120.0f,
        Json::array({sprite_component(11, "assets/scene-alias/sprite.png")})));
    document.root.push_back(object_record(20, "Initially broken animation", 230.0f, 120.0f,
        Json::array({sprite_component(21, ""), animator_component(22, logicalClip)})));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, assetRoot);
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_asset_preparation(preview, document);
    const std::filesystem::path logicalSprite("assets/scene-alias/sprite.png");
    const std::filesystem::path logicalAnimation(logicalClip);
    const std::filesystem::path logicalFrame("assets/scene-alias/frame.png");
    require(resources.resolve(logicalSprite, ResourceKind::Texture).status == ResourceStatus::BrokenSymlink &&
                resources.resolve(logicalAnimation, ResourceKind::Animation).status == ResourceStatus::BrokenSymlink,
            "the Scene must publish first-demand broken status before any successful alias resolution");
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    const auto initialConsumers = resources.affected_consumers(middle, false);
    require(initialConsumers.size() == 2 &&
                std::all_of(initialConsumers.begin(), initialConsumers.end(), [&](const ResourceConsumer &consumer) {
                    return consumer.document == identity;
                }),
            "the application event path must find both broken Scene consumers through the missing link");

    const auto unrelatedPath = assetRoot / "unrelated.png";
    write_solid_png(unrelatedPath, 40, 50, 60);
    resources.register_consumer(unrelatedPath, ResourceKind::Texture,
                                {identity, 90, 91, "sprite.texture"});
    const auto unrelatedVersion = resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion;
    const auto authoredRoot = document.root;
    const auto authoredRevision = document.valueRevision;

    std::filesystem::create_directory_symlink("../target", middle);
    apply_project_resource_change(resources, preview, document, middle, false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        preview.poll_asset_reloads(document);
        const auto sprite = resources.resolve(logicalSprite, ResourceKind::Texture);
        const auto animation = resources.resolve(logicalAnimation, ResourceKind::Animation);
        const auto frame = resources.resolve(logicalFrame, ResourceKind::Texture);
        if (sprite.preparation == ResourcePreparationState::Ready &&
            animation.preparation == ResourcePreparationState::Ready &&
            frame.preparation == ResourcePreparationState::Ready &&
            sprite.canonicalPath == std::filesystem::canonical(target / "sprite.png") &&
            animation.canonicalPath == std::filesystem::canonical(target / "clip.animset") &&
            frame.canonicalPath == std::filesystem::canonical(target / "frame.png"))
            break;
        SDL_Delay(1);
    }
    require(resources.resolve(logicalSprite, ResourceKind::Texture).preparation == ResourcePreparationState::Ready &&
                resources.resolve(logicalAnimation, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                resources.resolve(logicalFrame, ResourceKind::Texture).preparation == ResourcePreparationState::Ready,
            "repairing only the intermediate link must refresh Scene resources within three seconds");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the repaired first-demand Scene alias must render after owner-thread adoption");
    SDL_Surface *rendered = read_target(renderer, preview.target());
    const auto spritePixel = pixel(rendered, 80, 60);
    const auto framePixel = pixel(rendered, 115, 60);
    SDL_DestroySurface(rendered);
    require(spritePixel[0] > spritePixel[1] * 4 && framePixel[2] > framePixel[0] * 4,
            "the repaired Scene must display newly reachable sprite and animation-frame pixels");
    require(resources.resolve(unrelatedPath, ResourceKind::Texture).logicalVersion == unrelatedVersion &&
                document.root == authoredRoot && document.valueRevision == authoredRevision,
            "first-demand alias repair must not reload unrelated resources or mutate authored Scene state");
    preview.release();
}

void check_scene_dispatch_repairs_first_demand_rejected_aliases()
{
    const auto verify_repair = [](const std::string &name, ResourceStatus rejectedStatus,
                                  bool cycle) {
        TemporaryAssets assets;
        const auto assetRoot = assets.root / "assets";
        const auto links = assetRoot / "links";
        const auto middle = links / "middle";
        const auto alias = assetRoot / "scene-alias";
        const auto target = assetRoot / "scene-target";
        const auto outside = assets.root / "outside";
        std::filesystem::create_directories(links);
        std::filesystem::create_directories(target);
        std::filesystem::create_directories(outside);
        write_solid_png(target / "sprite.png", 230, 20, 12);
        write_solid_png(target / "frame.png", 14, 50, 230);
        write_solid_png(outside / "sprite.png", 12, 230, 20);
        const std::string logicalClip = "assets/scene-alias/clip.animset";
        write_animation(target / "clip.animset",
                        animation_asset("assets/scene-alias/frame.png", logicalClip,
                                        "Repaired rejected alias"));
        std::filesystem::create_directory_symlink("links/middle", alias);
        std::filesystem::create_directory_symlink(cycle ? std::filesystem::path("../scene-alias") : outside,
                                                  middle);

        RuntimeOptions options;
        options.windowSettings.width = 320;
        options.windowSettings.height = 240;
        options.assetRoot = assetRoot.string();
        platformator::Runtime runtime(options);
        SDL_Renderer *renderer = runtime.getRenderer();
        require(renderer != nullptr, name + " alias repair fixture requires an SDL renderer");

        DocumentSession document = make_default_document(assets.root.parent_path(), assetRoot, 98);
        document.tabId = TabId{21};
        document.root.push_back(object_record(10, "Rejected sprite", 160.0f, 120.0f,
            Json::array({sprite_component(11, "assets/scene-alias/sprite.png")})));
        document.root.push_back(object_record(20, "Rejected animation", 230.0f, 120.0f,
            Json::array({sprite_component(21, ""), animator_component(22, logicalClip)})));
        ++document.structureRevision;
        rebuild_document_indexes(document);

        ResourceIndex resources;
        resources.set_project(ProjectGeneration{document.projectGeneration}, assetRoot);
        EditorPreview preview(runtime);
        preview.set_resource_index(&resources);
        preview.reconcile(document, true);
        wait_for_asset_preparation(preview, document);
        const std::filesystem::path logicalSprite("assets/scene-alias/sprite.png");
        const std::filesystem::path logicalAnimation(logicalClip);
        const std::filesystem::path logicalFrame("assets/scene-alias/frame.png");
        require(resources.resolve(logicalSprite, ResourceKind::Texture).status == rejectedStatus &&
                    resources.resolve(logicalAnimation, ResourceKind::Animation).status == rejectedStatus,
                name + " Scene paths must publish their rejected first-demand state");
        const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                        DocumentGeneration{document.documentGeneration}};
        const auto initialConsumers = resources.affected_consumers(middle, false);
        require(initialConsumers.size() == 2 &&
                    std::all_of(initialConsumers.begin(), initialConsumers.end(), [&](const ResourceConsumer &consumer) {
                        return consumer.document == identity;
                    }),
                name + " application dispatch must find both Scene consumers through the observed link");
        const auto staleSprite = resources.begin_request(logicalSprite);
        const auto staleAnimation = resources.begin_request(logicalAnimation);
        const auto unrelated = assetRoot / "unrelated.png";
        write_solid_png(unrelated, 40, 50, 60);
        resources.register_consumer(unrelated, ResourceKind::Texture,
                                    {identity, 90, 91, "sprite.texture"});
        const auto unrelatedVersion = resources.resolve(unrelated, ResourceKind::Texture).logicalVersion;
        const auto authoredRoot = document.root;
        const auto authoredRevision = document.valueRevision;

        std::filesystem::remove(middle);
        std::filesystem::create_directory_symlink("../scene-target", middle);
        apply_project_resource_change(resources, preview, document, middle, false);
        require(!resources.accepts(staleSprite) && !resources.accepts(staleAnimation),
                name + " repair dispatch must reject prepared results from the rejected versions");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            preview.poll_asset_reloads(document);
            const auto sprite = resources.resolve(logicalSprite, ResourceKind::Texture);
            const auto animation = resources.resolve(logicalAnimation, ResourceKind::Animation);
            const auto frame = resources.resolve(logicalFrame, ResourceKind::Texture);
            if (sprite.preparation == ResourcePreparationState::Ready &&
                animation.preparation == ResourcePreparationState::Ready &&
                frame.preparation == ResourcePreparationState::Ready)
                break;
            SDL_Delay(1);
        }
        require(resources.resolve(logicalSprite, ResourceKind::Texture).status == ResourceStatus::Resolved &&
                    resources.resolve(logicalSprite, ResourceKind::Texture).preparation == ResourcePreparationState::Ready &&
                    resources.resolve(logicalAnimation, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                    resources.resolve(logicalFrame, ResourceKind::Texture).preparation == ResourcePreparationState::Ready,
                name + " Scene resource dispatch must prepare repaired content within three seconds");
        require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
                name + " repaired Scene must render after owner-thread adoption");
        SDL_Surface *rendered = read_target(renderer, preview.target());
        const auto spritePixel = pixel(rendered, 80, 60);
        const auto framePixel = pixel(rendered, 115, 60);
        SDL_DestroySurface(rendered);
        require(spritePixel[0] > spritePixel[1] * 4 && framePixel[2] > framePixel[0] * 4,
                name + " repair must render only the newly contained sprite and frame pixels");
        require(resources.resolve(unrelated, ResourceKind::Texture).logicalVersion == unrelatedVersion &&
                    document.root == authoredRoot && document.valueRevision == authoredRevision,
                name + " repair must preserve unrelated resource versions and authored Scene state");
        preview.release();
    };

    verify_repair("cyclic", ResourceStatus::SymlinkCycle, true);
    verify_repair("escaping", ResourceStatus::OutsideRoot, false);
}

#if !defined(_WIN32)
void check_scene_dispatch_repairs_first_demand_unreadable_alias()
{
    TemporaryAssets assets;
    const auto assetRoot = assets.root / "assets";
    const auto links = assetRoot / "links";
    const auto middle = links / "middle";
    const auto alias = assetRoot / "scene-alias";
    const auto restricted = assetRoot / "restricted";
    const auto target = assetRoot / "scene-target";
    std::filesystem::create_directories(links);
    std::filesystem::create_directories(restricted);
    std::filesystem::create_directories(target);
    write_solid_png(restricted / "sprite.png", 230, 20, 12);
    write_solid_png(restricted / "frame.png", 14, 50, 230);
    const std::string logicalClip = "assets/scene-alias/clip.animset";
    write_animation(restricted / "clip.animset",
                    animation_asset("assets/scene-alias/frame.png", logicalClip,
                                    "Repaired unreadable alias"));
    write_solid_png(target / "sprite.png", 230, 20, 12);
    write_solid_png(target / "frame.png", 14, 50, 230);
    write_animation(target / "clip.animset",
                    animation_asset("assets/scene-alias/frame.png", logicalClip,
                                    "Repaired unreadable alias"));
    std::filesystem::create_directory_symlink("links/middle", alias);
    std::filesystem::create_directory_symlink("../restricted", middle);

    platformator_editor_test::ScopedDirectoryPermissions restriction(
        restricted, std::filesystem::perms::none);
    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = assetRoot.string();
    platformator::Runtime runtime(options);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "unreadable alias repair requires an SDL renderer");

    DocumentSession document = make_default_document(assets.root.parent_path(), assetRoot, 99);
    document.tabId = TabId{22};
    document.root.push_back(object_record(10, "Unreadable sprite", 160.0f, 120.0f,
        Json::array({sprite_component(11, "assets/scene-alias/sprite.png")})));
    document.root.push_back(object_record(20, "Unreadable animation", 230.0f, 120.0f,
        Json::array({sprite_component(21, ""), animator_component(22, logicalClip)})));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, assetRoot);
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_asset_preparation(preview, document);
    const std::filesystem::path logicalSprite("assets/scene-alias/sprite.png");
    const std::filesystem::path logicalAnimation(logicalClip);
    const std::filesystem::path logicalFrame("assets/scene-alias/frame.png");
    require(resources.resolve(logicalSprite, ResourceKind::Texture).status == ResourceStatus::Unreadable &&
                resources.resolve(logicalSprite, ResourceKind::Texture).canonicalPath.empty() &&
                resources.resolve(logicalAnimation, ResourceKind::Animation).status == ResourceStatus::Unreadable,
            "the real Scene must begin with inaccessible sprite and clip resources before any successful resolution");
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    const auto initialConsumers = resources.affected_consumers(middle, false);
    require(initialConsumers.size() == 2 &&
                std::all_of(initialConsumers.begin(), initialConsumers.end(), [&](const ResourceConsumer &consumer) {
                    return consumer.document == identity;
                }),
            "application dispatch must discover first-demand unreadable Scene consumers through the intermediate link");
    const auto staleSprite = resources.begin_request(logicalSprite);
    const auto staleAnimation = resources.begin_request(logicalAnimation);
    const auto unrelated = assetRoot / "unrelated.png";
    write_solid_png(unrelated, 40, 50, 60);
    resources.register_consumer(unrelated, ResourceKind::Texture,
                                {identity, 90, 91, "sprite.texture"});
    const auto unrelatedVersion = resources.resolve(unrelated).logicalVersion;
    const auto authoredRoot = document.root;
    const auto authoredRevision = document.valueRevision;

    std::filesystem::remove(middle);
    std::filesystem::create_directory_symlink("../scene-target", middle);
    apply_project_resource_change(resources, preview, document, middle, false);
    require(!resources.accepts(staleSprite) && !resources.accepts(staleAnimation),
            "unreadable alias recovery must reject stale prepared results before scheduling the new version");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        preview.poll_asset_reloads(document);
        const auto sprite = resources.resolve(logicalSprite, ResourceKind::Texture);
        const auto animation = resources.resolve(logicalAnimation, ResourceKind::Animation);
        const auto frame = resources.resolve(logicalFrame, ResourceKind::Texture);
        if (sprite.preparation == ResourcePreparationState::Ready &&
            animation.preparation == ResourcePreparationState::Ready &&
            frame.preparation == ResourcePreparationState::Ready)
            break;
        SDL_Delay(1);
    }
    require(resources.resolve(logicalSprite, ResourceKind::Texture).status == ResourceStatus::Resolved &&
                resources.resolve(logicalSprite, ResourceKind::Texture).preparation == ResourcePreparationState::Ready &&
                resources.resolve(logicalAnimation, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                resources.resolve(logicalFrame, ResourceKind::Texture).preparation == ResourcePreparationState::Ready,
            "Scene sprite, clip and frame texture must become current within three seconds of the link repair");
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "the repaired unreadable Scene alias must render after owner-thread adoption");
    SDL_Surface *rendered = read_target(renderer, preview.target());
    const auto spritePixel = pixel(rendered, 80, 60);
    const auto framePixel = pixel(rendered, 115, 60);
    SDL_DestroySurface(rendered);
    require(spritePixel[0] > spritePixel[1] * 4 && framePixel[2] > framePixel[0] * 4,
            "the repaired Scene must present current sprite and animation-frame pixels");
    require(resources.resolve(unrelated, ResourceKind::Texture).logicalVersion == unrelatedVersion &&
                document.root == authoredRoot && document.valueRevision == authoredRevision,
            "unreadable alias recovery must preserve unrelated resource versions and authored Scene content");
    preview.release();
}
#endif

void check_shared_animation_dependency_reload()
{
    TemporaryAssets assets;
    const auto sharedTexture = assets.root / "assets" / "textures" / "shared-frame.png";
    const auto sharedTextureAlias = assets.root / "assets" / "textures" / "shared-alias.png";
    const auto clipAPath = assets.root / "assets" / "animations" / "shared-a.animset";
    const auto clipBPath = assets.root / "assets" / "animations" / "shared-b.animset";
    write_solid_png(sharedTexture, 30, 80, 220);
    std::error_code aliasError;
    std::filesystem::create_symlink(sharedTexture.filename(), sharedTextureAlias, aliasError);
    const std::string sharedLogicalB = aliasError
        ? "assets/textures/shared-frame.png" : "assets/textures/shared-alias.png";
    write_animation(clipAPath, animation_asset("assets/textures/black_square.png",
                                               "assets/animations/shared-a.animset", "Shared A"));
    write_animation(clipBPath, animation_asset("assets/textures/red_circle.png",
                                               "assets/animations/shared-b.animset", "Shared B"));

    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = (assets.root / "assets").string();
    platformator::Runtime runtime(options);
    require(runtime.getRenderer() != nullptr, "shared animation fixture requires an SDL renderer");

    DocumentSession document = make_default_document(assets.root.parent_path(), assets.root / "assets", 93);
    document.tabId = TabId{16};
    document.root.push_back(object_record(10, "Direct texture", 50.0f, 50.0f,
        Json::array({sprite_component(11, "assets/textures/shared-frame.png")})));
    document.root.push_back(object_record(20, "Animation A", 100.0f, 100.0f,
        Json::array({sprite_component(21, ""),
                     animator_component(22, "assets/animations/shared-a.animset")})));
    document.root.push_back(object_record(30, "Animation B", 150.0f, 100.0f,
        Json::array({sprite_component(31, ""),
                     animator_component(32, "assets/animations/shared-b.animset")})));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, document.assetRoot);
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_asset_preparation(preview, document);
    const auto logicalA = std::string("assets/animations/shared-a.animset");
    const auto logicalB = std::string("assets/animations/shared-b.animset");
    auto oldA = runtime.loadAnimationClip(logicalA);
    auto oldB = runtime.loadAnimationClip(logicalB);
    require(oldA != nullptr && oldB != nullptr,
            "both last-good animation wrappers must be loaded before their shared dependency changes");

    const auto countersBeforeReload = work::snapshot();
    write_animation(clipAPath, animation_asset("assets/textures/shared-frame.png", logicalA, "Shared A"));
    write_animation(clipBPath, animation_asset(sharedLogicalB, logicalB, "Shared B"));
    resources.invalidate(clipAPath);
    preview.refresh_asset(document, clipAPath, false, resources.affected_consumers(clipAPath));
    resources.invalidate(clipBPath);
    preview.refresh_asset(document, clipBPath, false, resources.affected_consumers(clipBPath));
    resources.invalidate(sharedTexture);
    preview.refresh_asset(document, sharedTexture, false, resources.affected_consumers(sharedTexture));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        preview.poll_asset_reloads(document);
        const auto a = resources.resolve(clipAPath, ResourceKind::Animation).preparation;
        const auto b = resources.resolve(clipBPath, ResourceKind::Animation).preparation;
        const auto texture = resources.resolve(sharedTexture, ResourceKind::Texture).preparation;
        if (a == ResourcePreparationState::Ready && b == ResourcePreparationState::Ready &&
            texture == ResourcePreparationState::Ready)
            break;
        SDL_Delay(1);
    }
    require(resources.resolve(clipAPath, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                resources.resolve(clipBPath, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                resources.resolve(sharedTexture, ResourceKind::Texture).preparation == ResourcePreparationState::Ready,
            "two clips and a direct sprite must complete together when they share one texture version");
    wait_for_asset_preparation(preview, document);
    const auto counts = work::snapshot();
    const auto sharedDecodeCount = counts[static_cast<std::size_t>(work::Counter::ImageDecodes)] -
        countersBeforeReload[static_cast<std::size_t>(work::Counter::ImageDecodes)];
    require(sharedDecodeCount == 1,
            "shared sprite and clip demand must decode the same texture content version only once (observed " +
                std::to_string(sharedDecodeCount) + ")");
    require(runtime.loadAnimationClip(logicalA) != oldA && runtime.loadAnimationClip(logicalB) != oldB &&
                runtime.loadAnimationClip(logicalA)->getFrames().front().getTextureWrapper() != nullptr &&
                runtime.loadAnimationClip(logicalB)->getFrames().front().getTextureWrapper() != nullptr,
            "both clips must adopt their shared, already prepared frame texture");
    preview.release();
}

void check_animation_dependency_queue_recovery_and_stale_generation()
{
    TemporaryAssets assets;
    const auto clipPath = assets.root / "assets" / "animations" / "queue-retry.animset";
    const auto retryTexture = assets.root / "assets" / "textures" / "queue-retry.png";
    write_animation(clipPath, animation_asset("assets/textures/black_square.png",
                                               "assets/animations/queue-retry.animset", "Queue retry"));
    write_solid_png(retryTexture, 220, 30, 50);

    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = (assets.root / "assets").string();
    platformator::Runtime runtime(options);
    require(runtime.getRenderer() != nullptr, "queue recovery fixture requires an SDL renderer");
    DocumentSession document = make_default_document(assets.root.parent_path(), assets.root / "assets", 94);
    document.tabId = TabId{17};
    document.root.push_back(object_record(40, "Queue recovery", 120.0f, 100.0f,
        Json::array({sprite_component(41, ""),
                     animator_component(42, "assets/animations/queue-retry.animset")})));
    ++document.structureRevision;
    rebuild_document_indexes(document);

    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, document.assetRoot);
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_asset_preparation(preview, document);
    const auto logicalClip = std::string("assets/animations/queue-retry.animset");
    auto lastGood = runtime.loadAnimationClip(logicalClip);
    require(lastGood != nullptr, "queue recovery must retain an initial good clip");

    write_animation(clipPath, animation_asset("assets/textures/queue-retry.png", logicalClip, "Queue retry"));
    resources.invalidate(clipPath);
    preview.refresh_asset(document, clipPath, false, resources.affected_consumers(clipPath));
    platformator_editor_detail::EditorPreviewAccess::set_asset_reload_queue_limit_for_testing(preview, 0);
    const auto queuedTexture = assets.root / "assets" / "textures" / "queue-retry.png";
    bool observedQueueFailure = false;
    const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < failureDeadline)
    {
        preview.poll_asset_reloads(document);
        const auto state = resources.resolve(queuedTexture, ResourceKind::Texture);
        if (state.preparation == ResourcePreparationState::Failed &&
            state.preparationError.starts_with("Animation frame texture queue is full"))
        {
            observedQueueFailure = true;
            break;
        }
        SDL_Delay(1);
    }
    require(observedQueueFailure && runtime.loadAnimationClip(logicalClip) == lastGood,
            "a full preparation queue must expose a bounded retry state and retain the last-good clip");

    platformator_editor_detail::EditorPreviewAccess::set_asset_reload_queue_limit_for_testing(preview, 64);
    const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < retryDeadline)
    {
        preview.poll_asset_reloads(document);
        if (resources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
            resources.resolve(queuedTexture, ResourceKind::Texture).preparation == ResourcePreparationState::Ready)
            break;
        SDL_Delay(1);
    }
    require(resources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                resources.resolve(queuedTexture, ResourceKind::Texture).preparation == ResourcePreparationState::Ready &&
                runtime.loadAnimationClip(logicalClip) != lastGood,
            "deferred frame preparation must recover after queue capacity returns");

    auto adopted = runtime.loadAnimationClip(logicalClip);
    {
        std::ofstream oversized(clipPath, std::ios::binary | std::ios::trunc);
        oversized << std::string(1024U * 1024U + 1U, ' ');
        require(static_cast<bool>(oversized), "oversized animation fixture must be written");
    }
    resources.invalidate(clipPath);
    preview.refresh_asset(document, clipPath, false, resources.affected_consumers(clipPath));
    const auto oversizedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < oversizedDeadline &&
           resources.resolve(clipPath, ResourceKind::Animation).preparation != ResourcePreparationState::Failed)
    {
        preview.poll_asset_reloads(document);
        SDL_Delay(1);
    }
    require(resources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Failed &&
                runtime.loadAnimationClip(logicalClip) == adopted,
            "oversized animation descriptions must fail within the preview budget and retain the last-good clip");

    Json tooManyFrames = animation_asset("assets/textures/red_circle.png", logicalClip, "Too many frames");
    tooManyFrames["frames"] = Json::array();
    for (int frame = 0; frame < 513; ++frame)
        tooManyFrames["frames"].push_back(Json{{"duration", 0.0}, {"hasSourceRect", false},
            {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}},
            {"textureWrapperFilePath", "assets/textures/red_circle.png"}});
    write_animation(clipPath, tooManyFrames);
    resources.invalidate(clipPath);
    preview.refresh_asset(document, clipPath, false, resources.affected_consumers(clipPath));
    const auto frameLimitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < frameLimitDeadline &&
           resources.resolve(clipPath, ResourceKind::Animation).preparation != ResourcePreparationState::Failed)
    {
        preview.poll_asset_reloads(document);
        SDL_Delay(1);
    }
    require(resources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Failed &&
                runtime.loadAnimationClip(logicalClip) == adopted,
            "animation previews over 512 frames must fail explicitly and retain the last-good clip");

    const auto cancelledTexture = assets.root / "assets" / "textures" / "cancelled-frame.png";
    write_solid_png(cancelledTexture, 20, 180, 70);
    write_animation(clipPath, animation_asset("assets/textures/cancelled-frame.png", logicalClip,
                                               "Cancelled clip demand"));
    resources.invalidate(clipPath);
    preview.refresh_asset(document, clipPath, false, resources.affected_consumers(clipPath));
    platformator_editor_detail::EditorPreviewAccess::set_asset_reload_queue_limit_for_testing(preview, 0);
    bool observedCancellationQueueFailure = false;
    const auto cancelledDependencyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < cancelledDependencyDeadline)
    {
        preview.poll_asset_reloads(document);
        const auto state = resources.resolve(cancelledTexture, ResourceKind::Texture);
        if (state.preparation == ResourcePreparationState::Failed &&
            state.preparationError.starts_with("Animation frame texture queue is full"))
        {
            observedCancellationQueueFailure = true;
            break;
        }
        SDL_Delay(1);
    }
    require(observedCancellationQueueFailure,
            "cancel fixture must reach a bounded deferred state before its clip owner is removed");
    document.root[1]["components"].erase(document.root[1]["components"].begin() + 1);
    ++document.structureRevision;
    rebuild_document_indexes(document);
    preview.reconcile(document, true, {40});
    platformator_editor_detail::EditorPreviewAccess::set_asset_reload_queue_limit_for_testing(preview, 64);
    preview.poll_asset_reloads(document);
    require(resources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Failed &&
                resources.resolve(clipPath, ResourceKind::Animation).preparationError.find("no longer open") !=
                    std::string::npos &&
                runtime.loadAnimationClip(logicalClip) == adopted,
            "closing a clip while its shared dependency is deferred must release the clip demand and keep last-good content");
    document.root[1]["components"].push_back(animator_component(42, logicalClip));
    ++document.structureRevision;
    rebuild_document_indexes(document);
    preview.reconcile(document, true, {40});

    write_animation(clipPath, animation_asset("assets/textures/red_circle.png", logicalClip, "Stale generation"));
    resources.invalidate(clipPath);
    preview.refresh_asset(document, clipPath, false, resources.affected_consumers(clipPath));
    ++document.projectGeneration;
    resources.set_project(ProjectGeneration{document.projectGeneration}, document.assetRoot);
    preview.reconcile(document, false);
    const auto staleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < staleDeadline)
    {
        preview.poll_asset_reloads(document);
        SDL_Delay(1);
    }
    require(resources.resolve(clipPath, ResourceKind::Animation).preparation == ResourcePreparationState::Ready &&
                runtime.loadAnimationClip(logicalClip) == adopted,
            "a completed clip result from an old project generation must not replace current resources");
    preview.release();
}

#ifndef NDEBUG
void check_preview_reconcile_work_bounds(platformator::Runtime &runtime,
                                         const std::filesystem::path &projectRoot,
                                         const std::filesystem::path &assetRoot)
{
    const auto spriteLeaves = [&](int firstObjectId, int count, int firstComponentId) {
        Json children = Json::array();
        for (int offset = 0; offset < count; ++offset)
            children.push_back(object_record(firstObjectId + offset, "Leaf", 1.0f, 2.0f,
                Json::array({sprite_component(firstComponentId + offset,
                                               "assets/textures/black_square.png")})));
        return children;
    };

    Json branchA = object_record(1001, "Branch A", 0.0f, 0.0f,
                                 Json::array(), spriteLeaves(1002, 8, 13002));
    Json branchB = object_record(1100, "Branch B", 0.0f, 0.0f,
                                 Json::array(), spriteLeaves(1101, 8, 13101));
    Json rootA = object_record(1000, "Root A", 0.0f, 0.0f,
                               Json::array(), Json::array({branchA, branchB}));
    Json branchC = object_record(2001, "Branch C", 0.0f, 0.0f,
                                 Json::array(), spriteLeaves(2002, 8, 14002));
    Json rootB = object_record(2000, "Root B", 0.0f, 0.0f,
                               Json::array(), Json::array({branchC}));
    Json roots = Json::array({rootA, rootB});
    for (int id = 3000; id < 3032; ++id)
        roots.push_back(object_record(id, "Unrelated", 0.0f, 0.0f,
            Json::array({sprite_component(15000 + id, "assets/textures/black_square.png")})));

    auto parsed = parse_document(std::move(roots), 80, std::nullopt, projectRoot, assetRoot);
    require(static_cast<bool>(parsed), "reconcile-work fixture must parse: " + parsed.error);
    DocumentSession document = std::move(*parsed.document);
    EditorPreview preview(runtime);
    preview.reconcile(document, true);
    const std::size_t totalObjects = document.indexes.objectsById.size();
    require(totalObjects > 60, "fixture must have enough unrelated objects to expose a full scan");

    document.root[0]["children"][0]["children"][0]["position"]["x"] = 5.0;
    ++document.valueRevision;
    preview.reconcile(document, false, {1002});
    auto stats = preview.last_reconcile_stats();
    require(stats.visitedObjects == 2 && stats.visitedAuthoringObjects == 1 &&
                stats.visitedProxyObjects == 1 && stats.updatedProxies == 1,
            "a leaf change updates one authoring descriptor and one sprite proxy");

    document.root[0]["position"]["x"] = 7.0;
    ++document.valueRevision;
    preview.reconcile(document, false, {1000});
    stats = preview.last_reconcile_stats();
    require(stats.visitedObjects == 38 && stats.visitedAuthoringObjects == 19 &&
                stats.visitedProxyObjects == 19 && stats.updatedProxies == 16,
            "an ancestor change updates only its 19-object subtree and 16 sprite proxies");
    require(preview.object_info(1002) != nullptr && preview.object_info(1002)->worldX == 12.0f,
            "subtree traversal preserves accumulated parent translation");

    preview.reconcile(document, false, {1000, 1001, 1000});
    stats = preview.last_reconcile_stats();
    require(stats.visitedObjects == 38 && stats.visitedAuthoringObjects == 19 &&
                stats.visitedProxyObjects == 19 && stats.updatedProxies == 16,
            "duplicate and overlapping changed roots are coalesced into one local update");

    preview.reconcile(document, false, {1000, 2000});
    stats = preview.last_reconcile_stats();
    require(stats.visitedObjects == 58 && stats.visitedAuthoringObjects == 29 &&
                stats.visitedProxyObjects == 29 && stats.updatedProxies == 24,
            "disjoint changed roots update exactly their combined subtrees without unrelated objects");
    require(stats.visitedAuthoringObjects < totalObjects && stats.visitedProxyObjects < totalObjects,
            "authoring and proxy reconciliation must not visit unrelated document objects");
}

void check_preview_large_local_reconcile_work_bounds(platformator::Runtime &runtime,
                                                      const std::filesystem::path &projectRoot,
                                                      const std::filesystem::path &assetRoot)
{
    Json roots = Json::array();
    for (int index = 0; index < 10'000; ++index)
        roots.push_back(object_record(100'000 + index, "Unrelated " + std::to_string(index),
                                      0.0f, 0.0f));

    constexpr int depth = 128;
    Json chain = object_record(20'000 + depth - 1, "Deep sprite", 0.0f, 0.0f,
        Json::array({sprite_component(30'000 + depth - 1,
                                      "assets/textures/black_square.png")}));
    for (int index = depth - 2; index >= 0; --index)
        chain = object_record(20'000 + index, "Deep sprite " + std::to_string(index), 0.0f, 0.0f,
            Json::array({sprite_component(30'000 + index, "assets/textures/black_square.png")}),
            Json::array({std::move(chain)}));
    roots.push_back(std::move(chain));

    auto parsed = parse_document(std::move(roots), 81, std::nullopt, projectRoot, assetRoot);
    require(static_cast<bool>(parsed), "large local reconcile fixture must parse: " + parsed.error);
    DocumentSession document = std::move(*parsed.document);
    EditorPreview preview(runtime);
    preview.reconcile(document, true);
    const std::size_t totalObjects = document.indexes.objectsById.size();
    require(totalObjects == 10'000 + depth,
            "the workload oracle must include ten thousand unrelated objects and a depth-128 sprite chain");

    auto &deepRoot = document.root.back();
    deepRoot["position"]["x"] = 4.0;
    ++document.valueRevision;
    preview.reconcile(document, false, {20'000}, true); // Warm local descriptor and traversal scratch.
    work::reset();
    deepRoot["position"]["x"] = 8.0;
    ++document.valueRevision;
    preview.reconcile(document, false, {20'000}, true);
    auto stats = preview.last_reconcile_stats();
    require(stats.visitedAuthoringObjects == depth && stats.visitedProxyObjects == depth &&
                stats.updatedProxies == depth && stats.visitedAuthoringObjects < totalObjects / 50,
            "an inherited translation must update only the deep chain, not the ten thousand unrelated objects");
    require(preview.object_info(20'000 + depth - 1) != nullptr &&
                preview.object_info(20'000 + depth - 1)->worldX == 8.0f,
            "incremental authoring descriptors must retain accumulated translation through depth 128");
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
    const auto localWork = work::snapshot();
    require(localWork[static_cast<std::size_t>(work::Counter::AuthoringDescriptorInsertions)] == 0 &&
                localWork[static_cast<std::size_t>(work::Counter::AuthoringSubtreeScratchGrowths)] == 0,
            "a warmed subtree edit must neither allocate descriptor nodes nor grow traversal scratch");
#endif

    deepRoot["components"][0]["width"] = 48.0;
    ++document.valueRevision;
    preview.reconcile(document, false, {20'000}, false);
    stats = preview.last_reconcile_stats();
    require(stats.visitedAuthoringObjects == 1 && stats.visitedProxyObjects == 1 &&
                stats.updatedProxies == 1,
            "a local sprite geometry change must refresh its root without walking translation-independent children");
    require(preview.object_info(20'000 + depth - 1)->worldX == 8.0f,
            "a local root geometry update must preserve already-current child world positions");
    preview.release();
}
#endif

std::array<std::uint8_t, 4> pixel(SDL_Surface *surface, int x, int y)
{
    const auto *row = static_cast<const std::uint8_t *>(surface->pixels) + y * surface->pitch;
    const auto *rgba = row + x * 4;
    return {rgba[0], rgba[1], rgba[2], rgba[3]};
}

void check_preview_render_and_lifetimes()
{
    TemporaryAssets assets;
    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.windowSettings.headless = false;
    options.assetRoot = (assets.root / "assets").string();
    platformator::Runtime runtime(options);
    SDL_Renderer *renderer = runtime.getRenderer();
    require(renderer != nullptr, "preview fixture requires the SDL software renderer");
    Camera *mainCameraBefore = runtime.getMainCamera();
    require(mainCameraBefore == nullptr, "preview fixture must cover a Runtime with no gameplay camera");

    DocumentSession document = make_default_document(assets.root.parent_path(), assets.root / "assets", 7);
    const Json script = component_record(22, 5,
        Json{{"behaviors", Json::array({Json{{"type", "PreviewMustNotRun"}, {"velocity", 900.0}}})}});
    Json back = object_record(10, "Back", 160.0f, 120.0f,
                              Json::array({sprite_component(11, "assets/textures/red_circle.png"), script}));
    Json front = object_record(20, "Front", 160.0f, 120.0f,
                               Json::array({sprite_component(21, "assets/textures/black_square.png", SDL_FLIP_HORIZONTAL)}));
    Json nestedSprite = object_record(31, "Translated child", 10.0f, 12.0f,
        Json::array({sprite_component(32, "assets/textures/black_square.png")}), Json::array(), true, 2.0f, 1.0f,
        std::numbers::pi_v<float> / 4.0f);
    Json parent = object_record(30, "Parent", 20.0f, 30.0f, Json::array(), Json::array({nestedSprite}));
    Json inactive = object_record(40, "Inactive", 60.0f, 200.0f,
        Json::array({sprite_component(41, "assets/textures/black_square.png")}), Json::array(), false);
    Json animated = object_record(50, "Static animation", 240.0f, 120.0f,
        Json::array({sprite_component(51, ""),
                     component_record(52, 0, Json{{"currentFrameIndex", 1}, {"playing", false},
                                                  {"playbackSpeed", 10.0},
                                                  {"animationClipFilePath", "assets/animations/preview.animset"}})}));
    Json missing = object_record(60, "Missing texture", 280.0f, 120.0f,
        Json::array({sprite_component(61, "assets/not-present.png")}));
    Json flippedSprite = sprite_component(71, "assets/textures/asymmetric.png", SDL_FLIP_HORIZONTAL);
    flippedSprite["sourceRectEnabled"] = true;
    flippedSprite["sourceRect"] = {{"x", 0.0}, {"y", 0.0}, {"w", 2.0}, {"h", 1.0}};
    Json flipped = object_record(70, "Flipped source rectangle", 80.0f, 120.0f,
                                 Json::array({flippedSprite}));
    Json croppedSprite = sprite_component(81, "assets/textures/asymmetric.png");
    croppedSprite["sourceRectEnabled"] = true;
    croppedSprite["sourceRect"] = {{"x", 1.0}, {"y", 0.0}, {"w", 1.0}, {"h", 1.0}};
    Json cropped = object_record(80, "Source rectangle crop", 120.0f, 120.0f,
                                 Json::array({croppedSprite}));
    document.root.insert(document.root.end(), {back, front, parent, inactive, animated, missing, flipped, cropped});
    document.structureRevision++;
    rebuild_document_indexes(document);
    const Json authoredBefore = document.root;

    const std::size_t runtimeObjectsBefore = runtime.getAllGameObjects().size();
    ResourceIndex resources;
    resources.set_project(ProjectGeneration{document.projectGeneration}, document.assetRoot);
    EditorPreview preview(runtime);
    preview.set_resource_index(&resources);
    preview.reconcile(document, true);
    wait_for_asset_preparation(preview, document);
    const auto blackSquareConsumers = resources.affected_consumers(
        assets.root / "assets" / "textures" / "black_square.png");
    require(std::any_of(blackSquareConsumers.begin(), blackSquareConsumers.end(), [](const auto &consumer) {
                return consumer.objectId == 50 && consumer.role.starts_with("animation.frame.");
            }) && std::any_of(blackSquareConsumers.begin(), blackSquareConsumers.end(), [](const auto &consumer) {
                return consumer.objectId == 20 && consumer.role == "sprite.texture";
            }),
            "preview consumers must index direct textures and animation-frame textures by tab and object");
    require(resources.affected_consumers(assets.root / "assets" / "animations" / "preview.animset").size() == 1,
            "animation clips must be indexed against their preview owner");
    require(preview.ordered_document_ids() == std::vector<int>({10, 20, 31, 40, 50, 60, 70, 80}),
            "preview proxies must follow the document's stable ordered sprite IDs");
    require(preview.ordered_authoring_ids() == std::vector<int>({0, 10, 20, 30, 31, 40, 50, 60, 70, 80}) &&
                preview.object_info(30) != nullptr && preview.object_info(30)->emptyMarker &&
                preview.object_info(0) != nullptr && preview.object_info(0)->hasCamera,
            "authoring representations must cover camera and empty objects independently of Sprite proxies");
    require(runtime.getAllGameObjects().size() == runtimeObjectsBefore + 8,
            "preview must create one flattened Sprite proxy per authored sprite and no authored behavior objects");
    const auto *childInfo = preview.object_info(31);
    require(childInfo != nullptr && childInfo->worldX == 30.0f && childInfo->worldY == 42.0f &&
                childInfo->localScaleX == 2.0f && childInfo->localScaleY == 1.0f &&
                std::abs(childInfo->rotationDegrees - 45.0f) < 0.001f,
            "preview must accumulate parent translation and retain the engine's local scale/rotation semantics");
    document.root[3]["children"][0]["position"]["x"] = 15.0;
    document.valueRevision++;
    preview.reconcile(document, false, {31});
    require(preview.object_info(31) != nullptr && preview.object_info(31)->worldX == 35.0f,
            "value-only changes must update affected preview geometry without rebuilding proxy identities");
    require(preview.object_info(40) != nullptr && !preview.object_info(40)->active,
            "inactive objects remain represented for viewport picking while their proxies stay inactive");
    require(preview.object_info(50) != nullptr && preview.object_info(50)->textureAvailable,
            "static animator preview must load the selected frame without playback: " +
                (preview.object_info(50) == nullptr ? std::string("missing object") :
                 std::string("texture=") + std::to_string(preview.object_info(50)->textureAvailable) +
                     " error=" + preview.object_info(50)->assetError));
    require(preview.object_info(60) != nullptr && !preview.object_info(60)->textureAvailable &&
                !preview.object_info(60)->assetError.empty(),
            "missing assets must remain represented with a visible cached error");

    SDL_Texture *sentinelTarget = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                    SDL_TEXTUREACCESS_TARGET, 17, 13);
    require(sentinelTarget != nullptr, std::string("create renderer state sentinel: ") + SDL_GetError());
    require(SDL_SetRenderTarget(renderer, sentinelTarget), std::string("set renderer sentinel: ") + SDL_GetError());
    require(SDL_SetRenderLogicalPresentation(renderer, 17, 13, SDL_LOGICAL_PRESENTATION_LETTERBOX),
            std::string("set renderer logical presentation: ") + SDL_GetError());
    require(SDL_SetRenderScale(renderer, 1.25f, 1.5f), std::string("set renderer scale: ") + SDL_GetError());
    require(SDL_SetRenderDrawColor(renderer, 11, 22, 33, 44), std::string("set renderer color: ") + SDL_GetError());
    require(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD),
            std::string("set renderer blend mode: ") + SDL_GetError());
    const SDL_Rect originalViewport{1, 2, 11, 9};
    const SDL_Rect originalClip{2, 1, 8, 7};
    require(SDL_SetRenderViewport(renderer, &originalViewport), std::string("set renderer viewport: ") + SDL_GetError());
    require(SDL_SetRenderClipRect(renderer, &originalClip), std::string("set renderer clip: ") + SDL_GetError());
    require(SDL_SetRenderColorScale(renderer, 0.75f), std::string("set renderer color scale: ") + SDL_GetError());
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "preview must render to its reusable target");
    const auto liveResources = preview.resource_counts();
    require(liveResources.proxies == preview.ordered_document_ids().size() &&
                liveResources.texturedProxies > 0 && liveResources.uniqueTextureWrappers >= 2 &&
                liveResources.textureWrapperReferences >= liveResources.texturedProxies &&
                liveResources.renderTargetAllocated,
            "preview resource counts must report live proxies, shared texture wrappers/references and target: " +
                std::to_string(liveResources.proxies) + "/" +
                std::to_string(preview.ordered_document_ids().size()) + " ordered sprite proxies, " +
                std::to_string(liveResources.texturedProxies) + " textured, " +
                std::to_string(liveResources.uniqueTextureWrappers) + " wrappers, " +
                std::to_string(liveResources.textureWrapperReferences) + " references, target=" +
                std::to_string(liveResources.renderTargetAllocated));
    require(SDL_GetRenderTarget(renderer) == sentinelTarget,
            "preview must restore the caller's active render target");
    float scaleX = 0.0f, scaleY = 0.0f, red = 0.0f, green = 0.0f, blue = 0.0f, alpha = 0.0f, colorScale = 0.0f;
    SDL_Rect actualViewport{}, actualClip{};
    int logicalWidth = 0, logicalHeight = 0;
    SDL_RendererLogicalPresentation logicalMode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_BlendMode drawBlend = SDL_BLENDMODE_NONE;
    require(SDL_GetRenderScale(renderer, &scaleX, &scaleY) && scaleX == 1.25f && scaleY == 1.5f,
            "preview must restore renderer scale");
    require(SDL_GetRenderLogicalPresentation(renderer, &logicalWidth, &logicalHeight, &logicalMode) &&
                logicalWidth == 17 && logicalHeight == 13 && logicalMode == SDL_LOGICAL_PRESENTATION_LETTERBOX,
            "preview must restore logical presentation");
    require(SDL_GetRenderDrawBlendMode(renderer, &drawBlend) && drawBlend == SDL_BLENDMODE_ADD,
            "preview must restore the renderer draw blend mode");
    require(SDL_GetRenderDrawColorFloat(renderer, &red, &green, &blue, &alpha) &&
                std::abs(red - 11.0f / 255.0f) < 0.01f && std::abs(green - 22.0f / 255.0f) < 0.01f &&
                std::abs(blue - 33.0f / 255.0f) < 0.01f && std::abs(alpha - 44.0f / 255.0f) < 0.01f,
            "preview must restore renderer draw color");
    require(SDL_GetRenderViewport(renderer, &actualViewport) && std::memcmp(&actualViewport, &originalViewport, sizeof(SDL_Rect)) == 0,
            "preview must restore the viewport");
    require(SDL_GetRenderClipRect(renderer, &actualClip) && std::memcmp(&actualClip, &originalClip, sizeof(SDL_Rect)) == 0,
            "preview must restore the clip rectangle");
    require(SDL_GetRenderColorScale(renderer, &colorScale) && colorScale == 0.75f,
            "preview must restore the renderer color scale");

    SDL_Surface *rendered = read_target(renderer, preview.target());
    SDL_Surface *backOnly = nullptr;
    const auto centerWithFront = pixel(rendered, 80, 60);
    const auto flippedLeft = pixel(rendered, 34, 60);
    const auto flippedRight = pixel(rendered, 46, 60);
    const auto croppedCenter = pixel(rendered, 60, 60);
    const auto inactivePixel = pixel(rendered, 30, 100);
    SDL_DestroySurface(rendered);
    require(flippedLeft[2] > flippedLeft[0] && flippedRight[0] > flippedRight[2],
            "source rectangle rendering must honor a horizontal flip");
    require(croppedCenter[2] > croppedCenter[0],
            "a source rectangle must restrict submission to its authored texture region");
    require(inactivePixel[0] < 45 && inactivePixel[1] < 50 && inactivePixel[2] < 55,
            "inactive authored sprites must not submit into the preview target");
    require(SDL_SetRenderTarget(renderer, nullptr), std::string("release sentinel target: ") + SDL_GetError());

    document.root[2]["active"] = false;
    document.valueRevision++;
    rebuild_document_indexes(document);
    preview.reconcile(document, false, {20});
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 160, 120),
            "preview must render after changed-only active-state reconciliation");
    backOnly = read_target(renderer, preview.target());
    const auto centerWithoutFront = pixel(backOnly, 80, 60);
    SDL_DestroySurface(backOnly);
    require(centerWithFront != centerWithoutFront,
            "later sprites must overlap earlier sprites in the document's stable draw order");
    SDL_Texture *stableTarget = preview.target();
    require(!preview.render(0.0f, 0.0f, 320.0f, 240.0f, 0, 120) && preview.target() == stableTarget,
            "zero-sized preview extents must suspend drawing without reallocating the target");
    require(SDL_SetRenderTarget(renderer, stableTarget), std::string("activate preview target: ") + SDL_GetError());
    require(!preview.render(0.0f, 0.0f, 320.0f, 240.0f, 200, 120) && preview.target() == stableTarget,
            "preview must not destroy a target while it is still active on the renderer");
    require(SDL_SetRenderTarget(renderer, nullptr), std::string("deactivate preview target: ") + SDL_GetError());
    require(preview.render(0.0f, 0.0f, 320.0f, 240.0f, 200, 120) && preview.target() != stableTarget,
            "preview target growth must replace the prior target after it is safe to release");
    float targetWidth = 0.0f, targetHeight = 0.0f;
    require(SDL_GetTextureSize(preview.target(), &targetWidth, &targetHeight) &&
                targetWidth == 200.0f && targetHeight == 120.0f,
            "resized preview target must match its requested pixel extent");
    SDL_Texture *boundedTarget = preview.target();
    require(!preview.render(0.0f, 0.0f, 320.0f, 240.0f, 5000, 4000) &&
                preview.target() == boundedTarget &&
                preview.last_error().find("64 MiB") != std::string::npos,
            "an over-budget Scene target must be rejected before allocation and preserve the last target");
    require(document.root[1]["components"][1]["behaviors"][0]["velocity"] == 900.0 &&
                document.root[5]["components"][1]["currentFrameIndex"] == 1 &&
                authoredBefore[1]["components"][1]["behaviors"][0]["velocity"] == 900.0,
            "preview reconciliation must not execute authored behavior or animation playback");
    std::filesystem::copy_file(assets.root / "assets" / "textures" / "black_square.png",
                               assets.root / "assets" / "not-present.png");
    document.root[6]["name"] = "Asset appeared after cached failure";
    document.valueRevision++;
    preview.reconcile(document, false, {60});
    require(!preview.object_info(60)->textureAvailable,
            "failed asset loads must remain cached until the author changes or refreshes the asset");
    require(preview.retry_failed_assets(document) == 1,
            "explicit asset refresh must retry only the failed referenced resource");
    wait_for_asset_preparation(preview, document);
    require(preview.object_info(60)->textureAvailable && preview.object_info(60)->assetError.empty(),
            "explicit asset refresh must retry a formerly missing resource");
    require(runtime.getMainCamera() == mainCameraBefore,
            "preview must not create or mutate the gameplay camera");
    preview.release();
    require(resources.affected_consumers(assets.root / "assets" / "textures" / "black_square.png").empty() &&
                resources.affected_consumers(assets.root / "assets" / "animations" / "preview.animset").empty(),
            "releasing preview proxies must unregister every resource consumer");
    const auto releasedResources = preview.resource_counts();
    require(releasedResources.proxies == 0 && releasedResources.texturedProxies == 0 &&
                releasedResources.uniqueTextureWrappers == 0 &&
                releasedResources.textureWrapperReferences == 0 && !releasedResources.renderTargetAllocated,
            "preview resource counts must return to zero after release");
    require(runtime.getAllGameObjects().size() == runtimeObjectsBefore,
            "preview release must destroy all flattened proxies before Runtime teardown");
#ifndef NDEBUG
    check_preview_reconcile_work_bounds(runtime, assets.root.parent_path(), assets.root / "assets");
    check_preview_large_local_reconcile_work_bounds(runtime, assets.root.parent_path(),
                                                    assets.root / "assets");
#endif
    SDL_DestroyTexture(sentinelTarget);
}

void check_malformed_collider_preview_placeholders()
{
    TemporaryAssets assets;
    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.windowSettings.headless = false;
    options.assetRoot = (assets.root / "assets").string();
    platformator::Runtime runtime(options);
    require(runtime.getRenderer() != nullptr, "malformed-collider fixture requires the SDL renderer");

    Json nullSubtype = component_record(401, 3,
        Json{{"colliderType", nullptr}, {"width", 8.0}, {"height", 6.0},
             {"futureColliderPayload", Json{{"keep", "authored"}}}});
    Json stringSubtype = component_record(411, 3,
        Json{{"colliderType", "box"}, {"width", 8.0}, {"height", 6.0}});
    Json malformedWidth = component_record(421, 3,
        Json{{"colliderType", 1}, {"width", nullptr}, {"height", 6.0}});
    Json unrepresentableWidth = component_record(431, 3,
        Json{{"colliderType", 1}, {"width", 1.0e100}, {"height", 6.0}});
    Json authored = Json::array({
        object_record(400, "Null collider shape", 30.0f, 30.0f,
            Json::array({sprite_component(402, "assets/textures/black_square.png"), nullSubtype})),
        object_record(410, "String collider shape", 70.0f, 30.0f,
            Json::array({sprite_component(412, "assets/textures/black_square.png"), stringSubtype})),
        object_record(420, "Malformed collider width", 110.0f, 30.0f,
            Json::array({sprite_component(422, "assets/textures/black_square.png"), malformedWidth})),
        object_record(430, "Unrepresentable collider width", 150.0f, 30.0f,
            Json::array({sprite_component(432, "assets/textures/black_square.png"), unrepresentableWidth})),
    });
    auto parsed = parse_document(std::move(authored), 81, std::nullopt,
                                 assets.root.parent_path(), assets.root / "assets");
    require(static_cast<bool>(parsed), "malformed collider preview document must remain structurally loadable");
    DocumentSession document = std::move(*parsed.document);
    const Json savedContent = document.root;
    const auto valueRevision = document.valueRevision;
    const auto structureRevision = document.structureRevision;

    EditorPreview preview(runtime);
    preview.reconcile(document, true);
    const auto validation = validate_document(document);
    const auto hasIssue = [&](int objectId, const char *field) {
        return std::any_of(validation.issues.begin(), validation.issues.end(), [&](const ValidationIssue &issue) {
            return issue.objectId == objectId && issue.fieldPath == field;
        });
    };
    require(hasIssue(400, "colliderType") && hasIssue(410, "colliderType") &&
                hasIssue(420, "width") && hasIssue(430, "width"),
            "previewable malformed collider fields must retain their validation diagnostics");
    for (int objectId : {400, 410, 420, 430})
    {
        const auto *info = preview.object_info(objectId);
        require(info != nullptr && info->hasSprite && info->colliderPlaceholder,
                "preview reconciliation must preserve a visible authoring placeholder for malformed colliders");
        require(std::isfinite(info->nonSpriteBounds.x) && std::isfinite(info->nonSpriteBounds.y) &&
                    std::isfinite(info->nonSpriteBounds.width) && std::isfinite(info->nonSpriteBounds.height) &&
                    info->nonSpriteBounds.width > 0.0f && info->nonSpriteBounds.height > 0.0f,
                "malformed collider preview bounds must remain finite and pickable");
    }
    require(document.root == savedContent && document.valueRevision == valueRevision &&
                document.structureRevision == structureRevision &&
                document.root[0]["components"][1]["futureColliderPayload"] == Json{{"keep", "authored"}},
            "preview reconciliation must preserve saved content, unknown fields and document revisions");
    document.root[0]["components"][1]["colliderType"] = 1;
    document.root[0]["components"][1]["width"] = 12.0;
    document.root[0]["components"][1]["height"] = 10.0;
    ++document.valueRevision;
    preview.reconcile(document, false, {400}, false);
    const auto *repairedCollider = preview.object_info(400);
    require(repairedCollider != nullptr && !repairedCollider->colliderPlaceholder &&
                repairedCollider->colliderShape == PreviewColliderShape::Box &&
                repairedCollider->colliderWidth == 12.0f && repairedCollider->colliderHeight == 10.0f,
            "a collider-only edit must refresh local proxy geometry without copying Sprite or Animator JSON");
    preview.release();
}
} // namespace

int main()
{
    try
    {
        work::reset();
        check_preview_render_and_lifetimes();
        check_malformed_collider_preview_placeholders();
        check_preview_versioned_asset_reload();
        check_scene_directory_alias_event_reload();
        check_scene_dispatch_repairs_first_demand_broken_alias();
        check_scene_dispatch_repairs_first_demand_rejected_aliases();
#if !defined(_WIN32)
        check_scene_dispatch_repairs_first_demand_unreadable_alias();
#endif
        check_shared_animation_dependency_reload();
        check_animation_dependency_queue_recovery_and_stale_generation();
        platformator_editor::run_viewport_cases();
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
        const auto counts = work::snapshot();
        require(counts[static_cast<std::size_t>(work::Counter::ImageDecodes)] > 0 &&
                    counts[static_cast<std::size_t>(work::Counter::TextureUploads)] > 0 &&
                    counts[static_cast<std::size_t>(work::Counter::SceneSubmissions)] > 0 &&
                    counts[static_cast<std::size_t>(work::Counter::SceneCaptures)] > 0,
                "the preview workload must exercise decode, upload, submission and scene-capture counters (" +
                    std::to_string(counts[static_cast<std::size_t>(work::Counter::ImageDecodes)]) + "," +
                    std::to_string(counts[static_cast<std::size_t>(work::Counter::TextureUploads)]) + "," +
                    std::to_string(counts[static_cast<std::size_t>(work::Counter::SceneSubmissions)]) + "," +
                    std::to_string(counts[static_cast<std::size_t>(work::Counter::SceneCaptures)]) + ")");
#endif
        std::cout << "[PASS] platformator_editor_preview_test\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] platformator_editor_preview_test: " << error.what() << '\n';
        return 1;
    }
}
