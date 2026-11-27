#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <charconv>
#include <thread>
#include <oneapi/tbb/global_control.h>
#include <unordered_map>
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "rendering/sdlwindow.h"
#include "physics/physicsmanager.h"

#include "benchmark/benchmark.h"
#include "core/constants.h"
#include "platformator/boxcollider.h"
#include "platformator/scriptcomponent.h"
#include "platformator/camera.h"
#include "platformator/circlecollider.h"
#include "platformator/rigidbody.h"
#include "platformator/runtime.h"
#include "platformator/sprite.h"

namespace
{
#include "benchmark/benchmark_scenarios.h"

BenchmarkRunnerOptions parseBenchmarkRunnerOptions(int argc, char *args[])
{
    BenchmarkRunnerOptions options;
    bool scenePathSpecified = false;
    std::optional<int> workers, warmupFrames, measureFrames;
    std::optional<double> timeDelta;

    for (int index = 1; index < argc; index++)
    {
        const std::string argument = args[index];
        if (argument == "--contact-pair-count")
        {
            options.contactPairCount =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--scripted-endpoints")
        {
            options.scriptedEndpoints =
                parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            if (options.scriptedEndpoints > 2)
                throw std::runtime_error("--scripted-endpoints must be 0, 1 or 2");
            continue;
        }
        if (argument == "--validate-invariants")
        {
            options.validateInvariants = true;
            continue;
        }
        if (argument == "--no-render-culling")
        {
            options.runtimeOptions.windowSettings.enableRenderCulling = false;
            continue;
        }
        if (argument == "--repetitions")
        {
            options.repetitions = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--run-index")
        {
            options.runIndex = parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--workers")
        {
            workers = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--seed")
        {
            const auto value = requireArgumentValue(argc, args, index, argument);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.seed);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                throw std::runtime_error("--seed must be a nonnegative integer");
            continue;
        }
        if (argument == "--asset-root")
        {
            options.runtimeOptions.assetRoot = requireArgumentValue(argc, args, index, argument);
            continue;
        }

        if (argument == "--scene")
        {
            if (options.scenario != BenchmarkScenario::None)
            {
                throw std::runtime_error("--scene cannot be combined with --scenario.");
            }

            options.runtimeOptions.sceneFilePath = requireArgumentValue(argc, args, index, argument);
            scenePathSpecified = true;
            continue;
        }

        if (argument == "--scenario")
        {
            if (scenePathSpecified)
            {
                throw std::runtime_error("--scenario cannot be combined with --scene.");
            }

            options.scenario = parseScenario(requireArgumentValue(argc, args, index, argument));
            options.runtimeOptions.sceneFilePath.clear();
            continue;
        }

        if (argument == "--warmup-frames")
        {
            warmupFrames =
                parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--measure-frames")
        {
            measureFrames =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--dt")
        {
            timeDelta = parsePositiveDouble(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--box-count")
        {
            options.boxCount = parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--circle-count")
        {
            options.circleCount = parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--broad-count")
        {
            options.broadPhaseElementCount =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--narrow-count")
        {
            options.narrowPhaseElementCount =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--ideal-count")
        {
            options.idealWorldBodyCount =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--steady-count")
        {
            options.addRemoveSteadyCount =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--add-count")
        {
            options.addRemoveAddCount =
                parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--remove-count")
        {
            options.addRemoveRemoveCount =
                parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }

        if (argument == "--churn-count")
        {
            const int churnCount = parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            options.addRemoveAddCount = churnCount;
            options.addRemoveRemoveCount = churnCount;
            continue;
        }

        if (argument == "--csv-output" || argument == "--csv")
        {
            options.csvOutputPath = requireArgumentValue(argc, args, index, argument);
            continue;
        }

        if (argument == "--debug-draw")
        {
            std::string categories = requireArgumentValue(argc, args, index, argument);
            char arg0[] = "platformator_benchmark_runner";
            char arg1[] = "--debug-draw";
            char *debugArgs[] = {arg0, arg1, categories.data()};
            options.runtimeOptions.debugSettings = parseRuntimeOptions(3, debugArgs, "").debugSettings;
            continue;
        }

        if (argument == "--render")
        {
            options.renderFrames = true;
            options.runtimeOptions.windowSettings.headless = false;
            options.runtimeOptions.windowSettings.width = 1600;
            options.runtimeOptions.windowSettings.height = 900;
            options.runtimeOptions.windowSettings.keepAspectRatio = true;
            continue;
        }

        if (!argument.starts_with("--") && !scenePathSpecified && options.scenario == BenchmarkScenario::None)
        {
            options.runtimeOptions.sceneFilePath = argument;
            scenePathSpecified = true;
            continue;
        }

        throw std::runtime_error("Unknown benchmark argument: " + argument);
    }

    if (findBox2DScenario(options.scenario))
    {
        options.workers = 1;
        options.warmupFrameCount = options.scenario == BenchmarkScenario::Smash ? 65 : 5;
        options.measureFrameCount = 30;
        options.timeDelta = box2dTimeDelta;
    }
    options.workers = workers ? static_cast<size_t>(*workers) : options.workers;
    options.warmupFrameCount = warmupFrames.value_or(options.warmupFrameCount);
    options.measureFrameCount = measureFrames.value_or(options.measureFrameCount);
    options.timeDelta = timeDelta.value_or(options.timeDelta);

    if (options.scenario == BenchmarkScenario::RigidBodyContainer && options.boxCount + options.circleCount <= 0)
    {
        throw std::runtime_error("rigid_body_container scenario requires at least one box or circle.");
    }

    if (isAddRemoveScenario(options.scenario))
    {
        if (options.boxCount + options.circleCount <= 0)
        {
            throw std::runtime_error("add/remove benchmark scenarios require at least one box or circle to define the "
                                     "steady-state shape mix.");
        }

        if (options.addRemoveAddCount != options.addRemoveRemoveCount)
        {
            throw std::runtime_error(
                "add/remove benchmark scenarios require matching --add-count and --remove-count values so the active "
                "population stays stable. Use --churn-count for the common case.");
        }
    }

    return options;
}

nlohmann::json resolvedParameters(const BenchmarkRunnerOptions &options)
{
    nlohmann::json result = {
        {"gravity", {GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y}},
        {"solver_substeps", COLLISION_SOLVER_SUBSTEPS},
        {"seed_policy", "deterministic velocity/pattern phase; seed zero preserves historical layouts"}};
    if (options.scenario == BenchmarkScenario::BroadPhase)
        result.update({{"body_count", options.broadPhaseElementCount},
                       {"lane_width", 48},
                       {"lane_height", 48},
                       {"lane_spacing", 80}});
    else if (options.scenario == BenchmarkScenario::BroadPhaseStress)
    {
        const auto settings = makeBroadPhaseStressScenarioSettings(options);
        const auto layout = calculateBroadPhaseStressScenarioLayout(options);
        result.update({{"body_count", settings.bodyCount},
                       {"box_width", settings.boxWidth},
                       {"box_height", settings.boxHeight},
                       {"spawn_step", {settings.spawnStepX, settings.spawnStepY}},
                       {"phase_offset", settings.phaseOffset},
                       {"layout_size", {layout.width, layout.height}},
                       {"columns", layout.columnCount},
                       {"collision_mask", 0}});
    }
    else if (options.scenario == BenchmarkScenario::NarrowPhase)
    {
        const auto layout = calculateNarrowPhaseScenarioLayout(options);
        result.update({{"element_count", layout.elementCount},
                       {"collider_count", 4 * layout.elementCount},
                       {"column_positions", layout.columnPositions},
                       {"row_positions", layout.rowPositions},
                       {"horizontal_band_positions", layout.horizontalBandPositions},
                       {"vertical_band_positions", layout.verticalBandPositions},
                       {"layout_size", {layout.lineWidth, layout.lineHeight}},
                       {"column_width", layout.columnWidth},
                       {"row_height", layout.rowHeight},
                       {"band_thickness", layout.bandThickness},
                       {"triggers", true}});
    }
    else if (options.scenario == BenchmarkScenario::RigidBodyContainer)
    {
        const auto settings = makeRigidBodyContainerScenarioSettings(options);
        const auto layout = calculateRigidBodyContainerLayout(options);
        result.update({{"box_count", settings.boxCount},
                       {"circle_count", settings.circleCount},
                       {"box_size", {settings.boxWidth, settings.boxHeight}},
                       {"circle_radius", settings.circleRadius},
                       {"wall_thickness", settings.wallThickness},
                       {"layout_size", {layout.interiorWidth, layout.interiorHeight}},
                       {"friction", settings.friction},
                       {"restitution", settings.restitution}});
    }
    else if (isAddRemoveScenario(options.scenario))
    {
        const auto settings = makeAddRemoveScenarioSettings(options);
        const auto layout = calculateAddRemoveScenarioLayout(options);
        result.update({{"steady_count", settings.steadyBodyCount},
                       {"box_count", settings.steadyBoxCount},
                       {"circle_count", settings.steadyCircleCount},
                       {"add_count", settings.addCountPerFrame},
                       {"remove_count", settings.removeCountPerFrame},
                       {"effective_churn", std::min(settings.steadyBodyCount, settings.removeCountPerFrame)},
                       {"box_size", {settings.boxWidth, settings.boxHeight}},
                       {"circle_radius", settings.circleRadius},
                       {"layout_size", {layout.width, layout.height}},
                       {"moving", settings.movingBodies},
                       {"body_type", bodyTypeName(settings.bodyType)}});
    }
    if (options.scenario == BenchmarkScenario::PersistentContacts)
        result.update({{"contact_pair_count", options.contactPairCount},
                       {"scripted_endpoints", options.scriptedEndpoints},
                       {"box_size", {4, 4}},
                       {"pair_spacing", 80},
                       {"triggers", true}});
    if (options.scenario == BenchmarkScenario::IdealWorld)
    {
        const size_t chambers = idealWorldChamberCount(options.idealWorldBodyCount);
        result.update({{"dynamic_body_count", options.idealWorldBodyCount},
                       {"bodies_per_chamber", IDEAL_WORLD_BODIES_PER_CHAMBER},
                       {"chamber_count", chambers},
                       {"static_wall_count", chambers * 4},
                       {"chamber_interior", 300.0f},
                       {"chamber_pitch", 600.0f},
                       {"friction", 0.8f},
                       {"restitution", 0.15f}});
    }
    if (const auto *scene = findBox2DScenario(options.scenario))
    {
        const bool smash = options.scenario == BenchmarkScenario::Smash;
        result.update({{"box2d_release", "v3.2.0"},
                       {"units_per_meter", box2dUnitsPerMeter},
                       {"dynamic_body_count", scene->dynamicBodies},
                       {"static_body_count", scene->staticBodies},
                       {"gravity", {0.0f, smash ? 0.0f : GRAVITY_VECTOR_Y}},
                       {"sleeping_enabled", smash},
                       {"friction", 0.6f},
                       {"restitution", 0.0f},
                       {"seed_policy", "fixed Box2D benchmark layout"}});
    }
    return result;
}
size_t expectedPopulation(const BenchmarkRunnerOptions &options)
{
    const size_t camera = options.renderFrames ? 1 : 0;
    switch (options.scenario)
    {
    case BenchmarkScenario::BroadPhase:
    case BenchmarkScenario::BroadPhaseStress:
        return options.broadPhaseElementCount + camera;
    case BenchmarkScenario::NarrowPhase:
        return size_t(options.narrowPhaseElementCount) * 4 + camera;
    case BenchmarkScenario::RigidBodyContainer:
        return size_t(options.boxCount) + options.circleCount + 4 + camera;
    case BenchmarkScenario::AddAndRemove:
    case BenchmarkScenario::MovingAddAndRemove:
        return options.addRemoveSteadyCount + camera;
    case BenchmarkScenario::PersistentContacts:
        return size_t(options.contactPairCount) * 2 + camera;
    case BenchmarkScenario::IdealWorld:
        return size_t(options.idealWorldBodyCount) + idealWorldChamberCount(options.idealWorldBodyCount) * 4 + camera;
    case BenchmarkScenario::LargePyramid:
    case BenchmarkScenario::ManyPyramids:
    case BenchmarkScenario::Smash: {
        const auto &scene = *findBox2DScenario(options.scenario);
        return scene.dynamicBodies + scene.staticBodies + camera;
    }
    case BenchmarkScenario::None:
        return 0;
    }
    throw std::logic_error("Unknown benchmark scenario");
}
nlohmann::json validatePhysicalState(const platformator::Runtime &runtime, const BenchmarkRunnerOptions &options,
                                     bool snapshot = false, bool contactsReady = true)
{
    const auto &objects = runtime.getAllGameObjects();
    if (options.scenario != BenchmarkScenario::None && objects.size() != expectedPopulation(options))
        throw std::runtime_error("Benchmark population does not match the resolved workload");
    if (options.scenario == BenchmarkScenario::PersistentContacts)
    {
        const auto &pairs = platformator_detail::RuntimeAccess::physicsManager()->getContactPairs();
        size_t scripts = 0;
        for (const auto *object : objects)
            scripts += object->getComponent<ScriptComponent>() != nullptr;
        if ((contactsReady && pairs.size() != size_t(options.contactPairCount)) ||
            scripts != size_t(options.contactPairCount) * options.scriptedEndpoints)
            throw std::runtime_error("Persistent-contact fixture has incorrect contact/script populations");
    }
    size_t colliders = 0, bodies = 0, dynamicBodies = 0, awakeDynamicBodies = 0, supportTotal = 0, maximumSupports = 0;
    std::unordered_map<const Rigidbody *, size_t> expectedSupports;
    nlohmann::json bodySnapshots = nlohmann::json::array(), contacts = nlohmann::json::array();
    for (const auto *object : objects)
    {
        if (object->getComponent<Collider>())
            ++colliders;
        if (options.validateInvariants)
            if (const auto *body = object->getComponent<Rigidbody>())
                expectedSupports.emplace(body, 0);
    }
    if (options.validateInvariants)
    {
        const auto *physics = platformator_detail::RuntimeAccess::physicsManager();
        for (const auto *pair : physics->getContactPairs())
        {
            const auto *collision = pair->getCollision();
            const auto *a = collision->getReferenceObject(), *b = collision->getIncidentObject();
            const auto *bodyA = a->getGameObject()->getComponent<Rigidbody>();
            const auto *bodyB = b->getGameObject()->getComponent<Rigidbody>();
            const auto &normal = collision->getNormal();
            const auto &points = collision->getContactPoints();
            if (!normal.allFinite() || std::abs(normal.squaredNorm() - 1) > 1e-4f || !points.count || points.count > 2)
                throw std::runtime_error("Invalid contact manifold: " + a->getGameObject()->getName() + " / " +
                                         b->getGameObject()->getName() + " count=" + std::to_string(points.count) +
                                         " normal_length_squared=" + std::to_string(normal.squaredNorm()));
            if (!(a->getCollisionGroup() & b->getCollisionMask()) || !(b->getCollisionGroup() & a->getCollisionMask()))
                throw std::runtime_error("Contact survived a rejecting filter");
            if (bodyA && bodyB && !a->getIsTrigger() && !b->getIsTrigger())
            {
                // Independent reduction from normal/configuration, not the pair's cached support flags.
                if (bodyA->qualifiesAsSupportContact(-normal, physics->getGravityVectorNormalized()))
                    ++expectedSupports.at(bodyA);
                if (bodyB->qualifiesAsSupportContact(normal, physics->getGravityVectorNormalized()))
                    ++expectedSupports.at(bodyB);
            }
            nlohmann::json features = nlohmann::json::array();
            for (size_t i = 0; i < points.count; ++i)
            {
                if (!points.points[i].point.point.allFinite() || !std::isfinite(points.points[i].separation))
                    throw std::runtime_error("Nonfinite contact point in validation mode");
                if (snapshot)
                    features.push_back({points.points[i].point.feature.key, points.points[i].separation});
            }
            if (snapshot)
                contacts.push_back({{"a", std::min(a->getId(), b->getId())},
                                    {"b", std::max(a->getId(), b->getId())},
                                    {"reference", a->getId()},
                                    {"normal", {normal.x(), normal.y()}},
                                    {"features", features}});
        }
    }
    for (const auto *object : objects)
    {
        if (!object->getPosition().allFinite() || !object->getScale().allFinite() ||
            !std::isfinite(object->getRotation()))
            throw std::runtime_error("Nonfinite benchmark transform");
        if (const auto *body = object->getComponent<Rigidbody>())
        {
            ++bodies;
            if (body->getBodyType() == DYNAMIC)
            {
                ++dynamicBodies;
                awakeDynamicBodies += !body->getIsSleeping();
            }
            if (!body->getVelocity().allFinite() || !std::isfinite(body->getAngularVelocity()))
                throw std::runtime_error("Nonfinite benchmark body state");
            const auto supports = body->getSupportContactCount();
            if (supports > (colliders ? colliders - 1 : 0))
                throw std::runtime_error("Benchmark support count exceeds possible contacts");
            if (options.validateInvariants && supports != expectedSupports.at(body))
                throw std::runtime_error("Support reduction disagrees with independent contact oracle for " +
                                         object->getName());
            supportTotal += supports;
            maximumSupports = std::max(maximumSupports, supports);
            if (snapshot)
            {
                const auto position = object->getPosition();
                bodySnapshots.push_back({{"id", object->getId()},
                                         {"name", object->getName()},
                                         {"position", {position.x(), position.y()}},
                                         {"rotation", object->getRotation()},
                                         {"velocity", {body->getVelocity().x(), body->getVelocity().y()}},
                                         {"angular_velocity", body->getAngularVelocity()},
                                         {"supports", supports},
                                         {"sleeping", body->getIsSleeping()}});
            }
        }
    }
    nlohmann::json result = {{"finite", true},
                             {"populations_valid", true},
                             {"supports_valid", true},
                             {"objects", objects.size()},
                             {"colliders", colliders},
                             {"bodies", bodies},
                             {"support_total", supportTotal},
                             {"maximum_supports", maximumSupports},
                             {"population_model", options.scenario == BenchmarkScenario::None
                                                      ? "scene controlled"
                                                      : "resolved scenario count"}};
    if (const auto *scene = findBox2DScenario(options.scenario))
    {
        if (dynamicBodies != scene->dynamicBodies || bodies != scene->dynamicBodies + scene->staticBodies ||
            colliders != bodies)
            throw std::runtime_error("Box2D benchmark body or collider count does not match the scenario");
        const bool smash = options.scenario == BenchmarkScenario::Smash;
        if ((!smash && awakeDynamicBodies != dynamicBodies) || (smash && !contactsReady && awakeDynamicBodies != 1))
            throw std::runtime_error("Box2D benchmark awake body count does not match the scenario");
        result["dynamic_bodies"] = dynamicBodies;
        result["awake_dynamic_bodies"] = awakeDynamicBodies;
    }
    if (snapshot)
    {
        std::sort(bodySnapshots.begin(), bodySnapshots.end(),
                  [](const auto &a, const auto &b) { return a.at("id") < b.at("id"); });
        std::sort(contacts.begin(), contacts.end(), [](const auto &a, const auto &b) {
            return std::pair{a.at("a").template get<int>(), a.at("b").template get<int>()} <
                   std::pair{b.at("a").template get<int>(), b.at("b").template get<int>()};
        });
        result["body_snapshots"] = std::move(bodySnapshots);
        result["contact_snapshots"] = std::move(contacts);
    }
    return result;
}

void runBenchmark(const BenchmarkRunnerOptions &options)
{
    tbb::global_control workers(tbb::global_control::max_allowed_parallelism, options.workers);
    platformator::Runtime runtime(options.runtimeOptions);
    AddRemoveScenarioState addRemoveState;
    BroadPhaseStressScenarioState broadPhaseStressState;
    std::function<void(double)> callback;
    if (options.scenario != BenchmarkScenario::None)
        buildScenario(runtime, options,
                      options.scenario == BenchmarkScenario::BroadPhaseStress ? &broadPhaseStressState : nullptr,
                      isAddRemoveScenario(options.scenario) ? &addRemoveState : nullptr);
    configureScenarioCamera(runtime, options,
                            options.scenario == BenchmarkScenario::BroadPhaseStress ? &broadPhaseStressState : nullptr,
                            isAddRemoveScenario(options.scenario) ? &addRemoveState : nullptr);
    if (isAddRemoveScenario(options.scenario))
        callback = [&](double) { applyAddRemoveScenarioChurn(runtime, addRemoveState, options.renderFrames); };
    else if (options.scenario == BenchmarkScenario::BroadPhaseStress)
        callback = [&](double) { applyBroadPhaseStressScenarioMotion(broadPhaseStressState); };
    const bool box2dScenario = findBox2DScenario(options.scenario) != nullptr;
    const auto sceneStart = box2dScenario ? validatePhysicalState(runtime, options, false, false) : nlohmann::json{};
    const auto step = [&]() {
        bool running = true;
        if (options.renderFrames)
            running = callback ? runtime.simulateAndRenderFrameWithCustomCallback(options.timeDelta, callback)
                               : runtime.simulateAndRenderFrame(options.timeDelta);
        else if (callback)
            runtime.simulateFrameWithCustomCallback(options.timeDelta, callback);
        else
            runtime.simulateFrame(options.timeDelta);
        if (options.validateInvariants)
            validatePhysicalState(runtime, options);
        return running;
    };
    int warmup = 0;
    bool running = true;
    for (; warmup < options.warmupFrameCount; ++warmup)
        if (!step())
        {
            running = false;
            break;
        }
    const auto initial = box2dScenario && warmup == 0 ? sceneStart : validatePhysicalState(runtime, options, false, warmup != 0);
    const auto *window = platformator_detail::RuntimeAccess::window();
    const auto fallbacksBeforeMeasured = window != nullptr
                                             ? window->getObservedRenderFallbackCounts()
                                             : SDLWindow::RenderFallbackCounts{};
    auto &recorder = platformator_detail::BenchmarkRecorder::getInstance();
    recorder.configure(options.measureFrameCount,
                       {{"scenario", scenarioName(options.scenario)},
                        {"seed", options.seed},
                        {"workers", tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism)},
                        {"dt", options.timeDelta},
                        {"warmup_steps", warmup},
                        {"measured_steps", options.measureFrameCount},
                        {"repetitions", options.repetitions},
                        {"repetition_index", options.runIndex},
                        {"rendering", options.renderFrames},
                        {"linear_render_culling", renderCullingMetadata(options, window)},
                        {"validation_mode", options.validateInvariants ? "per-step" : "boundary-only"},
                        {"timing_eligible", !options.validateInvariants},
                        {"asset_root", runtime.getAssetRoot().string()},
                        {"resolved_parameters", resolvedParameters(options)},
                        {"initial_state", initial}});
    if (box2dScenario)
        recorder.updateMetadata({{"scene_start_state", sceneStart}});
    for (int i = 0; running && i < options.measureFrameCount; ++i)
        running = step();
    const auto fallbacksAfterMeasured = window != nullptr
                                            ? window->getObservedRenderFallbackCounts()
                                            : SDLWindow::RenderFallbackCounts{};
    const SDLWindow::RenderFallbackCounts observedFallbacks{
        fallbacksAfterMeasured.mappingFallbacks - fallbacksBeforeMeasured.mappingFallbacks,
        fallbacksAfterMeasured.numericFallbacks - fallbacksBeforeMeasured.numericFallbacks};
    const auto final = validatePhysicalState(runtime, options, options.validateInvariants);
    for (const auto &sample : recorder.samples())
        if (options.scenario != BenchmarkScenario::None &&
            sample.counters[static_cast<size_t>(platformator_detail::BenchmarkCounterId::ObjectCount)] !=
                expectedPopulation(options))
            throw std::runtime_error("Measured step population does not match the scenario");
    if (options.scenario == BenchmarkScenario::NarrowPhase && !recorder.samples().empty() &&
        recorder.samples()
                .back()
                .counters[static_cast<size_t>(platformator_detail::BenchmarkCounterId::ActiveCollisionCount)] == 0)
        throw std::runtime_error("Narrow-phase workload produced no accepted contacts");
    recorder.updateMetadata({{"linear_render_culling", renderCullingMetadata(options, window, observedFallbacks)},
                             {"physical_invariants", final},
                             {"stopped_on_quit", !running}});
    if (!options.csvOutputPath.empty())
        recorder.writeOutputs(options.csvOutputPath);
    recorder.printSummary();
}

void printUsage(FILE *output)
{
    std::fprintf(
        output,
        "Usage: platformator_benchmark_runner [scene] [--scene path | --scenario "
        "broad_phase|broad_phase_stress|narrow_phase|rigid_body_container|add_and_remove|moving_add_and_remove|"
        "persistent_contacts|ideal_world|large_pyramid|many_pyramids|smash] "
        "[--warmup-frames N] [--measure-frames N] [--dt seconds] [--box-count N] [--circle-count N] [--broad-count N] "
        "[--contact-pair-count N] [--scripted-endpoints 0|1|2] [--narrow-count N] [--ideal-count N] [--steady-count N] "
        "[--add-count N] [--remove-count N] [--churn-count N] [--csv-output|--csv path] [--repetitions N] "
        "[--run-index N] [--workers N] [--seed N] [--validate-invariants] [--asset-root path] [--render] "
        "[--no-render-culling] [--debug-draw <categories>]\n");
}
} // namespace

int main(int argc, char *args[])
{
    try
    {
        runBenchmark(parseBenchmarkRunnerOptions(argc, args));
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::fprintf(stderr, "%s\n", exception.what());
        printUsage(stderr);
        return 1;
    }
}
