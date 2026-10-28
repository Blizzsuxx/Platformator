#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#include <charconv>
#include <limits>
#include <thread>
#include <oneapi/tbb/global_control.h>
#include <unordered_map>
#include "runtimeaccess.h"
#include "physicsmanager.h"
#include "sdlwindow.h"

#include "benchmark.h"
#include "spatialresourcehooks.h"
#include "spatialreplay.h"
#include "spatial_fixtures.h"
#include "constants.h"
#include "spatialsettingsparse.h"
#include "platformator/boxcollider.h"
#include "platformator/scriptcomponent.h"
#include "platformator/camera.h"
#include "platformator/circlecollider.h"
#include "platformator/rigidbody.h"
#include "platformator/runtime.h"
#include "platformator/sprite.h"

namespace
{
constexpr const char *DEFAULT_SCENE_PATH = "assets/scenes/default.scene";
constexpr int DEFAULT_CONTAINER_BOX_COUNT = 400;
constexpr int DEFAULT_CONTAINER_CIRCLE_COUNT = 400;
constexpr int DEFAULT_BROAD_PHASE_ELEMENT_COUNT = 128;
constexpr int DEFAULT_NARROW_PHASE_ELEMENT_COUNT = 8;
constexpr int DEFAULT_ADD_REMOVE_STEADY_BODY_COUNT = 1000;
constexpr int DEFAULT_ADD_REMOVE_CHURN_COUNT = 10;

struct RigidBodyContainerScenarioSettings
{
    int boxCount = DEFAULT_CONTAINER_BOX_COUNT;
    int circleCount = DEFAULT_CONTAINER_CIRCLE_COUNT;
    float boxWidth = 28.0f;
    float boxHeight = 28.0f;
    float circleRadius = 14.0f;
    float wallThickness = 64.0f;
    float spawnMargin = 10.0f;
    float cellPadding = 10.0f;
    float minimumInteriorWidth = 1600.0f;
    float minimumInteriorHeight = 900.0f;
    float cameraMargin = 160.0f;
    float friction = 1.0f;
    float restitution = 0.00f;
};

struct RigidBodyContainerLayout
{
    size_t totalBodyCount = 0;
    size_t columnCount = 1;
    float cellSize = 0.0f;
    float interiorWidth = 0.0f;
    float interiorHeight = 0.0f;
    float left = 0.0f;
    float top = 0.0f;
};

struct BroadPhaseStressScenarioSettings
{
    int bodyCount = DEFAULT_BROAD_PHASE_ELEMENT_COUNT;
    float boxWidth = 48.0f;
    float boxHeight = 48.0f;
    float spawnStepX = 6.0f;
    float spawnStepY = 6.0f;
    float phaseOffset = 320.0f;
    float cameraMargin = 200.0f;
};

struct BroadPhaseStressScenarioLayout
{
    size_t totalBodyCount = 0;
    size_t columnCount = 1;
    float width = 0.0f;
    float height = 0.0f;
    float left = 0.0f;
    float top = 0.0f;
};

struct AddRemoveScenarioSettings
{
    int steadyBodyCount = DEFAULT_ADD_REMOVE_STEADY_BODY_COUNT;
    int addCountPerFrame = DEFAULT_ADD_REMOVE_CHURN_COUNT;
    int removeCountPerFrame = DEFAULT_ADD_REMOVE_CHURN_COUNT;
    int steadyBoxCount = DEFAULT_ADD_REMOVE_STEADY_BODY_COUNT / 2;
    int steadyCircleCount = DEFAULT_ADD_REMOVE_STEADY_BODY_COUNT / 2;
    float boxWidth = 28.0f;
    float boxHeight = 28.0f;
    float circleRadius = 14.0f;
    float spawnMargin = 24.0f;
    float cellPadding = 20.0f;
    float minimumWidth = 1800.0f;
    float minimumHeight = 1000.0f;
    float cameraMargin = 160.0f;
    float friction = 0.0f;
    float restitution = 0.0f;
    BodyType bodyType = STATIC;
    bool isTrigger = false;
    bool movingBodies = false;
};

struct AddRemoveScenarioLayout
{
    size_t totalSlotCount = 0;
    size_t columnCount = 1;
    float cellSize = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float left = 0.0f;
    float top = 0.0f;
};

struct NarrowPhaseScenarioLayout
{
    int elementCount = 1;
    float columnWidth = 26.0f;
    float rowHeight = 26.0f;
    float bandThickness = 18.0f;
    float staticLaneSpacing = 44.0f;
    float bandLaneSpacing = 36.0f;
    float linePadding = 96.0f;
    float cameraMargin = 120.0f;
    float lineWidth = 0.0f;
    float lineHeight = 0.0f;
    std::vector<float> columnPositions;
    std::vector<float> rowPositions;
    std::vector<float> horizontalBandPositions;
    std::vector<float> verticalBandPositions;
};

struct BroadPhaseStressScenarioState
{
    BroadPhaseStressScenarioSettings settings;
    BroadPhaseStressScenarioLayout layout;
    std::vector<GameObject *> movingBodies;
    size_t phaseIndex = 0;
};

struct AddRemoveManagedBody
{
    GameObject *object = nullptr;
    size_t slotIndex = 0;
};

struct AddRemoveScenarioState
{
    AddRemoveScenarioSettings settings;
    AddRemoveScenarioLayout layout;
    std::vector<bool> slotUsesBox;
    std::deque<AddRemoveManagedBody> activeBodies;
    std::vector<size_t> freeSlotIndices;
    size_t spawnedBoxCount = 0;
    size_t spawnedCircleCount = 0;
    uint64_t seed = 0;
};

RuntimeOptions makeDefaultBenchmarkRuntimeOptions()
{
    // RuntimeOptions runtimeOptions{DEFAULT_SCENE_PATH, {}, {false, true, true, true, true}};
    // runtimeOptions.windowSettings.headless = false;
    // runtimeOptions.windowSettings.fullscreen = true;

    RuntimeOptions runtimeOptions{DEFAULT_SCENE_PATH, {}, {false, false, false, false, false}};
    runtimeOptions.windowSettings.headless = true;
    return runtimeOptions;
}

enum class BenchmarkScenario
{
    None,
    BroadPhase,
    BroadPhaseStress,
    NarrowPhase,
    RigidBodyContainer,
    AddAndRemove,
    MovingAddAndRemove,
    PersistentContacts,
    SpriteVisibility,
    SpatialCombined,
};

bool isAddRemoveScenario(const BenchmarkScenario scenario)
{
    return scenario == BenchmarkScenario::AddAndRemove || scenario == BenchmarkScenario::MovingAddAndRemove;
}

struct BenchmarkRunnerOptions
{
    RuntimeOptions runtimeOptions = makeDefaultBenchmarkRuntimeOptions();
    BenchmarkScenario scenario = BenchmarkScenario::None;
    int warmupFrameCount = 120;
    int measureFrameCount = 600;
    double timeDelta = FRAME_TIME;
    int boxCount = DEFAULT_CONTAINER_BOX_COUNT;
    int circleCount = DEFAULT_CONTAINER_CIRCLE_COUNT;
    int broadPhaseElementCount = DEFAULT_BROAD_PHASE_ELEMENT_COUNT;
    int narrowPhaseElementCount = DEFAULT_NARROW_PHASE_ELEMENT_COUNT;
    int addRemoveSteadyCount = DEFAULT_ADD_REMOVE_STEADY_BODY_COUNT;
    int addRemoveAddCount = DEFAULT_ADD_REMOVE_CHURN_COUNT;
    int addRemoveRemoveCount = DEFAULT_ADD_REMOVE_CHURN_COUNT;
    bool renderFrames = false;
    std::string csvOutputPath;
    size_t workers = std::max(1u, std::thread::hardware_concurrency());
    uint64_t seed = 0;
    int repetitions = 1, runIndex = 0;
    bool validateInvariants = false;
    int contactPairCount = 100, scriptedEndpoints = 2;
    int spriteCount = 1000;
    double visiblePercent = 10.0, changedPercent = 10.0;
    std::string spatialVariant = "static";
    std::string spatialTracePath;
    bool captureModeExplicit = false;
    bool warmupExplicit = false, measureExplicit = false, dtExplicit = false, seedExplicit = false;
    std::string captureMode = "performance";
    std::string caseId;
    std::string caseSha256;
};

BenchmarkScenario parseScenario(const std::string &value)
{
    if (value == "broad_phase" || value == "broad-phase" || value == "broadphase")
    {
        return BenchmarkScenario::BroadPhase;
    }

    if (value == "broad_phase_stress" || value == "broad-phase-stress" || value == "broadphasestress")
    {
        return BenchmarkScenario::BroadPhaseStress;
    }

    if (value == "narrow_phase" || value == "narrow-phase" || value == "narrowphase")
    {
        return BenchmarkScenario::NarrowPhase;
    }

    if (value == "rigid_body_container" || value == "rigid-body-container" || value == "rigidbody_container" ||
        value == "rigidbody-container" || value == "container")
    {
        return BenchmarkScenario::RigidBodyContainer;
    }

    if (value == "add_and_remove" || value == "add-and-remove" || value == "addandremove")
    {
        return BenchmarkScenario::AddAndRemove;
    }

    if (value == "moving_add_and_remove" || value == "moving-add-and-remove" || value == "movingaddandremove" ||
        value == "add_and_remove_moving" || value == "add-and-remove-moving" || value == "addandremovemoving")
    {
        return BenchmarkScenario::MovingAddAndRemove;
    }

    if (value == "persistent_contacts")
        return BenchmarkScenario::PersistentContacts;
    if (value == "sprite_visibility")
        return BenchmarkScenario::SpriteVisibility;
    if (value == "spatial_combined")
        return BenchmarkScenario::SpatialCombined;

    throw std::runtime_error("Unknown benchmark scenario: " + value);
}

const char *scenarioName(const BenchmarkScenario scenario)
{
    switch (scenario)
    {
    case BenchmarkScenario::BroadPhase:
        return "broad_phase";
    case BenchmarkScenario::BroadPhaseStress:
        return "broad_phase_stress";
    case BenchmarkScenario::NarrowPhase:
        return "narrow_phase";
    case BenchmarkScenario::RigidBodyContainer:
        return "rigid_body_container";
    case BenchmarkScenario::AddAndRemove:
        return "add_and_remove";
    case BenchmarkScenario::MovingAddAndRemove:
        return "moving_add_and_remove";
    case BenchmarkScenario::PersistentContacts:
        return "persistent_contacts";
    case BenchmarkScenario::SpriteVisibility:
        return "sprite_visibility";
    case BenchmarkScenario::SpatialCombined:
        return "spatial_combined";
    case BenchmarkScenario::None:
        return "scene";
    }

    return "scene";
}

const char *broadPhaseName(BroadPhaseMode mode)
{
    switch (mode)
    {
    case BroadPhaseMode::Segmented: return "segmented";
    case BroadPhaseMode::DynamicBvh: return "bvh";
    }
    return "invalid";
}

const char *renderCullingName(RenderCullingMode mode)
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

const char *maintenanceName(BvhMaintenance policy)
{
    switch (policy)
    {
    case BvhMaintenance::NativePartial: return "native-partial";
    case BvhMaintenance::FullRebuild: return "full-rebuild";
    }
    return "invalid";
}

const char *queryExecutionName(BvhQueryExecution execution)
{
    switch (execution)
    {
    case BvhQueryExecution::Serial: return "serial";
    case BvhQueryExecution::Parallel: return "parallel";
    }
    return "invalid";
}

nlohmann::json spatialSelectionMetadata(const BenchmarkRunnerOptions &options)
{
    const auto &settings = options.runtimeOptions.spatialSettings;
    const nlohmann::json requested = {
        {"broad_phase", broadPhaseName(settings.broadPhase)},
        {"render_culling", renderCullingName(settings.renderCulling)},
        {"bvh_maintenance", maintenanceName(settings.bvhMaintenance)},
        {"bvh_padding", settings.bvhPadding},
        {"bvh_query_execution", queryExecutionName(settings.bvhQueryExecution)}};
    const bool physicsUsesBvh = settings.broadPhase == BroadPhaseMode::DynamicBvh;
    const bool renderUsesBvh = options.renderFrames && settings.renderCulling == RenderCullingMode::DynamicBvh;
    const bool usesBvh = physicsUsesBvh || renderUsesBvh;
    const char *renderStatus = !options.renderFrames ? "disabled_no_render"
                               : settings.renderCulling == RenderCullingMode::None ? "none_selected"
                                                                                   : "active";
    const nlohmann::json effective = {
        {"broad_phase", broadPhaseName(settings.broadPhase)},
        {"render_culling", options.renderFrames ? renderCullingName(settings.renderCulling) : "none"},
        {"bvh_maintenance", usesBvh ? maintenanceName(settings.bvhMaintenance) : "not_applicable"},
        {"bvh_padding", usesBvh ? nlohmann::json(settings.bvhPadding) : nlohmann::json(nullptr)},
        {"bvh_query_execution", physicsUsesBvh ? queryExecutionName(settings.bvhQueryExecution)
                                             : renderUsesBvh ? "serial" : "not_applicable"},
        {"render_culling_status", renderStatus}};
    return {{"requested", requested}, {"effective", effective}, {"rendering_requested", options.renderFrames}};
}

std::string formatIndexedName(const std::string &prefix, const size_t index)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s %02zu", prefix.c_str(), index);
    return buffer;
}

GameObject *createBenchmarkBox(platformator::Runtime &runtime, const std::string &name, const float x, const float y,
                               const float width, const float height, const BodyType bodyType, const bool gravity,
                               const Eigen::Vector2f &velocity, const bool isTrigger, const float friction = 0.0f,
                               const float restitution = 0.0f, const float angularVelocity = 0.0f,
                               const bool attachSprite = false)
{
    GameObject *object = runtime.createGameObject()
                             ->setName(name)
                             ->addComponent<Rigidbody>(bodyType, gravity)
                             ->addComponent<BoxCollider>(width, height)
                             ->setPosition(Eigen::Vector2f(x, y));

    if (attachSprite)
    {
        object->addComponent<Sprite>("assets/textures/black_square.png", width, height);
    }

    object->getComponent<Rigidbody>()
        ->setVelocity(velocity)
        ->setAngularVelocity(angularVelocity)
        ->setFriction(friction)
        ->setRestitution(restitution);

    object->getComponent<BoxCollider>()->setIsTrigger(isTrigger);
    return object;
}

GameObject *createBenchmarkCircle(platformator::Runtime &runtime, const std::string &name, const float x, const float y,
                                  const float radius, const BodyType bodyType, const bool gravity,
                                  const Eigen::Vector2f &velocity, const bool isTrigger, const float friction = 0.0f,
                                  const float restitution = 0.0f, const float angularVelocity = 0.0f,
                                  const bool attachSprite = false)
{
    GameObject *object = runtime.createGameObject()
                             ->setName(name)
                             ->addComponent<Rigidbody>(bodyType, gravity)
                             ->addComponent<CircleCollider>(radius)
                             ->setPosition(Eigen::Vector2f(x, y));

    if (attachSprite)
    {
        object->addComponent<Sprite>("assets/textures/red_circle.png", radius * 2.0f, radius * 2.0f);
    }

    object->getComponent<Rigidbody>()
        ->setVelocity(velocity)
        ->setAngularVelocity(angularVelocity)
        ->setFriction(friction)
        ->setRestitution(restitution);

    object->getComponent<CircleCollider>()->setIsTrigger(isTrigger);
    return object;
}

RigidBodyContainerScenarioSettings makeRigidBodyContainerScenarioSettings(const BenchmarkRunnerOptions &options)
{
    RigidBodyContainerScenarioSettings settings;
    settings.boxCount = options.boxCount;
    settings.circleCount = options.circleCount;
    return settings;
}

BroadPhaseStressScenarioSettings makeBroadPhaseStressScenarioSettings(const BenchmarkRunnerOptions &options)
{
    BroadPhaseStressScenarioSettings settings;
    settings.bodyCount = options.broadPhaseElementCount;
    return settings;
}

AddRemoveScenarioSettings makeAddRemoveScenarioSettings(const BenchmarkRunnerOptions &options)
{
    AddRemoveScenarioSettings settings;
    settings.steadyBodyCount = options.addRemoveSteadyCount;
    settings.addCountPerFrame = options.addRemoveAddCount;
    settings.removeCountPerFrame = options.addRemoveRemoveCount;

    if (options.scenario == BenchmarkScenario::MovingAddAndRemove)
    {
        settings.bodyType = KINEMATIC;
        settings.isTrigger = true;
        settings.movingBodies = true;
        settings.cameraMargin = 320.0f;
    }

    const int compositionTotal = options.boxCount + options.circleCount;
    if (compositionTotal <= 0)
    {
        settings.steadyBoxCount = settings.steadyBodyCount / 2;
    }
    else
    {
        const int64_t scaledBoxCount =
            static_cast<int64_t>(settings.steadyBodyCount) * static_cast<int64_t>(options.boxCount);
        settings.steadyBoxCount = static_cast<int>(scaledBoxCount / compositionTotal);
    }

    settings.steadyBoxCount = std::clamp(settings.steadyBoxCount, 0, settings.steadyBodyCount);
    settings.steadyCircleCount = settings.steadyBodyCount - settings.steadyBoxCount;
    return settings;
}

RigidBodyContainerLayout calculateRigidBodyContainerLayout(const BenchmarkRunnerOptions &options)
{
    const RigidBodyContainerScenarioSettings settings = makeRigidBodyContainerScenarioSettings(options);
    const size_t totalBodyCount = static_cast<size_t>(settings.boxCount + settings.circleCount);
    const float maxBodyExtent = std::max({settings.boxWidth, settings.boxHeight, settings.circleRadius * 2.0f});
    const float cellSize = maxBodyExtent + settings.cellPadding;
    const float usableWidth = std::max(settings.minimumInteriorWidth - 2.0f * settings.spawnMargin, cellSize);
    const size_t columnCount = std::max<size_t>(1, static_cast<size_t>(usableWidth / cellSize));
    const size_t rowCount = std::max<size_t>(1, (totalBodyCount + columnCount - 1) / columnCount);
    const float interiorWidth = std::max(settings.minimumInteriorWidth,
                                         static_cast<float>(columnCount) * cellSize + 2.0f * settings.spawnMargin);
    const float interiorHeight =
        std::max(settings.minimumInteriorHeight, static_cast<float>(rowCount) * cellSize + 2.0f * settings.spawnMargin);

    return RigidBodyContainerLayout{
        totalBodyCount,         columnCount, cellSize, interiorWidth, interiorHeight, -0.5f * interiorWidth,
        -0.5f * interiorHeight,
    };
}

AddRemoveScenarioLayout calculateAddRemoveScenarioLayout(const BenchmarkRunnerOptions &options)
{
    const AddRemoveScenarioSettings settings = makeAddRemoveScenarioSettings(options);
    const size_t totalSlotCount = static_cast<size_t>(settings.steadyBodyCount);
    const float maxBodyExtent = std::max({settings.boxWidth, settings.boxHeight, settings.circleRadius * 2.0f});
    const float cellSize = maxBodyExtent + settings.cellPadding;
    const float usableWidth = std::max(settings.minimumWidth - 2.0f * settings.spawnMargin, cellSize);
    const size_t columnCount = std::max<size_t>(1, static_cast<size_t>(usableWidth / cellSize));
    const size_t rowCount = std::max<size_t>(1, (totalSlotCount + columnCount - 1) / columnCount);
    const float width =
        std::max(settings.minimumWidth, static_cast<float>(columnCount) * cellSize + 2.0f * settings.spawnMargin);
    const float height =
        std::max(settings.minimumHeight, static_cast<float>(rowCount) * cellSize + 2.0f * settings.spawnMargin);

    return AddRemoveScenarioLayout{
        totalSlotCount, columnCount, cellSize, width, height, -0.5f * width, -0.5f * height,
    };
}

BroadPhaseStressScenarioLayout calculateBroadPhaseStressScenarioLayout(const BenchmarkRunnerOptions &options)
{
    const BroadPhaseStressScenarioSettings settings = makeBroadPhaseStressScenarioSettings(options);
    const size_t totalBodyCount = static_cast<size_t>(std::max(1, settings.bodyCount));
    const size_t columnCount =
        std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(totalBodyCount)))));
    const size_t rowCount = std::max<size_t>(1, (totalBodyCount + columnCount - 1) / columnCount);

    const float width =
        settings.boxWidth + static_cast<float>(std::max<size_t>(0, columnCount - 1)) * settings.spawnStepX;
    const float height =
        settings.boxHeight + static_cast<float>(std::max<size_t>(0, rowCount - 1)) * settings.spawnStepY;

    return BroadPhaseStressScenarioLayout{
        totalBodyCount, columnCount, width, height, -0.5f * width, -0.5f * height,
    };
}

Eigen::Vector2f calculateRigidBodyContainerSpawnPosition(const RigidBodyContainerLayout &layout,
                                                         const RigidBodyContainerScenarioSettings &settings,
                                                         const size_t spawnIndex)
{
    const size_t columnIndex = spawnIndex % layout.columnCount;
    const size_t rowIndex = spawnIndex / layout.columnCount;
    const float x = layout.left + settings.spawnMargin + (static_cast<float>(columnIndex) + 0.5f) * layout.cellSize;
    const float y = layout.top + settings.spawnMargin + (static_cast<float>(rowIndex) + 0.5f) * layout.cellSize;
    const float xJitter = static_cast<float>(static_cast<int>(spawnIndex % 3) - 1) * 2.0f;
    const float yJitter = static_cast<float>(static_cast<int>(spawnIndex % 4) - 1) * 1.5f;
    return Eigen::Vector2f(x + xJitter, y + yJitter);
}

Eigen::Vector2f calculateAddRemoveSpawnPosition(const AddRemoveScenarioLayout &layout,
                                                const AddRemoveScenarioSettings &settings, const size_t slotIndex)
{
    const size_t columnIndex = slotIndex % layout.columnCount;
    const size_t rowIndex = slotIndex / layout.columnCount;
    const float x = layout.left + settings.spawnMargin + (static_cast<float>(columnIndex) + 0.5f) * layout.cellSize;
    const float y = layout.top + settings.spawnMargin + (static_cast<float>(rowIndex) + 0.5f) * layout.cellSize;
    return Eigen::Vector2f(x, y);
}

Eigen::Vector2f calculateBroadPhaseStressSpawnPosition(const BroadPhaseStressScenarioLayout &layout,
                                                       const BroadPhaseStressScenarioSettings &settings,
                                                       const size_t spawnIndex)
{
    const size_t columnIndex = spawnIndex % layout.columnCount;
    const size_t rowIndex = spawnIndex / layout.columnCount;
    const float x = layout.left + 0.5f * settings.boxWidth + static_cast<float>(columnIndex) * settings.spawnStepX;
    const float y = layout.top + 0.5f * settings.boxHeight + static_cast<float>(rowIndex) * settings.spawnStepY;
    return Eigen::Vector2f(x, y);
}

std::vector<bool> buildAddRemoveSlotShapeMap(const AddRemoveScenarioSettings &settings)
{
    std::vector<bool> slotUsesBox(static_cast<size_t>(settings.steadyBodyCount), false);
    size_t assignedBoxCount = 0;

    for (size_t slotIndex = 0; slotIndex < slotUsesBox.size(); ++slotIndex)
    {
        const size_t expectedBoxCount =
            (static_cast<size_t>(slotIndex + 1) * static_cast<size_t>(settings.steadyBoxCount)) /
            static_cast<size_t>(std::max(1, settings.steadyBodyCount));
        const bool useBox = assignedBoxCount < expectedBoxCount;
        slotUsesBox[slotIndex] = useBox;
        if (useBox)
        {
            assignedBoxCount++;
        }
    }

    return slotUsesBox;
}

const char *bodyTypeName(const BodyType bodyType)
{
    switch (bodyType)
    {
    case DYNAMIC:
        return "dynamic";
    case STATIC:
        return "static";
    case KINEMATIC:
        return "kinematic";
    }

    return "unknown";
}

Eigen::Vector2f calculateAddRemoveInitialVelocity(const size_t spawnIndex)
{
    constexpr std::array<float, 8> horizontalVelocityPattern = {-72.0f, 60.0f, -48.0f, 84.0f,
                                                                -64.0f, 52.0f, -88.0f, 68.0f};
    constexpr std::array<float, 6> verticalVelocityPattern = {34.0f, -28.0f, 22.0f, -40.0f, 30.0f, -18.0f};

    return Eigen::Vector2f(horizontalVelocityPattern[spawnIndex % horizontalVelocityPattern.size()],
                           verticalVelocityPattern[spawnIndex % verticalVelocityPattern.size()]);
}

Eigen::Vector2f calculateRigidBodyContainerInitialVelocity(const size_t spawnIndex)
{
    constexpr std::array<float, 8> horizontalVelocityPattern = {-48.0f, 36.0f, -28.0f, 44.0f,
                                                                -40.0f, 24.0f, -32.0f, 52.0f};
    constexpr std::array<float, 6> verticalVelocityPattern = {0.0f, -18.0f, -12.0f, 8.0f, -6.0f, 14.0f};
    return Eigen::Vector2f(horizontalVelocityPattern[spawnIndex % horizontalVelocityPattern.size()],
                           verticalVelocityPattern[spawnIndex % verticalVelocityPattern.size()]);
}

Eigen::Vector2f calculateBroadPhaseStressInitialVelocity(const size_t spawnIndex)
{
    (void)spawnIndex;
    return Eigen::Vector2f::Zero();
}

Eigen::Vector2f calculateBroadPhaseStressPhaseCenter(const BroadPhaseStressScenarioSettings &settings,
                                                     const size_t phaseIndex)
{
    switch (phaseIndex % 4)
    {
    case 0:
        return Eigen::Vector2f(-settings.phaseOffset, -settings.phaseOffset);
    case 1:
        return Eigen::Vector2f(settings.phaseOffset, -settings.phaseOffset);
    case 2:
        return Eigen::Vector2f(-settings.phaseOffset, settings.phaseOffset);
    case 3:
        return Eigen::Vector2f(settings.phaseOffset, settings.phaseOffset);
    }

    return Eigen::Vector2f::Zero();
}

float calculateRigidBodyContainerAngularVelocity(const size_t spawnIndex)
{
    constexpr std::array<float, 8> angularVelocityPattern = {-1.4f, 0.9f, -0.7f, 1.1f, -1.2f, 0.8f, -0.6f, 1.3f};
    return angularVelocityPattern[spawnIndex % angularVelocityPattern.size()];
}

void createRigidBodyContainerWalls(platformator::Runtime &runtime, const RigidBodyContainerScenarioSettings &settings,
                                   const RigidBodyContainerLayout &layout, const bool attachSprites)
{
    const float halfWidth = layout.interiorWidth * 0.5f;
    const float halfHeight = layout.interiorHeight * 0.5f;
    const float wallThickness = settings.wallThickness;
    const float horizontalWallWidth = layout.interiorWidth + 2.0f * wallThickness;
    const float verticalWallHeight = layout.interiorHeight + 2.0f * wallThickness;

    createBenchmarkBox(runtime, "Container Floor", 0.0f,
                       halfHeight + wallThickness * 0.5f +
                           20.0f, // Add a small margin to prevent initial overlaps with spawned bodies
                       horizontalWallWidth, wallThickness, STATIC, false, Eigen::Vector2f::Zero(), false,
                       settings.friction, settings.restitution, 0.0f, attachSprites);

    createBenchmarkBox(runtime, "Container Ceiling", 0.0f, -halfHeight - wallThickness * 0.5f, horizontalWallWidth,
                       wallThickness, STATIC, false, Eigen::Vector2f::Zero(), false, settings.friction,
                       settings.restitution, 0.0f, attachSprites);

    createBenchmarkBox(runtime, "Container Left Wall", -halfWidth - wallThickness * 0.5f, 0.0f, wallThickness,
                       verticalWallHeight, STATIC, false, Eigen::Vector2f::Zero(), false, settings.friction,
                       settings.restitution, 0.0f, attachSprites);

    createBenchmarkBox(runtime, "Container Right Wall", halfWidth + wallThickness * 0.5f, 0.0f, wallThickness,
                       verticalWallHeight, STATIC, false, Eigen::Vector2f::Zero(), false, settings.friction,
                       settings.restitution, 0.0f, attachSprites);
}

void printRigidBodyContainerScenarioDetails(const RigidBodyContainerScenarioSettings &settings,
                                            const RigidBodyContainerLayout &layout)
{
    std::fprintf(stdout,
                 "[Benchmark][Scenario] box_count=%d circle_count=%d container_width=%.1f container_height=%.1f\n",
                 settings.boxCount, settings.circleCount, layout.interiorWidth, layout.interiorHeight);
}

void printBroadPhaseStressScenarioDetails(const BroadPhaseStressScenarioSettings &settings,
                                          const BroadPhaseStressScenarioLayout &layout)
{
    std::fprintf(stdout,
                 "[Benchmark][Scenario] body_count=%d cluster_width=%.1f cluster_height=%.1f phase_offset=%.1f\n",
                 settings.bodyCount, layout.width, layout.height, settings.phaseOffset);
}

void printAddRemoveScenarioDetails(const AddRemoveScenarioSettings &settings, const AddRemoveScenarioLayout &layout)
{
    std::fprintf(stdout,
                 "[Benchmark][Scenario] steady_count=%d add_count=%d remove_count=%d steady_box_count=%d "
                 "steady_circle_count=%d grid_width=%.1f grid_height=%.1f body_type=%s trigger=%s moving=%s\n",
                 settings.steadyBodyCount, settings.addCountPerFrame, settings.removeCountPerFrame,
                 settings.steadyBoxCount, settings.steadyCircleCount, layout.width, layout.height,
                 bodyTypeName(settings.bodyType), settings.isTrigger ? "true" : "false",
                 settings.movingBodies ? "true" : "false");
}

void buildBroadPhaseScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
{
    const int laneCount = std::max(1, options.broadPhaseElementCount);
    constexpr float laneStepY = 80.0f;
    constexpr float laneWidth = 48.0f;
    constexpr float laneHeight = 48.0f;
    constexpr std::array<float, 16> startXPattern = {-1400.0f, 1470.0f, -1540.0f, 1610.0f, -1680.0f, 1750.0f,
                                                     -1820.0f, 1890.0f, -1960.0f, 2030.0f, -2100.0f, 2170.0f,
                                                     -2240.0f, 2310.0f, -2380.0f, 2450.0f};
    constexpr std::array<float, 16> velocityXPattern = {420.0f,  -500.0f,  580.0f,  -660.0f,  740.0f,  -820.0f,
                                                        900.0f,  -980.0f,  1060.0f, -1140.0f, 1220.0f, -1300.0f,
                                                        1380.0f, -1460.0f, 1540.0f, -1620.0f};
    const float laneStartY = -0.5f * static_cast<float>(laneCount - 1) * laneStepY;

    std::fprintf(stdout, "[Benchmark][Scenario] lane_count=%d\n", laneCount);

    for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        const size_t patternIndex = (static_cast<size_t>(laneIndex) + options.seed) % startXPattern.size();
        createBenchmarkBox(
            runtime, formatIndexedName("Lane Mover", static_cast<size_t>(laneIndex)), startXPattern[patternIndex],
            laneStartY + static_cast<float>(laneIndex) * laneStepY, laneWidth, laneHeight, KINEMATIC, false,
            Eigen::Vector2f(velocityXPattern[patternIndex], 0.0f), false, 0.0f, 0.0f, 0.0f, options.renderFrames);
    }
}

void buildBroadPhaseStressScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options,
                                   BroadPhaseStressScenarioState &state)
{
    state.settings = makeBroadPhaseStressScenarioSettings(options);
    state.layout = calculateBroadPhaseStressScenarioLayout(options);
    state.movingBodies.clear();
    state.movingBodies.reserve(state.layout.totalBodyCount);
    state.phaseIndex = options.seed % 4;

    printBroadPhaseStressScenarioDetails(state.settings, state.layout);

    const Eigen::Vector2f phaseCenter = calculateBroadPhaseStressPhaseCenter(state.settings, state.phaseIndex);

    for (size_t spawnIndex = 0; spawnIndex < state.layout.totalBodyCount; ++spawnIndex)
    {
        const Eigen::Vector2f position =
            phaseCenter + calculateBroadPhaseStressSpawnPosition(state.layout, state.settings, spawnIndex);
        const Eigen::Vector2f velocity = calculateBroadPhaseStressInitialVelocity(spawnIndex + options.seed);

        GameObject *object =
            createBenchmarkBox(runtime, formatIndexedName("Dense Broad Mover", spawnIndex), position.x(), position.y(),
                               state.settings.boxWidth, state.settings.boxHeight, STATIC, false, velocity, true, 0.0f,
                               0.0f, 0.0f, options.renderFrames);

        if (BoxCollider *collider = object->getComponent<BoxCollider>())
        {
            collider->setCollisionMask(0);
        }

        state.movingBodies.push_back(object);
    }
}

void applyBroadPhaseStressScenarioMotion(BroadPhaseStressScenarioState &state)
{
    state.phaseIndex = (state.phaseIndex + 1) % 4;
    const Eigen::Vector2f phaseCenter = calculateBroadPhaseStressPhaseCenter(state.settings, state.phaseIndex);

    for (size_t spawnIndex = 0; spawnIndex < state.movingBodies.size(); ++spawnIndex)
    {
        GameObject *object = state.movingBodies[spawnIndex];

        object->setPosition(phaseCenter +
                            calculateBroadPhaseStressSpawnPosition(state.layout, state.settings, spawnIndex));
    }
}

std::vector<float> generateCenteredAxisPositions(const int count, const float step, const float offset = 0.0f)
{
    std::vector<float> positions;
    const int clampedCount = std::max(1, count);
    positions.reserve(static_cast<size_t>(clampedCount));

    if (clampedCount == 1)
    {
        positions.push_back(offset);
        return positions;
    }

    const float start = -0.5f * static_cast<float>(clampedCount - 1) * step + offset;
    for (int index = 0; index < clampedCount; ++index)
    {
        positions.push_back(start + static_cast<float>(index) * step);
    }

    return positions;
}

std::vector<float> generateCenteredAxisPositionsForExtent(const int count, const float extent,
                                                          const float offset = 0.0f)
{
    const int clampedCount = std::max(1, count);
    if (clampedCount == 1)
    {
        return {offset};
    }

    return generateCenteredAxisPositions(clampedCount, (2.0f * extent) / static_cast<float>(clampedCount - 1), offset);
}

float calculateAxisHalfExtent(const std::vector<float> &positions, const float thickness)
{
    if (positions.empty())
    {
        return 0.5f * thickness;
    }

    return std::max(-positions.front(), positions.back()) + 0.5f * thickness;
}

NarrowPhaseScenarioLayout calculateNarrowPhaseScenarioLayout(const BenchmarkRunnerOptions &options)
{
    NarrowPhaseScenarioLayout layout;
    layout.elementCount = std::max(1, options.narrowPhaseElementCount);

    if (!options.renderFrames)
    {
        layout.columnPositions = generateCenteredAxisPositionsForExtent(layout.elementCount, 125.0f);
        layout.rowPositions = generateCenteredAxisPositionsForExtent(layout.elementCount, 125.0f);
        layout.horizontalBandPositions = generateCenteredAxisPositionsForExtent(layout.elementCount, 140.0f);
        layout.verticalBandPositions = generateCenteredAxisPositionsForExtent(layout.elementCount, 140.0f);
        layout.lineWidth = 360.0f;
        layout.lineHeight = 360.0f;
        return layout;
    }

    layout.staticLaneSpacing = 32.0f;
    layout.bandLaneSpacing = 28.0f;
    layout.linePadding = 72.0f;
    layout.cameraMargin = 96.0f;

    const float bandOffset = layout.elementCount > 1 ? 0.25f * layout.staticLaneSpacing : 0.0f;

    layout.columnPositions = generateCenteredAxisPositions(layout.elementCount, layout.staticLaneSpacing);
    layout.rowPositions = generateCenteredAxisPositions(layout.elementCount, layout.staticLaneSpacing);
    layout.horizontalBandPositions =
        generateCenteredAxisPositions(layout.elementCount, layout.bandLaneSpacing, bandOffset);
    layout.verticalBandPositions =
        generateCenteredAxisPositions(layout.elementCount, layout.bandLaneSpacing, -bandOffset);

    const float horizontalHalfExtent =
        std::max(calculateAxisHalfExtent(layout.columnPositions, layout.columnWidth),
                 calculateAxisHalfExtent(layout.verticalBandPositions, layout.bandThickness));
    const float verticalHalfExtent =
        std::max(calculateAxisHalfExtent(layout.rowPositions, layout.rowHeight),
                 calculateAxisHalfExtent(layout.horizontalBandPositions, layout.bandThickness));

    layout.lineWidth = 2.0f * (horizontalHalfExtent + layout.linePadding);
    layout.lineHeight = 2.0f * (verticalHalfExtent + layout.linePadding);
    return layout;
}

void buildNarrowPhaseScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
{
    const NarrowPhaseScenarioLayout layout = calculateNarrowPhaseScenarioLayout(options);
    constexpr std::array<float, 8> bandSpeedPattern = {20.0f, -20.0f, 24.0f, -24.0f, 20.0f, -20.0f, 24.0f, -24.0f};

    std::fprintf(stdout, "[Benchmark][Scenario] narrow_element_count=%d layout_width=%.1f layout_height=%.1f\n",
                 layout.elementCount, layout.lineWidth, layout.lineHeight);

    for (size_t index = 0; index < layout.columnPositions.size(); ++index)
    {
        createBenchmarkBox(runtime, formatIndexedName("Trigger Column", index), layout.columnPositions[index], 0.0f,
                           layout.columnWidth, layout.lineHeight, STATIC, false, Eigen::Vector2f::Zero(), true, 0.0f,
                           0.0f, 0.0f, options.renderFrames);
    }

    for (size_t index = 0; index < layout.rowPositions.size(); ++index)
    {
        createBenchmarkBox(runtime, formatIndexedName("Trigger Row", index), 0.0f, layout.rowPositions[index],
                           layout.lineWidth, layout.rowHeight, STATIC, false, Eigen::Vector2f::Zero(), true, 0.0f, 0.0f,
                           0.0f, options.renderFrames);
    }

    for (size_t index = 0; index < layout.horizontalBandPositions.size(); ++index)
    {
        createBenchmarkBox(runtime, formatIndexedName("Horizontal Band", index), 0.0f,
                           layout.horizontalBandPositions[index], layout.lineWidth, layout.bandThickness, KINEMATIC,
                           false,
                           Eigen::Vector2f(0.0f, bandSpeedPattern[(index + options.seed) % bandSpeedPattern.size()]),
                           true, 0.0f, 0.0f, 0.0f, options.renderFrames);
    }

    for (size_t index = 0; index < layout.verticalBandPositions.size(); ++index)
    {
        createBenchmarkBox(runtime, formatIndexedName("Vertical Band", index), layout.verticalBandPositions[index],
                           0.0f, layout.bandThickness, layout.lineHeight, KINEMATIC, false,
                           Eigen::Vector2f(bandSpeedPattern[(index + options.seed) % bandSpeedPattern.size()], 0.0f),
                           true, 0.0f, 0.0f, 0.0f, options.renderFrames);
    }
}

void buildRigidBodyContainerScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
{
    const RigidBodyContainerScenarioSettings settings = makeRigidBodyContainerScenarioSettings(options);
    const RigidBodyContainerLayout layout = calculateRigidBodyContainerLayout(options);
    const bool attachSprites = options.renderFrames;

    createRigidBodyContainerWalls(runtime, settings, layout, attachSprites);
    printRigidBodyContainerScenarioDetails(settings, layout);

    int spawnedBoxCount = 0;
    int spawnedCircleCount = 0;
    size_t spawnIndex = 0;
    while (spawnedBoxCount < settings.boxCount || spawnedCircleCount < settings.circleCount)
    {
        const bool shouldSpawnBox =
            spawnedBoxCount < settings.boxCount && (spawnedCircleCount >= settings.circleCount || spawnIndex % 2 == 0);

        const Eigen::Vector2f position = calculateRigidBodyContainerSpawnPosition(layout, settings, spawnIndex);
        const Eigen::Vector2f velocity = calculateRigidBodyContainerInitialVelocity(spawnIndex + options.seed);
        const float angularVelocity = calculateRigidBodyContainerAngularVelocity(spawnIndex);

        if (shouldSpawnBox)
        {
            createBenchmarkBox(runtime, formatIndexedName("Container Box", static_cast<size_t>(spawnedBoxCount)),
                               position.x(), position.y(), settings.boxWidth, settings.boxHeight, DYNAMIC, true,
                               velocity, false, settings.friction, settings.restitution, angularVelocity,
                               attachSprites);
            spawnedBoxCount++;
        }
        else
        {
            createBenchmarkCircle(runtime,
                                  formatIndexedName("Container Circle", static_cast<size_t>(spawnedCircleCount)),
                                  position.x(), position.y(), settings.circleRadius, DYNAMIC, true, velocity, false,
                                  settings.friction, settings.restitution, angularVelocity, attachSprites);
            spawnedCircleCount++;
        }

        spawnIndex++;
    }
}

AddRemoveManagedBody spawnAddRemoveManagedBody(platformator::Runtime &runtime, AddRemoveScenarioState &state,
                                               const size_t slotIndex, const bool attachSprites)
{
    const AddRemoveScenarioSettings &settings = state.settings;
    const Eigen::Vector2f position = calculateAddRemoveSpawnPosition(state.layout, settings, slotIndex);
    const bool useBox = state.slotUsesBox[slotIndex];
    const size_t spawnIndex = state.spawnedBoxCount + state.spawnedCircleCount;
    const Eigen::Vector2f velocity =
        settings.movingBodies ? calculateAddRemoveInitialVelocity(spawnIndex + state.seed) : Eigen::Vector2f::Zero();

    if (useBox)
    {
        return AddRemoveManagedBody{
            createBenchmarkBox(runtime, formatIndexedName("Churn Box", state.spawnedBoxCount++), position.x(),
                               position.y(), settings.boxWidth, settings.boxHeight, settings.bodyType, false, velocity,
                               settings.isTrigger, settings.friction, settings.restitution, 0.0f, attachSprites),
            slotIndex,
        };
    }

    return AddRemoveManagedBody{
        createBenchmarkCircle(runtime, formatIndexedName("Churn Circle", state.spawnedCircleCount++), position.x(),
                              position.y(), settings.circleRadius, settings.bodyType, false, velocity,
                              settings.isTrigger, settings.friction, settings.restitution, 0.0f, attachSprites),
        slotIndex,
    };
}

void buildAddRemoveScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options,
                            AddRemoveScenarioState &state)
{
    state.settings = makeAddRemoveScenarioSettings(options);
    state.seed = options.seed;
    state.layout = calculateAddRemoveScenarioLayout(options);
    state.slotUsesBox = buildAddRemoveSlotShapeMap(state.settings);
    state.activeBodies.clear();
    state.freeSlotIndices.clear();
    state.spawnedBoxCount = 0;
    state.spawnedCircleCount = 0;

    const bool attachSprites = options.renderFrames;
    printAddRemoveScenarioDetails(state.settings, state.layout);

    for (size_t slotIndex = 0; slotIndex < static_cast<size_t>(state.settings.steadyBodyCount); ++slotIndex)
    {
        state.activeBodies.push_back(spawnAddRemoveManagedBody(runtime, state, slotIndex, attachSprites));
    }
}

void applyAddRemoveScenarioChurn(platformator::Runtime &runtime, AddRemoveScenarioState &state,
                                 const bool attachSprites)
{
    const size_t removeCount =
        std::min(static_cast<size_t>(state.settings.removeCountPerFrame), state.activeBodies.size());
    for (size_t index = 0; index < removeCount; ++index)
    {
        const AddRemoveManagedBody removedBody = state.activeBodies.front();
        state.activeBodies.pop_front();
        runtime.destroyGameObject(removedBody.object);
        state.freeSlotIndices.push_back(removedBody.slotIndex);
    }

    const size_t addCount =
        std::min(static_cast<size_t>(state.settings.addCountPerFrame), state.freeSlotIndices.size());
    for (size_t index = 0; index < addCount; ++index)
    {
        const size_t slotIndex = state.freeSlotIndices.back();
        state.freeSlotIndices.pop_back();
        state.activeBodies.push_back(spawnAddRemoveManagedBody(runtime, state, slotIndex, attachSprites));
    }
}

class ContactObserver : public Behavior
{
  public:
    std::string getTypeName() const override
    {
        return "BenchmarkContactObserver";
    }
    void serialize(nlohmann::json &j) const override
    {
        j = nlohmann::json::object();
    }
    void deserialize(const nlohmann::json &) override
    {
    }
    void resolveReferences(platformator::ReferenceResolver &) override
    {
    }
    void onCollisionEnter(const Collision *, Collider *, double) override
    {
    }
    void onCollisionExit(Collider *, double) override
    {
    }
};

void buildPersistentContacts(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
{
    for (int i = 0; i < options.contactPairCount; ++i)
        for (int endpoint = 0; endpoint < 2; ++endpoint)
        {
            auto *object = runtime.createGameObject()
                               ->setName("Contact " + std::to_string(i) + "/" + std::to_string(endpoint))
                               ->setPosition({float(i % 32) * 80 + endpoint, float(i / 32) * 80})
                               ->addComponent<BoxCollider>(4, 4);
            object->getComponent<BoxCollider>()->setIsTrigger(true);
            if (endpoint < options.scriptedEndpoints)
                object->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
                    new ContactObserver);
        }
}

void buildScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options,
                   BroadPhaseStressScenarioState *broadPhaseStressState, AddRemoveScenarioState *addRemoveState,
                   platformator_benchmark::SpatialFixture *spatialFixture)
{
    switch (options.scenario)
    {
    case BenchmarkScenario::BroadPhase:
        buildBroadPhaseScenario(runtime, options);
        return;
    case BenchmarkScenario::BroadPhaseStress:
        if (broadPhaseStressState == nullptr)
        {
            throw std::runtime_error("broad_phase_stress scenario requires state.");
        }
        buildBroadPhaseStressScenario(runtime, options, *broadPhaseStressState);
        return;
    case BenchmarkScenario::NarrowPhase:
        buildNarrowPhaseScenario(runtime, options);
        return;
    case BenchmarkScenario::RigidBodyContainer:
        buildRigidBodyContainerScenario(runtime, options);
        return;
    case BenchmarkScenario::AddAndRemove:
    case BenchmarkScenario::MovingAddAndRemove:
        if (addRemoveState == nullptr)
        {
            throw std::runtime_error("add/remove benchmark scenarios require add/remove state.");
        }
        buildAddRemoveScenario(runtime, options, *addRemoveState);
        return;
    case BenchmarkScenario::PersistentContacts:
        buildPersistentContacts(runtime, options);
        return;
    case BenchmarkScenario::SpriteVisibility:
    case BenchmarkScenario::SpatialCombined:
        if (spatialFixture == nullptr)
            throw std::runtime_error("Spatial visibility scenario requires fixture storage");
        *spatialFixture = platformator_benchmark::buildSpatialFixture(
            runtime, {static_cast<size_t>(options.spriteCount), options.visiblePercent, options.changedPercent,
                      options.spatialVariant, options.scenario == BenchmarkScenario::SpatialCombined});
        return;
    case BenchmarkScenario::None:
        return;
    }
}

SDL_FRect fitCameraRectToWindowAspect(const SDL_FRect &cameraRect, const WindowSettings &windowSettings)
{
    const float renderWidth = static_cast<float>(std::max(1, windowSettings.width));
    const float renderHeight = static_cast<float>(std::max(1, windowSettings.height));
    const float targetAspectRatio = renderWidth / renderHeight;

    if (cameraRect.w <= 0.0f || cameraRect.h <= 0.0f || targetAspectRatio <= 0.0f)
    {
        return cameraRect;
    }

    SDL_FRect adjustedRect = cameraRect;
    const float cameraAspectRatio = cameraRect.w / cameraRect.h;

    if (cameraAspectRatio < targetAspectRatio)
    {
        const float adjustedWidth = cameraRect.h * targetAspectRatio;
        adjustedRect.x -= 0.5f * (adjustedWidth - cameraRect.w);
        adjustedRect.w = adjustedWidth;
    }
    else if (cameraAspectRatio > targetAspectRatio)
    {
        const float adjustedHeight = cameraRect.w / targetAspectRatio;
        adjustedRect.y -= 0.5f * (adjustedHeight - cameraRect.h);
        adjustedRect.h = adjustedHeight;
    }

    return adjustedRect;
}

void configureScenarioCamera(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options,
                             const BroadPhaseStressScenarioState *broadPhaseStressState,
                             const AddRemoveScenarioState *addRemoveState,
                             platformator_benchmark::SpatialFixture *spatialFixture)
{
    if (options.renderFrames == false)
    {
        return;
    }

    runtime.createMainCameraIfNoMainCameraExists();
    Camera *mainCamera = runtime.getMainCamera();
    if (mainCamera == nullptr)
    {
        return;
    }

    switch (options.scenario)
    {
    case BenchmarkScenario::BroadPhase: {
        const float laneStepY = 80.0f;
        const float laneCount = static_cast<float>(std::max(1, options.broadPhaseElementCount));
        const float cameraHeight = std::max(600.0f, laneCount * laneStepY + 320.0f);
        mainCamera->setCamera(fitCameraRectToWindowAspect(
            SDL_FRect{-2600.0f, -0.5f * cameraHeight, 5200.0f, cameraHeight}, options.runtimeOptions.windowSettings));
        return;
    }
    case BenchmarkScenario::BroadPhaseStress: {
        if (broadPhaseStressState == nullptr)
        {
            return;
        }

        const BroadPhaseStressScenarioSettings &settings = broadPhaseStressState->settings;
        const BroadPhaseStressScenarioLayout &layout = broadPhaseStressState->layout;
        const float cameraWidth = layout.width + 2.0f * (settings.phaseOffset + settings.cameraMargin);
        const float cameraHeight = layout.height + 2.0f * (settings.phaseOffset + settings.cameraMargin);
        mainCamera->setCamera(
            fitCameraRectToWindowAspect(SDL_FRect{-0.5f * cameraWidth, -0.5f * cameraHeight, cameraWidth, cameraHeight},
                                        options.runtimeOptions.windowSettings));
        return;
    }
    case BenchmarkScenario::NarrowPhase: {
        const NarrowPhaseScenarioLayout layout = calculateNarrowPhaseScenarioLayout(options);
        const float cameraWidth = layout.lineWidth + 2.0f * layout.cameraMargin;
        const float cameraHeight = layout.lineHeight + 2.0f * layout.cameraMargin;
        mainCamera->setCamera(
            fitCameraRectToWindowAspect(SDL_FRect{-0.5f * cameraWidth, -0.5f * cameraHeight, cameraWidth, cameraHeight},
                                        options.runtimeOptions.windowSettings));
        return;
    }
    case BenchmarkScenario::RigidBodyContainer: {
        const RigidBodyContainerScenarioSettings settings = makeRigidBodyContainerScenarioSettings(options);
        const RigidBodyContainerLayout layout = calculateRigidBodyContainerLayout(options);
        const float cameraWidth = layout.interiorWidth + 2.0f * settings.wallThickness + 2.0f * settings.cameraMargin;
        const float cameraHeight = layout.interiorHeight + 2.0f * settings.wallThickness + 2.0f * settings.cameraMargin;
        mainCamera->setCamera(
            fitCameraRectToWindowAspect(SDL_FRect{-0.5f * cameraWidth, -0.5f * cameraHeight, cameraWidth, cameraHeight},
                                        options.runtimeOptions.windowSettings));
        return;
    }
    case BenchmarkScenario::AddAndRemove:
    case BenchmarkScenario::MovingAddAndRemove: {
        if (addRemoveState == nullptr)
        {
            return;
        }

        const AddRemoveScenarioSettings &settings = addRemoveState->settings;
        const AddRemoveScenarioLayout &layout = addRemoveState->layout;
        const float cameraWidth = layout.width + 2.0f * settings.cameraMargin;
        const float cameraHeight = layout.height + 2.0f * settings.cameraMargin;
        mainCamera->setCamera(
            fitCameraRectToWindowAspect(SDL_FRect{-0.5f * cameraWidth, -0.5f * cameraHeight, cameraWidth, cameraHeight},
                                        options.runtimeOptions.windowSettings));
        return;
    }
    case BenchmarkScenario::PersistentContacts:
        mainCamera->setCamera(SDL_FRect{-10, -10, 2600, 2600});
        return;
    case BenchmarkScenario::SpriteVisibility:
    case BenchmarkScenario::SpatialCombined:
        if (spatialFixture == nullptr)
            throw std::runtime_error("Spatial visibility scenario requires fixture storage");
        platformator_benchmark::configureSpatialFixtureCamera(runtime, *spatialFixture);
        return;
    case BenchmarkScenario::None:
        return;
    }
}

int parseNonNegativeInteger(const std::string &value, const std::string &argumentName)
{
    int parsedValue = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || parsedValue < 0)
    {
        throw std::runtime_error(argumentName + " must be greater than or equal to zero.");
    }

    return parsedValue;
}

int parsePositiveInteger(const std::string &value, const std::string &argumentName)
{
    const int parsedValue = parseNonNegativeInteger(value, argumentName);
    if (parsedValue <= 0)
    {
        throw std::runtime_error(argumentName + " must be greater than zero.");
    }

    return parsedValue;
}

double parsePositiveDouble(const std::string &value, const std::string &argumentName)
{
    size_t consumed = 0;
    const double parsedValue = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(parsedValue) || parsedValue <= 0.0)
    {
        throw std::runtime_error(argumentName + " must be greater than zero.");
    }

    return parsedValue;
}

std::string requireArgumentValue(int argc, char *args[], int &index, const std::string &argumentName)
{
    if (index + 1 >= argc)
    {
        throw std::runtime_error("Missing value for " + argumentName + ".");
    }

    index++;
    return args[index];
}

BenchmarkRunnerOptions parseBenchmarkRunnerOptions(int argc, char *args[])
{
    BenchmarkRunnerOptions options;
    bool scenePathSpecified = false;

    for (int index = 1; index < argc; index++)
    {
        const std::string argument = args[index];
        if (platformator_detail::isSpatialSettingsOption(argument))
        {
            platformator_detail::parseSpatialSettingsOption(
                argument, requireArgumentValue(argc, args, index, argument), options.runtimeOptions.spatialSettings);
            continue;
        }
        if (argument == "--contact-pair-count")
        {
            options.contactPairCount =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--sprite-count")
        {
            options.spriteCount = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--visible-percent" || argument == "--changed-percent")
        {
            const auto value = requireArgumentValue(argc, args, index, argument);
            size_t consumed = 0;
            const double parsed = std::stod(value, &consumed);
            if (consumed != value.size() || !std::isfinite(parsed) || parsed < 0.0 || parsed > 100.0)
                throw std::runtime_error(argument + " must be between 0 and 100");
            if (argument == "--visible-percent")
                options.visiblePercent = parsed;
            else
                options.changedPercent = parsed;
            continue;
        }
        if (argument == "--spatial-variant")
        {
            options.spatialVariant = requireArgumentValue(argc, args, index, argument);
            if (options.spatialVariant != "static" && options.spatialVariant != "moving" &&
                options.spatialVariant != "pan" && options.spatialVariant != "jump" &&
                options.spatialVariant != "resize" && options.spatialVariant != "animation" &&
                options.spatialVariant != "churn")
                throw std::runtime_error("Unsupported --spatial-variant: " + options.spatialVariant);
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
        if (argument == "--capture-mode")
        {
            options.captureMode = requireArgumentValue(argc, args, index, argument);
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
            if (options.captureMode != "resource")
                throw std::runtime_error("platformator_spatial_resource_runner only supports --capture-mode resource");
#else
            if (options.captureMode != "performance" && options.captureMode != "validate")
                throw std::runtime_error("--capture-mode must be performance or validate; use the resource runner for resource captures");
#endif
            options.captureModeExplicit = true;
            continue;
        }
        if (argument == "--case-id")
        {
            options.caseId = requireArgumentValue(argc, args, index, argument);
            continue;
        }
        if (argument == "--case-sha256")
        {
            options.caseSha256 = requireArgumentValue(argc, args, index, argument);
            if (options.caseSha256.size() != 64 ||
                !std::all_of(options.caseSha256.begin(), options.caseSha256.end(), [](unsigned char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }))
                throw std::runtime_error("--case-sha256 must be 64 lowercase hexadecimal characters");
            continue;
        }
        if (argument == "--spatial-trace")
        {
            if (!options.spatialTracePath.empty())
                throw std::runtime_error("--spatial-trace may be specified only once");
            options.spatialTracePath = requireArgumentValue(argc, args, index, argument);
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
            options.workers = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            continue;
        }
        if (argument == "--seed")
        {
            const auto value = requireArgumentValue(argc, args, index, argument);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.seed);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                throw std::runtime_error("--seed must be a nonnegative integer");
            options.seedExplicit = true;
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
            continue;
        }

        if (argument == "--warmup-frames")
        {
            options.warmupFrameCount =
                parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
            options.warmupExplicit = true;
            continue;
        }

        if (argument == "--measure-frames")
        {
            options.measureFrameCount =
                parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
            options.measureExplicit = true;
            continue;
        }

        if (argument == "--dt")
        {
            options.timeDelta = parsePositiveDouble(requireArgumentValue(argc, args, index, argument), argument);
            options.dtExplicit = true;
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

        if (argument == "--render")
        {
            options.renderFrames = true;
            options.runtimeOptions.windowSettings.headless = false;
            options.runtimeOptions.windowSettings.width = 1600;
            options.runtimeOptions.windowSettings.height = 900;
            options.runtimeOptions.windowSettings.keepAspectRatio = true;
            // options.runtimeOptions.debugSettings.startPaused = false;
            // options.runtimeOptions.debugSettings.showColliders = true;
            // options.runtimeOptions.debugSettings.showCollisionPoints = true;
            // options.runtimeOptions.debugSettings.showCollisionNormals = true;
            // options.runtimeOptions.debugSettings.showGridCells = true;
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

    if (options.scenario == BenchmarkScenario::RigidBodyContainer && options.boxCount + options.circleCount <= 0)
    {
        throw std::runtime_error("rigid_body_container scenario requires at least one box or circle.");
    }

    if ((options.scenario == BenchmarkScenario::SpriteVisibility ||
         options.scenario == BenchmarkScenario::SpatialCombined) && !options.renderFrames)
        throw std::runtime_error("sprite_visibility and spatial_combined require --render");

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

    if (options.validateInvariants && options.captureModeExplicit && options.captureMode != "validate")
        throw std::runtime_error("--validate-invariants conflicts with --capture-mode performance");
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
    if (!options.captureModeExplicit)
        options.captureMode = "resource";
    if (options.captureMode != "resource" || options.validateInvariants)
        throw std::runtime_error("platformator_spatial_resource_runner is resource-only; omit validation flags and use --capture-mode resource");
#endif
    if (options.captureMode == "validate")
        options.validateInvariants = true;
    if (!options.spatialTracePath.empty())
    {
        if (options.scenario != BenchmarkScenario::None || scenePathSpecified)
            throw std::runtime_error("--spatial-trace cannot be combined with a scene or --scenario");
        if (options.renderFrames)
            throw std::runtime_error("--spatial-trace is headless replay and cannot be combined with --render");
    }

    return options;
}

nlohmann::json resolvedParameters(const BenchmarkRunnerOptions &options)
{
    nlohmann::json result = {
        {"gravity", {GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y}},
        {"solver_iterations", COLLISION_SOLVER_ITERATIONS},
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
    if (options.scenario == BenchmarkScenario::SpriteVisibility ||
        options.scenario == BenchmarkScenario::SpatialCombined)
    {
        const auto rounded = [](int count, double percent) {
            return static_cast<size_t>(std::llround(static_cast<double>(count) * percent / 100.0));
        };
        result.update({{"sprite_count", options.spriteCount},
                       {"visible_percent", options.visiblePercent},
                       {"visible_count", rounded(options.spriteCount, options.visiblePercent)},
                       {"changed_percent", options.changedPercent},
                       {"changed_count", rounded(options.spriteCount, options.changedPercent)},
                       {"variant", options.spatialVariant},
                       {"combined_collider_count", options.scenario == BenchmarkScenario::SpatialCombined ? 128 : 0},
                       {"sprite_geometry", {16, 16}},
                       {"camera_view", {1600, 900}}});
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
    case BenchmarkScenario::SpriteVisibility:
        return size_t(options.spriteCount) + camera;
    case BenchmarkScenario::SpatialCombined:
        return size_t(options.spriteCount) + 128 + camera;
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
    size_t colliders = 0, bodies = 0, supportTotal = 0, maximumSupports = 0;
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
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
    platformator_detail::spatialResourceStart();
#endif
    const auto initializationStarted = std::chrono::steady_clock::now();
    platformator::Runtime runtime(options.runtimeOptions);
    auto &recorder = platformator_detail::BenchmarkRecorder::getInstance();
    recorder.setInitializationActive(true);
    AddRemoveScenarioState addRemoveState;
    BroadPhaseStressScenarioState broadPhaseStressState;
    platformator_benchmark::SpatialFixture spatialFixture;
    const bool spatialFixtureScenario = options.scenario == BenchmarkScenario::SpriteVisibility ||
                                        options.scenario == BenchmarkScenario::SpatialCombined;
    const platformator_benchmark::SpatialFixtureSettings spatialFixtureSettings{
        static_cast<size_t>(options.spriteCount), options.visiblePercent, options.changedPercent,
        options.spatialVariant, options.scenario == BenchmarkScenario::SpatialCombined};
    std::function<void(double)> callback;
    if (options.scenario == BenchmarkScenario::None)
        runtime.loadScene(options.runtimeOptions.sceneFilePath);
    else
        buildScenario(runtime, options,
                      options.scenario == BenchmarkScenario::BroadPhaseStress ? &broadPhaseStressState : nullptr,
                      isAddRemoveScenario(options.scenario) ? &addRemoveState : nullptr,
                      spatialFixtureScenario ? &spatialFixture : nullptr);
    configureScenarioCamera(runtime, options,
                            options.scenario == BenchmarkScenario::BroadPhaseStress ? &broadPhaseStressState : nullptr,
                            isAddRemoveScenario(options.scenario) ? &addRemoveState : nullptr,
                            spatialFixtureScenario ? &spatialFixture : nullptr);
    if (isAddRemoveScenario(options.scenario))
        callback = [&](double) { applyAddRemoveScenarioChurn(runtime, addRemoveState, options.renderFrames); };
    else if (options.scenario == BenchmarkScenario::BroadPhaseStress)
        callback = [&](double) { applyBroadPhaseStressScenarioMotion(broadPhaseStressState); };
    else if (spatialFixtureScenario)
        callback = [&](double dt) {
            platformator_benchmark::stepSpatialFixture(runtime, spatialFixture, spatialFixtureSettings, dt);
        };
    const auto initializationNanoseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - initializationStarted).count());
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
    const auto recordOwnedCapacitySnapshot = [] {
        auto *physics = platformator_detail::RuntimeAccess::physicsManager();
        platformator_detail::spatialResourceSetOwnedCapacity(
            platformator_detail::SpatialResourceTag::PhysicsIndex, physics->getSpatialIndexOwnedCapacityBytes());
        platformator_detail::spatialResourceSetOwnedCapacity(
            platformator_detail::SpatialResourceTag::PhysicsCommonPairs, physics->getSpatialCommonPairCapacityBytes());
        platformator_detail::spatialResourceSetOwnedCapacity(
            platformator_detail::SpatialResourceTag::Scratch, physics->getSpatialScratchCapacityBytes());
        platformator_detail::spatialResourceSetOwnedCapacity(
            platformator_detail::SpatialResourceTag::VisibilityIndex,
            platformator_detail::RuntimeAccess::window()->getRenderCullingOwnedCapacityBytes());
    };
    recordOwnedCapacitySnapshot();
    recorder.setInitializationActive(false);
    platformator_detail::spatialResourceMarkInitial();
#else
    recorder.setInitializationActive(false);
#endif
    platformator_detail::RuntimeAccess::window()->setImageValidationEnabled(options.validateInvariants && options.renderFrames);
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
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
        recordOwnedCapacitySnapshot();
#endif
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
    const auto initial = validatePhysicalState(runtime, options, false, warmup != 0);
    recorder.configure(options.measureFrameCount,
                       {{"scenario", scenarioName(options.scenario)},
                        {"case_id", options.caseId.empty() ? scenarioName(options.scenario) : options.caseId},
                        {"case_sha256", options.caseSha256},
                        {"seed", options.seed},
                        {"workers", tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism)},
                        {"dt", options.timeDelta},
                        {"warmup_steps", warmup},
                        {"measured_steps", options.measureFrameCount},
                        {"repetitions", options.repetitions},
                        {"repetition_index", options.runIndex},
                        {"rendering", options.renderFrames},
                        {"initialization_ns", initializationNanoseconds},
                        {"renderer_signature", platformator_detail::RuntimeAccess::window()->getRendererSignature()},
                        {"headless", platformator_detail::RuntimeAccess::window()->isHeadless()},
                        {"camera_available", platformator_detail::RuntimeAccess::window()->getMainCamera() != nullptr},
                        {"spatial_settings", spatialSelectionMetadata(options)},
                        {"validation_mode", options.validateInvariants ? "per-step" : "boundary-only"},
                        {"capture_mode", options.captureMode},
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
                        {"resource_hooks", true},
#else
                        {"resource_hooks", false},
#endif
                        {"timing_eligible", !options.validateInvariants},
                        {"asset_root", runtime.getAssetRoot().string()},
                        {"resolved_parameters", resolvedParameters(options)},
                        {"initial_state", initial}});
    for (int i = 0; running && i < options.measureFrameCount; ++i)
        running = step();
    const auto final = validatePhysicalState(runtime, options, options.validateInvariants);
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
    recordOwnedCapacitySnapshot();
#endif
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
    recorder.updateMetadata({{"image_validation", platformator_detail::RuntimeAccess::window()->getImageValidationProof()},
                             {"physical_invariants", final}, {"stopped_on_quit", !running},
                             {"render_fallback_reason",
                              platformator_detail::RuntimeAccess::window()->getRenderCullingStats().fallbackReason}});
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
    recorder.updateMetadata({{"resource_data", platformator_detail::spatialResourceFinish()}});
#endif
    if (!options.csvOutputPath.empty())
        recorder.writeOutputs(options.csvOutputPath);
    recorder.printSummary();
}

void runSpatialReplayBenchmark(const BenchmarkRunnerOptions &options)
{
    if (options.csvOutputPath.empty())
        throw std::runtime_error("--spatial-trace requires --csv-output so the replay capture is retained");
    const auto trace = platformator_detail::parseSpatialTraceFile(options.spatialTracePath);
    const auto &header = trace.header;
    if (options.warmupExplicit && static_cast<size_t>(options.warmupFrameCount) != header.warmupSteps)
        throw std::runtime_error("--warmup-frames contradicts the spatial trace header");
    if (options.measureExplicit && static_cast<size_t>(options.measureFrameCount) != header.measureSteps)
        throw std::runtime_error("--measure-frames contradicts the spatial trace header");
    if (options.dtExplicit && std::abs(options.timeDelta - header.dt) >
                                 std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(header.dt)) * 4.0)
        throw std::runtime_error("--dt contradicts the spatial trace header");
    if (options.seedExplicit && options.seed != header.seed)
        throw std::runtime_error("--seed contradicts the spatial trace header");

    tbb::global_control workers(tbb::global_control::max_allowed_parallelism, options.workers);
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
    platformator_detail::spatialResourceStart();
#endif
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
    const auto resourceCapacityObserver = [](const platformator_detail::SpatialReplayOwnedCapacitySnapshot &snapshot,
                                             bool initial, void *) {
        using platformator_detail::SpatialResourceTag;
        platformator_detail::spatialResourceSetOwnedCapacity(SpatialResourceTag::PhysicsIndex,
                                                              snapshot.physicsIndexBytes);
        platformator_detail::spatialResourceSetOwnedCapacity(SpatialResourceTag::PhysicsCommonPairs,
                                                              snapshot.physicsCommonPairsBytes);
        platformator_detail::spatialResourceSetOwnedCapacity(SpatialResourceTag::VisibilityIndex,
                                                              snapshot.visibilityIndexBytes);
        platformator_detail::spatialResourceSetOwnedCapacity(SpatialResourceTag::Scratch, snapshot.scratchBytes);
        if (initial)
            platformator_detail::spatialResourceMarkInitial();
    };
    const auto replay = platformator_detail::executeSpatialReplay(
        trace, options.runtimeOptions.spatialSettings, options.validateInvariants, resourceCapacityObserver);
    auto resourceData = platformator_detail::spatialResourceFinish();
    resourceData["owned_capacity_snapshots"] = {
        {"status", "complete"},
        {"sampling", "after initialization and each complete replay step, outside measured intervals"},
        {"source", "spatial owner capacity APIs; fixture records and allocator metadata are excluded"},
        {"domains", trace.header.domain == platformator_detail::SpatialTraceDomain::Collision
                        ? nlohmann::json::array({"physics_index", "physics_common_pairs", "scratch"})
                        : nlohmann::json::array({"visibility_index"})}};
#else
    const auto replay = platformator_detail::executeSpatialReplay(
        trace, options.runtimeOptions.spatialSettings, options.validateInvariants);
    nlohmann::json resourceData = nlohmann::json::object();
#endif
    platformator_detail::writeSpatialReplayOutputs(options.csvOutputPath, options.spatialTracePath, trace,
                                                    options.runtimeOptions.spatialSettings, replay,
                                                    options.validateInvariants,
#if defined(PLATFORMATOR_RESOURCE_RUNNER)
                                                    true,
#else
                                                    false,
#endif
                                                    std::move(resourceData));
    std::fprintf(stdout, "[Benchmark] schema_version=4 replay_domain=%s measured_steps=%zu initialization_ns=%llu\n",
                 header.domain == platformator_detail::SpatialTraceDomain::Collision ? "collision" : "visibility",
                 replay.measuredSamples.size(), static_cast<unsigned long long>(replay.initializationNanoseconds));
}

void printUsage(FILE *output)
{
    std::fprintf(
        output,
        "Usage: platformator_benchmark_runner [scene] [--scene path | --scenario "
        "broad_phase|broad_phase_stress|narrow_phase|rigid_body_container|add_and_remove|moving_add_and_remove|"
        "persistent_contacts] "
        "[--warmup-frames N] [--measure-frames N] [--dt seconds] [--box-count N] [--circle-count N] [--broad-count N] "
        "[--contact-pair-count N] [--scripted-endpoints 0|1|2] [--narrow-count N] [--steady-count N] [--add-count N] "
        "[--remove-count N] [--churn-count N] [--csv-output "
        "path] [--workers N] [--seed N] [--capture-mode performance|validate] [--validate-invariants] "
        "[--spatial-trace file] [--asset-root path] [--render] "
        "[--broad-phase segmented|bvh] [--render-culling none|linear|bvh|segmented] "
        "[--bvh-maintenance native-partial|full-rebuild] [--bvh-padding units] "
        "[--bvh-query-execution serial|parallel]\n");
    std::fprintf(output, "--render-culling selects visibility independently; it does not enable --render.\n");
}
} // namespace

int main(int argc, char *args[])
{
    try
    {
        const auto options = parseBenchmarkRunnerOptions(argc, args);
        if (options.spatialTracePath.empty())
            runBenchmark(options);
        else
            runSpatialReplayBenchmark(options);
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::fprintf(stderr, "%s\n", exception.what());
        printUsage(stderr);
        return 1;
    }
}
