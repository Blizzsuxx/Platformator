#include "bench_common.h"

#include <box2d/box2d.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>

namespace
{

using namespace box2d_bench;

struct TaskSlot
{
    tbb::task_group group;
};

struct TbbScheduler
{
    static constexpr size_t MAX_SLOTS = 128;
    int workerCount = 1;
    tbb::task_arena arena;
    std::array<TaskSlot, MAX_SLOTS> slots;
    std::atomic<size_t> nextSlot{0};

    explicit TbbScheduler(int workers) : workerCount(std::max(1, workers)), arena(std::max(1, workers))
    {
    }

    void resetStep()
    {
        nextSlot.store(0, std::memory_order_relaxed);
    }

    static void *enqueue(b2TaskCallback *task, int itemCount, int minRange, void *taskContext, void *userContext)
    {
        auto *self = static_cast<TbbScheduler *>(userContext);
        const size_t idx = self->nextSlot.fetch_add(1, std::memory_order_relaxed);
        if (idx >= MAX_SLOTS)
        {
            task(0, itemCount, 0, taskContext);
            return nullptr;
        }
        TaskSlot *slot = &self->slots[idx];
        const uint32_t maxWorkers = static_cast<uint32_t>(self->workerCount);

        if (itemCount <= 1 || itemCount <= minRange)
        {
            slot->group.run([task, itemCount, taskContext, maxWorkers]() {
                const int tid = tbb::this_task_arena::current_thread_index();
                const uint32_t wid = static_cast<uint32_t>(tid >= 0 ? tid : 0) % maxWorkers;
                task(0, itemCount, wid, taskContext);
            });
        }
        else
        {
            const int grain = std::max(1, minRange);
            slot->group.run([task, itemCount, grain, taskContext, maxWorkers]() {
                tbb::parallel_for(tbb::blocked_range<int>(0, itemCount, static_cast<size_t>(grain)),
                                  [task, taskContext, maxWorkers](const tbb::blocked_range<int> &r) {
                                      const int tid = tbb::this_task_arena::current_thread_index();
                                      const uint32_t wid = static_cast<uint32_t>(tid >= 0 ? tid : 0) % maxWorkers;
                                      task(r.begin(), r.end(), wid, taskContext);
                                  });
            });
        }
        return slot;
    }

    static void finish(void *userTask, void * /*userContext*/)
    {
        if (userTask != nullptr)
        {
            static_cast<TaskSlot *>(userTask)->group.wait();
        }
    }
};

struct WorldStats
{
    int dynamicBodies = 0;
    int staticBodies = 0;
};

void addStaticBox(b2WorldId worldId, float x, float y, float w, float h, float friction, float restitution,
                  WorldStats &stats)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_staticBody;
    bd.position = {x * UNIT_SCALE, y * UNIT_SCALE};
    b2BodyId bodyId = b2CreateBody(worldId, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.material.friction = friction;
    sd.material.restitution = restitution;
    sd.invokeContactCreation = false;

    b2Polygon box = b2MakeBox(0.5f * w * UNIT_SCALE, 0.5f * h * UNIT_SCALE);
    b2CreatePolygonShape(bodyId, &sd, &box);
    stats.staticBodies++;
}

void addDynamicShape(b2WorldId worldId, bool isBox, float x, float y, float sizeOrRadius, float vx, float vy,
                     float omega, float friction, float restitution, bool enableSleep, WorldStats &stats)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {x * UNIT_SCALE, y * UNIT_SCALE};
    bd.linearVelocity = {vx * UNIT_SCALE, vy * UNIT_SCALE};
    bd.angularVelocity = omega;
    bd.linearDamping = 0.0f;
    bd.angularDamping = 0.0f;
    bd.enableSleep = enableSleep;
    bd.sleepThreshold = 5.0f * UNIT_SCALE; // Match Platformator's LINEAR_SLEEP_THRESHOLD
    b2BodyId bodyId = b2CreateBody(worldId, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 1.0f;
    sd.material.friction = friction;
    sd.material.restitution = restitution;

    if (isBox)
    {
        b2Polygon box = b2MakeBox(0.5f * sizeOrRadius * UNIT_SCALE, 0.5f * sizeOrRadius * UNIT_SCALE);
        b2CreatePolygonShape(bodyId, &sd, &box);
    }
    else
    {
        b2Circle circle = {{0.0f, 0.0f}, sizeOrRadius * UNIT_SCALE};
        b2CreateCircleShape(bodyId, &sd, &circle);
    }

    stats.dynamicBodies++;
}

void buildBroadPhase(b2WorldId worldId, const Options &opts, WorldStats &stats)
{
    constexpr float laneStepY = 80.0f;
    constexpr float laneSize = 48.0f;
    const float laneStartY = -0.5f * static_cast<float>(opts.count - 1) * laneStepY;
    b2Polygon box = b2MakeBox(0.5f * laneSize * UNIT_SCALE, 0.5f * laneSize * UNIT_SCALE);

    for (int i = 0; i < opts.count; ++i)
    {
        const size_t p = static_cast<size_t>(i) % BROAD_START_X.size();
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_kinematicBody;
        bd.position = {BROAD_START_X[p] * UNIT_SCALE, (laneStartY + static_cast<float>(i) * laneStepY) * UNIT_SCALE};
        bd.linearVelocity = {BROAD_VEL_X[p] * UNIT_SCALE, 0.0f};
        bd.enableSleep = false;
        b2BodyId bodyId = b2CreateBody(worldId, &bd);

        b2ShapeDef sd = b2DefaultShapeDef();
        b2CreatePolygonShape(bodyId, &sd, &box);
        stats.dynamicBodies++;
    }
}

void buildRigidBodyContainer(b2WorldId worldId, const Options &opts, WorldStats &stats)
{
    constexpr float boxSize = 28.0f;
    constexpr float circleRadius = 14.0f;
    constexpr float wallThickness = 64.0f;
    constexpr float spawnMargin = 10.0f;
    constexpr float cellPadding = 10.0f;
    constexpr float minW = 1600.0f;
    constexpr float minH = 900.0f;
    constexpr float friction = 1.0f;
    constexpr float restitution = 0.0f;

    const int boxCount = opts.count / 2;
    const int circleCount = opts.count - boxCount;
    const float cellSize = boxSize + cellPadding;
    const float usableW = std::max(minW - 2.0f * spawnMargin, cellSize);
    const size_t minCols = std::max<size_t>(1, static_cast<size_t>(usableW / cellSize));
    const double aspectRatio = static_cast<double>(minW) / static_cast<double>(minH);
    const size_t scaledCols = std::max<size_t>(
        1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(opts.count) * aspectRatio))));
    const size_t cols = std::max(minCols, scaledCols);
    const size_t rows = std::max<size_t>(1, (static_cast<size_t>(opts.count) + cols - 1) / cols);
    const float interiorW = std::max(minW, static_cast<float>(cols) * cellSize + 2.0f * spawnMargin);
    const float interiorH = std::max(minH, static_cast<float>(rows) * cellSize + 2.0f * spawnMargin);
    const float left = -0.5f * interiorW;
    const float top = -0.5f * interiorH;
    const float halfW = 0.5f * interiorW;
    const float halfH = 0.5f * interiorH;

    const float horizW = interiorW + 2.0f * wallThickness;
    const float vertH = interiorH + 2.0f * wallThickness;

    addStaticBox(worldId, 0.0f, halfH + 0.5f * wallThickness + 20.0f, horizW, wallThickness, friction, restitution,
                 stats);
    addStaticBox(worldId, 0.0f, -halfH - 0.5f * wallThickness, horizW, wallThickness, friction, restitution, stats);
    addStaticBox(worldId, -halfW - 0.5f * wallThickness, 0.0f, wallThickness, vertH, friction, restitution, stats);
    addStaticBox(worldId, halfW + 0.5f * wallThickness, 0.0f, wallThickness, vertH, friction, restitution, stats);

    int sb = 0, sc = 0;
    size_t idx = 0;
    while (sb < boxCount || sc < circleCount)
    {
        const bool isBox = sb < boxCount && (sc >= circleCount || idx % 2 == 0);
        const size_t col = idx % cols;
        const size_t row = idx / cols;
        const float x = left + spawnMargin + (static_cast<float>(col) + 0.5f) * cellSize +
                        static_cast<float>(static_cast<int>(idx % 3) - 1) * 2.0f;
        const float y = top + spawnMargin + (static_cast<float>(row) + 0.5f) * cellSize +
                        static_cast<float>(static_cast<int>(idx % 4) - 1) * 1.5f;
        const float vx = RBC_VX[idx % RBC_VX.size()];
        const float vy = RBC_VY[idx % RBC_VY.size()];
        const float omega = RBC_OMEGA[idx % RBC_OMEGA.size()];

        addDynamicShape(worldId, isBox, x, y, isBox ? boxSize : circleRadius, vx, vy, omega, friction, restitution,
                        opts.enableSleep, stats);
        if (isBox)
            sb++;
        else
            sc++;
        idx++;
    }
}

void buildIdealWorld(b2WorldId worldId, const Options &opts, WorldStats &stats)
{
    constexpr size_t bodiesPerChamber = 20;
    constexpr float chamberInterior = 300.0f;
    constexpr float wallThickness = 24.0f;
    constexpr float chamberPitch = 600.0f;
    constexpr float boxSize = 24.0f;
    constexpr float circleRadius = 12.0f;
    constexpr float bodyStep = 46.0f;
    constexpr float friction = 0.8f;
    constexpr float restitution = 0.15f;

    const size_t total = static_cast<size_t>(std::max(1, opts.count));
    const size_t chamberCount = (total + bodiesPerChamber - 1) / bodiesPerChamber;
    const size_t chamberCols = std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(chamberCount)))));
    const size_t chamberRows = std::max<size_t>(1, (chamberCount + chamberCols - 1) / chamberCols);
    const float originX = -0.5f * static_cast<float>(chamberCols - 1) * chamberPitch;
    const float originY = -0.5f * static_cast<float>(chamberRows - 1) * chamberPitch;
    const float halfInterior = 0.5f * chamberInterior;
    const float fullSpan = chamberInterior + 2.0f * wallThickness;

    size_t spawned = 0;
    for (size_t c = 0; c < chamberCount; ++c)
    {
        const float cx = originX + static_cast<float>(c % chamberCols) * chamberPitch;
        const float cy = originY + static_cast<float>(c / chamberCols) * chamberPitch;

        addStaticBox(worldId, cx, cy + halfInterior + 0.5f * wallThickness, fullSpan, wallThickness, friction,
                     restitution, stats);
        addStaticBox(worldId, cx, cy - halfInterior - 0.5f * wallThickness, fullSpan, wallThickness, friction,
                     restitution, stats);
        addStaticBox(worldId, cx - halfInterior - 0.5f * wallThickness, cy, wallThickness, fullSpan, friction,
                     restitution, stats);
        addStaticBox(worldId, cx + halfInterior + 0.5f * wallThickness, cy, wallThickness, fullSpan, friction,
                     restitution, stats);

        for (size_t local = 0; local < bodiesPerChamber && spawned < total; ++local, ++spawned)
        {
            const size_t bCol = local % 5;
            const size_t bRow = local / 5;
            const float jx = static_cast<float>(static_cast<int>(spawned % 3) - 1) * 1.5f;
            const float jy = static_cast<float>(static_cast<int>(spawned % 4) - 1) * 1.0f;
            const float bx = cx + (static_cast<float>(bCol) - 2.0f) * bodyStep + jx;
            const float by = cy + (static_cast<float>(bRow) - 1.5f) * bodyStep + jy;
            const float vx = RBC_VX[spawned % RBC_VX.size()];
            const float vy = RBC_VY[spawned % RBC_VY.size()];
            const float omega = RBC_OMEGA[spawned % RBC_OMEGA.size()];
            const bool isBox = (spawned % 2 == 0);

            addDynamicShape(worldId, isBox, bx, by, isBox ? boxSize : circleRadius, vx, vy, omega, friction,
                            restitution, opts.enableSleep, stats);
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        const Options opts = parseOptions(argc, argv);

        std::unique_ptr<TbbScheduler> scheduler;
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {0.0f, opts.scenario == "broad_phase" ? 0.0f : GRAVITY_Y};
        worldDef.enableSleep = opts.enableSleep;
        worldDef.enableContinuous = false; // Match Platformator/Godot discrete collision step

        if (opts.workers > 1)
        {
            scheduler = std::make_unique<TbbScheduler>(opts.workers);
            worldDef.workerCount = opts.workers;
            worldDef.enqueueTask = &TbbScheduler::enqueue;
            worldDef.finishTask = &TbbScheduler::finish;
            worldDef.userTaskContext = scheduler.get();
        }

        b2WorldId worldId = b2CreateWorld(&worldDef);
        WorldStats stats;
        if (opts.scenario == "broad_phase")
            buildBroadPhase(worldId, opts, stats);
        else if (opts.scenario == "rigid_body_container")
            buildRigidBodyContainer(worldId, opts, stats);
        else if (opts.scenario == "ideal_world")
            buildIdealWorld(worldId, opts, stats);
        else
            throw std::runtime_error("Unknown scenario: " + opts.scenario);

        const float dt = 1.0f / static_cast<float>(opts.physicsFps);
        std::vector<double> samplesMs;
        samplesMs.reserve(static_cast<size_t>(opts.measureFrames));

        double awakeSum = 0.0;
        double contactSum = 0.0;

        auto runStep = [&]() {
            if (scheduler)
            {
                scheduler->resetStep();
                scheduler->arena.execute([&]() { b2World_Step(worldId, dt, opts.subSteps); });
            }
            else
            {
                b2World_Step(worldId, dt, opts.subSteps);
            }
        };

        const int totalSteps = opts.warmupFrames + opts.measureFrames;
        for (int step = 0; step < totalSteps; ++step)
        {
            const auto t0 = std::chrono::steady_clock::now();
            runStep();
            const auto t1 = std::chrono::steady_clock::now();

            if (step >= opts.warmupFrames)
            {
                const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                samplesMs.push_back(ms);

                const b2Counters counters = b2World_GetCounters(worldId);
                awakeSum += static_cast<double>(b2World_GetAwakeBodyCount(worldId));
                contactSum += static_cast<double>(counters.contactCount);
            }
        }

        b2DestroyWorld(worldId);

        printSummaryJson("box2d_3.1.1", opts, samplesMs, stats.dynamicBodies, stats.staticBodies,
                         awakeSum / static_cast<double>(opts.measureFrames),
                         contactSum / static_cast<double>(opts.measureFrames));
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
