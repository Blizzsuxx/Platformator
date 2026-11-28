#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include <SDL3/SDL.h>

#include "platformator/objecthandle.h"

namespace platformator_detail
{
// One frame's camera/output mapping. This type contains values only; it never
// retains references to Camera, GameObject, Sprite, or SDL state.
struct SpriteRenderView
{
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float scaleX = 0.0f;
    float scaleY = 0.0f;
    int outputWidth = 0;
    int outputHeight = 0;
    bool cullingEligible = false;
};

// Shared view projection for the engine camera and the editor preview. Width
// and height already include the sprite GameObject's local scale, matching
// Sprite::getWidth/getHeight; only parent translation is reflected in worldX/Y.
inline SDL_FRect makeSpriteRenderDestination(float worldX, float worldY, float width, float height,
                                             const SpriteRenderView &view) noexcept
{
    return {(worldX - width * 0.5f - view.cameraX) * view.scaleX,
            (worldY - height * 0.5f - view.cameraY) * view.scaleY,
            width * view.scaleX, height * view.scaleY};
}

inline bool isColorModulated(SDL_Color color) noexcept
{
    return color.r != 255 || color.g != 255 || color.b != 255 || color.a != 255;
}

inline bool shouldPromoteSpriteAlphaBlend(SDL_Color color, SDL_BlendMode blendMode) noexcept
{
    return color.a < 255 && blendMode == SDL_BLENDMODE_NONE;
}

inline bool isSafeSpriteRenderDestination(const SDL_FRect &destination) noexcept
{
    // The SDL software backend converts coordinates to integers and uses fixed-point
    // vertices. Leave room for rotated corners and differences between vertices.
    constexpr float limit = static_cast<float>(std::numeric_limits<int>::max() / 8);
    return std::abs(destination.x) <= limit && std::abs(destination.y) <= limit &&
           destination.w > 0.0f && destination.w <= limit && destination.h > 0.0f && destination.h <= limit;
}

inline bool submitSpriteRender(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_FRect *source, const SDL_FRect &destination,
                               float angleDegrees, SDL_FlipMode flip,
                               SDL_Color color = SDL_Color{255, 255, 255, 255})
{
    if (!isSafeSpriteRenderDestination(destination) || !std::isfinite(angleDegrees))
        return false;

    const bool modulated = texture != nullptr && isColorModulated(color);
    Uint8 previousR = 255;
    Uint8 previousG = 255;
    Uint8 previousB = 255;
    Uint8 previousA = 255;
    SDL_BlendMode previousBlend = SDL_BLENDMODE_BLEND;
    bool restoreBlend = false;
    if (modulated)
    {
        SDL_GetTextureColorMod(texture, &previousR, &previousG, &previousB);
        SDL_GetTextureAlphaMod(texture, &previousA);
        SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(texture, color.a);
        if (color.a < 255 && SDL_GetTextureBlendMode(texture, &previousBlend) &&
            shouldPromoteSpriteAlphaBlend(color, previousBlend))
        {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            restoreBlend = true;
        }
    }
    const bool ok = SDL_RenderTextureRotated(renderer, texture, source, &destination,
                                             angleDegrees, nullptr, flip);
    if (modulated)
    {
        SDL_SetTextureColorMod(texture, previousR, previousG, previousB);
        SDL_SetTextureAlphaMod(texture, previousA);
        if (restoreBlend)
            SDL_SetTextureBlendMode(texture, previousBlend);
    }
    return ok;
}

struct SpriteRenderBounds
{
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
};

inline bool spriteBoundsOverlap(const SpriteRenderBounds &a, const SpriteRenderBounds &b) noexcept
{
    return a.maxX >= b.minX && a.minX <= b.maxX && a.maxY >= b.minY && a.minY <= b.maxY;
}

struct SpriteRenderProjectionKey
{
    float scaleX = 0.0f;
    float scaleY = 0.0f;
    int outputWidth = 0;
    int outputHeight = 0;
    bool valid = false;
};

struct SpriteRenderViewKey
{
    platformator::ObjectHandle cameraHandle{};
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    SpriteRenderProjectionKey projection{};
    bool valid = false;
};

enum class SpriteRenderClassification
{
    Unclassifiable,
    Retain,
    Reject
};

namespace sprite_render_geometry_detail
{
inline bool finitePositive(float value)
{
    return std::isfinite(value) && value > 0.0f;
}

inline constexpr double certifiedCoordinateLimit = 16384.0;

inline bool finiteNormalPositive(float value)
{
    return std::isfinite(value) && value >= std::numeric_limits<float>::min();
}

inline bool certifiedAxis(float scale)
{
    if (!finiteNormalPositive(scale))
        return false;
    const double maximumPreScaleMagnitude = 4.0 * certifiedCoordinateLimit / static_cast<double>(scale);
    return std::isfinite(maximumPreScaleMagnitude) &&
           maximumPreScaleMagnitude <= static_cast<double>(std::numeric_limits<float>::max());
}

inline bool projectedWithinCertificate(float value, float scale)
{
    return std::isfinite(value) &&
           std::abs(static_cast<double>(value) * static_cast<double>(scale)) <= certifiedCoordinateLimit;
}

inline double absoluteMaximum(double current, float value)
{
    return std::max(current, std::abs(static_cast<double>(value)));
}

// Match SDL_RenderTextureRotated's degree-to-radian float conversion and float sin/cos.
inline bool computeRotationSinCos(float angleDegrees, float &sine, float &cosine) noexcept
{
    const float radians = static_cast<float>((SDL_PI_D * static_cast<double>(angleDegrees)) / 180.0);
    if (!std::isfinite(radians))
        return false;
    sine = SDL_sinf(radians);
    cosine = SDL_cosf(radians);
    return std::isfinite(sine) && std::isfinite(cosine);
}

inline bool makePaddedBounds(const SDL_FRect &destination, float angleDegrees, SpriteRenderBounds &bounds)
{
    const float right = destination.x + destination.w;
    const float bottom = destination.y + destination.h;
    if (!std::isfinite(right) || !std::isfinite(bottom))
        return false;

    float minimumX = destination.x;
    float minimumY = destination.y;
    float maximumX = right;
    float maximumY = bottom;
    double maximumMagnitude = 0.0;
    maximumMagnitude = absoluteMaximum(maximumMagnitude, destination.x);
    maximumMagnitude = absoluteMaximum(maximumMagnitude, destination.y);
    maximumMagnitude = absoluteMaximum(maximumMagnitude, destination.w);
    maximumMagnitude = absoluteMaximum(maximumMagnitude, destination.h);
    maximumMagnitude = absoluteMaximum(maximumMagnitude, right);
    maximumMagnitude = absoluteMaximum(maximumMagnitude, bottom);

    if (angleDegrees != 0.0f)
    {
        float sine = 0.0f;
        float cosine = 1.0f;
        const float centerX = destination.x + destination.w * 0.5f;
        const float centerY = destination.y + destination.h * 0.5f;
        if (!computeRotationSinCos(angleDegrees, sine, cosine) || !std::isfinite(centerX) || !std::isfinite(centerY))
            return false;

        const float cornerX[4] = {destination.x, right, destination.x, right};
        const float cornerY[4] = {destination.y, destination.y, bottom, bottom};
        minimumX = std::numeric_limits<float>::infinity();
        minimumY = std::numeric_limits<float>::infinity();
        maximumX = -std::numeric_limits<float>::infinity();
        maximumY = -std::numeric_limits<float>::infinity();
        maximumMagnitude = std::max(maximumMagnitude, std::abs(static_cast<double>(centerX)));
        maximumMagnitude = std::max(maximumMagnitude, std::abs(static_cast<double>(centerY)));

        for (int index = 0; index < 4; ++index)
        {
            const float offsetX = cornerX[index] - centerX;
            const float offsetY = cornerY[index] - centerY;
            const float cosineX = cosine * offsetX;
            const float sineY = sine * offsetY;
            const float sineX = sine * offsetX;
            const float cosineY = cosine * offsetY;
            const float transformedX = (cosineX - sineY) + centerX;
            const float transformedY = (sineX + cosineY) + centerY;
            if (!std::isfinite(offsetX) || !std::isfinite(offsetY) || !std::isfinite(cosineX) ||
                !std::isfinite(sineY) || !std::isfinite(sineX) || !std::isfinite(cosineY) ||
                !std::isfinite(transformedX) || !std::isfinite(transformedY))
                return false;

            minimumX = std::min(minimumX, transformedX);
            minimumY = std::min(minimumY, transformedY);
            maximumX = std::max(maximumX, transformedX);
            maximumY = std::max(maximumY, transformedY);
            maximumMagnitude = absoluteMaximum(maximumMagnitude, transformedX);
            maximumMagnitude = absoluteMaximum(maximumMagnitude, transformedY);
        }
    }

    // The one-pixel raster guard also covers accumulated float error in SDL's
    // corner calculations. Outside this range, float spacing can consume the
    // guard, so retain the sprite and preserve SDL's existing handling.
    const double roundingAllowance = maximumMagnitude * std::numeric_limits<float>::epsilon() * 8.0;
    if (!std::isfinite(roundingAllowance) || roundingAllowance >= 1.0)
        return false;
    const double guard = 1.0 + roundingAllowance;
    const double negativeInfinity = -std::numeric_limits<double>::infinity();
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    bounds.minX = std::nextafter(static_cast<double>(minimumX), negativeInfinity) - guard;
    bounds.minY = std::nextafter(static_cast<double>(minimumY), negativeInfinity) - guard;
    bounds.maxX = std::nextafter(static_cast<double>(maximumX), positiveInfinity) + guard;
    bounds.maxY = std::nextafter(static_cast<double>(maximumY), positiveInfinity) + guard;
    return std::isfinite(bounds.minX) && std::isfinite(bounds.minY) && std::isfinite(bounds.maxX) &&
           std::isfinite(bounds.maxY);
}
} // namespace sprite_render_geometry_detail

inline SpriteRenderProjectionKey makeSpriteRenderProjectionKey(const SpriteRenderView &view) noexcept
{
    if (!view.cullingEligible || view.outputWidth <= 0 || view.outputHeight <= 0 ||
        static_cast<double>(view.outputWidth) > sprite_render_geometry_detail::certifiedCoordinateLimit ||
        static_cast<double>(view.outputHeight) > sprite_render_geometry_detail::certifiedCoordinateLimit ||
        !sprite_render_geometry_detail::certifiedAxis(view.scaleX) ||
        !sprite_render_geometry_detail::certifiedAxis(view.scaleY))
        return {};
    return {view.scaleX, view.scaleY, view.outputWidth, view.outputHeight, true};
}

inline SpriteRenderViewKey makeSpriteRenderViewKey(platformator::ObjectHandle cameraHandle,
                                                    const SpriteRenderView &view) noexcept
{
    const auto projection = makeSpriteRenderProjectionKey(view);
    if (!cameraHandle || !projection.valid || !std::isfinite(view.cameraX) || !std::isfinite(view.cameraY))
        return {};
    return {cameraHandle, view.cameraX, view.cameraY, projection, true};
}

inline bool sameSpriteRenderProjection(const SpriteRenderProjectionKey &a,
                                       const SpriteRenderProjectionKey &b) noexcept
{
    return a.valid && b.valid && a.scaleX == b.scaleX && a.scaleY == b.scaleY &&
           a.outputWidth == b.outputWidth && a.outputHeight == b.outputHeight;
}

inline bool sameSpriteRenderView(const SpriteRenderViewKey &a, const SpriteRenderViewKey &b) noexcept
{
    return a.valid && b.valid && a.cameraHandle == b.cameraHandle && a.cameraX == b.cameraX &&
           a.cameraY == b.cameraY && sameSpriteRenderProjection(a.projection, b.projection);
}

// The two-pixel envelope covers the current classifier's one-pixel raster
// padding plus a conservative float-operation error bound for the certified P
// domain. The derivation is retained in evidence/numeric-bounds.md.
inline bool makeCertifiedSpriteRenderProxy(float worldX, float worldY, float width, float height,
                                            float angleDegrees, float scaleX, float scaleY,
                                            SpriteRenderBounds &bounds) noexcept
{
    using namespace sprite_render_geometry_detail;
    if (!certifiedAxis(scaleX) || !certifiedAxis(scaleY) || !std::isfinite(worldX) || !std::isfinite(worldY) ||
        !finitePositive(width) || !finitePositive(height) || !std::isfinite(angleDegrees) ||
        !projectedWithinCertificate(worldX, scaleX) || !projectedWithinCertificate(worldY, scaleY) ||
        !projectedWithinCertificate(width, scaleX) || !projectedWithinCertificate(height, scaleY))
        return false;

    const float screenWidth = width * scaleX;
    const float screenHeight = height * scaleY;
    if (!finiteNormalPositive(screenWidth) || !finiteNormalPositive(screenHeight))
        return false;

    float sine = 0.0f;
    float cosine = 1.0f;
    if (angleDegrees != 0.0f)
    {
        if (!computeRotationSinCos(angleDegrees, sine, cosine) || std::abs(sine) > 1.0f || std::abs(cosine) > 1.0f)
            return false;
    }

    const double projectedHalfX =
        (std::abs(static_cast<double>(cosine)) * static_cast<double>(width) * scaleX +
         std::abs(static_cast<double>(sine)) * static_cast<double>(height) * scaleY) *
        0.5;
    const double projectedHalfY =
        (std::abs(static_cast<double>(sine)) * static_cast<double>(width) * scaleX +
         std::abs(static_cast<double>(cosine)) * static_cast<double>(height) * scaleY) *
        0.5;
    const double envelopeX = 2.0 / static_cast<double>(scaleX);
    const double envelopeY = 2.0 / static_cast<double>(scaleY);
    const double halfX = projectedHalfX / static_cast<double>(scaleX) + envelopeX;
    const double halfY = projectedHalfY / static_cast<double>(scaleY) + envelopeY;
    if (!std::isfinite(halfX) || !std::isfinite(halfY) || !std::isfinite(envelopeX) || !std::isfinite(envelopeY))
        return false;

    SpriteRenderBounds certified{
        std::nextafter(static_cast<double>(worldX) - halfX, -std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(worldY) - halfY, -std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(worldX) + halfX, std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(worldY) + halfY, std::numeric_limits<double>::infinity())};
    if (!std::isfinite(certified.minX) || !std::isfinite(certified.minY) || !std::isfinite(certified.maxX) ||
        !std::isfinite(certified.maxY))
        return false;
    bounds = certified;
    return true;
}

inline bool makeCertifiedRenderViewQuery(const SpriteRenderView &view, SpriteRenderBounds &bounds) noexcept
{
    using namespace sprite_render_geometry_detail;
    if (!makeSpriteRenderProjectionKey(view).valid || !std::isfinite(view.cameraX) || !std::isfinite(view.cameraY) ||
        !projectedWithinCertificate(view.cameraX, view.scaleX) ||
        !projectedWithinCertificate(view.cameraY, view.scaleY))
        return false;

    const double outputWorldWidth = static_cast<double>(view.outputWidth) / static_cast<double>(view.scaleX);
    const double outputWorldHeight = static_cast<double>(view.outputHeight) / static_cast<double>(view.scaleY);
    const double envelopeX = 2.0 / static_cast<double>(view.scaleX);
    const double envelopeY = 2.0 / static_cast<double>(view.scaleY);
    if (!std::isfinite(outputWorldWidth) || !std::isfinite(outputWorldHeight) || !std::isfinite(envelopeX) ||
        !std::isfinite(envelopeY) || outputWorldWidth <= 0.0 || outputWorldHeight <= 0.0)
        return false;

    SpriteRenderBounds certified{
        std::nextafter(static_cast<double>(view.cameraX) - envelopeX, -std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(view.cameraY) - envelopeY, -std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(view.cameraX) + outputWorldWidth + envelopeX,
                       std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(view.cameraY) + outputWorldHeight + envelopeY,
                       std::numeric_limits<double>::infinity())};
    if (!std::isfinite(certified.minX) || !std::isfinite(certified.minY) || !std::isfinite(certified.maxX) ||
        !std::isfinite(certified.maxY))
        return false;
    bounds = certified;
    return true;
}

// Bounds and classification consume the already prepared destination
// rectangle. Invalid or uncertain inputs return Unclassifiable and stay in the
// selection for geometry validation at submission time.
inline SpriteRenderClassification classifySpriteForRender(const SDL_FRect &destination, float angleDegrees,
                                                            const SpriteRenderView &view,
                                                            SpriteRenderBounds *paddedBounds = nullptr) noexcept
{
    if (!view.cullingEligible || view.outputWidth <= 0 || view.outputHeight <= 0 ||
        !std::isfinite(view.cameraX) || !std::isfinite(view.cameraY) ||
        !sprite_render_geometry_detail::finitePositive(view.scaleX) ||
        !sprite_render_geometry_detail::finitePositive(view.scaleY) || !std::isfinite(destination.x) ||
        !std::isfinite(destination.y) || !sprite_render_geometry_detail::finitePositive(destination.w) ||
        !sprite_render_geometry_detail::finitePositive(destination.h) || !std::isfinite(angleDegrees))
        return SpriteRenderClassification::Unclassifiable;

    SpriteRenderBounds bounds;
    if (!sprite_render_geometry_detail::makePaddedBounds(destination, angleDegrees, bounds))
        return SpriteRenderClassification::Unclassifiable;
    if (paddedBounds != nullptr)
        *paddedBounds = bounds;

    if (bounds.maxX < 0.0 || bounds.maxY < 0.0 || bounds.minX > view.outputWidth ||
        bounds.minY > view.outputHeight)
        return SpriteRenderClassification::Reject;
    return SpriteRenderClassification::Retain;
}
} // namespace platformator_detail
