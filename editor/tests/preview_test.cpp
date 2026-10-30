#include "document.h"
#include "preview.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>
#include <platformator/camera.h>

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
                            Json{{"duration", 0.0}, {"sourceRect", {{"x", 0.0}, {"y", 0.0}, {"w", 0.0}, {"h", 0.0}}},
                                 {"hasSourceRect", false}, {"textureWrapperFilePath", "assets/textures/red_circle.png"}},
                            Json{{"duration", 0.0}, {"sourceRect", {{"x", 2.0}, {"y", 3.0}, {"w", 20.0}, {"h", 18.0}}},
                                 {"hasSourceRect", true}, {"textureWrapperFilePath", "assets/textures/black_square.png"}}})},
                          {"framesPerSecond", 12}, {"loop", true}, {"width", 24}, {"height", 20},
                          {"name", "Preview Fixture"}, {"filePath", "assets/animations/preview.animset"}};
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
    EditorPreview preview(runtime);
    preview.reconcile(document, true);
    require(preview.ordered_document_ids() == std::vector<int>({10, 20, 31, 40, 50, 60, 70, 80}),
            "preview proxies must follow the document's stable ordered sprite IDs");
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
                (preview.object_info(50) == nullptr ? std::string("missing object") : preview.object_info(50)->assetError));
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
    preview.refresh_assets(document);
    require(preview.object_info(60)->textureAvailable && preview.object_info(60)->assetError.empty(),
            "explicit asset refresh must retry a formerly missing resource");
    require(runtime.getMainCamera() == mainCameraBefore,
            "preview must not create or mutate the gameplay camera");
    preview.release();
    require(runtime.getAllGameObjects().size() == runtimeObjectsBefore,
            "preview release must destroy all flattened proxies before Runtime teardown");
    SDL_DestroyTexture(sentinelTarget);
}
} // namespace

int main()
{
    try
    {
        check_preview_render_and_lifetimes();
        platformator_editor::run_viewport_cases();
        std::cout << "[PASS] platformator_editor_preview_test\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] platformator_editor_preview_test: " << error.what() << '\n';
        return 1;
    }
}
