// Investigation only: standalone probe, not part of the engine or CTest.
// Run from the repository root; see ../../P03_SPRITE_RENDERING_INVESTIGATION.md.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <unsupported/Eigen/BVH>
#include "platformator/runtime.h"
#include "platformator/camera.h"
#include "platformator/sprite.h"
#include "platformator/texturewrapper.h"
#include "platformator/gridtypes.h"

constexpr int outputW = 1280, outputH = 720;
struct Item { Sprite *sprite; GameObject *object; };
struct Bounds { float x, y, ex, ey; };
struct View { float x, y, sx, sy; };

View prepare(Camera *camera)
{
    auto p = camera->getGameObject()->getPosition();
    return {p.x(), p.y(), outputW / camera->getWidth(), outputH / camera->getHeight()};
}

Bounds bounds(const Item &item, const View &v)
{
    auto p = item.object->getPosition(); // Root objects only in this probe.
    const float w = item.sprite->getWidth() * v.sx;
    const float h = item.sprite->getHeight() * v.sy;
    const float c = std::abs(item.object->getCosRotation());
    const float s = std::abs(item.object->getSinRotation());
    return {(p.x() - v.x) * v.sx, (p.y() - v.y) * v.sy,
            (c * std::abs(w) + s * std::abs(h)) * 0.5f,
            (s * std::abs(w) + c * std::abs(h)) * 0.5f};
}

bool visible(const Bounds &b)
{
    // One pixel of conservative padding for boundary rounding/filtering.
    return b.x + b.ex >= -1 && b.y + b.ey >= -1 &&
           b.x - b.ex <= outputW + 1 && b.y - b.ey <= outputH + 1;
}

void draw(const Item &item, const View &v, SDL_Renderer *renderer)
{
    auto p = item.object->getPosition();
    const float w = item.sprite->getWidth(), h = item.sprite->getHeight();
    SDL_FRect q{(p.x() - w * .5f - v.x) * v.sx,
                (p.y() - h * .5f - v.y) * v.sy, w * v.sx, h * v.sy};
    if (!SDL_RenderTextureRotated(renderer, item.sprite->getTexture(), item.sprite->getSourceRect(),
                                 &q, item.object->getRotationInDegrees(), nullptr, item.sprite->getFlip()))
        throw std::runtime_error(SDL_GetError());
}

// Static render-only grid. Reuses the engine's key/range types, not physics pairs.
// Bounds are in output coordinates: valid only for the view used to build it.
struct StaticGrid
{
    std::unordered_map<GridCellKey, std::vector<size_t>, GridCellKey::Hash> cells;
    std::vector<unsigned> seen;
    std::vector<size_t> hits;
    unsigned stamp = 0;
    void build(const std::vector<Bounds> &boxes)
    {
        cells.clear();
        seen.assign(boxes.size(), 0);
        hits.reserve(boxes.size());
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            const auto &b = boxes[i];
            GridCellRange range(int(std::floor((b.x - b.ex) / 200)), int(std::floor((b.x + b.ex) / 200)),
                                int(std::floor((b.y - b.ey) / 200)), int(std::floor((b.y + b.ey) / 200)));
            range.forEachCell([&](const GridCellKey &key) { cells[key].push_back(i); });
        }
    }
    void query(const std::vector<Bounds> &boxes)
    {
        hits.clear();
        if (++stamp == 0) { std::fill(seen.begin(), seen.end(), 0); ++stamp; }
        GridCellRange range(-1, int(std::floor((outputW + 1) / 200.0)),
                            -1, int(std::floor((outputH + 1) / 200.0)));
        range.forEachCell([&](const GridCellKey &key) {
            auto found = cells.find(key);
            if (found == cells.end()) return;
            for (size_t i : found->second)
                if (seen[i] != stamp)
                {
                    seen[i] = stamp;
                    if (visible(boxes[i])) hits.push_back(i);
                }
        });
        std::sort(hits.begin(), hits.end()); // Preserve current sprite-vector order.
    }
};

struct StaticBVH
{
    using Tree = Eigen::KdBVH<float, 2, size_t>;
    Tree tree;
    std::vector<size_t> hits;
    const std::vector<Bounds> *boxes = nullptr;
    void build(const std::vector<Bounds> &input)
    {
        boxes = &input;
        std::vector<size_t> ids(input.size());
        std::iota(ids.begin(), ids.end(), 0);
        Tree::VolumeList volumes;
        for (const auto &b : input)
            volumes.emplace_back(Eigen::Vector2f(b.x - b.ex, b.y - b.ey),
                                 Eigen::Vector2f(b.x + b.ex, b.y + b.ey));
        tree.init(ids.begin(), ids.end(), volumes.begin(), volumes.end());
        hits.reserve(input.size());
    }
    bool intersectVolume(const Tree::Volume &v)
    {
        return v.max().x() >= -1 && v.max().y() >= -1 &&
               v.min().x() <= outputW + 1 && v.min().y() <= outputH + 1;
    }
    bool intersectObject(size_t i)
    {
        if (visible((*boxes)[i])) hits.push_back(i);
        return false;
    }
    void query()
    {
        hits.clear();
        Eigen::BVIntersect(tree, *this);
        std::sort(hits.begin(), hits.end());
    }
};

uint64_t pixels(SDL_Renderer *renderer)
{
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    if (!surface) throw std::runtime_error(SDL_GetError());
    uint64_t hash = 14695981039346656037ULL;
    const int rowBytes = surface->w * SDL_BYTESPERPIXEL(surface->format);
    for (int y = 0; y < surface->h; ++y)
        for (int x = 0; x < rowBytes; ++x)
        {
            hash ^= static_cast<unsigned char *>(surface->pixels)[y * surface->pitch + x];
            hash *= 1099511628211ULL;
        }
    SDL_DestroySurface(surface);
    return hash;
}

void validateEdges(platformator::Runtime &runtime, TextureWrapper &texture, SDL_Renderer *renderer)
{
    for (const Eigen::Vector2f size : {Eigen::Vector2f(1280, 720), Eigen::Vector2f(2560, 720),
                                     Eigen::Vector2f(640, 1440)})
    {
        std::vector<Item> items;
        Camera *camera = nullptr;
        runtime.simulateFrameWithCustomCallback(1.0 / 120, [&](double) {
            auto *cameraObject = runtime.createGameObject();
            cameraObject->setPosition({-123, 456});
            cameraObject->addComponent<Camera>(size.x(), size.y());
            camera = cameraObject->getComponent<Camera>();
            for (int side = 0; side < 4; ++side)
            for (int i = 0; i < 50; ++i)
            {
                const float offset = (i - 25) * 3.f;
                const float along = 10.f + i * 12.f;
                Eigen::Vector2f p;
                if (side < 2) p = {side == 0 ? offset : outputW + offset, along};
                else p = {along, side == 2 ? offset : outputH + offset};
                auto *o = runtime.createGameObject();
                o->setPosition({-123 + p.x() * size.x() / outputW, 456 + p.y() * size.y() / outputH});
                o->setRotation(i * .13f);
                o->setScale({i % 2 ? 2.f : 1.f, i % 3 ? 1.f : 3.f});
                o->addComponent<Sprite>(&texture, 10.f, 160.f);
                auto *sprite = o->getComponent<Sprite>();
                if (i % 2) sprite->setSourceRect({1, 2, 5, 4});
                sprite->setFlip(static_cast<SDL_FlipMode>(i % 4));
                items.push_back({sprite, o});
            }
        });
        const View v = prepare(camera);
        uint64_t reference = 0;
        for (int mode = 0; mode < 2; ++mode)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderClear(renderer);
            for (const auto &item : items)
                if (mode == 0) camera->render(item.sprite, renderer, outputW, outputH);
                else if (visible(bounds(item, v))) draw(item, v, renderer);
            SDL_RenderPresent(renderer);
            const auto hash = pixels(renderer);
            if (mode == 0) reference = hash;
            else if (hash != reference) throw std::runtime_error("Viewport edge pixel mismatch");
        }
        std::fprintf(stderr, "verified edges camera=%.0fx%.0f sprites=%zu hash=%llu\n", size.x(), size.y(),
                     items.size(), static_cast<unsigned long long>(reference));
        runtime.clearScene();
    }
}

int main()
{
    RuntimeOptions options;
    options.windowSettings.headless = true;
    options.assetRoot = "assets";
    platformator::Runtime runtime(options);
    SDL_Surface *target = SDL_CreateSurface(outputW, outputH, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(target);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    {
        SDL_Surface *source = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                SDL_WriteSurfacePixel(source, x, y, x * 30, y * 30, 200, 128);
        TextureWrapper texture(SDL_CreateTextureFromSurface(renderer, source), "");
        SDL_DestroySurface(source);
        if (!texture.getTexture()) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(texture.getTexture(), SDL_BLENDMODE_BLEND);
        std::fprintf(stderr, "SDL=%d renderer=%s output=%dx%d\n", SDL_GetVersion(),
                     SDL_GetRendererName(renderer), outputW, outputH);
        validateEdges(runtime, texture, renderer);
        std::puts("count,visible_percent,rotated,mode,repetition,frames,mean_us,submitted,index_build_us");
        for (size_t count : {size_t(1000), size_t(10000), size_t(100000)})
        for (int percent : {1, 100})
        for (bool rotated : {false, true})
        {
            if (count == 100000 && percent == 100) continue;
            std::vector<Item> items;
            Camera *camera = nullptr;
            runtime.simulateFrameWithCustomCallback(1.0 / 120, [&](double) {
                auto *object = runtime.createGameObject();
                object->addComponent<Camera>(1280.f, 720.f);
                camera = object->getComponent<Camera>();
                for (size_t i = 0; i < count; ++i)
                {
                    auto *o = runtime.createGameObject();
                    const bool onScreen = i % 100 < size_t(percent);
                    o->setPosition({onScreen ? float((i * 43) % 1260 + 10) : float((i % 1000) * 20 + 10000),
                                    onScreen ? float((i * 31) % 700 + 10) : float((i / 1000) * 20 + 10000)});
                    o->setRotation(rotated ? float((i % 60 + 1) * .07) : 0.f);
                    o->addComponent<Sprite>(&texture, 6.f, 8.f);
                    auto *sprite = o->getComponent<Sprite>();
                    if (i % 3 == 0) sprite->setSourceRect({2, 1, 4, 5});
                    if (rotated && i % 2) sprite->setFlip(SDL_FLIP_HORIZONTAL);
                    items.push_back({sprite, o});
                }
            });
            View view = prepare(camera);
            std::vector<Bounds> boxes;
            for (const auto &item : items) boxes.push_back(bounds(item, view));
            StaticGrid grid;
            auto buildStart = std::chrono::steady_clock::now();
            grid.build(boxes);
            const double buildUs = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - buildStart).count();
            StaticBVH bvh;
            buildStart = std::chrono::steady_clock::now();
            bvh.build(boxes);
            const double bvhBuildUs = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - buildStart).count();
            auto frame = [&](int mode) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                SDL_RenderClear(renderer);
                const View currentView = prepare(camera);
                size_t submitted = 0;
                if (mode == 5)
                {
                    for (size_t i = 0; i < boxes.size(); ++i)
                        if (visible(boxes[i])) { draw(items[i], currentView, renderer); ++submitted; }
                }
                else if (mode == 4)
                {
                    bvh.query();
                    for (size_t i : bvh.hits) { draw(items[i], currentView, renderer); ++submitted; }
                }
                else if (mode == 3)
                {
                    grid.query(boxes);
                    for (size_t i : grid.hits) { draw(items[i], currentView, renderer); ++submitted; }
                }
                else for (const auto &item : items)
                {
                    if (!item.object->getActive()) continue;
                    if (mode == 2 && !visible(bounds(item, currentView))) continue;
                    if (mode == 0) camera->render(item.sprite, renderer, outputW, outputH);
                    else draw(item, currentView, renderer);
                    ++submitted;
                }
                SDL_RenderPresent(renderer);
                return submitted;
            };
            std::array<uint64_t, 6> hashes{};
            for (int mode = 0; mode < 6; ++mode) { frame(mode); hashes[mode] = pixels(renderer); }
            if (!std::all_of(hashes.begin(), hashes.end(), [&](uint64_t h) { return h == hashes[0]; }))
                throw std::runtime_error("Pixel mismatch between candidates");
            const int frames = count == 100000 ? 5 : 15;
            constexpr const char *names[] = {"baseline", "prepared", "flat_cull", "static_grid", "static_bvh", "cached_flat"};
            for (int rep = 0; rep < 5; ++rep)
                for (int offset = 0; offset < 6; ++offset)
                {
                    const int mode = (offset + rep) % 6;
                    for (int warmup = 0; warmup < 2; ++warmup) frame(mode);
                    size_t submitted = 0;
                    auto start = std::chrono::steady_clock::now();
                    for (int i = 0; i < frames; ++i) submitted += frame(mode);
                    const double meanUs = std::chrono::duration<double, std::micro>(
                        std::chrono::steady_clock::now() - start).count() / frames;
                    std::printf("%zu,%d,%d,%s,%d,%d,%.3f,%zu,%.3f\n", count, percent,
                                int(rotated), names[mode], rep, frames, meanUs, submitted / frames,
                                mode == 3 ? buildUs : mode == 4 ? bvhBuildUs : 0.0);
                    std::fflush(stdout);
                }
            std::fprintf(stderr, "verified count=%zu visible=%d rotated=%d hash=%llu\n",
                         count, percent, int(rotated), static_cast<unsigned long long>(hashes[0]));
            runtime.clearScene();
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(target);
}
