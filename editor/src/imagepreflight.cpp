#include "imagepreflight.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_pixels.h>

namespace bounded_stbi
{
struct Budget
{
    explicit Budget(std::size_t maximumBytes) : maximum(maximumBytes) {}

    bool reserve(std::size_t bytes) noexcept
    {
        auto currentBytes = current.load(std::memory_order_relaxed);
        for (;;)
        {
            if (bytes > maximum || currentBytes > maximum - bytes)
            {
                allocationDenied.store(true, std::memory_order_relaxed);
                return false;
            }
            if (current.compare_exchange_weak(currentBytes, currentBytes + bytes,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed))
            {
                auto observedPeak = peak.load(std::memory_order_relaxed);
                while (observedPeak < currentBytes + bytes &&
                       !peak.compare_exchange_weak(observedPeak, currentBytes + bytes,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed))
                {
                }
                return true;
            }
        }
    }

    void release(std::size_t bytes) noexcept
    {
        current.fetch_sub(bytes, std::memory_order_acq_rel);
    }

    const std::size_t maximum;
    std::atomic<std::size_t> current{0};
    std::atomic<std::size_t> peak{0};
    std::atomic<bool> allocationDenied{false};
};

struct alignas(std::max_align_t) Allocation
{
    std::shared_ptr<Budget> budget;
    std::size_t chargedBytes = 0;
    std::size_t payloadBytes = 0;
};

thread_local std::shared_ptr<Budget> activeBudget;

void *allocate(std::size_t payloadBytes) noexcept
{
    if (!activeBudget || payloadBytes > std::numeric_limits<std::size_t>::max() - sizeof(Allocation))
        return nullptr;
    const std::size_t chargedBytes = sizeof(Allocation) + payloadBytes;
    if (!activeBudget->reserve(chargedBytes))
        return nullptr;
    void *storage = std::malloc(chargedBytes);
    if (storage == nullptr)
    {
        activeBudget->release(chargedBytes);
        return nullptr;
    }
    auto *allocation = new (storage) Allocation{activeBudget, chargedBytes, payloadBytes};
    return allocation + 1;
}

void release(void *pointer) noexcept
{
    if (pointer == nullptr)
        return;
    auto *allocation = static_cast<Allocation *>(pointer) - 1;
    auto budget = allocation->budget;
    const std::size_t chargedBytes = allocation->chargedBytes;
    allocation->~Allocation();
    std::free(allocation);
    budget->release(chargedBytes);
}

void *reallocate(void *pointer, std::size_t oldBytes, std::size_t newBytes) noexcept
{
    if (pointer == nullptr)
        return allocate(newBytes);
    if (newBytes == 0)
    {
        release(pointer);
        return nullptr;
    }
    auto *oldAllocation = static_cast<Allocation *>(pointer) - 1;
    const std::size_t copyBytes = std::min({oldBytes, oldAllocation->payloadBytes, newBytes});
    void *replacement = allocate(newBytes);
    if (replacement == nullptr)
        return nullptr;
    std::memcpy(replacement, pointer, copyBytes);
    release(pointer);
    return replacement;
}

class BudgetScope
{
public:
    explicit BudgetScope(std::shared_ptr<Budget> budget) : previous(std::move(activeBudget))
    {
        activeBudget = std::move(budget);
    }
    ~BudgetScope() { activeBudget = std::move(previous); }

private:
    std::shared_ptr<Budget> previous;
};

struct ByteCursor
{
    const std::uint8_t *data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
};

int read_bytes(void *user, char *destination, int requestedBytes)
{
    auto &cursor = *static_cast<ByteCursor *>(user);
    if (requestedBytes <= 0 || cursor.offset >= cursor.size)
        return 0;
    const auto count = std::min<std::size_t>(static_cast<std::size_t>(requestedBytes),
                                             cursor.size - cursor.offset);
    std::memcpy(destination, cursor.data + cursor.offset, count);
    cursor.offset += count;
    return static_cast<int>(count);
}

void skip_bytes(void *user, int bytes)
{
    auto &cursor = *static_cast<ByteCursor *>(user);
    if (bytes >= 0)
    {
        cursor.offset += std::min<std::size_t>(static_cast<std::size_t>(bytes),
                                               cursor.size - cursor.offset);
    }
    else
    {
        const auto rewind = static_cast<std::size_t>(-(static_cast<std::int64_t>(bytes)));
        cursor.offset -= std::min(cursor.offset, rewind);
    }
}

int end_of_bytes(void *user)
{
    const auto &cursor = *static_cast<ByteCursor *>(user);
    return cursor.offset >= cursor.size;
}
} // namespace bounded_stbi

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define PLATFORMATOR_BOUNDED_DECODER
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_MALLOC(size) bounded_stbi::allocate(size)
#define STBI_REALLOC_SIZED(pointer, oldSize, newSize) \
    bounded_stbi::reallocate(pointer, oldSize, newSize)
#define STBI_FREE(pointer) bounded_stbi::release(pointer)
// SDL_image's private fork removes this legacy diagnostic variable while its
// GIF helper still references it. This private stb_image translation unit
// supplies the required local state; failures are surfaced through SDL's
// thread-local error channel.
static const char *stbi__g_failure_reason = "";
#include "../../vendored/SDL3_image-3.4.8/src/stb_image.h"

namespace platformator_editor
{
namespace
{
struct Header
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const char *format = nullptr;
    bool recognized = false;
    bool valid = false;
    std::string message;
};

std::uint16_t read_be16(const std::uint8_t *bytes)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

std::uint32_t read_be32(const std::uint8_t *bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

std::uint32_t read_le32(const std::uint8_t *bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

Header parse_header(std::span<const std::uint8_t> bytes)
{
    static constexpr std::array<std::uint8_t, 8> pngSignature{
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (bytes.size() >= 24 && std::equal(pngSignature.begin(), pngSignature.end(), bytes.begin()))
    {
        Header result;
        result.recognized = true;
        result.format = "PNG";
        if (read_be32(bytes.data() + 8) != 13 || std::memcmp(bytes.data() + 12, "IHDR", 4) != 0)
        {
            result.message = "PNG header does not begin with a valid IHDR chunk.";
            return result;
        }
        result.width = read_be32(bytes.data() + 16);
        result.height = read_be32(bytes.data() + 20);
        result.valid = result.width != 0 && result.height != 0;
        if (!result.valid)
            result.message = "PNG dimensions must be positive.";
        return result;
    }

    if (bytes.size() >= 10 &&
        (std::memcmp(bytes.data(), "GIF87a", 6) == 0 || std::memcmp(bytes.data(), "GIF89a", 6) == 0))
    {
        Header result;
        result.recognized = true;
        result.format = "GIF";
        result.width = static_cast<std::uint32_t>(bytes[6] | (bytes[7] << 8));
        result.height = static_cast<std::uint32_t>(bytes[8] | (bytes[9] << 8));
        result.valid = result.width != 0 && result.height != 0;
        if (!result.valid)
            result.message = "GIF dimensions must be positive.";
        return result;
    }

    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
    {
        Header result;
        result.recognized = true;
        result.format = "BMP";
        if (bytes.size() < 54)
        {
            result.message = "BMP BITMAPINFOHEADER is truncated.";
            return result;
        }
        const std::uint32_t dibSize = read_le32(bytes.data() + 14);
        if (dibSize < 40)
        {
            result.message = "Only bounded BITMAPINFOHEADER images are supported for preview.";
            return result;
        }
        const auto width = static_cast<std::int32_t>(read_le32(bytes.data() + 18));
        const auto signedHeight = static_cast<std::int32_t>(read_le32(bytes.data() + 22));
        if (width <= 0 || signedHeight == 0 || signedHeight == std::numeric_limits<std::int32_t>::min())
        {
            result.message = "BMP dimensions are invalid.";
            return result;
        }
        result.width = static_cast<std::uint32_t>(width);
        result.height = static_cast<std::uint32_t>(signedHeight < 0 ? -signedHeight : signedHeight);
        result.valid = true;
        return result;
    }

    // JPEG metadata scanning is bounded to 64 KiB. This avoids unbounded
    // metadata walks while allowing common APP/EXIF headers before the frame.
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xd8)
    {
        Header result;
        result.recognized = true;
        result.format = "JPEG";
        std::size_t offset = 2;
        while (offset + 4 <= bytes.size())
        {
            if (bytes[offset++] != 0xff)
            {
                result.message = "JPEG marker sequence is malformed.";
                return result;
            }
            while (offset < bytes.size() && bytes[offset] == 0xff)
                ++offset;
            if (offset >= bytes.size())
                break;
            const std::uint8_t marker = bytes[offset++];
            if (marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7) || marker == 0x01)
                continue;
            if (offset + 2 > bytes.size())
                break;
            const std::uint16_t segmentLength = read_be16(bytes.data() + offset);
            if (segmentLength < 2)
            {
                result.message = "JPEG segment length is invalid.";
                return result;
            }
            if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2 || marker == 0xc3 ||
                marker == 0xc5 || marker == 0xc6 || marker == 0xc7 || marker == 0xc9 ||
                marker == 0xca || marker == 0xcb || marker == 0xcd || marker == 0xce || marker == 0xcf)
            {
                if (segmentLength < 7 || offset + segmentLength > bytes.size())
                    break;
                result.height = read_be16(bytes.data() + offset + 3);
                result.width = read_be16(bytes.data() + offset + 5);
                result.valid = result.width != 0 && result.height != 0;
                if (!result.valid)
                    result.message = "JPEG dimensions must be positive.";
                return result;
            }
            if (offset + segmentLength > bytes.size())
                break;
            offset += segmentLength;
        }
        result.message = "JPEG dimensions were not found in the bounded 64 KiB header scan.";
        return result;
    }

    Header result;
    result.message = "Unsupported or unbounded image format; an explanatory icon will be shown.";
    return result;
}

ImagePreflight make_admission(const Header &header, std::uint64_t encodedBytes)
{
    ImagePreflight result;
    result.encodedBytes = encodedBytes;
    result.format = header.format == nullptr ? std::string{} : header.format;
    result.width = header.width;
    result.height = header.height;
    if (!header.recognized)
    {
        result.status = ImagePreflightStatus::Unsupported;
        result.message = header.message;
        return result;
    }
    if (!header.valid)
    {
        result.status = ImagePreflightStatus::InvalidHeader;
        result.message = header.message.empty() ? "Image header is invalid." : header.message;
        return result;
    }
    if (header.width > ImageMaxDimension || header.height > ImageMaxDimension ||
        static_cast<std::uint64_t>(header.width) * header.height > ImageMaxPixels)
    {
        result.status = ImagePreflightStatus::DimensionLimit;
        result.message = "Image exceeds the 8,192-pixel dimension or 16,777,216-pixel preview limit.";
        return result;
    }
    result.decodedBytes = static_cast<std::uint64_t>(header.width) * header.height * 4ULL;
    if (result.decodedBytes > ImageMaxDecodedBytes ||
        encodedBytes > ImageMaxWorkingSetBytes - result.decodedBytes)
    {
        result.status = ImagePreflightStatus::WorkingSetLimit;
        result.message = "Image exceeds the 128 MiB decode working-set limit.";
        return result;
    }
    result.workingSetBytes = encodedBytes + result.decodedBytes;
    result.status = ImagePreflightStatus::Admitted;
    return result;
}

ImagePreflight inspect_buffer(std::span<const std::uint8_t> bytes)
{
    if (bytes.empty())
    {
        ImagePreflight result;
        result.status = ImagePreflightStatus::Empty;
        result.message = "Image file is empty.";
        return result;
    }
    if (bytes.size() > ImageMaxEncodedBytes)
    {
        ImagePreflight result;
        result.status = ImagePreflightStatus::EncodedLimit;
        result.encodedBytes = bytes.size();
        result.message = "Image exceeds the 64 MiB encoded input limit.";
        return result;
    }
    constexpr std::size_t MaxHeaderScan = 64U * 1024U;
    return make_admission(parse_header(bytes.first(std::min(bytes.size(), MaxHeaderScan))),
                          bytes.size());
}
} // namespace

ImagePreflight inspect_image(const std::filesystem::path &path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        ImagePreflight result;
        result.status = ImagePreflightStatus::Missing;
        result.message = "Image file is unavailable.";
        return result;
    }
    if (size == 0)
    {
        ImagePreflight result;
        result.status = ImagePreflightStatus::Empty;
        result.message = "Image file is empty.";
        return result;
    }
    if (size > ImageMaxEncodedBytes)
    {
        ImagePreflight result;
        result.status = ImagePreflightStatus::EncodedLimit;
        result.encodedBytes = size;
        result.message = "Image exceeds the 64 MiB encoded input limit.";
        return result;
    }

    constexpr std::size_t JpegHeaderScanBytes = 64U * 1024U;
    const auto bytesToRead = static_cast<std::size_t>(std::min<std::uint64_t>(
        size, std::max<std::uint64_t>(24U, JpegHeaderScanBytes)));
    std::array<std::uint8_t, JpegHeaderScanBytes> header{};
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char *>(header.data()),
                              static_cast<std::streamsize>(bytesToRead)))
    {
        ImagePreflight result;
        result.status = ImagePreflightStatus::Missing;
        result.encodedBytes = size;
        result.message = "Could not read image metadata.";
        return result;
    }
    const auto parsed = parse_header(std::span<const std::uint8_t>(header.data(), bytesToRead));
    return make_admission(parsed, size);
}

PreparedImage prepare_image(const std::filesystem::path &path)
{
    PreparedImage result;
    result.preflight = inspect_image(path);
    if (!result.preflight.admitted())
        return result;

    try
    {
        std::vector<std::uint8_t> encoded(static_cast<std::size_t>(result.preflight.encodedBytes));
        std::ifstream input(path, std::ios::binary);
        if (!input || !input.read(reinterpret_cast<char *>(encoded.data()),
                                  static_cast<std::streamsize>(encoded.size())) ||
            input.peek() != std::char_traits<char>::eof())
        {
            result.preflight.status = ImagePreflightStatus::ChangedDuringRead;
            result.preflight.message = "Image changed or became unreadable during preparation.";
            return result;
        }
        const auto stagedAdmission = inspect_buffer(encoded);
        if (!stagedAdmission.admitted() || stagedAdmission.width != result.preflight.width ||
            stagedAdmission.height != result.preflight.height || stagedAdmission.format != result.preflight.format)
        {
            result.preflight.status = ImagePreflightStatus::ChangedDuringRead;
            result.preflight.message = "Image header changed after metadata admission.";
            return result;
        }

        const auto decoderLimit = static_cast<std::size_t>(
            ImageMaxWorkingSetBytes - result.preflight.encodedBytes);
        auto decoderBudget = std::make_shared<bounded_stbi::Budget>(decoderLimit);
        result.decoderLifetime = decoderBudget;
        int decodedWidth = 0;
        int decodedHeight = 0;
        stbi_uc *decodedPixels = nullptr;
        {
            SDL_ClearError();
            bounded_stbi::BudgetScope budgetScope(decoderBudget);
            bounded_stbi::ByteCursor cursor{encoded.data(), encoded.size(), 0};
            const stbi_io_callbacks callbacks{
                bounded_stbi::read_bytes, bounded_stbi::skip_bytes, bounded_stbi::end_of_bytes};
            decodedPixels = stbi_load_from_callbacks(&callbacks, &cursor, &decodedWidth,
                                                    &decodedHeight, nullptr, 4);
        }
        if (decodedPixels == nullptr)
        {
            const char *decoderError = SDL_GetError();
            if (decoderBudget->allocationDenied.load(std::memory_order_relaxed))
            {
                result.preflight.status = ImagePreflightStatus::WorkingSetLimit;
                result.preflight.message = "Image decoder allocation exceeded the inclusive 128 MiB working-set limit.";
            }
            else
            {
                result.preflight.status = ImagePreflightStatus::DecodeFailed;
                result.preflight.message = std::string("Bounded image decode failed: ") +
                                          (decoderError == nullptr || *decoderError == '\0' ?
                                               "invalid image data" : decoderError);
            }
            return result;
        }
        result.pixels = ImagePixelBuffer(decodedPixels, stbi_image_free);
        if (decodedWidth != static_cast<int>(result.preflight.width) ||
            decodedHeight != static_cast<int>(result.preflight.height) || decodedWidth <= 0 || decodedHeight <= 0)
        {
            result.pixels.reset();
            result.preflight.status = ImagePreflightStatus::DecodeFailed;
            result.preflight.message = "Decoded image dimensions did not match admitted metadata.";
            return result;
        }
        const auto decodedBytes = static_cast<std::uint64_t>(decodedWidth) *
                                  static_cast<std::uint64_t>(decodedHeight) * 4ULL;
        const auto actualWorkingSet = result.preflight.encodedBytes +
            static_cast<std::uint64_t>(decoderBudget->peak.load(std::memory_order_relaxed));
        if (decodedBytes > ImageMaxDecodedBytes || actualWorkingSet > ImageMaxWorkingSetBytes ||
            decodedWidth > std::numeric_limits<int>::max() / 4)
        {
            result.pixels.reset();
            result.preflight.status = ImagePreflightStatus::WorkingSetLimit;
            result.preflight.message = "Decoded image exceeds the 128 MiB working-set limit.";
            return result;
        }
        result.surface.reset(SDL_CreateSurfaceFrom(decodedWidth, decodedHeight,
            SDL_PIXELFORMAT_RGBA32, decodedPixels, decodedWidth * 4));
        if (!result.surface)
        {
            result.pixels.reset();
            result.preflight.status = ImagePreflightStatus::DecodeFailed;
            result.preflight.message = std::string("Could not wrap bounded image pixels: ") + SDL_GetError();
            return result;
        }
        result.preflight.decodedBytes = decodedBytes;
        result.preflight.workingSetBytes = actualWorkingSet;
    }
    catch (const std::bad_alloc &)
    {
        result.preflight.status = ImagePreflightStatus::DecodeFailed;
        result.preflight.message = "Image staging allocation failed within the configured limit.";
    }
    catch (const std::exception &error)
    {
        result.preflight.status = ImagePreflightStatus::DecodeFailed;
        result.preflight.message = error.what();
    }
    return result;
}
} // namespace platformator_editor
