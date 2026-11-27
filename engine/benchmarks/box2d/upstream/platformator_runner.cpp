#include "runner.h"

#include <cmath>
#include <oneapi/tbb/global_control.h>

#include "benchmarkbuildconfig.h"
#include "core/constants.h"
#include "core/runtimeaccess.h"
#include "physics/physicsmanager.h"
#include "platformator/boxcollider.h"
#include "platformator/runtime.h"

namespace
{
using namespace box2d_reference;
using box2d_reference::Scene;

// Box2D uses upward-positive meters and -10 m/s². Scale its lengths to
// Platformator's fixed gravity, and reflect the vertical axis.
constexpr float unitsPerMeter = GRAVITY_VECTOR_Y / 10.0f;

Rigidbody *addBox(platformator::Runtime &runtime, float x, float y, float width, float height,
                  BodyType type, bool gravity = true, float density = 1.0f)
{
    auto *object = runtime.createGameObject()
                       ->setPosition({x * unitsPerMeter, -y * unitsPerMeter})
                       ->addComponent<Rigidbody>(type, gravity && type == DYNAMIC)
                       ->addComponent<BoxCollider>(width * unitsPerMeter, height * unitsPerMeter);
    auto *body = object->getComponent<Rigidbody>()->setFriction(0.6f)->setRestitution(0.0f);
    if (type == DYNAMIC)
        body->setMass(width * height * density);
    return body;
}

void addPyramid(platformator::Runtime &runtime, int base, float left, float floor)
{
    for (int row = 0; row < base; ++row)
        for (int column = 0; column < base - row; ++column)
            addBox(runtime, left + 0.5f * (row + 1) + column, floor + row + 0.5f, 1.0f, 1.0f, DYNAMIC);
}

// Layouts adapted from Box2D v3.2.0 shared/benchmarks.c (Erin Catto, MIT).
void buildScene(platformator::Runtime &runtime, Scene scene)
{
    switch (scene)
    {
    case Scene::LargePyramid:
        addBox(runtime, 0.0f, -1.0f, 200.0f, 2.0f, STATIC);
        addPyramid(runtime, 100, -50.0f, 0.0f);
        break;
    case Scene::ManyPyramids:
    {
        constexpr int rows = 20;
        constexpr int columns = 20;
        constexpr float pitch = 11.0f;
        constexpr float floorWidth = columns * pitch;
        constexpr float floorThickness = 0.1f;
        for (int row = 0; row < rows; ++row)
            addBox(runtime, 0.0f, pitch * row - floorThickness / 2, floorWidth, floorThickness, STATIC);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                addPyramid(runtime, 10, -floorWidth / 2 + pitch * column + 0.5f, pitch * row);
        break;
    }
    case Scene::Smash:
    {
        constexpr float blockSize = 0.4f;
        constexpr int columns = 120;
        constexpr int rows = 80;
        addBox(runtime, -20.0f, 0.0f, 8.0f, 8.0f, DYNAMIC, false, 8.0f)
            ->setVelocity({40.0f * unitsPerMeter, 0.0f});
        for (int column = 0; column < columns; ++column)
            for (int row = 0; row < rows; ++row)
                addBox(runtime, 30.0f + column * blockSize, (row - rows / 2.0f) * blockSize,
                       blockSize, blockSize, DYNAMIC, false)->setIsSleeping(true);
        break;
    }
    }
}

nlohmann::json inspectScene(const platformator::Runtime &runtime, const PhysicsManager &physics, const Workload &workload)
{
    int dynamicBodies = 0;
    int staticBodies = 0;
    int awake = 0;
    for (const auto *object : runtime.getAllGameObjects())
    {
        const auto *body = object->getComponent<Rigidbody>();
        if (body == nullptr || !object->getPosition().allFinite() || !body->getVelocity().allFinite() ||
            !std::isfinite(object->getRotation()) || !std::isfinite(body->getAngularVelocity()))
            throw std::runtime_error("Invalid Platformator body state");
        if (body->getBodyType() == DYNAMIC)
        {
            ++dynamicBodies;
            awake += !body->getIsSleeping();
        }
        else if (body->getBodyType() == STATIC)
        {
            ++staticBodies;
        }
    }
    const auto shapes = physics.getColliders().size();
    if (dynamicBodies != workload.dynamicBodies || staticBodies != workload.staticShapes ||
        shapes != static_cast<size_t>(dynamicBodies + staticBodies))
        throw std::runtime_error("Unexpected Platformator scene population");
    return {{"bodies", dynamicBodies + staticBodies}, {"shapes", shapes}, {"awake_dynamic_bodies", awake},
            {"collision_pairs", physics.getContactPairs().size()}};
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const Options options = parseOptions(argc, argv, 32);
        tbb::global_control workers(tbb::global_control::max_allowed_parallelism, options.workers);
        RuntimeOptions runtimeOptions;
        runtimeOptions.windowSettings.headless = true;
        platformator::Runtime runtime(runtimeOptions);
        auto &physics = *platformator_detail::RuntimeAccess::physicsManager();
        if (options.workload.scene != box2d_reference::Scene::Smash)
            physics.disableSleepingForBenchmark();
        buildScene(runtime, options.workload.scene);
        measure(options, {{"engine", "Platformator"},
                          {"commit", PLATFORMATOR_BENCH_COMMIT},
                          {"dirty", bool(PLATFORMATOR_BENCH_DIRTY)},
                          {"source_sha256", PLATFORMATOR_BENCH_SOURCE_SHA256},
                          {"configuration", PLATFORMATOR_BENCH_CONFIGURATION},
                          {"compiler", PLATFORMATOR_BENCH_COMPILER},
                          {"compiler_flags", PLATFORMATOR_BENCH_FLAGS},
                          {"sanitizer", PLATFORMATOR_BENCH_SANITIZER},
                          {"work_probes", bool(PLATFORMATOR_ENABLE_WORK_PROBES)},
                          {"units_per_meter", unitsPerMeter},
                          {"solver_iterations", COLLISION_SOLVER_ITERATIONS},
                          {"timed_call", "Runtime::simulateFrame"}},
                [&] { runtime.simulateFrame(dt); }, [&] { return inspectScene(runtime, physics, options.workload); });
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
