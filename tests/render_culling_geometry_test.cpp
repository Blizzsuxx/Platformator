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

    std::cout << "geometry samples=" << (cases.size() + 2 + 5000) << " randomized seed=0xC0111DE\n";
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
