#include "imagepreflight.h"
#include "thumbnailcache.h"
#include "test_support.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace platformator_editor;
using namespace platformator_editor_test;

namespace platformator_editor
{
struct ThumbnailCacheTestProbe
{
    static void set_after_preparation_hook(
        ThumbnailCache &cache,
        std::function<void(const std::filesystem::path &)> hook)
    {
        std::lock_guard lock(cache.mutex_);
        cache.afterPreparationTestHook_ = std::move(hook);
    }

    static std::uint64_t revision(const ThumbnailCache &cache,
                                  const std::filesystem::path &path)
    {
        std::lock_guard lock(cache.mutex_);
        const auto found = cache.entries_.find(ThumbnailCache::key_for(path));
        return found == cache.entries_.end() ? 0 : found->second.revision;
    }
};
} // namespace platformator_editor

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void write_png_header(const std::filesystem::path &path, std::uint32_t width,
                      std::uint32_t height, std::uint64_t fileBytes = 24)
{
    const std::array<std::uint8_t, 24> header{
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
        0, 0, 0, 13, 'I', 'H', 'D', 'R',
        static_cast<std::uint8_t>(width >> 24), static_cast<std::uint8_t>(width >> 16),
        static_cast<std::uint8_t>(width >> 8), static_cast<std::uint8_t>(width),
        static_cast<std::uint8_t>(height >> 24), static_cast<std::uint8_t>(height >> 16),
        static_cast<std::uint8_t>(height >> 8), static_cast<std::uint8_t>(height)};
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        require(static_cast<bool>(output), "write synthetic PNG header");
    }
    if (fileBytes > header.size())
        std::filesystem::resize_file(path, fileBytes);
}

void write_valid_png(const std::filesystem::path &path)
{
    SDL_Surface *surface = SDL_CreateSurface(4, 3, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, std::string("create PNG fixture: ") + SDL_GetError());
    const bool saved = SDL_FillSurfaceRect(surface, nullptr,
        SDL_MapSurfaceRGBA(surface, 30, 90, 180, 255)) &&
        IMG_SavePNG(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    require(saved, std::string("save PNG fixture: ") + SDL_GetError());
}

void write_encoded(const std::filesystem::path &path, const std::uint8_t *bytes,
                   std::size_t size)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes), static_cast<std::streamsize>(size));
    require(static_cast<bool>(output), "write encoded image fixture");
}

void write_color_png(const std::filesystem::path &path, std::uint8_t red,
                     int width = 2, int height = 2)
{
    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, std::string("create cache fixture: ") + SDL_GetError());
    const bool saved = SDL_FillSurfaceRect(surface, nullptr,
        SDL_MapSurfaceRGBA(surface, red, static_cast<std::uint8_t>(255 - red), 45, 255)) &&
        IMG_SavePNG(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    require(saved, std::string("save cache fixture: ") + SDL_GetError());
}

void write_fixed_size_color_png(const std::filesystem::path &path, std::uint8_t red)
{
    constexpr std::uintmax_t fixedSize = 512;
    write_color_png(path, red, 8, 8);
    require(std::filesystem::file_size(path) <= fixedSize,
            "same-metadata PNG fixture must fit its fixed encoded size");
    std::filesystem::resize_file(path, fixedSize);
}

class PreparationPause
{
  public:
    explicit PreparationPause(std::filesystem::path target)
        : target_(target.lexically_normal().string())
    {
    }

    void after_preparation(const std::filesystem::path &path)
    {
        std::unique_lock lock(mutex_);
        const auto key = path.lexically_normal().string();
        ++preparations_[key];
        if (key != target_ || pausedOnce_)
            return;
        pausedOnce_ = true;
        reached_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return released_; });
    }

    bool wait_until_paused()
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(3), [&] { return reached_; });
    }

    void release()
    {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

    std::size_t preparations(const std::filesystem::path &path)
    {
        std::lock_guard lock(mutex_);
        const auto found = preparations_.find(path.lexically_normal().string());
        return found == preparations_.end() ? 0 : found->second;
    }

  private:
    std::string target_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::string, std::size_t> preparations_;
    bool pausedOnce_ = false;
    bool reached_ = false;
    bool released_ = false;
};

std::array<std::uint8_t, 4> texture_pixel(SDL_Renderer *renderer, SDL_Texture *texture)
{
    require(texture != nullptr, "thumbnail pixel checks require a loaded texture");
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET, 1, 1);
    require(target != nullptr, std::string("create thumbnail readback target: ") + SDL_GetError());
    require(SDL_SetRenderTarget(renderer, target), std::string("set thumbnail readback target: ") + SDL_GetError());
    require(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) && SDL_RenderClear(renderer) &&
                SDL_RenderTexture(renderer, texture, nullptr, nullptr),
            std::string("render thumbnail for readback: ") + SDL_GetError());
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    require(surface != nullptr, std::string("read thumbnail pixel: ") + SDL_GetError());
    if (surface->format != SDL_PIXELFORMAT_RGBA32)
    {
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        surface = converted;
        require(surface != nullptr, std::string("convert thumbnail pixel: ") + SDL_GetError());
    }
    const auto *rgba = static_cast<const std::uint8_t *>(surface->pixels);
    const std::array<std::uint8_t, 4> pixel{rgba[0], rgba[1], rgba[2], rgba[3]};
    SDL_DestroySurface(surface);
    require(SDL_SetRenderTarget(renderer, nullptr), std::string("restore thumbnail renderer: ") + SDL_GetError());
    SDL_DestroyTexture(target);
    return pixel;
}

void write_color_with_same_metadata(const std::filesystem::path &path, std::uint8_t color,
                                    std::uintmax_t expectedBytes,
                                    std::filesystem::file_time_type timestamp)
{
    write_fixed_size_color_png(path, color);
    require(std::filesystem::file_size(path) == expectedBytes,
            "same-metadata replacement must preserve the encoded file size");
    std::filesystem::last_write_time(path, timestamp);
    require(std::filesystem::last_write_time(path) == timestamp,
            "same-metadata replacement must restore the original modification time");
}

SDL_Texture *wait_for_thumbnail(ThumbnailCache &cache, SDL_Renderer *renderer,
                                const std::filesystem::path &path);

void run_distinct_invalidation_scenario(SDL_Renderer *renderer,
                                        const std::filesystem::path &directory,
                                        bool refreshExisting)
{
    const std::string suffix = refreshExisting ? "refresh" : "first-load";
    const auto path = directory / ("superseded-" + suffix + ".png");
    const auto unrelatedPath = directory / ("unrelated-" + suffix + ".png");
    write_fixed_size_color_png(path, 19);
    const auto encodedBytes = std::filesystem::file_size(path);
    const auto timestamp = std::filesystem::last_write_time(path);
    write_color_png(unrelatedPath, 71, 8, 8);

    PreparationPause pause(path);
    ThumbnailCache cache;
    const auto unrelatedTexture = wait_for_thumbnail(cache, renderer, unrelatedPath);
    cache.end_frame();
    SDL_Texture *lastGood = nullptr;
    if (refreshExisting)
    {
        lastGood = wait_for_thumbnail(cache, renderer, path);
        cache.end_frame();
    }
    ThumbnailCacheTestProbe::set_after_preparation_hook(cache,
        [&](const std::filesystem::path &preparedPath) { pause.after_preparation(preparedPath); });

    if (refreshExisting)
    {
        write_color_with_same_metadata(path, 113, encodedBytes, timestamp);
        cache.invalidate(path, false, 101);
    }
    else
        cache.request(path);

    if (!pause.wait_until_paused())
    {
        pause.release();
        throw std::runtime_error("thumbnail preparation did not reach the deterministic pause");
    }

    try
    {
        const std::uint64_t firstEvent = refreshExisting ? 101 : 201;
        if (!refreshExisting)
        {
            write_color_with_same_metadata(path, 113, encodedBytes, timestamp);
            cache.invalidate(path, false, firstEvent);
        }
        const std::uint64_t firstRevision = ThumbnailCacheTestProbe::revision(cache, path);
        const auto lexicalAlias = path.parent_path() / "." / path.filename();
        cache.invalidate(lexicalAlias, false, firstEvent);
        require(ThumbnailCacheTestProbe::revision(cache, path) == firstRevision,
                "duplicate alias delivery with one event sequence must not create another revision");

        write_color_with_same_metadata(path, 207, encodedBytes, timestamp);
        const std::uint64_t secondEvent = refreshExisting ? 102 : 202;
        cache.invalidate(path, false, secondEvent);
        const std::uint64_t newestRevision = ThumbnailCacheTestProbe::revision(cache, path);
        require(newestRevision == firstRevision + 1,
                "a distinct same-metadata write must supersede an already stale revision");
        cache.invalidate(path, false, secondEvent);
        require(ThumbnailCacheTestProbe::revision(cache, path) == newestRevision,
                "duplicate fanout of the second event must keep its single content revision");
        require(cache.stale(path), "newest content must remain stale until its own preparation publishes");
        if (refreshExisting)
            require(cache.find(path) == lastGood &&
                        texture_pixel(renderer, lastGood) == std::array<std::uint8_t, 4>{19, 236, 45, 255},
                    "refresh preparation must retain the last-good texture while newer content is pending");
        else
            require(cache.find(path) == nullptr,
                    "a first-load stale result must not publish before the newest content is prepared");
    }
    catch (...)
    {
        pause.release();
        throw;
    }
    pause.release();

    const auto oldResultDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (cache.stats().completedBytes == 0 && cache.stats().running &&
           std::chrono::steady_clock::now() < oldResultDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(cache.stats().completedBytes != 0,
            "the paused old revision must complete so its rejection can be observed");
    cache.poll_uploads(renderer);
    require(cache.stale(path) &&
                (refreshExisting ? cache.find(path) == lastGood : cache.find(path) == nullptr),
            "polling a stale preparation must reject it and preserve the last-good state");

    SDL_Texture *finalTexture = nullptr;
    const auto finalDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < finalDeadline)
    {
        cache.poll_uploads(renderer);
        finalTexture = cache.find(path);
        if (finalTexture != nullptr && !cache.stale(path))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(finalTexture != nullptr && !cache.stale(path) &&
                texture_pixel(renderer, finalTexture) == std::array<std::uint8_t, 4>{207, 48, 45, 255},
            "the newest file pixels must publish automatically without another event or manual refresh");
    require(cache.find(unrelatedPath) == unrelatedTexture &&
                texture_pixel(renderer, unrelatedTexture) == std::array<std::uint8_t, 4>{71, 184, 45, 255} &&
                pause.preparations(unrelatedPath) == 0 && pause.preparations(path) == 2,
            "only the demanded changed tile may be prepared again; unrelated content stays untouched");
}

void test_distinct_invalidations_supersede_paused_thumbnail_preparation(
    const std::filesystem::path &directory)
{
    SDL_Window *window = SDL_CreateWindow("thumbnail invalidation race test", 64, 64,
                                          SDL_WINDOW_HIDDEN);
    require(window != nullptr, std::string("create invalidation test window: ") + SDL_GetError());
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    require(renderer != nullptr, std::string("create invalidation test renderer: ") + SDL_GetError());
    run_distinct_invalidation_scenario(renderer, directory, false);
    run_distinct_invalidation_scenario(renderer, directory, true);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

SDL_Texture *wait_for_thumbnail(ThumbnailCache &cache, SDL_Renderer *renderer,
                                const std::filesystem::path &path)
{
    cache.request(path);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        cache.poll_uploads(renderer);
        if (SDL_Texture *texture = cache.find(path))
            return texture;
        if (!cache.failure_message(path).empty())
            throw std::runtime_error(cache.failure_message(path));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("thumbnail preparation did not settle within three seconds");
}

void test_valid_image_is_admitted_and_decoded(const std::filesystem::path &directory)
{
    const auto path = directory / "valid.png";
    write_valid_png(path);
    const auto admission = inspect_image(path);
    require(admission.admitted(), "valid PNG metadata should be admitted");
    require(admission.width == 4 && admission.height == 3, "PNG metadata dimensions");
    require(admission.format == "PNG", "PNG format identity");
    require(admission.decodedBytes == 48, "admission uses bounded four-byte output estimate");

    auto prepared = prepare_image(path);
    require(prepared.ready(), "valid PNG should decode from staged bounded bytes: " +
            prepared.preflight.message);
    require(prepared.surface->w == 4 && prepared.surface->h == 3,
            "decoded dimensions must match admitted dimensions");
    require(prepared.preflight.workingSetBytes <= ImageMaxWorkingSetBytes,
            "decoded image stays within the inclusive working-set cap");
}

void test_bounded_decoder_formats(const std::filesystem::path &directory)
{
    constexpr std::array<std::uint8_t, 58> bmp{
        'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0,
        0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0};
    const auto bmpPath = directory / "bounded.bmp";
    write_encoded(bmpPath, bmp.data(), bmp.size());
    auto preparedBmp = prepare_image(bmpPath);
    require(preparedBmp.ready() && preparedBmp.surface->w == 1 && preparedBmp.surface->h == 1,
            "bounded callback decoder supports BMP without a second decoded-pixel copy");

    constexpr std::array<std::uint8_t, 43> gif{
        'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0,
        0, 0, 0, 255, 255, 255,
        0x21, 0xf9, 4, 1, 0, 0, 0, 0,
        0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0,
        2, 2, 0x44, 1, 0, 0x3b};
    const auto gifPath = directory / "bounded.gif";
    write_encoded(gifPath, gif.data(), gif.size());
    auto preparedGif = prepare_image(gifPath);
    require(preparedGif.ready() && preparedGif.surface->w == 1 && preparedGif.surface->h == 1,
            "bounded callback decoder supports the first GIF preview frame");
}

void test_oversized_headers_reject_before_decode(const std::filesystem::path &directory)
{
    const auto dimensions = directory / "too-wide.png";
    write_png_header(dimensions, ImageMaxDimension + 1, 1);
    const auto rejected = inspect_image(dimensions);
    require(rejected.status == ImagePreflightStatus::DimensionLimit,
            "oversized dimensions reject at metadata admission");
    require(!rejected.admitted() && rejected.message.find("8,192") != std::string::npos,
            "dimension rejection explains the configured cap");

    const auto pixels = directory / "too-many-pixels.png";
    write_png_header(pixels, 4097, 4096);
    require(inspect_image(pixels).status == ImagePreflightStatus::DimensionLimit,
            "pixel count rejects even when each dimension is below its cap");

    const auto encoded = directory / "too-large.png";
    write_png_header(encoded, 1, 1, ImageMaxEncodedBytes + 1);
    const auto encodedRejected = inspect_image(encoded);
    require(encodedRejected.status == ImagePreflightStatus::EncodedLimit,
            "encoded byte cap is checked before reading or allocating the image");
}

void test_inclusive_working_set_and_fallback(const std::filesystem::path &directory)
{
    const auto exact = directory / "inclusive-128m.png";
    write_png_header(exact, 8192, 2048, ImageMaxEncodedBytes);
    const auto admission = inspect_image(exact);
    require(admission.admitted(), "exact encoded plus decoded working set is admitted");
    require(admission.decodedBytes == ImageMaxDecodedBytes &&
            admission.workingSetBytes == ImageMaxWorkingSetBytes,
            "the 128 MiB working-set limit is inclusive");

    const auto svg = directory / "unsupported.svg";
    std::ofstream(svg) << "<svg width=\"4\" height=\"3\"></svg>";
    const auto unsupported = inspect_image(svg);
    require(unsupported.status == ImagePreflightStatus::Unsupported &&
            unsupported.message.find("icon") != std::string::npos,
            "unsupported SVG gets an explanatory fallback state");

    const auto invalid = directory / "invalid.png";
    std::ofstream(invalid, std::ios::binary) << "not an image";
    require(inspect_image(invalid).status == ImagePreflightStatus::Unsupported,
            "unknown bytes do not reach an image decoder");
}

void test_changed_file_is_rejected(const std::filesystem::path &directory)
{
    const auto path = directory / "empty.png";
    std::ofstream(path, std::ios::binary);
    const auto result = inspect_image(path);
    require(result.status == ImagePreflightStatus::Empty,
            "empty files fail before decoder setup");
}

void test_thumbnail_cache_limits_pins_eviction_and_negative_results(
    const std::filesystem::path &directory)
{
    SDL_Window *window = SDL_CreateWindow("thumbnail cache test", 64, 64, SDL_WINDOW_HIDDEN);
    require(window != nullptr, std::string("create thumbnail cache window: ") + SDL_GetError());
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    require(renderer != nullptr, std::string("create thumbnail cache renderer: ") + SDL_GetError());
    {
        ThumbnailCache cache;
        std::vector<std::filesystem::path> paths;
        paths.reserve(ThumbnailMaxEntries + 1);
        for (std::size_t index = 0; index < ThumbnailMaxEntries + 1; ++index)
        {
            const auto path = directory / ("cache-" + std::to_string(index) + ".png");
            write_color_png(path, static_cast<std::uint8_t>(index));
            paths.push_back(path);
        }

        for (std::size_t index = 0; index < ThumbnailMaxPendingPaths + 6; ++index)
            cache.request(paths[index]);
        const auto saturated = cache.stats();
        require(saturated.pendingPaths == ThumbnailMaxPendingPaths &&
                    saturated.retainedBytes <= ThumbnailMaxRetainedBytes,
                "the pending-path limit includes the running or completed decode");
        cache.request(paths.front());
        require(cache.stats().pendingPaths == ThumbnailMaxPendingPaths,
                "repeated visible demand for one version does not enqueue a duplicate decode");
        for (std::size_t index = 0; index < ThumbnailMaxPendingPaths + 6; ++index)
        {
            wait_for_thumbnail(cache, renderer, paths[index]);
            cache.end_frame();
        }

        for (std::size_t index = 0; index < ThumbnailMaxEntries; ++index)
        {
            wait_for_thumbnail(cache, renderer, paths[index]);
            const auto stats = cache.stats();
            require(stats.entries <= ThumbnailMaxEntries && stats.retainedBytes <= ThumbnailMaxRetainedBytes &&
                    stats.completedBytes <= ThumbnailMaxCompletedBytes,
                    "thumbnail cache retains no more than the entry, output and completed-byte limits");
            cache.end_frame();
        }
        const auto pinned = cache.request(paths.front());
        require(pinned != nullptr, "first cache entry should be available for a frame pin");
        SDL_Texture *wrongThreadRequest = nullptr;
        std::thread wrongRendererThread([&] {
            cache.poll_uploads(renderer);
            wrongThreadRequest = cache.request(paths.front());
        });
        wrongRendererThread.join();
        require(wrongThreadRequest == nullptr && cache.stats().ownerThreadViolation &&
                    cache.find(paths.front()) == pinned,
                "renderer upload and texture access must stay on the cache's owning thread");
        const auto newest = wait_for_thumbnail(cache, renderer, paths.back());
        require(newest != nullptr && cache.find(paths.front()) == pinned,
                "a frame-pinned texture survives LRU admission of another thumbnail");
        require(cache.request(paths.back()) == newest && cache.stats().framePins >= 2,
                "textures requested for the current frame stay pinned through all draw submissions");
        require(cache.find(paths[1]) == nullptr && cache.stats().entries == ThumbnailMaxEntries,
                "an unpinned least-recently-used texture is evicted at the entry cap");
        cache.end_frame();
        require(cache.stats().framePins == 0,
                "the owner releases submitted texture pins only after the frame has been presented");

        const auto reloaded = wait_for_thumbnail(cache, renderer, paths[1]);
        require(reloaded != nullptr && cache.find(paths[1]) == reloaded,
                "an evicted thumbnail can be prepared and loaded again on demand");
        require(cache.stats().entries == ThumbnailMaxEntries &&
                    cache.stats().retainedBytes <= ThumbnailMaxRetainedBytes,
                "reload preserves the cache caps");
        cache.end_frame();

        SDL_Window *secondWindow = SDL_CreateWindow("thumbnail replica test", 64, 64,
                                                     SDL_WINDOW_HIDDEN);
        require(secondWindow != nullptr,
                std::string("create second thumbnail window: ") + SDL_GetError());
        SDL_Renderer *secondRenderer = SDL_CreateRenderer(secondWindow, nullptr);
        require(secondRenderer != nullptr,
                std::string("create second thumbnail renderer: ") + SDL_GetError());
        const auto replicatedPath = paths.back();
        const auto beforeReplica = cache.stats();
        require(cache.request(replicatedPath, secondRenderer) == nullptr,
                "a renderer without a local texture receives no cross-renderer SDL handle");
        const auto replicaDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (cache.find(replicatedPath, secondRenderer) == nullptr &&
               std::chrono::steady_clock::now() < replicaDeadline)
        {
            cache.poll_uploads(secondRenderer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto replicaTexture = cache.find(replicatedPath, secondRenderer);
        const auto firstRendererTexture = cache.find(replicatedPath, renderer);
        const auto withReplica = cache.stats();
        require(replicaTexture != nullptr && firstRendererTexture != nullptr &&
                    replicaTexture != firstRendererTexture &&
                    withReplica.rendererReplicas == beforeReplica.rendererReplicas + 1 &&
                    withReplica.retainedBytes > beforeReplica.retainedBytes,
                "each renderer gets its own counted thumbnail texture replica");
        cache.release_renderer(secondRenderer);
        const auto afterRelease = cache.stats();
        require(cache.find(replicatedPath, secondRenderer) == nullptr &&
                    afterRelease.rendererReplicas + 1 == withReplica.rendererReplicas &&
                    afterRelease.retainedBytes < withReplica.retainedBytes,
                "releasing a renderer destroys only its local replicas and returns their budget");
        SDL_DestroyRenderer(secondRenderer);
        SDL_DestroyWindow(secondWindow);

        const auto unsupported = directory / "negative.svg";
        std::ofstream(unsupported) << "<svg width=\"4\" height=\"4\"></svg>";
        require(cache.request(unsupported) == nullptr,
                "unsupported image formats return a placeholder while preparation runs");
        const auto negativeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (cache.failure_message(unsupported).empty() &&
               std::chrono::steady_clock::now() < negativeDeadline)
        {
            cache.poll_uploads(renderer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(cache.failure_message(unsupported).find("icon") != std::string::npos,
                "negative cache entries retain an explained unsupported-format reason");
        cache.request(unsupported);
        require(!cache.stats().running && cache.stats().pendingPaths == 0,
                "unchanged failed versions are negatively cached instead of decoded every frame");

        write_color_png(unsupported, 88);
        cache.invalidate(unsupported);
        require(wait_for_thumbnail(cache, renderer, unsupported) != nullptr,
                "an invalidation clears a negative result and permits a repaired image to load");
        cache.end_frame();

        cache.clear(renderer);
        const auto stalePath = paths[2];
        require(cache.request(stalePath) == nullptr, "stale-worker fixture must start a decode");
        cache.clear(renderer);
        const auto staleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (cache.stats().running && std::chrono::steady_clock::now() < staleDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cache.poll_uploads(renderer);
        require(cache.stats().entries == 0 && cache.stats().completedBytes == 0,
                "worker completion from a cleared cache generation cannot publish stale textures");

        for (std::size_t index = 0; index < ThumbnailMaxEntries + 14; ++index)
        {
            const auto missing = directory / ("negative-entry-" + std::to_string(index) + ".png");
            cache.request(missing);
            const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (cache.failure_message(missing).empty() &&
                   std::chrono::steady_clock::now() < failureDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            require(!cache.failure_message(missing).empty(),
                    "missing thumbnail versions reach a bounded negative-cache result");
            require(cache.stats().entries <= ThumbnailMaxEntries,
                    "negative and pending entries share the 256-entry cache limit");
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

void test_thumbnail_pixel_budget_accounts_for_full_size_outputs(
    const std::filesystem::path &directory)
{
    SDL_Window *window = SDL_CreateWindow("thumbnail pixel budget test", 64, 64,
                                          SDL_WINDOW_HIDDEN);
    require(window != nullptr, std::string("create thumbnail budget window: ") + SDL_GetError());
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    require(renderer != nullptr, std::string("create thumbnail budget renderer: ") + SDL_GetError());
    {
        ThumbnailCache cache;
        for (std::size_t index = 0; index < ThumbnailMaxEntries; ++index)
        {
            const auto path = directory / ("max-thumbnail-" + std::to_string(index) + ".png");
            write_color_png(path, static_cast<std::uint8_t>(index), 256, 256);
            SDL_Texture *texture = wait_for_thumbnail(cache, renderer, path);
            float width = 0.0f;
            float height = 0.0f;
            const auto stats = cache.stats();
            require(texture != nullptr && SDL_GetTextureSize(texture, &width, &height) &&
                        width <= 256.0f && height <= 256.0f &&
                        stats.retainedBytes <= ThumbnailMaxRetainedBytes &&
                        stats.entries <= ThumbnailMaxEntries,
                    "256-pixel outputs and their retained pixel costs must remain within cache caps");
            cache.end_frame();
        }
        const auto saturated = cache.stats();
        require(saturated.retainedBytes > 60ULL * 1024ULL * 1024ULL &&
                    saturated.retainedBytes <= ThumbnailMaxRetainedBytes,
                "near-cap full-size thumbnails must account decoded output pixels without exceeding 64 MiB");
        cache.clear(renderer);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}
} // namespace

int main()
{
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO), std::string("initialize SDL: ") + SDL_GetError());
        TemporaryDirectory temporary;
        test_valid_image_is_admitted_and_decoded(temporary.path());
        test_bounded_decoder_formats(temporary.path());
        test_oversized_headers_reject_before_decode(temporary.path());
        test_inclusive_working_set_and_fallback(temporary.path());
        test_changed_file_is_rejected(temporary.path());
        test_distinct_invalidations_supersede_paused_thumbnail_preparation(temporary.path());
        test_thumbnail_cache_limits_pins_eviction_and_negative_results(temporary.path());
        test_thumbnail_pixel_budget_accounts_for_full_size_outputs(temporary.path());
        SDL_Quit();
        std::cout << "Resource image preflight cases passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
