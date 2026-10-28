#include "spatialreplay.h"
#include "spatialindex.h"
#include "workprobe.h"

#include <cmath>
#include <random>
#include <oneapi/tbb/global_control.h>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace platformator_detail;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string header(const char *domain, const char *caseId, int warmup, int measure, const char *extra = "")
{
    return std::string("{\"type\":\"header\",\"format\":\"platformator-spatial-trace\",\"version\":1,") +
           "\"domain\":\"" + domain + "\",\"case_id\":\"" + caseId +
           "\",\"seed\":0,\"units\":\"world\",\"dt\":0.008333333333333333,\"warmup_steps\":" +
           std::to_string(warmup) + ",\"measure_steps\":" + std::to_string(measure) +
           ",\"generator\":{\"name\":\"test\",\"revision\":\"1\",\"source_hash\":\"" +
           std::string(64, 'a') + "\"},\"parameters\":{" + extra + "}}\n";
}

std::string collisionTrace()
{
    return header("collision", "collisions", 1, 2, "\"population\":2,\"bounds_source\":\"provided-aabb\"") +
           "{\"type\":\"initial\",\"colliders\":["
           "{\"id\":{\"slot\":\"9007199254740993\",\"generation\":\"1\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true},"
           "{\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"bounds\":[2,0,3,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}]}\n"
           "{\"type\":\"step\",\"index\":0,\"ops\":[{\"op\":\"update_bounds\",\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"bounds\":[1,0,2,1]}]}\n"
           "{\"type\":\"step\",\"index\":1,\"ops\":[]}\n"
           "{\"type\":\"step\",\"index\":2,\"ops\":[]}\n";
}

std::string visibilityTrace()
{
    return header("visibility", "visibility", 0, 1,
                  "\"population\":1,\"bounds_source\":\"sprite-transform\",\"renderer\":\"none-replay\"") +
           "{\"type\":\"initial\",\"sprites\":[{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},"
           "\"center\":[5,5],\"dimensions\":[4,4],\"scale\":[1,1],\"angle_degrees\":0,\"active\":true}],"
           "\"view\":{\"id\":{\"slot\":\"99\",\"generation\":\"1\"},\"position\":[0,0],"
           "\"size\":[10,10],\"output\":[100,100]}}\n"
           "{\"type\":\"step\",\"index\":0,\"ops\":[{\"op\":\"set_view\",\"view\":{"
           "\"id\":{\"slot\":\"99\",\"generation\":\"1\"},\"position\":[1,1],"
           "\"size\":[10,10],\"output\":[100,100]}}]}\n";
}

std::string unsupportedInitialBoundsTrace()
{
    return header("collision", "unsupported-initial-extent", 0, 1,
                  "\"population\":1,\"bounds_source\":\"provided-aabb\"") +
           "{\"type\":\"initial\",\"colliders\":[{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},"
           "\"bounds\":[0,0,100000,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}]}\n"
           "{\"type\":\"step\",\"index\":0,\"ops\":[]}\n";
}

std::string unsupportedMidStepBoundsTrace()
{
    return header("collision", "unsupported-mid-step-extent", 0, 1,
                  "\"population\":2,\"bounds_source\":\"provided-aabb\"") +
           "{\"type\":\"initial\",\"colliders\":["
           "{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true},"
           "{\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}]}\n"
           "{\"type\":\"step\",\"index\":0,\"ops\":[{\"op\":\"update_bounds\","
           "\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"bounds\":[0,0,100000,1]}]}\n";
}

struct ResourceObserverFailure
{
    bool failAtInitial;
    size_t calls = 0;
};

void throwFromResourceObserver(const SpatialReplayOwnedCapacitySnapshot &, bool initial, void *context)
{
    auto &failure = *static_cast<ResourceObserverFailure *>(context);
    ++failure.calls;
    if (initial == failure.failAtInitial)
        throw std::runtime_error(failure.failAtInitial ? "injected initial resource observer failure"
                                                       : "injected step resource observer failure");
}

template <typename Callback> void rejects(const std::string &input, Callback callback, const char *message)
{
    std::istringstream stream(input);
    try
    {
        callback(stream);
    }
    catch (const std::exception &)
    {
        return;
    }
    throw std::runtime_error(message);
}

void validatesCollisionTrace()
{
    std::istringstream stream(collisionTrace());
    const auto trace = parseSpatialTrace(stream);
    require(trace.header.domain == SpatialTraceDomain::Collision, "Collision domain is retained");
    require(trace.header.caseId == "collisions" && trace.header.seed == 0, "Trace identity is retained");
    require(trace.header.warmupSteps == 1 && trace.header.measureSteps == 2 && trace.steps.size() == 3,
            "Header step counts exactly match ordered records");
    require(trace.collisionInitial.size() == 2 && trace.collisionInitial[0].id.slot == 9007199254740993ULL,
            "Uint64 decimal identity strings retain precision");
    require(trace.steps[0].operations.size() == 1 && trace.steps[0].index == 0,
            "Ordered operations are preloaded");
    for (const auto mode : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
    {
        SpatialSettings settings;
        settings.broadPhase = mode;
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::work = {};
#endif
        const auto execution = executeSpatialReplay(trace, settings, true);
#if PLATFORMATOR_ENABLE_WORK_PROBES
        require(platformator_test::work.bvhQualityScans == 0, "Replay gauges never traverse tree quality");
#endif
        require(execution.initialOutput.empty() && execution.measuredSamples.size() == 2 &&
                    execution.measuredSamples[0].output == std::vector<std::string>{"2:1/9007199254740993:1"} &&
                    execution.measuredSamples[1].output == execution.measuredSamples[0].output,
                "Both production spatial paths preserve persistent replay candidates across zero-change steps");
    }
}

void collisionReplayExceptionTeardown()
{
    for (const auto &[text, label] : {std::pair{unsupportedInitialBoundsTrace(), "initial"},
                                      std::pair{unsupportedMidStepBoundsTrace(), "mid-step"}})
    {
        std::istringstream stream(text);
        const auto trace = parseSpatialTrace(stream);
        SpatialSettings settings;
        settings.broadPhase = BroadPhaseMode::DynamicBvh;
        bool rejected = false;
        try
        {
            (void)executeSpatialReplay(trace, settings, true);
        }
        catch (const std::invalid_argument &error)
        {
            rejected = std::string(error.what()).find("unsupported padded proxy bounds or extent") != std::string::npos;
        }
        require(rejected, label == std::string("initial")
                              ? "An unsupported initial finite BVH extent fails before publishing output"
                              : "An unsupported mid-step finite BVH extent fails without returning partial output");

        // The same finite bounds remain representable in the segmented backend. This also
        // exercises retirement after successful initial publication and after a bad update.
        settings.broadPhase = BroadPhaseMode::Segmented;
        const auto accepted = executeSpatialReplay(trace, settings, true);
        require(accepted.measuredSamples.size() == 1,
                "Segmented replay tears down large finite endpoint memberships safely");
    }

    std::istringstream input(collisionTrace());
    const auto trace = parseSpatialTrace(input);
    for (const auto backend : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
        for (const bool failAtInitial : {true, false})
        {
            SpatialSettings settings;
            settings.broadPhase = backend;
            ResourceObserverFailure failure{failAtInitial};
            bool preserved = false;
            try
            {
                (void)executeSpatialReplay(trace, settings, true, &throwFromResourceObserver, &failure);
            }
            catch (const std::runtime_error &error)
            {
                preserved = std::string(error.what()) == (failAtInitial
                    ? "injected initial resource observer failure"
                    : "injected step resource observer failure");
            }
            require(preserved && failure.calls == (failAtInitial ? 1u : 2u),
                    "Replay cleanup preserves observer exceptions before or after step publication");
        }
}

void stationaryFilterChanges()
{
    const auto initial = "{\"type\":\"initial\",\"colliders\":["
        "{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"bounds\":[0,0,2,2],\"group\":\"9223372036854775808\",\"mask\":\"1\",\"active\":true},"
        "{\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"bounds\":[0,0,2,2],\"group\":\"1\",\"mask\":\"9223372036854775808\",\"active\":true}]}\n";
    const auto steps = "{\"type\":\"step\",\"index\":0,\"ops\":[{\"op\":\"set_filter\",\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"group\":\"1\",\"mask\":\"0\"}]}\n"
        "{\"type\":\"step\",\"index\":1,\"ops\":[{\"op\":\"set_filter\",\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"group\":\"1\",\"mask\":\"9223372036854775808\"}]}\n";
    std::istringstream input(header("collision", "stationary-filters", 0, 2, "\"population\":2,\"bounds_source\":\"provided-aabb\"") + initial + steps);
    const auto trace = parseSpatialTrace(input);
    for (auto mode : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
    {
        SpatialSettings settings; settings.broadPhase = mode;
        const auto run = executeSpatialReplay(trace, settings, true);
        require(run.initialOutput.size()==1 && run.measuredSamples[0].output.empty() && run.measuredSamples[1].output.size()==1,
                "Stationary high-bit masks reject and restore the production candidate");
        require(run.measuredSamples[0].counters.pairRemoves==1 && run.measuredSamples[1].counters.pairAdds==1,
                "Filter-only publication emits exact logical transitions");
    }
}

void validatesCollisionReplayChurn()
{
    const auto traceText = header("collision", "collision-churn", 0, 2,
                                  "\"population\":2,\"bounds_source\":\"provided-aabb\"") +
        "{\"type\":\"initial\",\"colliders\":["
        "{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true},"
        "{\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}]}\n"
        "{\"type\":\"step\",\"index\":0,\"ops\":["
        "{\"op\":\"remove\",\"id\":{\"slot\":\"2\",\"generation\":\"1\"}},"
        "{\"op\":\"add\",\"collider\":{\"id\":{\"slot\":\"2\",\"generation\":\"2\"},"
        "\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}}]}\n"
        "{\"type\":\"step\",\"index\":1,\"ops\":["
        "{\"op\":\"remove\",\"id\":{\"slot\":\"2\",\"generation\":\"2\"}}]}\n";
    std::istringstream input(traceText);
    const auto trace = parseSpatialTrace(input);
    for (const auto mode : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
    {
        SpatialSettings settings;
        settings.broadPhase = mode;
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::work = {};
#endif
        const auto execution = executeSpatialReplay(trace, settings, true);
#if PLATFORMATOR_ENABLE_WORK_PROBES
        require(platformator_test::work.bvhQualityScans == 0, "Replay gauges never traverse tree quality");
#endif
        require(execution.initialOutput == std::vector<std::string>{"1:1/2:1"} &&
                    execution.measuredSamples.size() == 2 &&
                    execution.measuredSamples[0].output == std::vector<std::string>{"1:1/2:2"} &&
                    execution.measuredSamples[0].counters.pairAdds == 1 &&
                    execution.measuredSamples[0].counters.pairRemoves == 1 &&
                    execution.measuredSamples[1].output.empty() &&
                    execution.measuredSamples[1].counters.pairRemoves == 1,
                "Both production paths preserve pair state through same-step retirement and generation reuse");
    }
}

void validatesClusteredBatchTransitionAccounting()
{
    constexpr int count = 64;
    std::ostringstream initial;
    initial << "{\"type\":\"initial\",\"colliders\":[";
    std::ostringstream operations;
    operations << "{\"type\":\"step\",\"index\":0,\"ops\":[";
    for (int slot = 0; slot < count; ++slot)
    {
        if (slot != 0)
        {
            initial << ',';
            operations << ',';
        }
        const int x = (slot % 16) * 4;
        const int y = (slot % 16) * 3;
        const int shift = slot % 2 == 0 ? 2 : -2;
        initial << "{\"id\":{\"slot\":\"" << slot + 1
                << "\",\"generation\":\"1\"},\"bounds\":[" << x << ',' << y << ',' << x + 12 << ','
                << y + 12 << "],\"group\":\"1\",\"mask\":\"18446744073709551615\",\"active\":true}";
        operations << "{\"op\":\"update_bounds\",\"id\":{\"slot\":\"" << slot + 1
                   << "\",\"generation\":\"1\"},\"bounds\":[" << x + shift << ',' << y << ','
                   << x + shift + 12 << ',' << y + 12 << "]}";
    }
    initial << "]}";
    operations << "]}";
    std::istringstream stream(header("collision", "clustered-batch", 0, 1,
                                     "\"population\":64,\"bounds_source\":\"provided-aabb\"") +
                              initial.str() + '\n' + operations.str() + '\n');
    const auto trace = parseSpatialTrace(stream);

    for (const auto backend : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
    for (const auto executionMode : {BvhQueryExecution::Serial, BvhQueryExecution::Parallel})
    {
    SpatialSettings settings; settings.broadPhase = backend; settings.bvhQueryExecution = executionMode;
    const auto execution = executeSpatialReplay(trace, settings, true);
    require(execution.measuredSamples.size() == 1,
            "Clustered batch validation produces its complete replay sample");
    const auto &sample = execution.measuredSamples.front();
    const auto &c = sample.counters;
    require(c.boundsPrepareNanoseconds + c.indexUpdateNanoseconds + c.indexMaintenanceNanoseconds +
                c.indexQueryNanoseconds + c.resultReduceNanoseconds <= sample.durationNanoseconds,
            "Replay sibling phase wall durations fit their enclosing update, including parallel queries");
    const std::set<std::string> before(execution.initialOutput.begin(), execution.initialOutput.end());
    const std::set<std::string> after(sample.output.begin(), sample.output.end());
    size_t netAdds = 0, netRemoves = 0;
    for (const auto &pair : after)
        netAdds += !before.contains(pair);
    for (const auto &pair : before)
        netRemoves += !after.contains(pair);
    require(sample.counters.pairAdds == netAdds && sample.counters.pairRemoves == netRemoves,
            "Clustered publication emits exactly the net pair transitions");
    }
}

void productionCapacityGrowthAndRetention()
{
    using Json = nlohmann::json;
    const auto id = [](int i) {return Json{{"slot",std::to_string(i)}, {"generation","1"}};};
    const auto endpoint = [&](int i) {return Json{{"id",id(i)}, {"bounds",{i*128,0,i*128+4,4}},
        {"group","1"},{"mask","1"},{"active",true}};};
    Json add = Json::array(), remove = Json::array();
    for (int i=2; i<130; ++i)
    {
        add.push_back({{"op","add"},{"collider",endpoint(i)}});
        remove.push_back({{"op","remove"},{"id",id(i)}});
    }
    std::istringstream input(header("collision", "capacity-growth", 0, 2,
        "\"population\":1,\"bounds_source\":\"provided-aabb\"") +
        Json{{"type","initial"},{"colliders",Json::array({endpoint(1)})}}.dump()+'\n'+
        Json{{"type","step"},{"index",0},{"ops",add}}.dump()+'\n'+
        Json{{"type","step"},{"index",1},{"ops",remove}}.dump()+'\n');
    const auto trace = parseSpatialTrace(input);
    for (auto backend : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
    {
        SpatialSettings settings; settings.broadPhase=backend;
        std::vector<SpatialReplayOwnedCapacitySnapshot> snapshots;
        executeSpatialReplay(trace, settings, true,
            [](const SpatialReplayOwnedCapacitySnapshot &snapshot, bool, void *context) {
                static_cast<std::vector<SpatialReplayOwnedCapacitySnapshot> *>(context)->push_back(snapshot);
            }, &snapshots);
        require(snapshots.size()==3 && snapshots[1].physicsIndexBytes > snapshots[0].physicsIndexBytes + 128*sizeof(SpatialBounds),
                "Actual selected index capacity includes grown record/cell/projection storage");
        require(snapshots[2].physicsIndexBytes > snapshots[0].physicsIndexBytes,
                "Capacity retains reusable high-water pools and buckets after endpoint retirement");
    }
}

void randomizedProductionCollisionBatches()
{
    using Json = nlohmann::json;
    std::mt19937 rng(731049);
    const auto identity = [](int i, int generation = 1) {
        return Json{{"slot", std::to_string(i + 1)}, {"generation", std::to_string(generation)}};
    };
    const auto bounds = [&]() {
        int x = int(rng() % 160) - 80, y = int(rng() % 160) - 80;
        int w = rng() % 3 == 0 ? 0 : int(rng() % 65), h = int(rng() % 3 == 0 ? 0 : rng() % 65);
        return Json::array({x, y, x+w, y+h});
    };
    Json colliders = Json::array();
    for (int i=0; i<64; ++i)
        colliders.push_back({{"id", identity(i)}, {"bounds", bounds()}, {"group", "18446744073709551615"},
                             {"mask", "18446744073709551615"}, {"active", true}});
    std::string input = header("collision", "production-random", 0, 80,
                                "\"population\":64,\"bounds_source\":\"provided-aabb\"") +
                        Json{{"type","initial"},{"colliders",colliders}}.dump() + '\n';
    int generations[64]; std::fill_n(generations, 64, 1);
    for (int step=0; step<80; ++step)
    {
        Json ops = Json::array();
        for (int j=0; j<16; ++j)
        {
            int slot = rng()%64;
            const auto id = identity(slot, generations[slot]);
            switch (rng()%4)
            {
            case 0: ops.push_back({{"op","update_bounds"},{"id",id},{"bounds",bounds()}}); break;
            case 1: ops.push_back({{"op","set_filter"},{"id",id},{"group",std::to_string(uint64_t(1)<<(rng()%64))},
                                   {"mask", rng()%2 ? "18446744073709551615" : "0"}}); break;
            case 2: ops.push_back({{"op","set_active"},{"id",id},{"active", bool(rng()%2)}}); break;
            case 3:
                ops.push_back({{"op","remove"},{"id",id}});
                ops.push_back({{"op","add"},{"collider",{{"id",identity(slot,++generations[slot])},
                    {"bounds",bounds()},{"group","18446744073709551615"},
                    {"mask","18446744073709551615"},{"active",true}}}}); break;
            }
        }
        input += Json{{"type","step"},{"index",step},{"ops",ops}}.dump() + '\n';
    }
    std::istringstream stream(input);
    const auto trace = parseSpatialTrace(stream);
    std::vector<std::vector<std::string>> reference;
    for (size_t workers : {size_t(1), size_t(6)})
    {
        tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
        for (auto backend : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
        {
            SpatialSettings settings; settings.broadPhase = backend; settings.bvhPadding=8;
            settings.bvhQueryExecution = BvhQueryExecution::Parallel;
            const auto run = executeSpatialReplay(trace, settings, true);
            if (reference.empty())
                for (const auto &sample : run.measuredSamples) reference.push_back(sample.output);
            else for (size_t i=0; i<reference.size(); ++i)
                require(reference[i] == run.measuredSamples[i].output,
                        "Actual production backends and worker counts match independent randomized snapshots");
        }
    }
}

void visibilityReplayIncrementalWork()
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    const auto input = header("visibility", "incremental-cache", 0, 4,
                              "\"population\":1,\"bounds_source\":\"sprite-transform\",\"renderer\":\"none-replay\"") +
        "{\"type\":\"initial\",\"sprites\":[{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"center\":[5,5],\"dimensions\":[2,2],\"scale\":[1,1],\"angle_degrees\":0,\"active\":true}],\"view\":{\"id\":{\"slot\":\"99\",\"generation\":\"1\"},\"position\":[0,0],\"size\":[10,10],\"output\":[100,100]}}\n" +
        "{\"type\":\"step\",\"index\":0,\"ops\":[]}\n"
        "{\"type\":\"step\",\"index\":1,\"ops\":[]}\n"
        "{\"type\":\"step\",\"index\":2,\"ops\":[]}\n"
        "{\"type\":\"step\",\"index\":3,\"ops\":[]}\n";
    std::istringstream stream(input);
    const auto trace = parseSpatialTrace(stream);
    for (const auto mode : {RenderCullingMode::Linear, RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented})
    {
        SpatialSettings settings;
        settings.renderCulling = mode;
        platformator_test::work = {};
        const auto run = executeSpatialReplay(trace, settings, true);
        require(run.measuredSamples.size() == 4 && run.measuredSamples.back().counters.visibilityCacheHits == 1,
                "Static replay retains current ordered output");
        require(platformator_test::work.renderSnapshotVisits <= 1,
                "Cached replay must not rebuild or diff full population snapshots");
    }
#endif
}

void validatesVisibilityTrace()
{
    std::istringstream stream(visibilityTrace());
    const auto trace = parseSpatialTrace(stream);
    require(trace.header.domain == SpatialTraceDomain::Visibility, "Visibility domain is retained");
    require(trace.visibilityInitial.size() == 1 && trace.visibilityView.outputWidth == 100,
            "Visibility transforms and output view are retained");
    require(trace.steps[0].operations[0].at("op") == "set_view", "Visibility operation is retained");
    for (const auto mode : {RenderCullingMode::None, RenderCullingMode::Linear, RenderCullingMode::DynamicBvh,
                            RenderCullingMode::Segmented})
    {
        SpatialSettings settings;
        settings.renderCulling = mode;
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::work = {};
#endif
        const auto execution = executeSpatialReplay(trace, settings, true);
#if PLATFORMATOR_ENABLE_WORK_PROBES
        require(platformator_test::work.bvhQualityScans == 0, "Replay gauges never traverse tree quality");
#endif
        require(execution.initialOutput == std::vector<std::string>{"1:1"} &&
                    execution.measuredSamples.size() == 1 &&
                    execution.measuredSamples[0].output == std::vector<std::string>{"1:1"},
                "All visibility replay modes retain the current ordered viewport result");
    }
}

void validatesVisibilityReplayCacheAndSwapRemoval()
{
    const auto traceText = header("visibility", "visibility-order-cache", 0, 4,
                                  "\"population\":3,\"bounds_source\":\"sprite-transform\",\"renderer\":\"none-replay\"") +
        "{\"type\":\"initial\",\"sprites\":["
        "{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"center\":[3,3],\"dimensions\":[2,2],\"scale\":[1,1],\"angle_degrees\":0,\"active\":true},"
        "{\"id\":{\"slot\":\"2\",\"generation\":\"1\"},\"center\":[6,3],\"dimensions\":[2,2],\"scale\":[1,1],\"angle_degrees\":0,\"active\":true},"
        "{\"id\":{\"slot\":\"3\",\"generation\":\"1\"},\"center\":[100,100],\"dimensions\":[2,2],\"scale\":[1,1],\"angle_degrees\":0,\"active\":false}],"
        "\"view\":{\"id\":{\"slot\":\"99\",\"generation\":\"1\"},\"position\":[0,0],\"size\":[10,10],\"output\":[100,100]}}\n"
        "{\"type\":\"step\",\"index\":0,\"ops\":[]}\n"
        "{\"type\":\"step\",\"index\":1,\"ops\":[{\"op\":\"set_active\",\"id\":{\"slot\":\"3\",\"generation\":\"1\"},\"active\":true}]}\n"
        "{\"type\":\"step\",\"index\":2,\"ops\":[{\"op\":\"set_active\",\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"active\":false}]}\n"
        "{\"type\":\"step\",\"index\":3,\"ops\":[{\"op\":\"set_active\",\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"active\":true}]}\n";
    std::istringstream input(traceText);
    const auto trace = parseSpatialTrace(input);
    for (const auto mode : {RenderCullingMode::None, RenderCullingMode::Linear,
                            RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented})
    {
        SpatialSettings settings;
        settings.renderCulling = mode;
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::work = {};
#endif
        const auto execution = executeSpatialReplay(trace, settings, true);
#if PLATFORMATOR_ENABLE_WORK_PROBES
        require(platformator_test::work.bvhQualityScans == 0, "Replay gauges never traverse tree quality");
#endif
        require(execution.initialOutput == std::vector<std::string>{"1:1", "2:1"} &&
                    execution.measuredSamples.size() == 4,
                "Visibility replay starts with ordered in-view sprites");
        if (mode == RenderCullingMode::None)
        {
            require(execution.measuredSamples[0].output == std::vector<std::string>{"1:1", "2:1"} &&
                        execution.measuredSamples[1].output == std::vector<std::string>{"1:1", "2:1", "3:1"} &&
                        execution.measuredSamples[2].output == std::vector<std::string>{"3:1", "2:1"} &&
                        execution.measuredSamples[3].output == std::vector<std::string>{"3:1", "2:1", "1:1"},
                    "None replay preserves the direct append/swap-removal/reactivation control path");
        }
        else
        {
            require(execution.measuredSamples[0].output == std::vector<std::string>{"1:1", "2:1"} &&
                        execution.measuredSamples[0].counters.visibilityCacheHits == 1 &&
                        execution.measuredSamples[1].output == std::vector<std::string>{"1:1", "2:1"} &&
                        execution.measuredSamples[2].output == std::vector<std::string>{"2:1"} &&
                        execution.measuredSamples[3].output == std::vector<std::string>{"2:1", "1:1"},
                    "Indexed visibility replay reuses unchanged results and tracks swap-removal/reactivation order");
        }
    }
}

void validatesLargeSegmentedVisibilityLifetime()
{
    using Json = nlohmann::json;
    Json headerValue = {{"type", "header"}, {"format", "platformator-spatial-trace"}, {"version", 1},
                        {"domain", "visibility"}, {"case_id", "large-segmented"}, {"seed", 0},
                        {"units", "world"}, {"dt", 1.0 / 120.0}, {"warmup_steps", 0}, {"measure_steps", 2},
                        {"generator", {{"name", "test"}, {"revision", "1"}, {"source_hash", std::string(64, 'b')}}},
                        {"parameters", {{"population", 1000}, {"bounds_source", "sprite-transform"},
                                         {"renderer", "none-replay"}}}};
    Json sprites = Json::array();
    for (uint64_t index = 0; index < 1000; ++index)
    {
        const bool visible = index < 100;
        const double x = visible ? 8.0 + static_cast<double>(index % 16) * 90.0
                                 : 100000.0 + static_cast<double>(index) * 12.0;
        const double y = visible ? 8.0 + static_cast<double>(index / 16) * 8.0 : 100000.0;
        sprites.push_back({{"id", {{"slot", std::to_string(index + 1)}, {"generation", "1"}}},
                           {"center", {x, y}}, {"dimensions", {6.0, 6.0}}, {"scale", {1.0, 1.0}},
                           {"angle_degrees", 0.0}, {"active", true}});
    }
    Json initial = {{"type", "initial"}, {"sprites", std::move(sprites)},
                    {"view", {{"id", {{"slot", "9001"}, {"generation", "1"}}}, {"position", {0.0, 0.0}},
                              {"size", {1600.0, 900.0}}, {"output", {1600, 900}}}}};
    const auto step = [](int index) { return Json{{"type", "step"}, {"index", index}, {"ops", Json::array()}}; };
    std::ostringstream stream;
    stream << headerValue << '\n' << initial << '\n' << step(0) << '\n' << step(1) << '\n';
    std::istringstream input(stream.str());
    const auto trace = parseSpatialTrace(input);
    SpatialSettings settings;
    settings.renderCulling = RenderCullingMode::Segmented;
    const auto execution = executeSpatialReplay(trace, settings, true);
    require(execution.initialOutput.size() == 100 && execution.measuredSamples.size() == 2 &&
                execution.measuredSamples.front().output == execution.initialOutput,
            "Large segmented visibility runs preserve results and retire context endpoints safely");
}

void rejectsMalformedAndStaleInputs()
{
    auto badVersion = collisionTrace();
    badVersion.replace(badVersion.find("\"version\":1"), 11, "\"version\":2");
    rejects(badVersion, [](auto &stream) { (void)parseSpatialTrace(stream); }, "Unknown trace versions must fail");

    auto unknownField = collisionTrace();
    unknownField.insert(unknownField.find("\"bounds\":["), "\"hidden\":true,");
    rejects(unknownField, [](auto &stream) { (void)parseSpatialTrace(stream); }, "Unknown operation fields must fail");

    auto unorderedBounds = collisionTrace();
    const std::string validBounds = "\"bounds\":[0,0,1,1]";
    unorderedBounds.replace(unorderedBounds.find(validBounds), validBounds.size(), "\"bounds\":[1,0,0,1]");
    rejects(unorderedBounds, [](auto &stream) { (void)parseSpatialTrace(stream); },
            "Non-ordered exact bounds must be rejected before replay");

    auto nonfiniteBounds = collisionTrace();
    nonfiniteBounds.replace(nonfiniteBounds.find(validBounds), validBounds.size(), "\"bounds\":[0,0,NaN,1]");
    rejects(nonfiniteBounds, [](auto &stream) { (void)parseSpatialTrace(stream); },
            "Nonfinite exact bounds must be rejected before replay");

    auto duplicateInitial = header("collision", "duplicate", 0, 1,
                                   "\"population\":2,\"bounds_source\":\"provided-aabb\"") +
        "{\"type\":\"initial\",\"colliders\":["
        "{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true},"
        "{\"id\":{\"slot\":\"1\",\"generation\":\"1\"},\"bounds\":[2,0,3,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}]}\n"
        "{\"type\":\"step\",\"index\":0,\"ops\":[]}\n";
    rejects(duplicateInitial, [](auto &stream) { (void)parseSpatialTrace(stream); },
            "Duplicate live endpoint identities must be rejected");

    auto staleGeneration = header("collision", "stale", 0, 2,
                                  "\"population\":1,\"bounds_source\":\"provided-aabb\"") +
        "{\"type\":\"initial\",\"colliders\":[{\"id\":{\"slot\":\"1\",\"generation\":\"2\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}]}\n"
        "{\"type\":\"step\",\"index\":0,\"ops\":[{\"op\":\"remove\",\"id\":{\"slot\":\"1\",\"generation\":\"2\"}}]}\n"
        "{\"type\":\"step\",\"index\":1,\"ops\":[{\"op\":\"add\",\"collider\":{\"id\":{\"slot\":\"1\",\"generation\":\"2\"},\"bounds\":[0,0,1,1],\"group\":\"1\",\"mask\":\"1\",\"active\":true}}]}\n";
    rejects(staleGeneration, [](auto &stream) { (void)parseSpatialTrace(stream); },
            "A reused slot must advance its generation");

    auto outOfOrder = collisionTrace();
    const auto step = outOfOrder.find("\"index\":1");
    outOfOrder.replace(step, 9, "\"index\":7");
    rejects(outOfOrder, [](auto &stream) { (void)parseSpatialTrace(stream); }, "Step indexes must be contiguous");

    auto trailingRecord = collisionTrace() + "{\"type\":\"extra\"}\n";
    rejects(trailingRecord, [](auto &stream) { (void)parseSpatialTrace(stream); },
            "Trace must contain exactly the declared steps");
}
} // namespace

int main()
{
    try
    {
        validatesCollisionTrace();
        collisionReplayExceptionTeardown();
        stationaryFilterChanges();
        randomizedProductionCollisionBatches();
        productionCapacityGrowthAndRetention();
        validatesCollisionReplayChurn();
        validatesClusteredBatchTransitionAccounting();
        visibilityReplayIncrementalWork();
        validatesVisibilityTrace();
        validatesVisibilityReplayCacheAndSwapRemoval();
        validatesLargeSegmentedVisibilityLifetime();
        rejectsMalformedAndStaleInputs();
        std::cout << "[PASS] spatial replay input contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
