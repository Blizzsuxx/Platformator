// Scenario definitions, deterministic layouts, and builders for platformator_benchmark_runner.
// Included inside benchmark_main.cpp's anonymous namespace.
#pragma once

constexpr const char *DEFAULT_SCENE_PATH = "assets/scenes/default.scene";
constexpr int DEFAULT_CONTAINER_BOX_COUNT = 400;
constexpr int DEFAULT_CONTAINER_CIRCLE_COUNT = 400;
constexpr int DEFAULT_BROAD_PHASE_ELEMENT_COUNT = 128;
constexpr int DEFAULT_NARROW_PHASE_ELEMENT_COUNT = 8;
constexpr int DEFAULT_ADD_REMOVE_STEADY_BODY_COUNT = 1000;
constexpr int DEFAULT_ADD_REMOVE_CHURN_COUNT = 10;
constexpr int DEFAULT_IDEAL_WORLD_BODY_COUNT = 1000;
constexpr int IDEAL_WORLD_BODIES_PER_CHAMBER = 20;

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
    IdealWorld,
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
    int idealWorldBodyCount = DEFAULT_IDEAL_WORLD_BODY_COUNT;
};

nlohmann::json renderCullingMetadata(const BenchmarkRunnerOptions &options, const SDLWindow *window = nullptr,
                                     const SDLWindow::RenderFallbackCounts &observedFallbacks = {})
{
    const auto policy = window != nullptr
                            ? window->getRenderCullingPolicy()
                            : SDLWindow::resolveRenderCullingPolicy(options.runtimeOptions.windowSettings);
    const bool rendering = options.renderFrames && !policy.headless;
    const char *selectorName = SDLWindow::renderCullingModeName(policy.requestedMode);
    const char *startupStrategy =
        !rendering ? "headless-no-rendering"
                   : (!policy.cullingEnabled ? "submit-all" : SDLWindow::renderCullingModeName(policy.effectiveMode));
    return {{"requested_selector", selectorName},
            {"resolved_startup_strategy", startupStrategy},
            {"requested_mode", policy.cullingEnabled ? selectorName : "disabled"},
            {"requested_enabled", policy.cullingEnabled},
            {"headless", !rendering},
            {"disabled", !policy.cullingEnabled},
            {"effective_policy", startupStrategy},
            {"effective_enabled", rendering && policy.cullingEnabled},
            {"observed_mapping_fallbacks", observedFallbacks.mappingFallbacks},
            {"observed_numeric_fallbacks", observedFallbacks.numericFallbacks},
            {"observed_frame_fallbacks",
             observedFallbacks.mappingFallbacks + observedFallbacks.numericFallbacks}};
}

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

    if (value == "ideal_world" || value == "ideal-world" || value == "idealworld")
        return BenchmarkScenario::IdealWorld;

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
    case BenchmarkScenario::IdealWorld:
        return "ideal_world";
    case BenchmarkScenario::None:
        return "scene";
    }

    return "scene";
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
    const size_t minColumnCount = std::max<size_t>(1, static_cast<size_t>(usableWidth / cellSize));
    const double aspectRatio =
        static_cast<double>(settings.minimumInteriorWidth) / static_cast<double>(settings.minimumInteriorHeight);
    const size_t scaledColumnCount = std::max<size_t>(
        1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(totalBodyCount) * aspectRatio))));
    const size_t columnCount = std::max(minColumnCount, scaledColumnCount);
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
            if (options.renderFrames)
            {
                const char *texturePath =
                    endpoint == 0 ? "assets/textures/black_square.png" : "assets/textures/red_circle.png";
                object->addComponent<Sprite>(texturePath, 4.0f, 4.0f);
            }
            if (endpoint < options.scriptedEndpoints)
                object->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
                    new ContactObserver);
        }
}

size_t idealWorldChamberCount(const int totalBodyCount)
{
    const size_t clamped = static_cast<size_t>(std::max(1, totalBodyCount));
    return (clamped + static_cast<size_t>(IDEAL_WORLD_BODIES_PER_CHAMBER) - 1) /
           static_cast<size_t>(IDEAL_WORLD_BODIES_PER_CHAMBER);
}

void buildIdealWorldScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
{
    const size_t totalBodies = static_cast<size_t>(std::max(1, options.idealWorldBodyCount));
    const size_t chamberCount = idealWorldChamberCount(options.idealWorldBodyCount);
    const size_t chamberCols =
        std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(chamberCount)))));
    const size_t chamberRows = std::max<size_t>(1, (chamberCount + chamberCols - 1) / chamberCols);

    constexpr float chamberInterior = 300.0f;
    constexpr float wallThickness = 24.0f;
    constexpr float chamberPitch = 600.0f; // 3 * GRID_CELL_SIZE keeps neighboring chambers in disjoint grid cells
    constexpr float boxSize = 24.0f;
    constexpr float circleRadius = 12.0f;
    constexpr float bodyStep = 46.0f;
    constexpr float friction = 0.8f;
    constexpr float restitution = 0.15f;
    const bool attachSprites = options.renderFrames;

    const float worldOriginX = -0.5f * static_cast<float>(chamberCols - 1) * chamberPitch;
    const float worldOriginY = -0.5f * static_cast<float>(chamberRows - 1) * chamberPitch;

    std::fprintf(stdout,
                 "[Benchmark][Scenario] ideal_world dynamic_bodies=%zu chambers=%zu static_walls=%zu pitch=%.1f\n",
                 totalBodies, chamberCount, chamberCount * 4, chamberPitch);

    size_t spawned = 0;
    for (size_t chamberIndex = 0; chamberIndex < chamberCount; ++chamberIndex)
    {
        const size_t cCol = chamberIndex % chamberCols;
        const size_t cRow = chamberIndex / chamberCols;
        const float cx = worldOriginX + static_cast<float>(cCol) * chamberPitch;
        const float cy = worldOriginY + static_cast<float>(cRow) * chamberPitch;
        const float halfInterior = 0.5f * chamberInterior;
        const float fullSpan = chamberInterior + 2.0f * wallThickness;

        createBenchmarkBox(runtime, formatIndexedName("Chamber Floor", chamberIndex), cx,
                           cy + halfInterior + 0.5f * wallThickness, fullSpan, wallThickness, STATIC, false,
                           Eigen::Vector2f::Zero(), false, friction, restitution, 0.0f, attachSprites);
        createBenchmarkBox(runtime, formatIndexedName("Chamber Ceiling", chamberIndex), cx,
                           cy - halfInterior - 0.5f * wallThickness, fullSpan, wallThickness, STATIC, false,
                           Eigen::Vector2f::Zero(), false, friction, restitution, 0.0f, attachSprites);
        createBenchmarkBox(runtime, formatIndexedName("Chamber Left", chamberIndex),
                           cx - halfInterior - 0.5f * wallThickness, cy, wallThickness, fullSpan, STATIC, false,
                           Eigen::Vector2f::Zero(), false, friction, restitution, 0.0f, attachSprites);
        createBenchmarkBox(runtime, formatIndexedName("Chamber Right", chamberIndex),
                           cx + halfInterior + 0.5f * wallThickness, cy, wallThickness, fullSpan, STATIC, false,
                           Eigen::Vector2f::Zero(), false, friction, restitution, 0.0f, attachSprites);

        for (size_t localIndex = 0;
             localIndex < static_cast<size_t>(IDEAL_WORLD_BODIES_PER_CHAMBER) && spawned < totalBodies;
             ++localIndex, ++spawned)
        {
            const size_t bCol = localIndex % 5;
            const size_t bRow = localIndex / 5;
            const float jitterX = static_cast<float>(static_cast<int>(spawned % 3) - 1) * 1.5f;
            const float jitterY = static_cast<float>(static_cast<int>(spawned % 4) - 1) * 1.0f;
            const float bx = cx + (static_cast<float>(bCol) - 2.0f) * bodyStep + jitterX;
            const float by = cy + (static_cast<float>(bRow) - 1.5f) * bodyStep + jitterY;
            const Eigen::Vector2f velocity = calculateRigidBodyContainerInitialVelocity(spawned + options.seed);
            const float angularVelocity = calculateRigidBodyContainerAngularVelocity(spawned);

            if (spawned % 2 == 0)
            {
                createBenchmarkBox(runtime, formatIndexedName("Ideal Box", spawned), bx, by, boxSize, boxSize, DYNAMIC,
                                   true, velocity, false, friction, restitution, angularVelocity, attachSprites);
            }
            else
            {
                createBenchmarkCircle(runtime, formatIndexedName("Ideal Circle", spawned), bx, by, circleRadius,
                                      DYNAMIC, true, velocity, false, friction, restitution, angularVelocity,
                                      attachSprites);
            }
        }
    }
}

void buildScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options,
                   BroadPhaseStressScenarioState *broadPhaseStressState, AddRemoveScenarioState *addRemoveState)
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
    case BenchmarkScenario::IdealWorld:
        buildIdealWorldScenario(runtime, options);
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
                             const AddRemoveScenarioState *addRemoveState)
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
    case BenchmarkScenario::PersistentContacts: {
        const int count = std::max(1, options.contactPairCount);
        const int cols = std::min(32, count);
        const int rows = (count + 31) / 32;
        const float maxX = static_cast<float>(cols - 1) * 80.0f + 5.0f;
        const float maxY = static_cast<float>(rows - 1) * 80.0f + 4.0f;
        const float pad = 40.0f;
        mainCamera->setCamera(fitCameraRectToWindowAspect(
            SDL_FRect{-pad, -pad, std::max(120.0f, maxX + 2.0f * pad), std::max(120.0f, maxY + 2.0f * pad)},
            options.runtimeOptions.windowSettings));
        return;
    }
    case BenchmarkScenario::IdealWorld: {
        const size_t chamberCount = idealWorldChamberCount(options.idealWorldBodyCount);
        const size_t cols =
            std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(chamberCount)))));
        const size_t rows = std::max<size_t>(1, (chamberCount + cols - 1) / cols);
        const float w = static_cast<float>(cols) * 600.0f + 400.0f;
        const float h = static_cast<float>(rows) * 600.0f + 400.0f;
        mainCamera->setCamera(
            fitCameraRectToWindowAspect(SDL_FRect{-0.5f * w, -0.5f * h, w, h}, options.runtimeOptions.windowSettings));
        return;
    }
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

