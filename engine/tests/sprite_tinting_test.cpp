#include "runtime_work_probe.h"
#include "test_support.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <platformator/camera.h>
#include <platformator/runtime.h>
#include <platformator/sprite.h>
#include <platformator/texturewrapper.h>

namespace fs = std::filesystem;
using namespace test_support;

namespace
{
struct ScopedTempDir
{
    fs::path dir;
    explicit ScopedTempDir(const std::string &name) : dir(fs::temp_directory_path() / name)
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "assets" / "textures");
    }
    ~ScopedTempDir()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void writeSolidBmp(const fs::path &path, Uint8 r, Uint8 g, Uint8 b)
{
    fs::create_directories(path.parent_path());
    SDL_Surface *surface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGB24);
    require(surface != nullptr, std::string("SDL_CreateSurface failed: ") + SDL_GetError());
    const Uint32 pixel = SDL_MapSurfaceRGB(surface, r, g, b);
    require(SDL_FillSurfaceRect(surface, nullptr, pixel), std::string("SDL_FillSurfaceRect failed: ") + SDL_GetError());
    const bool saved = SDL_SaveBMP(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    require(saved, std::string("SDL_SaveBMP failed: ") + SDL_GetError());
}

struct SampleRequest
{
    int x = 0;
    int y = 0;
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 0;
};

struct ReadbackContext
{
    std::vector<SampleRequest> samples;
    SDL_Texture *checkTexture = nullptr;
    SDL_Color textureColorMod{0, 0, 0, 0};
    SDL_BlendMode textureBlendMode = SDL_BLENDMODE_INVALID;
};

SDL_Color renderAndCaptureSamples(platformator::Runtime &runtime, std::vector<SampleRequest> &samples,
                                  SDL_Texture *textureToInspect = nullptr,
                                  SDL_BlendMode *outBlendAfter = nullptr)
{
    ReadbackContext ctx;
    ctx.samples = samples;
    ctx.checkTexture = textureToInspect;

    platformator_test::RenderObservation obs;
    obs.readbackContext = &ctx;
    obs.beforePresent = [](SDL_Renderer *renderer, int width, int height, void *rawCtx) {
        auto *state = static_cast<ReadbackContext *>(rawCtx);
        SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
        require(surface != nullptr, std::string("SDL_RenderReadPixels failed: ") + SDL_GetError());
        for (auto &sample : state->samples)
        {
            require(sample.x >= 0 && sample.x < width && sample.y >= 0 && sample.y < height,
                    "Sample coordinates out of bounds");
            require(SDL_ReadSurfacePixel(surface, sample.x, sample.y, &sample.r, &sample.g, &sample.b, &sample.a),
                    std::string("SDL_ReadSurfacePixel failed: ") + SDL_GetError());
        }
        SDL_DestroySurface(surface);

        if (state->checkTexture != nullptr)
        {
            require(SDL_GetTextureColorMod(state->checkTexture, &state->textureColorMod.r,
                                           &state->textureColorMod.g, &state->textureColorMod.b),
                    "SDL_GetTextureColorMod failed");
            require(SDL_GetTextureAlphaMod(state->checkTexture, &state->textureColorMod.a),
                    "SDL_GetTextureAlphaMod failed");
            require(SDL_GetTextureBlendMode(state->checkTexture, &state->textureBlendMode),
                    "SDL_GetTextureBlendMode failed");
        }
    };

    platformator_test::ScopedRenderObservation scope(obs);
    require(runtime.simulateAndRenderFrame(dt), "simulateAndRenderFrame failed");
    require(platformator_test::work.renderReadbackCallbacks > 0 || samples.empty(),
            "Expected renderReadbackCallback invocation");
    samples = ctx.samples;
    if (outBlendAfter != nullptr)
        *outBlendAfter = ctx.textureBlendMode;
    return ctx.textureColorMod;
}

bool nearByte(Uint8 actual, Uint8 expected, int tolerance = 3)
{
    return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= tolerance;
}

void testSharedTextureRgbAlphaAndBlendRestoration()
{
    ScopedTempDir tmp("platformator_sprite_tinting_shared");
    writeSolidBmp(tmp.dir / "assets" / "textures" / "white_rgb24.bmp", 255, 255, 255);

    RuntimeOptions options;
    options.assetRoot = (tmp.dir / "assets").string();
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.windowSettings.headless = false;

    platformator::Runtime runtime(options);
    GameObject *camObj = runtime.createGameObject()->setName("Camera");
    camObj->addComponent<Camera>(320.0f, 240.0f);
    Camera *camera = camObj->getComponent<Camera>();
    camera->setCamera(SDL_FRect{0.0f, 0.0f, 320.0f, 240.0f});

    TextureWrapper *whiteTex = runtime.loadTexture("assets/textures/white_rgb24.bmp");
    require(whiteTex != nullptr && whiteTex->getTexture() != nullptr, "Failed to load white_rgb24.bmp");
    // Ensure starting blend mode is SDL_BLENDMODE_NONE to exercise RGB24 alpha blend promotion.
    require(SDL_SetTextureBlendMode(whiteTex->getTexture(), SDL_BLENDMODE_NONE),
            std::string("SDL_SetTextureBlendMode failed: ") + SDL_GetError());

    auto makeQuad = [&](const char *name, float cx, float cy, int32_t zOrder) {
        GameObject *obj = runtime.createGameObject()->setName(name);
        obj->setPosition({cx, cy});
        obj->addComponent<Sprite>(whiteTex, 40.0f, 40.0f);
        Sprite *sprite = obj->getComponent<Sprite>();
        sprite->setZOrder(zOrder);
        return sprite;
    };

    // 1. Red tinted sprite alone over white background
    Sprite *redOnly = makeQuad("RedOnly", 40.0f, 60.0f, 1);
    redOnly->setColor(SDL_Color{255, 0, 0, 255});
    require(redOnly->getColor().r == 255 && redOnly->getColor().g == 0 && redOnly->getColor().b == 0 &&
                redOnly->getAlpha() == 255,
            "Sprite::getColor/getAlpha must reflect setColor");

    // 2. Untinted sibling sharing whiteTex over a dark base quad (proves zero mod leak from #1)
    Sprite *darkBase2 = makeQuad("DarkBase2", 110.0f, 60.0f, 0);
    darkBase2->setColor(0, 0, 0, 255);
    Sprite *untintedSibling = makeQuad("UntintedSibling", 110.0f, 60.0f, 1);
    require(untintedSibling->getColor().r == 255 && untintedSibling->getColor().g == 255 &&
                untintedSibling->getColor().b == 255 && untintedSibling->getAlpha() == 255,
            "Default Sprite color must be opaque white (255,255,255,255)");

    // 3. Semi-transparent red sprite (a=128) using NONE-blend RGB24 texture over solid blue base quad
    Sprite *blueBase3 = makeQuad("BlueBase3", 180.0f, 60.0f, 0);
    blueBase3->setColor(0, 0, 255);
    Sprite *alphaRed = makeQuad("AlphaRed", 180.0f, 60.0f, 1);
    alphaRed->setColor(255, 0, 0, 128);

    // 4. Fully transparent (a=0) sprite over solid green base quad
    Sprite *greenBase4 = makeQuad("GreenBase4", 250.0f, 60.0f, 0);
    greenBase4->setColor(0, 255, 0, 255);
    Sprite *invisibleWhite = makeQuad("InvisibleWhite", 250.0f, 60.0f, 1);
    invisibleWhite->setAlpha(0);
    require(invisibleWhite->getAlpha() == 0, "Sprite::setAlpha must update alpha channel cleanly");

    std::vector<SampleRequest> samples = {
        {40, 60},   // redOnly
        {110, 60},  // untintedSibling
        {180, 60},  // alphaRed over blueBase3
        {250, 60},  // invisibleWhite over greenBase4
    };
    SDL_BlendMode blendAfter = SDL_BLENDMODE_INVALID;
    const SDL_Color modAfter =
        renderAndCaptureSamples(runtime, samples, whiteTex->getTexture(), &blendAfter);

    require(samples[0].r == 255 && samples[0].g == 0 && samples[0].b == 0,
            "Red tinted sprite should render pure red");
    require(samples[1].r == 255 && samples[1].g == 255 && samples[1].b == 255,
            "Untinted shared-texture sprite must not inherit previous sprite's red mod");
    require(nearByte(samples[2].r, 128) && nearByte(samples[2].g, 0) && nearByte(samples[2].b, 127),
            "Semi-transparent RGB24 sprite (255,0,0,128) over blue must blend with promoted blend mode (got " +
                std::to_string(samples[2].r) + "," + std::to_string(samples[2].g) + "," +
                std::to_string(samples[2].b) + ")");
    require(samples[3].r == 0 && samples[3].g == 255 && samples[3].b == 0,
            "Zero-alpha sprite must leave underlying green quad untouched");
    require(modAfter.r == 255 && modAfter.g == 255 && modAfter.b == 255 && modAfter.a == 255,
            "Shared SDL_Texture color/alpha mod must be restored to (255,255,255,255) after draw");
    require(blendAfter == SDL_BLENDMODE_NONE,
            "Shared SDL_Texture blend mode must be restored to SDL_BLENDMODE_NONE after alpha draw");

    // Verify clone() copies color without affecting source
    std::unique_ptr<Component> clonedComp(alphaRed->clone());
    auto *clonedSprite = dynamic_cast<Sprite *>(clonedComp.get());
    require(clonedSprite != nullptr, "Sprite::clone() must return a Sprite");
    require(clonedSprite->getColor().r == 255 && clonedSprite->getColor().g == 0 &&
                clonedSprite->getColor().b == 0 && clonedSprite->getColor().a == 128,
            "Sprite::clone() must preserve color");
    clonedSprite->setColor(10, 20, 30, 40);
    require(alphaRed->getColor().a == 128, "Mutating cloned Sprite color must not affect source");
}

void testZOrderAlphaCompositing()
{
    ScopedTempDir tmp("platformator_sprite_tinting_z");
    writeSolidBmp(tmp.dir / "assets" / "textures" / "white.bmp", 255, 255, 255);

    RuntimeOptions options;
    options.assetRoot = (tmp.dir / "assets").string();
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.windowSettings.headless = false;

    platformator::Runtime runtime(options);
    GameObject *camObj = runtime.createGameObject()->setName("Camera");
    camObj->addComponent<Camera>(320.0f, 240.0f);
    camObj->getComponent<Camera>()->setCamera(SDL_FRect{0.0f, 0.0f, 320.0f, 240.0f});

    TextureWrapper *whiteTex = runtime.loadTexture("assets/textures/white.bmp");

    auto makeStackQuad = [&](const char *name, float x, float y, SDL_Color color, int32_t zOrder) {
        GameObject *obj = runtime.createGameObject()->setName(name);
        obj->setPosition({x, y});
        obj->addComponent<Sprite>(whiteTex, 60.0f, 60.0f);
        Sprite *s = obj->getComponent<Sprite>();
        s->setColor(color);
        s->setZOrder(zOrder);
        return s;
    };

    // Stack A (left): Black (Z=0), Red a=128 (Z=1), Green a=128 (Z=2) -> Green on top => g ~ 128, r ~ 64
    makeStackQuad("BaseA", 80.0f, 120.0f, {0, 0, 0, 255}, 0);
    Sprite *layerA1 = makeStackQuad("LayerA1", 80.0f, 120.0f, {255, 0, 0, 128}, 1);
    Sprite *layerA2 = makeStackQuad("LayerA2", 80.0f, 120.0f, {0, 255, 0, 128}, 2);

    std::vector<SampleRequest> samples = {{80, 120}};
    renderAndCaptureSamples(runtime, samples);
    require(samples[0].g > samples[0].r + 40 && nearByte(samples[0].g, 128) && nearByte(samples[0].r, 64),
            "Green at higher Z-order must dominate blended pixel (got " + std::to_string(samples[0].r) + "," +
                std::to_string(samples[0].g) + "," + std::to_string(samples[0].b) + ")");

    // Flip Z-order cleanly: Red on top (Z=3) -> r ~ 128, g ~ 64
    layerA1->setZOrder(3);
    samples = {{80, 120}};
    renderAndCaptureSamples(runtime, samples);
    require(samples[0].r > samples[0].g + 40 && nearByte(samples[0].r, 128) && nearByte(samples[0].g, 64),
            "Flipping Z-order must flip alpha compositing order");
    (void)layerA2;
}

void testColorEditsDoNotInvalidateCullingOrOrder()
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    ScopedTempDir tmp("platformator_sprite_tinting_probes");
    writeSolidBmp(tmp.dir / "assets" / "textures" / "white.bmp", 255, 255, 255);

    RuntimeOptions options;
    options.assetRoot = (tmp.dir / "assets").string();
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.windowSettings.headless = false;
    options.windowSettings.enableRenderCulling = true;

    platformator::Runtime runtime(options);
    GameObject *camObj = runtime.createGameObject()->setName("Camera");
    camObj->addComponent<Camera>(320.0f, 240.0f);
    camObj->getComponent<Camera>()->setCamera(SDL_FRect{0.0f, 0.0f, 320.0f, 240.0f});

    TextureWrapper *whiteTex = runtime.loadTexture("assets/textures/white.bmp");
    std::vector<Sprite *> sprites;
    for (int i = 0; i < 6; ++i)
    {
        GameObject *obj = runtime.createGameObject()->setName("Sprite_" + std::to_string(i));
        obj->setPosition({40.0f + static_cast<float>(i) * 40.0f, 120.0f});
        obj->addComponent<Sprite>(whiteTex, 24.0f, 24.0f);
        Sprite *s = obj->getComponent<Sprite>();
        s->setZOrder(i % 3);
        sprites.push_back(s);
    }

    // Warmup frames so culling tree & sprite order state reach steady state
    for (int f = 0; f < 3; ++f)
        require(runtime.simulateAndRenderFrame(dt), "Warmup frame failed");

    platformator_test::work = {};
    sprites[0]->setColor(SDL_Color{255, 64, 32, 255});
    sprites[1]->setAlpha(128);
    sprites[2]->setColor(16, 200, 240, 180);

    std::vector<SampleRequest> samples = {{40, 120}};
    renderAndCaptureSamples(runtime, samples);

    require(samples[0].r == 255 && samples[0].g == 64 && samples[0].b == 32,
            "Color-only edit must immediately render with updated RGB tint");
    require(platformator_test::work.renderCullingRecordsAdded == 0 &&
                platformator_test::work.renderCullingRecordsRemoved == 0 &&
                platformator_test::work.renderCullingRecordsRelocated == 0 &&
                platformator_test::work.renderCullingDirtyEnqueues == 0 &&
                platformator_test::work.renderCullingBoundsRefreshes == 0 &&
                platformator_test::work.renderCullingViewQueryBuilds == 0 &&
                platformator_test::work.renderCullingCandidateVisits == 0 &&
                platformator_test::work.renderCullingFullPopulationVisits == 0 &&
                platformator_test::work.visibleMembershipChanges == 0 &&
                platformator_test::work.renderCullingCacheHits == 1,
            "Color-only edits on warm cached view must reuse cache with zero query builds or candidate visits");
    require(platformator_test::work.spriteOrderSorts == 0 &&
                platformator_test::work.spriteOrderRepairs == 0 &&
                platformator_test::work.spriteOrderComparisons == 0 &&
                platformator_test::work.spriteOrderMoves == 0,
            "Color-only edits must trigger zero draw-order sorts or repairs");
#endif
}

void testPreModulatedTexturePreservation()
{
    ScopedTempDir tmp("platformator_sprite_tinting_premod");
    writeSolidBmp(tmp.dir / "assets" / "textures" / "white.bmp", 255, 255, 255);

    RuntimeOptions options;
    options.assetRoot = (tmp.dir / "assets").string();
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.windowSettings.headless = false;
    options.windowSettings.clearColor = SDL_Color{0, 0, 0, 255};

    platformator::Runtime runtime(options);
    GameObject *camObj = runtime.createGameObject()->setName("Camera");
    camObj->addComponent<Camera>(320.0f, 240.0f);
    camObj->getComponent<Camera>()->setCamera(SDL_FRect{0.0f, 0.0f, 320.0f, 240.0f});

    TextureWrapper *sharedTex = runtime.loadTexture("assets/textures/white.bmp");
    require(sharedTex != nullptr && sharedTex->getTexture() != nullptr, "Failed to load white.bmp");

    // Pre-modulate the caller-owned SDL_Texture before rendering tinted and default Sprites sharing it.
    require(SDL_SetTextureColorMod(sharedTex->getTexture(), 100, 150, 200), "Pre-set ColorMod failed");
    require(SDL_SetTextureAlphaMod(sharedTex->getTexture(), 180), "Pre-set AlphaMod failed");
    require(SDL_SetTextureBlendMode(sharedTex->getTexture(), SDL_BLENDMODE_BLEND), "Pre-set BlendMode failed");

    GameObject *tintedObj = runtime.createGameObject()->setName("TintedOverPreMod");
    tintedObj->setPosition({80.0f, 120.0f});
    tintedObj->addComponent<Sprite>(sharedTex, 40.0f, 40.0f);
    Sprite *tintedSprite = tintedObj->getComponent<Sprite>();
    tintedSprite->setColor(255, 0, 0, 255);
    tintedSprite->setZOrder(1);

    GameObject *defaultObj = runtime.createGameObject()->setName("DefaultPreservingPreMod");
    defaultObj->setPosition({180.0f, 120.0f});
    defaultObj->addComponent<Sprite>(sharedTex, 40.0f, 40.0f);
    Sprite *defaultSprite = defaultObj->getComponent<Sprite>();
    defaultSprite->setZOrder(2);

    std::vector<SampleRequest> samples = {
        {80, 120},   // tintedSprite (255, 0, 0, 255) overrides mod for its draw -> (255, 0, 0)
        {180, 120},  // defaultSprite preserves pre-mod (100, 150, 200, 180) over black -> ~(71, 106, 141)
    };
    SDL_BlendMode blendAfter = SDL_BLENDMODE_INVALID;
    const SDL_Color modAfter = renderAndCaptureSamples(runtime, samples, sharedTex->getTexture(), &blendAfter);

    require(samples[0].r == 255 && samples[0].g == 0 && samples[0].b == 0,
            "Non-default tinted Sprite must render its own RGBA regardless of pre-existing texture mod");
    require(nearByte(samples[1].r, 71, 4) && nearByte(samples[1].g, 106, 4) && nearByte(samples[1].b, 141, 4),
            "Default Sprite sharing pre-modulated texture must preserve caller modulation (got " +
                std::to_string(samples[1].r) + "," + std::to_string(samples[1].g) + "," +
                std::to_string(samples[1].b) + ")");
    require(modAfter.r == 100 && modAfter.g == 150 && modAfter.b == 200 && modAfter.a == 180,
            "Caller pre-existing SDL_Texture ColorMod/AlphaMod must be restored after non-default draw");
    require(blendAfter == SDL_BLENDMODE_BLEND, "Shared texture blend mode must stay preserved");
}

void testAllRenderPathsSharedTextureTinting()
{
    ScopedTempDir tmp("platformator_sprite_tinting_paths");
    writeSolidBmp(tmp.dir / "assets" / "textures" / "white.bmp", 255, 255, 255);

    auto runSharedCheck = [&](bool enableCulling, bool triggerScaleFallback) {
        RuntimeOptions options;
        options.assetRoot = (tmp.dir / "assets").string();
        options.windowSettings.width = 320;
        options.windowSettings.height = 240;
        options.windowSettings.headless = false;
        options.windowSettings.enableRenderCulling = enableCulling;

        platformator::Runtime runtime(options);
        GameObject *camObj = runtime.createGameObject()->setName("Camera");
        camObj->addComponent<Camera>(320.0f, 240.0f);
        Camera *camera = camObj->getComponent<Camera>();
        camera->setCamera(SDL_FRect{0.0f, 0.0f, 320.0f, 240.0f});

        TextureWrapper *whiteTex = runtime.loadTexture("assets/textures/white.bmp");
        auto makeQuad = [&](const char *name, float x, float y, SDL_Color color, int32_t z) {
            GameObject *o = runtime.createGameObject()->setName(name);
            o->setPosition({x, y});
            o->addComponent<Sprite>(whiteTex, 40.0f, 40.0f);
            Sprite *s = o->getComponent<Sprite>();
            s->setColor(color);
            s->setZOrder(z);
            return s;
        };

        makeQuad("BlueBase", 160.0f, 120.0f, {0, 0, 255, 255}, 0);
        Sprite *alphaRed = makeQuad("AlphaRed", 160.0f, 120.0f, {255, 0, 0, 128}, 1);
        makeQuad("UntintedRight", 240.0f, 120.0f, {255, 255, 255, 255}, 1);
        Sprite *greenQuad = makeQuad("Green", 80.0f, 120.0f, {0, 220, 0, 255}, 1);

        if (triggerScaleFallback)
            require(SDL_SetRenderScale(runtime.getRenderer(), 1.0f, 1.0f) &&
                        SDL_SetRenderScale(runtime.getRenderer(), 1.005f, 1.0f),
                    "Failed to set fallback render scale");

        std::vector<SampleRequest> samples = {
            {80, 120},   // green
            {160, 120},  // red a=128 over blue
            {240, 120},  // untinted white
        };
        renderAndCaptureSamples(runtime, samples);
        require(samples[0].g >= 218 && samples[0].r == 0 && samples[0].b == 0,
                "Path check: green tinted sprite mismatch");
        require(nearByte(samples[1].r, 128, 4) && nearByte(samples[1].g, 0, 2) && nearByte(samples[1].b, 127, 4),
                "Path check: translucent shared-texture blend mismatch");
        require(samples[2].r == 255 && samples[2].g == 255 && samples[2].b == 255,
                "Path check: untinted sibling leaked modulation");

        if (triggerScaleFallback)
            require(SDL_SetRenderScale(runtime.getRenderer(), 1.0f, 1.0f), "Restore render scale failed");

        // Direct Camera::render(...) path onto an offscreen target texture
        SDL_Renderer *renderer = runtime.getRenderer();
        SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
        require(target != nullptr, "Failed to create direct Camera::render target");
        require(SDL_SetRenderTarget(renderer, target), "SetRenderTarget failed");
        require(SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255) && SDL_RenderClear(renderer),
                "Clear direct Camera::render target failed");
        camera->render(alphaRed, renderer, 320, 240);
        camera->render(greenQuad, renderer, 320, 240);
        SDL_Surface *surf = SDL_RenderReadPixels(renderer, nullptr);
        require(surf != nullptr, "ReadPixels on direct Camera::render target failed");
        Uint8 dr = 0, dg = 0, db = 0, da = 0;
        require(SDL_ReadSurfacePixel(surf, 160, 120, &dr, &dg, &db, &da), "Read direct alpha pixel failed");
        require(nearByte(dr, 128, 4) && nearByte(db, 127, 4),
                "Direct Camera::render must apply per-sprite RGBA color & alpha modulation");
        SDL_DestroySurface(surf);
        require(SDL_SetRenderTarget(renderer, nullptr), "Reset render target failed");
        SDL_DestroyTexture(target);
    };

    runSharedCheck(false, false);
    runSharedCheck(true, true);
}

void testClearColorOverride()
{
    // 1. Default clear color is opaque white (255, 255, 255, 255).
    {
        RuntimeOptions defaultOpts;
        defaultOpts.windowSettings.width = 160;
        defaultOpts.windowSettings.height = 120;
        defaultOpts.windowSettings.headless = false;
        platformator::Runtime runtime(defaultOpts);

        const SDL_Color initClear = runtime.getClearColor();
        require(initClear.r == 255 && initClear.g == 255 && initClear.b == 255 && initClear.a == 255,
                "Default Runtime::getClearColor() must be opaque white (255,255,255,255)");

        std::vector<SampleRequest> samples = {{80, 60}};
        renderAndCaptureSamples(runtime, samples);
        require(samples[0].r == 255 && samples[0].g == 255 && samples[0].b == 255,
                "Default backdrop clear pixel must be white (255,255,255)");
    }

    // 2. Custom startup WindowSettings::clearColor + dynamic Runtime::setClearColor with & without camera.
    {
        RuntimeOptions opts;
        opts.windowSettings.width = 160;
        opts.windowSettings.height = 120;
        opts.windowSettings.headless = false;
        opts.windowSettings.clearColor = SDL_Color{18, 34, 56, 255};
        opts.windowSettings.enableRenderCulling = true;
        platformator::Runtime runtime(opts);

        const SDL_Color startClear = runtime.getClearColor();
        require(startClear.r == 18 && startClear.g == 34 && startClear.b == 56 && startClear.a == 255,
                "Startup WindowSettings::clearColor must initialize Runtime::getClearColor()");

        // Render WITHOUT mainCamera
        std::vector<SampleRequest> samples = {{80, 60}};
        renderAndCaptureSamples(runtime, samples);
        require(samples[0].r == 18 && samples[0].g == 34 && samples[0].b == 56,
                "Rendered backdrop without camera must match startup WindowSettings::clearColor");

        // Dynamic update via SDL_Color overload without camera
        runtime.setClearColor(SDL_Color{90, 110, 130, 255});
        samples = {{80, 60}};
        renderAndCaptureSamples(runtime, samples);
        require(samples[0].r == 90 && samples[0].g == 110 && samples[0].b == 130,
                "Runtime::setClearColor(SDL_Color) must update backdrop without camera");
    }

    // 3. Custom startup clearColor + dynamic Runtime::setClearColor with a Camera + culling invariance.
    {
        ScopedTempDir tmp("platformator_clear_color_probe");
        writeSolidBmp(tmp.dir / "assets" / "textures" / "white.bmp", 255, 255, 255);
        RuntimeOptions texOpts;
        texOpts.assetRoot = (tmp.dir / "assets").string();
        texOpts.windowSettings.width = 160;
        texOpts.windowSettings.height = 120;
        texOpts.windowSettings.headless = false;
        texOpts.windowSettings.clearColor = SDL_Color{18, 34, 56, 255};
        texOpts.windowSettings.enableRenderCulling = true;
        platformator::Runtime camRuntime(texOpts);

        GameObject *camObj = camRuntime.createGameObject()->setName("Camera");
        camObj->addComponent<Camera>(160.0f, 120.0f);
        camObj->getComponent<Camera>()->setCamera(SDL_FRect{0.0f, 0.0f, 160.0f, 120.0f});

        TextureWrapper *tex = camRuntime.loadTexture("assets/textures/white.bmp");
        GameObject *quadObj = camRuntime.createGameObject()->setName("Quad");
        quadObj->setPosition({40.0f, 60.0f});
        quadObj->addComponent<Sprite>(tex, 20.0f, 20.0f);
        quadObj->getComponent<Sprite>()->setColor(255, 0, 0, 255);

        // Warmup frame
        require(camRuntime.simulateAndRenderFrame(dt), "Clear color warmup frame failed");
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::work = {};
#endif
        camRuntime.setClearColor(12, 180, 45);
        const SDL_Color updated = camRuntime.getClearColor();
        require(updated.r == 12 && updated.g == 180 && updated.b == 45 && updated.a == 255,
                "Runtime::setClearColor(r,g,b) must default alpha to 255");

        std::vector<SampleRequest> camSamples = {
            {40, 60},   // over sprite
            {130, 60},  // uncovered backdrop
        };
        renderAndCaptureSamples(camRuntime, camSamples);
        require(camSamples[0].r == 255 && camSamples[0].g == 0 && camSamples[0].b == 0,
                "Sprite must render atop custom clear color");
        require(camSamples[1].r == 12 && camSamples[1].g == 180 && camSamples[1].b == 45,
                "Uncovered backdrop with camera must match dynamic Runtime::setClearColor()");
#if PLATFORMATOR_ENABLE_WORK_PROBES
        require(platformator_test::work.renderCullingBoundsRefreshes == 0 &&
                    platformator_test::work.renderCullingFullPopulationVisits == 0 &&
                    platformator_test::work.spriteOrderSorts == 0,
                "Runtime::setClearColor must cause zero culling or draw-order invalidations");
#endif
    }

    // 4. Alpha-capable RGBA8888 render target verification across all 4 channels (default white, non-opaque startup & dynamic clearColor with & without camera).
    {
        RuntimeOptions alphaOpts;
        alphaOpts.windowSettings.width = 160;
        alphaOpts.windowSettings.height = 120;
        alphaOpts.windowSettings.headless = false;
        platformator::Runtime alphaRuntime(alphaOpts);

        SDL_Renderer *renderer = alphaRuntime.getRenderer();
        SDL_Texture *rgbaTarget =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 160, 120);
        require(rgbaTarget != nullptr, "Failed to create RGBA8888 render target for clear color check");
        require(SDL_SetRenderTarget(renderer, rgbaTarget), "Failed to bind RGBA8888 render target");

        // Default white (255, 255, 255, 255)
        std::vector<SampleRequest> targetSamples = {{80, 60}};
        renderAndCaptureSamples(alphaRuntime, targetSamples);
        require(targetSamples[0].r == 255 && targetSamples[0].g == 255 &&
                    targetSamples[0].b == 255 && targetSamples[0].a == 255,
                "RGBA8888 render target default clear must match (255,255,255,255)");

        // Dynamic non-opaque clear color WITHOUT camera
        alphaRuntime.setClearColor(90, 110, 130, 64);
        targetSamples = {{80, 60}};
        renderAndCaptureSamples(alphaRuntime, targetSamples);
        require(targetSamples[0].r == 90 && targetSamples[0].g == 110 &&
                    targetSamples[0].b == 130 && targetSamples[0].a == 64,
                "RGBA8888 clear without camera must preserve all 4 non-opaque channels");

        // With main camera active + next-frame non-opaque clear update
        GameObject *camObj = alphaRuntime.createGameObject()->setName("AlphaCam");
        camObj->addComponent<Camera>(160.0f, 120.0f);
        camObj->getComponent<Camera>()->setCamera(SDL_FRect{0.0f, 0.0f, 160.0f, 120.0f});
        alphaRuntime.setClearColor(SDL_Color{18, 34, 56, 140});
        targetSamples = {{80, 60}};
        renderAndCaptureSamples(alphaRuntime, targetSamples);
        require(targetSamples[0].r == 18 && targetSamples[0].g == 34 &&
                    targetSamples[0].b == 56 && targetSamples[0].a == 140,
                "RGBA8888 clear with camera must preserve all 4 non-opaque channels");

        require(SDL_SetRenderTarget(renderer, nullptr), "Failed to unbind RGBA8888 render target");
        SDL_DestroyTexture(rgbaTarget);
    }

    // 5. Startup non-opaque clearColor on RGBA8888 render target + Headless storage no-op check.
    {
        RuntimeOptions startupAlphaOpts;
        startupAlphaOpts.windowSettings.width = 160;
        startupAlphaOpts.windowSettings.height = 120;
        startupAlphaOpts.windowSettings.headless = false;
        startupAlphaOpts.windowSettings.clearColor = SDL_Color{22, 44, 66, 177};
        platformator::Runtime startupRuntime(startupAlphaOpts);

        SDL_Renderer *renderer = startupRuntime.getRenderer();
        SDL_Texture *rgbaTarget =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 160, 120);
        require(rgbaTarget != nullptr, "Failed to create RGBA8888 startup clear target");
        require(SDL_SetRenderTarget(renderer, rgbaTarget), "Failed to bind startup clear target");

        std::vector<SampleRequest> samples = {{40, 30}};
        renderAndCaptureSamples(startupRuntime, samples);
        require(samples[0].r == 22 && samples[0].g == 44 && samples[0].b == 66 && samples[0].a == 177,
                "Startup non-opaque WindowSettings::clearColor must preserve all 4 RGBA channels");

        require(SDL_SetRenderTarget(renderer, nullptr), "Failed to unbind startup clear target");
        SDL_DestroyTexture(rgbaTarget);
    }

    // 6. Headless mode stores clearColor cleanly without renderer error.
    {
        RuntimeOptions headlessOpts = physicsOnly();
        headlessOpts.windowSettings.clearColor = SDL_Color{5, 15, 25, 200};
        platformator::Runtime headlessRuntime(headlessOpts);
        require(headlessRuntime.getClearColor().r == 5 && headlessRuntime.getClearColor().a == 200,
                "Headless Runtime must store startup clearColor");
        headlessRuntime.setClearColor(70, 80, 90, 128);
        const SDL_Color stored = headlessRuntime.getClearColor();
        require(stored.r == 70 && stored.g == 80 && stored.b == 90 && stored.a == 128,
                "Headless Runtime::setClearColor must update stored clearColor without error");
        headlessRuntime.simulateFrame(dt);
    }
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        testSharedTextureRgbAlphaAndBlendRestoration();
        testZOrderAlphaCompositing();
        testColorEditsDoNotInvalidateCullingOrOrder();
        testPreModulatedTexturePreservation();
        testAllRenderPathsSharedTextureTinting();
        testClearColorOverride();
        std::cout << "[PASS] sprite tinting tests\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] sprite tinting tests: " << error.what() << '\n';
        return 1;
    }
}
