#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "spatialsettingsparse.h"
#include "test_support.h"

#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/runtimeoptions.h>

#include <iostream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

using namespace test_support;

namespace
{
static_assert(std::is_const_v<std::remove_reference_t<decltype(
              std::declval<const platformator::Runtime &>().getSpatialSettings())>>);

RuntimeOptions parse(std::vector<std::string> arguments)
{
    std::vector<char *> argv;
    argv.reserve(arguments.size());
    for (auto &argument : arguments)
        argv.push_back(argument.data());
    return parseRuntimeOptions(static_cast<int>(argv.size()), argv.data(), "default.scene");
}

const char *broadPhaseToken(BroadPhaseMode mode)
{
    return mode == BroadPhaseMode::DynamicBvh ? "bvh" : "segmented";
}

const char *renderCullingToken(RenderCullingMode mode)
{
    switch (mode)
    {
    case RenderCullingMode::None: return "none";
    case RenderCullingMode::Linear: return "linear";
    case RenderCullingMode::DynamicBvh: return "bvh";
    case RenderCullingMode::Segmented: return "segmented";
    }
    return "invalid";
}

void requireThrows(const std::function<void()> &action, const std::string &contains)
{
    try
    {
        action();
    }
    catch (const std::exception &error)
    {
        require(contains.empty() || std::string(error.what()).find(contains) != std::string::npos,
                "Invalid spatial setting has a useful diagnostic: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Invalid spatial setting was accepted");
}

void parserDefaultsAndCompatibility()
{
    const RuntimeOptions legacyAggregate{"legacy.scene", {}, {}, "assets", 9};
    require(legacyAggregate.sceneFilePath == "legacy.scene" && legacyAggregate.assetRoot == "assets" &&
                legacyAggregate.smokeSteps == 9 &&
                legacyAggregate.spatialSettings.broadPhase == BroadPhaseMode::Segmented &&
                legacyAggregate.spatialSettings.renderCulling == RenderCullingMode::None,
            "Appending SpatialSettings preserves legacy RuntimeOptions aggregate initialization and defaults");

    const auto defaults = parse({"platformator"});
    require(defaults.spatialSettings.broadPhase == BroadPhaseMode::Segmented &&
                defaults.spatialSettings.renderCulling == RenderCullingMode::None &&
                defaults.spatialSettings.bvhMaintenance == BvhMaintenance::NativePartial &&
                defaults.spatialSettings.bvhPadding == 0.0f &&
                defaults.spatialSettings.bvhQueryExecution == BvhQueryExecution::Parallel,
            "Shared runner parser keeps the documented segmented/none/native-partial/zero/parallel defaults");

    const auto selected = parse({"platformator", "--broad-phase", "bvh", "--render-culling", "segmented",
                                 "--bvh-maintenance", "full-rebuild", "--bvh-padding", "1.25",
                                 "--bvh-query-execution", "serial"});
    require(selected.spatialSettings.broadPhase == BroadPhaseMode::DynamicBvh &&
                selected.spatialSettings.renderCulling == RenderCullingMode::Segmented &&
                selected.spatialSettings.bvhMaintenance == BvhMaintenance::FullRebuild &&
                selected.spatialSettings.bvhPadding == 1.25f &&
                selected.spatialSettings.bvhQueryExecution == BvhQueryExecution::Serial,
            "Runtime parser applies every spatial CLI selection");

    for (const auto broadPhase : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
        for (const auto renderMode : {RenderCullingMode::None, RenderCullingMode::Linear,
                                      RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented})
        {
            const auto combination = parse({"platformator", "--broad-phase", broadPhaseToken(broadPhase),
                                            "--render-culling", renderCullingToken(renderMode)});
            require(combination.spatialSettings.broadPhase == broadPhase &&
                        combination.spatialSettings.renderCulling == renderMode,
                    "Shared runner preserves each of the eight independent physics/render selections");
        }
}

void benchmarkParserContract()
{
    SpatialSettings settings{};
    for (const auto &[name, value] : std::vector<std::pair<std::string, std::string>>{
             {"--broad-phase", "bvh"}, {"--render-culling", "linear"},
             {"--bvh-maintenance", "native-partial"}, {"--bvh-padding", "0.5"},
             {"--bvh-query-execution", "parallel"}})
    {
        require(platformator_detail::isSpatialSettingsOption(name),
                "Benchmark runner recognizes each spatial option");
        platformator_detail::parseSpatialSettingsOption(name, value, settings);
    }
    require(settings.broadPhase == BroadPhaseMode::DynamicBvh && settings.renderCulling == RenderCullingMode::Linear &&
                settings.bvhMaintenance == BvhMaintenance::NativePartial && settings.bvhPadding == 0.5f &&
                settings.bvhQueryExecution == BvhQueryExecution::Parallel,
            "Benchmark runner's parser uses the same tokens and values as the shared runtime parser");

    for (const auto broadPhase : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
        for (const auto renderMode : {RenderCullingMode::None, RenderCullingMode::Linear,
                                      RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented})
        {
            SpatialSettings combination;
            platformator_detail::parseSpatialSettingsOption("--broad-phase", broadPhaseToken(broadPhase), combination);
            platformator_detail::parseSpatialSettingsOption("--render-culling", renderCullingToken(renderMode), combination);
            require(combination.broadPhase == broadPhase && combination.renderCulling == renderMode,
                    "Benchmark runner preserves each of the eight independent physics/render selections");
        }

    for (const auto &[name, value, message] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"--broad-phase", "grid", "segmented or bvh"},
             {"--render-culling", "tree", "none, linear, bvh or segmented"},
             {"--bvh-maintenance", "full", "native-partial or full-rebuild"},
             {"--bvh-query-execution", "auto", "serial or parallel"},
             {"--bvh-padding", "nan", "finite and nonnegative"},
             {"--bvh-padding", "-1", "finite and nonnegative"},
             {"--bvh-padding", "1.2units", "finite and nonnegative"}})
        requireThrows([&] { platformator_detail::parseSpatialSettingsOption(name, value, settings); }, message);
}

void invalidSettingsAndMissingArguments()
{
    for (const auto &arguments : std::vector<std::vector<std::string>>{
             {"platformator", "--broad-phase"}, {"platformator", "--render-culling"},
             {"platformator", "--bvh-maintenance"}, {"platformator", "--bvh-padding"},
             {"platformator", "--bvh-query-execution"}, {"platformator", "--unrecognized"}})
        requireThrows([&] { (void)parse(arguments); }, "");

    for (const auto &[field, invalid] : std::vector<std::pair<int, int>>{{0, 99}, {1, 99}, {2, 99}, {3, 99}})
    {
        RuntimeOptions options;
        if (field == 0)
            options.spatialSettings.broadPhase = static_cast<BroadPhaseMode>(invalid);
        else if (field == 1)
            options.spatialSettings.renderCulling = static_cast<RenderCullingMode>(invalid);
        else if (field == 2)
            options.spatialSettings.bvhMaintenance = static_cast<BvhMaintenance>(invalid);
        else
            options.spatialSettings.bvhQueryExecution = static_cast<BvhQueryExecution>(invalid);
        requireThrows([&] { platformator::Runtime runtime(options); }, "Invalid spatial setting");
    }
    for (float padding : {-0.01f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()})
    {
        RuntimeOptions options;
        options.spatialSettings.bvhPadding = padding;
        requireThrows([&] { platformator::Runtime runtime(options); }, "bvhPadding");
    }
}

void selectedRuntimeMembership(BroadPhaseMode mode)
{
    RuntimeOptions options = physicsOnly();
    options.spatialSettings.broadPhase = mode;
    options.spatialSettings.renderCulling = RenderCullingMode::DynamicBvh;
    platformator::Runtime runtime(options);
    options.spatialSettings.broadPhase = BroadPhaseMode::Segmented;
    require(runtime.getRenderer() == nullptr,
            "Selecting render culling does not create an SDL renderer for a headless runtime");
    require(runtime.getSpatialSettings().broadPhase == mode &&
                runtime.getSpatialSettings().renderCulling == RenderCullingMode::DynamicBvh,
            "Runtime owns an immutable copy of requested spatial settings");

    auto *object = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    runtime.simulateFrame(dt);
    auto *collider = object->getComponent<BoxCollider>();
    require(collider->getIsRegisteredInBroadPhase(), "Active collider reports selected broad-phase membership");
    require(collider->getIsRegisteredInGrid() == (mode == BroadPhaseMode::Segmented),
            "The literal Grid diagnostic agrees with the selected backend");
}

void allEightRuntimeSelections()
{
    for (const auto broadPhase : {BroadPhaseMode::Segmented, BroadPhaseMode::DynamicBvh})
        for (const auto renderMode : {RenderCullingMode::None, RenderCullingMode::Linear,
                                      RenderCullingMode::DynamicBvh, RenderCullingMode::Segmented})
        {
            RuntimeOptions options = physicsOnly();
            options.spatialSettings.broadPhase = broadPhase;
            options.spatialSettings.renderCulling = renderMode;
            platformator::Runtime runtime(options);
            auto *object = runtime.createGameObject()->addComponent<BoxCollider>(20.0f, 20.0f);
            runtime.createGameObject()->addComponent<Camera>(320.0f, 240.0f);
            runtime.simulateFrame(dt);
            auto *collider = object->getComponent<BoxCollider>();
            require(runtime.getRenderer() == nullptr && runtime.getSpatialSettings().broadPhase == broadPhase &&
                        runtime.getSpatialSettings().renderCulling == renderMode &&
                        collider->getIsRegisteredInBroadPhase() &&
                        collider->getIsRegisteredInGrid() == (broadPhase == BroadPhaseMode::Segmented),
                    "All eight SDK choices retain requested physics membership without activating SDL rendering");
        }
}

void unsupportedBvhBoundsFail()
{
    RuntimeOptions options = physicsOnly();
    options.spatialSettings.broadPhase = BroadPhaseMode::DynamicBvh;
    std::string diagnostic;
    try
    {
        platformator::Runtime runtime(options);
        runtime.createGameObject()->addComponent<BoxCollider>(100000.0f, 2.0f);
        runtime.simulateFrame(dt);
    }
    catch (const std::exception &error)
    {
        diagnostic = error.what();
    }
    require(diagnostic.find("BVH") != std::string::npos &&
                (diagnostic.find("extent") != std::string::npos || diagnostic.find("bounds") != std::string::npos),
            "A physics proxy outside supported BVH bounds fails with backend and reason");
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        parserDefaultsAndCompatibility();
        benchmarkParserContract();
        invalidSettingsAndMissingArguments();
        selectedRuntimeMembership(BroadPhaseMode::Segmented);
        selectedRuntimeMembership(BroadPhaseMode::DynamicBvh);
        allEightRuntimeSelections();
        unsupportedBvhBoundsFail();
        std::cout << "Spatial selection contract passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
