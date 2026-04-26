#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "animator.h"
#include "boxcollider.h"
#include "circlecollider.h"
#include "constants.h"
#include "grid.h"
#include "aabb.h"
#include "audio.h"
#include "gamemanager.h"
#include "localsortarray.h"
#include "physicsmanager.h"
#include "rigidbody.h"
#include "scriptcomponent.h"
#include "sprite.h"
#include "texturewrapper.h"

namespace
{
    constexpr double kTimeStep = FRAME_TIME;

    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void configureHeadlessEnvironment()
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        setenv("SDL_RENDER_DRIVER", "software", 1);
    }

    void simulateFrames(GameManager &gameManager, int frameCount)
    {
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            gameManager.simulateFrame(kTimeStep);
        }
    }

    void drainPhysicsQueuesAndDeleteMarkedObjects(GameManager &gameManager)
    {
        gameManager.simulateFrame(kTimeStep);
    }

    void destroyObjects(GameManager &gameManager, const std::vector<GameObject *> &objects)
    {
        for (GameObject *object : objects)
        {
            if (object != nullptr)
            {
                gameManager.destroyGameObject(object);
            }
        }

        drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);
    }

    class SceneScope
    {
    public:
        explicit SceneScope(GameManager &gameManager) : gameManager(gameManager), objects()
        {
        }

        ~SceneScope()
        {
            destroyObjects(gameManager, objects);
        }

        void add(GameObject *object)
        {
            objects.push_back(object);
        }

    private:
        GameManager &gameManager;
        std::vector<GameObject *> objects;
    };

    GameObject *createStaticBox(GameManager &gameManager, const std::string &name, float x, float y, float width, float height)
    {
        return gameManager
            .createGameObject()
            ->setName(name)
            ->addComponent<Rigidbody>(STATIC, false)
            ->addComponent<BoxCollider>(width, height)
            ->setPosition(Eigen::Vector2f(x, y));
    }

    GameObject *createDynamicCircle(GameManager &gameManager, const std::string &name, float x, float y, float radius)
    {
        GameObject *object = gameManager
                                 .createGameObject()
                                 ->setName(name)
                                 ->addComponent<Rigidbody>()
                                 ->addComponent<CircleCollider>(radius)
                                 ->setPosition(Eigen::Vector2f(x, y));

        object->getComponent<Rigidbody>()
            ->setMass(10.0f)
            ->setFriction(0.8f)
            ->setRestitution(0.0f);

        return object;
    }

    GameObject *createDynamicBox(GameManager &gameManager, const std::string &name, float x, float y, float width, float height)
    {
        GameObject *object = gameManager
                                 .createGameObject()
                                 ->setName(name)
                                 ->addComponent<Rigidbody>()
                                 ->addComponent<BoxCollider>(width, height)
                                 ->setPosition(Eigen::Vector2f(x, y));

        object->getComponent<Rigidbody>()
            ->setMass(8.0f)
            ->setFriction(0.9f)
            ->setRestitution(0.0f);

        return object;
    }

    GameObject *createKinematicBox(GameManager &gameManager, const std::string &name, float x, float y, float width, float height, bool gravity)
    {
        return gameManager
            .createGameObject()
            ->setName(name)
            ->addComponent<Rigidbody>(KINEMATIC, gravity)
            ->addComponent<BoxCollider>(width, height)
            ->setPosition(Eigen::Vector2f(x, y));
    }

    class CollisionEventCounterBehavior : public Behavior
    {
    public:
        CollisionEventCounterBehavior() : enterCount(0), stayCount(0), exitCount(0)
        {
        }

        void onCollisionEnter(const Collision *, Collider *, double) override
        {
            enterCount++;
        }

        void onCollisionExit(Collider *, double) override
        {
            exitCount++;
        }

        void onCollisionStay(const Collision *, Collider *, double) override
        {
            stayCount++;
        }

        size_t enterCount;
        size_t stayCount;
        size_t exitCount;
    };

    std::filesystem::path createTemporaryBmpAsset(const std::string &fileName, Uint8 red, Uint8 green, Uint8 blue)
    {
        std::filesystem::path filePath = (std::filesystem::current_path() / fileName).lexically_normal();

        std::error_code errorCode;
        std::filesystem::remove(filePath, errorCode);

        SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
        require(surface != nullptr, "Animator regression failed to create a temporary SDL surface.");

        Uint32 color = SDL_MapSurfaceRGBA(surface, red, green, blue, 255);
        bool filled = SDL_FillSurfaceRect(surface, nullptr, color);
        bool saved = SDL_SaveBMP(surface, filePath.string().c_str());
        SDL_DestroySurface(surface);

        require(filled, "Animator regression failed to fill the temporary surface.");
        require(saved, "Animator regression failed to save the temporary BMP asset.");
        return filePath;
    }

    std::filesystem::path createTemporaryStripBmpAsset(const std::string &fileName)
    {
        std::filesystem::path filePath = (std::filesystem::current_path() / fileName).lexically_normal();

        std::error_code errorCode;
        std::filesystem::remove(filePath, errorCode);

        SDL_Surface *surface = SDL_CreateSurface(8, 4, SDL_PIXELFORMAT_RGBA32);
        require(surface != nullptr, "Advanced animator regression failed to create a temporary strip surface.");

        const Uint32 red = SDL_MapSurfaceRGBA(surface, 255, 0, 0, 255);
        const Uint32 green = SDL_MapSurfaceRGBA(surface, 0, 255, 0, 255);
        const SDL_Rect leftHalf{0, 0, 4, 4};
        const SDL_Rect rightHalf{4, 0, 4, 4};

        const bool filledLeft = SDL_FillSurfaceRect(surface, &leftHalf, red);
        const bool filledRight = SDL_FillSurfaceRect(surface, &rightHalf, green);
        const bool saved = SDL_SaveBMP(surface, filePath.string().c_str());
        SDL_DestroySurface(surface);

        require(filledLeft && filledRight, "Advanced animator regression failed to fill the temporary strip surface.");
        require(saved, "Advanced animator regression failed to save the temporary strip BMP asset.");
        return filePath;
    }

    void addColliderToLocalAabb(AABB &aabb, Collider *collider)
    {
        require(collider != nullptr, "Local AABB regression received a null collider.");
        collider->applySync();
        aabb.add(collider);
    }

    void testCircleCollisionStability()
    {
        GameManager &gameManager = GameManager::getInstance();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Regression Floor", 200.0f, 300.0f, 400.0f, 40.0f);
        GameObject *ball = createDynamicCircle(gameManager, "Regression Ball", 200.0f, 140.0f, 20.0f);
        sceneScope.add(floor);
        sceneScope.add(ball);

        simulateFrames(gameManager, 720);

        const float expectedRestY = 300.0f - 20.0f - 20.0f;
        const float actualRestY = ball->getPosition().y();

        require(std::isfinite(actualRestY), "Circle stability regression produced a non-finite position.");
        require(std::abs(actualRestY - expectedRestY) <= 6.0f,
                "Circle stability regression drifted too far from the expected resting height.");
    }

    void testSleepingOnSupport()
    {
        GameManager &gameManager = GameManager::getInstance();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Sleep Floor", 320.0f, 340.0f, 420.0f, 40.0f);
        GameObject *box = createDynamicBox(gameManager, "Sleep Box", 320.0f, 180.0f, 40.0f, 40.0f);
        sceneScope.add(floor);
        sceneScope.add(box);

        simulateFrames(gameManager, 720);

        Rigidbody *rigidbody = box->getComponent<Rigidbody>();
        require(rigidbody != nullptr, "Sleeping regression lost the box rigidbody.");
        require(rigidbody->hasSupportContact(), "Sleeping regression ended without a support contact.");
        require(rigidbody->getIsSleeping(), "Supported dynamic body failed to enter sleep state.");
    }

    void testBroadPhasePairTracking()
    {
        GameManager &gameManager = GameManager::getInstance();

        SceneScope sceneScope(gameManager);
        GameObject *boxA = createStaticBox(gameManager, "Pair A", 120.0f, 120.0f, 40.0f, 40.0f);
        GameObject *boxB = createStaticBox(gameManager, "Pair B", 320.0f, 120.0f, 40.0f, 40.0f);
        sceneScope.add(boxA);
        sceneScope.add(boxB);

        boxA->addComponent<ScriptComponent>();
        ScriptComponent *scriptComponent = boxA->getComponent<ScriptComponent>();
        require(scriptComponent != nullptr, "Broad-phase regression failed to create a script component for the first collider.");

        auto *collisionCounter = new CollisionEventCounterBehavior();
        scriptComponent->addBehavior(collisionCounter);

        gameManager.simulateFrame(kTimeStep);
        require(collisionCounter->enterCount == 0 && collisionCounter->stayCount == 0 && collisionCounter->exitCount == 0,
                "Broad-phase regression unexpectedly reported a collision before overlap.");

        boxB->setPosition(Eigen::Vector2f(135.0f, 120.0f));
        gameManager.simulateFrame(kTimeStep);
        require(collisionCounter->enterCount == 1,
                "Broad-phase regression expected a collision enter callback after overlap.");
        require(collisionCounter->exitCount == 0,
                "Broad-phase regression unexpectedly reported a collision exit while overlap persisted.");

        boxB->setPosition(Eigen::Vector2f(360.0f, 120.0f));
        gameManager.simulateFrame(kTimeStep);
        require(collisionCounter->exitCount == 1,
                "Broad-phase regression expected a collision exit callback after separation.");
    }

    void testCheckpointFastMotionCancellation()
    {
        GameManager &gameManager = GameManager::getInstance();

        SceneScope sceneScope(gameManager);
        GameObject *box = createStaticBox(gameManager, "Checkpoint Box", 120.0f, 120.0f, 40.0f, 40.0f);
        sceneScope.add(box);

        Collider *collider = box->getComponent<Collider>();
        require(collider != nullptr, "Checkpoint regression lost the collider.");

        LocalSortArray chunk(static_cast<SegmentedIntervalList *>(nullptr));

        chunk.removeCheckpoint(collider);
        require(!chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression unexpectedly created a checkpoint during the pending-remove phase.");

        chunk.addCheckpoint(collider);
        require(!chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression failed to cancel a pending fast-motion remove-then-add sequence.");

        chunk.addCheckpoint(collider);
        require(chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression failed to add a real checkpoint after the fast-motion cancellation state was cleared.");

        chunk.removeCheckpoint(collider);
        require(!chunk.getCheckpoints()->contains(collider),
                "Checkpoint regression failed to remove an existing checkpoint cleanly.");
    }

    void testSegmentedIntervalListFastMotionAcrossChunks()
    {
        GameManager &gameManager = GameManager::getInstance();

        SceneScope sceneScope(gameManager);
        Grid localGrid;
        AABB localAabb(&localGrid);

        for (int index = 0; index < 17; ++index)
        {
            GameObject *filler = createStaticBox(
                gameManager,
                "Chunk Filler " + std::to_string(index),
                10.0f + static_cast<float>(index) * 20.0f,
                0.0f,
                2.0f,
                2.0f);
            sceneScope.add(filler);
            addColliderToLocalAabb(localAabb, filler->getComponent<Collider>());
        }

        GameObject *bridge = createStaticBox(gameManager, "Fast Bridge", 110.0f, 100.0f, 80.0f, 20.0f);
        sceneScope.add(bridge);
        Collider *bridgeCollider = bridge->getComponent<Collider>();
        addColliderToLocalAabb(localAabb, bridgeCollider);

        require(localGrid.getCandidatePairCount() == 0,
                "Fast-motion regression expected no broad-phase pairs before adding the probe collider.");

        bridge->setPosition(Eigen::Vector2f(170.0f, 100.0f));
        bridgeCollider->applySync();

        GameObject *probe = createStaticBox(gameManager, "Checkpoint Probe", 145.0f, 100.0f, 2.0f, 20.0f);
        sceneScope.add(probe);
        Collider *probeCollider = probe->getComponent<Collider>();
        addColliderToLocalAabb(localAabb, probeCollider);

        require(localGrid.getCandidatePairCount() == 1,
                "Fast-motion regression expected the probe to overlap the fast bridge through real chunk checkpoint handling.");

        bridge->setPosition(Eigen::Vector2f(250.0f, 100.0f));
        bridgeCollider->applySync();

        require(localGrid.getCandidatePairCount() == 0,
                "Fast-motion regression expected the probe pair to be removed after the fast bridge moved away.");
    }

    std::filesystem::path findWorkspaceRelativePath(const std::filesystem::path &relativePath)
    {
        std::filesystem::path searchDirectory = std::filesystem::current_path();

        while (true)
        {
            std::filesystem::path candidate = searchDirectory / relativePath;
            if (std::filesystem::exists(candidate))
            {
                return candidate.lexically_normal();
            }

            std::filesystem::path parentDirectory = searchDirectory.parent_path();
            if (parentDirectory == searchDirectory)
            {
                break;
            }

            searchDirectory = parentDirectory;
        }

        throw std::runtime_error("Regression tests could not locate resource '" + relativePath.generic_string() + "'.");
    }

    std::filesystem::path findRegressionAsset(const std::string &fileName)
    {
        return findWorkspaceRelativePath(std::filesystem::path("assets") / fileName);
    }

    std::filesystem::path findRegressionAudioAsset(const std::string &fileName)
    {
        return findWorkspaceRelativePath(std::filesystem::path("examples") / "mario" / "assets" / "audio" / fileName);
    }

    std::string makeSceneRelativeResourcePath(const std::filesystem::path &resourcePath, const std::filesystem::path &scenePath)
    {
        std::error_code errorCode;
        std::filesystem::path relativePath = std::filesystem::relative(resourcePath, scenePath.parent_path(), errorCode);
        if (!errorCode && !relativePath.empty())
        {
            return relativePath.generic_string();
        }

        return resourcePath.generic_string();
    }

    void destroyNamedObject(GameManager &gameManager, SDLWindow *window, const std::string &name)
    {
        GameObject *gameObject = gameManager.getGameObject(name);
        if (gameObject == nullptr)
        {
            return;
        }

        Camera *camera = gameObject->getComponent<Camera>();
        if (camera != nullptr && window != nullptr && window->getMainCamera() == camera)
        {
            window->setMainCamera(nullptr);
        }

        gameManager.destroyGameObject(gameObject);
    }

    void testSceneLoading()
    {
        GameManager &gameManager = GameManager::getInstance();
        SDLWindow *window = gameManager.getWindow();
        require(window != nullptr, "Scene loading regression requires an SDLWindow.");

        std::filesystem::path scenePath = std::filesystem::current_path() / "scene_loading_regression.scene";
        std::filesystem::path wallTexturePath = findRegressionAsset("wall.png");
        std::string sceneSpritePath = makeSceneRelativeResourcePath(wallTexturePath, scenePath);

        auto cleanup = [&]()
        {
            destroyNamedObject(gameManager, window, "Loaded Ball");
            destroyNamedObject(gameManager, window, "Loaded Camera");

            drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);

            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
        };

        cleanup();

        try
        {
            std::ofstream sceneFile(scenePath);
            require(sceneFile.is_open(), "Scene loading regression failed to create the temporary scene file.");

            sceneFile << "object {\n";
            sceneFile << "    name \"Loaded Ball\"\n";
            sceneFile << "    tag \"Player\"\n";
            sceneFile << "    active true\n";
            sceneFile << "    position 123 234\n";
            sceneFile << "    rotation 0.5\n";
            sceneFile << "    scale 2 1.5\n";
            sceneFile << "    rigidbody {\n";
            sceneFile << "        bodyType dynamic\n";
            sceneFile << "        gravity false\n";
            sceneFile << "        mass 7\n";
            sceneFile << "        velocity 10 -20\n";
            sceneFile << "        force 1 2\n";
            sceneFile << "        angularVelocity 0.25\n";
            sceneFile << "        torque 0.75\n";
            sceneFile << "        friction 0.4\n";
            sceneFile << "        restitution 0.6\n";
            sceneFile << "    }\n";
            sceneFile << "    circleCollider {\n";
            sceneFile << "        radius 12\n";
            sceneFile << "        trigger false\n";
            sceneFile << "        collisionGroup 3\n";
            sceneFile << "        collisionMask 5\n";
            sceneFile << "    }\n";
            sceneFile << "    sprite {\n";
            sceneFile << "        path \"" << sceneSpritePath << "\"\n";
            sceneFile << "        flip horizontal\n";
            sceneFile << "        size 32 48\n";
            sceneFile << "    }\n";
            sceneFile << "}\n\n";
            sceneFile << "object {\n";
            sceneFile << "    name \"Loaded Camera\"\n";
            sceneFile << "    camera {\n";
            sceneFile << "        viewport 5 10 320 240\n";
            sceneFile << "    }\n";
            sceneFile << "}\n";
            sceneFile.close();

            Scene scene(scenePath.string());
            gameManager.loadScene(scene);

            GameObject *loadedBall = gameManager.getGameObject("Loaded Ball");
            require(loadedBall != nullptr, "Scene loading regression failed to create the ball object.");
            require(loadedBall->getTag() == "Player", "Scene loading regression failed to load the object tag.");
            require(std::abs(loadedBall->getPosition().x() - 123.0f) <= 1e-5f && std::abs(loadedBall->getPosition().y() - 234.0f) <= 1e-5f,
                    "Scene loading regression failed to load the object position.");
            require(std::abs(loadedBall->getRotation() - 0.5f) <= 1e-5f,
                    "Scene loading regression failed to load the object rotation.");
            require(std::abs(loadedBall->getScale().x() - 2.0f) <= 1e-5f && std::abs(loadedBall->getScale().y() - 1.5f) <= 1e-5f,
                    "Scene loading regression failed to load the object scale.");

            Rigidbody *rigidbody = loadedBall->getComponent<Rigidbody>();
            require(rigidbody != nullptr, "Scene loading regression failed to load the rigidbody component.");
            require(rigidbody->getBodyType() == BodyType::DYNAMIC && !rigidbody->getGravity(),
                    "Scene loading regression failed to load rigidbody type or gravity.");
            require(std::abs(rigidbody->getMass() - 7.0f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody mass.");
            require(std::abs(rigidbody->getVelocity().x() - 10.0f) <= 1e-5f && std::abs(rigidbody->getVelocity().y() + 20.0f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody velocity.");
            require(std::abs(rigidbody->getForce().x() - 1.0f) <= 1e-5f && std::abs(rigidbody->getForce().y() - 2.0f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody force.");
            require(std::abs(rigidbody->getAngularVelocity() - 0.25f) <= 1e-5f && std::abs(rigidbody->getTorque() - 0.75f) <= 1e-5f,
                    "Scene loading regression failed to load angular rigidbody values.");
            require(std::abs(rigidbody->getFriction() - 0.4f) <= 1e-5f && std::abs(rigidbody->getRestitution() - 0.6f) <= 1e-5f,
                    "Scene loading regression failed to load rigidbody material properties.");

            CircleCollider *circleCollider = loadedBall->getComponent<CircleCollider>();
            require(circleCollider != nullptr, "Scene loading regression failed to load the circle collider.");
            require(std::abs(circleCollider->getRadius() - 24.0f) <= 1e-5f,
                    "Scene loading regression failed to load the circle collider radius.");
            require(circleCollider->getCollisionGroup() == 3 && circleCollider->getCollisionMask() == 5 && !circleCollider->getIsTrigger(),
                    "Scene loading regression failed to load the circle collider filter settings.");

            Sprite *sprite = loadedBall->getComponent<Sprite>();
            require(sprite != nullptr, "Scene loading regression failed to load the sprite component.");
            require(sprite->getTexture() != nullptr, "Scene loading regression failed to load the sprite texture.");
            require(sprite->getFlip() == SDL_FLIP_HORIZONTAL,
                    "Scene loading regression failed to load the sprite flip mode.");
            require(std::abs(sprite->getWidth() - 32.0f) <= 1e-5f && std::abs(sprite->getHeight() - 48.0f) <= 1e-5f,
                    "Scene loading regression failed to load the sprite size.");

            GameObject *loadedCameraObject = gameManager.getGameObject("Loaded Camera");
            require(loadedCameraObject != nullptr, "Scene loading regression failed to create the camera object.");

            Camera *camera = loadedCameraObject->getComponent<Camera>();
            require(camera != nullptr, "Scene loading regression failed to load the camera component.");
            require(window->getMainCamera() == camera,
                    "Scene loading regression failed to register the loaded camera as the main camera.");
            const SDL_FRect &cameraRect = camera->getCamera();
            require(std::abs(cameraRect.x - 5.0f) <= 1e-5f && std::abs(cameraRect.y - 10.0f) <= 1e-5f &&
                        std::abs(cameraRect.w - 320.0f) <= 1e-5f && std::abs(cameraRect.h - 240.0f) <= 1e-5f,
                    "Scene loading regression failed to load the camera viewport.");

            cleanup();
        }
        catch (const std::exception &)
        {
            cleanup();
            throw;
        }
    }

    void testSceneSavingRoundTrip()
    {
        GameManager &gameManager = GameManager::getInstance();
        SDLWindow *window = gameManager.getWindow();
        require(window != nullptr, "Scene round-trip regression requires an SDLWindow.");

        std::filesystem::path scenePath = std::filesystem::current_path() / "scene_roundtrip_regression.scene";
        std::filesystem::path wallTexturePath = findRegressionAsset("wall.png");
        std::string expectedSavedSpritePath = makeSceneRelativeResourcePath(wallTexturePath, scenePath);
        std::string wallTexturePathString = wallTexturePath.string();

        auto cleanup = [&]()
        {
            destroyNamedObject(gameManager, window, "Roundtrip Ball");
            destroyNamedObject(gameManager, window, "Roundtrip Camera");

            drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);

            std::error_code errorCode;
            std::filesystem::remove(scenePath, errorCode);
        };

        cleanup();

        try
        {
            GameObject *roundtripBall = gameManager
                                            .createGameObject()
                                            ->setName("Roundtrip Ball")
                                            ->setTag("Roundtrip")
                                            ->setPosition(Eigen::Vector2f(210.0f, 220.0f))
                                            ->setRotation(0.25f)
                                            ->setScale(Eigen::Vector2f(2.0f, 1.5f))
                                            ->addComponent<Rigidbody>(KINEMATIC, false)
                                            ->addComponent<CircleCollider>(12.0f)
                                            ->addComponent<Sprite>(wallTexturePathString.c_str(), SDL_FLIP_VERTICAL, 32.0f, 48.0f);

            Sprite *sourceSprite = roundtripBall->getComponent<Sprite>();
            require(sourceSprite != nullptr && sourceSprite->getTexture() != nullptr,
                    "Scene round-trip regression failed to create the source sprite texture.");

            Rigidbody *rigidbody = roundtripBall->getComponent<Rigidbody>();
            require(rigidbody != nullptr, "Scene round-trip regression failed to create the source rigidbody.");
            rigidbody->setMass(9.0f)
                ->setVelocity(Eigen::Vector2f(30.0f, -40.0f))
                ->setForce(Eigen::Vector2f(4.0f, 5.0f))
                ->setAngularVelocity(0.6f)
                ->setTorque(1.2f)
                ->setFriction(0.3f)
                ->setRestitution(0.7f);

            CircleCollider *circleCollider = roundtripBall->getComponent<CircleCollider>();
            require(circleCollider != nullptr, "Scene round-trip regression failed to create the source circle collider.");
            circleCollider->setIsTrigger(true);
            circleCollider->setCollisionGroup(7);
            circleCollider->setCollisionMask(11);

            GameObject *roundtripCamera = gameManager
                                              .createGameObject()
                                              ->setName("Roundtrip Camera")
                                              ->addComponent<Camera>(3.0f, 4.0f, 300.0f, 200.0f);

            Scene scene(scenePath.string());
            gameManager.saveScene(scene);

            require(std::filesystem::exists(scenePath), "Scene round-trip regression failed to create the saved scene file.");

            std::ifstream savedSceneFile(scenePath);
            require(savedSceneFile.is_open(), "Scene round-trip regression failed to reopen the saved scene file.");
            std::stringstream savedSceneContents;
            savedSceneContents << savedSceneFile.rdbuf();
            require(savedSceneContents.str().find("path \"" + expectedSavedSpritePath + "\"") != std::string::npos,
                    "Scene round-trip regression failed to save sprite paths relative to the scene file.");

            destroyNamedObject(gameManager, window, "Roundtrip Ball");
            destroyNamedObject(gameManager, window, "Roundtrip Camera");
            drainPhysicsQueuesAndDeleteMarkedObjects(gameManager);

            gameManager.loadScene(scene);

            GameObject *loadedBall = gameManager.getGameObject("Roundtrip Ball");
            require(loadedBall != nullptr, "Scene round-trip regression failed to recreate the saved ball.");
            require(loadedBall->getTag() == "Roundtrip", "Scene round-trip regression failed to preserve the object tag.");
            require(std::abs(loadedBall->getPosition().x() - 210.0f) <= 1e-5f && std::abs(loadedBall->getPosition().y() - 220.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the object position.");
            require(std::abs(loadedBall->getRotation() - 0.25f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the object rotation.");
            require(std::abs(loadedBall->getScale().x() - 2.0f) <= 1e-5f && std::abs(loadedBall->getScale().y() - 1.5f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the object scale.");

            Rigidbody *loadedRigidbody = loadedBall->getComponent<Rigidbody>();
            require(loadedRigidbody != nullptr, "Scene round-trip regression failed to reload the rigidbody.");
            require(loadedRigidbody->getBodyType() == BodyType::KINEMATIC && !loadedRigidbody->getGravity(),
                    "Scene round-trip regression failed to preserve rigidbody bodyType or gravity.");
            require(std::abs(loadedRigidbody->getMass() - 9.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody mass.");
            require(std::abs(loadedRigidbody->getVelocity().x() - 30.0f) <= 1e-5f && std::abs(loadedRigidbody->getVelocity().y() + 40.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody velocity.");
            require(std::abs(loadedRigidbody->getForce().x() - 4.0f) <= 1e-5f && std::abs(loadedRigidbody->getForce().y() - 5.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody force.");
            require(std::abs(loadedRigidbody->getAngularVelocity() - 0.6f) <= 1e-5f && std::abs(loadedRigidbody->getTorque() - 1.2f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve angular rigidbody values.");
            require(std::abs(loadedRigidbody->getFriction() - 0.3f) <= 1e-5f && std::abs(loadedRigidbody->getRestitution() - 0.7f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve rigidbody material properties.");

            CircleCollider *loadedCircleCollider = loadedBall->getComponent<CircleCollider>();
            require(loadedCircleCollider != nullptr, "Scene round-trip regression failed to reload the circle collider.");
            require(std::abs(loadedCircleCollider->getRadius() - 24.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the circle collider radius.");
            require(loadedCircleCollider->getIsTrigger() && loadedCircleCollider->getCollisionGroup() == 7 && loadedCircleCollider->getCollisionMask() == 11,
                    "Scene round-trip regression failed to preserve the circle collider filter settings.");

            Sprite *loadedSprite = loadedBall->getComponent<Sprite>();
            require(loadedSprite != nullptr, "Scene round-trip regression failed to reload the sprite.");
            require(loadedSprite->getTexture() != nullptr, "Scene round-trip regression failed to reload the sprite texture.");
            require(loadedSprite->getFlip() == SDL_FLIP_VERTICAL,
                    "Scene round-trip regression failed to preserve the sprite flip mode.");
            require(std::abs(loadedSprite->getWidth() - 32.0f) <= 1e-5f && std::abs(loadedSprite->getHeight() - 48.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the sprite size.");

            GameObject *loadedCameraObject = gameManager.getGameObject("Roundtrip Camera");
            require(loadedCameraObject != nullptr, "Scene round-trip regression failed to recreate the saved camera.");
            Camera *loadedCamera = loadedCameraObject->getComponent<Camera>();
            require(loadedCamera != nullptr, "Scene round-trip regression failed to reload the camera component.");
            require(window->getMainCamera() == loadedCamera,
                    "Scene round-trip regression failed to register the reloaded camera as the main camera.");
            const SDL_FRect &loadedCameraRect = loadedCamera->getCamera();
            require(std::abs(loadedCameraRect.x - 3.0f) <= 1e-5f && std::abs(loadedCameraRect.y - 4.0f) <= 1e-5f &&
                        std::abs(loadedCameraRect.w - 300.0f) <= 1e-5f && std::abs(loadedCameraRect.h - 200.0f) <= 1e-5f,
                    "Scene round-trip regression failed to preserve the camera viewport.");

            cleanup();
        }
        catch (const std::exception &)
        {
            cleanup();
            throw;
        }
    }

    void testRestitutionBounce()
    {
        GameManager &gameManager = GameManager::getInstance();

        SceneScope sceneScope(gameManager);
        GameObject *floor = createStaticBox(gameManager, "Bounce Floor", 220.0f, 320.0f, 400.0f, 40.0f);
        GameObject *ball = createDynamicCircle(gameManager, "Bounce Ball", 220.0f, 120.0f, 20.0f);
        sceneScope.add(floor);
        sceneScope.add(ball);

        Rigidbody *rigidbody = ball->getComponent<Rigidbody>();
        require(rigidbody != nullptr, "Restitution regression lost the circle rigidbody.");
        rigidbody->setRestitution(0.8f);
        rigidbody->setFriction(0.0f);

        const float expectedContactHeight = 320.0f - 20.0f - 20.0f;
        bool observedBounce = false;

        for (int frameIndex = 0; frameIndex < 360; ++frameIndex)
        {
            gameManager.simulateFrame(kTimeStep);

            if (ball->getPosition().y() >= expectedContactHeight - 2.0f && rigidbody->getVelocity().y() < -5.0f)
            {
                observedBounce = true;
                break;
            }
        }

        require(observedBounce, "Restitution regression never produced an upward post-impact velocity.");
    }

    void testKinematicBodySemantics()
    {
        GameManager &gameManager = GameManager::getInstance();

        {
            SceneScope sceneScope(gameManager);
            GameObject *kinematicMover = createKinematicBox(gameManager, "Kinematic Mover", 100.0f, 100.0f, 80.0f, 20.0f, true);
            GameObject *staticWall = createStaticBox(gameManager, "Kinematic Wall", 200.0f, 100.0f, 20.0f, 220.0f);
            sceneScope.add(kinematicMover);
            sceneScope.add(staticWall);

            Rigidbody *kinematicBody = kinematicMover->getComponent<Rigidbody>();
            require(kinematicBody != nullptr, "Kinematic regression lost the mover rigidbody.");

            kinematicBody->setVelocity(Eigen::Vector2f(120.0f, 0.0f));
            kinematicBody->setForce(Eigen::Vector2f(5000.0f, 5000.0f));

            simulateFrames(gameManager, 180);

            require(kinematicBody->getInverseMass() == 0.0f, "Kinematic bodies must have zero inverse mass in the solver.");
            require(kinematicBody->getInverseMomentOfInertia() == 0.0f, "Kinematic bodies must have zero inverse inertia in the solver.");
            require(kinematicMover->getPosition().x() <= 151.0f,
                    "Kinematic body passed through a static wall instead of being blocked.");
            require(std::abs(kinematicMover->getPosition().y() - 100.0f) <= 1.0f,
                    "Kinematic body should ignore gravity and force-based integration.");
            require(std::abs(kinematicBody->getVelocity().x()) <= 1.0f,
                    "Kinematic body should have its wall-normal velocity cancelled on contact.");
        }

        {
            SceneScope sceneScope(gameManager);
            GameObject *kinematicFloor = createKinematicBox(gameManager, "Kinematic Floor", 260.0f, 340.0f, 360.0f, 40.0f, true);
            GameObject *dynamicBox = createDynamicBox(gameManager, "Dynamic On Kinematic", 260.0f, 180.0f, 40.0f, 40.0f);
            sceneScope.add(kinematicFloor);
            sceneScope.add(dynamicBox);

            simulateFrames(gameManager, 720);

            Rigidbody *dynamicBody = dynamicBox->getComponent<Rigidbody>();
            require(dynamicBody != nullptr, "Kinematic floor regression lost the dynamic rigidbody.");
            require(std::abs(kinematicFloor->getPosition().y() - 340.0f) <= 0.5f,
                    "Kinematic floor should not be displaced by gravity or solver impulses.");
            require(dynamicBody->hasSupportContact(), "Dynamic body failed to register support from a kinematic floor.");
            require(std::abs(dynamicBox->getPosition().y() - 300.0f) <= 6.0f,
                    "Dynamic body failed to settle on the kinematic floor.");
        }

        {
            SceneScope sceneScope(gameManager);
            GameObject *staticFloor = createStaticBox(gameManager, "Static Floor For Kinematic", 260.0f, 340.0f, 360.0f, 40.0f);
            GameObject *kinematicBox = createKinematicBox(gameManager, "Kinematic Supported Box", 260.0f, 300.0f, 40.0f, 40.0f, false);
            sceneScope.add(staticFloor);
            sceneScope.add(kinematicBox);

            gameManager.simulateFrame(kTimeStep);

            Rigidbody *kinematicBody = kinematicBox->getComponent<Rigidbody>();
            require(kinematicBody != nullptr, "Kinematic support regression lost the rigidbody.");
            require(kinematicBody->hasSupportContact(),
                    "Kinematic body failed to register support contact while resting on a static floor.");
        }
    }

    void testAnimatorComponentPlayback()
    {
        GameManager &gameManager = GameManager::getInstance();

        const std::filesystem::path frameAPath = createTemporaryBmpAsset("animator_frame_a_regression.bmp", 255, 0, 0);
        const std::filesystem::path frameBPath = createTemporaryBmpAsset("animator_frame_b_regression.bmp", 0, 255, 0);

        auto cleanupFiles = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(frameAPath, errorCode);
            std::filesystem::remove(frameBPath, errorCode);
        };

        try
        {
            SceneScope sceneScope(gameManager);
            GameObject *animatedObject = gameManager
                                             .createGameObject()
                                             ->setName("Animated Sprite")
                                             ->addComponent<Sprite>(frameAPath.string().c_str(), SDL_FLIP_NONE, 8.0f, 8.0f)
                                             ->addComponent<Animator>();
            sceneScope.add(animatedObject);

            Animator *animator = animatedObject->getComponent<Animator>();
            Sprite *sprite = animatedObject->getComponent<Sprite>();
            require(animator != nullptr, "Animator regression failed to create the animator component.");
            require(sprite != nullptr, "Animator regression failed to create the sprite component.");

            TextureWrapper *frameATexture = gameManager.loadTexture(frameAPath.string());
            TextureWrapper *frameBTexture = gameManager.loadTexture(frameBPath.string());
            require(frameATexture != nullptr && frameBTexture != nullptr,
                    "Animator regression failed to load the test frame textures.");

            const std::vector<TextureWrapper *> blinkFrames = {frameATexture, frameBTexture};

            animator->addClip(AnimationClip(blinkFrames, 20.0f, true, 8.0f, 8.0f, "blink", false));
            require(animator->getClips().size() == 1, "Animator regression failed to add the test clip.");
            require(animator->play("blink"), "Animator regression failed to start the test clip.");
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == frameAPath.string(),
                    "Animator regression failed to apply the initial clip frame.");

            simulateFrames(gameManager, 7);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == frameBPath.string(),
                    "Animator regression failed to advance to the second frame.");

            simulateFrames(gameManager, 7);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == frameAPath.string(),
                    "Animator regression failed to loop back to the first frame.");

            cleanupFiles();
        }
        catch (const std::exception &)
        {
            cleanupFiles();
            throw;
        }
    }

    void testAnimatorAdvancedPlayback()
    {
        GameManager &gameManager = GameManager::getInstance();

        const std::filesystem::path frameAPath = createTemporaryBmpAsset("animator_advanced_idle_regression.bmp", 255, 0, 0);
        const std::filesystem::path sheetPath = createTemporaryStripBmpAsset("animator_advanced_sheet_regression.bmp");

        auto cleanupFiles = [&]()
        {
            std::error_code errorCode;
            std::filesystem::remove(frameAPath, errorCode);
            std::filesystem::remove(sheetPath, errorCode);
        };

        try
        {
            SceneScope sceneScope(gameManager);
            GameObject *animatedObject = gameManager
                                             .createGameObject()
                                             ->setName("Advanced Animated Sprite")
                                             ->addComponent<Sprite>(frameAPath.string().c_str(), SDL_FLIP_NONE, 4.0f, 4.0f)
                                             ->addComponent<Animator>();
            sceneScope.add(animatedObject);

            Animator *animator = animatedObject->getComponent<Animator>();
            Sprite *sprite = animatedObject->getComponent<Sprite>();
            require(animator != nullptr, "Advanced animator regression failed to create the animator component.");
            require(sprite != nullptr, "Advanced animator regression failed to create the sprite component.");

            TextureWrapper *idleTexture = gameManager.loadTexture(frameAPath.string());
            TextureWrapper *sheetTexture = gameManager.loadTexture(sheetPath.string());
            require(idleTexture != nullptr && sheetTexture != nullptr,
                    "Advanced animator regression failed to load the clip textures.");

            const std::vector<TextureWrapper *> idleFrames = {idleTexture};

            const std::vector<AnimationFrame> stripFrames = {
                AnimationFrame(sheetTexture, SDL_FRect{0.0f, 0.0f, 4.0f, 4.0f}, 0.05f),
                AnimationFrame(sheetTexture, SDL_FRect{4.0f, 0.0f, 4.0f, 4.0f}, 0.05f)};

            animator->addClip(AnimationClip(stripFrames, 20.0f, false, 4.0f, 4.0f, "strip", false));
            animator->addClip(AnimationClip(idleFrames, 1.0f, true, 4.0f, 4.0f, "idle", false));
            require(animator->getClips().size() == 2, "Advanced animator regression failed to add the strip and idle clips.");
            require(animator->play("strip"), "Advanced animator regression failed to start one-shot playback.");

            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == sheetPath.string(),
                    "Advanced animator regression failed to apply the strip texture.");
            require(sprite->getSourceRect() != nullptr && std::abs(sprite->getSourceRect()->x - 0.0f) <= 1e-5f,
                    "Advanced animator regression failed to apply the first strip source rectangle.");

            simulateFrames(gameManager, 7);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == sheetPath.string(),
                    "Advanced animator regression unexpectedly changed the strip texture mid-clip.");
            require(sprite->getSourceRect() != nullptr && std::abs(sprite->getSourceRect()->x - 4.0f) <= 1e-5f,
                    "Advanced animator regression failed to advance to the second strip frame.");

            simulateFrames(gameManager, 7);
            require(sprite->getTextureWrapper() != nullptr && sprite->getTextureWrapper()->getFilePath() == frameAPath.string(),
                    "Advanced animator regression failed to return to the queued idle clip.");
            require(sprite->getSourceRect() == nullptr,
                    "Advanced animator regression failed to clear the sprite source rectangle when returning to a full-frame clip.");

            cleanupFiles();
        }
        catch (const std::exception &)
        {
            cleanupFiles();
            throw;
        }
    }

    void testAudioComponentControls()
    {
        GameManager &gameManager = GameManager::getInstance();
        SDLWindow *window = gameManager.getWindow();
        require(window != nullptr, "Audio regression requires an SDLWindow.");
        require(window->getMixer() != nullptr, "Audio regression requires an active mixer device.");

        const std::filesystem::path audioPath = findRegressionAudioAsset("jump.wav");
        const std::string audioPathString = audioPath.string();

        SceneScope sceneScope(gameManager);
        GameObject *audioObject = gameManager
                                      .createGameObject()
                                      ->setName("Audio Object")
                                      ->addComponent<Audio>(audioPathString.c_str());
        sceneScope.add(audioObject);

        Audio *audio = audioObject->getComponent<Audio>();
        require(audio != nullptr, "Audio regression failed to create the audio component.");
        require(audio->getAudioWrapper() != nullptr, "Audio regression failed to load the test audio asset.");

        audio->setGain(0.5f);
        require(std::abs(audio->getGain() - 0.5f) <= 1e-5f,
                "Audio regression failed to apply the configured gain.");

        require(audio->play(), "Audio regression failed to start playback.");
        require(audio->isPlaying(), "Audio regression expected the track to be playing after play().");

        require(audio->pause(), "Audio regression failed to pause playback.");
        require(audio->isPaused(), "Audio regression expected the track to be paused after pause().");

        require(audio->resume(), "Audio regression failed to resume playback.");
        require(audio->isPlaying(), "Audio regression expected the track to be playing after resume().");

        require(audio->stop(), "Audio regression failed to stop playback.");
        require(!audio->isPlaying() && !audio->isPaused(),
                "Audio regression expected the track to be idle after stop().");
    }

    void testRotatedBoxSupportEdgeSelection()
    {
        GameManager &gameManager = GameManager::getInstance();

        SceneScope sceneScope(gameManager);
        GameObject *boxObject = createStaticBox(gameManager, "Rotated Support Box", 240.0f, 180.0f, 80.0f, 40.0f);
        sceneScope.add(boxObject);

        boxObject->setRotation(0.6f);

        BoxCollider *boxCollider = boxObject->getComponent<BoxCollider>();
        require(boxCollider != nullptr, "Rotated support-edge regression lost the box collider.");

        boxCollider->applySync();

        const auto &vertices = boxCollider->getVertices();

        auto verifySupportEdge = [&](const Eigen::Vector2f &normal, const char *label)
        {
            Edge edge = boxCollider->getEdgeWithNormal(normal);

            float maxProjection = -std::numeric_limits<float>::infinity();
            for (const Eigen::Vector2f &vertex : vertices)
            {
                maxProjection = std::max(maxProjection, vertex.dot(normal));
            }

            constexpr float kProjectionTolerance = 1e-3f;
            require(std::abs(edge.v1.dot(normal) - maxProjection) <= kProjectionTolerance,
                    std::string("Rotated support-edge regression chose a non-support first vertex for ") + label + ".");
            require(std::abs(edge.v2.dot(normal) - maxProjection) <= kProjectionTolerance,
                    std::string("Rotated support-edge regression chose a non-support second vertex for ") + label + ".");
        };

        const std::vector<Eigen::Vector2f> normals = boxCollider->getNormals(boxCollider);
        require(normals.size() == 2, "Rotated support-edge regression expected two box normals.");

        verifySupportEdge(normals[0], "+x face normal");
        verifySupportEdge(-normals[0], "-x face normal");
        verifySupportEdge(normals[1], "+y face normal");
        verifySupportEdge(-normals[1], "-y face normal");
    }

    struct TestCase
    {
        const char *name;
        void (*run)();
    };
} // namespace

int main()
{
    configureHeadlessEnvironment();

    static const TestCase testCases[] = {
        {"circle_collision_stability", testCircleCollisionStability},
        {"sleeping_on_support", testSleepingOnSupport},
        {"broad_phase_pair_tracking", testBroadPhasePairTracking},
        {"checkpoint_fast_motion_cancellation", testCheckpointFastMotionCancellation},
        {"segmented_interval_fast_motion_across_chunks", testSegmentedIntervalListFastMotionAcrossChunks},
        {"scene_loading", testSceneLoading},
        {"scene_saving_round_trip", testSceneSavingRoundTrip},
        {"restitution_bounce", testRestitutionBounce},
        {"kinematic_body_semantics", testKinematicBodySemantics},
        {"animator_component_playback", testAnimatorComponentPlayback},
        {"animator_advanced_playback", testAnimatorAdvancedPlayback},
        {"audio_component_controls", testAudioComponentControls},
        {"rotated_box_support_edge_selection", testRotatedBoxSupportEdgeSelection},
    };

    GameManager::getInstance();

    size_t failureCount = 0;
    for (const TestCase &testCase : testCases)
    {
        try
        {
            testCase.run();
            std::cout << "[PASS] " << testCase.name << '\n';
        }
        catch (const std::exception &exception)
        {
            ++failureCount;
            std::cerr << "[FAIL] " << testCase.name << ": " << exception.what() << '\n';
        }
    }

    if (failureCount != 0)
    {
        std::cerr << failureCount << " regression test(s) failed.\n";
        return 1;
    }

    std::cout << "All Platformator regression tests passed.\n";
    return 0;
}