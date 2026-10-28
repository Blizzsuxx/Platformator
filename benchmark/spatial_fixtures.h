#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <Eigen/Dense>

class Camera;
class GameObject;
class Sprite;
namespace platformator { class Runtime; }

namespace platformator_benchmark
{
struct SpatialFixtureSettings
{
    size_t spriteCount = 1000;
    double visiblePercent = 10.0;
    double changedPercent = 10.0;
    std::string variant = "static";
    bool combined = false;
};

struct SpatialFixture
{
    std::vector<GameObject *> objects;
    std::vector<Sprite *> sprites;
    Camera *camera = nullptr;
    size_t visibleCount = 0;
    size_t changedCount = 0;
    size_t colliderCount = 0;
    size_t animationCount = 0;
    size_t churnCount = 0;
    double elapsedSeconds = 0.0;
    size_t frame = 0;
    std::vector<Eigen::Vector2f> initialPositions;
};

SpatialFixture buildSpatialFixture(platformator::Runtime &runtime, const SpatialFixtureSettings &settings);
void configureSpatialFixtureCamera(platformator::Runtime &runtime, SpatialFixture &fixture);
void stepSpatialFixture(platformator::Runtime &runtime, SpatialFixture &fixture,
                        const SpatialFixtureSettings &settings, double dt);
}
