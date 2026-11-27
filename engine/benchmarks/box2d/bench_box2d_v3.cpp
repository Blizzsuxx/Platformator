#include "bench_common.h"

#include <box2d/box2d.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define BOX2D_BENCH_CPU_PAUSE() _mm_pause()
#else
#define BOX2D_BENCH_CPU_PAUSE() std::this_thread::yield()
#endif

namespace
{

using namespace box2d_bench;

struct alignas(64) TaskSlot
{
    b2TaskCallback *task = nullptr;
    void *taskContext = nullptr;
    int itemCount = 0;
    int chunkSize = 1;
    uint32_t slotIndex = 0;
    alignas(64) std::atomic<int> nextItem{0};
    alignas(64) std::atomic<int> inFlight{0};
};

// Lightweight lock-free work-claiming pool with permanent 0..workerCount-1 thread IDs.
// Avoids nested TBB task_group + parallel_for overhead and guarantees unique workerIndex per thread.
struct WorkerPool
{
    static constexpr size_t MAX_SLOTS = 64;
    int workerCount = 1;
    uint64_t mainInUseMask = 0;
    alignas(64) std::atomic<uint64_t> activeMask{0};
    alignas(64) std::atomic<uint32_t> sleepingWorkers{0};
    alignas(64) std::atomic<bool> stop{false};
    alignas(64) std::array<TaskSlot, MAX_SLOTS> slots{};
    std::mutex sleepMutex;
    std::condition_variable sleepCv;
    std::vector<std::thread> threads;

    explicit WorkerPool(int workers) : workerCount(std::max(1, workers))
    {
        for (size_t i = 0; i < MAX_SLOTS; ++i)
        {
            slots[i].slotIndex = static_cast<uint32_t>(i);
        }
        threads.reserve(static_cast<size_t>(workerCount - 1));
        for (int w = 1; w < workerCount; ++w)
        {
            threads.emplace_back([this, wid = static_cast<uint32_t>(w)] { workerLoop(wid); });
        }
    }

    ~WorkerPool()
    {
        stop.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(sleepMutex);
        }
        sleepCv.notify_all();
        for (auto &t : threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }

    static void drainSlot(TaskSlot &slot, uint32_t wid, std::atomic<uint64_t> &activeMask) noexcept
    {
        const int count = slot.itemCount;
        const int chunk = slot.chunkSize;
        const uint64_t clearMask = ~(uint64_t{1} << slot.slotIndex);
        while (true)
        {
            const int start = slot.nextItem.fetch_add(chunk, std::memory_order_relaxed);
            if (start >= count)
            {
                activeMask.fetch_and(clearMask, std::memory_order_release);
                break;
            }
            const int end = std::min(start + chunk, count);
            if (end >= count)
            {
                activeMask.fetch_and(clearMask, std::memory_order_release);
            }
            slot.task(start, end, wid, slot.taskContext);
            if (end >= count)
            {
                break;
            }
        }
    }

    bool helpAnyActive(uint32_t wid) noexcept
    {
        const uint64_t mask = activeMask.load(std::memory_order_acquire);
        if (mask == 0)
        {
            return false;
        }
        const uint32_t idx = static_cast<uint32_t>(__builtin_ctzll(mask));
        TaskSlot &slot = slots[idx];
        slot.inFlight.fetch_add(1, std::memory_order_acq_rel);
        if ((activeMask.load(std::memory_order_acquire) & (uint64_t{1} << idx)) != 0)
        {
            drainSlot(slot, wid, activeMask);
        }
        slot.inFlight.fetch_sub(1, std::memory_order_release);
        return true;
    }

    void workerLoop(uint32_t wid)
    {
        int idleSpins = 0;
        while (!stop.load(std::memory_order_acquire))
        {
            if (helpAnyActive(wid))
            {
                idleSpins = 0;
                continue;
            }

            if (idleSpins < 50000)
            {
                ++idleSpins;
                BOX2D_BENCH_CPU_PAUSE();
                continue;
            }

            std::unique_lock<std::mutex> lock(sleepMutex);
            sleepingWorkers.fetch_add(1, std::memory_order_acq_rel);
            sleepCv.wait(lock, [this] {
                return stop.load(std::memory_order_acquire) || activeMask.load(std::memory_order_acquire) != 0;
            });
            sleepingWorkers.fetch_sub(1, std::memory_order_release);
            idleSpins = 0;
        }
    }

    static void *enqueue(b2TaskCallback *task, int itemCount, int minRange, void *taskContext, void *userContext)
    {
        auto *self = static_cast<WorkerPool *>(userContext);
        if (self->workerCount <= 1 || itemCount <= 0)
        {
            if (itemCount > 0)
            {
                task(0, itemCount, 0, taskContext);
            }
            return nullptr;
        }

        const uint64_t freeMask = ~self->mainInUseMask;
        if (freeMask == 0)
        {
            task(0, itemCount, 0, taskContext);
            return nullptr;
        }

        const uint32_t idx = static_cast<uint32_t>(__builtin_ctzll(freeMask));
        self->mainInUseMask |= (uint64_t{1} << idx);

        TaskSlot &slot = self->slots[idx];
        slot.task = task;
        slot.taskContext = taskContext;
        slot.itemCount = itemCount;
        const int targetChunks = self->workerCount * 4;
        const int guidedChunk = (itemCount + targetChunks - 1) / targetChunks;
        slot.chunkSize = std::max(std::max(1, minRange), guidedChunk);
        slot.nextItem.store(0, std::memory_order_relaxed);
        slot.inFlight.store(0, std::memory_order_relaxed);

        self->activeMask.fetch_or(uint64_t{1} << idx, std::memory_order_release);
        if (self->sleepingWorkers.load(std::memory_order_acquire) > 0)
        {
            std::lock_guard<std::mutex> lock(self->sleepMutex);
            self->sleepCv.notify_all();
        }
        return &slot;
    }

    static void finish(void *userTask, void *userContext)
    {
        if (userTask == nullptr)
        {
            return;
        }
        auto *self = static_cast<WorkerPool *>(userContext);
        auto *slot = static_cast<TaskSlot *>(userTask);

        drainSlot(*slot, 0, self->activeMask);
        self->activeMask.fetch_and(~(uint64_t{1} << slot->slotIndex), std::memory_order_release);
        while (slot->inFlight.load(std::memory_order_acquire) != 0)
        {
            // Help execute other queued tasks (e.g. sibling b2SolverTask instances that share a barrier).
            if (!self->helpAnyActive(0))
            {
                BOX2D_BENCH_CPU_PAUSE();
            }
        }
        self->mainInUseMask &= ~(uint64_t{1} << slot->slotIndex);
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

        std::unique_ptr<WorkerPool> scheduler;
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {0.0f, opts.scenario == "broad_phase" ? 0.0f : GRAVITY_Y};
        worldDef.enableSleep = opts.enableSleep;
        worldDef.enableContinuous = false; // Match Platformator/Godot discrete collision step

        if (opts.workers > 1)
        {
            scheduler = std::make_unique<WorkerPool>(opts.workers);
            worldDef.workerCount = opts.workers;
            worldDef.enqueueTask = &WorkerPool::enqueue;
            worldDef.finishTask = &WorkerPool::finish;
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

        const int totalSteps = opts.warmupFrames + opts.measureFrames;
        for (int step = 0; step < totalSteps; ++step)
        {
            const auto t0 = std::chrono::steady_clock::now();
            b2World_Step(worldId, dt, opts.subSteps);
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

#ifndef BOX2D_BENCH_AVX2
#define BOX2D_BENCH_AVX2 0
#endif
        printSummaryJson(BOX2D_BENCH_AVX2 ? "box2d_3.1.1_avx2" : "box2d_3.1.1_sse2", opts, samplesMs,
                         stats.dynamicBodies, stats.staticBodies,
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
