#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <SDL3/SDL_surface.h>

namespace platformator_editor
{
inline constexpr std::uint64_t ImageMaxEncodedBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t ImageMaxDimension = 8192;
inline constexpr std::uint64_t ImageMaxPixels = 16'777'216ULL;
inline constexpr std::uint64_t ImageMaxDecodedBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t ImageMaxWorkingSetBytes = 128ULL * 1024ULL * 1024ULL;

enum class ImagePreflightStatus
{
    Admitted,
    Missing,
    Empty,
    EncodedLimit,
    Unsupported,
    InvalidHeader,
    DimensionLimit,
    WorkingSetLimit,
    ChangedDuringRead,
    DecodeFailed
};

struct ImagePreflight
{
    ImagePreflightStatus status = ImagePreflightStatus::InvalidHeader;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t encodedBytes = 0;
    std::uint64_t decodedBytes = 0;
    std::uint64_t workingSetBytes = 0;
    std::string format;
    std::string message;

    bool admitted() const noexcept { return status == ImagePreflightStatus::Admitted; }
};

using SDLImageSurface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

struct ImagePixelBuffer
{
    std::uint8_t *data = nullptr;
    void (*release)(void *) = nullptr;

    ImagePixelBuffer() = default;
    ImagePixelBuffer(std::uint8_t *pixels, void (*deleter)(void *)) noexcept
        : data(pixels), release(deleter) {}
    ~ImagePixelBuffer() { reset(); }
    ImagePixelBuffer(const ImagePixelBuffer &) = delete;
    ImagePixelBuffer &operator=(const ImagePixelBuffer &) = delete;
    ImagePixelBuffer(ImagePixelBuffer &&other) noexcept
        : data(std::exchange(other.data, nullptr)), release(std::exchange(other.release, nullptr)) {}
    ImagePixelBuffer &operator=(ImagePixelBuffer &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            data = std::exchange(other.data, nullptr);
            release = std::exchange(other.release, nullptr);
        }
        return *this;
    }
    void reset() noexcept
    {
        if (data != nullptr && release != nullptr)
            release(data);
        data = nullptr;
        release = nullptr;
    }
};

struct PreparedImage
{
    ImagePreflight preflight;
    // Keeps the per-decode allocator budget alive until the borrowed pixel
    // buffer is released. Members are ordered for surface → pixels → budget.
    std::shared_ptr<void> decoderLifetime;
    ImagePixelBuffer pixels;
    SDLImageSurface surface{nullptr, SDL_DestroySurface};

    bool ready() const noexcept { return surface != nullptr; }
};

// Reads only a small, format-specific header. Unsupported or unbounded formats
// are deliberately rejected before any decoder allocates a surface.
ImagePreflight inspect_image(const std::filesystem::path &path);

// Rechecks the staged bytes against the admitted identity before decoding from
// memory, so file replacement between inspection and decode cannot bypass caps.
PreparedImage prepare_image(const std::filesystem::path &path);
} // namespace platformator_editor
