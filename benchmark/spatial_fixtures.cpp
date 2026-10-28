#include "spatial_fixtures.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

#include <platformator/animator.h>
#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/rigidbody.h>
#include <platformator/runtime.h>
#include <platformator/sprite.h>

namespace
{
constexpr float spriteWidth = 16.0f;
constexpr float spriteHeight = 16.0f;
constexpr float viewWidth = 1600.0f;
constexpr float viewHeight = 900.0f;
constexpr size_t combinedColliderCount = 128;

size_t roundedPopulation(size_t population, double percent)
{
    return static_cast<size_t>(std::llround(static_cast<double>(population) * percent / 100.0));
}

Eigen::Vector2f spritePosition(size_t index, size_t visibleCount)
{
    if (index >= visibleCount)
        return {2000.0f + static_cast<float>(index - visibleCount) * 24.0f, 0.0f};
    constexpr size_t columns = 32;
    const size_t row = index / columns;
    const size_t column = index % columns;
    return {-387.5f + static_cast<float>(column) * 25.0f,
            -387.5f + static_cast<float>(row) * 25.0f};
}

GameObject *makeSprite(platformator::Runtime &runtime, size_t index, size_t visibleCount)
{
    return runtime.createGameObject()
        ->setName("Spatial sprite " + std::to_string(index))
        ->setPosition(spritePosition(index, visibleCount))
        ->addComponent<Sprite>("assets/textures/black_square.png", spriteWidth, spriteHeight);
}
}

namespace platformator_benchmark
{
SpatialFixture buildSpatialFixture(platformator::Runtime &runtime, const SpatialFixtureSettings &settings)
{
    if (settings.spriteCount == 0 || !std::isfinite(settings.visiblePercent) || settings.visiblePercent < 0.0 ||
        settings.visiblePercent > 100.0 || !std::isfinite(settings.changedPercent) || settings.changedPercent < 0.0 ||
        settings.changedPercent > 100.0)
        throw std::invalid_argument("Invalid spatial fixture population or percentage");

    SpatialFixture fixture;
    fixture.visibleCount = roundedPopulation(settings.spriteCount, settings.visiblePercent);
    fixture.changedCount = roundedPopulation(settings.spriteCount, settings.changedPercent);
    fixture.colliderCount = settings.combined ? combinedColliderCount : 0;
    fixture.sprites.reserve(settings.spriteCount);
    fixture.objects.reserve(settings.spriteCount + fixture.colliderCount + 1);
    fixture.initialPositions.reserve(settings.spriteCount);

    for (size_t index = 0; index < settings.spriteCount; ++index)
    {
        auto *object = makeSprite(runtime, index, fixture.visibleCount);
        fixture.objects.push_back(object);
        fixture.sprites.push_back(object->getComponent<Sprite>());
        fixture.initialPositions.push_back(spritePosition(index, fixture.visibleCount));
    }

    if (settings.variant == "animation")
    {
        fixture.animationCount = std::min<size_t>(fixture.visibleCount, 32);
        const auto *clip = runtime.loadAnimationClip("assets/animations/spatial_fixture.animset");
        for (size_t index = 0; index < fixture.animationCount; ++index)
        {
            auto *animator = fixture.objects[index]->addComponent<Animator>()->getComponent<Animator>();
            if (!animator->play(clip))
                throw std::runtime_error("Could not start spatial fixture animation");
        }
    }

    if (settings.combined)
    {
        for (size_t index = 0; index < fixture.colliderCount; ++index)
        {
            const float x = -480.0f + static_cast<float>(index % 16) * 64.0f;
            const float y = 240.0f + static_cast<float>(index / 16) * 48.0f;
            auto *object = runtime.createGameObject()
                               ->setName("Spatial combined collider " + std::to_string(index))
                               ->setPosition({x, y})
                               ->addComponent<Rigidbody>(KINEMATIC, false)
                               ->addComponent<BoxCollider>(20.0f, 20.0f);
            object->getComponent<Rigidbody>()->setVelocity({index % 2 == 0 ? 6.0f : -6.0f, 0.0f});
            fixture.objects.push_back(object);
        }
    }

    return fixture;
}

void configureSpatialFixtureCamera(platformator::Runtime &runtime, SpatialFixture &fixture)
{
    runtime.createMainCameraIfNoMainCameraExists();
    fixture.camera = runtime.getMainCamera();
    if (fixture.camera)
        fixture.camera->setCamera(SDL_FRect{-viewWidth * 0.5f, -viewHeight * 0.5f, viewWidth, viewHeight});
}

void stepSpatialFixture(platformator::Runtime &runtime, SpatialFixture &fixture,
                        const SpatialFixtureSettings &settings, double dt)
{
    fixture.elapsedSeconds += dt;
    ++fixture.frame;
    if (settings.variant == "moving" || settings.variant == "static" || settings.variant == "animation")
    {
        const float offset = static_cast<float>(std::sin(fixture.elapsedSeconds * 1.7) * 10.0);
        for (size_t index = 0; index < fixture.changedCount; ++index)
            fixture.objects[index]->setPosition(fixture.initialPositions[index] + Eigen::Vector2f(offset, 0.0f));
    }
    if (fixture.camera && settings.variant == "pan")
    {
        auto view = fixture.camera->getCamera();
        view.x += static_cast<float>(dt * 30.0);
        fixture.camera->setCamera(view);
    }
    else if (fixture.camera && settings.variant == "jump")
    {
        auto view = fixture.camera->getCamera();
        view.x = (fixture.frame / 30) % 2 == 0 ? -800.0f : 800.0f;
        fixture.camera->setCamera(view);
    }
    else if (fixture.camera && settings.variant == "resize")
    {
        auto view = fixture.camera->getCamera();
        const float width = (fixture.frame / 30) % 2 == 0 ? 1600.0f : 1900.0f;
        view.x = -width * 0.5f;
        view.w = width;
        fixture.camera->setCamera(view);
    }
    if (settings.variant == "churn" && fixture.frame % 30 == 0)
    {
        fixture.churnCount = std::min<size_t>(10, fixture.sprites.size());
        for (size_t index = 0; index < fixture.churnCount; ++index)
        {
            const auto position = fixture.objects[index]->getPosition();
            fixture.objects[index]->destroy();
            auto *replacement = runtime.createGameObject()
                                    ->setName("Spatial sprite replacement " + std::to_string(fixture.frame) + "/" +
                                              std::to_string(index))
                                    ->setPosition(position)
                                    ->addComponent<Sprite>("assets/textures/black_square.png", spriteWidth,
                                                           spriteHeight);
            fixture.objects[index] = replacement;
            fixture.sprites[index] = replacement->getComponent<Sprite>();
        }
    }
}
} // namespace platformator_benchmark
