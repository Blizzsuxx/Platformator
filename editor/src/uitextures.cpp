#include "uitextures.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace platformator_editor
{
namespace
{
// Count a conservative eight-byte readback surface along with the four-byte
// transfer target, staging buffer, and four-byte destination texture.
constexpr std::size_t PeakBytesPerPixel = 24;

class RendererState
{
  public:
    explicit RendererState(SDL_Renderer *renderer) : renderer_(renderer)
    {
        if (renderer_ == nullptr)
            return;
        target_ = SDL_GetRenderTarget(renderer_);
        viewportSet_ = SDL_RenderViewportSet(renderer_);
        valid_ = SDL_GetRenderViewport(renderer_, &viewport_);
        valid_ = SDL_GetRenderLogicalPresentation(renderer_, &logicalWidth_, &logicalHeight_,
                                                  &logicalMode_) && valid_;
        valid_ = SDL_GetRenderScale(renderer_, &scaleX_, &scaleY_) && valid_;
        valid_ = SDL_GetRenderClipRect(renderer_, &clip_) && valid_;
        valid_ = SDL_GetRenderDrawColorFloat(renderer_, &drawR_, &drawG_, &drawB_, &drawA_) && valid_;
        valid_ = SDL_GetRenderDrawBlendMode(renderer_, &drawBlend_) && valid_;
        valid_ = SDL_GetRenderColorScale(renderer_, &colorScale_) && valid_;
        clipEnabled_ = SDL_RenderClipEnabled(renderer_);
    }

    ~RendererState() { restore(); }

    bool valid() const noexcept { return valid_; }

    bool restore() noexcept
    {
        if (restored_ || renderer_ == nullptr)
            return valid_;
        restored_ = true;
        bool ok = SDL_SetRenderTarget(renderer_, target_);
        ok = SDL_SetRenderLogicalPresentation(renderer_, logicalWidth_, logicalHeight_, logicalMode_) && ok;
        ok = SDL_SetRenderScale(renderer_, scaleX_, scaleY_) && ok;
        ok = SDL_SetRenderViewport(renderer_, viewportSet_ ? &viewport_ : nullptr) && ok;
        ok = SDL_SetRenderClipRect(renderer_, clipEnabled_ ? &clip_ : nullptr) && ok;
        ok = SDL_SetRenderColorScale(renderer_, colorScale_) && ok;
        ok = SDL_SetRenderDrawColorFloat(renderer_, drawR_, drawG_, drawB_, drawA_) && ok;
        ok = SDL_SetRenderDrawBlendMode(renderer_, drawBlend_) && ok;
        valid_ = valid_ && ok;
        return valid_;
    }

  private:
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *target_ = nullptr;
    SDL_Rect viewport_{};
    SDL_Rect clip_{};
    int logicalWidth_ = 0;
    int logicalHeight_ = 0;
    SDL_RendererLogicalPresentation logicalMode_ = SDL_LOGICAL_PRESENTATION_DISABLED;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    float drawR_ = 0.0f;
    float drawG_ = 0.0f;
    float drawB_ = 0.0f;
    float drawA_ = 1.0f;
    float colorScale_ = 1.0f;
    SDL_BlendMode drawBlend_ = SDL_BLENDMODE_NONE;
    bool viewportSet_ = false;
    bool clipEnabled_ = false;
    bool valid_ = false;
    bool restored_ = false;
};

bool fit_dimensions(int requestedWidth, int requestedHeight, std::size_t maxPixels,
                    int &width, int &height) noexcept
{
    if (requestedWidth <= 0 || requestedHeight <= 0 || maxPixels == 0)
        return false;
    const double requestedPixels = static_cast<double>(requestedWidth) * requestedHeight;
    const double scale = std::min(1.0, std::sqrt(static_cast<double>(maxPixels) / requestedPixels));
    width = std::max(1, static_cast<int>(std::floor(requestedWidth * scale)));
    height = std::max(1, static_cast<int>(std::floor(requestedHeight * scale)));
    while (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > maxPixels)
    {
        if (width >= height && width > 1)
            --width;
        else if (height > 1)
            --height;
        else
            return false;
    }
    return true;
}
} // namespace

SceneTextureReplicas::~SceneTextureReplicas()
{
    release_all();
}

SDL_Texture *SceneTextureReplicas::texture_for(SDL_Renderer *sourceRenderer,
                                               SDL_Texture *sourceTexture,
                                               std::uint64_t sourceRevision,
                                               SDL_Renderer *destinationRenderer,
                                               int desiredWidth, int desiredHeight) noexcept
{
    if (sourceRenderer == nullptr || sourceTexture == nullptr || destinationRenderer == nullptr ||
        sourceRenderer == destinationRenderer || desiredWidth <= 0 || desiredHeight <= 0)
        return nullptr;

    if ((sourceRenderer_ != nullptr && sourceRenderer_ != sourceRenderer) ||
        (destinationRenderer_ != nullptr && destinationRenderer_ != destinationRenderer))
        release_all();

    float sourceWidth = 0.0f;
    float sourceHeight = 0.0f;
    if (!SDL_GetTextureSize(sourceTexture, &sourceWidth, &sourceHeight) ||
        sourceWidth <= 0.0f || sourceHeight <= 0.0f)
        return replica_;

    sourceRenderer_ = sourceRenderer;
    destinationRenderer_ = destinationRenderer;
    const auto maxPixels = MaxTransferBytes / PeakBytesPerPixel;
    if (transferTarget_ == nullptr)
    {
        const int requestedWidth = std::max(1, std::min(desiredWidth,
            static_cast<int>(std::ceil(sourceWidth))));
        const int requestedHeight = std::max(1, std::min(desiredHeight,
            static_cast<int>(std::ceil(sourceHeight))));
        if (!fit_dimensions(requestedWidth, requestedHeight, maxPixels, width_, height_))
            return nullptr;

        const std::size_t pixels = static_cast<std::size_t>(width_) * height_;
        if (pixels > std::numeric_limits<std::size_t>::max() / 4)
            return nullptr;
        try
        {
            staging_.resize(pixels * 4);
        }
        catch (...)
        {
            width_ = height_ = 0;
            return nullptr;
        }
        transferTarget_ = SDL_CreateTexture(sourceRenderer_, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_TARGET, width_, height_);
        if (transferTarget_ == nullptr)
        {
            staging_.clear();
            width_ = height_ = 0;
            return nullptr;
        }
        SDL_SetTextureBlendMode(transferTarget_, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(transferTarget_, SDL_SCALEMODE_LINEAR);
        replica_ = SDL_CreateTexture(destinationRenderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING, width_, height_);
        if (replica_ == nullptr)
        {
            clear_transfer();
            return nullptr;
        }
        SDL_SetTextureBlendMode(replica_, SDL_BLENDMODE_NONE);
    }

    if (sourceRevision_ != sourceRevision || sourceTexture != lastSourceTexture_)
    {
        RendererState state(sourceRenderer_);
        if (!state.valid())
            return replica_;
        bool prepared = SDL_SetRenderTarget(sourceRenderer_, transferTarget_);
        prepared = SDL_SetRenderLogicalPresentation(sourceRenderer_, 0, 0,
            SDL_LOGICAL_PRESENTATION_DISABLED) && prepared;
        prepared = SDL_SetRenderViewport(sourceRenderer_, nullptr) && prepared;
        prepared = SDL_SetRenderClipRect(sourceRenderer_, nullptr) && prepared;
        prepared = SDL_SetRenderScale(sourceRenderer_, 1.0f, 1.0f) && prepared;
        prepared = SDL_SetRenderColorScale(sourceRenderer_, 1.0f) && prepared;
        if (!prepared || !SDL_RenderTexture(sourceRenderer_, sourceTexture, nullptr, nullptr))
        {
            state.restore();
            return replica_;
        }
        SDL_Surface *readback = SDL_RenderReadPixels(sourceRenderer_, nullptr);
        const bool converted = readback != nullptr && readback->w == width_ && readback->h == height_ &&
            SDL_ConvertPixels(width_, height_, readback->format, readback->pixels, readback->pitch,
                              SDL_PIXELFORMAT_RGBA32, staging_.data(), width_ * 4);
        if (readback != nullptr)
            SDL_DestroySurface(readback);
        const bool restored = state.restore();
        if (!converted || !restored ||
            !SDL_UpdateTexture(replica_, nullptr, staging_.data(), width_ * 4))
            return replica_;
        lastSourceTexture_ = sourceTexture;
        sourceRevision_ = sourceRevision;
    }
    return replica_;
}

void SceneTextureReplicas::release_renderer(SDL_Renderer *renderer) noexcept
{
    if (renderer == nullptr)
        return;
    if (renderer == sourceRenderer_ || renderer == destinationRenderer_)
        release_all();
}

void SceneTextureReplicas::release_all() noexcept
{
    clear_replica();
    clear_transfer();
    sourceRenderer_ = nullptr;
    destinationRenderer_ = nullptr;
    lastSourceTexture_ = nullptr;
    sourceRevision_ = 0;
}

std::size_t SceneTextureReplicas::allocated_bytes() const noexcept
{
    return staging_.size() + (width_ > 0 && height_ > 0
        ? static_cast<std::size_t>(width_) * height_ * 8 : 0);
}

void SceneTextureReplicas::clear_transfer() noexcept
{
    if (transferTarget_ != nullptr)
    {
        SDL_DestroyTexture(transferTarget_);
        transferTarget_ = nullptr;
    }
    staging_.clear();
    std::vector<std::byte>().swap(staging_);
    width_ = height_ = 0;
}

void SceneTextureReplicas::clear_replica() noexcept
{
    if (replica_ != nullptr)
    {
        SDL_DestroyTexture(replica_);
        replica_ = nullptr;
    }
}
} // namespace platformator_editor
