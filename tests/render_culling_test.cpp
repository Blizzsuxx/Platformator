#include "renderculling.h"
#include "test_support.h"

#include <platformator/camera.h>
#include <platformator/runtime.h>
#include <platformator/sprite.h>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace test_support;

namespace
{
using platformator_detail::RenderBounds;
using platformator_detail::RenderCulling;
using platformator_detail::VisibilityCandidate;

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct QuadReference
{
    std::array<std::array<double, 2>, 4> corners;
    RenderBounds bounds;
};

QuadReference projectSubmittedQuad(const Sprite &sprite, const Camera &camera, int outputWidth, int outputHeight)
{
    const float scaleX = static_cast<float>(outputWidth) / camera.getWidth();
    const float scaleY = static_cast<float>(outputHeight) / camera.getHeight();
    const auto *object = sprite.getGameObject();
    const auto *cameraObject = camera.getGameObject();
    const float width = sprite.getWidth();
    const float height = sprite.getHeight();
    // Match the float destination rectangle that Camera::render passes to SDL.
    const float x = (object->getPosition().x() - width * 0.5f - cameraObject->getPosition().x()) * scaleX;
    const float y = (object->getPosition().y() - height * 0.5f - cameraObject->getPosition().y()) * scaleY;
    const float w = width * scaleX;
    const float h = height * scaleY;
    const double centerX = static_cast<double>(x) + static_cast<double>(w) * 0.5;
    const double centerY = static_cast<double>(y) + static_cast<double>(h) * 0.5;
    const double radians = static_cast<double>(object->getRotationInDegrees()) * std::numbers::pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const std::array<std::array<double, 2>, 4> submitted{{
        {x, y}, {static_cast<double>(x) + w, y}, {static_cast<double>(x) + w, static_cast<double>(y) + h},
        {x, static_cast<double>(y) + h}}};

    QuadReference result{};
    result.bounds = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (size_t i = 0; i < submitted.size(); ++i)
    {
        const double dx = submitted[i][0] - centerX;
        const double dy = submitted[i][1] - centerY;
        const double rotatedX = centerX + cosine * dx - sine * dy;
        const double rotatedY = centerY + sine * dx + cosine * dy;
        const double worldX = static_cast<double>(cameraObject->getPosition().x()) + rotatedX / scaleX;
        const double worldY = static_cast<double>(cameraObject->getPosition().y()) + rotatedY / scaleY;
        result.corners[i] = {worldX, worldY};
        result.bounds.minX = std::min(result.bounds.minX, worldX);
        result.bounds.minY = std::min(result.bounds.minY, worldY);
        result.bounds.maxX = std::max(result.bounds.maxX, worldX);
        result.bounds.maxY = std::max(result.bounds.maxY, worldY);
    }
    return result;
}

bool overlapsInclusive(const RenderBounds &a, const RenderBounds &b)
{
    return a.minX <= b.maxX && a.maxX >= b.minX && a.minY <= b.maxY && a.maxY >= b.minY;
}

void testProjectedGeometryAndView()
{
    RuntimeOptions options;
    options.windowSettings.headless = true;
    platformator::Runtime runtime(options);

    auto *cameraObject = runtime.createGameObject()->setPosition({250.0f, -225.0f});
    cameraObject->addComponent<Camera>(100.0f, 50.0f);
    auto *camera = cameraObject->getComponent<Camera>();
    auto *parent = runtime.createGameObject()->setPosition({300.0f, -200.0f});
    auto *spriteObject = runtime.createGameObject();
    parent->addChild(spriteObject);
    spriteObject->setPosition({340.0f, -150.0f})->setScale({1.25f, 0.75f})->setRotation(0.71f);
    spriteObject->addComponent<Sprite>();
    auto *sprite = spriteObject->getComponent<Sprite>();
    sprite->setWidth(24.0f);
    sprite->setHeight(10.0f);
    runtime.simulateFrame(dt);

    RenderBounds actual{};
    require(RenderCulling::computeSpriteWorldBounds(*sprite, *camera, 800, 600, actual),
            "Finite scaled and rotated sprite geometry has a conservative world bound");
    const QuadReference reference = projectSubmittedQuad(*sprite, *camera, 800, 600);
    const double pixelX = camera->getWidth() / 800.0;
    const double pixelY = camera->getHeight() / 600.0;
    require(actual.minX <= reference.bounds.minX &&
                actual.maxX >= reference.bounds.maxX &&
                actual.minY <= reference.bounds.minY &&
                actual.maxY >= reference.bounds.maxY,
            "Cached geometry encloses projected corners; camera query owns the raster guard");

    RenderBounds view{};
    require(RenderCulling::computeCameraWorldBounds(*camera, 800, 600, view),
            "Anisotropically scaled camera view has a supported query bound");
    const auto cameraRect = camera->getCamera();
    require(view.minX <= cameraRect.x - pixelX && view.minY <= cameraRect.y - pixelY &&
                view.maxX >= cameraRect.x + cameraRect.w + pixelX &&
                view.maxY >= cameraRect.y + cameraRect.h + pixelY,
            "Camera query includes a minimum one-output-pixel guard per axis");

    RenderBounds touching = view;
    touching.minX = view.maxX;
    touching.maxX = view.maxX + 1.0;
    require(overlapsInclusive(view, touching), "The reference keeps exact corner/edge ties visible");
}

void testSignedDimensionsAndRounding()
{
    RuntimeOptions options;
    options.windowSettings.headless = true;
    platformator::Runtime runtime(options);
    auto *cameraObject = runtime.createGameObject()->setPosition({1.0e9f, -1.0e9f});
    cameraObject->addComponent<Camera>(100.0f, 60.0f);
    auto *camera = cameraObject->getComponent<Camera>();
    auto *object = runtime.createGameObject()->setPosition({1.0e9f + 128.0f, -1.0e9f + 24.0f})
                       ->setScale({1.5f, 0.5f})
                       ->setRotation(-0.43f);
    object->addComponent<Sprite>();
    auto *sprite = object->getComponent<Sprite>();
    sprite->setWidth(-18.0f); // SDL destination geometry accepts signed width; bounds normalize the corners.
    sprite->setHeight(32.0f);
    runtime.simulateFrame(dt);

    RenderBounds actual{};
    require(RenderCulling::computeSpriteWorldBounds(*sprite, *camera, 1000, 300, actual),
            "Signed destination dimensions and large finite coordinates remain representable");
    const QuadReference reference = projectSubmittedQuad(*sprite, *camera, 1000, 300);
    require(actual.minX <= reference.bounds.minX && actual.minY <= reference.bounds.minY &&
                actual.maxX >= reference.bounds.maxX && actual.maxY >= reference.bounds.maxY,
            "Outward-rounded bounds include the float-rounded submitted quad at large coordinates");

    object->setPosition({std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()});
    runtime.simulateFrame(dt);
    require(!RenderCulling::computeSpriteWorldBounds(*sprite, *camera, 1000, 300, actual),
            "Unrepresentable projected coordinates are reported for conservative uncullable handling");
}

void testInvalidGeometryAndSparseOrder()
{
    RuntimeOptions options;
    options.windowSettings.headless = true;
    platformator::Runtime runtime(options);
    auto *cameraObject = runtime.createGameObject();
    cameraObject->addComponent<Camera>(100.0f, 100.0f);
    auto *camera = cameraObject->getComponent<Camera>();
    auto *object = runtime.createGameObject();
    object->addComponent<Sprite>();
    auto *sprite = object->getComponent<Sprite>();
    sprite->setWidth(0.0f);
    sprite->setHeight(10.0f);
    runtime.simulateFrame(dt);

    RenderBounds bounds{};
    require(!RenderCulling::computeSpriteWorldBounds(*sprite, *camera, 640, 480, bounds),
            "Zero-area sprite geometry stays on the conservative uncullable path");
    sprite->setWidth(std::numeric_limits<float>::quiet_NaN());
    require(!RenderCulling::computeSpriteWorldBounds(*sprite, *camera, 640, 480, bounds),
            "Nonfinite sprite dimensions are rejected from indexed geometry");
    require(!RenderCulling::computeCameraWorldBounds(*camera, 0, 480, bounds),
            "An invalid output extent is rejected before visibility queries");

    std::vector<VisibilityCandidate> candidates{{4, 4}, {8, 3}, {1, 1}, {7, 6}, {2, 3}};
    RenderCulling::sortByDrawOrder(candidates);
    require(candidates == std::vector<VisibilityCandidate>{{1, 1}, {2, 3}, {8, 3}, {4, 4}, {7, 6}},
            "Sparse query hits restore current window order and use stable identity order for ties");
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        testProjectedGeometryAndView();
        testSignedDimensionsAndRounding();
        testInvalidGeometryAndSparseOrder();
        std::cout << "Render culling geometry regressions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
