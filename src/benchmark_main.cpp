#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark.h"
#include "constants.h"
#include "platformator/boxcollider.h"
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
        NarrowPhase,
        RigidBodyContainer,
    };

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
        bool renderFrames = false;
        std::string csvOutputPath;
    };

    BenchmarkScenario parseScenario(const std::string &value)
    {
        if (value == "broad_phase" || value == "broad-phase" || value == "broadphase")
        {
            return BenchmarkScenario::BroadPhase;
        }

        if (value == "narrow_phase" || value == "narrow-phase" || value == "narrowphase")
        {
            return BenchmarkScenario::NarrowPhase;
        }

        if (value == "rigid_body_container" || value == "rigid-body-container" || value == "rigidbody_container" || value == "rigidbody-container" || value == "container")
        {
            return BenchmarkScenario::RigidBodyContainer;
        }

        throw std::runtime_error("Unknown benchmark scenario: " + value);
    }

    const char *scenarioName(const BenchmarkScenario scenario)
    {
        switch (scenario)
        {
        case BenchmarkScenario::BroadPhase:
            return "broad_phase";
        case BenchmarkScenario::NarrowPhase:
            return "narrow_phase";
        case BenchmarkScenario::RigidBodyContainer:
            return "rigid_body_container";
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

    GameObject *createBenchmarkBox(
        platformator::Runtime &runtime,
        const std::string &name,
        const float x,
        const float y,
        const float width,
        const float height,
        const BodyType bodyType,
        const bool gravity,
        const Eigen::Vector2f &velocity,
        const bool isTrigger,
        const float friction = 0.0f,
        const float restitution = 0.0f,
        const float angularVelocity = 0.0f,
        const bool attachSprite = false)
    {
        GameObject *object = runtime
                                 .createGameObject()
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

    GameObject *createBenchmarkCircle(
        platformator::Runtime &runtime,
        const std::string &name,
        const float x,
        const float y,
        const float radius,
        const BodyType bodyType,
        const bool gravity,
        const Eigen::Vector2f &velocity,
        const bool isTrigger,
        const float friction = 0.0f,
        const float restitution = 0.0f,
        const float angularVelocity = 0.0f,
        const bool attachSprite = false)
    {
        GameObject *object = runtime
                                 .createGameObject()
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

    RigidBodyContainerLayout calculateRigidBodyContainerLayout(const BenchmarkRunnerOptions &options)
    {
        const RigidBodyContainerScenarioSettings settings = makeRigidBodyContainerScenarioSettings(options);
        const size_t totalBodyCount = static_cast<size_t>(settings.boxCount + settings.circleCount);
        const float maxBodyExtent = std::max({settings.boxWidth, settings.boxHeight, settings.circleRadius * 2.0f});
        const float cellSize = maxBodyExtent + settings.cellPadding;
        const float usableWidth = std::max(settings.minimumInteriorWidth - 2.0f * settings.spawnMargin, cellSize);
        const size_t columnCount = std::max<size_t>(1, static_cast<size_t>(usableWidth / cellSize));
        const size_t rowCount = std::max<size_t>(1, (totalBodyCount + columnCount - 1) / columnCount);
        const float interiorWidth = std::max(settings.minimumInteriorWidth, static_cast<float>(columnCount) * cellSize + 2.0f * settings.spawnMargin);
        const float interiorHeight = std::max(settings.minimumInteriorHeight, static_cast<float>(rowCount) * cellSize + 2.0f * settings.spawnMargin);

        return RigidBodyContainerLayout{
            totalBodyCount,
            columnCount,
            cellSize,
            interiorWidth,
            interiorHeight,
            -0.5f * interiorWidth,
            -0.5f * interiorHeight,
        };
    }

    Eigen::Vector2f calculateRigidBodyContainerSpawnPosition(const RigidBodyContainerLayout &layout, const RigidBodyContainerScenarioSettings &settings, const size_t spawnIndex)
    {
        const size_t columnIndex = spawnIndex % layout.columnCount;
        const size_t rowIndex = spawnIndex / layout.columnCount;
        const float x = layout.left + settings.spawnMargin + (static_cast<float>(columnIndex) + 0.5f) * layout.cellSize;
        const float y = layout.top + settings.spawnMargin + (static_cast<float>(rowIndex) + 0.5f) * layout.cellSize;
        const float xJitter = static_cast<float>(static_cast<int>(spawnIndex % 3) - 1) * 2.0f;
        const float yJitter = static_cast<float>(static_cast<int>(spawnIndex % 4) - 1) * 1.5f;
        return Eigen::Vector2f(x + xJitter, y + yJitter);
    }

    Eigen::Vector2f calculateRigidBodyContainerInitialVelocity(const size_t spawnIndex)
    {
        constexpr std::array<float, 8> horizontalVelocityPattern = {-48.0f, 36.0f, -28.0f, 44.0f, -40.0f, 24.0f, -32.0f, 52.0f};
        constexpr std::array<float, 6> verticalVelocityPattern = {0.0f, -18.0f, -12.0f, 8.0f, -6.0f, 14.0f};
        return Eigen::Vector2f(
            horizontalVelocityPattern[spawnIndex % horizontalVelocityPattern.size()],
            verticalVelocityPattern[spawnIndex % verticalVelocityPattern.size()]);
    }

    float calculateRigidBodyContainerAngularVelocity(const size_t spawnIndex)
    {
        constexpr std::array<float, 8> angularVelocityPattern = {-1.4f, 0.9f, -0.7f, 1.1f, -1.2f, 0.8f, -0.6f, 1.3f};
        return angularVelocityPattern[spawnIndex % angularVelocityPattern.size()];
    }

    void createRigidBodyContainerWalls(
        platformator::Runtime &runtime,
        const RigidBodyContainerScenarioSettings &settings,
        const RigidBodyContainerLayout &layout,
        const bool attachSprites)
    {
        const float halfWidth = layout.interiorWidth * 0.5f;
        const float halfHeight = layout.interiorHeight * 0.5f;
        const float wallThickness = settings.wallThickness;
        const float horizontalWallWidth = layout.interiorWidth + 2.0f * wallThickness;
        const float verticalWallHeight = layout.interiorHeight + 2.0f * wallThickness;

        createBenchmarkBox(
            runtime,
            "Container Floor",
            0.0f,
            halfHeight + wallThickness * 0.5f + 20.0f, // Add a small margin to prevent initial overlaps with spawned bodies
            horizontalWallWidth,
            wallThickness,
            STATIC,
            false,
            Eigen::Vector2f::Zero(),
            false,
            settings.friction,
            settings.restitution,
            0.0f,
            attachSprites);

        createBenchmarkBox(
            runtime,
            "Container Ceiling",
            0.0f,
            -halfHeight - wallThickness * 0.5f,
            horizontalWallWidth,
            wallThickness,
            STATIC,
            false,
            Eigen::Vector2f::Zero(),
            false,
            settings.friction,
            settings.restitution,
            0.0f,
            attachSprites);

        createBenchmarkBox(
            runtime,
            "Container Left Wall",
            -halfWidth - wallThickness * 0.5f,
            0.0f,
            wallThickness,
            verticalWallHeight,
            STATIC,
            false,
            Eigen::Vector2f::Zero(),
            false,
            settings.friction,
            settings.restitution,
            0.0f,
            attachSprites);

        createBenchmarkBox(
            runtime,
            "Container Right Wall",
            halfWidth + wallThickness * 0.5f,
            0.0f,
            wallThickness,
            verticalWallHeight,
            STATIC,
            false,
            Eigen::Vector2f::Zero(),
            false,
            settings.friction,
            settings.restitution,
            0.0f,
            attachSprites);
    }

    void printRigidBodyContainerScenarioDetails(
        const RigidBodyContainerScenarioSettings &settings,
        const RigidBodyContainerLayout &layout)
    {
        std::fprintf(
            stdout,
            "[Benchmark][Scenario] box_count=%d circle_count=%d container_width=%.1f container_height=%.1f\n",
            settings.boxCount,
            settings.circleCount,
            layout.interiorWidth,
            layout.interiorHeight);
    }

    void buildBroadPhaseScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
    {
        const int laneCount = std::max(1, options.broadPhaseElementCount);
        constexpr float laneStepY = 80.0f;
        constexpr float laneWidth = 48.0f;
        constexpr float laneHeight = 48.0f;
        constexpr std::array<float, 16> startXPattern = {-1400.0f, 1470.0f, -1540.0f, 1610.0f, -1680.0f, 1750.0f, -1820.0f, 1890.0f, -1960.0f, 2030.0f, -2100.0f, 2170.0f, -2240.0f, 2310.0f, -2380.0f, 2450.0f};
        constexpr std::array<float, 16> velocityXPattern = {420.0f, -500.0f, 580.0f, -660.0f, 740.0f, -820.0f, 900.0f, -980.0f, 1060.0f, -1140.0f, 1220.0f, -1300.0f, 1380.0f, -1460.0f, 1540.0f, -1620.0f};
        const float laneStartY = -0.5f * static_cast<float>(laneCount - 1) * laneStepY;

        std::fprintf(stdout, "[Benchmark][Scenario] lane_count=%d\n", laneCount);

        for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex)
        {
            const size_t patternIndex = static_cast<size_t>(laneIndex) % startXPattern.size();
            createBenchmarkBox(
                runtime,
                formatIndexedName("Lane Mover", static_cast<size_t>(laneIndex)),
                startXPattern[patternIndex],
                laneStartY + static_cast<float>(laneIndex) * laneStepY,
                laneWidth,
                laneHeight,
                KINEMATIC,
                false,
                Eigen::Vector2f(velocityXPattern[patternIndex], 0.0f),
                false,
                0.0f,
                0.0f,
                0.0f,
                options.renderFrames);
        }
    }

    std::vector<float> generateEvenlySpacedAxisPositions(const int count, const float extent)
    {
        std::vector<float> positions;
        positions.reserve(static_cast<size_t>(count));

        if (count <= 1)
        {
            positions.push_back(0.0f);
            return positions;
        }

        const float step = (2.0f * extent) / static_cast<float>(count - 1);
        const float start = -extent;
        for (int index = 0; index < count; ++index)
        {
            positions.push_back(start + static_cast<float>(index) * step);
        }

        return positions;
    }

    void buildNarrowPhaseScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
    {
        const int elementCount = std::max(1, options.narrowPhaseElementCount);
        const std::vector<float> columnPositions = generateEvenlySpacedAxisPositions(elementCount, 125.0f);
        const std::vector<float> rowPositions = generateEvenlySpacedAxisPositions(elementCount, 125.0f);
        const std::vector<float> bandPositions = generateEvenlySpacedAxisPositions(elementCount, 140.0f);
        constexpr std::array<float, 8> bandSpeedPattern = {20.0f, -20.0f, 24.0f, -24.0f, 20.0f, -20.0f, 24.0f, -24.0f};

        std::fprintf(stdout, "[Benchmark][Scenario] narrow_element_count=%d\n", elementCount);

        for (size_t index = 0; index < columnPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Trigger Column", index),
                columnPositions[index],
                0.0f,
                26.0f,
                360.0f,
                STATIC,
                false,
                Eigen::Vector2f::Zero(),
                true,
                0.0f,
                0.0f,
                0.0f,
                options.renderFrames);
        }

        for (size_t index = 0; index < rowPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Trigger Row", index),
                0.0f,
                rowPositions[index],
                360.0f,
                26.0f,
                STATIC,
                false,
                Eigen::Vector2f::Zero(),
                true,
                0.0f,
                0.0f,
                0.0f,
                options.renderFrames);
        }

        for (size_t index = 0; index < bandPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Horizontal Band", index),
                0.0f,
                bandPositions[index],
                360.0f,
                18.0f,
                KINEMATIC,
                false,
                Eigen::Vector2f(0.0f, bandSpeedPattern[index % bandSpeedPattern.size()]),
                true,
                0.0f,
                0.0f,
                0.0f,
                options.renderFrames);
        }

        for (size_t index = 0; index < bandPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Vertical Band", index),
                bandPositions[index],
                0.0f,
                18.0f,
                360.0f,
                KINEMATIC,
                false,
                Eigen::Vector2f(bandSpeedPattern[index % bandSpeedPattern.size()], 0.0f),
                true,
                0.0f,
                0.0f,
                0.0f,
                options.renderFrames);
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
                spawnedBoxCount < settings.boxCount &&
                (spawnedCircleCount >= settings.circleCount || spawnIndex % 2 == 0);

            const Eigen::Vector2f position = calculateRigidBodyContainerSpawnPosition(layout, settings, spawnIndex);
            const Eigen::Vector2f velocity = calculateRigidBodyContainerInitialVelocity(spawnIndex);
            const float angularVelocity = calculateRigidBodyContainerAngularVelocity(spawnIndex);

            if (shouldSpawnBox)
            {
                createBenchmarkBox(
                    runtime,
                    formatIndexedName("Container Box", static_cast<size_t>(spawnedBoxCount)),
                    position.x(),
                    position.y(),
                    settings.boxWidth,
                    settings.boxHeight,
                    DYNAMIC,
                    true,
                    velocity,
                    false,
                    settings.friction,
                    settings.restitution,
                    angularVelocity,
                    attachSprites);
                spawnedBoxCount++;
            }
            else
            {
                createBenchmarkCircle(
                    runtime,
                    formatIndexedName("Container Circle", static_cast<size_t>(spawnedCircleCount)),
                    position.x(),
                    position.y(),
                    settings.circleRadius,
                    DYNAMIC,
                    true,
                    velocity,
                    false,
                    settings.friction,
                    settings.restitution,
                    angularVelocity,
                    attachSprites);
                spawnedCircleCount++;
            }

            spawnIndex++;
        }
    }

    void buildScenario(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
    {
        switch (options.scenario)
        {
        case BenchmarkScenario::BroadPhase:
            buildBroadPhaseScenario(runtime, options);
            return;
        case BenchmarkScenario::NarrowPhase:
            buildNarrowPhaseScenario(runtime, options);
            return;
        case BenchmarkScenario::RigidBodyContainer:
            buildRigidBodyContainerScenario(runtime, options);
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

    void configureScenarioCamera(platformator::Runtime &runtime, const BenchmarkRunnerOptions &options)
    {
        runtime.createMainCameraIfNoMainCameraExists();
        Camera *mainCamera = runtime.getMainCamera();
        if (mainCamera == nullptr)
        {
            return;
        }

        switch (options.scenario)
        {
        case BenchmarkScenario::BroadPhase:
        {
            const float laneStepY = 80.0f;
            const float laneCount = static_cast<float>(std::max(1, options.broadPhaseElementCount));
            const float cameraHeight = std::max(600.0f, laneCount * laneStepY + 320.0f);
            mainCamera->setCamera(fitCameraRectToWindowAspect(
                SDL_FRect{-2600.0f, -0.5f * cameraHeight, 5200.0f, cameraHeight},
                options.runtimeOptions.windowSettings));
            return;
        }
        case BenchmarkScenario::NarrowPhase:
        {
            const float span = std::max(200.0f, 50.0f * static_cast<float>(std::max(1, options.narrowPhaseElementCount)));
            mainCamera->setCamera(fitCameraRectToWindowAspect(
                SDL_FRect{-span, -span, 2.0f * span, 2.0f * span},
                options.runtimeOptions.windowSettings));
            return;
        }
        case BenchmarkScenario::RigidBodyContainer:
        {
            const RigidBodyContainerScenarioSettings settings = makeRigidBodyContainerScenarioSettings(options);
            const RigidBodyContainerLayout layout = calculateRigidBodyContainerLayout(options);
            const float cameraWidth = layout.interiorWidth + 2.0f * settings.wallThickness + 2.0f * settings.cameraMargin;
            const float cameraHeight = layout.interiorHeight + 2.0f * settings.wallThickness + 2.0f * settings.cameraMargin;
            mainCamera->setCamera(fitCameraRectToWindowAspect(
                SDL_FRect{-0.5f * cameraWidth, -0.5f * cameraHeight, cameraWidth, cameraHeight},
                options.runtimeOptions.windowSettings));
            return;
        }
        case BenchmarkScenario::None:
            return;
        }
    }

    int parseNonNegativeInteger(const std::string &value, const std::string &argumentName)
    {
        const int parsedValue = std::stoi(value);
        if (parsedValue < 0)
        {
            throw std::runtime_error(argumentName + " must be greater than or equal to zero.");
        }

        return parsedValue;
    }

    int parsePositiveInteger(const std::string &value, const std::string &argumentName)
    {
        const int parsedValue = std::stoi(value);
        if (parsedValue <= 0)
        {
            throw std::runtime_error(argumentName + " must be greater than zero.");
        }

        return parsedValue;
    }

    double parsePositiveDouble(const std::string &value, const std::string &argumentName)
    {
        const double parsedValue = std::stod(value);
        if (parsedValue <= 0.0)
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
                options.warmupFrameCount = parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
                continue;
            }

            if (argument == "--measure-frames")
            {
                options.measureFrameCount = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
                continue;
            }

            if (argument == "--dt")
            {
                options.timeDelta = parsePositiveDouble(requireArgumentValue(argc, args, index, argument), argument);
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
                options.broadPhaseElementCount = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
                continue;
            }

            if (argument == "--narrow-count")
            {
                options.narrowPhaseElementCount = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
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
            throw std::runtime_error("rigid_body_container requires at least one box or circle.");
        }

        return options;
    }

    void runBenchmark(const BenchmarkRunnerOptions &options)
    {
        if (options.csvOutputPath.empty())
        {
            PLATFORMATOR_BENCH_CLEAR_CSV_OUTPUT_PATH();
        }
        else
        {
            PLATFORMATOR_BENCH_SET_CSV_OUTPUT_PATH(options.csvOutputPath);
        }

        platformator::Runtime runtime(options.runtimeOptions);

        std::fprintf(
            stdout,
            "[Benchmark] render=%s warmup_frames=%d measure_frames=%d dt=%.6f\n",
            options.renderFrames ? "true" : "false",
            options.warmupFrameCount,
            options.measureFrameCount,
            options.timeDelta);

        if (!options.csvOutputPath.empty())
        {
            std::fprintf(stdout, "[Benchmark] csv_output=%s\n", options.csvOutputPath.c_str());
        }

        if (options.scenario == BenchmarkScenario::None)
        {
            std::fprintf(stdout, "[Benchmark] scene=%s\n", options.runtimeOptions.sceneFilePath.c_str());
            runtime.loadScene(options.runtimeOptions.sceneFilePath);
        }
        else
        {
            std::fprintf(stdout, "[Benchmark] scenario=%s\n", scenarioName(options.scenario));
            buildScenario(runtime, options);
        }

        configureScenarioCamera(runtime, options);

        if (options.renderFrames)
        {
            runtime.run();

            return;
        }

        for (int frameIndex = 0; frameIndex < options.warmupFrameCount; frameIndex++)
        {
            runtime.simulateFrame(options.timeDelta);
        }

        PLATFORMATOR_BENCH_RESET();

        for (int frameIndex = 0; frameIndex < options.measureFrameCount; frameIndex++)
        {
            runtime.simulateFrame(options.timeDelta);
        }
    }

    void printUsage(FILE *output)
    {
        std::fprintf(
            output,
            "Usage: platformator_benchmark_runner [scene] [--scene path | --scenario broad_phase|narrow_phase|rigid_body_container] [--warmup-frames N] [--measure-frames N] [--dt seconds] [--box-count N] [--circle-count N] [--broad-count N] [--narrow-count N] [--csv-output path] [--render]\n");
    }
}

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