#include "spriterendergeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using platformator_detail::SpriteRenderBounds;
using platformator_detail::SpriteRenderClassification;
using platformator_detail::SpriteRenderView;

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct OracleBounds
{
    double minX;
    double minY;
    double maxX;
    double maxY;
};

OracleBounds transformedCornerOracle(const SDL_FRect &destination, double angleDegrees)
{
    const double x = destination.x;
    const double y = destination.y;
    const double width = destination.w;
    const double height = destination.h;
    const double centerX = x + width * 0.5;
    const double centerY = y + height * 0.5;
    const double radians = angleDegrees * (std::acos(-1.0) / 180.0);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);

    const std::array<std::array<double, 2>, 4> corners = {{{x, y}, {x + width, y}, {x, y + height},
                                                            {x + width, y + height}}};
    OracleBounds bounds{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto &corner : corners)
    {
        const double offsetX = corner[0] - centerX;
        const double offsetY = corner[1] - centerY;
        const double transformedX = centerX + offsetX * cosine - offsetY * sine;
        const double transformedY = centerY + offsetX * sine + offsetY * cosine;
        bounds.minX = std::min(bounds.minX, transformedX);
        bounds.minY = std::min(bounds.minY, transformedY);
        bounds.maxX = std::max(bounds.maxX, transformedX);
        bounds.maxY = std::max(bounds.maxY, transformedY);
    }
    return bounds;
}

bool strictlyOutside(const OracleBounds &bounds, int width, int height)
{
    return bounds.maxX < 0.0 || bounds.maxY < 0.0 || bounds.minX > width || bounds.minY > height;
}

void checkValid(const SDL_FRect &destination, float angleDegrees, const SpriteRenderView &view,
                const std::string &label);

bool overlaps(const SpriteRenderBounds &a, const SpriteRenderBounds &b)
{
    return a.maxX >= b.minX && a.minY <= b.maxY && a.maxY >= b.minY && a.minX <= b.maxX;
}

SDL_FRect actualDestination(float worldX, float worldY, float width, float height, const SpriteRenderView &view)
{
    // Preserve Camera::renderWithView's exact float expression order as the
    // implementation-under-test input; expected bounds below are independent.
    return SDL_FRect{(worldX - width * 0.5f - view.cameraX) * view.scaleX,
                     (worldY - height * 0.5f - view.cameraY) * view.scaleY, width * view.scaleX,
                     height * view.scaleY};
}

void checkCertifiedProxy(float worldX, float worldY, float width, float height, float angleDegrees,
                         const SpriteRenderView &view, const std::string &label)
{
    SpriteRenderBounds proxy;
    require(platformator_detail::makeCertifiedSpriteRenderProxy(worldX, worldY, width, height, angleDegrees,
                                                                 view.scaleX, view.scaleY, proxy),
            label + ": ordinary finite sprite should be admitted by the numeric certificate");

    SpriteRenderBounds query;
    require(platformator_detail::makeCertifiedRenderViewQuery(view, query),
            label + ": ordinary finite camera should have a certified query");
    const auto destination = actualDestination(worldX, worldY, width, height, view);
    SpriteRenderBounds currentPadded;
    const auto classification = platformator_detail::classifySpriteForRender(destination, angleDegrees, view,
                                                                               &currentPadded);
    require(classification != SpriteRenderClassification::Unclassifiable,
            label + ": admitted proxy input unexpectedly used the current classifier fallback");
    checkValid(destination, angleDegrees, view, label + " current-float oracle");

    const SpriteRenderBounds currentWorldPadded{
        std::nextafter(static_cast<double>(view.cameraX) + currentPadded.minX / view.scaleX,
                       -std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(view.cameraY) + currentPadded.minY / view.scaleY,
                       -std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(view.cameraX) + currentPadded.maxX / view.scaleX,
                       std::numeric_limits<double>::infinity()),
        std::nextafter(static_cast<double>(view.cameraY) + currentPadded.maxY / view.scaleY,
                       std::numeric_limits<double>::infinity())};
    require(proxy.minX <= currentWorldPadded.minX && proxy.minY <= currentWorldPadded.minY &&
                proxy.maxX >= currentWorldPadded.maxX && proxy.maxY >= currentWorldPadded.maxY,
            label + ": projection-dependent world proxy failed to enclose the independently mapped current float bounds and their classifier padding");

    if (classification == SpriteRenderClassification::Retain)
        require(overlaps(proxy, query),
                label + ": a retained current-view sprite was absent from its conservative world query");
}

void checkNumericCertificate()
{
    constexpr float certificateLimit = 16384.0f;
    const SpriteRenderView unequalProjection{15999.75f, -9000.25f, 0.5f, 1.75f, 320, 240, true};
    checkCertifiedProxy(16003.25f, -8993.5f, 145.0f, 5.25f, 89.999f, unequalProjection,
                        "large common offset, elongated near-quadrant sprite");
    checkCertifiedProxy(-105.125f, 48.875f, 500.0f, 4.0f, -0.0001f,
                        SpriteRenderView{-320.25f, 44.5f, 0.75f, 1.25f, 320, 240, true},
                        "fractional projection and edge-adjacent elongated sprite");
    checkCertifiedProxy(16380.0f, 16380.0f, 6.0f, 6.0f, 45.0f,
                        SpriteRenderView{16384.0f, 16384.0f, 1.0f, 1.0f, 320, 240, true},
                        "camera certificate boundary");

    const auto projectionA = platformator_detail::makeSpriteRenderProjectionKey(unequalProjection);
    const auto translatedView = SpriteRenderView{-500.0f, 700.0f, 0.5f, 1.75f, 320, 240, true};
    const auto projectionB = platformator_detail::makeSpriteRenderProjectionKey(translatedView);
    require(platformator_detail::sameSpriteRenderProjection(projectionA, projectionB),
            "Projection keys must exclude camera translation");
    const auto viewA = platformator_detail::makeSpriteRenderViewKey({1, 2, 3}, unequalProjection);
    const auto viewB = platformator_detail::makeSpriteRenderViewKey({1, 2, 3}, translatedView);
    require(!platformator_detail::sameSpriteRenderView(viewA, viewB),
            "View keys must include camera translation");
    const auto otherCamera = platformator_detail::makeSpriteRenderViewKey({2, 2, 3}, unequalProjection);
    require(!platformator_detail::sameSpriteRenderView(viewA, otherCamera),
            "View keys must include camera identity");
    const auto invalidView = platformator_detail::makeSpriteRenderViewKey(
        {1, 2, 3}, SpriteRenderView{std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f, 1.0f, 320, 240, true});
    require(!invalidView.valid && !platformator_detail::sameSpriteRenderView(viewA, invalidView),
            "Invalid view values must never become reusable cache keys");

    SpriteRenderBounds result;
    require(!platformator_detail::makeCertifiedSpriteRenderProxy(
                std::nextafter(certificateLimit, std::numeric_limits<float>::infinity()), 0.0f, 4.0f, 4.0f,
                0.0f, 1.0f, 1.0f, result),
            "A sprite beyond P must not enter the certified proxy index");
    require(!platformator_detail::makeCertifiedSpriteRenderProxy(0.0f, 0.0f,
                                                                 std::numeric_limits<float>::denorm_min(), 4.0f,
                                                                 0.0f, 1.0f, 1.0f, result),
            "Subnormal sprite width must conservatively fail proxy certification");
    require(!platformator_detail::makeCertifiedSpriteRenderProxy(0.0f, 0.0f, FLT_MAX, 4.0f, 0.0f,
                                                                 std::numeric_limits<float>::max(), 1.0f, result),
            "Pre-scale overflow must conservatively fail proxy certification");
    require(!platformator_detail::makeCertifiedSpriteRenderProxy(FLT_MAX, FLT_MAX, 8.0f, 8.0f, 0.0f,
                                                                 1.0f, 1.0f, result),
            "Extreme finite common offsets must conservatively fail proxy certification");

    SpriteRenderView unsafeCamera{std::nextafter(certificateLimit,
                                                  std::numeric_limits<float>::infinity()),
                                  0.0f, 1.0f, 1.0f, 320, 240, true};
    require(!platformator_detail::makeCertifiedRenderViewQuery(unsafeCamera, result),
            "A camera outside the certified domain must use the current-linear fallback");
    unsafeCamera = SpriteRenderView{0.0f, 0.0f, std::numeric_limits<float>::denorm_min(), 1.0f, 320, 240, true};
    require(!platformator_detail::makeCertifiedRenderViewQuery(unsafeCamera, result),
            "Subnormal projection scale must use the current-linear fallback");
    unsafeCamera = SpriteRenderView{FLT_MAX, -FLT_MAX, 1.0f, 1.0f, 320, 240, true};
    require(!platformator_detail::makeCertifiedRenderViewQuery(unsafeCamera, result),
            "Extreme finite camera coordinates must use the current-linear fallback");

    std::mt19937 random(0xC3111F1Eu);
    std::uniform_real_distribution<float> offset(-3000.0f, 3000.0f);
    std::uniform_real_distribution<float> local(-600.0f, 700.0f);
    std::uniform_real_distribution<float> dimension(0.125f, 500.0f);
    std::uniform_real_distribution<float> angle(-1440.0f, 1440.0f);
    std::uniform_real_distribution<float> scale(0.25f, 3.0f);
    for (int index = 0; index < 2000; ++index)
    {
        const float cameraX = offset(random);
        const float cameraY = offset(random);
        const SpriteRenderView view{cameraX, cameraY, scale(random), scale(random), 640, 160, true};
        const float worldX = cameraX + local(random);
        const float worldY = cameraY + local(random);
        checkCertifiedProxy(worldX, worldY, dimension(random), dimension(random), angle(random), view,
                            "certified random case " + std::to_string(index));
    }
}

void checkValid(const SDL_FRect &destination, float angleDegrees, const SpriteRenderView &view,
                const std::string &label)
{
    SpriteRenderBounds padded;
    const auto classification =
        platformator_detail::classifySpriteForRender(destination, angleDegrees, view, &padded);
    const OracleBounds exact = transformedCornerOracle(destination, angleDegrees);
    const OracleBounds guarded{exact.minX - 1.0, exact.minY - 1.0, exact.maxX + 1.0, exact.maxY + 1.0};

    if (classification == SpriteRenderClassification::Unclassifiable)
    {
        throw std::runtime_error(label + ": finite valid geometry unexpectedly fell back");
    }

    require(padded.minX <= guarded.minX && padded.minY <= guarded.minY && padded.maxX >= guarded.maxX &&
                padded.maxY >= guarded.maxY,
            label + ": production bounds do not contain the independent corner oracle plus one output pixel");

    if (classification == SpriteRenderClassification::Reject)
    {
        require(strictlyOutside(guarded, view.outputWidth, view.outputHeight),
                label + ": classifier rejected geometry whose padded oracle touches the output");
        require(padded.maxX < 0.0 || padded.maxY < 0.0 || padded.minX > view.outputWidth ||
                    padded.minY > view.outputHeight,
                label + ": reject was not strictly disjoint from the output");
    }
    else
    {
        require(!strictlyOutside({padded.minX, padded.minY, padded.maxX, padded.maxY}, view.outputWidth,
                                 view.outputHeight),
                label + ": retained classification must intersect the padded view");
    }
}

void checkFallback(const SDL_FRect &destination, float angleDegrees, const SpriteRenderView &view,
                   const std::string &label)
{
    require(platformator_detail::classifySpriteForRender(destination, angleDegrees, view) ==
                SpriteRenderClassification::Unclassifiable,
            label + ": invalid or ineligible geometry must retain the existing submit path");
}

void run()
{
    checkNumericCertificate();
    SpriteRenderView view{13.0f, -9.0f, 0.5f, 1.75f, 320, 240, true};
    const std::array<std::pair<SDL_FRect, float>, 16> cases = {{
        {{12.0f, 15.0f, 44.0f, 30.0f}, 0.0f},
        {{-80.0f, 40.0f, 79.0f, 30.0f}, 0.0f}, // partial left edge
        {{-10.0f, 40.0f, 10.0f, 30.0f}, 0.0f}, // exact boundary contact
        {{-200.0f, 40.0f, 50.0f, 30.0f}, 0.0f},
        {{310.0f, 230.0f, 40.0f, 30.0f}, 0.0f},
        {{-90.0f, 120.0f, 500.0f, 4.0f}, 0.0f}, // long horizontal
        {{130.0f, -90.0f, 4.0f, 500.0f}, 0.0f}, // long vertical
        {{-30.0f, 50.0f, 70.0f, 24.0f}, 90.0f},
        {{-30.0f, 50.0f, 70.0f, 24.0f}, 180.0f},
        {{-30.0f, 50.0f, 70.0f, 24.0f}, 270.0f},
        {{-30.0f, 50.0f, 70.0f, 24.0f}, 31.25f},
        {{-30.0f, 50.0f, 70.0f, 24.0f}, 89.999f},
        {{-30.0f, 50.0f, 70.0f, 24.0f}, 180.001f},
        {{31.25f, 47.75f, 18.5f, 9.25f}, -44.5f},
        {{0.0000001f, 0.0f, 0.125f, 0.125f}, 12.0f},
        {{319.875f, 239.875f, 0.125f, 0.125f}, 45.0f},
    }};
    for (size_t index = 0; index < cases.size(); ++index)
    {
        checkValid(cases[index].first, cases[index].second, view, "hand case " + std::to_string(index));
    }

    const float belowZero = std::nextafter(0.0f, -std::numeric_limits<float>::infinity());
    const float aboveWidth = std::nextafter(320.0f, std::numeric_limits<float>::infinity());
    checkValid(SDL_FRect{belowZero, 20.0f, 0.5f, 1.0f}, 0.0f, view, "adjacent float at left boundary");
    checkValid(SDL_FRect{aboveWidth, 20.0f, 0.5f, 1.0f}, 0.0f, view, "adjacent float at right boundary");

    std::mt19937 random(0xC0111DEu);
    std::uniform_real_distribution<float> xDistribution(-600.0f, 700.0f);
    std::uniform_real_distribution<float> yDistribution(-500.0f, 650.0f);
    std::uniform_real_distribution<float> sizeDistribution(0.125f, 500.0f);
    std::uniform_real_distribution<float> angleDistribution(-1440.0f, 1440.0f);
    for (int index = 0; index < 5000; ++index)
    {
        const SDL_FRect destination{xDistribution(random), yDistribution(random), sizeDistribution(random),
                                    sizeDistribution(random)};
        checkValid(destination, angleDistribution(random), view, "random case " + std::to_string(index));
    }

    checkFallback({-1.0f, 1.0f, 5.0f, 5.0f}, 0.0f, SpriteRenderView{0, 0, 1, 1, 0, 240, true},
                  "zero output width");
    checkFallback({-1.0f, 1.0f, 5.0f, 5.0f}, 0.0f, SpriteRenderView{0, 0, 0, 1, 320, 240, true},
                  "zero camera scale");
    checkFallback({0.0f, 0.0f, -1.0f, 4.0f}, 0.0f, view, "negative width");
    checkFallback({0.0f, 0.0f, 0.0f, 4.0f}, 0.0f, view, "degenerate width");
    checkFallback({0.0f, 0.0f, 4.0f, 4.0f}, std::numeric_limits<float>::infinity(), view, "infinite angle");
    checkFallback({std::numeric_limits<float>::quiet_NaN(), 0.0f, 4.0f, 4.0f}, 0.0f, view, "NaN position");
    checkFallback({0.0f, 0.0f, 4.0f, 4.0f}, 0.0f, SpriteRenderView{0, 0, 1, 1, 320, 240, false},
                  "ineligible mapping");
    checkFallback({FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX}, 45.0f, view, "extreme finite magnitude");

    std::cout << "geometry samples=" << (cases.size() + 2 + 5000 + 2003)
              << " randomized seeds=0xC0111DE,0xC3111F1E\n";
}
} // namespace

int main()
{
    try
    {
        run();
        std::cout << "[PASS] render_culling_geometry_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] render_culling_geometry_test: " << exception.what() << '\n';
        return 1;
    }
}
