#include "buildconfig.h"
#include "benchmarkbuildconfig.h"
#include "benchmark/benchmark.h"
#include "render_culling_fixture.h"
#include "core/runtimeaccess.h"
#include "rendering/sdlwindow.h"

#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <oneapi/tbb/global_control.h>
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

#if PLATFORMATOR_ENABLE_WORK_PROBES
#include <atomic>
#include <new>
#include <thread>
namespace linear_render_allocation_probe
{
struct Snapshot
{
    uint64_t calls = 0;
    uint64_t bytes = 0;
};

inline std::atomic<uint64_t> epoch{0};
inline std::atomic<uint64_t> inFlight{0};
inline std::atomic<uint64_t> calls{0};
inline std::atomic<uint64_t> bytes{0};
inline std::atomic<bool> scopeActive{false};

inline void record(std::size_t size) noexcept
{
    const uint64_t observedEpoch = epoch.load(std::memory_order_acquire);
    if ((observedEpoch & 1u) == 0)
        return;

    inFlight.fetch_add(1, std::memory_order_acq_rel);
    if (epoch.load(std::memory_order_acquire) == observedEpoch)
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        bytes.fetch_add(size, std::memory_order_relaxed);
    }
    inFlight.fetch_sub(1, std::memory_order_release);
}

inline bool isActive() noexcept
{
    return (epoch.load(std::memory_order_acquire) & 1u) != 0;
}

class ScopedObservation
{
  public:
    explicit ScopedObservation(Snapshot &snapshot) : snapshot_(snapshot)
    {
        bool expected = false;
        if (!scopeActive.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            std::abort();
        calls.store(0, std::memory_order_relaxed);
        bytes.store(0, std::memory_order_relaxed);
        epoch.fetch_add(1, std::memory_order_release);
    }

    ScopedObservation(const ScopedObservation &) = delete;
    ScopedObservation &operator=(const ScopedObservation &) = delete;

    ~ScopedObservation() noexcept
    {
        epoch.fetch_add(1, std::memory_order_acq_rel);
        while (inFlight.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        snapshot_.calls = calls.load(std::memory_order_relaxed);
        snapshot_.bytes = bytes.load(std::memory_order_relaxed);
        scopeActive.store(false, std::memory_order_release);
    }

  private:
    Snapshot &snapshot_;
};
}
void *operator new(std::size_t size)
{
    if (void *memory = std::malloc(size == 0 ? 1 : size))
    {
        linear_render_allocation_probe::record(size);
        return memory;
    }
    throw std::bad_alloc();
}
void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }
#endif

namespace
{
using render_culling_test::DeterministicFixtureTrajectory;
using render_culling_test::FixturePose;
using render_culling_test::FixtureTexture;
using render_culling_test::FixtureTrajectoryProfile;
using render_culling_test::RenderCullingFixture;
#if PLATFORMATOR_ENABLE_WORK_PROBES
using platformator_test::SpriteIdentity;
#endif

constexpr int OUTPUT_WIDTH = 320;
constexpr int OUTPUT_HEIGHT = 240;

struct Options
{
    FixtureTrajectoryProfile profile = FixtureTrajectoryProfile::Sparse;
    std::string profileName = "sparse";
    std::string renderStrategy = "cached-linear";
    std::string orderingVariant = "rebuild";
    std::string layoutName = "profile-defined";
    std::string sizeDistribution = "standard";
    bool denseZEdit = false;
    bool rejectedZEdit = false;
    bool renderStrategyExplicit = false;
    size_t changedPercent = 0;
    bool changedPercentExplicit = false;
    size_t population = 1000;
    size_t workers = 1;
    uint64_t seed = 0;
    double dt = 1.0 / 120.0;
    size_t warmupFrames = 120;
    size_t measureFrames = 600;
    size_t repetitions = 1;
    size_t runIndex = 0;
    std::optional<size_t> captureFrameIndex;
    bool disableCulling = false;
    bool headless = false;
    bool validateRendering = false;
    bool validateResources = false;
    std::string csvOutput;
    std::string evidenceOutput;
    std::string validationProof;
    std::string resourceProof;
    std::string assetRoot;
};

size_t parseSize(std::string_view value, std::string_view name, bool allowZero)
{
    size_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || (!allowZero && parsed == 0))
        throw std::invalid_argument(std::string(name) + (allowZero ? " must be a nonnegative integer" : " must be positive"));
    return parsed;
}

double parseDt(std::string_view value)
{
    std::string copy(value);
    char *end = nullptr;
    const double parsed = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(parsed) || parsed <= 0.0)
        throw std::invalid_argument("--dt must be finite and positive");
    return parsed;
}

std::string requireValue(int argc, char **argv, int &index, const std::string &option)
{
    if (index + 1 >= argc)
        throw std::invalid_argument("Missing value for " + option);
    return argv[++index];
}

Options parseOptions(int argc, char **argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--profile")
        {
            options.profileName = requireValue(argc, argv, index, argument);
            if (options.profileName == "visible") options.profile = FixtureTrajectoryProfile::Visible;
            else if (options.profileName == "offscreen") options.profile = FixtureTrajectoryProfile::Offscreen;
            else if (options.profileName == "sparse") options.profile = FixtureTrajectoryProfile::Sparse;
            else if (options.profileName == "clustered") options.profile = FixtureTrajectoryProfile::Clustered;
            else if (options.profileName == "crossing") options.profile = FixtureTrajectoryProfile::Crossing;
            else if (options.profileName == "camera-pan") options.profile = FixtureTrajectoryProfile::CameraPan;
            else if (options.profileName == "camera-stationary") options.profile = FixtureTrajectoryProfile::CameraStationary;
            else if (options.profileName == "churn") options.profile = FixtureTrajectoryProfile::Churn;
            else if (options.profileName == "rotated") options.profile = FixtureTrajectoryProfile::Rotated;
            else if (options.profileName == "visible-motion") options.profile = FixtureTrajectoryProfile::VisibleMotion;
            else if (options.profileName == "changed") options.profile = FixtureTrajectoryProfile::ChangedSubset;
            else if (options.profileName == "spread") options.profile = FixtureTrajectoryProfile::Spread;
            else if (options.profileName == "giant") options.profile = FixtureTrajectoryProfile::Giant;
            else if (options.profileName == "large-offset") options.profile = FixtureTrajectoryProfile::LargeOffset;
            else if (options.profileName == "output-resize") options.profile = FixtureTrajectoryProfile::OutputResize;
            else if (options.profileName == "scene-default") options.profile = FixtureTrajectoryProfile::SceneDefault;
            else if (options.profileName == "scene-stacking") options.profile = FixtureTrajectoryProfile::SceneStacking;
            else if (options.profileName == "dense-z-edit")
            {
                options.profile = FixtureTrajectoryProfile::Visible;
                options.denseZEdit = true;
            }
            else if (options.profileName == "rejected-z-edit")
            {
                options.profile = FixtureTrajectoryProfile::Sparse;
                options.rejectedZEdit = true;
            }
            else throw std::invalid_argument("Unknown render culling profile: " + options.profileName);
        }
        else if (argument == "--ordering-variant")
        {
            options.orderingVariant = requireValue(argc, argv, index, argument);
            if (options.orderingVariant != "rebuild" && options.orderingVariant != "forced-sort" &&
                options.orderingVariant != "repair")
                throw std::invalid_argument("Unknown ordering variant: " + options.orderingVariant);
        }
        else if (argument == "--render-strategy")
        {
            options.renderStrategy = requireValue(argc, argv, index, argument);
            if (options.renderStrategy == "indexed" || options.renderStrategy == "current-linear")
                throw std::invalid_argument(
                    "Retired render strategy '" + options.renderStrategy +
                    "': use 'cached-linear' or pass --no-render-culling");
            if (options.renderStrategy != "cached-linear")
                throw std::invalid_argument("Unknown render strategy: " + options.renderStrategy);
            options.renderStrategyExplicit = true;
        }
        else if (argument == "--sprite-count") options.population = parseSize(requireValue(argc, argv, index, argument), argument, true);
        else if (argument == "--workers") options.workers = parseSize(requireValue(argc, argv, index, argument), argument, false);
        else if (argument == "--changed-percent")
        {
            options.changedPercent = parseSize(requireValue(argc, argv, index, argument), argument, true);
            if (options.changedPercent > 100)
                throw std::invalid_argument("--changed-percent must be in [0, 100]");
            options.changedPercentExplicit = true;
        }
        else if (argument == "--layout")
        {
            options.layoutName = requireValue(argc, argv, index, argument);
            if (options.layoutName != "profile-defined" && options.layoutName != "constant-density-spread" &&
                options.layoutName != "fixed-area" && options.layoutName != "fixed-area-skew" &&
                options.layoutName != "uniform-sparse" && options.layoutName != "large-common-offset" &&
                options.layoutName != "continuous-crossing" && options.layoutName != "stable-slot-churn" &&
                options.layoutName != "scene-natural")
                throw std::invalid_argument("Unknown render-culling layout: " + options.layoutName);
        }
        else if (argument == "--size-distribution")
        {
            options.sizeDistribution = requireValue(argc, argv, index, argument);
            if (options.sizeDistribution != "standard" && options.sizeDistribution != "seeded-rotated" &&
                options.sizeDistribution != "giant-side-list")
                throw std::invalid_argument("Unknown render-culling size distribution: " + options.sizeDistribution);
        }
        else if (argument == "--seed") options.seed = parseSize(requireValue(argc, argv, index, argument), argument, true);
        else if (argument == "--warmup-frames") options.warmupFrames = parseSize(requireValue(argc, argv, index, argument), argument, true);
        else if (argument == "--measure-frames") options.measureFrames = parseSize(requireValue(argc, argv, index, argument), argument, false);
        else if (argument == "--repetitions") options.repetitions = parseSize(requireValue(argc, argv, index, argument), argument, false);
        else if (argument == "--run-index") options.runIndex = parseSize(requireValue(argc, argv, index, argument), argument, true);
        else if (argument == "--capture-frame-index") options.captureFrameIndex = parseSize(requireValue(argc, argv, index, argument), argument, true);
        else if (argument == "--dt") options.dt = parseDt(requireValue(argc, argv, index, argument));
        else if (argument == "--csv-output") options.csvOutput = requireValue(argc, argv, index, argument);
        else if (argument == "--evidence-output") options.evidenceOutput = requireValue(argc, argv, index, argument);
        else if (argument == "--validation-proof") options.validationProof = requireValue(argc, argv, index, argument);
        else if (argument == "--resource-proof") options.resourceProof = requireValue(argc, argv, index, argument);
        else if (argument == "--asset-root") options.assetRoot = requireValue(argc, argv, index, argument);
        else if (argument == "--no-render-culling") options.disableCulling = true;
        else if (argument == "--headless") options.headless = true;
        else if (argument == "--validate-rendering") options.validateRendering = true;
        else if (argument == "--validate-resources") options.validateResources = true;
        else throw std::invalid_argument("Unknown linear render probe argument: " + argument);
    }
    if (options.csvOutput.empty())
        throw std::invalid_argument("--csv-output is required");
    if (options.runIndex >= options.repetitions)
        throw std::invalid_argument("--run-index must be less than --repetitions");
    if (options.captureFrameIndex && !options.validateRendering)
        throw std::invalid_argument("--capture-frame-index requires --validate-rendering");
    if (options.captureFrameIndex && *options.captureFrameIndex >= options.measureFrames)
        throw std::invalid_argument("--capture-frame-index must be less than --measure-frames");
    if (options.validateRendering && options.validateResources)
        throw std::invalid_argument("--validate-rendering and --validate-resources are mutually exclusive");
    if (options.disableCulling && options.renderStrategyExplicit)
        throw std::invalid_argument("--no-render-culling cannot be combined with --render-strategy");
    if ((options.validateRendering || options.validateResources) && options.evidenceOutput.empty())
        throw std::invalid_argument("Validation captures require --evidence-output");
    return options;
}

template <typename Settings> constexpr bool supportsCullingSetting()
{
    return requires(Settings &settings) { settings.enableRenderCulling = true; };
}

std::string modeName(const Options &options)
{
    if (options.renderStrategyExplicit)
    {
        if constexpr (!supportsCullingSetting<WindowSettings>())
        {
            if (options.renderStrategy != "current-linear")
                throw std::invalid_argument("The frozen reference supports only current-linear and submit-all");
        }
        return options.renderStrategy;
    }
    if constexpr (!supportsCullingSetting<WindowSettings>())
        return "pre-feature";
    if (options.disableCulling)
        return "disabled";
    return SDLWindow::renderCullingModeName(SDLWindow::resolveRequestedRenderCullingModeFromEnvironment());
}

void setRequestedStrategy(const Options &options)
{
    if (!options.renderStrategyExplicit)
        return;
#if defined(_WIN32)
    if (_putenv_s("PLATFORMATOR_RENDER_CULLING_MODE", options.renderStrategy.c_str()) != 0)
        throw std::runtime_error("Could not set the requested render strategy");
#else
    if (setenv("PLATFORMATOR_RENDER_CULLING_MODE", options.renderStrategy.c_str(), 1) != 0)
        throw std::runtime_error("Could not set the requested render strategy");
#endif
}

std::string sha256(std::string_view input)
{
    static constexpr std::array<uint32_t, 64> constants = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    std::vector<uint8_t> bytes(input.begin(), input.end());
    const uint64_t bitLength = static_cast<uint64_t>(bytes.size()) * 8u;
    bytes.push_back(0x80u);
    while ((bytes.size() % 64) != 56) bytes.push_back(0);
    for (int byte = 7; byte >= 0; --byte) bytes.push_back(static_cast<uint8_t>(bitLength >> (byte * 8)));
    std::array<uint32_t, 8> state = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                     0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    const auto rotate = [](uint32_t value, unsigned count) { return (value >> count) | (value << (32 - count)); };
    for (size_t offset = 0; offset < bytes.size(); offset += 64)
    {
        std::array<uint32_t, 64> words{};
        for (size_t index = 0; index < 16; ++index)
        {
            const size_t base = offset + index * 4;
            words[index] = (uint32_t(bytes[base]) << 24) | (uint32_t(bytes[base + 1]) << 16) |
                           (uint32_t(bytes[base + 2]) << 8) | uint32_t(bytes[base + 3]);
        }
        for (size_t index = 16; index < words.size(); ++index)
        {
            const uint32_t s0 = rotate(words[index - 15], 7) ^ rotate(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const uint32_t s1 = rotate(words[index - 2], 17) ^ rotate(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto [a0,b0,c0,d0,e0,f0,g0,h0] = state;
        uint32_t a=a0,b=b0,c=c0,d=d0,e=e0,f=f0,g=g0,h=h0;
        for (size_t index = 0; index < words.size(); ++index)
        {
            const uint32_t s1 = rotate(e,6) ^ rotate(e,11) ^ rotate(e,25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choice + constants[index] + words[index];
            const uint32_t s0 = rotate(a,2) ^ rotate(a,13) ^ rotate(a,22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (uint32_t word : state) result << std::setw(8) << word;
    return result.str();
}

std::string sha256File(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read evidence file: " + path.string());
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return sha256(bytes);
}

nlohmann::json currentAffinity()
{
    nlohmann::json cpus = nlohmann::json::array();
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &allowed)) cpus.push_back(cpu);
#endif
    return cpus;
}

#if PLATFORMATOR_ENABLE_WORK_PROBES
struct Readback
{
    std::vector<uint8_t> pixels;
    std::string hash;
    std::string error;
    size_t callbacks = 0;
    int width = OUTPUT_WIDTH;
    int height = OUTPUT_HEIGHT;
    int storageWidth = OUTPUT_WIDTH;
    int storageHeight = OUTPUT_HEIGHT;
    static void capture(SDL_Renderer *renderer, int width, int height, void *context)
    {
        auto *self = static_cast<Readback *>(context);
        ++self->callbacks;
        SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
        if (surface == nullptr)
        {
            self->error = std::string("SDL_RenderReadPixels: ") + SDL_GetError();
            return;
        }
        SDL_Surface *rgba = surface;
        if (surface->format != SDL_PIXELFORMAT_RGBA32)
        {
            rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(surface);
            if (rgba == nullptr)
            {
                self->error = std::string("SDL_ConvertSurface: ") + SDL_GetError();
                return;
            }
        }
        const int capturedWidth = rgba->w;
        const int capturedHeight = rgba->h;
        // A resize can update SDL's renderer output before the next event pump
        // refreshes SDLWindow's cached size. Capture the actual surface now;
        // the cached selection falls back conservatively during that frame.
        if (capturedWidth > self->storageWidth || capturedHeight > self->storageHeight ||
            self->pixels.size() != static_cast<size_t>(self->storageWidth) * self->storageHeight * 4)
        {
            self->error = "Unexpected renderer readback dimensions or unreserved destination: requested=" +
                          std::to_string(width) + "x" + std::to_string(height) + " raw=" +
                          std::to_string(rgba->w) + "x" + std::to_string(rgba->h) + " storage=" +
                          std::to_string(self->storageWidth) + "x" + std::to_string(self->storageHeight);
            SDL_DestroySurface(rgba);
            return;
        }
        if (SDL_MUSTLOCK(rgba) && !SDL_LockSurface(rgba))
        {
            self->error = std::string("SDL_LockSurface: ") + SDL_GetError();
            SDL_DestroySurface(rgba);
            return;
        }
        for (int row = 0; row < capturedHeight; ++row)
            std::memcpy(self->pixels.data() + static_cast<size_t>(row) * self->storageWidth * 4,
                        static_cast<const uint8_t *>(rgba->pixels) + static_cast<size_t>(row) * rgba->pitch,
                        static_cast<size_t>(capturedWidth) * 4);
        if (SDL_MUSTLOCK(rgba)) SDL_UnlockSurface(rgba);
        SDL_DestroySurface(rgba);
        self->width = capturedWidth;
        self->height = capturedHeight;
        uint64_t hash = 14695981039346656037ull;
        for (int row = 0; row < self->height; ++row)
            for (int column = 0; column < self->width * 4; ++column)
            {
                hash ^= self->pixels[static_cast<size_t>(row) * self->storageWidth * 4 + column];
                hash *= 1099511628211ull;
            }
        std::ostringstream hashText;
        hashText << std::hex << std::setfill('0') << std::setw(16) << hash;
        self->hash = hashText.str();
    }
};

long long residentMemoryKiB()
{
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    long long virtualPages = 0;
    long long residentPages = 0;
    if (statm >> virtualPages >> residentPages)
        return residentPages * static_cast<long long>(sysconf(_SC_PAGESIZE)) / 1024;
#endif
    return -1;
}

void writePpm(const std::filesystem::path &path, const Readback &image)
{
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (int row = 0; row < image.height; ++row)
        for (int column = 0; column < image.width; ++column)
    {
        const size_t pixel = static_cast<size_t>(row) * image.storageWidth + column;
        const char rgb[3] = {static_cast<char>(image.pixels[pixel * 4]),
                             static_cast<char>(image.pixels[pixel * 4 + 1]),
                             static_cast<char>(image.pixels[pixel * 4 + 2])};
        output.write(rgb, sizeof(rgb));
    }
}
#endif

void buildFixture(RenderCullingFixture &fixture, const Options &options)
{
    fixture.reserveSprites(options.population);
    if (options.profile == FixtureTrajectoryProfile::SceneDefault ||
        options.profile == FixtureTrajectoryProfile::SceneStacking)
    {
        const char *scenePath = options.profile == FixtureTrajectoryProfile::SceneDefault
                                    ? "assets/scenes/default.scene" : "assets/scenes/stacking.scene";
        fixture.loadSceneAndCreateCamera(scenePath, 640.0f, 480.0f);
        if (fixture.records().size() != options.population)
            throw std::runtime_error("Loaded scene Sprite population differs from the declared case identity");
        return;
    }
    const DeterministicFixtureTrajectory trajectory(options.profile, options.population,
                                                     static_cast<uint32_t>(options.seed),
                                                     options.changedPercentExplicit ? options.changedPercent : 0,
                                                     options.layoutName == "constant-density-spread" ||
                                                         options.profile == FixtureTrajectoryProfile::Spread);
    const bool rotated = options.profile == FixtureTrajectoryProfile::Rotated;
    const bool wideCamera = rotated;
    if (wideCamera)
        fixture.camera()->setCamera({0.0f, 0.0f, 640.0f, 160.0f});
    if (options.profile == FixtureTrajectoryProfile::LargeOffset ||
        options.profile == FixtureTrajectoryProfile::CameraStationary)
    {
        const auto [cameraX, cameraY] = trajectory.cameraPosition(0);
        fixture.camera()->setCamera({cameraX, cameraY, 320.0f, 240.0f});
    }
    if (options.denseZEdit)
    {
        for (size_t slot = 0; slot < options.population; ++slot)
        {
            const float x = 16.0f + static_cast<float>(slot % 100) * 2.88f;
            const float y = 16.0f + static_cast<float>((slot / 100) % 100) * 2.08f;
            auto &rec = fixture.addSprite(slot, 0, x, y, 1.0f, 1.0f,
                                          slot % 3 == 0 ? FixtureTexture::RedCircle : FixtureTexture::BlackSquare,
                                          SDL_FLIP_NONE, true, 0.0f);
            rec.sprite->setZOrder(static_cast<std::int32_t>(slot % 257) - 128);
        }
        return;
    }
    if (options.rejectedZEdit)
    {
        const size_t visibleCount = std::min<size_t>(64, options.population);
        for (size_t slot = 0; slot < options.population; ++slot)
        {
            const size_t offscreen = slot >= visibleCount ? (slot - visibleCount) : 0;
            const float x = slot < visibleCount ? 16.0f + static_cast<float>(slot % 8) * 32.0f
                                                : 500.0f + static_cast<float>(offscreen % 100) * 80.0f;
            const float y = slot < visibleCount ? 16.0f + static_cast<float>(slot / 8) * 24.0f
                                                : 500.0f + static_cast<float>(offscreen / 100) * 80.0f;
            auto &rec = fixture.addSprite(slot, 0, x, y, 8.0f, 8.0f,
                                          slot % 3 == 0 ? FixtureTexture::RedCircle : FixtureTexture::BlackSquare,
                                          SDL_FLIP_NONE, true, 0.0f);
            rec.sprite->setZOrder(static_cast<std::int32_t>(slot % 257) - 128);
        }
        return;
    }
    const auto [spriteWidth, spriteHeight] = trajectory.spriteDimensions();
    for (size_t slot = 0; slot < options.population; ++slot)
    {
        const FixturePose pose = trajectory.pose(slot, 0);
        fixture.addSprite(slot, pose.generation, pose.x, pose.y, spriteWidth, spriteHeight,
                          slot % 3 == 0 ? FixtureTexture::RedCircle : FixtureTexture::BlackSquare,
                          SDL_FLIP_NONE, true, pose.rotationRadians);
    }
}

std::vector<std::int32_t> computeDeterministicZOrders(const Options &options, size_t step)
{
    std::vector<std::int32_t> zOrders(options.population, 0);
    for (size_t slot = 0; slot < options.population; ++slot)
        zOrders[slot] = static_cast<std::int32_t>(slot % 257) - 128;
    if (options.denseZEdit && options.population > 0)
    {
        for (size_t t = 0; t <= step; ++t)
        {
            const bool isEditStep = t < options.warmupFrames
                                        ? (t + 1 < options.warmupFrames && (t % 2) == 0)
                                        : (((t - options.warmupFrames) % 2) == 1);
            if (!isEditStep)
                continue;
            const size_t slot = (t * 131 + 17) % options.population;
            zOrders[slot] = zOrders[slot] >= 0 ? (-200 - static_cast<std::int32_t>(t))
                                               : (200 + static_cast<std::int32_t>(t));
        }
    }
    else if (options.rejectedZEdit && options.population > 64)
    {
        const size_t rejectedCount = options.population - 64;
        const size_t batch = std::min<size_t>(100, rejectedCount);
        for (size_t t = 0; t <= step; ++t)
        {
            for (size_t i = 0; i < batch; ++i)
            {
                const size_t slot = 64 + ((t * 100 + i) % rejectedCount);
                zOrders[slot] = zOrders[slot] >= 0 ? (-200 - static_cast<std::int32_t>(t + i))
                                                   : (200 + static_cast<std::int32_t>(t + i));
            }
        }
    }
    return zOrders;
}

bool crossesPeerInSortedOrder(const std::vector<std::int32_t> &beforeZ,
                              const std::vector<std::int32_t> &afterZ,
                              size_t changedSlot)
{
    const size_t n = beforeZ.size();
    if (changedSlot >= n || afterZ.size() != n || beforeZ[changedSlot] == afterZ[changedSlot])
        return false;
    size_t rankBefore = 0;
    size_t rankAfter = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (i == changedSlot)
            continue;
        if (beforeZ[i] < beforeZ[changedSlot] || (beforeZ[i] == beforeZ[changedSlot] && i < changedSlot))
            ++rankBefore;
        if (afterZ[i] < afterZ[changedSlot] || (afterZ[i] == afterZ[changedSlot] && i < changedSlot))
            ++rankAfter;
    }
    return rankBefore != rankAfter;
}

void verifyDenseZEditStep(size_t frameIndex, const std::vector<std::int32_t> &beforeZ,
                          const std::vector<std::int32_t> &afterZ, size_t poseChanges)
{
    if (beforeZ.size() != afterZ.size())
        throw std::runtime_error("dense-z-edit population size changed unexpectedly");
    if (poseChanges != 0)
        throw std::runtime_error("dense-z-edit frame unexpectedly mutated sprite pose");
    size_t changedCount = 0;
    size_t changedSlot = 0;
    for (size_t slot = 0; slot < beforeZ.size(); ++slot)
    {
        if (beforeZ[slot] != afterZ[slot])
        {
            ++changedCount;
            changedSlot = slot;
        }
    }
    const bool expectEdit = (frameIndex % 2) == 1;
    if (!expectEdit)
    {
        if (changedCount != 0)
            throw std::runtime_error("dense-z-edit clean frame unexpectedly mutated Z order");
        return;
    }
    if (changedCount != 1)
        throw std::runtime_error("dense-z-edit edit frame omitted or multiplied Z mutation");
    if (!crossesPeerInSortedOrder(beforeZ, afterZ, changedSlot))
        throw std::runtime_error("dense-z-edit edit frame did not cross any peer rank in sorted order");
}

void verifyRejectedZEditStep(const Options &options, size_t trajectoryStep,
                             const std::vector<std::int32_t> &beforeZ,
                             const std::vector<std::int32_t> &afterZ, size_t poseChanges)
{
    if (beforeZ.size() != options.population || afterZ.size() != options.population)
        throw std::runtime_error("rejected-z-edit population size mismatch");
    if (poseChanges != 0)
        throw std::runtime_error("rejected-z-edit frame unexpectedly mutated sprite pose");
    std::vector<std::int32_t> expectedBeforeZ(options.population, 0);
    if (trajectoryStep == 0)
    {
        for (size_t slot = 0; slot < options.population; ++slot)
            expectedBeforeZ[slot] = static_cast<std::int32_t>(slot % 257) - 128;
    }
    else
    {
        expectedBeforeZ = computeDeterministicZOrders(options, trajectoryStep - 1);
    }
    const std::vector<std::int32_t> expectedAfterZ = computeDeterministicZOrders(options, trajectoryStep);
    if (beforeZ != expectedBeforeZ)
        throw std::runtime_error("rejected-z-edit pre-step Z orders diverged from expected schedule");
    if (afterZ != expectedAfterZ)
        throw std::runtime_error("rejected-z-edit post-step Z orders diverged from expected slots or values");
    if (options.population > 64)
    {
        size_t expectedChanged = 0;
        for (size_t slot = 0; slot < options.population; ++slot)
        {
            if (expectedBeforeZ[slot] != expectedAfterZ[slot])
            {
                if (slot < 64)
                    throw std::runtime_error("rejected-z-edit schedule mutated a visible slot");
                ++expectedChanged;
            }
        }
        if (expectedChanged == 0)
            throw std::runtime_error("rejected-z-edit frame produced zero expected rejected Z mutations");
    }
}

void verifyCrossingPoseStep(const Options &options, const DeterministicFixtureTrajectory &trajectory,
                            size_t trajectoryStep,
                            const std::vector<std::array<float, 3>> &beforePoses,
                            const std::vector<std::array<float, 3>> &afterPoses,
                            size_t changedZOrders)
{
    if (changedZOrders != 0)
        throw std::runtime_error("crossing frame unexpectedly mutated Z order");
    if (beforePoses.size() != options.population || afterPoses.size() != options.population)
        throw std::runtime_error("crossing population size mismatch");
    const size_t previousStep = trajectoryStep == 0 ? 0 : trajectoryStep - 1;
    size_t observedChanged = 0;
    size_t expectedChanged = 0;
    for (size_t slot = 0; slot < options.population; ++slot)
    {
        const FixturePose expected = trajectory.pose(slot, trajectoryStep);
        if (afterPoses[slot][0] != expected.x || afterPoses[slot][1] != expected.y ||
            afterPoses[slot][2] != expected.rotationRadians)
            throw std::runtime_error("crossing sprite pose diverged from deterministic fixture schedule");
        const FixturePose expectedPrev = trajectory.pose(slot, previousStep);
        if (expectedPrev.x != expected.x || expectedPrev.y != expected.y ||
            expectedPrev.rotationRadians != expected.rotationRadians)
            ++expectedChanged;
        if (beforePoses[slot][0] != afterPoses[slot][0] || beforePoses[slot][1] != afterPoses[slot][1] ||
            beforePoses[slot][2] != afterPoses[slot][2])
            ++observedChanged;
    }
    if (observedChanged != expectedChanged || (options.population > 0 && trajectoryStep > 0 && observedChanged == 0))
        throw std::runtime_error("crossing control observed pose changes differed from fixture schedule");
}

#if PLATFORMATOR_ENABLE_WORK_PROBES
std::vector<SpriteIdentity> independentVisibleOracle(const Options &options,
                                                       const DeterministicFixtureTrajectory &trajectory,
                                                       const RenderCullingFixture &fixture,
                                                       size_t step, float cameraWidth, float cameraHeight,
                                                       int outputWidth, int outputHeight)
{
    std::vector<SpriteIdentity> visible;
    visible.reserve(options.population);
    const double scaleX = static_cast<double>(outputWidth) / cameraWidth;
    const double scaleY = static_cast<double>(outputHeight) / cameraHeight;
    if (options.profile == FixtureTrajectoryProfile::SceneDefault ||
        options.profile == FixtureTrajectoryProfile::SceneStacking)
    {
        const SDL_FRect camera = fixture.camera()->getCamera();
        for (const auto &record : fixture.records())
        {
            const GameObject *object = record.object;
            const Sprite *sprite = record.sprite;
            if (object->getParent() != nullptr)
                throw std::runtime_error("Shipped scene oracle currently requires root Sprite objects");
            if (!object->getActive() || sprite->getTexture() == nullptr)
                continue;
            const auto position = object->getPosition();
            const auto objectScale = object->getScale();
            const double centerX = (static_cast<double>(position.x()) - camera.x) * scaleX;
            const double centerY = (static_cast<double>(position.y()) - camera.y) * scaleY;
            const double halfWidth = static_cast<double>(sprite->getWidthWithoutScale()) * objectScale.x() * scaleX * 0.5;
            const double halfHeight = static_cast<double>(sprite->getHeightWithoutScale()) * objectScale.y() * scaleY * 0.5;
            const double radians = static_cast<double>(object->getRotation());
            const double sine = std::sin(radians);
            const double cosine = std::cos(radians);
            double minX = std::numeric_limits<double>::infinity();
            double minY = std::numeric_limits<double>::infinity();
            double maxX = -std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            for (const double localX : {-halfWidth, halfWidth})
                for (const double localY : {-halfHeight, halfHeight})
                {
                    const double x = centerX + cosine * localX - sine * localY;
                    const double y = centerY + sine * localX + cosine * localY;
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
            if (maxX >= -1.0 && maxY >= -1.0 && minX <= outputWidth + 1.0 && minY <= outputHeight + 1.0)
                visible.push_back({record.identity.slot, record.identity.generation});
        }
        return visible;
    }
    if (options.denseZEdit || options.rejectedZEdit)
    {
        const size_t limit = options.denseZEdit ? options.population : std::min<size_t>(64, options.population);
        const std::vector<std::int32_t> zOrders = computeDeterministicZOrders(options, step);
        std::vector<std::pair<std::int32_t, size_t>> ordered;
        ordered.reserve(limit);
        for (size_t slot = 0; slot < limit; ++slot)
            ordered.emplace_back(zOrders[slot], slot);
        std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
            if (a.first != b.first)
                return a.first < b.first;
            return a.second < b.second;
        });
        for (const auto &[z, slot] : ordered)
        {
            (void)z;
            visible.push_back({slot, 0});
        }
        return visible;
    }
    const auto [cameraX, cameraY] = trajectory.cameraPosition(step);
    const auto [spriteWidth, spriteHeight] = trajectory.spriteDimensions();
    for (size_t slot = 0; slot < options.population; ++slot)
    {
        const FixturePose pose = trajectory.pose(slot, step);
        const double centerX = (static_cast<double>(pose.x) - cameraX) * scaleX;
        const double centerY = (static_cast<double>(pose.y) - cameraY) * scaleY;
        const double halfWidth = spriteWidth * scaleX * 0.5;
        const double halfHeight = spriteHeight * scaleY * 0.5;
        const double sine = std::sin(static_cast<double>(pose.rotationRadians));
        const double cosine = std::cos(static_cast<double>(pose.rotationRadians));
        double minX = std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        for (const double localX : {-halfWidth, halfWidth})
            for (const double localY : {-halfHeight, halfHeight})
            {
                const double x = centerX + cosine * localX - sine * localY;
                const double y = centerY + sine * localX + cosine * localY;
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
        // The independent double-precision oracle allows an extra pixel on
        // every edge, so candidate conservatism may produce false positives.
        if (maxX >= -1.0 && maxY >= -1.0 && minX <= outputWidth + 1.0 && minY <= outputHeight + 1.0)
            visible.push_back({slot, pose.generation});
    }
    return visible;
}

void verifyAllZeroSubmissionOrder(size_t population, const std::vector<SpriteIdentity> &submissions,
                                  const std::vector<SpriteIdentity> &oracle)
{
    bool hasPrevious = false;
    size_t previousSlot = 0;
    for (const auto &identity : submissions)
    {
        if (identity.slot >= population || identity.generation != 0)
            throw std::runtime_error("All-zero validation frame submitted an invalid or unexpected generation identity");
        if (hasPrevious && identity.slot <= previousSlot)
            throw std::runtime_error("All-zero validation frame violated registration submission order");
        previousSlot = identity.slot;
        hasPrevious = true;
    }

    size_t subIndex = 0;
    for (const auto &expected : oracle)
    {
        while (subIndex < submissions.size() && submissions[subIndex].slot < expected.slot)
            ++subIndex;
        if (subIndex >= submissions.size() || submissions[subIndex].slot != expected.slot ||
            submissions[subIndex].generation != expected.generation)
            throw std::runtime_error("All-zero validation frame omitted an independent visibility oracle member");
        ++subIndex;
    }
}
#endif

void updateFixture(RenderCullingFixture &fixture, const Options &options,
                   const DeterministicFixtureTrajectory &trajectory, size_t step)
{
    if (options.denseZEdit)
    {
        if (options.population == 0)
            return;
        const bool isEditStep = step < options.warmupFrames
                                    ? (step + 1 < options.warmupFrames && (step % 2) == 0)
                                    : (((step - options.warmupFrames) % 2) == 1);
        if (!isEditStep)
            return;
        const size_t slot = (step * 131 + 17) % options.population;
        Sprite *sprite = fixture.record(slot).sprite;
        const std::int32_t oldZ = sprite->getZOrder();
        const std::int32_t newZ =
            oldZ >= 0 ? (-200 - static_cast<std::int32_t>(step)) : (200 + static_cast<std::int32_t>(step));
        sprite->setZOrder(newZ);
        return;
    }
    if (options.rejectedZEdit)
    {
        if (options.population <= 64)
            return;
        const size_t rejectedCount = options.population - 64;
        const size_t batch = std::min<size_t>(100, rejectedCount);
        for (size_t i = 0; i < batch; ++i)
        {
            const size_t slot = 64 + ((step * 100 + i) % rejectedCount);
            Sprite *sprite = fixture.record(slot).sprite;
            const std::int32_t oldZ = sprite->getZOrder();
            sprite->setZOrder(oldZ >= 0 ? (-200 - static_cast<std::int32_t>(step + i))
                                        : (200 + static_cast<std::int32_t>(step + i)));
        }
        return;
    }
    if (!fixtureProfileUsesStepCallback(options.profile))
        return;
    if (options.profile == FixtureTrajectoryProfile::CameraPan || options.profile == FixtureTrajectoryProfile::LargeOffset)
    {
        const auto [x, y] = trajectory.cameraPosition(step);
        fixture.camera()->setCamera({x, y, fixture.camera()->getWidth(), fixture.camera()->getHeight()});
    }
    if (options.profile == FixtureTrajectoryProfile::OutputResize && step == 240)
        fixture.resizeOutput(400, 300);
    if (options.profile == FixtureTrajectoryProfile::Churn)
    {
        trajectory.forEachReplacedSlot(step, [&](size_t slot, uint32_t generation) {
            const FixturePose pose = trajectory.pose(slot, step);
            fixture.replaceSprite(slot, generation, pose.x, pose.y, 16.0f, 16.0f,
                                  slot % 3 == 0 ? FixtureTexture::RedCircle : FixtureTexture::BlackSquare,
                                  SDL_FLIP_NONE, pose.rotationRadians);
        });
    }
    else if (options.profile == FixtureTrajectoryProfile::Crossing ||
             options.profile == FixtureTrajectoryProfile::VisibleMotion ||
             options.profile == FixtureTrajectoryProfile::ChangedSubset ||
             options.profile == FixtureTrajectoryProfile::Giant ||
             options.profile == FixtureTrajectoryProfile::LargeOffset ||
             options.profile == FixtureTrajectoryProfile::Clustered ||
             options.profile == FixtureTrajectoryProfile::Rotated)
    {
        for (size_t slot = 0; slot < options.population; ++slot)
        {
            if ((options.profile == FixtureTrajectoryProfile::ChangedSubset ||
                 options.profile == FixtureTrajectoryProfile::Giant ||
                 options.profile == FixtureTrajectoryProfile::LargeOffset ||
                 options.profile == FixtureTrajectoryProfile::Clustered ||
                 options.profile == FixtureTrajectoryProfile::Rotated) && !trajectory.isChanged(slot))
                continue;
            const FixturePose pose = trajectory.pose(slot, step);
            fixture.record(slot).object->setPosition({pose.x, pose.y});
            fixture.record(slot).object->setRotation(pose.rotationRadians);
        }
    }
}

nlohmann::json mappingMetadata(SDL_Renderer *renderer, int width, int height)
{
    const char *rendererName = renderer != nullptr ? SDL_GetRendererName(renderer) : nullptr;
    float scaleX = 0.0f, scaleY = 0.0f;
    int outputWidth = 0, outputHeight = 0, currentWidth = 0, currentHeight = 0;
    int logicalWidth = 0, logicalHeight = 0;
    SDL_RendererLogicalPresentation logicalMode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_Rect viewport{};
    const bool mappingKnown = renderer != nullptr && SDL_GetRenderScale(renderer, &scaleX, &scaleY) &&
                              SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight) &&
                              SDL_GetCurrentRenderOutputSize(renderer, &currentWidth, &currentHeight) &&
                              SDL_GetRenderLogicalPresentation(renderer, &logicalWidth, &logicalHeight, &logicalMode) &&
                              SDL_GetRenderViewport(renderer, &viewport);
    int vsync = 0;
    const bool vsyncKnown = renderer != nullptr && SDL_GetRenderVSync(renderer, &vsync);
    const bool viewportSet = renderer != nullptr && SDL_RenderViewportSet(renderer);
    const bool targetActive = renderer != nullptr && SDL_GetRenderTarget(renderer) != nullptr;
    const bool mappingEligible = mappingKnown && !viewportSet && !targetActive && scaleX == 1.0f && scaleY == 1.0f &&
                                 logicalMode == SDL_LOGICAL_PRESENTATION_DISABLED && logicalWidth == 0 && logicalHeight == 0 &&
                                 viewport.x == 0 && viewport.y == 0 && viewport.w == width && viewport.h == height &&
                                 outputWidth == width && outputHeight == height && currentWidth == width && currentHeight == height;
    return {{"renderer", rendererName != nullptr ? rendererName : "none"},
            {"output_width", width}, {"output_height", height},
            {"actual_output_width", outputWidth}, {"actual_output_height", outputHeight},
            {"current_output_width", currentWidth}, {"current_output_height", currentHeight},
            {"viewport", {viewport.x, viewport.y, viewport.w, viewport.h}},
            {"viewport_set", viewportSet}, {"target_active", targetActive},
            {"scale", {scaleX, scaleY}},
            {"logical_presentation", {logicalWidth, logicalHeight, static_cast<int>(logicalMode)}},
            {"mapping_known", mappingKnown}, {"mapping_eligible", mappingEligible},
            {"vsync_known", vsyncKnown}, {"vsync", vsync}};
}

void ensureFresh(const Options &options)
{
    const std::filesystem::path csv(options.csvOutput);
    auto samples = csv; samples.replace_extension(".samples.csv");
    auto manifest = csv; manifest.replace_extension(".manifest.json");
    auto paired = csv; paired.replace_extension(".paired.csv");
    if (std::filesystem::exists(csv) || std::filesystem::exists(samples) || std::filesystem::exists(manifest) ||
        std::filesystem::exists(paired))
        throw std::runtime_error("Refusing to overwrite an existing benchmark output path");
    if (!options.evidenceOutput.empty())
    {
        for (const std::string suffix : {".evidence.json", ".frames.jsonl", ".representative.ppm", ".resources.json", ".mismatch.ppm"})
            if (std::filesystem::exists(options.evidenceOutput + suffix))
                throw std::runtime_error("Refusing to overwrite existing validation evidence");
    }
}

void run(const Options &options)
{
    ensureFresh(options);
#if (PLATFORMATOR_ENABLE_WORK_PROBES == 0)
    if (options.validateRendering || options.validateResources)
        throw std::runtime_error("Validation captures require a build with PLATFORMATOR_ENABLE_WORK_PROBES=ON");
#endif
    if constexpr (!supportsCullingSetting<WindowSettings>())
        if (options.disableCulling)
            throw std::invalid_argument("--no-render-culling is unavailable in the pre-feature reference runtime");

    setRequestedStrategy(options);
    const auto mode = modeName(options);
    tbb::global_control workerLimit(tbb::global_control::max_allowed_parallelism, options.workers);
    float cameraWidth = options.profile == FixtureTrajectoryProfile::Rotated ||
                                options.profile == FixtureTrajectoryProfile::SceneDefault ||
                                options.profile == FixtureTrajectoryProfile::SceneStacking
                            ? 640.0f : 320.0f;
    float cameraHeight = options.profile == FixtureTrajectoryProfile::Rotated ? 160.0f :
                         options.profile == FixtureTrajectoryProfile::SceneDefault ||
                                 options.profile == FixtureTrajectoryProfile::SceneStacking ? 480.0f : 240.0f;
    uint64_t fixtureConstructionNanoseconds = 0;
    uint64_t fixtureConstructionAllocationCalls = 0;
    uint64_t fixtureConstructionAllocatedBytes = 0;
    std::unique_ptr<RenderCullingFixture> fixtureStorage;
    const auto constructFixture = [&] {
        auto result = std::make_unique<RenderCullingFixture>(OUTPUT_WIDTH, OUTPUT_HEIGHT, cameraWidth, cameraHeight,
                                                              !options.disableCulling, options.headless, options.assetRoot);
        buildFixture(*result, options);
        return result;
    };
    const auto constructionStart = std::chrono::steady_clock::now();
#if PLATFORMATOR_ENABLE_WORK_PROBES
    if (options.validateResources)
    {
        linear_render_allocation_probe::Snapshot snapshot;
        {
            linear_render_allocation_probe::ScopedObservation observation(snapshot);
            fixtureStorage = constructFixture();
        }
        fixtureConstructionAllocationCalls = snapshot.calls;
        fixtureConstructionAllocatedBytes = snapshot.bytes;
    }
    else
#endif
        fixtureStorage = constructFixture();
    const auto constructionEnd = std::chrono::steady_clock::now();
    fixtureConstructionNanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(constructionEnd - constructionStart).count());
    RenderCullingFixture &fixture = *fixtureStorage;
    if (options.profile == FixtureTrajectoryProfile::SceneDefault ||
        options.profile == FixtureTrajectoryProfile::SceneStacking)
    {
        cameraWidth = fixture.camera()->getWidth();
        cameraHeight = fixture.camera()->getHeight();
    }
    const platformator_test::WorkCounters initializationWork = platformator_test::work;
    const DeterministicFixtureTrajectory trajectory(options.profile, options.population,
                                                     static_cast<uint32_t>(options.seed),
                                                     options.changedPercentExplicit ? options.changedPercent : 0,
                                                     options.layoutName == "constant-density-spread" ||
                                                         options.profile == FixtureTrajectoryProfile::Spread);
    SDL_Renderer *renderer = fixture.runtime().getRenderer();
    SDLWindow *window = platformator_detail::RuntimeAccess::window();
    if (window != nullptr)
    {
        if (options.orderingVariant == "forced-sort")
            window->setBenchmarkSpriteOrderMode(SDLWindow::BenchmarkSpriteOrderMode::ForcedFullSort);
        else if (options.orderingVariant == "repair")
            window->setBenchmarkSpriteOrderMode(SDLWindow::BenchmarkSpriteOrderMode::SingleEntryRepair);
        else
            window->setBenchmarkSpriteOrderMode(SDLWindow::BenchmarkSpriteOrderMode::DefaultRebuild);
    }
    struct StepContext
    {
        RenderCullingFixture &fixture;
        const Options &options;
        const DeterministicFixtureTrajectory &trajectory;
        size_t &stepIndex;
    };
    size_t stepIndex = 0;
    StepContext stepCtx{fixture, options, trajectory, stepIndex};
    std::function<void(double)> stepCallback = [&stepCtx](double) {
        updateFixture(stepCtx.fixture, stepCtx.options, stepCtx.trajectory, stepCtx.stepIndex);
    };
    const auto step = [&] {
        bool running = true;
        if (fixtureProfileUsesStepCallback(options.profile) || options.denseZEdit || options.rejectedZEdit)
        {
            if (options.headless)
                fixture.runtime().simulateFrameWithCustomCallback(options.dt, stepCallback);
            else
                running = fixture.runtime().simulateAndRenderFrameWithCustomCallback(options.dt, stepCallback);
        }
        else if (options.headless)
            fixture.runtime().simulateFrame(options.dt);
        else
            running = fixture.runtime().simulateAndRenderFrame(options.dt);
        if (!running)
            throw std::runtime_error("Probe runtime stopped before the requested capture completed");
        ++stepIndex;
    };

    uint64_t coldFirstStepRenderNanoseconds = 0;
    uint64_t coldFirstStepAllocationCalls = 0;
    uint64_t coldFirstStepAllocatedBytes = 0;
    platformator_test::WorkCounters coldFirstStepWork{};
    for (size_t frame = 0; frame < options.warmupFrames; ++frame)
    {
#if PLATFORMATOR_ENABLE_WORK_PROBES
        if (options.validateResources && frame == 0)
        {
            platformator_test::work = {};
            linear_render_allocation_probe::Snapshot snapshot;
            const auto before = std::chrono::steady_clock::now();
            {
                linear_render_allocation_probe::ScopedObservation observation(snapshot);
                step();
            }
            const auto after = std::chrono::steady_clock::now();
            coldFirstStepRenderNanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
            coldFirstStepAllocationCalls = snapshot.calls;
            coldFirstStepAllocatedBytes = snapshot.bytes;
            coldFirstStepWork = platformator_test::work;
        }
        else
#endif
            step();
    }
    WindowSettings fallbackSettings;
    fallbackSettings.headless = options.headless;
    fallbackSettings.enableRenderCulling = !options.disableCulling;
    const auto windowPolicy = window != nullptr
                                  ? window->getRenderCullingPolicy()
                                  : SDLWindow::resolveRenderCullingPolicy(fallbackSettings);
    const char *requestedSelectorName = SDLWindow::renderCullingModeName(windowPolicy.requestedMode);
    const char *resolvedStartupStrategy = SDLWindow::effectiveRenderPolicyName(windowPolicy);
    const auto buildCullingMetadata = [&](const SDLWindow::RenderFallbackCounts &fallbacks) {
        return nlohmann::json{
            {"mode", mode},
            {"requested_mode", options.disableCulling ? "disabled" : mode},
            {"requested_selector", requestedSelectorName},
            {"resolved_startup_strategy", resolvedStartupStrategy},
            {"headless", windowPolicy.headless},
            {"disabled", !windowPolicy.cullingEnabled},
            {"effective_policy", windowPolicy.headless ? "headless-no-rendering" : mode},
            {"enabled", !windowPolicy.headless && windowPolicy.cullingEnabled &&
                            mode != "disabled" && mode != "pre-feature"},
            {"observed_mapping_fallbacks", fallbacks.mappingFallbacks},
            {"observed_numeric_fallbacks", fallbacks.numericFallbacks},
            {"observed_frame_fallbacks", fallbacks.mappingFallbacks + fallbacks.numericFallbacks}};
    };
    const auto resolvedParameters = nlohmann::json{
        {"profile", options.profileName}, {"sprite_count", options.population}, {"seed", options.seed},
        {"trajectory_period_steps", (options.profile == FixtureTrajectoryProfile::Crossing ||
                                      options.profile == FixtureTrajectoryProfile::CameraPan ||
                                      options.profile == FixtureTrajectoryProfile::VisibleMotion ||
                                      options.profile == FixtureTrajectoryProfile::ChangedSubset ||
                                      options.profile == FixtureTrajectoryProfile::Giant ||
                                      options.profile == FixtureTrajectoryProfile::LargeOffset ||
                                      options.profile == FixtureTrajectoryProfile::Rotated ||
                                      options.profile == FixtureTrajectoryProfile::Clustered) ? 240 : 0},
        {"camera_width", cameraWidth}, {"camera_height", cameraHeight},
        {"layout", options.layoutName != "profile-defined" ? options.layoutName :
                   options.profile == FixtureTrajectoryProfile::Clustered ? "clustered-groups" :
                   options.profile == FixtureTrajectoryProfile::Sparse ? "uniform-sparse" :
                   options.profile == FixtureTrajectoryProfile::Spread ? "constant-density-spread" :
                   options.profile == FixtureTrajectoryProfile::SceneDefault ||
                           options.profile == FixtureTrajectoryProfile::SceneStacking ? "scene-natural" : "profile-defined"},
        {"size_distribution", options.sizeDistribution},
        {"changed_percent", options.profile == FixtureTrajectoryProfile::VisibleMotion ||
                             options.profile == FixtureTrajectoryProfile::Crossing ? 100 :
                             options.profile == FixtureTrajectoryProfile::Churn
                                 ? (options.population == 0 ? 0 : trajectory.replacementsPerStep() * 100 / options.population)
                                 : options.changedPercentExplicit ? options.changedPercent : 0},
        {"output_resize_step", options.profile == FixtureTrajectoryProfile::OutputResize ? 240 : 0},
        {"rotation_distribution", options.profile == FixtureTrajectoryProfile::SceneDefault ||
                                 options.profile == FixtureTrajectoryProfile::SceneStacking ? "scene-resolved" :
                                 options.profile == FixtureTrajectoryProfile::Rotated ||
                                 options.profile == FixtureTrajectoryProfile::Giant ? "seeded-uniform-pi" : "zero"},
        {"sprite_width", options.profile == FixtureTrajectoryProfile::SceneDefault ||
                         options.profile == FixtureTrajectoryProfile::SceneStacking
                             ? nlohmann::json(nullptr)
                             : (options.denseZEdit ? nlohmann::json(1.0)
                                                   : (options.rejectedZEdit ? nlohmann::json(8.0)
                                                                            : nlohmann::json(trajectory.spriteDimensions().first)))},
        {"sprite_height", options.profile == FixtureTrajectoryProfile::SceneDefault ||
                          options.profile == FixtureTrajectoryProfile::SceneStacking
                              ? nlohmann::json(nullptr)
                              : (options.denseZEdit ? nlohmann::json(1.0)
                                                    : (options.rejectedZEdit ? nlohmann::json(8.0)
                                                                             : nlohmann::json(trajectory.spriteDimensions().second)))},
        {"camera_schedule", options.profile == FixtureTrajectoryProfile::CameraPan ? "continuous-pan-240" :
                             options.profile == FixtureTrajectoryProfile::LargeOffset ? "large-offset-pan-240" : "fixed"},
        {"output_schedule", options.profile == FixtureTrajectoryProfile::OutputResize ? "320x240-then-400x300-at-240" : "320x240"},
        {"scene_asset", options.profile == FixtureTrajectoryProfile::SceneDefault ? "assets/scenes/default.scene" :
                         options.profile == FixtureTrajectoryProfile::SceneStacking ? "assets/scenes/stacking.scene" : ""},
        {"churn_replacements_per_step", options.profile == FixtureTrajectoryProfile::Churn
                                             ? trajectory.replacementsPerStep() : 0},
        {"churn_minimum_replacements", options.profile == FixtureTrajectoryProfile::Churn ? 1 : 0}};
    auto metadata = nlohmann::json{
        {"scenario", "linear_render_culling"}, {"seed", options.seed}, {"workers", options.workers},
        {"dt", options.dt}, {"warmup_steps", options.warmupFrames}, {"measured_steps", options.measureFrames},
        {"repetitions", options.repetitions}, {"repetition_index", options.runIndex},
        {"rendering", !options.headless}, {"validation_mode", options.validateRendering || options.validateResources ? "per-step" : "boundary-only"},
        {"timing_eligible", !options.validateRendering && !options.validateResources},
        {"resolved_parameters", resolvedParameters},
        {"ordering_variant", options.orderingVariant},
        {"requested_render_strategy", options.renderStrategyExplicit ? options.renderStrategy : mode},
        {"linear_render_culling", buildCullingMetadata(SDLWindow::RenderFallbackCounts{})},
        {"fixture_sha256", PLATFORMATOR_LINEAR_FIXTURE_SHA256},
        {"harness_sha256", PLATFORMATOR_LINEAR_HARNESS_SHA256},
        {"renderer_mapping", mappingMetadata(renderer, OUTPUT_WIDTH, OUTPUT_HEIGHT)},
        {"order_prepare_clock", {{"source", "std::chrono::steady_clock"},
                                 {"boundary", "SDLWindow::prepareDrawOrder CPU elapsed time excluding presentation and frame pacing"}}},
        {"visible_selection_clock", {{"source", "std::chrono::steady_clock"},
                                     {"boundary", "SDLWindow::prepareVisibleSelection CPU elapsed time excluding draw ordering, submission, and presentation"}}},
        {"affinity", currentAffinity()},
        {"validation_proof", nullptr}};
    if (!options.validationProof.empty())
    {
        const auto proof = nlohmann::json::parse(std::ifstream(options.validationProof));
        if (proof.at("profile") != options.profileName || proof.at("sprite_count") != options.population ||
            proof.at("workers") != options.workers || proof.at("seed") != options.seed ||
            proof.at("dt") != options.dt || proof.at("warmup_frames") != options.warmupFrames ||
            proof.at("measure_frames") != options.measureFrames || proof.at("mode") != mode ||
            proof.value("headless", false) != options.headless || proof.value("timing_eligible", true) ||
            proof.value("validation_mode", std::string{}) != "per-step" ||
            proof.value("ordering_variant", std::string("rebuild")) != options.orderingVariant ||
            proof.at("fixture_sha256") != PLATFORMATOR_LINEAR_FIXTURE_SHA256 ||
            proof.at("harness_sha256") != PLATFORMATOR_LINEAR_HARNESS_SHA256)
            throw std::runtime_error("Validation proof does not match this complete fixture identity");
        metadata["validation_proof"] = {{"path", std::filesystem::absolute(options.validationProof).string()},
                                        {"sha256", sha256File(options.validationProof)}};
    }
    if (!options.resourceProof.empty())
    {
        const auto proof = nlohmann::json::parse(std::ifstream(options.resourceProof));
        if (proof.at("profile") != options.profileName || proof.at("sprite_count") != options.population ||
            proof.at("workers") != options.workers || proof.at("seed") != options.seed ||
            proof.at("dt") != options.dt || proof.at("warmup_frames") != options.warmupFrames ||
            proof.at("measure_frames") != options.measureFrames || proof.at("mode") != mode ||
            proof.value("headless", false) != options.headless ||
            proof.value("ordering_variant", std::string("rebuild")) != options.orderingVariant ||
            proof.at("fixture_sha256") != PLATFORMATOR_LINEAR_FIXTURE_SHA256 ||
            proof.at("harness_sha256") != PLATFORMATOR_LINEAR_HARNESS_SHA256 ||
            proof.value("timing_eligible", true) || proof.value("validation_mode", std::string{}) != "per-step")
            throw std::runtime_error("Resource proof does not match this complete fixture identity");
        metadata["resource_observation"] = {{"path", std::filesystem::absolute(options.resourceProof).string()},
                                             {"sha256", sha256File(options.resourceProof)}};
    }

    auto &recorder = platformator_detail::BenchmarkRecorder::getInstance();
    recorder.configure(options.measureFrames, metadata);
    std::vector<uint64_t> pairedStepRenderNanoseconds;
    std::vector<uint64_t> pairedOrderPrepareNanoseconds;
    std::vector<uint64_t> pairedVisibleSelectionNanoseconds;
    if (!options.validateRendering && !options.validateResources)
    {
        pairedStepRenderNanoseconds.reserve(options.measureFrames);
        pairedOrderPrepareNanoseconds.reserve(options.measureFrames);
        pairedVisibleSelectionNanoseconds.reserve(options.measureFrames);
    }

#if PLATFORMATOR_ENABLE_WORK_PROBES
    platformator_test::RenderObservation observation;
    Readback readback;
    std::vector<SpriteIdentity> submissionStorage;
    if (options.validateRendering)
    {
        if (!options.headless)
        {
            readback.storageWidth = options.profile == FixtureTrajectoryProfile::OutputResize ? 400 : OUTPUT_WIDTH;
            readback.storageHeight = options.profile == FixtureTrajectoryProfile::OutputResize ? 300 : OUTPUT_HEIGHT;
            readback.pixels.resize(static_cast<size_t>(readback.storageWidth) * readback.storageHeight * 4);
            observation.identityContext = &fixture;
            observation.resolveIdentity = &RenderCullingFixture::resolveIdentityCallback;
            submissionStorage.reserve(options.population);
            observation.submissions = &submissionStorage;
            observation.readbackContext = &readback;
            observation.beforePresent = &Readback::capture;
        }
    }
#endif
    std::unique_ptr<std::ofstream> framesFile;
    if (options.validateRendering)
    {
        framesFile = std::make_unique<std::ofstream>(options.evidenceOutput + ".frames.jsonl", std::ios::binary);
        framesFile->exceptions(std::ios::badbit | std::ios::failbit);
    }
    uint64_t crossingEntries = 0;
    uint64_t crossingExits = 0;
    std::set<std::pair<size_t, uint32_t>> previousOracleVisible;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    const long long resourceRssBefore = options.validateResources ? residentMemoryKiB() : -1;
#else
    const long long resourceRssBefore = -1;
#endif
    uint64_t resourceAllocationCalls = 0;
    uint64_t resourceAllocatedBytes = 0;
    uint64_t resourceViews = 0, resourceEligibilityChecks = 0, resourceVisibilityChecks = 0,
             resourceSubmissions = 0;
    size_t actualChangedSpriteTotal = 0;
    size_t actualChangedPoseTotal = 0;
    size_t actualChangedZTotal = 0;
    size_t maximumActualChangedSpritesPerStep = 0;
    size_t initialCullingStateCreations = 0;
    size_t initialCullingRecords = 0;
    nlohmann::json cullingResetObservation = nullptr;
    nlohmann::json capacityGrowthObservation = nullptr;
    nlohmann::json resourceCullingWorkJson = nlohmann::json::object();
#if PLATFORMATOR_ENABLE_WORK_PROBES
    platformator_test::WorkCounters resourceCullingWork{};
    platformator_test::WorkCounters cleanPhaseWork{};
    platformator_test::WorkCounters editPhaseWork{};
    size_t cleanPhaseFrames = 0;
    size_t editPhaseFrames = 0;
    const auto accumulateWorkInto = [](platformator_test::WorkCounters &target,
                                       const platformator_test::WorkCounters &source) {
#define ADD_CULLING_WORK_FIELD(field) target.field += source.field
        ADD_CULLING_WORK_FIELD(renderCullingRecordsAdded);
        ADD_CULLING_WORK_FIELD(renderCullingRecordsRemoved);
        ADD_CULLING_WORK_FIELD(renderCullingRecordsRelocated);
        ADD_CULLING_WORK_FIELD(renderCullingDirtyEnqueues);
        ADD_CULLING_WORK_FIELD(renderCullingDirtyCancellations);
        ADD_CULLING_WORK_FIELD(renderCullingBoundsRefreshes);
        ADD_CULLING_WORK_FIELD(renderCullingCandidateVisits);
        ADD_CULLING_WORK_FIELD(renderCullingFullPopulationVisits);
        ADD_CULLING_WORK_FIELD(renderCullingCacheHits);
        ADD_CULLING_WORK_FIELD(renderCullingProjectionRebuilds);
        ADD_CULLING_WORK_FIELD(renderCullingNumericFallbacks);
        ADD_CULLING_WORK_FIELD(renderCullingMappingFallbacks);
        ADD_CULLING_WORK_FIELD(visibleMembershipChecks);
        ADD_CULLING_WORK_FIELD(visibleMembershipChanges);
        ADD_CULLING_WORK_FIELD(spriteOrderPreparations);
        ADD_CULLING_WORK_FIELD(spriteOrderEntries);
        ADD_CULLING_WORK_FIELD(spriteOrderLookupVisits);
        ADD_CULLING_WORK_FIELD(spriteOrderComparisons);
        ADD_CULLING_WORK_FIELD(spriteOrderSorts);
        ADD_CULLING_WORK_FIELD(spriteOrderRepairs);
        ADD_CULLING_WORK_FIELD(spriteOrderMoves);
#undef ADD_CULLING_WORK_FIELD
    };
    const auto updatePeaksFrom = [](platformator_test::WorkCounters &target,
                                    const platformator_test::WorkCounters &source) {
        target.renderCullingOwnedRecords =
            std::max(target.renderCullingOwnedRecords, source.renderCullingOwnedRecords);
        target.renderCullingOwnedRecordCapacity =
            std::max(target.renderCullingOwnedRecordCapacity, source.renderCullingOwnedRecordCapacity);
        target.renderCullingOwnedBytes =
            std::max(target.renderCullingOwnedBytes, source.renderCullingOwnedBytes);
        target.spriteOrderOwnedRecords =
            std::max(target.spriteOrderOwnedRecords, source.spriteOrderOwnedRecords);
        target.spriteOrderOwnedEntryCapacity =
            std::max(target.spriteOrderOwnedEntryCapacity, source.spriteOrderOwnedEntryCapacity);
        target.spriteOrderOwnedBytes =
            std::max(target.spriteOrderOwnedBytes, source.spriteOrderOwnedBytes);
    };
    const auto accumulateCullingWork = [&](const platformator_test::WorkCounters &source) {
        accumulateWorkInto(resourceCullingWork, source);
        updatePeaksFrom(resourceCullingWork, source);
    };
    // Keep fixture construction/initialization out of measured-frame work sums while
    // preserving peak resource/capacity observations from initial & cold setup.
    updatePeaksFrom(resourceCullingWork, initializationWork);
    updatePeaksFrom(resourceCullingWork, coldFirstStepWork);
    initialCullingStateCreations = initializationWork.renderCullingStateCreations;
    initialCullingRecords = initializationWork.renderCullingRecordsAdded;
#endif
    struct FixturePoseSample
    {
        size_t slot = 0;
        uint32_t generation = 0;
        float x = 0.0f;
        float y = 0.0f;
        float rotation = 0.0f;
        std::int32_t zOrder = 0;
    };
    std::vector<FixturePoseSample> previousMeasuredPoses;
    std::vector<std::int32_t> previousMeasuredZ;
    std::vector<std::int32_t> currentMeasuredZ;
    if (options.validateResources)
    {
        previousMeasuredPoses.reserve(options.population);
        previousMeasuredZ.reserve(options.population);
        currentMeasuredZ.reserve(options.population);
    }
    if (window != nullptr)
    {
        (void)window->consumeBenchmarkOrderTiming();
        (void)window->consumeBenchmarkSelectionTiming();
    }
    const auto fallbacksBeforeMeasured = window != nullptr
                                             ? window->getObservedRenderFallbackCounts()
                                             : SDLWindow::RenderFallbackCounts{};
    for (size_t frame = 0; frame < options.measureFrames; ++frame)
    {
        if (options.validateRendering)
        {
#if PLATFORMATOR_ENABLE_WORK_PROBES
            const size_t trajectoryStep = stepIndex;
            platformator_test::work = {};
            if (options.headless)
            {
                step();
                const auto work = platformator_test::work;
                if (work.renderViewPreparations != 0 || work.renderEligibilityChecks != 0 ||
                    work.renderVisibilityChecks != 0 || work.renderSubmittedSprites != 0)
                    throw std::runtime_error("Headless validation performed rendering work");
                *framesFile << nlohmann::json{{"frame", frame}, {"image_fnv64", nullptr},
                                             {"submissions", nlohmann::json::array()},
                                             {"oracle_visible", nlohmann::json::array()},
                                             {"headless_work", {{"views", work.renderViewPreparations},
                                                                  {"eligibility_checks", work.renderEligibilityChecks},
                                                                  {"visibility_checks", work.renderVisibilityChecks},
                                                                  {"submissions", work.renderSubmittedSprites}}}}.dump()
                             << '\n';
            }
            else
            {
                readback.error.clear();
                readback.hash.clear();
                platformator_test::ScopedRenderObservation scope(observation);
                step();
                if (!readback.error.empty() || readback.callbacks != frame + 1)
                    throw std::runtime_error("Readback failed or did not run once per measured frame: " + readback.error);
                nlohmann::json currentVisible = nlohmann::json::array();
                std::set<std::pair<size_t, uint32_t>> visible;
                for (const auto identity : *observation.submissions)
                {
                    currentVisible.push_back({identity.slot, identity.generation});
                    visible.emplace(identity.slot, identity.generation);
                }
                const auto oracle = independentVisibleOracle(options, trajectory, fixture, trajectoryStep,
                                                              options.profile == FixtureTrajectoryProfile::Rotated ? 640.0f : 320.0f,
                                                              options.profile == FixtureTrajectoryProfile::Rotated ? 160.0f : 240.0f,
                                                              trajectory.outputDimensions(trajectoryStep).first,
                                                              trajectory.outputDimensions(trajectoryStep).second);
                nlohmann::json oracleMembership = nlohmann::json::array();
                std::set<std::pair<size_t, uint32_t>> oracleVisible;
                for (const auto identity : oracle)
                {
                    oracleMembership.push_back({identity.slot, identity.generation});
                    oracleVisible.emplace(identity.slot, identity.generation);
                }
                for (const auto &identity : oracleVisible)
                    if (!previousOracleVisible.contains(identity)) ++crossingEntries;
                for (const auto &identity : previousOracleVisible)
                    if (!oracleVisible.contains(identity)) ++crossingExits;
                previousOracleVisible = std::move(oracleVisible);
                if (mode == "cached-linear")
                    for (const auto &identity : previousOracleVisible)
                        if (!visible.contains(identity))
                            throw std::runtime_error("Linear mode omitted a sprite retained by the independent oracle");
                if (visible.size() != observation.submissions->size() || observation.unresolvedSprites != 0 ||
                    observation.submissionOverflows != 0)
                    throw std::runtime_error("Validation trace contains a duplicate, unresolved sprite, or overflow");
                if (options.denseZEdit &&
                    (*observation.submissions != oracle || observation.submissions->size() != options.population))
                    throw std::runtime_error("Dense Z validation frame did not preserve exact Z-ordered retention");
                if (!options.denseZEdit && !options.rejectedZEdit &&
                    options.profile != FixtureTrajectoryProfile::Churn &&
                    options.profile != FixtureTrajectoryProfile::SceneDefault &&
                    options.profile != FixtureTrajectoryProfile::SceneStacking)
                {
                    verifyAllZeroSubmissionOrder(options.population, *observation.submissions, oracle);
                }
                *framesFile << nlohmann::json{{"frame", frame}, {"image_fnv64", readback.hash},
                                             {"output_width", readback.width}, {"output_height", readback.height},
                                             {"submissions", std::move(currentVisible)},
                                             {"oracle_visible", std::move(oracleMembership)},
                                             {"unresolved", observation.unresolvedSprites},
                                             {"overflow", observation.submissionOverflows}}.dump() << '\n';
                if (frame == 0 && !options.captureFrameIndex)
                    writePpm(options.evidenceOutput + ".representative.ppm", readback);
                if (options.captureFrameIndex && frame == *options.captureFrameIndex)
                    writePpm(options.evidenceOutput + ".mismatch.ppm", readback);
            }
#endif
        }
        else if (options.validateResources)
        {
#if PLATFORMATOR_ENABLE_WORK_PROBES
            const size_t trajectoryStep = stepIndex;
            previousMeasuredPoses.clear();
            previousMeasuredZ.clear();
            for (const auto &record : fixture.records())
            {
                const auto position = record.object->getPosition();
                const std::int32_t zOrder = record.sprite->getZOrder();
                previousMeasuredPoses.push_back({record.identity.slot, record.identity.generation,
                                                 position.x(), position.y(), record.object->getRotation(), zOrder});
                previousMeasuredZ.push_back(zOrder);
            }
            platformator_test::work = {};
            linear_render_allocation_probe::Snapshot allocationSnapshot;
            {
                linear_render_allocation_probe::ScopedObservation allocationObservation(allocationSnapshot);
                step();
            }
            size_t changedPoses = 0;
            size_t changedZOrders = 0;
            size_t changedSprites = 0;
            currentMeasuredZ.clear();
            std::vector<std::array<float, 3>> beforeCrossingPoses;
            std::vector<std::array<float, 3>> afterCrossingPoses;
            if (options.profile == FixtureTrajectoryProfile::Crossing)
            {
                beforeCrossingPoses.reserve(fixture.records().size());
                afterCrossingPoses.reserve(fixture.records().size());
            }
            for (size_t index = 0; index < fixture.records().size(); ++index)
            {
                const auto &record = fixture.records()[index];
                const auto &previous = previousMeasuredPoses[index];
                const auto position = record.object->getPosition();
                const float rotation = record.object->getRotation();
                const std::int32_t zOrder = record.sprite->getZOrder();
                currentMeasuredZ.push_back(zOrder);
                if (options.profile == FixtureTrajectoryProfile::Crossing)
                {
                    beforeCrossingPoses.push_back({previous.x, previous.y, previous.rotation});
                    afterCrossingPoses.push_back({position.x(), position.y(), rotation});
                }
                const bool poseChanged = record.identity.slot != previous.slot ||
                                         record.identity.generation != previous.generation ||
                                         position.x() != previous.x || position.y() != previous.y ||
                                         rotation != previous.rotation;
                const bool zChanged = zOrder != previous.zOrder;
                if (poseChanged)
                    ++changedPoses;
                if (zChanged)
                    ++changedZOrders;
                if (poseChanged || zChanged)
                    ++changedSprites;
            }
            if (options.denseZEdit)
                verifyDenseZEditStep(frame, previousMeasuredZ, currentMeasuredZ, changedPoses);
            if (options.rejectedZEdit)
                verifyRejectedZEditStep(options, trajectoryStep, previousMeasuredZ, currentMeasuredZ, changedPoses);
            if (options.profile == FixtureTrajectoryProfile::Crossing)
                verifyCrossingPoseStep(options, trajectory, trajectoryStep,
                                       beforeCrossingPoses, afterCrossingPoses, changedZOrders);
            actualChangedPoseTotal += changedPoses;
            actualChangedZTotal += changedZOrders;
            actualChangedSpriteTotal += changedSprites;
            maximumActualChangedSpritesPerStep = std::max(maximumActualChangedSpritesPerStep, changedSprites);
            resourceAllocationCalls += allocationSnapshot.calls;
            resourceAllocatedBytes += allocationSnapshot.bytes;
            resourceViews += platformator_test::work.renderViewPreparations;
            resourceEligibilityChecks += platformator_test::work.renderEligibilityChecks;
            resourceVisibilityChecks += platformator_test::work.renderVisibilityChecks;
            resourceSubmissions += platformator_test::work.renderSubmittedSprites;
            accumulateCullingWork(platformator_test::work);
            if (options.denseZEdit)
            {
                if ((frame % 2) == 0)
                {
                    ++cleanPhaseFrames;
                    accumulateWorkInto(cleanPhaseWork, platformator_test::work);
                }
                else
                {
                    ++editPhaseFrames;
                    accumulateWorkInto(editPhaseWork, platformator_test::work);
                }
            }
#else
            step();
#endif
        }
        else
        {
            const auto before = std::chrono::steady_clock::now();
            step();
            const auto after = std::chrono::steady_clock::now();
            const auto orderTiming = window != nullptr
                                         ? window->consumeBenchmarkOrderTiming()
                                         : SDLWindow::BenchmarkOrderTiming{};
            const auto selectionTiming = window != nullptr
                                             ? window->consumeBenchmarkSelectionTiming()
                                             : SDLWindow::BenchmarkOrderTiming{};
            pairedStepRenderNanoseconds.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count()));
            pairedOrderPrepareNanoseconds.push_back(orderTiming.totalNs);
            pairedVisibleSelectionNanoseconds.push_back(selectionTiming.totalNs);
        }
    }
    if (framesFile) framesFile->close();
    if (!pairedStepRenderNanoseconds.empty())
    {
        auto pairedPath = std::filesystem::path(options.csvOutput);
        pairedPath.replace_extension(".paired.csv");
        std::ofstream paired(pairedPath, std::ios::binary);
        paired.exceptions(std::ios::badbit | std::ios::failbit);
        paired << "step,paired_step_render_ns,draw_order_prepare_ns,visible_selection_ns\n";
        for (size_t index = 0; index < pairedStepRenderNanoseconds.size(); ++index)
            paired << index << ',' << pairedStepRenderNanoseconds[index] << ','
                   << pairedOrderPrepareNanoseconds[index] << ','
                   << pairedVisibleSelectionNanoseconds[index] << '\n';
    }
    if (options.validateRendering)
    {
        const std::string framesPath = options.evidenceOutput + ".frames.jsonl";
        const std::string representativePath = options.evidenceOutput + ".representative.ppm";
        const std::string imagePath = options.captureFrameIndex
                                          ? options.evidenceOutput + ".mismatch.ppm"
                                          : representativePath;
        const nlohmann::json representativeSha = options.headless ? nlohmann::json(nullptr)
                                                                  : nlohmann::json(sha256File(imagePath));
        const auto proof = nlohmann::json{{"profile", options.profileName}, {"sprite_count", options.population},
                                         {"workers", options.workers}, {"seed", options.seed}, {"dt", options.dt},
                                         {"warmup_frames", options.warmupFrames}, {"measure_frames", options.measureFrames},
                                         {"mode", mode}, {"ordering_variant", options.orderingVariant},
                                         {"fixture_sha256", PLATFORMATOR_LINEAR_FIXTURE_SHA256},
                                         {"harness_sha256", PLATFORMATOR_LINEAR_HARNESS_SHA256},
                                         {"source_sha256", PLATFORMATOR_BENCH_SOURCE_SHA256},
                                         {"trajectory_parameters", resolvedParameters},
                                         {"renderer_mapping", metadata["renderer_mapping"]},
                                         {"frames_sha256", sha256File(framesPath)},
                                         {"representative_sha256", representativeSha},
                                         {"crossing_counts", {{"entries", crossingEntries}, {"exits", crossingExits}}},
                                         {"headless", options.headless},
                                         {"timing_eligible", false},
                                         {"validation_mode", "per-step"}};
        std::ofstream evidence(options.evidenceOutput + ".evidence.json", std::ios::binary);
        evidence.exceptions(std::ios::badbit | std::ios::failbit);
        evidence << proof.dump(2) << '\n';
    }
    long long resourceRssAfter = -1;
    long long resourceRssAfterReset = -1;
    uint64_t resetNanoseconds = 0;
    uint64_t resetAllocationCalls = 0;
    uint64_t resetAllocatedBytes = 0;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    if (options.validateResources) resourceRssAfter = residentMemoryKiB();
    if (options.validateResources)
    {
        const size_t startingPopulation = fixture.records().size();
        const size_t startingRecordCapacity = platformator_test::work.renderCullingOwnedRecordCapacity;
        if (!options.headless && mode == "cached-linear" &&
            startingRecordCapacity >= startingPopulation && startingRecordCapacity < std::numeric_limits<size_t>::max())
        {
            const size_t requestedPopulation = startingRecordCapacity + 1;
            fixture.reserveSprites(requestedPopulation);
            platformator_test::work = {};
            linear_render_allocation_probe::Snapshot growthAllocation;
            const auto growthStart = std::chrono::steady_clock::now();
            {
                linear_render_allocation_probe::ScopedObservation observation(growthAllocation);
                for (size_t slot = startingPopulation; slot < requestedPopulation; ++slot)
                    fixture.addSprite(slot, 0, 10000.0f + static_cast<float>(slot), 10000.0f, 16.0f, 16.0f,
                                      FixtureTexture::BlackSquare);
            }
            const auto growthEnd = std::chrono::steady_clock::now();
            capacityGrowthObservation = {
                {"attempted", true}, {"records_before", startingPopulation},
                {"record_capacity_before", startingRecordCapacity},
                {"records_added", requestedPopulation - startingPopulation},
                {"elapsed_ns", static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                      growthEnd - growthStart).count())},
                {"allocation_calls", growthAllocation.calls}, {"allocated_bytes", growthAllocation.bytes},
                {"record_capacity_after", platformator_test::work.renderCullingOwnedRecordCapacity},
                {"owned_bytes_after", platformator_test::work.renderCullingOwnedBytes},
                {"observer", "C++ operator new/new[] during Sprite registration; aligned new, C, SDL and driver allocations are not attributed"}};
        }
        else
        {
            capacityGrowthObservation = {{"attempted", false},
                                         {"reason", "inactive mode, headless mode, or no reserved capacity boundary"},
                                         {"record_capacity_before", startingRecordCapacity}};
        }

        platformator_test::work = {};
        linear_render_allocation_probe::Snapshot resetAllocation;
        const auto resetStart = std::chrono::steady_clock::now();
        {
            linear_render_allocation_probe::ScopedObservation observation(resetAllocation);
            fixture.clearSceneAndCreateCamera(cameraWidth, cameraHeight);
        }
        const auto resetEnd = std::chrono::steady_clock::now();
        resetNanoseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(resetEnd - resetStart).count());
        resetAllocationCalls = resetAllocation.calls;
        resetAllocatedBytes = resetAllocation.bytes;
        const auto &resetWork = platformator_test::work;
        resourceRssAfterReset = residentMemoryKiB();
        cullingResetObservation = {
            {"records_removed", resetWork.renderCullingRecordsRemoved},
            {"records_registered_after_clear", fixture.records().size()},
            {"owned_bytes_after_clear", resetWork.renderCullingOwnedBytes},
            {"record_capacity_after_clear", resetWork.renderCullingOwnedRecordCapacity},
            {"elapsed_ns", resetNanoseconds}, {"allocation_calls", resetAllocationCalls},
            {"allocated_bytes", resetAllocatedBytes},
            {"observer", "C++ operator new/new[] during scene clear and camera recreation; deallocation, aligned new, C, SDL and driver allocations are not attributed"}};
    }
    resourceCullingWorkJson = {
        {"records_added", resourceCullingWork.renderCullingRecordsAdded},
        {"records_removed", resourceCullingWork.renderCullingRecordsRemoved},
        {"records_relocated", resourceCullingWork.renderCullingRecordsRelocated},
        {"dirty_enqueues", resourceCullingWork.renderCullingDirtyEnqueues},
        {"dirty_cancellations", resourceCullingWork.renderCullingDirtyCancellations},
        {"bounds_refreshes", resourceCullingWork.renderCullingBoundsRefreshes},
        {"candidate_visits", resourceCullingWork.renderCullingCandidateVisits},
        {"full_population_visits", resourceCullingWork.renderCullingFullPopulationVisits},
        {"cache_hits", resourceCullingWork.renderCullingCacheHits},
        {"projection_rebuilds", resourceCullingWork.renderCullingProjectionRebuilds},
        {"numeric_fallbacks", resourceCullingWork.renderCullingNumericFallbacks},
        {"mapping_fallbacks", resourceCullingWork.renderCullingMappingFallbacks},
        {"peak_record_capacity", resourceCullingWork.renderCullingOwnedRecordCapacity},
        {"peak_owned_bytes", resourceCullingWork.renderCullingOwnedBytes},
        {"peak_common_owned_bytes", resourceCullingWork.renderCullingOwnedBytes},
        {"visible_membership_checks", resourceCullingWork.visibleMembershipChecks},
        {"visible_membership_changes", resourceCullingWork.visibleMembershipChanges},
        {"sprite_order_preparations", resourceCullingWork.spriteOrderPreparations},
        {"sprite_order_entries", resourceCullingWork.spriteOrderEntries},
        {"sprite_order_lookup_visits", resourceCullingWork.spriteOrderLookupVisits},
        {"sprite_order_comparisons", resourceCullingWork.spriteOrderComparisons},
        {"sprite_order_sorts", resourceCullingWork.spriteOrderSorts},
        {"sprite_order_repairs", resourceCullingWork.spriteOrderRepairs},
        {"sprite_order_moves", resourceCullingWork.spriteOrderMoves},
        {"peak_order_records", resourceCullingWork.spriteOrderOwnedRecords},
        {"peak_order_entry_capacity", resourceCullingWork.spriteOrderOwnedEntryCapacity},
        {"peak_order_owned_bytes", resourceCullingWork.spriteOrderOwnedBytes}};
    if (options.denseZEdit)
    {
        resourceCullingWorkJson["clean_phase"] = {
            {"frames", cleanPhaseFrames},
            {"order_preparations", cleanPhaseWork.spriteOrderPreparations},
            {"order_entries", cleanPhaseWork.spriteOrderEntries},
            {"order_lookup_visits", cleanPhaseWork.spriteOrderLookupVisits},
            {"order_comparisons", cleanPhaseWork.spriteOrderComparisons},
            {"order_sorts", cleanPhaseWork.spriteOrderSorts},
            {"order_repairs", cleanPhaseWork.spriteOrderRepairs},
            {"order_moves", cleanPhaseWork.spriteOrderMoves},
            {"cache_hits", cleanPhaseWork.renderCullingCacheHits}};
        resourceCullingWorkJson["edit_phase"] = {
            {"frames", editPhaseFrames},
            {"order_preparations", editPhaseWork.spriteOrderPreparations},
            {"order_entries", editPhaseWork.spriteOrderEntries},
            {"order_lookup_visits", editPhaseWork.spriteOrderLookupVisits},
            {"order_comparisons", editPhaseWork.spriteOrderComparisons},
            {"order_sorts", editPhaseWork.spriteOrderSorts},
            {"order_repairs", editPhaseWork.spriteOrderRepairs},
            {"order_moves", editPhaseWork.spriteOrderMoves},
            {"bounds_refreshes", editPhaseWork.renderCullingBoundsRefreshes}};
    }
    if (options.validateResources && options.rejectedZEdit && options.population > 64)
    {
        if (actualChangedZTotal == 0 || resourceCullingWork.spriteOrderPreparations != 0 ||
            resourceCullingWork.spriteOrderSorts != 0 || resourceCullingWork.renderCullingBoundsRefreshes != 0 ||
            resourceCullingWork.renderCullingFullPopulationVisits != 0 ||
            resourceCullingWork.renderCullingCandidateVisits != 0)
            throw std::runtime_error("rejected-z-edit resource validation observed unexpected ordering/culling work or zero Z edits");
    }
#endif
    if (options.validateResources)
    {
        const auto resourceProof = nlohmann::json{
            {"profile", options.profileName}, {"sprite_count", options.population}, {"workers", options.workers},
            {"seed", options.seed}, {"dt", options.dt}, {"warmup_frames", options.warmupFrames},
            {"measure_frames", options.measureFrames}, {"mode", mode},
            {"ordering_variant", options.orderingVariant},
            {"fixture_sha256", PLATFORMATOR_LINEAR_FIXTURE_SHA256},
            {"harness_sha256", PLATFORMATOR_LINEAR_HARNESS_SHA256},
            {"source_sha256", PLATFORMATOR_BENCH_SOURCE_SHA256},
            {"trajectory_parameters", resolvedParameters},
            {"renderer_mapping", metadata["renderer_mapping"]},
            {"headless", options.headless},
            {"validation_mode", "per-step"}, {"timing_eligible", false},
            {"allocation_probe", "thread-safe process-wide C++ operator new/new[] call and requested-byte counts during each measured step, including calls on oneTBB workers; aligned new, C allocations, SDL, and driver allocations are not attributed"},
            {"allocation_calls", resourceAllocationCalls}, {"allocated_bytes", resourceAllocatedBytes},
            {"allocation_calls_per_frame", static_cast<double>(resourceAllocationCalls) / options.measureFrames},
            {"allocated_bytes_per_frame", static_cast<double>(resourceAllocatedBytes) / options.measureFrames},
            {"process_rss_before_kib", resourceRssBefore}, {"process_rss_after_kib", resourceRssAfter},
            {"process_rss_after_reset_kib", resourceRssAfterReset},
            {"actual_changed_sprite_total", actualChangedSpriteTotal},
            {"actual_changed_pose_total", actualChangedPoseTotal},
            {"actual_changed_z_total", actualChangedZTotal},
            {"actual_changed_sprites_per_step_max", maximumActualChangedSpritesPerStep},
            {"actual_changed_fraction", options.population == 0 ? 0.0 :
                static_cast<double>(actualChangedSpriteTotal) /
                (static_cast<double>(options.population) * options.measureFrames)},
            {"cold_observation", {{"first_warmup_step_elapsed_ns", coldFirstStepRenderNanoseconds},
                                   {"first_warmup_step_scope", "one complete simulation step plus rendering when active"},
                                   {"fixture_construction_and_registration_ns", fixtureConstructionNanoseconds},
                                   {"fixture_construction_allocation_calls", fixtureConstructionAllocationCalls},
                                   {"fixture_construction_allocated_bytes", fixtureConstructionAllocatedBytes},
                                   {"fixture_construction_allocation_observer", "C++ operator new/new[] from Runtime construction through Sprite registration; aligned new, C, SDL and driver allocations are not attributed"},
                                   {"initial_registered_records", initialCullingRecords},
                                   {"initial_state_creations", initialCullingStateCreations},
                                   {"initial_record_capacity", initializationWork.renderCullingOwnedRecordCapacity},
                                   {"initial_owned_bytes", initializationWork.renderCullingOwnedBytes},
                                   {"initial_order_owned_bytes", initializationWork.spriteOrderOwnedBytes},
                                   {"first_warmup_step_allocation_calls", coldFirstStepAllocationCalls},
                                   {"first_warmup_step_allocated_bytes", coldFirstStepAllocatedBytes},
                                   {"dirty_enqueues", coldFirstStepWork.renderCullingDirtyEnqueues},
                                   {"bounds_refreshes", coldFirstStepWork.renderCullingBoundsRefreshes},
                                   {"owned_bytes", coldFirstStepWork.renderCullingOwnedBytes},
                                   {"order_owned_bytes", coldFirstStepWork.spriteOrderOwnedBytes}}},
            {"capacity_growth_observation", capacityGrowthObservation},
            {"reset_observation", cullingResetObservation},
            {"render_view_preparations", resourceViews}, {"render_eligibility_checks", resourceEligibilityChecks},
            {"render_visibility_checks", resourceVisibilityChecks}, {"render_submissions", resourceSubmissions},
            {"culling_initial", {{"state_creations", initialCullingStateCreations},
                                  {"records_registered", initialCullingRecords}}},
            {"culling_work", resourceCullingWorkJson}};
        const std::string resourcePath = options.evidenceOutput + ".resources.json";
        std::ofstream evidence(resourcePath, std::ios::binary);
        evidence.exceptions(std::ios::badbit | std::ios::failbit);
        evidence << resourceProof.dump(2) << '\n';
        recorder.updateMetadata({{"resource_observation", {{"path", std::filesystem::absolute(resourcePath).string()},
                                                              {"sha256", sha256File(resourcePath)}}}});
    }
    const auto fallbacksAfterMeasured = window != nullptr
                                            ? window->getObservedRenderFallbackCounts()
                                            : SDLWindow::RenderFallbackCounts{};
    const SDLWindow::RenderFallbackCounts observedFallbacks{
        fallbacksAfterMeasured.mappingFallbacks - fallbacksBeforeMeasured.mappingFallbacks,
        fallbacksAfterMeasured.numericFallbacks - fallbacksBeforeMeasured.numericFallbacks};
    recorder.updateMetadata({{"linear_render_culling", buildCullingMetadata(observedFallbacks)},
                             {"stopped_on_quit", false},
                             {"physical_invariants", {{"finite", true},
                                                      {"populations_valid", true},
                                                      {"supports_valid", true}}}});
    recorder.writeOutputs(options.csvOutput);
    recorder.printSummary();
}

#if PLATFORMATOR_ENABLE_WORK_PROBES
void runAllocationObserverSelfTest()
{
    constexpr size_t workerCount = 4;
    constexpr size_t allocationsPerWorker = 64;
    constexpr size_t allocationSize = 29;
    std::atomic<size_t> ready{0};
    std::atomic<bool> start{false};
    std::atomic<uint64_t> observedValues{0};
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker)
    {
        workers.emplace_back([&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (size_t allocation = 0; allocation < allocationsPerWorker; ++allocation)
            {
                auto *storage = new unsigned char[allocationSize];
                storage[0] = static_cast<unsigned char>(allocation);
                observedValues.fetch_add(static_cast<uint64_t>(storage[0]) + 1, std::memory_order_relaxed);
                delete[] storage;
            }
        });
    }
    while (ready.load(std::memory_order_acquire) != workerCount)
        std::this_thread::yield();

    linear_render_allocation_probe::Snapshot concurrentSnapshot;
    {
        linear_render_allocation_probe::ScopedObservation observation(concurrentSnapshot);
        start.store(true, std::memory_order_release);
        for (auto &worker : workers)
            worker.join();
    }
    const uint64_t expectedCalls = workerCount * allocationsPerWorker;
    const uint64_t expectedBytes = expectedCalls * allocationSize;
    if (concurrentSnapshot.calls < expectedCalls || concurrentSnapshot.bytes < expectedBytes ||
        observedValues.load(std::memory_order_relaxed) == 0 || linear_render_allocation_probe::isActive())
        throw std::runtime_error("Concurrent allocation observation lost counts or remained enabled");

    linear_render_allocation_probe::Snapshot exceptionalSnapshot;
    try
    {
        linear_render_allocation_probe::ScopedObservation observation(exceptionalSnapshot);
        auto *storage = new unsigned char[53];
        delete[] storage;
        throw 19;
    }
    catch (int value)
    {
        if (value != 19)
            throw std::runtime_error("Allocation observation changed the exception value");
    }
    if (exceptionalSnapshot.calls == 0 || exceptionalSnapshot.bytes < 53 ||
        linear_render_allocation_probe::isActive())
        throw std::runtime_error("Exceptional exit did not restore allocation observation state");
}

void runZEditOracleSelfTest()
{
    Options opts;
    opts.denseZEdit = true;
    opts.population = 8;
    opts.warmupFrames = 2;
    const auto zStep2 = computeDeterministicZOrders(opts, 2);
    const auto zStep3 = computeDeterministicZOrders(opts, 3);
    // Warmup frame 0 modified slot 17%8=1 from -111 to +200; step 2 is measured clean frame (0 edits);
    // step 3 is measured edit frame (frame=1) modifying slot (3*131+17)%8 = 2 from -126 to +203.
    if (zStep2[2] == zStep3[2] || zStep3[2] != 203)
        throw std::runtime_error("Deterministic Z schedule mismatch");

    // Verify clean frame accepts unchanged Z and rejects mutated Z.
    verifyDenseZEditStep(0, zStep2, zStep2, 0);
    auto expectReject = [](auto &&fn, const char *message) {
        bool threw = false;
        try
        {
            fn();
        }
        catch (const std::runtime_error &)
        {
            threw = true;
        }
        if (!threw)
            throw std::runtime_error(message);
    };
    expectReject([&] { verifyDenseZEditStep(0, zStep2, zStep3, 0); },
                 "Clean frame must reject unexpected Z mutation");
    expectReject([&] { verifyDenseZEditStep(1, zStep2, zStep2, 0); },
                 "Edit frame must reject omitted Z mutation");
    expectReject([&] { verifyDenseZEditStep(1, zStep2, zStep3, 1); },
                 "Edit frame must reject unexpected pose mutation");

    // Ineffective Z edit (already lowest Z=-1000 changing to -1001 crosses zero peers) must be rejected.
    std::vector<std::int32_t> beforeLow = zStep2;
    std::vector<std::int32_t> afterLow = zStep2;
    beforeLow[0] = -1000;
    afterLow[0] = -1001;
    expectReject([&] { verifyDenseZEditStep(1, beforeLow, afterLow, 0); },
                 "Edit frame must reject non-peer-crossing ineffective Z mutation");

    // Rejected-Z-edit oracle checks: valid step, omitted rejected edit, and misdirected edit.
    Options rejOpts;
    rejOpts.rejectedZEdit = true;
    rejOpts.population = 80;
    rejOpts.warmupFrames = 2;
    const auto rejBefore = computeDeterministicZOrders(rejOpts, 1);
    const auto rejAfter = computeDeterministicZOrders(rejOpts, 2);
    verifyRejectedZEditStep(rejOpts, 2, rejBefore, rejAfter, 0);

    std::vector<std::int32_t> rejOmitted = rejAfter;
    const size_t firstScheduledSlot = 64 + ((2 * 100) % 16);
    rejOmitted[firstScheduledSlot] = rejBefore[firstScheduledSlot];
    expectReject([&] { verifyRejectedZEditStep(rejOpts, 2, rejBefore, rejOmitted, 0); },
                 "Rejected-z-edit oracle must reject an omitted rejected edit");

    std::vector<std::int32_t> rejMisdirectedVisible = rejAfter;
    rejMisdirectedVisible[0] = -999;
    expectReject([&] { verifyRejectedZEditStep(rejOpts, 2, rejBefore, rejMisdirectedVisible, 0); },
                 "Rejected-z-edit oracle must reject an edit hitting a visible slot");

    std::vector<std::int32_t> rejMisdirectedPeer = rejAfter;
    rejMisdirectedPeer[firstScheduledSlot] = rejBefore[firstScheduledSlot];
    rejMisdirectedPeer[64] += 1;
    expectReject([&] { verifyRejectedZEditStep(rejOpts, 2, rejBefore, rejMisdirectedPeer, 0); },
                 "Rejected-z-edit oracle must reject a misdirected rejected-slot edit");

    // Crossing schedule oracle checks: moving crossing passes, stationary crossing rejects.
    Options crossOpts;
    crossOpts.profile = FixtureTrajectoryProfile::Crossing;
    crossOpts.population = 8;
    crossOpts.warmupFrames = 2;
    const DeterministicFixtureTrajectory crossTraj(FixtureTrajectoryProfile::Crossing, crossOpts.population, 0, 100, false);
    std::vector<std::array<float, 3>> crossBefore(crossOpts.population);
    std::vector<std::array<float, 3>> crossAfter(crossOpts.population);
    for (size_t slot = 0; slot < crossOpts.population; ++slot)
    {
        const auto p1 = crossTraj.pose(slot, 1);
        const auto p2 = crossTraj.pose(slot, 2);
        crossBefore[slot] = {p1.x, p1.y, p1.rotationRadians};
        crossAfter[slot] = {p2.x, p2.y, p2.rotationRadians};
    }
    verifyCrossingPoseStep(crossOpts, crossTraj, 2, crossBefore, crossAfter, 0);
    expectReject([&] { verifyCrossingPoseStep(crossOpts, crossTraj, 2, crossBefore, crossBefore, 0); },
                 "Crossing oracle must reject a stationary crossing control");

    // All-zero registration-order oracle checks: conservative superset passes, inversions & missing oracle fail.
    const std::vector<SpriteIdentity> oracleSubset{{1, 0}, {3, 0}};
    const std::vector<SpriteIdentity> validSuperset{{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    verifyAllZeroSubmissionOrder(4, validSuperset, oracleSubset);
    expectReject([&] { verifyAllZeroSubmissionOrder(4, {{3, 0}, {2, 0}, {1, 0}, {0, 0}}, oracleSubset); },
                 "All-zero oracle must reject a reversed submission sequence");
    expectReject([&] { verifyAllZeroSubmissionOrder(4, {{0, 0}, {2, 0}, {1, 0}, {3, 0}}, oracleSubset); },
                 "All-zero oracle must reject an adjacent inversion");
    expectReject([&] { verifyAllZeroSubmissionOrder(4, {{0, 0}, {2, 0}, {3, 0}}, oracleSubset); },
                 "All-zero oracle must reject an omitted visibility oracle member");
}
#endif
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--print-build-identity")
        {
            std::cout << nlohmann::json{{"source_sha256", PLATFORMATOR_BENCH_SOURCE_SHA256},
                                        {"fixture_sha256", PLATFORMATOR_LINEAR_FIXTURE_SHA256},
                                        {"harness_sha256", PLATFORMATOR_LINEAR_HARNESS_SHA256},
                                        {"configuration", PLATFORMATOR_BENCH_CONFIGURATION},
                                        {"sanitizer", PLATFORMATOR_BENCH_SANITIZER},
                                        {"work_probes", static_cast<bool>(PLATFORMATOR_ENABLE_WORK_PROBES)},
                                        {"debug_tools", static_cast<bool>(PLATFORMATOR_ENABLE_DEBUG_TOOLS)}}
                             .dump()
                      << '\n';
            return 0;
        }
#if PLATFORMATOR_ENABLE_WORK_PROBES
        if (argc == 2 && std::string_view(argv[1]) == "--allocation-observer-self-test")
        {
            runAllocationObserverSelfTest();
            std::cout << "[PASS] concurrent and exceptional allocation observer checks\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--z-edit-oracle-self-test")
        {
            runZEditOracleSelfTest();
            std::cout << "[PASS] deterministic dense-z-edit oracle and ineffective mutation checks\n";
            return 0;
        }
#endif
        run(parseOptions(argc, argv));
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
