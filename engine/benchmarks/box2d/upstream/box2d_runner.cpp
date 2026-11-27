#include "runner.h"

#include <box2d/box2d.h>
#include <benchmarks.h>

namespace
{
using box2d_reference::Scene;

struct Fixture
{
    b2Capacity (*capacity)();
    void (*create)(b2WorldId);
};

Fixture fixtureFor(Scene scene)
{
    switch (scene)
    {
    case Scene::LargePyramid:
        return {GetLargePyramidCapacity, CreateLargePyramid};
    case Scene::ManyPyramids:
        return {GetManyPyramidsCapacity, CreateManyPyramids};
    case Scene::Smash:
        return {GetSmashCapacity, CreateSmash};
    }
    throw std::logic_error("Unknown Box2D fixture");
}

struct World
{
    explicit World(const b2WorldDef &def) : id(b2CreateWorld(&def))
    {
        if (!b2World_IsValid(id))
            throw std::runtime_error("Could not create Box2D world");
    }

    ~World() { b2DestroyWorld(id); }
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    b2WorldId id;
};
} // namespace

int main(int argc, char **argv)
{
    try
    {
        using namespace box2d_reference;
        const auto options = parseOptions(argc, argv, B2_MAX_WORKERS);
        if (std::string_view(BOX2D_REFERENCE_CONFIGURATION) != "Release")
            throw std::runtime_error("Build Box2D in Release for the upstream Release scene populations");
        const auto fixture = fixtureFor(options.workload.scene);
        auto worldDef = b2DefaultWorldDef();
        worldDef.capacity = fixture.capacity();
        worldDef.workerCount = options.workers;
        const World world(worldDef);
        if (b2World_GetWorkerCount(world.id) != options.workers)
            throw std::runtime_error("Box2D could not provide the requested worker count");
        fixture.create(world.id);
        constexpr int subSteps = 4;
        const auto inspect = [&] {
            const auto counters = b2World_GetCounters(world.id);
            const int staticBodies = options.workload.staticShapes == 0 ? 0 : 1;
            if (counters.bodyCount != options.workload.dynamicBodies + staticBodies ||
                counters.shapeCount != options.workload.dynamicBodies + options.workload.staticShapes)
                throw std::runtime_error("Unexpected Box2D scene population");
            return nlohmann::json{{"bodies", counters.bodyCount}, {"shapes", counters.shapeCount},
                                  {"awake_dynamic_bodies", b2World_GetAwakeBodyCount(world.id)},
                                  {"box2d_contacts", counters.contactCount}};
        };
        measure(options, {{"engine", "Box2D v3.2.0"},
                          {"commit", BOX2D_REFERENCE_COMMIT},
                          {"configuration", BOX2D_REFERENCE_CONFIGURATION},
                          {"compiler", BOX2D_REFERENCE_COMPILER},
                          {"scheduler", "native"},
                          {"avx2_available", b2IsAVX2Available()},
                          {"double_precision", b2IsDoublePrecision()},
                          {"continuous_collision", worldDef.enableContinuous},
                          {"sub_steps", subSteps},
                          {"timed_call", "b2World_Step"}},
                [&] { b2World_Step(world.id, static_cast<float>(dt), subSteps); }, inspect);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
