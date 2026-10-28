#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <oneapi/tbb/global_control.h>
#include <platformator/objecthandle.h>
#include <platformator/runtime.h>

namespace spatial_test_support
{
struct EndpointIdentity
{
    uint64_t context = 0;
    platformator::ObjectHandle endpoint;
    auto operator<=>(const EndpointIdentity &) const = default;
};

struct CanonicalPair
{
    EndpointIdentity first;
    EndpointIdentity second;
    auto operator<=>(const CanonicalPair &) const = default;

    static CanonicalPair make(EndpointIdentity a, EndpointIdentity b)
    {
        if (b < a)
            std::swap(a, b);
        return {a, b};
    }
};

struct Bounds
{
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;

    bool finiteOrdered() const
    {
        return std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) && std::isfinite(maxY) &&
               minX <= maxX && minY <= maxY;
    }

    bool overlapsInclusive(const Bounds &other) const
    {
        return minX <= other.maxX && maxX >= other.minX && minY <= other.maxY && maxY >= other.minY;
    }
};

struct EndpointState
{
    EndpointIdentity identity;
    Bounds bounds;
    uint64_t group = 0;
    uint64_t mask = 0;
    bool active = true;
};

struct SpatialResult
{
    std::vector<CanonicalPair> candidates;
    std::vector<CanonicalPair> added;
    std::vector<CanonicalPair> removed;
    auto operator<=>(const SpatialResult &) const = default;
};

inline void sortUnique(std::vector<CanonicalPair> &pairs)
{
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
}

inline std::vector<CanonicalPair> exactOverlapOracle(const std::vector<EndpointState> &endpoints)
{
    std::vector<CanonicalPair> result;
    for (size_t i = 0; i < endpoints.size(); ++i)
    {
        const auto &a = endpoints[i];
        if (!a.active || !a.bounds.finiteOrdered())
            continue;
        for (size_t j = i + 1; j < endpoints.size(); ++j)
        {
            const auto &b = endpoints[j];
            if (!b.active || a.identity.context != b.identity.context || !b.bounds.finiteOrdered())
                continue;
            if ((a.group & b.mask) != 0 && (b.group & a.mask) != 0 && a.bounds.overlapsInclusive(b.bounds))
                result.push_back(CanonicalPair::make(a.identity, b.identity));
        }
    }
    sortUnique(result);
    return result;
}

enum class RendererMode
{
    Headless,
    Software
};

class RuntimeFixture
{
  public:
    explicit RuntimeFixture(size_t workerAllowance, RendererMode rendererMode = RendererMode::Headless,
                            int width = 640, int height = 480)
        : workerControl(tbb::global_control::max_allowed_parallelism, checkedWorkerAllowance(workerAllowance)),
          options(makeOptions(rendererMode, width, height)), runtime(std::make_unique<platformator::Runtime>(options))
    {
    }

    RuntimeFixture(const RuntimeFixture &) = delete;
    RuntimeFixture &operator=(const RuntimeFixture &) = delete;

    platformator::Runtime &getRuntime() const
    {
        return *runtime;
    }

    SDL_Renderer *getRenderer() const
    {
        return runtime->getRenderer();
    }

  private:
    static size_t checkedWorkerAllowance(size_t workerAllowance)
    {
        if (workerAllowance == 0)
            throw std::invalid_argument("Worker allowance must be positive");
        return workerAllowance;
    }

    static RuntimeOptions makeOptions(RendererMode rendererMode, int width, int height)
    {
        if (rendererMode == RendererMode::Software)
        {
            setenv("SDL_AUDIODRIVER", "dummy", 1);
            setenv("SDL_VIDEODRIVER", "dummy", 1);
            setenv("SDL_RENDER_DRIVER", "software", 1);
        }
        else
        {
            setenv("SDL_AUDIODRIVER", "dummy", 1);
            setenv("SDL_VIDEODRIVER", "dummy", 1);
            setenv("SDL_RENDER_DRIVER", "software", 1);
        }

        RuntimeOptions result;
        result.windowSettings.headless = rendererMode == RendererMode::Headless;
        result.windowSettings.width = width;
        result.windowSettings.height = height;
        return result;
    }

    tbb::global_control workerControl;
    RuntimeOptions options;
    std::unique_ptr<platformator::Runtime> runtime;
};

struct PixelSnapshot
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    bool operator==(const PixelSnapshot &) const = default;
};

inline PixelSnapshot readRendererPixels(SDL_Renderer *renderer)
{
    if (renderer == nullptr)
        throw std::invalid_argument("Cannot read pixels from a null renderer");

    SDL_Surface *readback = SDL_RenderReadPixels(renderer, nullptr);
    if (readback == nullptr)
        throw std::runtime_error(std::string("SDL_RenderReadPixels failed: ") + SDL_GetError());
    const auto destroySurface = [](SDL_Surface *surface) { SDL_DestroySurface(surface); };
    std::unique_ptr<SDL_Surface, decltype(destroySurface)> source(readback, destroySurface);

    SDL_Surface *converted = SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32);
    if (converted == nullptr)
        throw std::runtime_error(std::string("SDL_ConvertSurface failed: ") + SDL_GetError());
    std::unique_ptr<SDL_Surface, decltype(destroySurface)> rgba(converted, destroySurface);

    PixelSnapshot result;
    result.width = rgba->w;
    result.height = rgba->h;
    result.rgba.resize(static_cast<size_t>(result.width) * result.height * 4);
    const size_t rowBytes = static_cast<size_t>(result.width) * 4;
    const auto *pixels = static_cast<const uint8_t *>(rgba->pixels);
    for (int y = 0; y < result.height; ++y)
        std::memcpy(result.rgba.data() + static_cast<size_t>(y) * rowBytes,
                    pixels + static_cast<size_t>(y) * rgba->pitch, rowBytes);
    return result;
}
} // namespace spatial_test_support
